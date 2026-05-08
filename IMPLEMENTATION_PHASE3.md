# Phase 3 Blueprint: Standalone MIDI Input Shell and Monitor

## Phase Selection

Selected phase: `Phase 3 - Standalone MIDI input shell and monitor`

Selection basis:

- `TODO.md` shows `Phase 1` and `Phase 2` completed and no `Phase 3` work staged yet.
- `DONE.md` records `Phase 1` and `Phase 2` as verified complete.
- `IMPLEMENTATION_PLAN.md` defines `Phase 3` as the next dependency-bearing milestone after the standalone audio shell.
- The current codebase already contains the exact seams this phase should extend: a custom standalone app bootstrap, shared editor gating for standalone-only UI, and a standalone runtime helper around JUCE's `StandalonePluginHolder`.

This blueprint therefore assumes the next review cycle is the first standalone MIDI bring-up pass, not synth playback, controller mapping, or plugin-host MIDI work.

## Scope Guardrails

In scope for this phase:

- One active standalone MIDI input device at a time.
- Standalone MIDI selection UI and clear device-state reporting.
- Bounded recent-history MIDI monitor for note and CC bring-up.
- Safe handling of missing remembered devices and runtime disconnects.
- Reuse of JUCE's existing standalone MIDI routing path so later synth phases do not need to re-plumb hardware MIDI.

Explicitly out of scope for this phase:

- Any audible synth behavior or note rendering.
- Fixed MiniLab 3 CC-to-parameter mapping.
- MIDI learn, presets, panic routing, or parameter mutation from controller input.
- Plugin-editor MIDI monitor or plugin hardware-device selection.
- Pitch bend, aftertouch, poly aftertouch, and program change support.
- Editing JUCE source files under `external/JUCE`.
- Turning on JUCE's built-in multi-device MIDI input controls in the audio settings dialog.

## Source Anchors

This blueprint is grounded in the current implementation surface:

- `CMakeLists.txt` currently builds `src/standalone/StandaloneAudioSupport.cpp`, `src/standalone/CoolSynthStandaloneApp.cpp`, and `src/ui/StandaloneAudioStatusPanel.cpp` as the full standalone-specific slice.
- `src/standalone/CoolSynthStandaloneApp.cpp` already constructs `juce::StandalonePluginHolder` with the final `shouldAutoOpenMidiDevices` argument set to `false` on Windows. That is the correct baseline for the one-active-device rule.
- `src/standalone/StandaloneAudioSupport.cpp` already hides direct access to `juce::StandalonePluginHolder` behind free functions. That file is the narrowest existing seam for additional standalone runtime accessors.
- `src/plugin/SynthAudioProcessorEditor.cpp` already gates standalone-only UI using `juce::JUCEApplicationBase::isStandaloneApp()`. Phase 3 should extend that pattern rather than introducing a second editor type.
- `juce::StandalonePluginHolder` already registers `AudioProcessorPlayer` as a wildcard MIDI callback on the shared `AudioDeviceManager`. Enabling exactly one MIDI input device is therefore enough to route hardware MIDI to the shared processor in later phases without extra device-opening code.
- `SynthAudioProcessor` is still silent, which means Phase 3 can verify routing and monitor behavior without touching the shared synth core.

## Exact `TODO.md` Entries This Blueprint Expands

- [ ] Add standalone MIDI input selection for one active device at a time.
- [ ] Add standalone MIDI device status reporting.
- [ ] Add a bounded MIDI monitor that shows timestamp or order, type, channel, and message values.
- [ ] Ignore unsupported MIDI messages safely.
- [ ] Keep the app running when the selected MIDI device disconnects.
- [ ] Verify MiniLab 3 note and CC events appear in the monitor.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Use JUCE's existing standalone `AudioDeviceManager` MIDI path as the single hardware-MIDI owner, then add a standalone-only controller that:

- enumerates available MIDI inputs,
- enforces the one-enabled-device policy,
- persists the selected device identifier,
- mirrors supported incoming messages into a bounded monitor queue.

Do not open MIDI devices with `juce::MidiInput::openDevice()` in parallel.

Reason:

- `StandalonePluginHolder` already routes enabled MIDI inputs into `AudioProcessorPlayer`, which is the correct future path for shared processor MIDI.
- Opening devices separately would create two competing ownership models: one for the monitor and one for the processor.
- JUCE's `AudioDeviceSelectorComponent` MIDI options are also the wrong control surface here because they support multiple simultaneously enabled MIDI inputs, which directly violates the requirement for one active device at a time.

### 1.2 Runtime Object Graph

