#pragma once

#include <algorithm>
#include <cmath>

/*
    V3 envelope (DESIGN_V3.md section 4.3).

    Delay, attack, hold, decay, sustain, release. Every segment lasts exactly its programmed time
    and follows an exponential curve of adjustable sign and depth,
        shape(t) = (e^(c t) - 1) / (e^c - 1),   t in 0..1,
    where a negative curve starts fast and settles slowly (an RC charge, the analogue feel), zero
    is a straight line and a positive curve starts slowly. Velocity sets the peak, so a retrigger
    with a different velocity moves to the new level without a jump. A retrigger continues from the
    current level and shortens the attack by the distance already covered. Times can shrink with
    the key, as plucked and struck instruments do. Loop cycles attack, hold and decay while the
    gate is held; one-shot runs to the end of the decay whatever the gate does. Sustain changes
    are followed with 5 ms smoothing.
*/
namespace coolsynth::synth::v3
{
    class Envelope final
    {
    public:
        struct Parameters
        {
            float delay = 0.0f;   // seconds before the attack; from silence only
            float attack = 0.005f;
            float hold = 0.0f;    // seconds at the peak
            float decay = 0.2f;
            float sustain = 1.0f; // share of the peak
            float release = 0.2f;
            float attackCurve = -0.3f;  // -1..1, see above
            float decayCurve = -0.6f;
            float releaseCurve = -0.6f;
            float velocitySensitivity = 0.0f; // 0: peak 1 always; 1: peak equals velocity
            float keyTimeTracking = 0.0f;     // 1: every time halves per octave above C4
            bool loop = false;
            bool oneShot = false;

            bool operator==(const Parameters&) const = default;
        };

        enum class Stage { idle, delay, attack, hold, decay, sustain, release };

        static constexpr float curveRange = 8.0f; // curve 1 is an exponent of 8

