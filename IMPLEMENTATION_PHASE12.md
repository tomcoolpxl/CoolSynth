<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 12 Blueprint: Standalone Device Persistence

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 12 - Standalone device persistence`

Selection basis:

- `TODO.md` is still scoped to `Phase 11`.
- `IMPLEMENTATION_PHASE11.md` already exists, so the next new phase blueprint file is `IMPLEMENTATION_PHASE12.md`.
- `IMPLEMENTATION_PLAN.md` defines `Phase 12` as the standalone device-persistence milestone that follows the VST3 smoke phase intentionally, so plugin-state boundaries are validated before more standalone-only persistence logic is added.
- The current codebase already contains a standalone settings file, JUCE-managed `audioSetup` persistence, custom MIDI-selection persistence, and a disconnect-to-panic path. That means this phase is primarily about ownership, validation, and UX accuracy rather than inventing a new persistence backend.

## Working Hypothesis

The underlying persistence mechanisms already exist:

- `src/standalone/CoolSynthStandaloneApp.cpp` creates a JUCE `ApplicationProperties` settings file and passes `getUserSettings()` into `juce::StandalonePluginHolder`.
- JUCE's standalone wrapper already saves and restores `audioSetup` XML under the `audioSetup` key.
- `src/standalone/StandaloneMidiInput.cpp` already stores `midiInputIdentifier` and `midiInputName` and reloads them on startup.
- `src/plugin/SynthAudioProcessorEditor.cpp` already wires MIDI-device disconnects to `processor.requestPanic()`.

The likely gaps are not raw persistence capability. The likely gaps are:

- the lack of one typed standalone settings boundary over `juce::PropertySet`,
- validation of stored values before they become runtime truth,
- accurate standalone UI reporting when a remembered audio configuration cannot be restored,
- and tightening the difference between "remembered but unavailable" and "not selected" states.

Cheap disconfirming check already satisfied by the current code:

1. The standalone app owns a settings file.
2. JUCE already persists `audioSetup`.
3. CoolSynth already persists a custom MIDI selection.
4. The disconnect path already reaches panic.

That means `Phase 12` should be treated as a persistence-boundary consolidation phase, not a greenfield persistence-feature phase.

## Scope Guardrails

In scope for this phase:

- Make standalone settings access explicit and typed through one standalone-owned store layer.
- Treat JUCE `audioSetup` XML as the authoritative persisted audio configuration.
- Keep custom one-device MIDI selection persistence as a CoolSynth-owned setting.
- Restore remembered standalone audio and MIDI choices when still available.
- Show a clear standalone status when a remembered audio or MIDI device/configuration is missing.
- Preserve the current disconnect-to-panic behavior and harden it only if a local defect is found.
- Document exactly what standalone state is persisted and what is intentionally not persisted.

Explicitly out of scope for this phase:

- Any change to APVTS parameter IDs, plugin metadata, or VST3 state handling.
- Preset files, patch browser work, or file-based preset persistence.
- MIDI learn, host CC mapping, or controller abstraction redesign.
- Window placement persistence, recent-files persistence, or extra standalone preferences.
- Any processor-side settings serialization beyond the existing APVTS state path.
- Any new DSP, voice allocation, or UI redesign work unrelated to persistence/status accuracy.

Non-negotiable constraints:

- Standalone runtime settings must remain outside `SynthAudioProcessor::getStateInformation()` and `setStateInformation()`.
- The audio callback must not parse XML, touch `juce::PropertySet`, enumerate devices, or depend on persistence state.
- `audioSetup` remains JUCE-owned. CoolSynth may read it, validate it, and surface it in the UI, but CoolSynth must not replace it with a competing custom audio-settings format.
- The existing custom MIDI-selection keys may be wrapped, but they must not silently disappear or become unreadable to current local settings files.

## Current Code Anchors

The implementation for this phase should stay anchored to the code that already owns standalone runtime state:

- `src/standalone/CoolSynthStandaloneApp.cpp` owns the `ApplicationProperties` lifetime and the `juce::StandalonePluginHolder` construction.
- `external/JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` already persists `audioSetup` and standalone `filterState` into the same `PropertySet`.
- `src/standalone/StandaloneAudioSupport.h` and `.cpp` currently expose the audio-device manager, inspect the active device, and apply the preferred WASAPI backend only when no persisted `audioSetup` exists.
- `src/standalone/StandaloneMidiInput.h` and `.cpp` currently own one-device MIDI selection, persisted selection keys, device-refresh policy, and disconnect detection.
- `src/plugin/SynthAudioProcessorEditor.cpp` constructs `StandaloneMidiInputController` only in standalone mode and uses a disconnect callback to request panic.
- `src/ui/StandaloneStatusBar.h` and `.cpp` currently surface a compact summary but do not yet distinguish remembered-unavailable audio state from generic "Audio: Off".
- `CMakeLists.txt` uses an explicit `target_sources(...)` list, so any new standalone `.cpp` file must be added there.

## Exact `TODO.md` Entries This Blueprint Expands

These are the `Phase 12` checklist items from `IMPLEMENTATION_PLAN.md` that this blueprint expands into concrete execution work when `TODO.md` advances to this phase:

- [ ] Persist the last valid standalone audio backend, output device, sample rate, and buffer size.
- [ ] Persist the last valid standalone MIDI input selection.
- [ ] Restore persisted standalone device settings when still available.
- [ ] Show unavailable state when saved devices are missing.
- [ ] Clear held-note state when the active MIDI device disconnects during playback.
- [ ] Verify standalone restart and missing-device behavior.

## 1. Architectural Design

### 1.1 Controlling Design Decision

This phase must not invent a second audio-persistence format.

Required consequence:

- Keep JUCE `audioSetup` XML as the authoritative persisted audio configuration.
- Introduce a small CoolSynth `StandaloneSettingsStore` only to provide typed access to standalone settings and to parse the remembered audio configuration for status/validation purposes.
- Continue to persist one-device MIDI selection through CoolSynth-owned keys because the project requirement is one remembered selected MIDI input, not JUCE's generic set of enabled MIDI inputs.

Why this is the correct minimum design:

- JUCE already serializes the exact audio fields this phase needs: `deviceType`, `audioOutputDeviceName`, `audioDeviceRate`, and `audioDeviceBufferSize`.
- Writing a second custom audio serializer would create drift between the actual restore source and the status source.
- A typed store still adds value because the current code spreads raw `PropertySet` access across the app and MIDI controller.

### 1.2 State Ownership Model

Keep state ownership explicit and asymmetric.

| State Surface | Owner | Persistence Source | Thread/Timing Rules | Phase 12 Rule |
| --- | --- | --- | --- | --- |
| Shared synth parameters | `SynthAudioProcessor::parameters` | APVTS state / plugin state / standalone `filterState` via JUCE holder | Audio thread reads, message thread writes | Do not change in this phase |
| Standalone audio configuration | JUCE `StandalonePluginHolder` + `AudioDeviceManager` | `PropertySet["audioSetup"]` XML | Message-thread/device-manager lifecycle only | CoolSynth reads only; JUCE remains writer |
| Standalone MIDI selection | CoolSynth standalone runtime | `PropertySet["midiInputIdentifier"]`, `PropertySet["midiInputName"]` | Message thread only | Wrap in typed store and validate before persisting |
| Standalone UI status | `StandaloneAudioSupport` and `StandaloneStatusBar` | Derived from active runtime state plus remembered state | Message thread only | Must distinguish unavailable remembered config from no selection |

Important boundary:

- It is acceptable for `audioSetup`, `filterState`, and custom standalone MIDI keys to live in the same physical `.settings` file.
- It is not acceptable for audio/MIDI runtime settings to be serialized through processor state or APVTS.

### 1.3 Persistence-Key Policy

These keys are already part of the standalone runtime contract and should be treated deliberately:

```text
audioSetup                // JUCE-owned audio XML; keep exact name
filterState               // JUCE-owned standalone processor state; do not reinterpret here
midiInputIdentifier       // CoolSynth-owned one-device MIDI selection key
midiInputName             // CoolSynth-owned remembered display name
```

Rules:

- Keep `audioSetup` unchanged because JUCE standalone code reads and writes it directly.
- Keep `midiInputIdentifier` and `midiInputName` readable for backward compatibility with the current local settings file.
- Do not rename or migrate keys during this phase unless a concrete settings-compatibility defect requires it.

### 1.4 Required Data Structures and State Definitions

The new or revised standalone-only state should be explicit and minimal.

#### 1.4.1 Typed Persisted State

```cpp
namespace coolsynth::standalone
{
    struct PersistedAudioSelection
    {
        juce::String deviceTypeName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;