Phase 3 should result in this standalone-only object graph:

```text
CoolSynthStandaloneApp
  -> juce::StandalonePluginHolder
     -> juce::AudioDeviceManager
        -> AudioProcessorPlayer wildcard MIDI callback (existing JUCE path)
        -> StandaloneMidiInputController wildcard MIDI callback (new monitor path)
     -> shared SynthAudioProcessor
     -> shared SynthAudioProcessorEditor
        -> StandaloneAudioStatusPanel (existing)
        -> StandaloneMidiInputPanel (new)
           -> StandaloneMidiInputController (new)
        -> MidiMonitorPanel (new)
           -> MidiMonitorBuffer (new)
```

Ownership rules:

- `AudioDeviceManager` remains the sole owner of actual MIDI device open/close state.
- `StandaloneMidiInputController` is editor-owned and standalone-only.
- `MidiMonitorBuffer` is standalone-only and is not stored in APVTS or processor state.
- The shared processor stays free of MIDI-device selection and monitor UI responsibilities.

### 1.3 State Definitions

#### 1.3.1 MIDI Device Selection State

Use an explicit snapshot type for message-thread UI reads.

```cpp
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
}
```

State rules:

- Persist the device identifier as the source of truth because names are not guaranteed unique.
- Persist the last selected device name only for user-facing unavailable-state text.
- `rememberedDeviceUnavailable` means the stored device is missing at startup or on editor re-open before it ever reconnects in the current session.
- `disconnected` means the device was previously present and then disappeared during the current session.
- Do not silently auto-select the first available device when a remembered device is missing.

#### 1.3.2 MIDI Monitor Event Model

Store only the minimum data required by `REQUIREMENTS.md`.

```cpp
namespace coolsynth::midi
{
    enum class MidiMonitorMessageType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
    };

    struct MidiMonitorEvent
    {
        uint64_t eventOrder = 0;
        double timestampSeconds = 0.0;
        MidiMonitorMessageType type = MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
    };
}
```

Normalization rules:

- `note-on` with velocity `0` must be normalized to `noteOff` before storage.
- Only `noteOn`, `noteOff`, and `controlChange` events are stored in Phase 3.
- Unsupported messages are dropped before entering the monitor queue.
- `eventOrder` is the primary required display field because it is deterministic and cheap; `timestampSeconds` is stored for future debugging without forcing UI changes now.

#### 1.3.3 Realtime-to-UI Queue Model

Use a single-producer, single-consumer pending queue plus message-thread-owned visible history.

```cpp
namespace coolsynth::midi
{
    class MidiMonitorBuffer
    {
    public:
        static constexpr int queueCapacity = 256;
        static constexpr int visibleHistoryCapacity = 128;

        void pushMessage(const juce::MidiMessage& message, double timestampSeconds) noexcept;
        int drainPending(MidiMonitorEvent* destination, int maxEvents) noexcept;
        void clear() noexcept;

    private:
        std::atomic<uint64_t> nextEventOrder { 1 };
        juce::AbstractFifo pendingQueue { queueCapacity };
        std::array<MidiMonitorEvent, queueCapacity> pendingEvents {};
    };
}
```

Queue rules:

- `pushMessage()` must not allocate, format strings, or touch UI.
- `drainPending()` is called only on the message thread by the monitor panel timer.
- The visible table history is capped to `128` events even if the pending queue is larger.
- If the pending queue is full, prefer dropping the oldest pending event so the monitor keeps the newest activity.

### 1.4 Function Signatures and Module Boundaries

#### 1.4.1 Extend the Existing Standalone Runtime Access Seam

Do not rename `StandaloneAudioSupport` in this phase. The name is slightly narrow, but renaming it would be pure churn unrelated to the phase goal.

Required additions:

```cpp
namespace coolsynth::standalone
{
    juce::PropertySet* getStandaloneSettings() noexcept;
}
```

Responsibility:

- Let standalone-only MIDI code access the same `PropertySet` already owned by `StandalonePluginHolder`.
- Avoid including `juce_StandaloneFilterWindow.h` from UI headers.

#### 1.4.2 Standalone MIDI Controller Module

Create a dedicated non-Component controller in `src/standalone/StandaloneMidiInput.h/.cpp`.

```cpp
namespace coolsynth::standalone
{
    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer);
        ~StandaloneMidiInputController() override;

        const MidiInputSnapshot& getSnapshot() const noexcept;

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
        bool selectedDeviceWasPresent = false;
    };
}
```

Implementation rules:

