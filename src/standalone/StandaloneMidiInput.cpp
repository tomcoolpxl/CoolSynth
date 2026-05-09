#include "StandaloneMidiInput.h"

namespace coolsynth::standalone
{
    StandaloneMidiInputController::StandaloneMidiInputController(juce::AudioDeviceManager& dm,
                                                                 juce::PropertySet* s,
                                                                 ControllerEventHandler onControllerEvent,
                                                                 DisconnectCallback onDisconnected)
        : deviceManager(dm)
        , settings(s)
        , onControllerEvent(std::move(onControllerEvent))
        , onSelectedDeviceDisconnected(std::move(onDisconnected))
    {
        deviceListConnection = juce::MidiDeviceListConnection::make([this] 
        { 
            deviceRefreshPending = true;
            triggerAsyncUpdate(); 
        });
        
        deviceManager.addMidiInputDeviceCallback({}, this);
        
        refreshDevices(RefreshReason::initialLoad);
    }

    StandaloneMidiInputController::~StandaloneMidiInputController()
    {
        deviceManager.removeMidiInputDeviceCallback({}, this);
    }

    LastMidiEventSnapshot StandaloneMidiInputController::getLastMidiEventSnapshot() const noexcept
    {
        LastMidiEventSnapshot snap;
        snap.eventOrder = lastMidiEventState.eventOrder.load(std::memory_order_acquire);
        snap.hasEvent = (snap.eventOrder > 0);
        
        if (snap.hasEvent)
        {
            snap.type = static_cast<coolsynth::midi::MidiMonitorMessageType>(lastMidiEventState.type.load(std::memory_order_relaxed));
            snap.channel = lastMidiEventState.channel.load(std::memory_order_relaxed);
            snap.primaryValue = lastMidiEventState.primaryValue.load(std::memory_order_relaxed);
            snap.secondaryValue = lastMidiEventState.secondaryValue.load(std::memory_order_relaxed);
            snap.noteNumber = lastMidiEventState.noteNumber.load(std::memory_order_relaxed);
            snap.controllerNumber = lastMidiEventState.controllerNumber.load(std::memory_order_relaxed);
        }
        
        return snap;
    }

    void StandaloneMidiInputController::refreshDevices()
    {
        deviceRefreshPending = true;
        triggerAsyncUpdate();
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
        updateLastMidiEventSnapshot(message);
        enqueueControllerEvent(message);
    }

    void StandaloneMidiInputController::updateLastMidiEventSnapshot(const juce::MidiMessage& message) noexcept
    {
        using namespace coolsynth::midi;
        
        uint8_t type = 0;
        int primary = 0;
        int secondary = 0;
        int noteNum = -1;
        int ccNum = -1;
        
        if (message.isNoteOn())
        {
            type = static_cast<uint8_t>(MidiMonitorMessageType::noteOn);
            noteNum = message.getNoteNumber();
            primary = noteNum;
            secondary = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            type = static_cast<uint8_t>(MidiMonitorMessageType::noteOff);
            noteNum = message.getNoteNumber();
            primary = noteNum;
            secondary = message.getVelocity();
        }
        else if (message.isController())
        {
            type = static_cast<uint8_t>(MidiMonitorMessageType::controlChange);
            ccNum = message.getControllerNumber();
            primary = ccNum;
            secondary = message.getControllerValue();
        }
        else
        {
            return;
        }
        
        lastMidiEventState.type.store(type, std::memory_order_relaxed);
        lastMidiEventState.channel.store(static_cast<uint8_t>(message.getChannel()), std::memory_order_relaxed);
        lastMidiEventState.primaryValue.store(primary, std::memory_order_relaxed);
        lastMidiEventState.secondaryValue.store(secondary, std::memory_order_relaxed);
        lastMidiEventState.noteNumber.store(noteNum, std::memory_order_relaxed);
        lastMidiEventState.controllerNumber.store(ccNum, std::memory_order_relaxed);
        
        // Publish event order last
        lastMidiEventState.eventOrder.fetch_add(1, std::memory_order_release);
    }

    void StandaloneMidiInputController::handleAsyncUpdate()
    {
        if (deviceRefreshPending.exchange(false))
        {
            refreshDevices(RefreshReason::deviceListChanged);
        }

        std::array<coolsynth::midi::ControllerMidiEvent, 32> localEvents;
        const int drained = drainControllerEvents(localEvents.data(), static_cast<int>(localEvents.size()));
        
        if (onControllerEvent != nullptr)
        {
            for (int i = 0; i < drained; ++i)
                onControllerEvent(localEvents[i]);
        }

        // If there are more events, trigger again
        if (pendingControllerEventQueue.getNumReady() > 0)
            triggerAsyncUpdate();
    }

    void StandaloneMidiInputController::enqueueControllerEvent(const juce::MidiMessage& message) noexcept
    {
        using namespace coolsynth::midi;

        ControllerMidiEvent event;
        if (message.isNoteOn())
        {
            event.type = ControllerMidiEventType::noteOn;
            event.data1 = static_cast<uint8_t>(message.getNoteNumber());
            event.data2 = static_cast<uint8_t>(message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            event.type = ControllerMidiEventType::noteOff;
            event.data1 = static_cast<uint8_t>(message.getNoteNumber());
            event.data2 = static_cast<uint8_t>(message.getVelocity());
        }
        else if (message.isController())
        {
            event.type = ControllerMidiEventType::controlChange;
            event.data1 = static_cast<uint8_t>(message.getControllerNumber());
            event.data2 = static_cast<uint8_t>(message.getControllerValue());
        }
        else
        {
            return;
        }

        event.channel = static_cast<uint8_t>(message.getChannel());

        int start1, size1, start2, size2;
        pendingControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);
        
        if (size1 > 0)
        {
            pendingControllerEvents[static_cast<size_t>(start1)] = event;
            pendingControllerEventQueue.finishedWrite(1);
            triggerAsyncUpdate();
        }
    }

    int StandaloneMidiInputController::drainControllerEvents(coolsynth::midi::ControllerMidiEvent* destination, int maxEvents) noexcept
    {
        int start1, size1, start2, size2;
        pendingControllerEventQueue.prepareToRead(maxEvents, start1, size1, start2, size2);
        
        int totalRead = 0;
        for (int i = 0; i < size1; ++i)
            destination[totalRead++] = pendingControllerEvents[static_cast<size_t>(start1 + i)];
        
        for (int i = 0; i < size2; ++i)
            destination[totalRead++] = pendingControllerEvents[static_cast<size_t>(start2 + i)];
        
        pendingControllerEventQueue.finishedRead(totalRead);
        return totalRead;
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