        bool isValid() const noexcept;
        bool hasNamedOutput() const noexcept;
    };

    struct PersistedMidiInputSelection
    {
        juce::String identifier;
        juce::String name;

        bool isValid() const noexcept;
    };
}
```

Design intent:

- `PersistedAudioSelection` is a parsed view over JUCE `audioSetup` XML, not a second save format.
- `PersistedMidiInputSelection` is the typed view over the existing custom MIDI keys.

#### 1.4.2 Audio Status Classification

Extend the current audio snapshot model so the UI can differentiate "off", "restored", and "remembered but unavailable".

```cpp
namespace coolsynth::standalone
{
    enum class AudioDeviceStatus
    {
        managerUnavailable,
        noOutputDeviceAvailable,
        ready,
        fallbackConfigurationActive,
        rememberedConfigurationUnavailable,
    };

    struct AudioDeviceSnapshot
    {
        bool runningInStandalone = false;
        bool hasCurrentDevice = false;
        bool hasActiveOutput = false;
        bool persistedConfigurationFound = false;
        bool currentMatchesPersistedConfiguration = false;
        AudioDeviceStatus status = AudioDeviceStatus::managerUnavailable;

        juce::String backendName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;

        PersistedAudioSelection persistedSelection;
        juce::String statusMessage;
    };
}
```

Classification rules:

- `ready`: active output exists, and either no persisted config exists or the active config matches the remembered config.
- `fallbackConfigurationActive`: active output exists, but backend/device/sample-rate/buffer differ from the remembered config.
- `rememberedConfigurationUnavailable`: remembered config exists, but no active output device could be opened.
- `noOutputDeviceAvailable`: no remembered config exists and no active output exists.
- `managerUnavailable`: not running in standalone or the device manager cannot be reached.

Why compare sample rate and buffer too:

- The phase explicitly requires persistence of sample rate and buffer size, not just device name.
- A host or OS fallback to a different valid device with a different rate/buffer must be surfaced as a degraded restore rather than silently treated as a perfect restore.

#### 1.4.3 MIDI Status Rules

The existing `MidiInputSnapshot` and `MidiInputStatus` are already close to the required model.

Keep them, but tighten these semantics:

- `selectedDeviceIdentifier` and `selectedDeviceName` may come from persisted state during initial load even when the device is missing.
- `selectDeviceByIdentifier()` must only persist a user-chosen identifier if that identifier is present in `availableInputs`.
- A missing remembered MIDI device at startup must remain a remembered-unavailable state, not collapse to "No MIDI input device selected".

### 1.5 Required Function Signatures

#### 1.5.1 New Settings Store Surface

Add one standalone-owned store layer under `src/standalone/SettingsStore.{h,cpp}`.

```cpp
class StandaloneSettingsStore final
{
public:
    explicit StandaloneSettingsStore(juce::PropertySet& propertySet);

