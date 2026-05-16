#include "StandaloneMidiInput.h"

#include "midi/MidiToControllerEvent.h"

namespace coolsynth::standalone
{
    StandaloneMidiInputController::StandaloneMidiInputController(juce::AudioDeviceManager& dm,
                                                                 StandaloneSettingsStore* s,
                                                                 ControllerEventHandler onControllerEvent,
                                                                 DisconnectCallback onDisconnected,
                                                                 AvailableDevicesProvider devicesProvider,
                                                                 DeviceEnableHandler enableHandler)
        : deviceManager(dm)
        , settingsStore(s)
        , onControllerEvent(std::move(onControllerEvent))
        , availableDevicesProvider(std::move(devicesProvider))
        , deviceEnableHandler(std::move(enableHandler))
        , onSelectedDeviceDisconnected(std::move(onDisconnected))
    {
        if (deviceEnableHandler == nullptr)
        {
            deviceEnableHandler = [this](const juce::String& identifier, bool shouldBeEnabled)
            {
                deviceManager.setMidiInputDeviceEnabled(identifier, shouldBeEnabled);
            };
        }

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

        bool found = false;
        juce::String foundName;
        for (const auto& info : snapshot.availableInputs)
        {
            if (info.identifier == deviceIdentifier)
            {
                foundName = info.name;
                found = true;
                break;
            }
        }

        if (!found)
            return false;

        snapshot.selectedDeviceIdentifier = deviceIdentifier;
        snapshot.selectedDeviceName = foundName;

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
        const auto event = coolsynth::midi::toControllerMidiEvent(message);
        if (! event)
            return;

        int start1, size1, start2, size2;
        pendingControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            pendingControllerEvents[static_cast<size_t>(start1)] = *event;
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
        snapshot.availableInputs = availableDevicesProvider();

        if (reason == RefreshReason::initialLoad && settingsStore != nullptr)
        {
            if (auto persisted = settingsStore->loadPersistedMidiInputSelection())
            {
                snapshot.selectedDeviceIdentifier = persisted->identifier;
                snapshot.selectedDeviceName = persisted->name;
            }
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
        const bool didDisconnect = wasConnected && !isConnectedNow;

        if (didDisconnect)
        {
            clearDisconnectTransientState();
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
            else if (wasConnected)
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
            if (wasConnected)
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
            deviceEnableHandler(info.identifier, shouldBeEnabled);
        }
    }

    void StandaloneMidiInputController::persistSelection() const
    {
        if (settingsStore != nullptr)
        {
            juce::MidiDeviceInfo info;
            info.identifier = snapshot.selectedDeviceIdentifier;
            info.name = snapshot.selectedDeviceName;
            settingsStore->savePersistedMidiInputSelection(info);
        }
    }

    void StandaloneMidiInputController::clearPersistedSelection() const
    {
        if (settingsStore != nullptr)
        {
            settingsStore->clearPersistedMidiInputSelection();
        }
    }

    void StandaloneMidiInputController::clearPendingControllerEvents() noexcept
    {
        std::array<coolsynth::midi::ControllerMidiEvent, 32> discardedEvents {};

        while (drainControllerEvents(discardedEvents.data(), static_cast<int>(discardedEvents.size())) > 0)
        {
        }
    }

    void StandaloneMidiInputController::resetLastMidiEventSnapshot() noexcept
    {
        lastMidiEventState.type.store(static_cast<uint8_t>(coolsynth::midi::MidiMonitorMessageType::noteOn), std::memory_order_relaxed);
        lastMidiEventState.channel.store(0, std::memory_order_relaxed);
        lastMidiEventState.primaryValue.store(0, std::memory_order_relaxed);
        lastMidiEventState.secondaryValue.store(0, std::memory_order_relaxed);
        lastMidiEventState.noteNumber.store(-1, std::memory_order_relaxed);
        lastMidiEventState.controllerNumber.store(-1, std::memory_order_relaxed);
        lastMidiEventState.eventOrder.store(0, std::memory_order_release);
    }

    void StandaloneMidiInputController::clearDisconnectTransientState() noexcept
    {
        clearPendingControllerEvents();
        resetLastMidiEventSnapshot();
    }
}
