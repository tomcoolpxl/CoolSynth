#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 plucked-string generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md Karplus-Strong:
    excitation, pick position, damping, string decay, release damping, a loop filter that tracks
    pitch without going sharp). After Karplus and Strong, and Jaffe and Smith, "Extensions of the
    Karplus-Strong Plucked-String Algorithm", Computer Music Journal 7(2), 1983.

    A delay line one period long feeds back through a one-pole low-pass (Damping) and a gain set
    from Decay, the time the string takes to fall 60 dB. The line's length is the period minus
    the low-pass's phase delay at the fundamental, so the string stays in tune however dark the
    damping; the read is a 4-point Hermite one and the length ramps per sample, so bends, glides
    and vibrato do not click.

    A pluck injects one period of excitation into the loop: noise through a low-pass whose cutoff
    is counted in harmonics of the note (Pick: 1.5 harmonics, soft, to 40, bright), or a short
    click. Pick Position is a comb on the output, the same filter as
    plucking the string at that share of its length: at 0.5 the even harmonics vanish. When the
    key goes up, Release shortens the decay (0 lets it ring, 1 stops it in 30 ms), the damper of
    a harp or a hand on the strings. A retriggered note plucks the ringing string again instead
    of replacing it. Output is mono, panned, DC-blocked, two samples late like every generator.