    bool hasPersistedAudioSetup() const;
    std::optional<coolsynth::standalone::PersistedAudioSelection> loadPersistedAudioSelection() const;

    std::optional<coolsynth::standalone::PersistedMidiInputSelection> loadPersistedMidiInputSelection() const;
    void savePersistedMidiInputSelection(const juce::MidiDeviceInfo& device);
    void clearPersistedMidiInputSelection();

private:
    juce::PropertySet& propertySet;
};
```

Rules for this class:

- It is a typed adapter over `juce::PropertySet`, not a new persistence engine.
- It may parse XML from `audioSetup`, but it must not try to outsmart JUCE by writing a parallel audio configuration format.
- It should remain free of UI code, `AudioProcessor` references, and audio-thread access.

#### 1.5.2 Standalone Runtime Accessors

The standalone runtime already exposes global accessors for the device manager and raw settings. Add the minimal typed accessor alongside them.

```cpp
namespace coolsynth::standalone
{
    void bindStandaloneSettingsStore(StandaloneSettingsStore* store) noexcept;
    StandaloneSettingsStore* getStandaloneSettingsStore() noexcept;

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot();
    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager,
                                                   const StandaloneSettingsStore* settingsStore = nullptr);
}
```

Why this is acceptable:

- The codebase already uses standalone-only global accessors for runtime objects.
- This keeps typed settings access consistent with the existing standalone-device-manager access pattern.
- The optional `settingsStore` argument keeps snapshot classification testable without forcing global state into every call site.

#### 1.5.3 Revised MIDI Controller Constructor

Change the MIDI controller to depend on the typed store rather than raw `PropertySet` access.

```cpp
StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                              StandaloneSettingsStore* settingsStore,
                              ControllerEventHandler onControllerEvent,
                              DisconnectCallback onSelectedDeviceDisconnected = {});