        void setSampleRate(double newSampleRate) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            sustainCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));
        }

        /** Times and curves apply from the next segment; the sustain level is followed at once. */
        void setParameters(const Parameters& p) noexcept
        {
            parameters = p;
            parameters.delay = std::max(0.0f, p.delay);
            parameters.attack = std::max(0.0f, p.attack);
            parameters.hold = std::max(0.0f, p.hold);
            parameters.decay = std::max(0.0f, p.decay);
            parameters.release = std::max(0.0f, p.release);
            parameters.sustain = std::clamp(p.sustain, 0.0f, 1.0f);
            parameters.attackCurve = std::clamp(p.attackCurve, -1.0f, 1.0f);
            parameters.decayCurve = std::clamp(p.decayCurve, -1.0f, 1.0f);
            parameters.releaseCurve = std::clamp(p.releaseCurve, -1.0f, 1.0f);
            parameters.velocitySensitivity = std::clamp(p.velocitySensitivity, 0.0f, 1.0f);
            parameters.keyTimeTracking = std::clamp(p.keyTimeTracking, 0.0f, 1.0f);
        }

        [[nodiscard]] const Parameters& getParameters() const noexcept { return parameters; }
        [[nodiscard]] bool isActive() const noexcept { return stage != Stage::idle; }
        [[nodiscard]] Stage getStage() const noexcept { return stage; }
        [[nodiscard]] float getValue() const noexcept { return value; }
        [[nodiscard]] float getPeak() const noexcept { return peak; }

        void reset() noexcept
        {
            stage = Stage::idle;
            value = 0.0f;
            gate = false;
            remaining = 0;
        }

        /** keyOctaves: the note's distance from C4 in octaves, for key time tracking. */
        void noteOn(float velocity = 1.0f, float keyOctaves = 0.0f) noexcept
        {
            const float sensitivity = parameters.velocitySensitivity;
            peak = 1.0f - sensitivity + sensitivity * std::clamp(velocity, 0.0f, 1.0f);
            timeScale = std::exp2(-parameters.keyTimeTracking * keyOctaves);
            gate = true;

            if (value <= kSilent && parameters.delay > 0.0f)
            {
                value = 0.0f;
                stage = Stage::delay;
                remaining = samplesFor(parameters.delay);
                return;
            }
            startAttack();
        }

        void noteOff() noexcept
        {
            gate = false;
            if (stage == Stage::idle || stage == Stage::release)
                return;
            if (parameters.oneShot && stage != Stage::sustain)
                return; // finishes its decay first
            startRelease();
        }

        float getNextSample() noexcept
        {
            switch (stage)
            {
                case Stage::idle:
                    return 0.0f;

                case Stage::delay:
                    if (--remaining <= 0)
                        startAttack();
                    break;

                case Stage::attack:
                    value = segmentStep(peak);
                    if (segmentDone())
                    {
                        value = peak;
                        startHold();
                    }
                    break;

                case Stage::hold:
                    if (--remaining <= 0)
                        startDecay();
                    break;

                case Stage::decay:
                    value = segmentStep(sustainLevel());
                    if (segmentDone())
                    {
                        value = sustainLevel();
                        endDecay();
                    }
                    break;

                case Stage::sustain:
                    value += (sustainLevel() - value) * sustainCoefficient;
                    break;

                case Stage::release:
                    value = segmentStep(0.0f);
                    if (segmentDone())
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                    }
                    break;
            }
            return value;
        }

        /** Runs n samples and returns the last value, for modulation envelopes read once per sub-block. */
        float advance(int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
                getNextSample();
            return value;
        }

    private:
        static constexpr float kSilent = 1.0e-6f;

        [[nodiscard]] float sustainLevel() const noexcept { return parameters.sustain * peak; }

        [[nodiscard]] int samplesFor(float seconds) const noexcept
        {
            return static_cast<int>(std::lround(static_cast<double>(seconds) * static_cast<double>(timeScale) * sampleRate));
        }

        void startSegment(Stage newStage, float seconds, float curve) noexcept
        {
            stage = newStage;
            segmentFrom = value;
            segmentLength = std::max(1, samplesFor(seconds));
            segmentIndex = 0;
            const double c = static_cast<double>(curve) * curveRange;
            segmentLinear = std::abs(c) < 1.0e-3;
            if (! segmentLinear)
            {
                segmentRatio = std::exp(c / static_cast<double>(segmentLength));
                segmentPower = 1.0;
                segmentDenominator = std::exp(c) - 1.0;
            }
        }

        float segmentStep(float to) noexcept
        {
            ++segmentIndex;
            double shape = 0.0;
            if (segmentLinear)
                shape = static_cast<double>(segmentIndex) / static_cast<double>(segmentLength);
            else
            {
                segmentPower *= segmentRatio;
                shape = (segmentPower - 1.0) / segmentDenominator;
            }
            return segmentFrom + (to - segmentFrom) * static_cast<float>(shape);
        }

        [[nodiscard]] bool segmentDone() const noexcept { return segmentIndex >= segmentLength; }

        void startAttack() noexcept
        {
            if (value >= peak)
            {
                // Already at or above the new peak: settle towards the sustain of this velocity.
                if (value > peak + kSilent)
                    startDecay();
                else
                {
                    value = peak;
                    startHold();
                }
                return;
            }
            const float remainingShare = peak > 0.0f ? (peak - value) / peak : 1.0f;
            startSegment(Stage::attack, parameters.attack * remainingShare, parameters.attackCurve);
        }

        void startHold() noexcept
        {
            const int samples = samplesFor(parameters.hold);
            if (samples < 1)
            {
                startDecay();
                return;
            }
            stage = Stage::hold;
            remaining = samples;
        }

        void startDecay() noexcept { startSegment(Stage::decay, parameters.decay, parameters.decayCurve); }

        void endDecay() noexcept
        {
            if (parameters.oneShot)
                startRelease();
            else if (parameters.loop && gate)
                startAttack();
            else
                stage = Stage::sustain;
        }

        void startRelease() noexcept
        {
            if (value <= kSilent)
            {
                value = 0.0f;
                stage = Stage::idle;
                return;
            }
            startSegment(Stage::release, parameters.release, parameters.releaseCurve);
        }

        Parameters parameters;
        Stage stage = Stage::idle;
        double sampleRate = 48000.0;
        float sustainCoefficient = 0.004f;
        float value = 0.0f;
        float peak = 1.0f;
        float timeScale = 1.0f;
        bool gate = false;
        int remaining = 0;

        float segmentFrom = 0.0f;
        int segmentLength = 1;
        int segmentIndex = 0;
        bool segmentLinear = true;
        double segmentRatio = 1.0;
        double segmentPower = 1.0;
        double segmentDenominator = 1.0;
    };
}
