#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

class SynthAudioProcessor;

/*
    Drives a connected Arturia MiniLab 3 (docs/interface-v3.md section 5): opens its DAW port ("Minilab3 MIDI") for
    input and output, switches it to the DAW program, turns its knobs, main knob, pads and faders into InterfaceModel
    actions, and keeps its screen and pad colours in step with the model. Finds the device again after it is
    unplugged and plugged back in. Message thread.

    In the app (D184) it also passes the MiniLab's keys, bend, mod wheel and pressure on to the sound. In a plugin
    (D211) the host brings the keys, and every CoolSynth there drops the MiniLab's control messages the host passes on
    as well, so a pad plays no note. One CoolSynth drives the MiniLab at a time: the one whose window opened last, the
    first one loaded until then; between programs, the first one to take it keeps it until it closes. Windows shares
    the port between programs through Windows MIDI Services (Windows 11 24H2 and later).
*/
namespace coolsynth::ui
{
    class InterfaceModel;

    class MiniLab3Driver final : private juce::MidiInputCallback,
                                 private juce::Timer
    {
    public:
        /** devices: the app's own, which brings the keys (D159); null in a plugin, where the host brings them. */
        MiniLab3Driver(InterfaceModel& model, SynthAudioProcessor& synth, juce::AudioDeviceManager* devices);
        ~MiniLab3Driver() override;

        [[nodiscard]] bool isConnected() const noexcept { return output != nullptr && input != nullptr; }

        /** This CoolSynth's window opened: it takes the MiniLab from any other CoolSynth in this program (D211). */
        void claim();

        /** Audio thread: a CoolSynth plugin in this program drives the MiniLab, so the MiniLab's control messages that
            the host passes on are not the sound's (D211). */
        [[nodiscard]] static bool drivenInPlugin() noexcept { return pluginDriving.load(std::memory_order_relaxed); }


    private:
        void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
        void timerCallback() override;

        void tryOpen();
        void close(bool sayGoodbye);
        void letGo();
        void giveUp();
        [[nodiscard]] bool takeDevice();
        void apply(const juce::MidiMessage& message);
        void refreshDevice(bool force);
        void send(const juce::MidiMessage& message);
        void setConnected(bool connected);

        InterfaceModel& model;
        SynthAudioProcessor& synth;
        juce::AudioDeviceManager* devices = nullptr;
        const bool inPlugin;

        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        juce::String inputIdentifier;

        // JUCE 9 says when a port it opened is gone, and whether a message got out: either marks the ports lost, and
        // the next timer tick drops them (not inside the port's own call). Message thread.
        struct PortWatch final : juce::ump::DisconnectionListener
        {
            explicit PortWatch(bool& lostFlag) : lost(lostFlag) {}
            void disconnected() override { lost = true; }
            bool& lost;
        };
        bool portLost = false;
        PortWatch portWatch { portLost };
        void dropPorts();

        juce::CriticalSection queueLock;
        std::vector<juce::MidiMessage> queue;   // control messages from the MIDI thread

        // The MiniLab 3 firmware now and then sends a key's note-on with velocity 1 (Arturia forum, "Random
        // Velocity=1 bug"): such a note takes the previous note's velocity instead. MIDI thread only.
        int lastGoodVelocity = 100;
        // The last key the MiniLab sent, for the screen's line over its keys (D192): MIDI thread writes, timer reads.
        std::atomic<int> lastKeyNote { -1 };

        bool shiftHeld = false;
        std::array<int, 8> lastKnobValue { -1, -1, -1, -1, -1, -1, -1, -1 };
        juce::String sentLine1, sentLine2;
        int sentPosition = -1;
        std::array<juce::uint32, 8> sentPadColours {};
        int sentPopupSerial = 0;
        // A pad's flash (D191): its name on the full screen for a moment, the scroll screen held back meanwhile.
        static constexpr juce::uint32 padFlashMilliseconds = 1000;
        juce::uint32 flashUntil = 0;
        bool flashing = false;
        juce::String flashLine1, flashLine2; // the scroll screen the pad left: another change ends the flash
        int flashPosition = -1;
        int ticksSinceProbe = 0;
        bool wasConnected = false;
        bool switchedOff = false;    // the driver switched off in Preferences: the port is the app's MIDI input (D211)

        // The device list, read only when Windows says it changed (D216).
        juce::MidiDeviceListConnection deviceList;
        std::atomic<bool> devicesChanged { true };
        bool devicePresent = false;
        bool portBusy = false;       // present, but another program holds the port (no Windows MIDI Services)
        bool busyAnnounced = false;
        static inline bool liveChannelHintShown = false; // Live's channel-1 hint, once per program (D216)
        [[nodiscard]] static bool miniLabPresent();

        // Who drives the MiniLab (D211): one driver in this program, and this program among others.
        static inline MiniLab3Driver* owner = nullptr;
        static inline std::atomic<bool> pluginDriving { false };
        static juce::InterProcessLock& programLock();
        static inline bool programHoldsLock = false;
    };
}
