#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/*
    V3 LFO (DESIGN_V3.md sections 4.3 and 5), one class for the local LFO1..2 of every voice and
    the global GLFO1..2 of a part.

    Output is bipolar, -1..1, times a depth that a delay and a fade-in build up from the last
    restart. Sine and triangle start at 0 rising, the saws and the steps at their low end (saw
    down at its top), square high. Steps is a 16-level staircase rising over the cycle;
    sample-and-hold takes a new value at every cycle start and smoothed random glides there along
    a raised cosine.

    restart() runs at a note (local) or at the first note after silence (global). The delay and
    the fade always start again; with retrigger (or one-shot) on, the phase starts at its start
    phase and waits for the delay, so the first cycle begins when the LFO is heard. Off, the
    phase runs freely. One-shot runs a single cycle from the restart and holds its last value.

    Rate is in Hz, or with sync a note division of the tempo. A synced LFO that does not restart
    follows the host's song position while the transport plays, so every voice and every pass
    of the loop sees the same phase at the same beat.
*/
namespace coolsynth::synth::v3
{
    enum class LfoShape : int { sine = 0, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, steps };
    inline constexpr int lfoShapeCount = 8;

    struct LfoDivision
    {
        const char* name;
        double beats; // quarter notes per cycle
    };

    inline constexpr std::array<LfoDivision, 16> lfoDivisions { {
        { "8 Bars", 32.0 },  { "4 Bars", 16.0 },     { "2 Bars", 8.0 },  { "1 Bar", 4.0 },
        { "1/2", 2.0 },      { "1/2T", 4.0 / 3.0 },  { "1/4.", 1.5 },    { "1/4", 1.0 },
        { "1/4T", 2.0 / 3.0 }, { "1/8.", 0.75 },     { "1/8", 0.5 },     { "1/8T", 1.0 / 3.0 },
        { "1/16.", 0.375 },  { "1/16", 0.25 },       { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
    } };
    inline constexpr int lfoDefaultDivision = 7; // 1/4

    struct LfoSettings
    {
        LfoShape shape = LfoShape::sine;
        float rateHz = 2.0f;
        bool sync = false;
        int division = lfoDefaultDivision;
        float startPhase = 0.0f; // 0..1 of a cycle
        bool retrigger = true;
        float delaySeconds = 0.0f;
        float fadeSeconds = 0.0f;
        bool oneShot = false;

        bool operator==(const LfoSettings&) const = default;
    };

    /** Tempo and song position at the start of a span. */
    struct LfoClock
    {
        double bpm = 120.0;
        double ppq = 0.0;     // quarter notes, valid while playing
        bool playing = false; // the host transport runs and reports its position
    };

    class Lfo final
    {
    public:
        static constexpr int stepCount = 16;

        void prepare(double newSampleRate, std::uint32_t seed) noexcept
        {
            sampleRate = std::max(1.0, newSampleRate);
            rng = seed * 2654435761u + 0x6C8E9CF5u;
            if (rng == 0)
                rng = 1;
            reset();
        }

        /** Free-running from phase 0 at full depth. */
        void reset() noexcept
        {
            phase = 0.0;
            progress = 0.0;
            sinceRestart = kLongAgo;
            waitForDelay = false;
            finished = false;
            held = nextRandom();
            from = held;
            to = nextRandom();
            lastValue = 0.0f;
        }

        void restart(const LfoSettings& s) noexcept
        {
            sinceRestart = 0.0;
            if (s.retrigger || s.oneShot)
            {
                phase = wrap(static_cast<double>(s.startPhase));
                progress = 0.0;
                finished = false;
                waitForDelay = true;
                held = nextRandom();
                from = nextRandom();
                to = nextRandom();
            }
            else
            {
                waitForDelay = false;
            }
        }

        [[nodiscard]] static double cyclesPerSecond(const LfoSettings& s, double bpm) noexcept
        {
            if (! s.sync)
                return std::max(0.0, static_cast<double>(s.rateHz));
            const auto& d = lfoDivisions[static_cast<std::size_t>(std::clamp(s.division, 0, static_cast<int>(lfoDivisions.size()) - 1))];
            return std::max(1.0, bpm) / 60.0 / d.beats;
        }

        /**
            The value for a span of numSamples starting now (what the span's modulation uses),
            then moves the LFO on to the span's end. rateScale multiplies the rate.
        */
        float advance(int numSamples, const LfoSettings& s, const LfoClock& clock, float rateScale = 1.0f) noexcept
        {
            const bool locked = clock.playing && s.sync && ! s.retrigger && ! s.oneShot;
            if (locked)
            {
                const auto& d = lfoDivisions[static_cast<std::size_t>(std::clamp(s.division, 0, static_cast<int>(lfoDivisions.size()) - 1))];
                const double next = wrap(clock.ppq / d.beats + static_cast<double>(s.startPhase));
                if (next < phase)
                    newCycle();
                phase = next;
                finished = false;
            }

            const bool inDelay = sinceRestart < static_cast<double>(s.delaySeconds);
            float depth = 0.0f;
            if (! inDelay)
            {
                const double faded = sinceRestart - static_cast<double>(s.delaySeconds);
                depth = s.fadeSeconds > 0.0f ? static_cast<float>(std::min(1.0, faded / static_cast<double>(s.fadeSeconds))) : 1.0f;
            }
            lastValue = depth * shapeValue(s.shape, finished ? wrap(static_cast<double>(s.startPhase) + kJustBelowOne) : phase);

            const double seconds = static_cast<double>(std::max(0, numSamples)) / sampleRate;
            if (! locked && ! finished && ! (inDelay && waitForDelay))
            {
                const double increment = cyclesPerSecond(s, clock.bpm) * std::max(0.0f, rateScale) * seconds;
                if (s.oneShot)
                {
                    progress += increment;
                    if (progress >= 1.0)
                    {
                        progress = 1.0;
                        finished = true;
                    }
                    const double next = wrap(static_cast<double>(s.startPhase) + progress);
                    if (next < phase && ! finished)
                        newCycle();
                    phase = next;
                }
                else
                {
                    phase += increment;
                    if (phase >= 1.0)
                    {
                        phase -= std::floor(phase);
                        newCycle();
                    }
                }
            }
            sinceRestart = std::min(kLongAgo, sinceRestart + seconds);
            return lastValue;
        }

        [[nodiscard]] float value() const noexcept { return lastValue; }
        [[nodiscard]] double phaseForTesting() const noexcept { return phase; }
        [[nodiscard]] bool isFinished() const noexcept { return finished; }

        /** The shape at a phase in 0..1; the random shapes read this LFO's own values. */
        [[nodiscard]] float shapeValue(LfoShape shape, double p) const noexcept
        {
            switch (shape)
            {
                case LfoShape::sine:
                    return static_cast<float>(std::sin(kTwoPi * p));
                case LfoShape::triangle:
                    return static_cast<float>(p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));
                case LfoShape::sawUp:
                    return static_cast<float>(2.0 * p - 1.0);
                case LfoShape::sawDown:
                    return static_cast<float>(1.0 - 2.0 * p);
                case LfoShape::square:
                    return p < 0.5 ? 1.0f : -1.0f;
                case LfoShape::sampleHold:
                    return held;
                case LfoShape::smoothRandom:
                {
                    const float t = static_cast<float>(0.5 - 0.5 * std::cos(kPi * p));
                    return from + (to - from) * t;
                }
                case LfoShape::steps:
                {
                    const double level = std::min(static_cast<double>(stepCount - 1), std::floor(p * stepCount));
                    return static_cast<float>(level * (2.0 / (stepCount - 1)) - 1.0);
                }
            }
            return 0.0f;
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr double kTwoPi = 2.0 * kPi;
        static constexpr double kLongAgo = 1.0e6;
        static constexpr double kJustBelowOne = 0.999999;

        static double wrap(double p) noexcept { return p - std::floor(p); }

        void newCycle() noexcept
        {
            held = nextRandom();
            from = to;
            to = nextRandom();
        }

        float nextRandom() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        double sampleRate = 48000.0;
        double phase = 0.0;
        double progress = 0.0;     // one-shot: cycles since the restart, up to 1
        double sinceRestart = 0.0; // seconds
        bool waitForDelay = false;
        bool finished = false;
        float held = 0.0f, from = 0.0f, to = 0.0f;
        float lastValue = 0.0f;
        std::uint32_t rng = 1;
    };
}