- Register the controller as a wildcard callback: `deviceManager.addMidiInputDeviceCallback({}, this);`
- Enforce the one-device rule by calling `setMidiInputDeviceEnabled()` for every available device whenever selection changes or the device list changes.
- Never open MIDI devices directly.
- Use `AsyncUpdater` to marshal device-list changes back to the message thread before calling `sendChangeMessage()`.

#### 1.4.3 UI Components

Create two standalone-only UI components.

`StandaloneMidiInputPanel` owns selection UI and the controller:

```cpp
class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    StandaloneMidiInputPanel();
    ~StandaloneMidiInputPanel() override;

    coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshFromController();
    void repopulateDeviceSelector();
    void handleDeviceSelectionChanged();

    bool isRefreshingSelector = false;
    coolsynth::midi::MidiMonitorBuffer monitorBuffer;
    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> controller;

    juce::Label deviceTitleLabel;
    juce::ComboBox deviceSelector;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputPanel)
};
```

`MidiMonitorPanel` owns visible history and rendering:

```cpp
class MidiMonitorPanel final : public juce::Component,
                               private juce::TableListBoxModel,
                               private juce::Timer
{
public:
    explicit MidiMonitorPanel(coolsynth::midi::MidiMonitorBuffer& monitorBuffer);
    ~MidiMonitorPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g,
                            int rowNumber,
                            int width,
                            int height,
                            bool rowIsSelected) override;
    void paintCell(juce::Graphics& g,
                   int rowNumber,
                   int columnId,
                   int width,
                   int height,
                   bool rowIsSelected) override;
    void timerCallback() override;
    void drainPendingEvents();

    coolsynth::midi::MidiMonitorBuffer& monitorBuffer;
    juce::TableListBox table;
    juce::Array<coolsynth::midi::MidiMonitorEvent> recentEvents;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitorPanel)
};
```

UI rules:

- `StandaloneMidiInputPanel` must never format per-event monitor strings.
- `MidiMonitorPanel` must never talk directly to `AudioDeviceManager`.
- The plugin editor must not instantiate either component.

### 1.5 Selection and Persistence Algorithm

Use this exact selection policy:

1. On standalone panel construction, fetch `AudioDeviceManager*` and `PropertySet*` from the standalone runtime helpers.
2. Create `StandaloneMidiInputController` only if both are available.
3. Controller initial load:
   - Read `midiInputIdentifierPropertyKey` and `midiInputNamePropertyKey` from settings.
   - Enumerate `juce::MidiInput::getAvailableDevices()`.
   - If no devices exist:
     - leave all devices disabled,
     - show `noDevicesAvailable` if nothing was remembered,
     - show `rememberedDeviceUnavailable` if a device was remembered.
   - If a remembered identifier exists and is present:
     - enable only that device,
     - disable every other available device,
     - set state to `connected`.
   - If a remembered identifier exists and is missing:
     - disable every currently available device,
     - preserve the remembered name and identifier,
     - set state to `rememberedDeviceUnavailable`.
   - If nothing is remembered and devices exist:
     - disable every available device,
     - set state to `noSelection`.
4. When the user picks a device from the selector:
   - persist identifier and name,
   - enable exactly that device,
   - disable all others,
   - set state to `connected`.
5. When the user clears selection:
   - clear persisted keys,
   - disable every available device,
   - set state to `noSelection`.
6. When the device list changes:
   - refresh on the message thread,
   - if the selected identifier disappeared after previously being present, set state to `disconnected`,
   - if the remembered identifier reappears, auto-enable it and return to `connected` without choosing a different device.

This is the exact behavior required to satisfy both persistence and missing-device boundary conditions without guessing.

### 1.6 MIDI Monitor Capture and UI Drain Model

The monitor must be useful during bring-up but invisible to the audio/MIDI hot path.

Required capture model:

- `StandaloneMidiInputController::handleIncomingMidiMessage()` receives callbacks from the `AudioDeviceManager` for enabled devices only.
- It forwards supported messages into `MidiMonitorBuffer::pushMessage()`.
- `pushMessage()` converts JUCE messages into `MidiMonitorEvent` records and writes them into a bounded queue.
- `MidiMonitorPanel` polls the queue on the message thread with a light timer, for example `startTimerHz(20)`.
- On each timer tick, `drainPendingEvents()` copies pending events into a message-thread-owned `recentEvents` array and trims it to `visibleHistoryCapacity`.

Required displayed columns:

- `#` for event order.
- `Type` for `Note On`, `Note Off`, or `CC`.
- `Ch` for 1 to 16.
- `Data 1` for note number or CC number.
- `Data 2` for velocity or CC value.
- `Note` for note-name formatting on note events only.
- `CC` for controller number on CC events only.

