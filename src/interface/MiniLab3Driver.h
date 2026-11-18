#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

namespace coolsynth::ui { class InterfaceModel; }
class SynthAudioProcessor;

/*
    Drives a connected Arturia MiniLab 3 from the standalone app (docs/interface-v3.md section 5):
    opens its DAW port ("Minilab3 MIDI") for input and output, switches it to the DAW program,
    turns its knobs, main knob, pads and faders into InterfaceModel actions, passes its keys, bend,
    mod wheel and pressure on to the sound, and keeps its screen and pad colours in step with the
    model. Finds the device again after it is unplugged and plugged back in. Message thread.
*/
namespace coolsynth::standalone
{
    class MiniLab3Driver final : private juce::MidiInputCallback,
                                 private juce::Timer
    {
    public:
        /** synth: where note and performance messages go, through its lock-free live queue (D159). */
        MiniLab3Driver(coolsynth::ui::InterfaceModel& model,
                       juce::AudioDeviceManager& deviceManager,
                       SynthAudioProcessor& synth);
        ~MiniLab3Driver() override;

        [[nodiscard]] bool isConnected() const noexcept { return output != nullptr && input != nullptr; }

    private:
        void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
        void timerCallback() override;

        void tryOpen();
        void close();
        void apply(const juce::MidiMessage& message);
        void refreshDevice(bool force);
        void send(const juce::MidiMessage& message);

        coolsynth::ui::InterfaceModel& model;
        juce::AudioDeviceManager& devices;
        SynthAudioProcessor& synth;

        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        juce::String inputIdentifier;

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
    };
}