```

This change is important because:

- it keeps the persistence policy in one place,
- it avoids further spread of raw string keys,
- and it lets `selectDeviceByIdentifier()` validate selection before persisting.

### 1.6 Runtime Flow

#### 1.6.1 Standalone Startup Flow

The intended flow for this phase is:

1. `CoolSynthStandaloneApp::initialise()` configures `ApplicationProperties`.
2. The app constructs `StandaloneSettingsStore` from `appProperties.getUserSettings()`.
3. The app binds the store through `bindStandaloneSettingsStore()` before the standalone editor is created.
4. `juce::StandalonePluginHolder` restores `audioSetup` automatically.
5. `maybeApplyPreferredAudioBackend()` runs only if no persisted `audioSetup` is present.
6. `SynthAudioProcessorEditor` constructs `StandaloneMidiInputController` with `getStandaloneSettingsStore()`.
7. The MIDI controller loads the remembered MIDI selection from the store during `RefreshReason::initialLoad` and immediately reapplies the one-device policy.

#### 1.6.2 Audio Status Flow

The audio UI flow should be derived, not manually synchronized:

1. `StandaloneStatusBar` receives a device-manager change.
2. `captureAudioDeviceSnapshot(...)` queries the active device manager state.
3. The same snapshot function loads the remembered audio configuration from `StandaloneSettingsStore` by parsing `audioSetup`.
4. The snapshot classifies the result as `ready`, `fallbackConfigurationActive`, `rememberedConfigurationUnavailable`, `noOutputDeviceAvailable`, or `managerUnavailable`.
5. The status bar renders a concise but informative message from that snapshot.

#### 1.6.3 MIDI Disconnect Flow

The disconnect path should remain message-thread owned and minimal:

1. `StandaloneMidiInputController::refreshDevices(...)` detects transition from present to missing.
2. It retains the remembered identifier/name in snapshot state.
3. It invokes `onSelectedDeviceDisconnected()` exactly once per disconnect transition.
4. `SynthAudioProcessorEditor` maps that callback to `processor.requestPanic()`.
5. `SynthAudioProcessor::processBlock()` sees `panicRequested` and calls `synthEngine.panic()` on the next block.

No part of this flow should read the settings file or mutate UI from the audio callback.

## 2. File-Level Strategy

### 2.1 Required Edit Set

These files should be the default implementation touch list for `Phase 12`.

| File | Responsibility in this phase |
| --- | --- |
| `CMakeLists.txt` | Add the new standalone settings-store implementation file to the explicit `target_sources(...)` list. |
| `src/standalone/SettingsStore.h` | Declare the typed standalone settings API, parsed persisted-state structs, and standalone-only key policy. |
| `src/standalone/SettingsStore.cpp` | Implement typed reads over JUCE `audioSetup` XML and typed reads/writes over the existing MIDI-selection keys. |
| `src/standalone/CoolSynthStandaloneApp.cpp` | Construct the typed settings store from `ApplicationProperties`, bind it for standalone runtime access, and keep JUCE as the writer for `audioSetup`. |
| `src/standalone/StandaloneAudioSupport.h` | Extend audio snapshot/status definitions and declare typed settings-store accessors. |
| `src/standalone/StandaloneAudioSupport.cpp` | Classify active-versus-remembered audio state, surface fallback/unavailable messages, and preserve the current preferred-backend rule without overriding saved audio setups. |
| `src/standalone/StandaloneMidiInput.h` | Replace raw `PropertySet` dependency with `StandaloneSettingsStore*` and keep one-device selection semantics explicit. |
| `src/standalone/StandaloneMidiInput.cpp` | Load remembered MIDI selection through the store, persist only validated user selections, and preserve disconnect-to-panic behavior. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Fetch `getStandaloneSettingsStore()` and pass the typed store to `StandaloneMidiInputController` in standalone mode only. |
| `src/ui/StandaloneStatusBar.h` | Accept the richer audio snapshot/status model. |
| `src/ui/StandaloneStatusBar.cpp` | Render backend, output device, sample rate, buffer size, and remembered-unavailable or fallback status accurately. |
| `README.md` | Document what standalone state is remembered, what is not remembered, and how unavailable remembered devices are surfaced. |
| `TODO.md` | Refresh with the exact `Phase 12` checklist when implementation begins. |
| `DONE.md` | Record completion only after restart, missing-device, and disconnect checks pass. |

### 2.2 Conditional Edit Set

These files should be edited only if the required implementation reveals a concrete need.

| File | Touch only if... |
| --- | --- |
| `src/plugin/SynthAudioProcessorEditor.h` | A stored `StandaloneSettingsStore*` or additional standalone-only editor member becomes necessary. |
| `src/ui/StandaloneMidiInputPanel.cpp` | The existing MIDI status copy or color handling needs to be adjusted to match the tightened remembered-unavailable semantics. |
| `src/ui/StandaloneSettingsDialog.h` | The dialog API must expose an audio-status label or extra standalone-only persistence text. |
| `src/ui/StandaloneSettingsDialog.cpp` | Reviewers decide the settings dialog needs a visible explanation of remembered-unavailable audio configuration rather than relying on the status bar alone. |

### 2.3 Audit-Only Files That Should Not Change Without Proof

These files must be reviewed during the phase, but they should stay frozen unless a local defect proves otherwise.

| File | Reason to keep frozen |
| --- | --- |
| `src/plugin/SynthAudioProcessor.h` | Shared processor API is not the owner of standalone settings persistence. |
| `src/plugin/SynthAudioProcessor.cpp` | APVTS state and panic handling already satisfy the shared-state boundary for this phase. |
| `src/parameters/ParameterIDs.h` | Parameter IDs are unrelated to standalone device persistence and must not drift. |
| `src/parameters/ParameterLayout.cpp` | Parameter definitions are not part of this milestone. |
| `external/JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` | JUCE already persists `audioSetup`; modifying vendored JUCE is not justified for this phase. |

## 3. Atomic Execution Steps

Each step below expands one `Phase 12` checkbox into a strict `Plan -> Act -> Validate` loop.

### 3.1 Persist the Last Valid Standalone Audio Backend, Output Device, Sample Rate, and Buffer Size

#### Plan

- Do not build a second audio serializer.
- Treat JUCE `audioSetup` XML as the persisted source of truth.
- Make that dependency explicit through `StandaloneSettingsStore` so the codebase can reason about remembered audio state deliberately instead of treating it as hidden JUCE magic.

#### Act

1. Create `src/standalone/SettingsStore.h` and `.cpp`.
2. Implement `loadPersistedAudioSelection()` by parsing `PropertySet::getXmlValue("audioSetup")`.
3. Parse exactly these attributes from a `DEVICESETUP` root: `deviceType`, `audioOutputDeviceName`, `audioDeviceRate`, and `audioDeviceBufferSize`.
4. Keep `audioSetup` as read-only from CoolSynth code. Do not add a custom `savePersistedAudioSelection()` function.
5. Update `maybeApplyPreferredAudioBackend(...)` to check typed persisted-audio presence through `StandaloneSettingsStore` or an equivalent typed helper rather than raw key probing.
6. Add `SettingsStore.cpp` to `CMakeLists.txt` so the implementation actually links.

#### Validate

- The codebase has one authoritative persisted audio format: JUCE `audioSetup` XML.
- The remembered audio selection can be inspected in typed form without touching JUCE internals from UI code.
- The preferred-backend shortcut still runs only when there is no remembered audio configuration.
- No processor or audio-thread code depends on `StandaloneSettingsStore`.

### 3.2 Persist the Last Valid Standalone MIDI Input Selection

#### Plan

- Wrap the existing `midiInputIdentifier` and `midiInputName` keys in `StandaloneSettingsStore`.
- Tighten persistence so only a validated device chosen from the current device list is written as a new remembered selection.
- Keep missing remembered devices visible rather than silently erasing the remembered choice.

#### Act

1. Change `StandaloneMidiInputController` to accept `StandaloneSettingsStore*` instead of `juce::PropertySet*`.
2. Move `persistSelection()` and `clearPersistedSelection()` behind store calls.
3. In `refreshDevices(RefreshReason::initialLoad)`, load the remembered MIDI selection from `StandaloneSettingsStore`.
4. In `selectDeviceByIdentifier(...)`, validate that the requested identifier exists in `snapshot.availableInputs` before changing `snapshot.selectedDeviceIdentifier` and persisting it.
5. If the requested identifier is unknown, return `false` and keep the previous remembered selection unchanged.
6. Keep `clearSelection()` as the only explicit path that removes remembered MIDI keys.

#### Validate

- A valid user selection still persists across app restarts.
- An invalid identifier passed into `selectDeviceByIdentifier()` does not overwrite the last good remembered device.
- A missing remembered MIDI device at startup still reports `rememberedDeviceUnavailable` instead of collapsing to `noSelection`.
- No raw string key access remains outside the store and the low-level JUCE holder integration.

### 3.3 Restore Persisted Standalone Device Settings When Still Available

#### Plan

- Reuse the current restore owners instead of moving restore logic around.
- JUCE restores audio state through `StandalonePluginHolder`.
- CoolSynth restores remembered MIDI selection through `StandaloneMidiInputController` during initial device refresh.

#### Act

1. Construct `StandaloneSettingsStore` in `CoolSynthStandaloneApp` immediately after `ApplicationProperties` storage is configured.
2. Bind the typed store into standalone runtime access before the standalone editor is created.
3. Keep `juce::StandalonePluginHolder` construction unchanged so JUCE still restores `audioSetup`.
4. Keep `maybeApplyPreferredAudioBackend(...)` after holder construction, but gate it behind the typed `hasPersistedAudioSetup()` check.
5. In `SynthAudioProcessorEditor.cpp`, fetch `getStandaloneSettingsStore()` and pass it into `StandaloneMidiInputController`.
6. Let `StandaloneMidiInputController` perform its initial remembered-selection restore during `RefreshReason::initialLoad`.
7. Confirm `applyOneDeviceEnabledPolicy()` still overrides any generic multi-device MIDI enablement that JUCE may have reopened from `audioSetup`.

#### Validate

- Restarting the app with the same hardware restores the remembered audio configuration and the remembered MIDI selection without user action.
- The app still starts if audio restore falls back to a default device or no device.
- Only one MIDI input remains enabled after the controller refreshes, even if JUCE reopened multiple remembered MIDI inputs generically.
- No standalone restore logic leaks into plugin mode.

### 3.4 Show Unavailable State When Saved Devices Are Missing

#### Plan

- Use the typed remembered-audio view plus the active device-manager state to classify startup results precisely.
- Keep status derivation in `StandaloneAudioSupport`, not in UI string-building code.
- Reuse the existing remembered-unavailable MIDI state model and tighten only where necessary.

#### Act

1. Extend `AudioDeviceSnapshot` with remembered-selection fields and `AudioDeviceStatus`.
2. In `captureAudioDeviceSnapshot(...)`, load `PersistedAudioSelection` from `StandaloneSettingsStore`.
3. Compare remembered-versus-active backend, output device, sample rate, and buffer size.
4. Emit one of three user-meaningful outcomes when remembered audio exists:
   - exact restore,
   - fallback configuration active,
   - remembered configuration unavailable.
5. Update `StandaloneStatusBar::formatAudioSummary(...)` so it no longer reduces all missing-audio scenarios to `Audio: Off`.
6. Preserve the existing MIDI status-message flow, but confirm that remembered-unavailable and disconnected states remain visibly distinct.

#### Validate

- If the saved audio config cannot be restored and no current device opens, the status clearly says the remembered config is unavailable.
- If the saved audio config cannot be restored but a fallback device opens, the status clearly says a fallback configuration is active.
- If the remembered MIDI device is missing, the UI still shows that remembered device as unavailable rather than pretending nothing had been selected.
- A normal exact restore still produces a concise ready state rather than a verbose warning.

### 3.5 Clear Held-Note State When the Active MIDI Device Disconnects During Playback

#### Plan

- Treat the existing disconnect-to-panic path as the default implementation.
- Harden only if a local defect appears, such as a stale queued event after disconnect.
- Do not move disconnect recovery into the processor or audio callback beyond the existing `panicRequested` atomic.

#### Act

1. Audit the current path: `StandaloneMidiInputController` disconnect detection -> editor callback -> `SynthAudioProcessor::requestPanic()` -> `SynthAudioProcessor::processBlock()` -> `synthEngine.panic()`.
2. Confirm that disconnect detection fires only on a transition from present to missing, not on every refresh while the device remains absent.
3. If manual testing reveals stale controller actions after disconnect, clear queued controller events inside `StandaloneMidiInputController` on disconnect before broadcasting the change.
4. If manual testing shows no stale-event defect, leave the event-queue logic untouched.

#### Validate

- Holding notes while unplugging the active MIDI device does not leave stuck voices sounding.
- The app remains running after disconnect.
- Reconnecting the device does not require restarting the app.
- No new persistence or device-enumeration work is added to `processBlock()`.

### 3.6 Verify Standalone Restart and Missing-Device Behavior

#### Plan

- Close the phase with a reproducible restart and failure-handling checklist, not a vague "seems remembered" impression.
- Update `README.md` with exactly what is persisted and what remains deferred.

#### Act

1. Add a `README.md` section describing remembered standalone state.
2. Document that remembered standalone state includes:
   - audio backend,
   - output device,
   - sample rate,
   - buffer size,
   - one MIDI input selection.
3. Document that remembered standalone state intentionally excludes:
   - window geometry,
   - recent files,
   - MIDI monitor visibility,
   - preset files.
4. Run the full restart/missing-device validation checklist from Section 5.
5. Refresh `TODO.md` and `DONE.md` only after the implementation and validation pass is complete.

#### Validate

- Another reviewer can follow the documented restart tests and reach the same outcome.
- The app fails softly when remembered devices are missing.
- The phase closes with explicit evidence for restart behavior, not just build success.

## 4. Edge Case and Boundary Audit

The implementation and review for this phase must actively check these failure modes.

### 4.1 Settings-File and Parsing Risks

- The standalone settings file does not exist yet.
- The standalone settings file exists but `audioSetup` is missing.
- `audioSetup` exists but the XML root is not `DEVICESETUP`.
- `audioSetup` exists but `audioDeviceRate` or `audioDeviceBufferSize` are missing or malformed.
- `midiInputIdentifier` is empty while `midiInputName` is present, leaving a human-readable name with no usable selection key.
- The settings file contains stale values from an older local build.

### 4.2 Audio-Restore Classification Risks

- The saved backend exists but the saved output device no longer exists.
- The saved output device exists but no active output channels open.
- The saved device restores, but sample rate or buffer size are changed by the OS or backend.
- JUCE opens a fallback device automatically, and the UI incorrectly reports a perfect restore.
- The remembered audio config uses an empty output-device name that represents a default device rather than a named concrete device.

### 4.3 MIDI-Restore and Selection Risks

- The remembered MIDI device identifier changes across reconnects while the display name remains the same.
- `selectDeviceByIdentifier()` is called with an invalid identifier and accidentally overwrites the last good remembered selection.
- JUCE `audioSetup` XML contains generic `MIDIINPUT` entries that conflict with the project rule of one active device at a time.
- The UI clears the remembered selection too aggressively when a device is temporarily absent.

### 4.4 Disconnect-Recovery Risks

- The disconnect callback fires repeatedly instead of once per disconnect transition.
- Notes remain stuck because panic is never observed by `processBlock()` after disconnect.
- Queued controller events are delivered after disconnect and re-apply parameter or command changes unexpectedly.

### 4.5 Shared-State Boundary Risks

- Standalone settings access leaks into shared plugin-mode code paths.
- Shared processor state starts depending on `StandaloneSettingsStore`.
- A future refactor tries to serialize audio or MIDI runtime settings through APVTS or processor state.

### 4.6 Non-Goal Traps

- Turning this phase into a general preferences system.
- Adding window-geometry persistence because the settings file already exists.
- Refactoring standalone UI layout just because the status bar text changes.
- Moving MIDI learn or preset persistence forward into this milestone.

## 5. Verification Protocol

There is no permanent unit-test framework required by the project yet, so the verification stack for this phase is:

- focused build validation,
- deterministic standalone settings-store logic checks,
- and restart/disconnect manual validation.

### 5.1 Required Build Checks

Run these checks in order:

1. `cmake --build build --config Debug --target CoolSynth_Standalone`
2. Launch the standalone app from the built artifact.
3. Confirm the new standalone settings-store source is linked by verifying the build succeeds after `SettingsStore.cpp` is added to `CMakeLists.txt`.
4. Review build output for new warnings introduced by the phase.

### 5.2 Required Manual UX Checklist

All checks below must pass before the phase can move to `DONE.md`.

- [ ] Choose a concrete audio backend, output device, sample rate, and buffer size. Quit and relaunch. Confirm the same configuration restores when still available.
- [ ] Choose one MIDI input device. Quit and relaunch. Confirm the same device is still selected when still available.
- [ ] Launch with no stored devices available and confirm the app remains running.
- [ ] Launch with a remembered MIDI device missing and confirm the MIDI status shows remembered-unavailable rather than no-selection.
- [ ] Launch with a remembered audio config missing and no fallback device available and confirm the audio status shows remembered-unavailable rather than generic off.
- [ ] Launch with a remembered audio config missing but a fallback device active and confirm the audio status shows a fallback configuration rather than a perfect restore.
- [ ] Clear the MIDI selection explicitly, relaunch, and confirm no device is selected and no stale remembered-unavailable status remains.
- [ ] Hold notes, disconnect the active MIDI device, and confirm all sounding notes stop without crashing the app.
- [ ] Reconnect the MIDI device and confirm the app can recover without restart.
- [ ] Confirm the plugin target behavior is unchanged: no standalone device state appears in plugin mode.

### 5.3 Required Automated Logic Cases

These cases should be implemented as the smallest possible deterministic checks for the new standalone-only logic. If the team still wants to avoid a permanent test framework, run them in a temporary local harness before phase closure.

Required cases:

1. `StandaloneSettingsStore::loadPersistedAudioSelection()` returns `std::nullopt` when `audioSetup` is missing.
2. `StandaloneSettingsStore::loadPersistedAudioSelection()` returns `std::nullopt` when the XML root is not `DEVICESETUP`.
3. `StandaloneSettingsStore::loadPersistedAudioSelection()` correctly parses `deviceType`, `audioOutputDeviceName`, `audioDeviceRate`, and `audioDeviceBufferSize` from valid XML.
4. `StandaloneSettingsStore::loadPersistedMidiInputSelection()` returns `std::nullopt` when the identifier is empty, even if a name is present.
5. `StandaloneMidiInputController::selectDeviceByIdentifier()` leaves remembered selection unchanged when given an unknown identifier.
6. Audio snapshot classification returns `fallbackConfigurationActive` when remembered and active configurations differ but a current output device exists.
7. Audio snapshot classification returns `rememberedConfigurationUnavailable` when remembered config exists and no current output device exists.

### 5.4 Failure Triage Order

If a manual or deterministic check fails, use this order and do not widen scope prematurely.

1. Settings-file content and typed parsing check.
2. Active device-manager state versus remembered state comparison.
3. MIDI-controller selection persistence guard.
4. Status-message classification and rendering.
5. Disconnect-to-panic path.
6. Only after the above: reconsider runtime ownership boundaries.

## 6. Code Scaffolding

Use the following scaffolding only as structure. Keep the real implementation smaller when possible.

### 6.1 `StandaloneSettingsStore` Scaffolding

```cpp
#pragma once

