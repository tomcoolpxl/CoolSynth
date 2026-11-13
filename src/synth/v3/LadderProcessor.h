#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    V3 ladder processor (DESIGN_V3.md section 3.2).

    A transistor ladder with a nonlinearity in every stage, not only at the input: each of the
    four one-pole stages integrates tanh(previous) - tanh(own), as the differential pairs of the
    Moog ladder do (Huovilainen 2004), and the input pair sees tanh(input - k * output). The loop
    is solved without a unit delay by linearising every tanh around its last operating point
    (tanh(v) ~ v * tanh(v0) / v0, the "cheap nonlinear zero-delay filter" method) and solving the
    resulting linear ladder exactly: one rational tanh per stage per sample, no iteration.
    Self-oscillation settles at the level the transistors allow, and a -110 dB noise floor lets it
    start from silence as the analogue circuit does.

    Before the ladder, Drive feeds a peak-normalised tanh stage that is nearly clean at 0 dB.
    Bass restores the low end the feedback takes away, at the input, so self-oscillation is not
    boosted with it. Mode taps the stages: 24 and 12 dB low-pass, band-pass and 12 dB high-pass,
    each with the resonance of the full loop. Cutoff and resonance ramp per sample.
*/
namespace coolsynth::synth::v3
{
    class LadderProcessor final
    {
    public:
        enum Param : int { cutoff = 0, resonance, drive, keyTrack, mode, bass, paramCount };
        enum class Mode : int { lowpass24 = 0, lowpass12, bandpass, highpass12 };

        static constexpr float keyReferenceHz = 261.6256f; // C4: key tracking is neutral here
        static constexpr float oscillationResonance = 0.9f; // feedback reaches 4, the oscillation threshold, here
        static constexpr float maxFeedback = 5.0f;          // at full resonance: a strong, stable sine