*/
namespace coolsynth::synth::v3
{
    class StringGenerator final
    {
    public:
        enum Param : int { decay = 0, damping, pick, position, release, excitation, paramCount };
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr float lowestHz = 20.0f;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "String", 1,
                {{ { "decay",   "Decay",         0.1f,  20.0f, 4.0f, Skew::logarithmic, Unit::generic, true },
                   { "damp",    "Damping",       0.0f,  1.0f,  0.3f, Skew::linear,      Unit::percent, true },
                   { "pick",    "Pick",          0.0f,  1.0f,  0.7f, Skew::linear,      Unit::percent, true },
                   { "pos",     "Pick Position", 0.02f, 0.5f,  0.2f, Skew::linear,      Unit::percent, true },
                   { "release", "Release Damp",  0.0f,  1.0f,  0.6f, Skew::linear,      Unit::percent, true },
                   { "excite",  "Excitation",    0.0f,  1.0f,  0.0f, Skew::linear,      Unit::choice,  false, "Noise|Click" } }},
                paramCount };
            return d;
        }

        static int lineLength(double sampleRate) noexcept { return static_cast<int>(std::ceil(sampleRate / lowestHz)) + 8; }

        static std::size_t memoryFloats(double sampleRate, int maxSubBlock)
        {
            return static_cast<std::size_t>(lineLength(sampleRate)) + static_cast<std::size_t>(std::max(1, maxSubBlock));
        }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            length = lineLength(sampleRate);
            line = memory.take(static_cast<std::size_t>(length));
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            dcCoefficient = static_cast<float>(1.0 - 2.0 * juce::MathConstants<double>::pi * 15.0 / sampleRate);
            static std::atomic<std::uint32_t> instances { 0 };
            rng = (instances.fetch_add(1, std::memory_order_relaxed) + 1u) * 2654435761u ^ 0x7F4A7C15u;
            if (rng == 0)
                rng = 1;
            reset();
        }

        void reset() noexcept
        {
            std::fill(line.begin(), line.end(), 0.0f);
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            write = 0;
            loopState = exciteState = dcIn = dcOut = 0.0f;
            exciteRemaining = exciteTotal = 0;
            strikePending = false;
            gateOpen = false;
            primed = false;
            ring.fill(0.0f);
            cursor = 0;
            input = {};
        }

        void noteOn(const NoteContext&) noexcept
        {
            strikePending = true;
            gateOpen = true;
        }

        /** The same voice again: pluck the ringing string. */
        void strike() noexcept
        {
            strikePending = true;
            gateOpen = true;
        }

        void noteOff() noexcept { gateOpen = false; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        /** The loop low-pass's phase delay in samples at a frequency, for a coefficient a. */
        [[nodiscard]] static double loopFilterDelay(double a, double omega) noexcept
        {
            return omega > 0.0 ? std::atan2(a * std::sin(omega), 1.0 - a * std::cos(omega)) / omega : a / (1.0 - a);
        }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            if (line.size() < static_cast<std::size_t>(length) || length < 16)
            {
                std::fill_n(left, numSamples, 0.0f);
                std::fill_n(right, numSamples, 0.0f);
                return;
            }

            const double hz = juce::jlimit(static_cast<double>(lowestHz), 0.2 * sampleRate, static_cast<double>(params.frequencyHz));
            const double period = sampleRate / hz;
            const double omega = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
            const float a = 0.9f * juce::jlimit(0.0f, 1.0f, params.p[damping]);
            const double targetDelay = juce::jlimit(2.0, static_cast<double>(length - 4), period - loopFilterDelay(a, omega));

            double t60 = juce::jlimit(0.1, 20.0, static_cast<double>(params.p[decay]));
            if (! gateOpen)
            {
                const double releaseAmount = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[release]));
                if (releaseAmount > 0.0)
                    t60 = std::min(t60, 3.0 * std::pow(0.01, releaseAmount)); // 3 s down to 30 ms
            }
            const float loopGain = static_cast<float>(std::pow(10.0, -3.0 / (hz * t60)));

            // The pluck's brightness is a cutoff counted in harmonics of the note (1.5 soft to 40 bright),
            // so its energy sits where the string can ring, not in an ultrasonic tick that dies at once.
            const float pickAmount = juce::jlimit(0.0f, 1.0f, params.p[pick]);
            const double pickHz = std::min(18000.0, hz * (1.5 + 38.5 * static_cast<double>(pickAmount * pickAmount)));
            const float exciteCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * pickHz / sampleRate));
            const bool click = params.p[excitation] >= 0.5f;
            // At least two samples back, so the 4-point read stays behind the write position.
            const double tapOffset = std::max(2.0, juce::jlimit(0.02, 0.5, static_cast<double>(params.p[position])) * period);

            if (strikePending)
            {
                exciteTotal = std::max(8, static_cast<int>(std::ceil(period)));
                exciteRemaining = exciteTotal;
                strikePending = false;
            }
            if (! primed)
            {
                delay = targetDelay;
                primed = true;
            }
            const double delayStep = (targetDelay - delay) / static_cast<double>(std::max(1, numSamples));
            // Filtered noise loses level as the filter closes: bring it back, so Pick changes colour, not loudness.
            const float exciteMakeup = std::sqrt((2.0f - exciteCoefficient) / exciteCoefficient);

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                delay += delayStep;

                float excite = 0.0f;
                if (exciteRemaining > 0)
                {
                    const int index = exciteTotal - exciteRemaining;
                    if (click)
                    {
                        // A raised-cosine pulse over the first eighth of the period (narrower when Pick is bright).
                        const float width = std::max(4.0f, static_cast<float>(exciteTotal) * (0.25f - 0.2f * pickAmount));
                        const float x = static_cast<float>(index) / width;
                        // Twice full scale: a narrow pulse carries far less energy than a period of noise.
                        excite = x < 1.0f ? 2.0f * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * x)) : 0.0f;
                    }
                    else
                    {
                        exciteState += (nextRandom() - exciteState) * exciteCoefficient;
                        excite = 0.6f * exciteMakeup * exciteState;
                    }
                    --exciteRemaining;
                }

                const float delayed = read(delay);
                loopState = (1.0f - a) * delayed + a * loopState;
                const float in = excite + loopGain * loopState;
                line[static_cast<std::size_t>(write)] = std::abs(in) < 1.0e-20f ? 0.0f : in;

                // Pick position: the string against itself that share of a period earlier.
                const float tapped = 0.6f * (in - read(tapOffset));
                write = (write + 1) % length;

                const float blocked = tapped - dcIn + dcCoefficient * dcOut;
                dcIn = tapped;
                dcOut = blocked;

                const unsigned int now = cursor & 3u;
                ring[now] = blocked;
                float out = ring[(cursor + 2u) & 3u];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    out *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = out * gainLeft;
                right[s] = out * gainRight;
                signalTrack[static_cast<std::size_t>(s)] = out;
            }
        }

    private:
        /** The line's content `delaySamples` before the write position, 4-point Hermite. */
        float read(double delaySamples) const noexcept
        {
            const double position = static_cast<double>(write) - delaySamples;
            const double f = std::floor(position);
            const float t = static_cast<float>(position - f);
            const int base = static_cast<int>(f);
            auto at = [&](int index)
            {
                index %= length;
                if (index < 0)
                    index += length;
                return line[static_cast<std::size_t>(index)];
            };
            const float xm1 = at(base - 1), x0 = at(base), x1 = at(base + 1), x2 = at(base + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        float nextRandom() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        double sampleRate = 96000.0;
        int length = 0;
        int write = 0;
        double delay = 100.0;
        bool primed = false;
        bool strikePending = false;
        bool gateOpen = false;
        int exciteRemaining = 0, exciteTotal = 0;
        float loopState = 0.0f, exciteState = 0.0f;
        float dcCoefficient = 0.999f, dcIn = 0.0f, dcOut = 0.0f;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1;
        std::span<float> line, signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<StringGenerator>);
}
