#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace coolsynth::synth
{
    /**
     * Four-pole (24 dB/octave) transistor-ladder low-pass filter.
     *
     * Topology: four TPT one-pole stages with global feedback, solved as a
     * zero-delay-feedback loop (Zavalishin, "The Art of VA Filter Design",
     * ch. 5-6), plus a tanh saturator on the ladder input so high resonance
     * and hot signals bend the way an analog ladder does instead of clipping.
     *
     * The filter is modulation-safe: cutoff and resonance may change on every
     * sample. Resonance runs 0..1 and reaches self-oscillation at the top.
     */
    class VoiceLadderFilter final
    {
    public:
        void prepare(double sampleRate) noexcept
        {
            currentSampleRate = std::max(1.0, sampleRate);
            reset();
            setCutoffHz(cutoffHz);
        }

        void reset() noexcept
        {
            state.fill(0.0f);
        }

        /** Cutoff in Hz, clamped to a stable range for the prepared sample rate. */
        void setCutoffHz(float newCutoffHz) noexcept
        {
            const float maxCutoffHz = static_cast<float>(0.45 * currentSampleRate);
            cutoffHz = std::clamp(newCutoffHz, 20.0f, std::max(20.0f, maxCutoffHz));

            const float g = std::tan(kPi * cutoffHz / static_cast<float>(currentSampleRate));
            stageGain = g / (1.0f + g);
            feedbackStateScale = 1.0f / (1.0f + g);
            const float g2 = stageGain * stageGain;
            loopGain4 = g2 * g2;
        }

        /** Resonance 0..1. Feedback amount k = 4 * resonance; k = 4 self-oscillates. */
        void setResonance(float resonanceNormalized) noexcept
        {
            feedbackAmount = 4.0f * std::clamp(resonanceNormalized, 0.0f, 1.0f);
            // The ladder loses low end as feedback rises (DC gain 1/(1+k)); win part of it back.
            resonanceMakeupGain = 1.0f + kBassCompensation * feedbackAmount;
        }

        float processSample(float input) noexcept
        {
            const float G = stageGain;
            const float G2 = G * G;
            const float G3 = G2 * G;

            // Instantaneous response of the four stages to their stored state.
            const float feedbackEstimate = (G3 * state[0] + G2 * state[1] + G * state[2] + state[3])
                                           * feedbackStateScale;

            // Zero-delay solution of u = x - k * y4 for the linear ladder, then the saturator.
            float u = (input - feedbackAmount * feedbackEstimate) / (1.0f + feedbackAmount * loopGain4);
            u = std::tanh(u);

            float y = u;
            for (auto& s : state)
            {
                const float v = (y - s) * G;
                const float stageOut = v + s;
                s = stageOut + v;
                y = stageOut;
            }

            return y * resonanceMakeupGain;
        }

        /** Flush sub-audible state so idle voices cannot linger in denormal territory. */
        void snapToZero() noexcept
        {
            for (auto& s : state)
                if (std::abs(s) < 1.0e-12f)
                    s = 0.0f;
        }

    private:
        static constexpr float kPi = 3.14159265358979323846f;
        static constexpr float kBassCompensation = 0.5f;

        std::array<float, 4> state {};
        double currentSampleRate = 48000.0;
        float cutoffHz = 10000.0f;
        float stageGain = 0.0f;
        float feedbackStateScale = 1.0f;
        float loopGain4 = 0.0f;
        float feedbackAmount = 0.0f;
        float resonanceMakeupGain = 1.0f;
    };
}
