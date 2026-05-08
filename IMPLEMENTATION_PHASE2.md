# Phase 2 Blueprint: Standalone Audio Device Shell

## Phase Selection

Selected phase: `Phase 2 - Standalone audio device shell`

Selection basis:

- `TODO.md` contains only the completed `Phase 1` checklist.
- `DONE.md` records `Phase 1` as verified complete.
- The current codebase already provides the exact `Phase 1` baseline this phase depends on: a shared JUCE plugin target, a silent `SynthAudioProcessor`, and a placeholder `SynthAudioProcessorEditor`.
- `IMPLEMENTATION_PLAN.md` defines `Phase 2` as the next dependency-bearing milestone.

## Scope Guardrails

In scope for this phase:

- Expose standalone audio-device configuration using JUCE-native controls.
- Show active backend, output device, sample rate, and buffer size in the standalone UI.
- Prefer WASAPI shared mode on first standalone launch when it is available.
- Keep the app stable when the audio device changes, restarts, disappears, or is unavailable at startup.
- Preserve the shared processor boundary so the VST3 target remains free of standalone device UI and hardware assumptions.

Explicitly out of scope for this phase:

- Any audible synth, test-tone, or DSP behavior.
- MIDI device handling, MIDI monitor, or controller mapping.
- Standalone MIDI persistence, patch persistence, or preset files.
- Plugin-host validation beyond preserving a successful build.
- Forking or editing JUCE source files.
- Moving audio-device state into APVTS or plugin state serialization.

## Source Anchors

This blueprint is grounded in the current implementation surface:

- `CMakeLists.txt` currently builds only the shared parameter and plugin/editor skeleton.
- `src/plugin/SynthAudioProcessor.cpp` is intentionally silent and already safe across `prepareToPlay()` and `releaseResources()` because it owns no device-specific state.
- `src/plugin/SynthAudioProcessorEditor.cpp` is the current shared UI entry point and is the correct place to host a standalone-only status panel.
- JUCE already owns standalone audio-device lifetime through `juce::StandalonePluginHolder`, which publicly exposes:
  - `AudioDeviceManager deviceManager`
  - `showAudioSettingsDialog()`
  - persistent standalone settings via `PropertySet`
- JUCE's Windows WASAPI shared device type name is `Windows Audio`; DirectSound remains a reasonable fallback.

The implementation should extend these seams, not replace them with a parallel runtime layer.

## Exact `TODO.md` Entries This Blueprint Expands

- [ ] Add standalone audio-device selection controls.
- [ ] Add standalone status display for backend, output device, sample rate, and buffer size.
- [ ] Default standalone startup to WASAPI shared mode when available.
- [ ] Handle output-device, sample-rate, and buffer-size changes without crashing.
- [ ] Keep the standalone app running when no valid output device is available.
- [ ] Verify the audio shell build and manual bring-up checks.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Use JUCE's standalone wrapper as the owner of audio-device lifetime and settings, then add a thin CoolSynth standalone integration layer around it.

Required design choices:

- Do not create a custom global `AudioDeviceManager` outside the JUCE standalone wrapper.
- Do not put audio-device enumeration, backend switching, or persistence code into `SynthAudioProcessor`.
- Use a shared editor with standalone-only UI blocks gated by runtime detection.
- Use JUCE's existing `AudioDeviceSelectorComponent` via `StandalonePluginHolder::showAudioSettingsDialog()` rather than embedding a full selector panel in the editor for this phase.
- Use a custom standalone app hook only to influence first-run backend preference and preserve wrapper-owned persistence.

Resulting ownership model:

```text
Custom standalone app bootstrap
  -> juce::StandalonePluginHolder
     -> juce::AudioDeviceManager
     -> shared SynthAudioProcessor
     -> shared SynthAudioProcessorEditor
        -> StandaloneAudioStatusPanel (standalone only)
           -> read-only snapshot of AudioDeviceManager state
           -> "Audio Settings..." action routed back to StandalonePluginHolder
```

### 1.2 Runtime State Definitions

