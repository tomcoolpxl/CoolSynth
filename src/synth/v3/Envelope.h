#pragma once

#include <algorithm>
#include <cmath>

/*
    V3 envelope (DESIGN_V3.md section 4.3): the V2 AnalogEnvelope extended.

    Each stage is a one-pole run toward a target a little past the stage end (the EarLevel
    design), which gives the convex attack and concave decay and release of an RC envelope. The
    overshoot ratio is now a per-stage curve: 0 is the V2 exponential shape, 1 is close to a
    straight line. Added: velocity sensitivity on the peak, loop (attack and decay cycle while the
    gate is held) and one-shot (decay runs to its end and goes straight to release, whatever the
    gate does).
*/
namespace coolsynth::synth::v3
{
    class Envelope final
    {
    public:
        struct Parameters
        {
            float attack = 0.01f;   // seconds
            float decay = 0.1f;
            float sustain = 1.0f;   // 0..1
            float release = 0.1f;
            float attackCurve = 0.58f;  // 0 exponential .. 1 linear; 0.58 reproduces the V2 attack
            float decayCurve = 0.0f;
            float releaseCurve = 0.0f;
            float velocitySensitivity = 0.0f; // 0: full level always; 1: peak follows velocity
            bool loop = false;
            bool oneShot = false;
        };

        enum class State { idle, attack, decay, sustain, release };

        void setSampleRate(double newSampleRate) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            recalculate();
        }

        void setParameters(const Parameters& newParameters) noexcept
        {
            parameters = newParameters;
            parameters.sustain = std::clamp(parameters.sustain, 0.0f, 1.0f);
            parameters.attackCurve = std::clamp(parameters.attackCurve, 0.0f, 1.0f);
            parameters.decayCurve = std::clamp(parameters.decayCurve, 0.0f, 1.0f);
            parameters.releaseCurve = std::clamp(parameters.releaseCurve, 0.0f, 1.0f);
            parameters.velocitySensitivity = std::clamp(parameters.velocitySensitivity, 0.0f, 1.0f);
            recalculate();
        }

        [[nodiscard]] const Parameters& getParameters() const noexcept { return parameters; }
        [[nodiscard]] bool isActive() const noexcept { return state != State::idle; }
        [[nodiscard]] State getState() const noexcept { return state; }
        [[nodiscard]] float getValue() const noexcept { return value * velocityGain; }

        void reset() noexcept
        {
            value = 0.0f;
            state = State::idle;
            gateHeld = false;
        }

        /** Starts the attack from the current level, so retriggers never click. */
        void noteOn(float velocity = 1.0f) noexcept
        {
            const float v = std::clamp(velocity, 0.0f, 1.0f);
            velocityGain = 1.0f - parameters.velocitySensitivity + parameters.velocitySensitivity * v;
            gateHeld = true;
            state = State::attack;
        }

        void noteOff() noexcept
        {
            gateHeld = false;
            if (state == State::idle)
                return;

            // One-shot finishes its decay first; the release follows from there.
            if (parameters.oneShot && (state == State::attack || state == State::decay))
                return;

            enterRelease();
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
                        if (parameters.oneShot)
                            enterRelease();
                        else if (parameters.loop && gateHeld)
                            state = State::attack;
                        else
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

            return value * velocityGain;
        }

    private:
        /** Curve 0 is the V2 decay ratio (deeply exponential), curve 1 overshoots by 100x (nearly linear). */
        static float ratioFor(float curve) noexcept
        {
            return std::pow(10.0f, -4.0f + 6.0f * curve);
        }

        [[nodiscard]] float coefficientFor(float seconds, float targetRatio) const noexcept
        {
            const float samples = std::max(1.0f, seconds * static_cast<float>(sampleRate));
            return std::exp(-std::log((1.0f + targetRatio) / targetRatio) / samples);
        }

        void recalculate() noexcept
        {
            const float attackRatio = ratioFor(parameters.attackCurve);
            attackCoefficient = coefficientFor(parameters.attack, attackRatio);
            attackBase = (1.0f + attackRatio) * (1.0f - attackCoefficient);

            const float decayRatio = ratioFor(parameters.decayCurve);
            decayCoefficient = coefficientFor(parameters.decay, decayRatio);
            decayBase = (parameters.sustain - decayRatio) * (1.0f - decayCoefficient);

            const float releaseRatio = ratioFor(parameters.releaseCurve);
            releaseCoefficient = coefficientFor(parameters.release, releaseRatio);
            releaseBase = -releaseRatio * (1.0f - releaseCoefficient);
        }

        void enterRelease() noexcept
        {
            if (parameters.release > 0.0f)
                state = State::release;
            else
            {
                value = 0.0f;
                state = State::idle;
            }
        }

        Parameters parameters;
        State state = State::idle;
        double sampleRate = 48000.0;
        float value = 0.0f;
        float velocityGain = 1.0f;
        bool gateHeld = false;
        float attackCoefficient = 0.0f;
        float attackBase = 0.0f;
        float decayCoefficient = 0.0f;
        float decayBase = 0.0f;
        float releaseCoefficient = 0.0f;
        float releaseBase = 0.0f;
    };
}