#include <optional>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

namespace coolsynth::standalone
{
    class StandaloneSettingsStore final
    {
    public:
        explicit StandaloneSettingsStore(juce::PropertySet& propertySet)
            : propertySet(propertySet)
        {
        }

        bool hasPersistedAudioSetup() const
        {
            return propertySet.containsKey("audioSetup");
        }

        std::optional<PersistedAudioSelection> loadPersistedAudioSelection() const;
        std::optional<PersistedMidiInputSelection> loadPersistedMidiInputSelection() const;

        void savePersistedMidiInputSelection(const juce::MidiDeviceInfo& device)
        {
            propertySet.setValue("midiInputIdentifier", device.identifier);
            propertySet.setValue("midiInputName", device.name);
        }

        void clearPersistedMidiInputSelection()
        {
            propertySet.removeValue("midiInputIdentifier");
            propertySet.removeValue("midiInputName");
        }

    private:
        juce::PropertySet& propertySet;
    };
}
```

Implementation rule:

- `loadPersistedAudioSelection()` should parse JUCE's existing `DEVICESETUP` XML attributes directly and return `std::nullopt` for malformed or incomplete data instead of guessing.

### 6.2 Audio-Setup Parsing Scaffolding

```cpp
std::optional<coolsynth::standalone::PersistedAudioSelection>
StandaloneSettingsStore::loadPersistedAudioSelection() const
{
    auto xml = propertySet.getXmlValue("audioSetup");

    if (xml == nullptr || !xml->hasTagName("DEVICESETUP"))
        return std::nullopt;

    PersistedAudioSelection selection;
    selection.deviceTypeName = xml->getStringAttribute("deviceType");
    selection.outputDeviceName = xml->getStringAttribute("audioOutputDeviceName");
    selection.sampleRateHz = xml->getDoubleAttribute("audioDeviceRate", 0.0);
    selection.bufferSizeSamples = xml->getIntAttribute("audioDeviceBufferSize", 0);

    if (!selection.isValid())
        return std::nullopt;

    return selection;
}
```

Rule for `isValid()`:

- Require a non-empty `deviceTypeName` and positive sample-rate and buffer-size values.
- Allow an empty `outputDeviceName` so the code can represent "remembered default output" without fabricating a named device.

### 6.3 Audio Snapshot Classification Scaffolding

```cpp
static bool matchesPersistedConfiguration(const juce::AudioDeviceManager& deviceManager,
                                          const coolsynth::standalone::PersistedAudioSelection& persisted)
{
    juce::AudioDeviceManager::AudioDeviceSetup currentSetup;
    deviceManager.getAudioDeviceSetup(currentSetup);

    return deviceManager.getCurrentAudioDeviceType() == persisted.deviceTypeName
        && currentSetup.outputDeviceName == persisted.outputDeviceName
        && juce::approximatelyEqual(currentSetup.sampleRate, persisted.sampleRateHz)
        && currentSetup.bufferSize == persisted.bufferSizeSamples;
}