Standalone audio state must be represented as message-thread-readable snapshots, not long-lived device objects inside the shared processor.

Required standalone state model:

```cpp
namespace coolsynth::standalone
{
    inline constexpr char preferredWasapiSharedType[] = "Windows Audio";
    inline constexpr char fallbackDirectSoundType[] = "DirectSound";
    inline constexpr char audioSetupPropertyKey[] = "audioSetup";

    struct AudioDeviceSnapshot
    {
        bool runningInStandalone = false;
        bool hasCurrentDevice = false;
        bool hasActiveOutput = false;
        juce::String backendName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;
        juce::String statusMessage;
    };

    struct BackendSelectionResult
    {
        bool persistedAudioSetupFound = false;
        bool preferredBackendAvailable = false;
        bool preferredBackendApplied = false;
        bool fallbackBackendApplied = false;
        juce::String initialBackendName;
        juce::String activeBackendName;
    };
}
```

State rules:

- `AudioDeviceSnapshot` is derived state only. It is not serialized into APVTS.
- `BackendSelectionResult` exists only to keep first-launch backend selection logic explicit and debuggable.
- `statusMessage` must always be filled. The UI should never infer device health from empty strings.
- `sampleRateHz` and `bufferSizeSamples` must remain `0` when no device is active.

### 1.3 Function Signatures and Module Boundaries

Create one standalone support module that hides all direct interaction with `juce::StandalonePluginHolder` from the shared editor header.

Required support API:

```cpp
namespace coolsynth::standalone
{
    bool isStandaloneRuntimeAvailable() noexcept;
    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept;

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot();
    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager);

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           juce::PropertySet* settings);

    bool showStandaloneAudioSettingsDialog();

    juce::String formatSampleRateHz(double sampleRateHz);
    juce::String formatBufferSizeSamples(int bufferSizeSamples);
}
```

Required UI component shape:

```cpp
class StandaloneAudioStatusPanel final : public juce::Component,
                                         private juce::ChangeListener
{
public:
    StandaloneAudioStatusPanel();
    ~StandaloneAudioStatusPanel() override;

    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshSnapshot();
    void refreshLabels();
    void handleOpenSettings();

    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::AudioDeviceSnapshot snapshot;

    juce::Label backendTitleLabel;
    juce::Label backendValueLabel;
    juce::Label outputTitleLabel;
    juce::Label outputValueLabel;
    juce::Label sampleRateTitleLabel;
    juce::Label sampleRateValueLabel;
    juce::Label bufferSizeTitleLabel;
    juce::Label bufferSizeValueLabel;
    juce::Label statusValueLabel;
    juce::TextButton audioSettingsButton { "Audio Settings..." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneAudioStatusPanel)
};
```

Required custom standalone app seam:

```cpp
#if JucePlugin_Build_Standalone
juce::JUCEApplicationBase* juce_CreateApplication();
#endif
```

### 1.4 Shared Versus Standalone Responsibilities

Shared editor responsibilities:

- Keep the existing title and phase-status labels.
- Add a standalone-only status panel region.
- Remain buildable and visually correct in VST3 mode, with no audio-device controls shown there.

Standalone support responsibilities:

- Locate the JUCE standalone holder at runtime.
- Read current device state and format it for the UI.
- Open JUCE's native audio settings dialog on request.
- Apply WASAPI shared as the first-run preferred backend only when user audio settings do not already exist.

Processor responsibilities in this phase:

- Remain silent.
- Remain free of audio-device selection logic.
- Continue to tolerate repeated `prepareToPlay()` and `releaseResources()` calls during device restarts.

### 1.5 First-Run Backend Preference Logic

The WASAPI requirement should be satisfied without overriding a user's later manual choices.

Exact behavior:

1. On standalone launch, before the main window becomes visible, create the `StandalonePluginHolder`.
2. If the wrapper settings already contain `audioSetup`, do nothing. Persisted user choice wins.
3. Otherwise inspect the currently available device types.
4. If `Windows Audio` is available and is not already active, switch the device manager to that type with `treatAsChosenDevice = false`.
5. If the switch leaves no current device, try the original device type again.
6. If no original device is usable and `DirectSound` is available, use it as fallback with `treatAsChosenDevice = false`.
7. Never write an APVTS parameter or plugin state blob to record audio backend choice.

