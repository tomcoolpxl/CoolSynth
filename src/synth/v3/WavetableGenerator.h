#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "UserLibrary.h"
#include "Wavetables.h"

/*
    V3 wavetable generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: a factory table set,
    position with crossfade morph, band-limited playback, phase randomisation).

    Plays one of the factory tables (Wavetables.h), or the user's own table when the slot names a
    file (UserLibrary.h, decision D137): silent while that file loads. Position runs through the
    table's frames (16 in a factory table, 1 to 256 in the user's),
    crossfading between neighbours, and ramps across each sub-block so a modulated position
    never steps. Each copy reads the frame at the band limit its frequency needs
    (WavetableBank::levelFor) with a 4-point Hermite read; the limits change only above 36 kHz,
    so switching between them is never heard after the voice's decimation.

    Unison 1 to 5 copies, detuned evenly over the Detune span, spread across the stereo field
    and power-normalised. Phase Rand draws each copy's start phase at a fresh note. The output
    runs generatorLatencySamples late like every generator and hands its mono signal and the
    wrap track of an undetuned reference onward. Combine FM moves the phase of every copy; ring
    and AM work as on the VA.
*/
namespace coolsynth::synth::v3
{
    class WavetableGenerator final
    {
    public:
        enum Param : int { table = 0, position, unison, detune, spread, randomPhase, paramCount };
        static constexpr int maxUnison = 5;
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double externalFmCycles = 0.6; // phase deviation of combine FM at full amount

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Wavetable", 2,
                {{ { "table",  "Table",      0.0f, 3.0f,   0.0f, Skew::linear,  Unit::choice,  false, "Classic|Digital|Vocal|Chip" },
                   { "pos",    "Position",   0.0f, 1.0f,   0.0f, Skew::linear,  Unit::percent, true },
                   { "unison", "Unison",     1.0f, 5.0f,   1.0f, Skew::linear,  Unit::choice,  false },
                   { "detune", "Detune",     0.0f, 100.0f, 15.0f, Skew::squared, Unit::cents,  true },
                   { "spread", "Spread",     0.0f, 1.0f,   0.5f, Skew::linear,  Unit::percent, true },
                   { "rand",   "Phase Rand", 0.0f, 1.0f,   1.0f, Skew::linear,  Unit::percent, false } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return 2 * static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &WavetableBank::instance();
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            wrapTrack = memory.take(n);
            static std::atomic<std::uint32_t> instances { 0 };
            rng = (instances.fetch_add(1, std::memory_order_relaxed) + 1u) * 2654435761u ^ 0x9E3779B9u;
            if (rng == 0)
                rng = 1;
            reset();
        }

        void reset() noexcept
        {
            phases.fill(0.0);
            referencePhase = 0.0;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            ringMono.fill(0.0f);
            ringWrap.fill(-1.0f);
            cursor = 0;
            positionPrimed = false;
            phasesPending = false;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            input = {};
        }

        void noteOn(const NoteContext&) noexcept { phasesPending = true; }
        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), wrapTrack.data() }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const int tableIndex = juce::jlimit(0, WavetableBank::tableCount - 1, static_cast<int>(std::lround(params.p[table])));
            // The user's own table when the slot names one; nothing while it loads.
            const UserWavetable* user = params.userSound >= 0 ? UserLibrary::instance().wavetable(params.userSound) : nullptr;
            const bool silent = params.userSound >= 0 && user == nullptr;
            const int frameCount = user != nullptr ? user->frameCount : WavetableBank::frames;
            auto frameAt = [&](int frameIndex, int level) noexcept
            {
                return user != nullptr ? user->frame(frameIndex, level) : bank->frame(tableIndex, frameIndex, level);
            };
            const int count = juce::jlimit(1, maxUnison, static_cast<int>(std::lround(params.p[unison])));
            const double detuneCents = juce::jlimit(0.0, 100.0, static_cast<double>(params.p[detune]));
            const float spreadAmount = juce::jlimit(0.0f, 1.0f, params.p[spread]);
            const double note = std::max(0.0, static_cast<double>(params.frequencyHz));

