#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 virtual-analogue generator (DESIGN_V3.md section 3.1).

    The V2 PolyBLEP oscillator as a slot module: pulse, triangle, saw and sine, pulse width,
    unison of 1 to 7 detuned copies with stereo spread and 1/sqrt(N) level compensation, a sine
    sub one octave down, and the combine modes with the previous generator: hard sync from its
    wrap track (PolyBLEP-corrected reset as in V2), ring, AM and phase-modulation FM from its
    output, which the voice leaves in the buffer this generator overwrites.
*/
namespace coolsynth::synth::v3
{
    class VaGenerator final
    {
    public:
        enum class Wave : int { pulse = 0, triangle, saw, sine };

        static constexpr int maxUnison = 7;

        enum Param : int { wave = 0, pulseWidth, unison, detune, spread, sub, fm, paramCount };

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "VA", 1,
                {{ { "wave",    "Wave",       0.0f,  3.0f,   2.0f, Skew::linear, Unit::choice,  false },
                   { "pw",      "Pulse Width", 0.05f, 0.95f, 0.5f, Skew::linear, Unit::percent, true },
                   { "unison",  "Unison",     1.0f,  7.0f,   1.0f, Skew::linear, Unit::choice,  false },
                   { "detune",  "Detune",     0.0f,  100.0f, 12.0f, Skew::linear, Unit::cents,  true },
                   { "spread",  "Spread",     0.0f,  1.0f,   0.5f, Skew::linear, Unit::percent, true },
                   { "sub",     "Sub",        0.0f,  1.0f,   0.0f, Skew::linear, Unit::percent, true },
                   { "fm",      "FM",         0.0f,  1.0f,   0.0f, Skew::linear, Unit::percent, true } }},
                paramCount };
            return d;
        }

        /** One float per sample for the wrap track handed to the next generator. */
        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = static_cast<float>(newSampleRate);
            wrapTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            reset();
        }

        void reset() noexcept
        {
            phases.fill(0.0f);
            subPhase = 0.0f;
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            syncInput = nullptr;
        }

        /** Unison copies start evenly spread in phase so a detuned stack does not comb-filter at the onset. */
        void noteOn(const NoteContext&) noexcept
        {
            for (int i = 0; i < maxUnison; ++i)
                phases[static_cast<std::size_t>(i)] = static_cast<float>(i) / static_cast<float>(maxUnison);
            subPhase = 0.0f;
        }

        /** The previous generator's wrap track for Combine::sync, or null. Valid for the next process() call. */
        void setSyncInput(const float* track) noexcept { syncInput = track; }

        /** This generator's wrap track from the last process() call: wrap fraction in 0..1, or -1 for no wrap. */
        [[nodiscard]] std::span<const float> syncTrack() const noexcept { return wrapTrack; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const auto shape = static_cast<Wave>(juce::jlimit(0, 3, static_cast<int>(std::lround(params.p[wave]))));
            const float pw = juce::jlimit(0.05f, 0.95f, params.p[pulseWidth]);
            const int count = juce::jlimit(1, maxUnison, static_cast<int>(std::lround(params.p[unison])));
            const float detuneCents = params.p[detune];
            const float spreadAmount = juce::jlimit(0.0f, 1.0f, params.p[spread]);
            const float subMix = juce::jlimit(0.0f, 1.0f, params.p[sub]);
            const float fmDepth = juce::jlimit(0.0f, 1.0f, params.p[fm]) * 0.5f;
            const float level = juce::jlimit(0.0f, 1.0f, params.level);
            const float unisonGain = level / std::sqrt(static_cast<float>(count));
            const bool useSync = params.combine == Combine::sync && syncInput != nullptr;
            const bool useRing = params.combine == Combine::ring;
            const bool useAm = params.combine == Combine::am;
            const bool useFm = params.combine == Combine::fm && fmDepth > 0.0f;

            // Per-copy increments and constant-power pan gains for this sub-block.
            std::array<float, maxUnison> increments {}, gainLeft {}, gainRight {};
            for (int i = 0; i < count; ++i)
            {
                const float position = count > 1 ? (static_cast<float>(i) / static_cast<float>(count - 1)) * 2.0f - 1.0f : 0.0f;
                const float cents = detuneCents * position;
                const float frequency = params.frequencyHz * std::exp2(cents / 1200.0f);
                increments[static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 0.5f, frequency / sampleRate);

                const float pan = juce::jlimit(-1.0f, 1.0f, params.pan + spreadAmount * position);
                const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                gainLeft[static_cast<std::size_t>(i)] = std::cos(angle);
                gainRight[static_cast<std::size_t>(i)] = std::sin(angle);
            }
            const float subIncrement = juce::jlimit(0.0f, 0.5f, 0.5f * params.frequencyHz / sampleRate);
            const float slotAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float subLeft = std::cos(slotAngle) * subMix * level;
            const float subRight = std::sin(slotAngle) * subMix * level;

            for (int s = 0; s < numSamples; ++s)
            {
                const float modulator = left[s]; // previous generator's output, if any
                const float syncFraction = useSync ? syncInput[s] : -1.0f;
                const bool resetNow = useSync && syncFraction >= 0.0f;

                float outLeft = 0.0f, outRight = 0.0f;
                for (int i = 0; i < count; ++i)
                {
                    const auto index = static_cast<std::size_t>(i);
                    const float increment = increments[index];
                    float& phase = phases[index];

                    if (i == 0)
                    {
                        const bool wraps = phase + increment >= 1.0f;
                        wrapTrack[static_cast<std::size_t>(s)] = (wraps && increment > 0.0f) ? (phase + increment - 1.0f) / increment : -1.0f;
                    }

                    const float phaseBefore = phase;
                    if (resetNow)
                        phase = 0.0f;

                    float renderPhase = phase;
                    if (useFm)
                    {
                        renderPhase += modulator * fmDepth;
                        renderPhase -= std::floor(renderPhase);
                    }

                    float sample = bandlimited(renderPhase, increment, shape, pw);
                    if (resetNow && increment > 0.0f)
                        sample += polyBlep(syncFraction, increment) * (naive(0.0f, shape, pw) - naive(phaseBefore, shape, pw));

                    phase += increment;
                    phase -= std::floor(phase);

                    outLeft += sample * gainLeft[index];
                    outRight += sample * gainRight[index];
                }

                outLeft *= unisonGain;
                outRight *= unisonGain;

                if (useRing)
                {
                    outLeft *= modulator;
                    outRight *= modulator;
                }
                else if (useAm)
                {
                    const float am = 0.5f + 0.5f * modulator;
                    outLeft *= am;
                    outRight *= am;
                }

                if (subMix > 0.0f)
                {
                    const float subSample = std::sin(subPhase * juce::MathConstants<float>::twoPi);
                    outLeft += subSample * subLeft;
                    outRight += subSample * subRight;
                }
                subPhase += subIncrement;
                subPhase -= std::floor(subPhase);

                left[s] = outLeft;
                right[s] = outRight;
            }
        }

    private:
        // Standard 2-point PolyBLEP correction. t in [0,1), dt = phase increment per sample.
        static float polyBlep(float t, float dt) noexcept
        {
            if (dt <= 0.0f)
                return 0.0f;
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0f;
            }
            if (t > 1.0f - dt)
            {
                t = (t - 1.0f) / dt;
                return t * t + t + t + 1.0f;
            }
            return 0.0f;
        }

        static float naive(float phase, Wave shape, float pw) noexcept
        {
            switch (shape)
            {
                case Wave::saw:      return 2.0f * phase - 1.0f;
                case Wave::pulse:    return phase < pw ? (1.0f - pw) : -pw;
                case Wave::triangle: return phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
                case Wave::sine:     break;
            }
            return std::sin(phase * juce::MathConstants<float>::twoPi);
        }

        // Saw and pulse get PolyBLEP steps; the triangle stays naive (1/n^2 harmonics, voice runs at 2x).
        static float bandlimited(float phase, float dt, Wave shape, float pw) noexcept
        {
            switch (shape)
            {
                case Wave::saw:
                    return 2.0f * phase - 1.0f - polyBlep(phase, dt);
                case Wave::pulse:
                {
                    float y = phase < pw ? (1.0f - pw) : -pw;
                    y += polyBlep(phase, dt);
                    y -= polyBlep(std::fmod(phase - pw + 1.0f, 1.0f), dt);
                    return y;
                }
                case Wave::triangle:
                case Wave::sine:
                    break;
            }
            return naive(phase, shape, pw);
        }

        float sampleRate = 48000.0f;
        std::array<float, maxUnison> phases {};
        float subPhase = 0.0f;
        std::span<float> wrapTrack;
        const float* syncInput = nullptr;
    };

    static_assert(Module<VaGenerator>);
}