Formatting rules:

- Format note names on the message thread using JUCE utilities or a small helper; never pre-build strings in the callback.
- Show `-` for non-applicable `Note` or `CC` columns.
- Default sort order is arrival order only. No sorting, filtering, or searching in Phase 3.

### 1.7 Shared Versus Standalone Boundary

Shared processor boundary for Phase 3:

- `SynthAudioProcessor` remains unchanged unless a concrete validation failure proves otherwise.
- No standalone MIDI selector state belongs in APVTS.
- No standalone MIDI monitor state belongs in APVTS.
- No controller mapping, parameter writes, or synth-engine branching belongs in this phase.

Standalone boundary for Phase 3:

- Device enumeration, selection, remembered-device handling, and monitor capture are standalone-only responsibilities.
- The editor remains the composition point for standalone-only panels.
- The plugin editor continues to show only the placeholder shared UI.

This keeps the architecture aligned with `REQUIREMENTS.md` section 4 and avoids pulling fixed-mapping work forward.

### 1.8 Explicit Design Rejections

Do not choose any of the following:

- `MidiInput::openDevice()` for the monitor while separately using `AudioDeviceManager` for the processor.
- Enabling JUCE's built-in MIDI input options inside the existing audio settings dialog.
- Auto-selecting the first available MIDI device when a remembered device is missing.
- Writing monitor strings or note names in `handleIncomingMidiMessage()`.
- Introducing a generic standalone runtime framework or service locator bigger than the current phase needs.

## 2. File-Level Strategy

Exact file set for the Phase 3 implementation:

| Path | Change Type | Responsibility |
| --- | --- | --- |
| `CMakeLists.txt` | update | Add the new standalone, MIDI, and UI source files to the `CoolSynth` target. No new targets or link libraries. |
| `src/standalone/StandaloneAudioSupport.h` | update | Expose `getStandaloneSettings()` as the existing standalone runtime access seam. |
| `src/standalone/StandaloneAudioSupport.cpp` | update | Implement `getStandaloneSettings()` using `juce::StandalonePluginHolder::getInstance()`. Keep existing audio helpers intact. |
| `src/standalone/StandaloneMidiInput.h` | new | Declare selection-state types and `StandaloneMidiInputController`. |
| `src/standalone/StandaloneMidiInput.cpp` | new | Implement MIDI device enumeration, persistence, one-enabled-device policy, hot-plug refresh, and callback-to-monitor forwarding. |
| `src/midi/MidiMonitor.h` | new | Declare `MidiMonitorEvent`, `MidiMonitorMessageType`, and `MidiMonitorBuffer`. |
| `src/midi/MidiMonitor.cpp` | new | Implement supported-message normalization, bounded pending queue writes, drain helpers, and thread-safe event-order assignment. |
| `src/ui/StandaloneMidiInputPanel.h` | new | Declare the standalone MIDI selection/status component. |
| `src/ui/StandaloneMidiInputPanel.cpp` | new | Implement controller ownership, combo-box refresh, status-label updates, and selector callbacks. |
| `src/ui/MidiMonitorPanel.h` | new | Declare the standalone monitor table component. |
| `src/ui/MidiMonitorPanel.cpp` | new | Implement table rendering, message-thread draining, and bounded visible history. |
| `src/plugin/SynthAudioProcessorEditor.h` | update | Add standalone-only component members for the MIDI selector and monitor. Keep the existing generic `std::unique_ptr<juce::Component>` style unless a local reason forces otherwise. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | update | Instantiate the new standalone-only panels, update layout, and keep plugin mode free of standalone MIDI UI. |
| `README.md` | update after verification | Document standalone MIDI selection behavior, remembered-device behavior, and monitor scope. |
| `TODO.md` | update before implementation | Refresh to the `Phase 3` checklist from `IMPLEMENTATION_PLAN.md`. |
| `DONE.md` | update after verification | Record only the verified `Phase 3` outcomes. |

Files intentionally not touched in Phase 3:

- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/parameters/*`
- `src/standalone/CoolSynthStandaloneApp.cpp`
- `src/ui/StandaloneAudioStatusPanel.*`
- Any file under `external/JUCE`

Rationale for leaving `CoolSynthStandaloneApp.cpp` alone:

- It already opts out of JUCE's auto-open MIDI behavior on Windows by passing `false` for `shouldAutoOpenMidiDevices`.
- That is the correct prerequisite for the one-active-device policy.

## 3. Atomic Execution Steps

Each Phase 3 checkbox should be implemented as its own `Plan -> Act -> Validate` cycle.

### 3.1 Checkbox: Add standalone MIDI input selection for one active device at a time

Plan:

- Reuse `AudioDeviceManager` as the single source of device enablement.
- Persist selected device by identifier, not index or display name.
- Enforce the one-device rule by disabling every other available input whenever selection is refreshed.

Act:

1. Update `TODO.md` to the Phase 3 checklist before touching code.
2. Add `src/standalone/StandaloneMidiInput.h/.cpp`.
3. Add `getStandaloneSettings()` to `StandaloneAudioSupport` so the controller can reuse the holder-owned `PropertySet`.
4. In `StandaloneMidiInputController`, register a wildcard MIDI callback with `AudioDeviceManager`.
5. On construction, enumerate `juce::MidiInput::getAvailableDevices()`, read the persisted identifier, and apply the selection algorithm from section 1.5.
6. Implement `selectDeviceByIdentifier()` so it disables all devices, then re-enables only the selected identifier.
7. Create `StandaloneMidiInputPanel` with a `ComboBox` that shows available device names and a `None` option.
8. Wire combo-box selection changes to the controller without direct `AudioDeviceManager` access in the panel.
9. Update the editor to create the new panel only in standalone mode.

Validate:

1. Build Debug.
2. Launch the standalone app with two MIDI devices connected if available.
3. Select device A and confirm only device A is enabled by behavior, not both.
4. Select device B and confirm device A stops producing monitor activity while device B begins.
5. Restart the app and confirm the last selected device is restored when still available.
6. If the remembered device is missing on restart, confirm no substitute device is auto-selected.

### 3.2 Checkbox: Add standalone MIDI device status reporting

Plan:

- Device status must distinguish `connected`, `disconnected`, and `remembered but unavailable` states.
- Status should be derived by the controller and rendered by the panel.
- Device-list change notifications must be marshalled to the message thread.

Act:

1. Add `MidiInputStatus` and `MidiInputSnapshot` to the standalone MIDI module.
2. Add a `juce::MidiDeviceListConnection` field to the controller.
3. On device-list change, trigger an async refresh rather than touching UI directly.
4. In `refreshDevices(RefreshReason::deviceListChanged)`, detect transitions:
   - missing after previously present -> `disconnected`
   - missing from initial load -> `rememberedDeviceUnavailable`
   - no remembered device and no devices -> `noDevicesAvailable`
   - remembered or selected device present -> `connected`
5. Add `statusValueLabel` to `StandaloneMidiInputPanel` and map statuses to stable text.
6. Use color only as a secondary cue. The text itself must fully explain state.

Validate:

1. Launch with no MIDI device connected and confirm the panel shows a usable unavailable state.
2. Launch with a remembered device disconnected and confirm the panel shows an unavailable message that includes the remembered name if present.
3. Connect the remembered device and confirm status transitions to connected without manual re-selection.
4. Disconnect the selected device during runtime and confirm status changes to disconnected.
5. Reconnect the same device and confirm it returns to connected automatically.

### 3.3 Checkbox: Add a bounded MIDI monitor that shows timestamp or order, type, channel, and message values

Plan:

- Keep event capture minimal in the MIDI callback.
- Drain and format monitor content only on the message thread.
- Use explicit columns so monitor output is readable during MiniLab bring-up.

Act:

1. Add `src/midi/MidiMonitor.h/.cpp` with `MidiMonitorEvent`, `MidiMonitorMessageType`, and `MidiMonitorBuffer`.
2. Implement `pushMessage()` to normalize supported JUCE messages into POD event records.
3. Add `src/ui/MidiMonitorPanel.h/.cpp` with a `juce::TableListBox` and timer-based draining.
4. Cap visible history to `128` events.
5. Add the columns `#`, `Type`, `Ch`, `Data 1`, `Data 2`, `Note`, and `CC`.
6. Update the editor to place the monitor beneath the standalone MIDI selector.

Validate:

1. Launch the standalone app and confirm the monitor table renders even before events arrive.
2. Send note events and confirm order increments monotonically.
3. Confirm note rows show channel, note number, velocity, and note name.
4. Send CC events and confirm CC rows show channel, controller number, and controller value.
5. Generate more than `128` events and confirm the UI retains only the newest bounded history.

### 3.4 Checkbox: Ignore unsupported MIDI messages safely

Plan:

- Unsupported messages must be dropped at the callback-to-monitor boundary.
- The callback must remain allocation-free and side-effect free for unsupported message types.
- Do not clutter the Phase 3 UI with unsupported-event diagnostics.

Act:

1. In `MidiMonitorBuffer::pushMessage()`, explicitly branch on supported message families only:
   - note on
   - note off
   - note on with velocity zero treated as note off
   - control change
2. Return immediately for every other MIDI message type.
3. Do not append placeholder rows such as `Other`, `Unsupported`, or raw hex dumps in Phase 3.
4. Keep the selector/controller state unchanged when unsupported messages arrive.

Validate:

1. With a MIDI tool or device capable of sending program change or pitch bend, confirm no crash occurs.
2. Confirm unsupported messages do not create monitor rows.
3. Confirm subsequent supported note and CC traffic still appears normally.
4. Review the callback path and confirm it performs no string formatting or UI calls.

### 3.5 Checkbox: Keep the app running when the selected MIDI device disconnects

Plan:

- Disconnect handling is a device-list refresh problem, not a monitor-panel problem.
- The controller must survive device disappearance without dereferencing stale list entries or crashing the UI.
- Since the processor is still silent in Phase 3, do not invent no-op processor hooks for held-note clearing yet.

Act:

1. Use `MidiDeviceListConnection` to detect list changes.
2. When the selected identifier disappears:
   - keep the remembered identifier in settings,
   - disable any currently available non-selected inputs,
   - mark state as `disconnected`,
   - keep the controller callback registered.
3. Do not clear the persisted device identifier on disconnect.
4. When the selected identifier reappears, re-enable it automatically and return state to `connected`.
5. Keep the monitor panel alive and able to show previously captured history after disconnect.

Validate:

1. Select a physical MIDI controller.
2. Confirm incoming events appear in the monitor.
3. Unplug the selected controller while the app remains open.
4. Confirm the app stays responsive, the editor remains open, and the status changes to disconnected.
5. Replug the same controller and confirm the app remains stable and resumes monitor activity after reconnection.

### 3.6 Checkbox: Verify MiniLab 3 note and CC events appear in the monitor

Plan:

- Phase 3 verification is about visibility and correctness of incoming messages, not mapping or sound.
- MiniLab validation must use the actual selected device path, not a mock selector state.

Act:

1. Update `README.md` after implementation to note that standalone mode now exposes MIDI selection and monitor bring-up.
2. Select the MiniLab 3 as the active input.
3. Capture note presses and knob or fader movement through the real hardware path.
4. Record any observed device-name quirks for later mapping work if they affect selection stability.

Validate:

1. Press MiniLab keys and confirm note rows appear in the monitor.
2. Move MiniLab knobs and faders and confirm CC rows appear in the monitor.
3. Confirm monitor output remains bounded and readable during repeated interaction.
4. Confirm the selected-device label matches the MiniLab 3 device entry shown by Windows or JUCE.

## 4. Edge Case & Boundary Audit

The following failure modes and logic traps are phase-critical and must be explicitly handled or checked during implementation review:

- Device names are not unique. Persist identifiers, not combo-box indices or display names.
- A stale `audioSetup` XML may leave a previously enabled MIDI input active. The controller must normalize enabled-device state on every refresh so only the intended device remains enabled.
- `juce::MidiDeviceListConnection` callbacks are not guaranteed to be on the message thread. Use `AsyncUpdater` before broadcasting UI changes.
- Refreshing the combo-box contents can re-trigger `onChange`. Guard with `isRefreshingSelector` or an equivalent scope flag.
- `note-on` with velocity `0` must be displayed as `noteOff`, not as a malformed note-on row.
- Unsupported messages must not append placeholder monitor rows that give the impression they are supported.
- Queue overflow must not block the MIDI callback. If overflow occurs, drop old pending events and preserve responsiveness.
- The monitor panel must not assume continuous event arrival. Empty-state rendering should remain stable.
- The selected device may disappear and reappear with the same name but a different list index. All logic must key by identifier.
- The remembered device may be missing at startup while other devices are present. Do not auto-substitute another device.
- Standalone-only panels must remain fully absent in plugin mode.
- Avoid direct inclusion of `juce_StandaloneFilterWindow.h` from headers outside the standalone runtime helpers or standalone-only `.cpp` files.
- Monitor history should survive disconnect inside the current session so the last activity remains visible for debugging.
- Do not route monitor UI state through APVTS or processor state serialization.
- Do not add speculative processor APIs for disconnect handling while the processor is still silent. When Phase 4 introduces audible notes, held-note clearing can be added with real behavior and real tests.

## 5. Verification Protocol

Phase 3 exit is not satisfied until both the automated checks and the manual UX checks below pass.

### 5.1 Automated Checks

There is no dedicated test harness in the current repository. For this phase, automated verification means build-level and problem-scan validation, not a new framework.

Required automated checks:

1. Refresh `TODO.md` to the Phase 3 checklist before implementation begins.
2. Build Debug from the existing CMake workflow:

   ```powershell
   cmake --build build --config Debug
   ```

3. Confirm the standalone target and VST3 target both still compile as part of the existing build.
4. Check the touched files for compiler or IntelliSense errors after the edit set is complete.
5. Review the build output for new warnings introduced by the MIDI shell or monitor files.

Non-goal for this phase:

- Do not introduce a unit-test framework just to test the monitor or selector. That is outside the current milestone and would enlarge the review surface unnecessarily.

### 5.2 Manual UX Checklist

Run these checks in standalone mode on Windows 11:

1. Launch with no MIDI controllers connected.
   Expected: app opens, standalone MIDI panel renders, status explains that no device is selected or no devices are available, no crash.
2. Launch with one MIDI controller connected and no remembered device.
   Expected: device appears in the selector, no auto-selection unless explicitly chosen.
3. Select the MiniLab 3.
   Expected: status changes to connected and the device remains selected after restart while available.
4. Press MiniLab keys.
   Expected: monitor shows note rows with event order, type, channel, note number, velocity, and note name.
5. Move MiniLab knobs or faders.
   Expected: monitor shows CC rows with event order, type, channel, controller number, and controller value.
6. Generate more than 128 events.
   Expected: newest events remain visible, oldest visible rows roll off, UI stays responsive.
7. Send an unsupported message type if available.
   Expected: no crash, no malformed monitor row, subsequent supported traffic still displays.
8. Unplug the selected MiniLab during runtime.
   Expected: app stays open, status changes to disconnected, monitor stops receiving events.
9. Reconnect the same MiniLab.
   Expected: status returns to connected and new events appear again without reselecting a different device.
10. Restart with the remembered device unplugged.
    Expected: panel reports the remembered device as unavailable and does not silently switch to another device.
11. Open the plugin target.
    Expected: no standalone MIDI selector or monitor is shown in plugin mode.

### 5.3 Exit Checklist

The phase may move from `TODO.md` to `DONE.md` only when all of the following are true:

- One active MIDI device can be selected and persisted by identifier.
- Missing remembered devices produce an unavailable state rather than a silent fallback.
- Runtime disconnects leave the app stable and update the UI state correctly.
- The monitor shows bounded recent history for note and CC events only.
- Unsupported messages are ignored safely.
- MiniLab 3 note and CC traffic has been manually observed in the monitor.
- The standalone-only code path remains absent from plugin mode.
- README, TODO, and DONE are consistent with the verified behavior.

## 6. Code Scaffolding

The following scaffolds are the minimum structural templates required to keep the Phase 3 implementation consistent with the current codebase.

### 6.1 `CMakeLists.txt`

Add only the new implementation files. Do not create a new library target.

```cmake
target_sources(CoolSynth
    PRIVATE
        src/midi/MidiMonitor.cpp
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/SynthAudioProcessorEditor.cpp
        src/standalone/CoolSynthStandaloneApp.cpp
        src/standalone/StandaloneAudioSupport.cpp
        src/standalone/StandaloneMidiInput.cpp
        src/ui/MidiMonitorPanel.cpp
        src/ui/StandaloneAudioStatusPanel.cpp
        src/ui/StandaloneMidiInputPanel.cpp)
```

### 6.2 `src/standalone/StandaloneAudioSupport.h`

Add the smallest possible standalone-settings accessor.

```cpp
namespace coolsynth::standalone
{
    bool isStandaloneRuntimeAvailable() noexcept;
    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept;
    juce::PropertySet* getStandaloneSettings() noexcept;

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot();
    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager);

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           juce::PropertySet* settings);

    bool showStandaloneAudioSettingsDialog();

    juce::String formatSampleRateHz(double sampleRateHz);
    juce::String formatBufferSizeSamples(int bufferSizeSamples);
}
```

### 6.3 `src/midi/MidiMonitor.h`

Keep the monitor model data-only and callback-safe.

```cpp
#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace coolsynth::midi
{
    enum class MidiMonitorMessageType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
    };

    struct MidiMonitorEvent
    {
        uint64_t eventOrder = 0;
        double timestampSeconds = 0.0;
        MidiMonitorMessageType type = MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
    };

    class MidiMonitorBuffer
    {
    public:
        static constexpr int queueCapacity = 256;
        static constexpr int visibleHistoryCapacity = 128;

        void pushMessage(const juce::MidiMessage& message, double timestampSeconds) noexcept;
        int drainPending(MidiMonitorEvent* destination, int maxEvents) noexcept;
        void clear() noexcept;

    private:
        std::atomic<uint64_t> nextEventOrder { 1 };
        juce::AbstractFifo pendingQueue { queueCapacity };
        std::array<MidiMonitorEvent, queueCapacity> pendingEvents {};
    };

    juce::String formatMonitorMessageType(MidiMonitorMessageType type);
    juce::String formatNoteName(int noteNumber);
}
```

### 6.4 `src/standalone/StandaloneMidiInput.h`

Keep device management and monitor capture together, but outside the UI class.

```cpp
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
        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer);
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
        bool selectedDeviceWasPresent = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputController)
    };
}
```

### 6.5 `src/ui/StandaloneMidiInputPanel.h`

Keep the panel lightweight and message-thread only.

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "midi/MidiMonitor.h"

namespace coolsynth::standalone
{
    class StandaloneMidiInputController;
}

class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    StandaloneMidiInputPanel();
    ~StandaloneMidiInputPanel() override;

    coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept { return monitorBuffer; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshFromController();
    void repopulateDeviceSelector();
    void handleDeviceSelectionChanged();

    bool isRefreshingSelector = false;
    coolsynth::midi::MidiMonitorBuffer monitorBuffer;
    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> controller;

    juce::Label deviceTitleLabel;
    juce::ComboBox deviceSelector;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputPanel)
};
```

### 6.6 `src/ui/MidiMonitorPanel.h`

Use a table so the required monitor fields remain explicit.

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "midi/MidiMonitor.h"

class MidiMonitorPanel final : public juce::Component,
                               private juce::TableListBoxModel,
                               private juce::Timer
{
public:
    explicit MidiMonitorPanel(coolsynth::midi::MidiMonitorBuffer& monitorBuffer);
    ~MidiMonitorPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g,
                            int rowNumber,
                            int width,
                            int height,
                            bool rowIsSelected) override;
    void paintCell(juce::Graphics& g,
                   int rowNumber,
                   int columnId,
                   int width,
                   int height,
                   bool rowIsSelected) override;
    void timerCallback() override;
    void drainPendingEvents();

    coolsynth::midi::MidiMonitorBuffer& monitorBuffer;
    juce::TableListBox table;
    juce::Array<coolsynth::midi::MidiMonitorEvent> recentEvents;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitorPanel)
};
```

### 6.7 `src/plugin/SynthAudioProcessorEditor.cpp`

Extend the existing standalone-only composition pattern. Do not introduce a second editor class.

```cpp
#include "ui/MidiMonitorPanel.h"
#include "ui/StandaloneAudioStatusPanel.h"
#include "ui/StandaloneMidiInputPanel.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
{
    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Phase 3 standalone MIDI shell and monitor", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        standaloneAudioPanel = std::make_unique<StandaloneAudioStatusPanel>();
        addAndMakeVisible(*standaloneAudioPanel);

        auto midiInputPanel = std::make_unique<StandaloneMidiInputPanel>();
        auto* monitorBuffer = &midiInputPanel->getMonitorBuffer();
        standaloneMidiInputPanel = std::move(midiInputPanel);
        addAndMakeVisible(*standaloneMidiInputPanel);

        standaloneMidiMonitorPanel = std::make_unique<MidiMonitorPanel>(*monitorBuffer);
        addAndMakeVisible(*standaloneMidiMonitorPanel);
    }

    setSize(900, 700);
}
```

Layout rule:

- Audio panel near the top.
- MIDI selector/status panel directly beneath it.
- Monitor fills the remaining vertical space.

### 6.8 README Update Template

After verification, update the README status section with concise facts only.

```md
**Phase 3: Standalone MIDI Shell and Monitor** is complete.

- The standalone app now exposes one-device MIDI input selection.
- The last selected MIDI input is remembered by device identifier when available.
- Missing remembered devices show an unavailable state instead of silently falling back.
- The standalone app now includes a bounded MIDI monitor for note and CC bring-up.
- Unsupported MIDI message types are ignored safely.
```

## Implementation Summary

Phase 3 should be a thin standalone extension of the Phase 2 shell, not a shared-core refactor.

The critical architectural bets are:

- one owner for hardware MIDI (`AudioDeviceManager`),
- one standalone controller for selection and status,
- one bounded data-only monitor queue,
- one message-thread table view,
- zero processor changes unless validation proves they are required.

That is the smallest design that satisfies the current milestone and keeps later synth, MiniLab mapping, and VST3 work on the correct shared path.
