#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    V3 stutter effect (decision D220): catches a slice of what plays and repeats it, as Ableton's Beat Repeat does
    (Live's audio effect reference) with the build-up roll of iZotope's Stutter Edit.

    When: on the tempo grid, every Interval at its Offset, with a Chance; or at once while Hold is on, which an effect
    route can put on the mod wheel, a macro or the step modulator. A stutter first lets one Grid's length play through
    and keeps it (Variation picks the length at random around Grid, in straight or triplet steps as Grid is), then
    repeats it for Gate, or for as long as Hold stays on.

    Each repeat: Roll makes it shorter than the one before (halved at full), Pitch plays it that many semitones lower
    than the one before (slower, so longer, as a tape slowed down), Decay makes it quieter, Reverse plays it (or every
    other one) backwards. A repeat fades in and out over at most 2 ms, so its edges never click; reading between
    samples is cubic.

    Mode: Insert mutes the sound while the repeats play (the hard edit), Mix plays them over it, Gate passes only the
    repeats (for a master slot used in parallel).
*/
namespace coolsynth::synth::v3
{
    class StutterInsert final
    {
    public:
        enum class Mode : int { insert = 0, mix, gate };
        enum class Reverse : int { off = 0, all, alternate };

        struct Settings
        {
            double intervalBeats = 4.0;   // how often the grid may catch
            double offsetBeats = 2.0;     // where in the interval
            float chance = 1.0f;
            bool hold = false;
            int gridIndex = 4;            // into gridNames
            double gateBeats = 1.0;       // how long the repeats go on after the slice
            float variation = 0.0f;
            float roll = 0.0f;
            float pitchSemitones = 0.0f;  // lower per repeat
            float decay = 0.0f;
            Reverse reverse = Reverse::off;
            Mode mode = Mode::insert;
        };

        static constexpr const char* gridNames = "1/4|1/4T|1/8|1/8T|1/16|1/16T|1/32|1/32T|1/64";
        static constexpr std::array<double, 9> gridBeats { 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        static constexpr const char* intervalNames = "1/8|1/4|1/2|1 Bar|2 Bars|4 Bars";
        static constexpr std::array<double, 6> intervalBeats { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        static constexpr const char* gateNames = "1/16|1/8|3/16|1/4|3/8|1/2|3/4|1 Bar|2 Bars";
        static constexpr std::array<double, 9> gateBeats { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0 };

        static constexpr double maxSliceSeconds = 2.0; // a quarter note down to 30 BPM
        static constexpr double edgeSeconds = 0.002;   // the fades at a repeat's edges and at the stutter's

        /** How long the effect may keep sounding once its input is silent: the slice and the repeats. */
        [[nodiscard]] static double tailSeconds(const Settings& s, double bpm) noexcept
        {
            const double secondsPerBeat = 60.0 / std::max(20.0, bpm);
            const double slice = std::min(maxSliceSeconds, gridBeats[static_cast<std::size_t>(std::clamp(s.gridIndex, 0, 8))] * secondsPerBeat);
            // Every repeat a pitch step lower lasts longer: the gate is in output time, so it bounds them anyway.
            return slice + s.gateBeats * secondsPerBeat + 2.0 * edgeSeconds;
        }

        void prepare(double newSampleRate)
        {
            sampleRate = std::max(1000.0, newSampleRate);
            const auto frames = static_cast<std::size_t>(maxSliceSeconds * sampleRate) + 4;
            left.assign(frames, 0.0f);
            right.assign(frames, 0.0f);
            edgeSamples = std::max(1, static_cast<int>(std::lround(edgeSeconds * sampleRate)));
            clear();
        }

        void clear() noexcept
        {
            state = State::idle;
            dryLevel = 1.0f;
            wetLevel = 0.0f;
            held = false;
            lastCell = 0.0;
            haveLastCell = false;
            captured = 0;
            if (! left.empty())
            {
                std::fill(left.begin(), left.end(), 0.0f);
                std::fill(right.begin(), right.end(), 0.0f);
            }
        }

        /** beatsAtStart and beatsPerSample: where the first sample sits on the beat, and how far each sample moves. */
        void process(juce::AudioBuffer<float>& stereo, const Settings& s, double beatsAtStart, double beatsPerSample) noexcept
        {
            const int n = stereo.getNumSamples();
            float* outLeft = stereo.getWritePointer(0);
            float* outRight = stereo.getWritePointer(1);
            const float edgeStep = 1.0f / static_cast<float>(edgeSamples);
            const double interval = std::max(1.0e-6, s.intervalBeats);

            for (int i = 0; i < n; ++i)
            {
                // Hold: on, a stutter starts at once and keeps going; off again, it ends at once (a slice still
                // being caught has nothing to repeat yet).
                if (s.hold && ! held)
                {
                    held = true;
                    if (state == State::idle)
                        begin(s, beatsPerSample);
                }
                else if (! s.hold && held)
                {
                    held = false;
                    if (state == State::capturing)
                        state = State::idle;
                    else if (state == State::repeating)
                        state = State::ending;
                }

                // The grid: a stutter may start where the beat crosses into the next interval, at its offset.
                if (beatsPerSample > 0.0)
                {
                    const double beat = beatsAtStart + beatsPerSample * static_cast<double>(i);
                    const double cell = std::floor((beat - s.offsetBeats) / interval);
                    const bool crossed = haveLastCell ? cell != lastCell
                                                      : (beat - s.offsetBeats) / interval - cell < beatsPerSample / interval;
                    lastCell = cell;
                    haveLastCell = true;
                    if (crossed && state == State::idle && nextUniform() < s.chance)
                        begin(s, beatsPerSample);
                }

                const float inLeft = outLeft[i], inRight = outRight[i];
                float wetLeft = 0.0f, wetRight = 0.0f;

                switch (state)
                {
                    case State::capturing:
                        left[static_cast<std::size_t>(captured)] = inLeft;
                        right[static_cast<std::size_t>(captured)] = inRight;
                        if (++captured >= sliceLength)
                            startRepeat(s, 1);
                        break;
                    case State::repeating:
                        renderRepeat(wetLeft, wetRight);
                        if (++repeatDone >= repeatLength)
                            startRepeat(s, repeat + 1);
                        if (! held && ++gateDone >= gateLength)
                            state = State::ending; // Gate is over: out within the edge time
                        break;
                    case State::ending:
                        if (repeatDone < repeatLength)
                        {
                            renderRepeat(wetLeft, wetRight);
                            ++repeatDone;
                        }
                        break;
                    case State::idle:
                        break;
                }

                // The repeats come in and go out over the edge time, the sound the other way in Insert mode.
                const bool repeating = state == State::repeating;
                wetLevel += std::clamp((repeating ? 1.0f : 0.0f) - wetLevel, -edgeStep, edgeStep);
                if (state == State::ending && wetLevel <= 0.0f)
                    state = State::idle;
                float dryTarget = 1.0f;
                if (s.mode == Mode::gate)
                    dryTarget = 0.0f;
                else if (s.mode == Mode::insert && repeating)
                    dryTarget = 0.0f;
                dryLevel += std::clamp(dryTarget - dryLevel, -edgeStep, edgeStep);

                outLeft[i] = inLeft * dryLevel + wetLeft * wetLevel;
                outRight[i] = inRight * dryLevel + wetRight * wetLevel;
            }
        }

        [[nodiscard]] bool stutteringForTesting() const noexcept { return state != State::idle; }
        [[nodiscard]] int sliceLengthForTesting() const noexcept { return sliceLength; }

    private:
        enum class State : std::uint8_t { idle, capturing, repeating, ending };

        void begin(const Settings& s, double beatsPerSample) noexcept
        {
            // Variation: a slice length a few steps either way, straight or triplet as Grid is.
            int index = std::clamp(s.gridIndex, 0, static_cast<int>(gridBeats.size()) - 1);
            if (s.variation > 0.0f)
            {
                const int steps = static_cast<int>(std::lround((2.0f * nextUniform() - 1.0f) * s.variation * 2.0f));
                index = std::clamp(index + 2 * steps, index % 2, static_cast<int>(gridBeats.size()) - 1);
                if (index % 2 != s.gridIndex % 2)
                    --index; // past the end of the triplet steps
            }
            const double samplesPerBeat = beatsPerSample > 0.0 ? 1.0 / beatsPerSample : sampleRate / 2.0;
            sliceLength = std::clamp(static_cast<int>(std::lround(gridBeats[static_cast<std::size_t>(index)] * samplesPerBeat)),
                                     4 * edgeSamples, static_cast<int>(maxSliceSeconds * sampleRate));
            gateLength = std::max(1, static_cast<int>(std::lround(s.gateBeats * samplesPerBeat)));
            captured = 0;
            gateDone = 0;
            state = State::capturing;
        }

        void startRepeat(const Settings& s, int number) noexcept
        {
            repeat = number;
            // Roll shortens each repeat (halved at full), to a 64th triplet's length at the least.
            const double shrink = std::pow(2.0, -static_cast<double>(std::clamp(s.roll, 0.0f, 1.0f)) * static_cast<double>(number - 1));
            sourceLength = std::max(static_cast<double>(std::min(sliceLength, 4 * edgeSamples)), static_cast<double>(sliceLength) * shrink);
            // The first repeat at the slice's own pitch and level, each one after a Pitch step lower (four octaves at
            // most, so a held stutter never stalls) and Decay quieter.
            const double octavesDown = std::min(4.0, static_cast<double>(std::clamp(s.pitchSemitones, 0.0f, 12.0f)) * static_cast<double>(number - 1) / 12.0);
            rate = std::exp2(-octavesDown);
            repeatLength = std::max(1, static_cast<int>(std::floor(sourceLength / rate)));
            repeatDone = 0;
            gain = std::pow(1.0f - std::clamp(s.decay, 0.0f, 1.0f), static_cast<float>(number - 1));
            backwards = s.reverse == Reverse::all || (s.reverse == Reverse::alternate && number % 2 == 0);
            fade = std::max(1, std::min(edgeSamples, repeatLength / 4));
            state = State::repeating;
        }

        void renderRepeat(float& outLeft, float& outRight) const noexcept
        {
            const double position = static_cast<double>(repeatDone) * rate;
            const double at = backwards ? sourceLength - 1.0 - position : position;
            const int limit = static_cast<int>(std::ceil(sourceLength)) - 1;
            const int k = std::clamp(static_cast<int>(std::floor(at)), 0, std::max(0, limit));
            const float t = static_cast<float>(at - std::floor(at));
            auto sampleAt = [limit](const std::vector<float>& b, int j) { return b[static_cast<std::size_t>(std::clamp(j, 0, std::max(0, limit)))]; };
            auto cubic = [&](const std::vector<float>& b)
            {
                const float y0 = sampleAt(b, k - 1), y1 = sampleAt(b, k), y2 = sampleAt(b, k + 1), y3 = sampleAt(b, k + 2);
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * t + c2) * t + c1) * t + y1;
            };
            // The repeat's own fades: in and out over at most the edge time.
            const float window = std::min({ 1.0f, static_cast<float>(repeatDone + 1) / static_cast<float>(fade),
                                            static_cast<float>(repeatLength - repeatDone) / static_cast<float>(fade) });
            const float g = gain * window;
            outLeft = cubic(left) * g;
            outRight = cubic(right) * g;
        }

        float nextUniform() noexcept
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            return static_cast<float>(random >> 8) * (1.0f / 16777216.0f);
        }

        double sampleRate = 48000.0;
        std::vector<float> left, right;
        int edgeSamples = 96;
        State state = State::idle;
        bool held = false;
        double lastCell = 0.0;
        bool haveLastCell = false;
        int sliceLength = 0, captured = 0;
        int gateLength = 0, gateDone = 0;
        int repeat = 0, repeatLength = 0, repeatDone = 0, fade = 1;
        double sourceLength = 0.0, rate = 1.0;
        float gain = 1.0f;
        bool backwards = false;
        float dryLevel = 1.0f, wetLevel = 0.0f;
        std::uint32_t random = 0x6C8E9CF5u;
    };
}
