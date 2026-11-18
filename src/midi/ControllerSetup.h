#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "ControllerMap.h"

/*
    The computer's controller setup (D211): one map for the app and every CoolSynth in every host, kept in
    %APPDATA%\CoolSynth\controller.json, as the velocity curve is the computer's (D175). With none saved, a controller
    plugged in that CoolSynth knows brings its ready profile, and the screen says so once. Learning (the guided
    setup, a right click's Learn) listens to every CoolSynth in this process for the next control moved.
    Message thread, apart from the audio thread's two questions, which read atomics only.
*/
namespace coolsynth::midi
{
    /** A controller's message as the mapping sees it. */
    struct ControllerEvent
    {
        ControlId control;
        int value = 0;     // the controller value; a note's velocity, 0 for its release
        bool note = false; // a note-on or note-off
    };

    /** Notes and controllers only; anything else is not a controller's. */
    [[nodiscard]] std::optional<ControllerEvent> toControllerEvent(const juce::MidiMessage& message) noexcept;

    class ControllerSetup final : private juce::Timer
    {
    public:
        static ControllerSetup& instance();

        /** Reads the computer's setup and starts watching it and the MIDI inputs; once, from any CoolSynth that plays
            (not tests and tools, which set a map with useForTesting). */
        void start();

        [[nodiscard]] const ControllerMap& map() const noexcept { return current; }
        enum class Origin { none, saved, found };
        /** Nothing set up; the player's own setup; a ready profile found plugged in and not saved. */
        [[nodiscard]] Origin origin() const noexcept { return currentOrigin; }
        /** Grows with every change of the map, here or in another program: the panel draws again. */
        [[nodiscard]] int revision() const noexcept { return changes; }

        /** The player's setup: saved for the whole computer and used at once everywhere. An empty map removes it, and
            a known controller's ready profile comes back if one is plugged in. */
        void setMap(ControllerMap map);
        /** No file and no looking at inputs: the map as given. */
        void useForTesting(ControllerMap map);

        /** A line for the screen, once: "Akai MPK mini mk3 found: its ready setup is in use (Preferences)". */
        [[nodiscard]] int noticeSerial() const noexcept { return notices; }
        [[nodiscard]] const juce::String& noticeText() const noexcept { return notice; }
        /** A line for the screen, once, from anything about the controllers (the MiniLab's driver). */
        void announce(const juce::String& text)
        {
            notice = text;
            ++notices;
        }

        /** The MiniLab 3 has CoolSynth's own driver (D184): its ready profile is not offered for it then (D211). */
        void setMiniLabDriven(bool driven) noexcept { miniLabDriven.store(driven, std::memory_order_relaxed); }
        /** The MiniLab driver may take the MiniLab (D211): off, the MiniLab is a controller like any other. Kept with
            the setup, for this computer. */
        [[nodiscard]] bool miniLabDriverOn() const noexcept { return driverOn; }
        void setMiniLabDriverOn(bool on);

        // ---- Learning -------------------------------------------------------------------------------------------
        struct Heard
        {
            ControlId control;
            Encoding encoding = Encoding::absolute;
            bool button = false;
        };
        enum class Expect { turn, press };
        /** Listens for the next control moved on any controller: a turn is judged once the control rests (it is
            asked to go right, then left), a press at once. `progress` says what was heard so far. */
        void learn(Expect expect, std::function<void(const Heard&)> done, std::function<void(const juce::String&)> progress = {});
        void stopLearning();
        [[nodiscard]] bool isLearning() const noexcept { return learning.load(std::memory_order_relaxed); }
        /** Every CoolSynth passes on what its controller sent while learning goes on. */
        void hear(const ControllerEvent& event);
        /** Tests: judges what was heard now, as if the control had rested. */
        void finishLearningNow() { finishLearning(true); }

        // ---- Audio thread ---------------------------------------------------------------------------------------
        /** A message the sound must not have: a linked control's, or any control's while learning (D211). A note's
            release always goes to the sound, so no note is ever left hanging. */
        [[nodiscard]] static bool takesFromSound(const juce::MidiMessage& message) noexcept;
        /** A message for the controller input: a linked control's, or any control's while learning. */
        [[nodiscard]] static bool wanted(const juce::MidiMessage& message) noexcept;

        [[nodiscard]] juce::File file() const;

    private:
        ControllerSetup() = default;
        void timerCallback() override;
        void publish();
        void readFile();
        void save();
        void lookForKnownController();
        void finishLearning(bool rested = false);

        ControllerMap current;
        Origin currentOrigin = Origin::none;
        int changes = 0;
        bool started = false;
        bool testing = false;
        juce::Time fileTime;
        int ticks = 0;
        juce::String foundProfile;
        int notices = 0;
        juce::String notice;
        std::atomic<bool> miniLabDriven { false };
        bool driverOn = true;

        std::atomic<bool> learning { false };
        Expect expecting = Expect::turn;
        std::function<void(const Heard&)> onHeard;
        std::function<void(const juce::String&)> onProgress;
        std::optional<ControlId> candidate;
        std::vector<int> values;
        double lastHeardMs = 0.0;
        std::unique_ptr<juce::TimedCallback> learnTimer;

        // Linked controls for the audio thread: [kind][channel 0-15] as 128 bits each (D211).
        static std::array<std::atomic<std::uint64_t>, 2 * 16 * 2> linked;
        static std::atomic<bool> learningNow;
        [[nodiscard]] static bool isLinked(const ControlId& control) noexcept;
    };
}