        /**
            Resonance to loop feedback. Up to the threshold, k = 4 sqrt(r / 0.9), so the audible peak
            grows evenly across the travel; the last tenth drives the loop past it, so the ladder
            sings from the noise floor and the transistors set its level.
        */
        static float feedbackFor(float resonanceNormalised) noexcept
        {
            const float r = juce::jlimit(0.0f, 1.0f, resonanceNormalised);
            if (r < oscillationResonance)
                return 4.0f * std::sqrt(r / oscillationResonance);
            return 4.0f + (maxFeedback - 4.0f) * (r - oscillationResonance) / (1.0f - oscillationResonance);
        }

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Ladder", 3,
                {{ { "cutoff", "Cutoff",    20.0f, 20000.0f, 3200.0f, Skew::logarithmic, Unit::hertz,    true },
                   { "res",    "Resonance", 0.0f,  1.0f,     0.1f,    Skew::linear,      Unit::percent,  true },
                   { "drive",  "Drive",     0.0f,  24.0f,    0.0f,    Skew::linear,      Unit::decibels, true },
                   { "key",    "Key Track", 0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false },
                   { "mode",   "Mode",      0.0f,  3.0f,     0.0f,    Skew::linear,      Unit::choice,   false },
                   { "bass",   "Bass",      0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            // Above 24 kHz the filter is inaudible; stopping there keeps the solve well away from Nyquist.
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
            const float kTarget = feedbackFor(params.p[resonance]);

            // Past the threshold the saturating stages slow the loop down and the oscillation sinks
            // flat, about 4 % per unit of excess feedback; raise the cutoff by as much to keep it in tune.
            const float tuning = 1.0f + kOscillationTuning * std::max(0.0f, kTarget - 4.0f);
            float hz = params.p[cutoff] * tuning * std::pow(std::max(1.0f, params.frequencyHz) / keyReferenceHz, key);
            hz = juce::jlimit(10.0f, maxCutoffHz, hz);

            const float gTarget = std::tan(juce::MathConstants<float>::pi * hz / sampleRate);
            const auto tap = static_cast<Mode>(juce::jlimit(0, 3, static_cast<int>(std::lround(params.p[mode]))));
            // Only the low-pass taps lose low end to the feedback; band- and high-pass have none to restore.
            const bool lowpass = tap == Mode::lowpass24 || tap == Mode::lowpass12;
            const float bassAmount = lowpass ? juce::jlimit(0.0f, 1.0f, params.p[bass]) : 0.0f;
            const float driveGain = kDriveHeadroom * std::pow(10.0f, juce::jlimit(0.0f, 24.0f, params.p[drive]) * 0.05f);
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
                const float inputGain = (1.0f + bassAmount * k) * driveNormalise;
                const float floor = kNoiseFloor * nextNoise();
                leftSamples[i] = tick(left, dsp::fastTanh(leftSamples[i] * driveGain) * inputGain + floor, tap);
                rightSamples[i] = tick(right, dsp::fastTanh(rightSamples[i] * driveGain) * inputGain + floor, tap);
            }

            g = gTarget;
            k = kTarget;
        }

    private:
        static constexpr float kDriveHeadroom = 0.25f; // drive 0 dB: a full-scale input bends by under 2 %
        static constexpr float kNoiseFloor = 3.0e-6f;  // about -110 dB
        static constexpr float kOscillationTuning = 0.041f;

        struct Channel
        {
            std::array<float, 4> s {}; // trapezoidal integrator states
            std::array<float, 4> y {}; // last stage outputs: the operating points of the linearisation
        };

        struct Solution
        {
            float u = 0.0f, y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        };

        /** The linear ladder with every pair's transconductance fixed at its value for the given operating point. */
        Solution solve(const Channel& c, float x, float input, float o1, float o2, float o3, float o4) const noexcept
        {
            using dsp::tanhOverX;
            const float t0 = tanhOverX(input);
            const float t1 = tanhOverX(o1);
            const float t2 = tanhOverX(o2);
            const float t3 = tanhOverX(o3);
            const float t4 = tanhOverX(o4);

            // Stage i: y_i = g (t_(i-1) y_(i-1) - t_i y_i) + s_i, so y_i = a_i y_(i-1) + b_i.
            const float d1 = 1.0f / (1.0f + g * t1);
            const float d2 = 1.0f / (1.0f + g * t2);
            const float d3 = 1.0f / (1.0f + g * t3);
            const float d4 = 1.0f / (1.0f + g * t4);
            const float a1 = g * t0 * d1;
            const float a2 = g * t1 * d2;
            const float a3 = g * t2 * d3;
            const float a4 = g * t3 * d4;
            const float b1 = c.s[0] * d1;
            const float b2 = c.s[1] * d2;
            const float b3 = c.s[2] * d3;
            const float b4 = c.s[3] * d4;

            // The four stages in series give y4 = A u + B with u = x - k y4: solve the loop at once.
            const float a = a1 * a2 * a3 * a4;
            const float b = a4 * (a3 * (a2 * b1 + b2) + b3) + b4;
            Solution s;
            s.y4 = (a * x + b) / (1.0f + k * a);
            s.u = x - k * s.y4;
            s.y1 = a1 * s.u + b1;
            s.y2 = a2 * s.y1 + b2;
            s.y3 = a3 * s.y2 + b3;
            return s;
        }

        float tick(Channel& c, float x, Mode tap) const noexcept
        {
            // First pass around the last sample's operating point, second pass around the first
            // pass's answer: close to the fully implicit solution even when the cutoff sits near
            // Nyquist and the state moves a long way in one sample.
            const Solution first = solve(c, x, x - k * c.y[3], c.y[0], c.y[1], c.y[2], c.y[3]);
            const Solution s = solve(c, x, first.u, first.y1, first.y2, first.y3, first.y4);

            c.s[0] = 2.0f * s.y1 - c.s[0];
            c.s[1] = 2.0f * s.y2 - c.s[1];
            c.s[2] = 2.0f * s.y3 - c.s[2];
            c.s[3] = 2.0f * s.y4 - c.s[3];
            c.y = { s.y1, s.y2, s.y3, s.y4 };

            // Band- and high-pass peak 4.3 and 2.4 times higher than the 24 dB tap at resonance;
            // scaling with the feedback keeps every mode at about the same level when it sings.
            switch (tap)
            {
                case Mode::lowpass24:  return s.y4;
                case Mode::lowpass12:  return s.y2;
                case Mode::bandpass:   return 2.0f * (s.y1 - s.y2) / (1.0f + 0.66f * k);
                case Mode::highpass12: return (s.u - 2.0f * s.y1 + s.y2) / (1.0f + 0.28f * k);
            }
            return s.y4;
        }

        float nextNoise() noexcept
        {
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            return static_cast<float>(noise >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        float sampleRate = 48000.0f;
        float maxCutoffHz = 21600.0f;
        float g = 0.1f;
        float k = 0.0f;
        bool primed = false;
        Channel left, right;
        std::uint32_t noise = 0x9E3779B9u;
    };

    static_assert(Module<LadderProcessor>);
}
