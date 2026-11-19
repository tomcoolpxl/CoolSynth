#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    Drops a note that reaches CoolSynth twice by two routes (D216). FL Studio hands a plugin whose wrapper has a MIDI
    port every note of that port, and the same note again through its routing to the selected channel, at the same
    moment and not always on the same channel. A second note-on for the same note within 3 ms is that copy: it is
    dropped, and so is one of its note-offs. A note the controller took (a pad) counts as the first copy, so its
    copy by the other route does not play either. No hand plays one key twice within 3 ms. Audio thread only.
*/
namespace coolsynth::midi
{
    class DuplicateNoteFilter final
    {
    public:
        void prepare(double sampleRate) noexcept
        {
            window = std::max<std::int64_t>(1, static_cast<std::int64_t>(sampleRate * 0.003 + 0.5));
            reset();
        }

        void reset() noexcept
        {
            lastOn.fill(never);
            offsToDrop.fill(0);
        }

        /** A note-on the panel took (a pad): another copy of it within the window is a duplicate. */
        void noteTaken(int note, std::int64_t time) noexcept
        {
            if (note >= 0 && note < 128)
                lastOn[static_cast<std::size_t>(note)] = time;
        }

        /** True when the message is the second copy of a note (or one of its releases) and must be dropped. `time`
            counts samples from the start of playing. */
        [[nodiscard]] bool isDuplicate(const juce::MidiMessage& message, std::int64_t time) noexcept
        {
            if (! message.isNoteOnOrOff())
                return false;
            const auto n = static_cast<std::size_t>(message.getNoteNumber());
            if (message.isNoteOn())
            {
                if (lastOn[n] != never && time - lastOn[n] >= 0 && time - lastOn[n] <= window)
                {
                    if (offsToDrop[n] < 255)
                        ++offsToDrop[n];
                    return true;
                }
                lastOn[n] = time;
                return false;
            }
            if (offsToDrop[n] > 0)
            {
                --offsToDrop[n]; // the copy's release: the original's own release still comes
                return true;
            }
            return false;
        }

    private:
        static constexpr std::int64_t never = std::numeric_limits<std::int64_t>::min();
        std::int64_t window = 144; // 3 ms at 48 kHz
        std::array<std::int64_t, 128> lastOn {};
        std::array<std::uint8_t, 128> offsToDrop {};
    };
}
