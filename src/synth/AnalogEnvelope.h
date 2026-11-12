#pragma once

#include <algorithm>
#include <cmath>

namespace coolsynth::synth
{
    /**
     * ADSR envelope with analog-style exponential segments.
     *
     * Each stage is a one-pole run toward a target that lies a little past the
     * stage end, so the curve has the convex attack and concave decay/release
     * of an RC-driven envelope generator rather than straight lines. The
     * overshoot ratios follow the well-known EarLevel design: a large ratio for
     * the attack keeps it snappy, a tiny ratio for decay and release lets them
     * reach their targets in the programmed time.
     *
     * The public surface mirrors juce::ADSR so the voice code reads the same.
     */
    class AnalogEnvelope final
    {
    public:
        struct Parameters
        {
            float attack = 0.1f;
            float decay = 0.1f;
            float sustain = 1.0f;
            float release = 0.1f;
        };

        void setSampleRate(double newSampleRate) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            recalculate();
        }

        void setParameters(const Parameters& newParameters) noexcept
        {
            parameters = newParameters;
            parameters.sustain = std::clamp(parameters.sustain, 0.0f, 1.0f);
            recalculate();
        }

        [[nodiscard]] const Parameters& getParameters() const noexcept { return parameters; }
        [[nodiscard]] bool isActive() const noexcept { return state != State::idle; }

        void reset() noexcept
        {
            value = 0.0f;
            state = State::idle;
        }

        /** Starts the attack from the current level, so retriggers never click. */
        void noteOn() noexcept
        {
            state = State::attack;
        }

        void noteOff() noexcept
        {
            if (state == State::idle)
                return;

            if (parameters.release > 0.0f)
                state = State::release;
            else
                reset();
        }

        float getNextSample() noexcept
        {
            switch (state)
            {
                case State::idle:
                    return 0.0f;

                case State::attack:
                    value = attackBase + value * attackCoefficient;
                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        state = State::decay;
                    }
                    break;

                case State::decay:
                    value = decayBase + value * decayCoefficient;
                    if (value <= parameters.sustain)
                    {
                        value = parameters.sustain;
                        state = State::sustain;
                    }
                    break;

                case State::sustain:
                    value = parameters.sustain;
                    break;

                case State::release:
                    value = releaseBase + value * releaseCoefficient;
                    if (value <= 0.0f)
                    {
                        value = 0.0f;
                        state = State::idle;
                    }
                    break;
            }

            return value;
        }

    private:
        enum class State { idle, attack, decay, sustain, release };

        static constexpr float kAttackTargetRatio = 0.3f;
        static constexpr float kDecayReleaseTargetRatio = 0.0001f;

        [[nodiscard]] float coefficientFor(float seconds, float targetRatio) const noexcept
        {
            const float samples = std::max(1.0f, seconds * static_cast<float>(sampleRate));
            return std::exp(-std::log((1.0f + targetRatio) / targetRatio) / samples);
        }

        void recalculate() noexcept
        {
            attackCoefficient = coefficientFor(parameters.attack, kAttackTargetRatio);
            attackBase = (1.0f + kAttackTargetRatio) * (1.0f - attackCoefficient);

            decayCoefficient = coefficientFor(parameters.decay, kDecayReleaseTargetRatio);
            decayBase = (parameters.sustain - kDecayReleaseTargetRatio) * (1.0f - decayCoefficient);

            releaseCoefficient = coefficientFor(parameters.release, kDecayReleaseTargetRatio);
            releaseBase = -kDecayReleaseTargetRatio * (1.0f - releaseCoefficient);
        }

        Parameters parameters;
        State state = State::idle;
        double sampleRate = 44100.0;
        float value = 0.0f;
        float attackCoefficient = 0.0f;
        float attackBase = 0.0f;
        float decayCoefficient = 0.0f;
        float decayBase = 0.0f;
        float releaseCoefficient = 0.0f;
        float releaseBase = 0.0f;
    };
}