static coolsynth::standalone::AudioDeviceStatus classifyAudioStatus(
    const coolsynth::standalone::AudioDeviceSnapshot& snapshot)
{
    if (!snapshot.runningInStandalone)
        return coolsynth::standalone::AudioDeviceStatus::managerUnavailable;

    if (snapshot.hasActiveOutput)
        return snapshot.persistedConfigurationFound && !snapshot.currentMatchesPersistedConfiguration
            ? coolsynth::standalone::AudioDeviceStatus::fallbackConfigurationActive
            : coolsynth::standalone::AudioDeviceStatus::ready;

    return snapshot.persistedConfigurationFound
        ? coolsynth::standalone::AudioDeviceStatus::rememberedConfigurationUnavailable
        : coolsynth::standalone::AudioDeviceStatus::noOutputDeviceAvailable;
}
```

Rules for this scaffold:

- Keep classification in standalone support code, not in label-formatting code.
- Compare backend, device, sample rate, and buffer size together.
- Never read settings from the audio callback.

### 6.4 MIDI Selection Guard Scaffolding

```cpp
bool StandaloneMidiInputController::selectDeviceByIdentifier(const juce::String& deviceIdentifier)
{
    const auto iter = std::find_if(snapshot.availableInputs.begin(),
                                   snapshot.availableInputs.end(),
                                   [&deviceIdentifier](const auto& info)
                                   {
                                       return info.identifier == deviceIdentifier;
                                   });

    if (iter == snapshot.availableInputs.end())
        return false;

    snapshot.selectedDeviceIdentifier = iter->identifier;
    snapshot.selectedDeviceName = iter->name;

    if (settingsStore != nullptr)
        settingsStore->savePersistedMidiInputSelection(*iter);

    refreshDevices(RefreshReason::userSelection);
    return true;
}
```

Rule for this scaffold:

- Only user-cleared selection should erase persisted MIDI state.
- A bad identifier should fail closed, not overwrite good state.

### 6.5 README Scaffolding

Use this structure for the standalone persistence documentation.

````md
## Standalone Persistence

CoolSynth remembers these standalone-only settings between runs:

- audio backend
- output device
- sample rate
- buffer size
- one selected MIDI input device

If a remembered audio or MIDI device is unavailable at startup, the app stays open and shows the unavailable state in the standalone UI.

CoolSynth does not currently persist:

- window size or position
- preset files
- recent files
- MIDI monitor UI state
````

## Exit Signal for This Blueprint

This phase is ready for implementation only when reviewers agree on all of the following:

- JUCE `audioSetup` remains the authoritative audio restore source.
- `StandaloneSettingsStore` is accepted as the only new standalone persistence abstraction.
- Audio status must distinguish exact restore, fallback restore, and remembered-unavailable states.
- MIDI disconnect recovery should stay on the current panic path unless validation reveals a local defect.
- No processor-state or parameter-model changes are required for this phase.
