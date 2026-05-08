#include "StandaloneMidiInput.h"

namespace coolsynth::standalone
{
    StandaloneMidiInputController::StandaloneMidiInputController(juce::AudioDeviceManager& dm,
                                                                 juce::PropertySet* s,
                                                                 coolsynth::midi::MidiMonitorBuffer& mb,
                                                                 DisconnectCallback onDisconnected)
        : deviceManager(dm)
        , settings(s)
        , monitorBuffer(mb)
        , onSelectedDeviceDisconnected(std::move(onDisconnected))
    {
        deviceListConnection = juce::MidiDeviceListConnection::make([this] { triggerAsyncUpdate(); });
        
        deviceManager.addMidiInputDeviceCallback({}, this);
        
        refreshDevices(RefreshReason::initialLoad);
    }

    StandaloneMidiInputController::~StandaloneMidiInputController()
    {
        deviceManager.removeMidiInputDeviceCallback({}, this);
    }

    void StandaloneMidiInputController::refreshDevices()
    {
        refreshDevices(RefreshReason::deviceListChanged);
    }

    bool StandaloneMidiInputController::selectDeviceByIdentifier(const juce::String& deviceIdentifier)
    {
        if (deviceIdentifier.isEmpty())
        {
            clearSelection();
            return true;
        }

        snapshot.selectedDeviceIdentifier = deviceIdentifier;
        
        // Find the name for persistence
        for (const auto& info : snapshot.availableInputs)
        {
            if (info.identifier == deviceIdentifier)
            {
                snapshot.selectedDeviceName = info.name;
                break;
            }
        }

        persistSelection();
        refreshDevices(RefreshReason::userSelection);
        return snapshot.selectedDevicePresent;
    }

    void StandaloneMidiInputController::clearSelection()
    {
        snapshot.selectedDeviceIdentifier = {};
        snapshot.selectedDeviceName = {};
        clearPersistedSelection();
        refreshDevices(RefreshReason::userSelection);
    }

    void StandaloneMidiInputController::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
    {
        monitorBuffer.pushMessage(message, juce::Time::getMillisecondCounterHiRes() * 0.001);
    }

    void StandaloneMidiInputController::handleAsyncUpdate()
    {
        refreshDevices(RefreshReason::deviceListChanged);
    }

    void StandaloneMidiInputController::refreshDevices(RefreshReason reason)
    {
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.availableInputs = juce::MidiInput::getAvailableDevices();

        if (reason == RefreshReason::initialLoad && settings != nullptr)
        {
            snapshot.selectedDeviceIdentifier = settings->getValue(midiInputIdentifierPropertyKey);
            snapshot.selectedDeviceName = settings->getValue(midiInputNamePropertyKey);
        }

        snapshot.selectedDevicePresent = false;
        if (snapshot.selectedDeviceIdentifier.isNotEmpty())
        {
            for (const auto& info : snapshot.availableInputs)
            {
                if (info.identifier == snapshot.selectedDeviceIdentifier)
                {
                    snapshot.selectedDevicePresent = true;
                    break;
                }
            }
        }

        applyOneDeviceEnabledPolicy();

        const bool wasConnected = selectedDeviceWasPresent;
        const bool isConnectedNow = snapshot.selectedDevicePresent;

        if (wasConnected && !isConnectedNow)
        {
            selectedDeviceWasPresent = false;
            if (onSelectedDeviceDisconnected)
                onSelectedDeviceDisconnected();
        }
        else if (isConnectedNow)
        {
            selectedDeviceWasPresent = true;
        }

        // Determine status
        if (snapshot.availableInputs.isEmpty())
        {
            if (snapshot.selectedDeviceIdentifier.isEmpty())
            {
                snapshot.status = MidiInputStatus::noDevicesAvailable;
                snapshot.statusMessage = "No MIDI input devices found";
            }
            else
            {
                snapshot.status = MidiInputStatus::rememberedDeviceUnavailable;
                snapshot.statusMessage = "Device unavailable: " + snapshot.selectedDeviceName;
            }
        }
        else if (snapshot.selectedDeviceIdentifier.isEmpty())
        {
            snapshot.status = MidiInputStatus::noSelection;
            snapshot.statusMessage = "No MIDI input device selected";
        }
        else if (snapshot.selectedDevicePresent)
        {
            snapshot.status = MidiInputStatus::connected;
            snapshot.statusMessage = "Connected to " + snapshot.selectedDeviceName;
            selectedDeviceWasPresent = true;
        }
        else
        {
            if (selectedDeviceWasPresent)
            {
                snapshot.status = MidiInputStatus::disconnected;
                snapshot.statusMessage = "Disconnected: " + snapshot.selectedDeviceName;
            }
            else
            {
                snapshot.status = MidiInputStatus::rememberedDeviceUnavailable;
                snapshot.statusMessage = "Device unavailable: " + snapshot.selectedDeviceName;
            }
        }

        sendChangeMessage();
    }

    void StandaloneMidiInputController::applyOneDeviceEnabledPolicy()
    {
        // Disable all first to ensure only one is active
        for (const auto& info : snapshot.availableInputs)
        {
            bool shouldBeEnabled = (info.identifier == snapshot.selectedDeviceIdentifier);
            deviceManager.setMidiInputDeviceEnabled(info.identifier, shouldBeEnabled);
        }
    }

    void StandaloneMidiInputController::persistSelection() const
    {
        if (settings != nullptr)
        {
            settings->setValue(midiInputIdentifierPropertyKey, snapshot.selectedDeviceIdentifier);
            settings->setValue(midiInputNamePropertyKey, snapshot.selectedDeviceName);
        }
    }

    void StandaloneMidiInputController::clearPersistedSelection() const
    {
        if (settings != nullptr)
        {
            settings->removeValue(midiInputIdentifierPropertyKey);
            settings->removeValue(midiInputNamePropertyKey);
        }
    }
}
