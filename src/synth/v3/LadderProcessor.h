#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 ladder processor (DESIGN_V3.md section 3.2): the V2 VoiceLadderFilter as a slot module.

    Four TPT one-pole stages with global feedback solved as a zero-delay-feedback loop
    (Zavalishin, "The Art of VA Filter Design", ch. 5-6) and a tanh saturator on the ladder
    input. Stereo with shared coefficients. Cutoff (after key tracking) and resonance are ramped
    per sample across the sub-block, so a sweep that moves between sub-blocks stays smooth.
    Resonance 0..1 maps to feedback k = 4 sqrt(r): self-oscillation at the top, an even growth
    of the audible peak across the travel. The drive is a plain gain into the saturator.
*/
namespace coolsynth::synth::v3
{
    class LadderProcessor final
    {
    public:
        enum Param : int { cutoff = 0, resonance, drive, keyTrack, paramCount };

        static constexpr float keyReferenceHz = 261.6256f; // C4: key tracking is neutral here

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Ladder", 2,
                {{ { "cutoff", "Cutoff",    20.0f, 20000.0f, 3200.0f, Skew::logarithmic, Unit::hertz,    true },
                   { "res",    "Resonance", 0.0f,  1.0f,     0.08f,   Skew::linear,      Unit::percent,  true },
                   { "drive",  "Drive",     0.0f,  24.0f,    0.0f,    Skew::linear,      Unit::decibels, true },
                   { "key",    "Key Track", 0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            maxCutoffHz = 0.45f * sampleRate;
            reset();
        }

        void reset() noexcept
        {
            leftState.fill(0.0f);
            rightState.fill(0.0f);
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const float key = juce::jlimit(0.0f, 1.0f, params.p[keyTrack]);
            float hz = params.p[cutoff] * std::pow(std::max(1.0f, params.frequencyHz) / keyReferenceHz, key);
            hz = juce::jlimit(20.0f, std::max(20.0f, maxCutoffHz), hz);

            const float gTarget = std::tan(juce::MathConstants<float>::pi * hz / sampleRate);
            // k = 4 is the linear stability edge; the top of the travel goes past it so the ladder
            // sings on its own and the saturator sets the level, as an analogue ladder does.
            const float kTarget = kMaxFeedback * std::sqrt(juce::jlimit(0.0f, 1.0f, params.p[resonance]));
            const float inputGain = std::pow(10.0f, juce::jlimit(0.0f, 24.0f, params.p[drive]) * 0.05f);

            if (! primed)
            {
                g = gTarget;
                k = kTarget;
                primed = true;
            }

            const float inverseCount = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float gStep = (gTarget - g) * inverseCount;
            const float kStep = (kTarget - k) * inverseCount;

            for (int i = 0; i < numSamples; ++i)
            {
                g += gStep;
                k += kStep;

                const float G = g / (1.0f + g);
                const float feedbackStateScale = 1.0f / (1.0f + g);
                const float G2 = G * G;
                const float G4 = G2 * G2;
                // The ladder loses low end as feedback rises (DC gain 1/(1+k)); win part of it back.
                const float makeup = 1.0f + kBassCompensation * k;
                const float solveScale = 1.0f / (1.0f + k * G4);

                left[i] = tick(leftState, left[i] * inputGain, G, G2, feedbackStateScale, solveScale) * makeup;
                right[i] = tick(rightState, right[i] * inputGain, G, G2, feedbackStateScale, solveScale) * makeup;
            }

            g = gTarget;
            k = kTarget;
            snapToZero(leftState);
            snapToZero(rightState);
        }

    private:
        static constexpr float kBassCompensation = 0.5f;
        static constexpr float kMaxFeedback = 4.3f; // self-oscillation from resonance 0.87 upwards

        float tick(std::array<float, 4>& state, float input, float G, float G2, float feedbackStateScale, float solveScale) const noexcept
        {
            const float G3 = G2 * G;
            // Instantaneous response of the four stages to their stored state.
            const float feedbackEstimate = (G3 * state[0] + G2 * state[1] + G * state[2] + state[3]) * feedbackStateScale;

            // Zero-delay solution of u = x - k * y4 for the linear ladder, then the saturator.
            float u = (input - k * feedbackEstimate) * solveScale;
            u = std::tanh(u);

            float y = u;
            for (auto& s : state)
            {
                const float v = (y - s) * G;
                const float stageOut = v + s;
                s = stageOut + v;
                y = stageOut;
            }
            return y;
        }

        static void snapToZero(std::array<float, 4>& state) noexcept
        {
            for (auto& s : state)
                if (std::abs(s) < 1.0e-12f)
                    s = 0.0f;
        }

        float sampleRate = 48000.0f;
        float maxCutoffHz = 21600.0f;
        float g = 0.1f;
        float k = 0.0f;
        bool primed = false;
        std::array<float, 4> leftState {};
        std::array<float, 4> rightState {};
    };

    static_assert(Module<LadderProcessor>);
}
