#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Lfo.h"

/*
    V3 global modulators besides the LFOs (DESIGN_V3.md section 5): the step modulator and the
    chaos source. The global envelope is an `Envelope` run by the part.

    STEP plays 16 values (-1..1) at a tempo division, over a length of 1 to 16 steps. A step with
    slide on glides from the previous step's value, over a share of the step. With retrigger on
    it restarts at the first note after silence and runs at the tempo; off, it follows the host's
    song position while the transport plays (and runs freely otherwise), so a pattern sits on the
    beat of a song.

    CHAOS is a slow random walk shared by every voice: a new random target at irregular intervals
    around 1 / rate, reached through two smoothing stages, so it wanders without corners.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int stepCount = 16;
    inline constexpr int stepDefaultDivision = 13; // 1/16

    struct StepSettings
    {
        std::array<float, stepCount> values {};
        std::array<bool, stepCount> slides {};
        int division = stepDefaultDivision;
        int length = stepCount;
        float glide = 0.5f; // share of a step a slide takes
        bool retrigger = true;
    };

    class StepModulator final
    {
    public:
        void prepare(double newSampleRate) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            reset();
        }

        void reset() noexcept
        {
            position = 0.0;
            lastValue = 0.0f;
        }

        void restart() noexcept { position = 0.0; }

        /** The value for a span starting now, then moves on by numSamples. */
        float advance(int numSamples, const StepSettings& s, const LfoClock& clock) noexcept
        {
            const auto& division = lfoDivisions[static_cast<std::size_t>(std::clamp(s.division, 0, static_cast<int>(lfoDivisions.size()) - 1))];
            const bool locked = clock.playing && ! s.retrigger;
            if (locked)
                position = std::fmod(std::max(0.0, clock.ppq / division.beats), kWrapSteps);

            lastValue = valueAt(position, s);

            if (! locked)
            {
                const double stepsPerSecond = std::max(1.0, clock.bpm) / 60.0 / division.beats;
                position = std::fmod(position + stepsPerSecond * static_cast<double>(std::max(0, numSamples)) / sampleRate, kWrapSteps);
            }
            return lastValue;
        }

        [[nodiscard]] float value() const noexcept { return lastValue; }
        [[nodiscard]] double positionForTesting() const noexcept { return position; }

        [[nodiscard]] static float valueAt(double position, const StepSettings& s) noexcept
        {
            const int length = std::clamp(s.length, 1, stepCount);
            const double whole = std::floor(position);
            const int index = static_cast<int>(static_cast<long long>(whole) % length);
            const float target = std::clamp(s.values[static_cast<std::size_t>(index)], -1.0f, 1.0f);
            if (! s.slides[static_cast<std::size_t>(index)])
                return target;
            const float previous = std::clamp(s.values[static_cast<std::size_t>((index + length - 1) % length)], -1.0f, 1.0f);
            const float glide = std::clamp(s.glide, 0.0f, 1.0f);
            const float t = glide > 0.0f ? std::min(1.0f, static_cast<float>(position - whole) / glide) : 1.0f;
            return previous + (target - previous) * t;
        }

    private:
        static constexpr double kWrapSteps = 720720.0; // a multiple of every length from 1 to 16

        double sampleRate = 48000.0;
        double position = 0.0; // steps since the restart
        float lastValue = 0.0f;
    };

    class ChaosSource final
    {
    public:
        void prepare(double newSampleRate, std::uint32_t seed) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            rng = seed * 2246822519u + 0x165667B1u;
            if (rng == 0)
                rng = 1;
            reset();
        }

        void reset() noexcept
        {
            stage = value = target = 0.0f;
            samplesToNext = 0;
        }

        /** The value for a span starting now, then moves on by numSamples at rateHz. */
        float advance(int numSamples, float rateHz) noexcept
        {
            const float out = value;
            const float rate = std::clamp(rateHz, 0.001f, 100.0f);
            samplesToNext -= numSamples;
            if (samplesToNext <= 0)
            {
                target = nextRandom();
                const float interval = (0.5f + 0.5f * (1.0f + nextRandom())) / rate; // 0.5 to 1.5 periods
                samplesToNext = std::max(1, static_cast<int>(interval * static_cast<float>(sampleRate)));
            }
            const float c = 1.0f - std::exp(-static_cast<float>(numSamples) * rate * 2.0f / static_cast<float>(sampleRate));
            stage += (target - stage) * c;
            value += (stage - value) * c;
            return out;
        }

        [[nodiscard]] float currentValue() const noexcept { return value; }

    private:
        float nextRandom() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        double sampleRate = 48000.0;
        float stage = 0.0f, value = 0.0f, target = 0.0f;
        int samplesToNext = 0;
        std::uint32_t rng = 1;
    };
}