            if (phasesPending)
            {
                const double amount = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[randomPhase]));
                for (int i = 0; i < count; ++i)
                    phases[static_cast<std::size_t>(i)] = amount * nextUnit();
                phasesPending = false;
            }

            const float targetPosition = juce::jlimit(0.0f, 1.0f, params.p[position]);
            if (! positionPrimed)
            {
                currentPosition = targetPosition;
                positionPrimed = true;
            }
            const float positionStep = (targetPosition - currentPosition) / static_cast<float>(std::max(1, numSamples));

            std::array<double, maxUnison> steps {};
            std::array<float, maxUnison> gainsLeft {}, gainsRight {};
            std::array<int, maxUnison> bandLevels {};
            const float norm = 1.0f / std::sqrt(static_cast<float>(count));
            for (int i = 0; i < count; ++i)
            {
                const auto ui = static_cast<std::size_t>(i);
                const double u = count > 1 ? -1.0 + 2.0 * i / (count - 1) : 0.0;
                const double hz = std::min(0.45 * sampleRate, note * std::exp2(0.5 * detuneCents * u / 1200.0));
                steps[ui] = hz / sampleRate;
                bandLevels[ui] = WavetableBank::levelFor(hz, sampleRate);
                const float pan = juce::jlimit(-1.0f, 1.0f, params.pan + spreadAmount * static_cast<float>(u));
                const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                gainsLeft[ui] = std::cos(angle) * norm;
                gainsRight[ui] = std::sin(angle) * norm;
            }
            const double referenceStep = std::min(0.45 * sampleRate, note) / sampleRate;

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useFm = params.combine == Combine::fm && input.signal != nullptr && amount > 0.0f;
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            const double fmDepth = externalFmCycles * static_cast<double>(amount * amount);

            for (int s = 0; s < numSamples; ++s)
            {
                currentPosition += positionStep;
                const float framePosition = juce::jlimit(0.0f, 1.0f, currentPosition) * static_cast<float>(frameCount - 1);
                const int frameA = std::max(0, std::min(frameCount - 2, static_cast<int>(framePosition)));
                const float blend = frameCount > 1 ? framePosition - static_cast<float>(frameA) : 0.0f;
                const double phaseOffset = useFm ? fmDepth * static_cast<double>(input.signal[s]) : 0.0;

                float sumLeft = 0.0f, sumRight = 0.0f, sumMono = 0.0f;
                for (int i = 0; i < count; ++i)
                {
                    const auto ui = static_cast<std::size_t>(i);
                    double readPhase = phases[ui] + phaseOffset;
                    readPhase -= std::floor(readPhase);
                    float v = 0.0f;
                    if (! silent)
                    {
                        v = hermite(frameAt(frameA, bandLevels[ui]), readPhase);
                        if (blend > 0.0f)
                            v += (hermite(frameAt(frameA + 1, bandLevels[ui]), readPhase) - v) * blend;
                    }
                    sumLeft += v * gainsLeft[ui];
                    sumRight += v * gainsRight[ui];
                    sumMono += v * norm;
                    phases[ui] += steps[ui];
                    phases[ui] -= std::floor(phases[ui]);
                }

                float wrapTau = -1.0f;
                const double next = referencePhase + referenceStep;
                if (next >= 1.0 && referenceStep > 0.0)
                    wrapTau = static_cast<float>(juce::jlimit(1.0e-6, 1.0, (1.0 - referencePhase) / referenceStep));
                referencePhase = next - std::floor(next);

                const unsigned int now = cursor & 3u;
                ringLeft[now] = sumLeft;
                ringRight[now] = sumRight;
                ringMono[now] = sumMono;
                ringWrap[now] = wrapTau;
                const unsigned int delayed = (cursor + 2u) & 3u;
                float outLeft = ringLeft[delayed], outRight = ringRight[delayed], outMono = ringMono[delayed];
                const float outWrap = ringWrap[delayed];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    const float k = useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }
                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[static_cast<std::size_t>(s)] = outMono;
                wrapTrack[static_cast<std::size_t>(s)] = outWrap;
            }
        }

    private:
        /** 4-point, 3rd-order Hermite read of a guarded frame at a phase in 0..1. */
        static float hermite(const float* frame, double phase) noexcept
        {
            const double x = phase * WavetableBank::tableSize;
            const int i = std::min(WavetableBank::tableSize - 1, static_cast<int>(x));
            const float t = static_cast<float>(x - i);
            const float xm1 = frame[i - 1], x0 = frame[i], x1 = frame[i + 1], x2 = frame[i + 2];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        double nextUnit() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<double>(rng >> 8) / 16777216.0;
        }

        const WavetableBank* bank = nullptr;
        double sampleRate = 96000.0;
        std::array<double, maxUnison> phases {};
        double referencePhase = 0.0;
        float currentPosition = 0.0f;
        bool positionPrimed = false;
        bool phasesPending = false;
        std::uint32_t rng = 1;
        std::array<float, 4> ringLeft {}, ringRight {}, ringMono {}, ringWrap {};
        unsigned int cursor = 0;
        std::span<float> signalTrack, wrapTrack;
        GeneratorLink input {};
    };

    static_assert(Module<WavetableGenerator>);
}
