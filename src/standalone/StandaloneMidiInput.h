#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "midi/MidiMonitor.h"

namespace coolsynth::standalone
{
    inline constexpr char midiInputIdentifierPropertyKey[] = "midiInputIdentifier";
    inline constexpr char midiInputNamePropertyKey[] = "midiInputName";

    enum class MidiInputStatus
    {
        noDevicesAvailable,
        noSelection,
        connected,
        rememberedDeviceUnavailable,
        disconnected,
    };

    struct MidiInputSnapshot
    {
        bool runningInStandalone = false;
        juce::Array<juce::MidiDeviceInfo> availableInputs;
        juce::String selectedDeviceIdentifier;
        juce::String selectedDeviceName;
        bool selectedDevicePresent = false;
        MidiInputStatus status = MidiInputStatus::noSelection;
        juce::String statusMessage;
    };

    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        using DisconnectCallback = std::function<void()>;

        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer,
                                      DisconnectCallback onSelectedDeviceDisconnected = {});
        ~StandaloneMidiInputController() override;

        const MidiInputSnapshot& getSnapshot() const noexcept { return snapshot; }
        void refreshDevices();
        bool selectDeviceByIdentifier(const juce::String& deviceIdentifier);
        void clearSelection();

    private:
        enum class RefreshReason
        {
            initialLoad,
            userSelection,
            deviceListChanged,
        };

        void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
        void handleAsyncUpdate() override;

        void refreshDevices(RefreshReason reason);
        void applyOneDeviceEnabledPolicy();
        void disableAllAvailableDevices();
        void persistSelection() const;
        void clearPersistedSelection() const;

        juce::AudioDeviceManager& deviceManager;
        juce::PropertySet* settings = nullptr;
        coolsynth::midi::MidiMonitorBuffer& monitorBuffer;
        juce::MidiDeviceListConnection deviceListConnection;
        MidiInputSnapshot snapshot;
        DisconnectCallback onSelectedDeviceDisconnected;
        bool selectedDeviceWasPresent = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputController)
    };
}
