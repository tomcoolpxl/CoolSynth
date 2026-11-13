#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "synth/SynthParameters.h"

/*
    V3 chord memory (DESIGN_V3.md section 6; REQUIREMENTS_V3.md 3.4): the first stage of a part's
    note layer. One key plays a stored chord: one of twelve types, or a learned chord of up to
    six notes. What it plays goes on to the arpeggiator, the play mode and the allocator as if
    the keys had been pressed.

    Every note-on becomes the chord's notes on that key; with Strum they follow one another
    (a positive time from the bottom up, a negative one from the top down), and the notes still
    waiting are dropped if the key goes up first. Each key remembers what it started, so its
    release ends exactly those notes even if the chord type changed meanwhile. A note two keys
    share is struck again by the second key and ends when the last of them lets go. Everything
    that is not a note passes through in place; All Notes Off and All Sound Off also forget
    every key.

    Learning: while Learn is on, keys play plain notes and are collected; when the last of them
    goes up, the lowest is the root and up to five others within three octaves above it become
    the learned chord, handed over once through consumeLearned().

    process() also reports, for every event it writes, how many of the incoming events had been
    read by then, so the part can follow the keys as played in step with what they cause.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int maxChordNotes = 6;
    inline constexpr int learnedIntervalCount = maxChordNotes - 1;
    inline constexpr int maxLearnedInterval = 36;

    namespace chordType
    {
        inline constexpr int off = 0;
        inline constexpr int octave = 1;
        inline constexpr int fifth = 2;
        inline constexpr int major = 3;
        inline constexpr int minor = 4;
        inline constexpr int sus2 = 5;
        inline constexpr int sus4 = 6;
        inline constexpr int major7 = 7;
        inline constexpr int minor7 = 8;
        inline constexpr int dominant7 = 9;
        inline constexpr int major9 = 10;
        inline constexpr int minor9 = 11;
        inline constexpr int minor11 = 12;
        inline constexpr int learned = 13;
        inline constexpr int count = 14;
    }

    struct ChordSettings
    {
        int type = chordType::off;
        float strumMs = 0.0f;                              // from one note to the next; negative strums downwards
        std::array<int, learnedIntervalCount> learned {};  // semitones above the played key, 0 = unused
        bool learn = false;                                // collect the next chord played
    };

    struct ChordShape
    {
        int count = 1;
        std::array<int, maxChordNotes> semitones {}; // ascending, the first is 0
    };

    /** The notes of the chord a key plays, as semitones above it. */
    [[nodiscard]] inline ChordShape chordShape(const ChordSettings& settings) noexcept
    {
        static constexpr ChordShape shapes[chordType::learned] = {
            { 1, { 0 } },                   // Off
            { 2, { 0, 12 } },               // Octave
            { 3, { 0, 7, 12 } },            // Fifth
            { 3, { 0, 4, 7 } },             // Major
            { 3, { 0, 3, 7 } },             // Minor
            { 3, { 0, 2, 7 } },             // Sus2
            { 3, { 0, 5, 7 } },             // Sus4
            { 4, { 0, 4, 7, 11 } },         // Major 7
            { 4, { 0, 3, 7, 10 } },         // Minor 7
            { 4, { 0, 4, 7, 10 } },         // Dominant 7
            { 5, { 0, 4, 7, 11, 14 } },     // Major 9
            { 5, { 0, 3, 7, 10, 14 } },     // Minor 9
            { 6, { 0, 3, 7, 10, 14, 17 } }, // Minor 11
        };
        if (settings.type != chordType::learned)
            return shapes[std::clamp(settings.type, 0, chordType::learned - 1)];

        ChordShape shape;
        auto intervals = settings.learned;
        std::sort(intervals.begin(), intervals.end());
        for (int interval : intervals)
            if (interval >= 1 && interval <= maxLearnedInterval && interval != shape.semitones[static_cast<std::size_t>(shape.count - 1)])
                shape.semitones[static_cast<std::size_t>(shape.count++)] = interval;
        return shape;
    }

    /** A learned chord in one word: bit 31 set, then six bits per interval. 0 means none. */
    [[nodiscard]] inline std::uint32_t packLearnedChord(const std::array<int, learnedIntervalCount>& intervals) noexcept
    {
        std::uint32_t packed = 0x80000000u;
        for (std::size_t i = 0; i < intervals.size(); ++i)
            packed |= static_cast<std::uint32_t>(std::clamp(intervals[i], 0, maxLearnedInterval)) << (6u * static_cast<std::uint32_t>(i));
        return packed;
    }

    [[nodiscard]] inline std::array<int, learnedIntervalCount> unpackLearnedChord(std::uint32_t packed) noexcept
    {
        std::array<int, learnedIntervalCount> intervals {};
        for (std::size_t i = 0; i < intervals.size(); ++i)
            intervals[i] = static_cast<int>((packed >> (6u * static_cast<std::uint32_t>(i))) & 63u);
        return intervals;
    }

    class ChordMemory final
    {
    public:
        static constexpr int maxPending = 64;
        /** The most events one incoming event can become: a key struck again lets go of six notes and starts six. */
        static constexpr int maxEventsPerInput = 2 * maxChordNotes;

        void prepare(double hostSampleRate) noexcept
        {
            rate = std::max(1000.0, hostSampleRate);
            reset();
            learnedResult = 0;
        }

        /** Panic: every key forgotten, nothing waiting. */
        void reset() noexcept
        {
            keys.fill({});
            sounding.fill(0);
            pendingCount = 0;
            learnKeyDown.fill(false);
            learnCollected.fill(false);
            learnHeld = learnCount = 0;
        }

        /**
            Expands one chunk's events. Offsets written are relative to the chunk, in 0..numSamples-1,
            never decreasing. `inputsBefore[i]` is how many of `in` had been read when out[i] was written.
            Returns the number written; both spans must hold maxEventsPerInput events per incoming
            event plus maxPending.
        */
        int process(std::span<const EngineMidiEvent> in, int offsetBase, int numSamples, const ChordSettings& settings,
                    std::span<EngineMidiEvent> out, std::span<std::uint16_t> inputsBefore) noexcept
        {
            Writer writer { out, inputsBefore, 0 };
            if (numSamples <= 0)
                return 0;

            if (settings.learn && ! learning)
            {
                learnKeyDown.fill(false);
                learnCollected.fill(false);
                learnHeld = learnCount = 0;
            }
            learning = settings.learn;

            for (std::size_t j = 0; j < in.size(); ++j)
            {
                const auto& event = in[j];
                const int offset = std::clamp(event.sampleOffset - offsetBase, 0, numSamples - 1);
                firePending(offset, static_cast<int>(j), writer);

                const int inputs = static_cast<int>(j) + 1;
                const int key = std::min(127, static_cast<int>(event.noteNumber));
                switch (event.type)
                {
                    case EngineMidiEventType::noteOn:
                        keyDown(key, event.value, offset, inputs, settings, writer);
                        break;
                    case EngineMidiEventType::noteOff:
                        keyUp(key, event.value, offset, inputs, writer);
                        break;
                    case EngineMidiEventType::allNotesOff:
                    case EngineMidiEventType::allSoundOff:
                        reset();
                        [[fallthrough]];
                    default:
                    {
                        auto passed = event;
                        passed.sampleOffset = offset;
                        writer.write(passed, inputs);
                        break;
                    }
                }
            }
            firePending(numSamples - 1, static_cast<int>(in.size()), writer);
            for (int i = 0; i < pendingCount; ++i)
                pending[static_cast<std::size_t>(i)].due -= numSamples;
            return writer.count;
        }

        /** The chord just learned, packed (see packLearnedChord), or 0. Reading clears it. */
        [[nodiscard]] std::uint32_t consumeLearned() noexcept
        {
            const auto result = learnedResult;
            learnedResult = 0;
            return result;
        }

        [[nodiscard]] int pendingCountForTesting() const noexcept { return pendingCount; }
        [[nodiscard]] int soundingCountForTesting(int note) const noexcept { return sounding[static_cast<std::size_t>(std::clamp(note, 0, 127))]; }

    private:
        struct Writer
        {
            std::span<EngineMidiEvent> out;
            std::span<std::uint16_t> inputsBefore;
            int count = 0;

            void write(const EngineMidiEvent& event, int inputs) noexcept
            {
                if (count >= static_cast<int>(std::min(out.size(), inputsBefore.size())))
                    return;
                out[static_cast<std::size_t>(count)] = event;
                inputsBefore[static_cast<std::size_t>(count)] = static_cast<std::uint16_t>(std::clamp(inputs, 0, 65535));
                ++count;
            }

            void note(EngineMidiEventType type, int note, float value, int offset, int inputs) noexcept
            {
                EngineMidiEvent event;
                event.type = type;
                event.sampleOffset = offset;
                event.noteNumber = static_cast<std::uint8_t>(note);
                event.value = value;
                write(event, inputs);
            }
        };

        struct Key
        {
            std::array<std::uint8_t, maxChordNotes> notes {}; // in the order they are struck
            std::uint8_t count = 0;
            std::uint8_t started = 0; // one bit per note: sounding, counted in `sounding`
        };

        struct Pending
        {
            int due = 0; // host samples from the start of the chunk
            float velocity = 0.0f;
            std::uint8_t key = 0;
            std::uint8_t slot = 0;
            std::uint8_t note = 0;
        };

        void keyDown(int k, float velocity, int offset, int inputs, const ChordSettings& settings, Writer& writer) noexcept
        {
            auto& key = keys[static_cast<std::size_t>(k)];

            if (learning)
            {
                if (! learnKeyDown[static_cast<std::size_t>(k)])
                {
                    learnKeyDown[static_cast<std::size_t>(k)] = true;
                    ++learnHeld;
                }
                if (! learnCollected[static_cast<std::size_t>(k)])
                {
                    learnCollected[static_cast<std::size_t>(k)] = true;
                    ++learnCount;
                }
            }

            const ChordShape shape = learning ? ChordShape {} : chordShape(settings);
            std::array<std::uint8_t, maxChordNotes> notes {};
            int count = 0;
            for (int i = 0; i < shape.count; ++i)
                if (const int note = k + shape.semitones[static_cast<std::size_t>(i)]; note <= 127)
                    notes[static_cast<std::size_t>(count++)] = static_cast<std::uint8_t>(note);
            if (settings.strumMs < 0.0f)
                std::reverse(notes.begin(), notes.begin() + count);

            // A key struck again without a release in between: it keeps the notes the new chord
            // shares with the old one and lets the others go.
            std::uint8_t kept = 0;
            if (key.count > 0)
            {
                cancelPending(k);
                for (int i = 0; i < key.count; ++i)
                {
                    if ((key.started & (1u << i)) == 0)
                        continue;
                    const auto old = key.notes[static_cast<std::size_t>(i)];
                    const auto found = std::find(notes.begin(), notes.begin() + count, old);
                    if (found != notes.begin() + count)
                        kept = static_cast<std::uint8_t>(kept | (1u << static_cast<unsigned>(found - notes.begin())));
                    else
                        release(old, 0.0f, offset, inputs, writer);
                }
            }

            key.notes = notes;
            key.count = static_cast<std::uint8_t>(count);
            key.started = kept;

            const double step = std::abs(static_cast<double>(settings.strumMs)) * rate / 1000.0;
            for (int m = 0; m < count; ++m)
            {
                const int delay = static_cast<int>(std::lround(step * m));
                const auto note = notes[static_cast<std::size_t>(m)];
                if (delay <= 0 || pendingCount >= maxPending)
                {
                    if ((key.started & (1u << m)) == 0)
                    {
                        key.started = static_cast<std::uint8_t>(key.started | (1u << m));
                        ++sounding[note];
                    }
                    writer.note(EngineMidiEventType::noteOn, note, velocity, offset, inputs);
                }
                else
                {
                    pending[static_cast<std::size_t>(pendingCount++)] = { offset + delay, velocity, static_cast<std::uint8_t>(k),
                                                                         static_cast<std::uint8_t>(m), note };
                }
            }
        }

        void keyUp(int k, float releaseVelocity, int offset, int inputs, Writer& writer) noexcept
        {
            if (learnKeyDown[static_cast<std::size_t>(k)])
            {
                learnKeyDown[static_cast<std::size_t>(k)] = false;
                if (--learnHeld <= 0)
                {
                    learnHeld = 0;
                    if (learning && learnCount > 0)
                        finishLearning();
                }
            }

            auto& key = keys[static_cast<std::size_t>(k)];
            if (key.count == 0)
            {
                // Not a key of ours (pressed before a panic, or never): pass it on unless a chord holds that note.
                if (sounding[static_cast<std::size_t>(k)] == 0)
                    writer.note(EngineMidiEventType::noteOff, k, releaseVelocity, offset, inputs);
                return;
            }
            cancelPending(k);
            for (int i = 0; i < key.count; ++i)
                if ((key.started & (1u << i)) != 0)
                    release(key.notes[static_cast<std::size_t>(i)], releaseVelocity, offset, inputs, writer);
            key = {};
        }

        void release(std::uint8_t note, float releaseVelocity, int offset, int inputs, Writer& writer) noexcept
        {
            if (sounding[note] > 0 && --sounding[note] == 0)
                writer.note(EngineMidiEventType::noteOff, note, releaseVelocity, offset, inputs);
        }

        /** Strikes the waiting notes due up to and including `limit`, earliest first. */
        void firePending(int limit, int inputs, Writer& writer) noexcept
        {
            for (;;)
            {
                int best = -1;
                for (int i = 0; i < pendingCount; ++i)
                    if (pending[static_cast<std::size_t>(i)].due <= limit
                        && (best < 0 || pending[static_cast<std::size_t>(i)].due < pending[static_cast<std::size_t>(best)].due))
                        best = i;
                if (best < 0)
                    return;

                const auto p = pending[static_cast<std::size_t>(best)];
                std::copy(pending.begin() + best + 1, pending.begin() + pendingCount, pending.begin() + best);
                --pendingCount;

                auto& key = keys[p.key];
                if ((key.started & (1u << p.slot)) == 0)
                {
                    key.started = static_cast<std::uint8_t>(key.started | (1u << p.slot));
                    ++sounding[p.note];
                }
                writer.note(EngineMidiEventType::noteOn, p.note, p.velocity, std::max(0, p.due), inputs);
            }
        }

        void cancelPending(int k) noexcept
        {
            int kept = 0;
            for (int i = 0; i < pendingCount; ++i)
                if (pending[static_cast<std::size_t>(i)].key != k)
                    pending[static_cast<std::size_t>(kept++)] = pending[static_cast<std::size_t>(i)];
            pendingCount = kept;
        }

        void finishLearning() noexcept
        {
            int root = -1;
            std::array<int, learnedIntervalCount> intervals {};
            std::size_t used = 0;
            for (int note = 0; note < 128; ++note)
            {
                if (! learnCollected[static_cast<std::size_t>(note)])
                    continue;
                if (root < 0)
                    root = note;
                else if (note - root <= maxLearnedInterval && used < intervals.size())
                    intervals[used++] = note - root;
            }
            learnedResult = packLearnedChord(intervals);
            learnCollected.fill(false);
            learnCount = 0;
        }

        double rate = 48000.0;
        std::array<Key, 128> keys {};
        std::array<std::uint8_t, 128> sounding {}; // how many keys hold each note
        std::array<Pending, maxPending> pending {};
        int pendingCount = 0;

        bool learning = false;
        std::array<bool, 128> learnKeyDown {};
        std::array<bool, 128> learnCollected {};
        int learnHeld = 0, learnCount = 0;
        std::uint32_t learnedResult = 0;
    };
}
