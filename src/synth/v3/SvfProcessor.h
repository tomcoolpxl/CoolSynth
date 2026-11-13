#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    V3 state-variable filter processor (DESIGN_V3.md section 3.2; REQUIREMENTS_V3.md: LP2, HP,
    BP, notch, cutoff, resonance, drive).

    Andy Simper's trapezoidal (zero-delay-feedback) state-variable filter, 12 dB per octave, with
    four taps: low-pass, high-pass, band-pass at a peak gain of 1, and notch. Resonance runs from
    Q 0.5 (no peak) to Q 40 (a long, singing ring that still decays). A soft limit on the band
    state (4 tanh(v / 4)) keeps a driven, fully resonant filter bounded without touching normal
    levels. Drive feeds a peak-normalised tanh stage before the filter, as on the ladder. Key
    tracking scales the cutoff with the note around C4. Cutoff and resonance ramp per sample.
*/
namespace coolsynth::synth::v3
{
    class SvfProcessor final
    {
    public:
        enum Param : int { cutoff = 0, resonance, drive, keyTrack, mode, paramCount };
        enum class Mode : int { lowpass = 0, highpass, bandpass, notch };
        static constexpr float keyReferenceHz = 261.6256f;
        static constexpr float minQ = 0.5f;
        static constexpr float maxQ = 40.0f;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "SVF", 1,
                {{ { "cutoff", "Cutoff",    20.0f, 20000.0f, 1000.0f, Skew::logarithmic, Unit::hertz,    true },
                   { "res",    "Resonance", 0.0f,  1.0f,     0.1f,    Skew::linear,      Unit::percent,  true },
                   { "drive",  "Drive",     0.0f,  24.0f,    0.0f,    Skew::linear,      Unit::decibels, true },
                   { "key",    "Key Track", 0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false },
                   { "mode",   "Mode",      0.0f,  3.0f,     0.0f,    Skew::linear,      Unit::choice,   false, "LP|HP|BP|Notch" } }},
                paramCount };
            return d;
        }

        /** Resonance 0..1 to the filter's damping k = 1 / Q, Q growing exponentially from 0.5 to 40. */
        static float dampingFor(float resonanceNormalised) noexcept
        {
            const float r = juce::jlimit(0.0f, 1.0f, resonanceNormalised);
            return 1.0f / (minQ * std::pow(maxQ / minQ, r));
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            maxCutoffHz = std::min(24000.0f, 0.45f * sampleRate);
            reset();
        }

        void reset() noexcept
        {
            left = {};
            right = {};
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* leftSamples, float* rightSamples, int numSamples, const SlotParams& params) noexcept
        {
            const float key = juce::jlimit(0.0f, 1.0f, params.p[keyTrack]);
            float hz = params.p[cutoff] * std::pow(std::max(1.0f, params.frequencyHz) / keyReferenceHz, key);
            hz = juce::jlimit(10.0f, maxCutoffHz, hz);
            const float gTarget = std::tan(juce::MathConstants<float>::pi * hz / sampleRate);
            const float kTarget = dampingFor(params.p[resonance]);
            const auto tap = static_cast<Mode>(juce::jlimit(0, 3, static_cast<int>(std::lround(params.p[mode]))));
            const float driveDb = juce::jlimit(0.0f, 24.0f, params.p[drive]);
            const bool driven = driveDb > 0.0f;
            const float driveGain = kDriveHeadroom * std::pow(10.0f, driveDb * 0.05f);
            const float driveNormalise = 1.0f / dsp::fastTanh(driveGain);

            if (! primed)
            {
                g = gTarget;
                k = kTarget;
                primed = true;
            }
            const float inverse = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float gStep = (gTarget - g) * inverse;
            const float kStep = (kTarget - k) * inverse;

            for (int i = 0; i < numSamples; ++i)
            {
                g += gStep;
                k += kStep;
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                float inL = leftSamples[i], inR = rightSamples[i];
                if (driven)
                {
                    inL = dsp::fastTanh(inL * driveGain) * driveNormalise;
                    inR = dsp::fastTanh(inR * driveGain) * driveNormalise;
                }
                leftSamples[i] = tick(left, inL, g, a1, a2, k, tap);
                rightSamples[i] = tick(right, inR, g, a1, a2, k, tap);
            }
            g = gTarget;
            k = kTarget;
        }

    private:
        static constexpr float kDriveHeadroom = 0.25f;

        struct Channel
        {
            float ic1 = 0.0f, ic2 = 0.0f;
        };

        static float tick(Channel& c, float x, float g, float a1, float a2, float k, Mode tap) noexcept
        {
            const float v3 = x - c.ic2;
            float v1 = a1 * c.ic1 + a2 * v3;
            v1 = 4.0f * dsp::fastTanh(0.25f * v1); // the band state's soft limit
            const float v2 = c.ic2 + g * v1;       // the low-pass integrates the limited band
            c.ic1 = 2.0f * v1 - c.ic1;
            c.ic2 = 2.0f * v2 - c.ic2;
            switch (tap)
            {
                case Mode::lowpass:  return v2;
                case Mode::highpass: return x - k * v1 - v2;
                case Mode::bandpass: return k * v1;
                case Mode::notch:    return x - k * v1;
            }
            return v2;
        }

        float sampleRate = 96000.0f;
        float maxCutoffHz = 24000.0f;
        float g = 0.0f, k = 1.0f;
        bool primed = false;
        Channel left, right;
    };

    static_assert(Module<SvfProcessor>);
}