Why `treatAsChosenDevice = false`:

- The preference is a first-run default, not an explicit user choice.
- Once the user changes audio settings from the JUCE dialog, that persisted setup should replace the startup preference.

### 1.6 Reprepare and Change-Handling Model

Audio-device, sample-rate, and buffer-size changes are already driven by `AudioDeviceManager` and `AudioProcessorPlayer`. Phase 2 should preserve that lifecycle and add only message-thread observation.

Rules:

- The status panel must listen to `AudioDeviceManager` via `ChangeListener`.
- The panel must call `captureAudioDeviceSnapshot()` only on the message thread.
- No device enumeration or UI string formatting may occur in `processBlock()`.
- Do not add listener callbacks or host-notifying parameter writes in the audio callback.
- If a device disappears and `getCurrentAudioDevice()` becomes `nullptr`, the UI must show an unavailable state without dereferencing null.

### 1.7 UI Layout Requirements

Phase 2 should keep the current placeholder visual structure, then add one compact standalone status block beneath it.

Required layout behavior:

- In standalone mode:
  - Title label remains at top.
  - Phase label remains directly below it.
  - `StandaloneAudioStatusPanel` occupies the next block in the editor.
  - The panel exposes at least these fields: backend, output device, sample rate, buffer size, status text, and an `Audio Settings...` button.
- In plugin mode:
  - The status panel is absent.
  - Existing placeholder labels still render cleanly.

## 2. File-Level Strategy

Exact file set for the Phase 2 implementation:

| Path | Change Type | Responsibility |
| --- | --- | --- |
| `CMakeLists.txt` | update | Add new standalone and UI source files and enable the custom standalone app hook via `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`. |
| `src/standalone/StandaloneAudioSupport.h` | new | Public standalone support declarations that are safe to include from shared UI code. |
| `src/standalone/StandaloneAudioSupport.cpp` | new | Runtime access to `StandalonePluginHolder`, audio snapshot capture, formatting helpers, settings-dialog routing, and first-run backend preference logic. |
| `src/standalone/CoolSynthStandaloneApp.cpp` | new | Custom standalone app bootstrap that mirrors JUCE's default standalone app, but applies first-launch backend preference before showing the window. |
| `src/ui/StandaloneAudioStatusPanel.h` | new | Standalone-only status panel declaration and component state. |
| `src/ui/StandaloneAudioStatusPanel.cpp` | new | Device-manager listener wiring, label refresh logic, and settings-button behavior. |
| `src/plugin/SynthAudioProcessorEditor.h` | update | Add standalone panel member and editor layout state needed to host it conditionally. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | update | Instantiate and lay out the standalone status panel only in standalone runtime. |
| `README.md` | update | Add short Phase 2 bring-up guidance for the standalone audio shell and mention first-run backend preference behavior. |
| `TODO.md` | update before implementation | Refresh from the Phase 2 checklist. |
| `DONE.md` | update after verification | Record only the verified Phase 2 checklist items. |

Files intentionally not touched in Phase 2:

- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/parameters/*`
- Any JUCE source file under `external/JUCE`

If device-change validation exposes a concrete processor lifecycle defect, that becomes a separate local repair. It is not the planned implementation path for this phase.

## 3. Atomic Execution Steps

Every `Phase 2` checkbox should be executed as its own `Plan -> Act -> Validate` loop.

### 3.1 Checkbox: Add standalone audio-device selection controls

Plan:

- Reuse JUCE's existing standalone `AudioDeviceSelectorComponent` through the wrapper's modal dialog rather than duplicating all selector logic inside the editor.
- Keep the selector entry point standalone-only.
- Avoid leaking standalone wrapper headers into shared UI headers.

Act:

1. Update `CMakeLists.txt` to compile new standalone support files.
2. Create `src/standalone/StandaloneAudioSupport.h/.cpp` with `showStandaloneAudioSettingsDialog()`.
3. In `StandaloneAudioSupport.cpp`, include `juce_StandaloneFilterWindow.h` only inside the `.cpp` and only for standalone-aware code paths.
4. Create `src/ui/StandaloneAudioStatusPanel.h/.cpp` with an `Audio Settings...` button.
5. Wire the button to `coolsynth::standalone::showStandaloneAudioSettingsDialog()`.
6. Update `SynthAudioProcessorEditor` so the panel is created only when `juce::JUCEApplicationBase::isStandaloneApp()` is true.

Validate:

1. Build Debug.
2. Launch the standalone app.
3. Click `Audio Settings...` and confirm JUCE's `Audio/MIDI Settings` dialog opens.
4. Confirm the VST3 target still builds and that no standalone device selector appears in plugin mode.
5. Confirm the settings button is disabled or absent only when the standalone runtime is genuinely unavailable.

### 3.2 Checkbox: Add standalone status display for backend, output device, sample rate, and buffer size

Plan:

- Build one read-only status panel that observes `AudioDeviceManager` changes on the message thread.
- Represent UI state with `AudioDeviceSnapshot` so null-device and restart states are explicit.
- Keep formatting logic in the standalone support module, not scattered across the editor.

Act:

1. Define `AudioDeviceSnapshot` in `StandaloneAudioSupport.h`.
2. Implement `captureAudioDeviceSnapshot(const juce::AudioDeviceManager&)`.
3. Implement `captureCurrentAudioDeviceSnapshot()` as the no-argument convenience entry point for UI code.
4. Implement `formatSampleRateHz()` and `formatBufferSizeSamples()`.
5. In `StandaloneAudioStatusPanel`, attach to the runtime `AudioDeviceManager` and refresh labels from a captured snapshot.
6. Surface at minimum these values:
   - backend name from `AudioDeviceManager::getCurrentAudioDeviceType()`
   - output device name from `getCurrentAudioDevice()->getName()` when present
   - sample rate from `AudioIODevice::getCurrentSampleRate()`
   - buffer size from `AudioIODevice::getCurrentBufferSizeSamples()`
   - fallback status message when no device is active

Validate:

1. Launch the standalone app with a working output device.
2. Confirm the panel shows the active backend name.
3. Confirm the panel shows the active output device name.
4. Confirm the sample-rate and buffer-size labels match the settings dialog.
5. Change one setting in the audio dialog and confirm the panel updates without reopening the app.

### 3.3 Checkbox: Default standalone startup to WASAPI shared mode when available

Plan:

- Use JUCE's custom standalone app hook rather than patching JUCE or retrofitting the preference from inside the shared processor.
- Apply the preference only on first launch, never over an existing saved audio setup.
- Fall back safely if WASAPI shared is unavailable or fails.

Act:

1. Add `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` to `CMakeLists.txt`.
2. Add `src/standalone/CoolSynthStandaloneApp.cpp` that mirrors JUCE's default standalone app bootstrap.
3. In that file, create the `StandalonePluginHolder` explicitly.
4. Immediately after holder creation, call `coolsynth::standalone::maybeApplyPreferredAudioBackend(holder->deviceManager, appProperties.getUserSettings())`.
5. In `maybeApplyPreferredAudioBackend(...)`:
   - return early if `settings` already contain `audioSetup`
   - enumerate available device types and detect `Windows Audio`
   - switch to `Windows Audio` with `treatAsChosenDevice = false` when available
   - if the switch leaves no current device, restore the original type if possible
   - if no usable original type remains and `DirectSound` exists, use `DirectSound` as fallback
6. Keep this logic standalone-only and outside shared headers except for the public function declaration.

Validate:

1. Start from a clean standalone settings state.
2. Launch the app and confirm the active backend is `Windows Audio` when that device type exists.
3. Manually switch to another backend in the settings dialog, close the app, relaunch, and confirm the user-selected backend persists.
4. Test on a machine or local setup where `Windows Audio` is unavailable or unusable and confirm the app falls back instead of opening with no window or crashing.

### 3.4 Checkbox: Handle output-device, sample-rate, and buffer-size changes without crashing

Plan:

- Let JUCE own audio restart behavior.
- Keep CoolSynth additions message-threaded and read-only relative to device state.
- Preserve the processor's current no-op audio lifecycle so restarts remain trivial.

Act:

1. Do not add device-change work to `processBlock()`.
2. Ensure `StandaloneAudioStatusPanel` only observes changes through `ChangeListener`.
3. Ensure the panel detaches from `AudioDeviceManager` in its destructor.
4. Keep all UI-string formatting and snapshot capture outside the audio callback.
5. If the editor is rebuilt while the device manager changes, reattach cleanly to the current runtime manager.

Validate:

1. Launch the standalone app.
2. Change output device repeatedly from the settings dialog.
3. Change sample rate repeatedly.
4. Change buffer size repeatedly.
5. Confirm the app window remains responsive and does not crash.
6. Confirm the panel reflects the new values after each successful change.

### 3.5 Checkbox: Keep the standalone app running when no valid output device is available

Plan:

- Treat missing-device startup and device loss as normal UI states, not exceptional control flow.
- Never assume `getCurrentAudioDevice()` is non-null.
- Keep the settings dialog reachable so the user can recover without restarting.

Act:

1. Make `captureAudioDeviceSnapshot()` null-safe.
2. Populate null-device snapshots with:
   - `hasCurrentDevice = false`
   - `hasActiveOutput = false`
   - empty device name
   - `0` sample rate and buffer size
   - status text such as `No output device available`
3. Make the status panel render this state explicitly rather than leaving stale previous values visible.
4. Ensure the settings button still works when no current device exists.
5. Ensure first-run backend preference logic can exit cleanly even if no backend can open.

Validate:

1. Launch the app with the normal output device disabled, disconnected, or otherwise unavailable.
2. Confirm the window still opens.
3. Confirm the panel reports an unavailable state instead of stale values.
4. Confirm the settings dialog still opens and allows recovery.
5. Re-enable a valid device and confirm the app recovers without a restart if JUCE reopens it successfully.

### 3.6 Checkbox: Verify the audio shell build and manual bring-up checks

Plan:

- Treat this as the phase gate.
- Validate both the new standalone UX and the shared-code safety boundary.
- Keep VST3 validation build-level only in this phase.

Act:

1. Rebuild Debug.
2. Run the manual standalone checks in the verification protocol below.
3. Confirm the VST3 artifact still builds.
4. Update `README.md` with concise audio-shell notes.
5. Refresh `TODO.md` and, only after all checks pass, move verified items to `DONE.md`.

Validate:

1. `cmake --build build --config Debug` succeeds.
2. `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe` exists and launches.
3. `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3` still exists.
4. Standalone manual checks pass on the development machine.
5. No new warnings introduced by Phase 2 are accepted as temporary.

## 4. Edge Case and Boundary Audit

- Standalone header leak: including `juce_StandaloneFilterWindow.h` directly from a shared public header risks dragging standalone-only types into VST3 compilation. Keep that include in `.cpp` files only.
- User-choice override trap: if the WASAPI preference runs on every launch, it will erase a user's explicit backend choice and violate expected standalone persistence behavior.
- Null-device dereference: `AudioDeviceManager::getCurrentAudioDevice()` may be `nullptr` at startup, after disconnect, or after an unsuccessful backend switch.
- Stale label trap: if the panel does not clear fields on null-device snapshots, it can display stale sample rates and device names from the previous device.
- Listener lifetime trap: failing to remove the `ChangeListener` in the status panel destructor can leave dangling callbacks during app shutdown.
- Audio-thread contamination: string formatting, backend switching, or host-notifying parameter writes in `processBlock()` would violate real-time rules and are not allowed.
- Persistence-boundary trap: audio backend, sample rate, and buffer size belong to JUCE standalone settings, not APVTS state.
- Wrong device-type constant trap: JUCE's WASAPI shared mode name is `Windows Audio`, not a guessed string such as `WASAPI`.
- Fallback-loop trap: if switching to `Windows Audio` fails and the code retries the same unusable type repeatedly, startup can become unstable or confusing.
- Plugin contamination trap: the shared editor must not show audio-device controls when loaded as VST3.
- Shutdown ordering trap: the standalone holder may be destroyed during application shutdown; helper code must treat runtime access as nullable at all times.
- False-capability trap: showing `Audio Settings...` in a context where no standalone holder exists should not happen.
- Sample-rate zero trap: `0` sample rate and buffer size are valid sentinel values for an unavailable device and must not be formatted as normal running values.
- Future-persistence overlap trap: Phase 12 will handle standalone settings persistence deliberately. Phase 2 should use only JUCE's existing audio settings persistence and avoid introducing a second settings file.

## 5. Verification Protocol

### 5.1 Automated Checks

1. Run `cmake --build build --config Debug`.
2. Confirm the standalone artifact exists at `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe`.
3. Confirm the VST3 artifact still exists at `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3`.
4. Review the build output and clear all new warnings introduced by this phase.

### 5.2 Manual UX Checks

1. Launch the standalone app and confirm the placeholder editor still opens.
2. Confirm the standalone status panel is visible below the Phase 1 placeholder labels.
3. Confirm the panel shows backend, output device, sample rate, buffer size, and a status message.
4. Click `Audio Settings...` and confirm JUCE's settings dialog opens.
5. On a clean standalone settings state, confirm the active backend defaults to `Windows Audio` when available.
6. Change sample rate in the settings dialog and confirm the panel updates.
7. Change buffer size and confirm the panel updates.
8. Change output device and confirm the panel updates.
9. Change backend away from `Windows Audio`, close the app, relaunch, and confirm the user-selected backend persists.
10. Launch or simulate launch with no valid output device and confirm the window still opens with an unavailable state.
11. Recover by selecting a valid device and confirm the app remains stable.

### 5.3 Review Checklist Before Moving Anything to `DONE.md`

1. Confirm no audio-device code was added to `SynthAudioProcessor`.
2. Confirm the shared editor stays clean in plugin mode.
3. Confirm first-run WASAPI preference does not override an existing saved audio setup.
4. Confirm null-device states are explicit and recoverable.
5. Confirm no JUCE source files were patched.
6. Confirm `README.md` matches the actual audio-shell behavior and current limitations.

## 6. Code Scaffolding

These snippets are structural templates for Phase 2 only. They define intended seams and ownership, not final reviewed implementation text.

### 6.1 `CMakeLists.txt`

```cmake
target_sources(CoolSynth
    PRIVATE
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/SynthAudioProcessorEditor.cpp
        src/standalone/CoolSynthStandaloneApp.cpp
        src/standalone/StandaloneAudioSupport.cpp
        src/ui/StandaloneAudioStatusPanel.cpp)

target_compile_definitions(CoolSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
```

### 6.2 `src/standalone/StandaloneAudioSupport.h`

```cpp
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

namespace coolsynth::standalone
{
    inline constexpr char preferredWasapiSharedType[] = "Windows Audio";
    inline constexpr char fallbackDirectSoundType[] = "DirectSound";
    inline constexpr char audioSetupPropertyKey[] = "audioSetup";

    struct AudioDeviceSnapshot
    {
        bool runningInStandalone = false;
        bool hasCurrentDevice = false;
        bool hasActiveOutput = false;
        juce::String backendName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;
        juce::String statusMessage;
    };

    struct BackendSelectionResult
    {
        bool persistedAudioSetupFound = false;
        bool preferredBackendAvailable = false;
        bool preferredBackendApplied = false;
        bool fallbackBackendApplied = false;
        juce::String initialBackendName;
        juce::String activeBackendName;
    };

    bool isStandaloneRuntimeAvailable() noexcept;
    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept;

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot();
    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager);

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           juce::PropertySet* settings);

    bool showStandaloneAudioSettingsDialog();

    juce::String formatSampleRateHz(double sampleRateHz);
    juce::String formatBufferSizeSamples(int bufferSizeSamples);
}
```

### 6.3 `src/standalone/StandaloneAudioSupport.cpp`

```cpp
#include "StandaloneAudioSupport.h"

#include <juce_events/juce_events.h>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace coolsynth::standalone
{
    namespace
    {
       #if JucePlugin_Build_Standalone
        juce::StandalonePluginHolder* getStandalonePluginHolder() noexcept
        {
            return juce::StandalonePluginHolder::getInstance();
        }
       #else
        void* getStandalonePluginHolder() noexcept
        {
            return nullptr;
        }
       #endif
    }

    bool isStandaloneRuntimeAvailable() noexcept
    {
        return juce::JUCEApplicationBase::isStandaloneApp()
            && getStandaloneAudioDeviceManager() != nullptr;
    }

    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept
    {
       #if JucePlugin_Build_Standalone
        if (auto* holder = getStandalonePluginHolder())
            return &holder->deviceManager;
       #endif

        return nullptr;
    }

    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager)
    {
        AudioDeviceSnapshot snapshot;
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.backendName = deviceManager.getCurrentAudioDeviceType();

        if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
        {
            snapshot.hasCurrentDevice = true;
            snapshot.hasActiveOutput = currentDevice->getActiveOutputChannels().countNumberOfSetBits() > 0;
            snapshot.outputDeviceName = currentDevice->getName();
            snapshot.sampleRateHz = currentDevice->getCurrentSampleRate();
            snapshot.bufferSizeSamples = currentDevice->getCurrentBufferSizeSamples();
            snapshot.statusMessage = snapshot.hasActiveOutput ? "Ready" : "Device open with no active output channels";
        }
        else
        {
            snapshot.statusMessage = "No output device available";
        }

        return snapshot;
    }
}
```

### 6.4 `src/ui/StandaloneAudioStatusPanel.h`

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "standalone/StandaloneAudioSupport.h"

class StandaloneAudioStatusPanel final : public juce::Component,
                                         private juce::ChangeListener
{
public:
    StandaloneAudioStatusPanel();
    ~StandaloneAudioStatusPanel() override;

    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshSnapshot();
    void refreshLabels();
    void handleOpenSettings();

    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::AudioDeviceSnapshot snapshot;

    juce::Label backendTitleLabel;
    juce::Label backendValueLabel;
    juce::Label outputTitleLabel;
    juce::Label outputValueLabel;
    juce::Label sampleRateTitleLabel;
    juce::Label sampleRateValueLabel;
    juce::Label bufferSizeTitleLabel;
    juce::Label bufferSizeValueLabel;
    juce::Label statusValueLabel;
    juce::TextButton audioSettingsButton { "Audio Settings..." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneAudioStatusPanel)
};
```

### 6.5 `src/plugin/SynthAudioProcessorEditor.cpp`

```cpp
#include "ui/StandaloneAudioStatusPanel.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
{
    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Phase 2 standalone audio shell", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        standaloneAudioPanel = std::make_unique<StandaloneAudioStatusPanel>();
        addAndMakeVisible(*standaloneAudioPanel);
    }

    setSize(900, 600);
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(48));
    statusLabel.setBounds(area.removeFromTop(32));

    if (standaloneAudioPanel != nullptr)
    {
        area.removeFromTop(16);
        standaloneAudioPanel->setBounds(area.removeFromTop(160));
    }
}
```

### 6.6 `src/standalone/CoolSynthStandaloneApp.cpp`

```cpp
#if JucePlugin_Build_Standalone

#include "StandaloneAudioSupport.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
class CoolSynthStandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override   { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override         { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            createPluginHolder());

        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

private:
    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder>(
            appProperties.getUserSettings(),
            false,
            juce::String{},
            nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
            false);

        coolsynth::standalone::maybeApplyPreferredAudioBackend(holder->deviceManager,
                                                               appProperties.getUserSettings());
        return holder;
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
}

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new CoolSynthStandaloneApp();
}

#endif
```
