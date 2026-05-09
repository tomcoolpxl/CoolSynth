<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 14 Blueprint: Patch Save/Load Workflow

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 14 - Patch save/load workflow`

Selection basis:

- `TODO.md` is currently blank, so it is not carrying an active execution slice.
- `README.md` states that `Phase 13: MIDI learn workflow` is complete, which makes `Phase 14` the next unsliced milestone.
- `IMPLEMENTATION_PLAN.md` defines `Phase 14` as the patch-state milestone after APVTS host-state validation, standalone device persistence, and MIDI learn boundary work.
- The current codebase already has the critical prerequisites in place:
  - `SynthAudioProcessor` already serializes and restores APVTS state through XML in `getStateInformation()` and `setStateInformation()`.
  - `ParameterLayout.cpp` already defines the canonical default values for every automatable synth parameter.
  - `SynthAudioProcessorEditor` already uses APVTS attachments, so UI restoration should fall out of parameter-state replacement instead of requiring a second UI-state mechanism.
  - `StandaloneSettingsStore` already persists standalone-only state and learned mappings separately from APVTS state, which is the exact boundary Phase 14 must preserve.

## Working Hypothesis

Phase 14 is not a preset-browser problem.

It is a parameter-state snapshot problem with one boundary requirement:

- expose an init-patch action,
- save the current synth parameter state into a small inspectable file,
- load that file back into APVTS,
- and keep standalone audio settings, standalone MIDI settings, and learned MIDI mappings entirely outside the patch file.

That leads to one controlling design decision for this phase:

- treat a patch file as a thin XML wrapper around the exact APVTS parameter-state XML that the processor already knows how to export and restore.

Cheap disconfirming checks already satisfied by the current code:

1. `src/plugin/SynthAudioProcessor.cpp` already exports APVTS state through `parameters.copyState().createXml()`.
2. `src/plugin/SynthAudioProcessor.cpp` already restores APVTS state through `parameters.replaceState(...)`.
3. `src/plugin/SynthAudioProcessorEditor.cpp` already binds every synth control to APVTS, so parameter-state replacement is already the cheapest route to UI restoration.
4. `src/standalone/SettingsStore.cpp` already persists `audioSetup`, `midiInputIdentifier`, `midiInputName`, and `midiLearnMappings` outside the processor state.

That means the smallest correct Phase 14 change is:

- a new patch-state module for wrapper parsing and file I/O,
- a processor helper surface that reuses the existing APVTS state logic,
- and a narrow editor action surface for init, save, and load.

It does not require:

- a preset browser,
- a second state model,
- changes to standalone settings persistence,
- or any synth-engine work.

## Scope Guardrails

In scope for this phase:

- Add an explicit `Init Patch` action that resets every automatable synth parameter to its default value.
- Save the current synth parameter state to a patch file using an XML wrapper around APVTS state serialization.
- Load a saved patch file back into APVTS.
- Restore both sound and editor control state from the loaded parameter state.
- Keep standalone device settings and learned MIDI mappings out of patch files.
- Add focused automated tests for patch wrapping, parsing, reset-to-defaults, and state round-trip behavior.

Explicitly out of scope for this phase:

- Preset browsing, patch banks, patch categories, recent-files lists, or file-history UI.
- Persisting the last loaded patch path or auto-loading the last patch on startup.
- Any change to the standalone settings file format.
- Any change to MIDI learn persistence or mapping precedence.
- Any new DAW-host preset integration beyond the APVTS state behavior already validated in `Phase 11`.
- Any change to parameter IDs, parameter ranges, default values, DSP behavior, or synth voice allocation.
- Any attempt to make patch morphing seamless during playback.

Default implementation assumption for this blueprint:

- the patch-state engine is shared and target-neutral,
- but the first user-facing patch buttons should be exposed in the standalone editor only.

Reason:

- the requirements require a patch workflow, not DAW-specific dirty-state integration,
- the project is still standalone-first,
- and keeping the first UI surface in standalone avoids widening this phase into host undo/dirty-state behavior that is not part of the current exit criteria.

If the reviewer explicitly wants patch buttons visible inside the VST3 editor too, the file format and processor helpers in this blueprint remain valid. Only the editor visibility gate and validation surface would widen.

Non-negotiable constraints:

- Patch files must contain synth parameter state only.
- Patch files must not contain `audioSetup`, remembered MIDI-device selection, or `midiLearnMappings`.
- `copyState()` and `replaceState()` must remain off the audio thread.
- The init-patch path must derive defaults from the live parameter objects, not from a duplicated hand-written constant table.
- `TODO.md` remains sourced from the six `Phase 14` checklist items in `IMPLEMENTATION_PLAN.md` until implementation starts.

## Current Code Anchors

These files already own the behavior that Phase 14 must extend.

- `src/plugin/SynthAudioProcessor.h` and `.cpp`
  - Already own APVTS lifetime and the current plugin-state XML path.
  - Are the correct place to expose reusable parameter-state export/import helpers and the init-patch reset helper.

- `src/plugin/SynthAudioProcessorEditor.h` and `.cpp`
  - Already own the shared editor and the APVTS attachments.
  - Already host standalone-only workflows such as MIDI learn and the standalone status bar.
  - Are the correct place for standalone patch buttons and file-chooser callbacks.

- `src/parameters/ParameterLayout.cpp`
  - Already defines the parameter defaults that `Init Patch` must restore.
  - Must remain the single source of truth for defaults.

- `src/standalone/SettingsStore.h` and `.cpp`
  - Already isolate standalone-only state and learned mappings from APVTS state.
  - Should remain untouched in the normal implementation path.

- `CMakeLists.txt`
  - Uses an explicit `target_sources(...)` list for the plugin target and the test target.
  - Any new `.cpp` file for Phase 14 must be added there.

- `tests/MidiLearnTests.cpp`
  - Shows the current JUCE `UnitTest` style and confirms the project already has a small console-test harness.
  - Phase 14 should add new patch-state coverage in the same lightweight style.

## Exact `TODO.md` Entries This Blueprint Expands

These are the exact `Phase 14` checklist items from `IMPLEMENTATION_PLAN.md` that this blueprint expands into an execution guide when `TODO.md` is refreshed:

- [ ] Add an init-patch action that resets automatable synth parameters to defaults.
- [ ] Add patch save for synth parameter state.
- [ ] Add patch load for synth parameter state.
- [ ] Restore UI state when a saved patch is loaded.
- [ ] Keep standalone device settings and learned mappings out of patch files.
- [ ] Verify patch save and load restore sound correctly.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Do not invent a second parameter serializer.

Required consequence:

- The processor remains the source of truth for parameter-state export and import.
- The new patch module is responsible only for wrapping that parameter-state XML in a patch document, reading it back from disk, validating the outer document, and writing it atomically.

Why this is the correct minimum design:

- It reuses the APVTS state path that `Phase 11` already validated in a host context.
- It keeps the patch format parameter-only by construction.
- It avoids introducing a preset-only model that could drift from plugin state.
- It keeps file I/O out of the processor and out of any realtime path.

### 1.2 Patch Content Contract

Phase 14 patch files must contain exactly the current automatable synth parameters and nothing else.

Included in the patch payload:

| Parameter ID | Included | Default Source | Notes |
| --- | --- | --- | --- |
| `waveform` | Yes | `AudioParameterChoice::getDefaultValue()` | Choice parameter; restore to `saw` on init patch |
| `ampAttackMs` | Yes | Parameter default | |
| `ampDecayMs` | Yes | Parameter default | |
| `ampSustain` | Yes | Parameter default | |
| `ampReleaseMs` | Yes | Parameter default | |
| `filterCutoffHz` | Yes | Parameter default | |
| `filterResonance` | Yes | Parameter default | |
| `delayTimeMs` | Yes | Parameter default | |
| `delayFeedback` | Yes | Parameter default | |
| `delayMix` | Yes | Parameter default | |
| `masterGainDb` | Yes | Parameter default | |

Explicitly excluded from the patch payload:

| State Surface | Owner | Excluded Reason |
| --- | --- | --- |
| Audio backend and output-device selection | Standalone settings / JUCE `audioSetup` | Runtime environment, not synth sound |
| MIDI input-device selection | Standalone settings | Runtime environment, not synth sound |
| Learned MIDI mappings | Standalone settings + MIDI learn manager | Controller customization, not synth sound |
| MIDI monitor state | Standalone UI | Debug-only surface |
| Last loaded patch path | None in Phase 14 | Explicitly deferred persistence |

### 1.3 Patch File Shape

The patch document should stay minimal and inspectable.

Required outer XML shape:

```xml
<COOLSYNTH_PATCH formatVersion="1" product="CoolSynth" stateType="CoolSynthState">
  <CoolSynthState>
    <!-- exact APVTS XML produced by parameters.copyState().createXml() -->
  </CoolSynthState>
</COOLSYNTH_PATCH>
```

Required properties of this format:

- The root tag is stable and product-specific.
- The child APVTS state element remains untouched so patch load and plugin-state load use the same internal state shape.
- The root contains only enough metadata to validate the document boundary:
  - `formatVersion`
  - `product`
  - `stateType`

Do not add for Phase 14:

- patch name fields,
- author fields,
- categories,
- tags,
- comments,
- ratings,
- controller mappings,
- or last-used device hints.

### 1.4 Required Data Structures and State Definitions

#### 1.4.1 Patch Format Constants and Error Model

Add a new module under `src/presets/PatchState.h` and `.cpp`.

```cpp
namespace coolsynth::presets
{
    inline constexpr char patchRootTag[] = "COOLSYNTH_PATCH";
    inline constexpr int patchFormatVersion = 1;
    inline constexpr char defaultPatchExtension[] = ".cspatch";

    enum class PatchStateError : uint8_t
    {
        none,
        fileReadFailed,
        fileWriteFailed,
        invalidXmlDocument,
        invalidRootTag,
        unsupportedFormatVersion,
        missingParameterState,
        ambiguousParameterState,
        unexpectedStateType,
        applyFailed,
    };

    struct PatchIoResult
    {
        PatchStateError error = PatchStateError::none;
        juce::String message;

        bool succeeded() const noexcept
        {
            return error == PatchStateError::none;
        }
    };

    struct PatchLoadResult : PatchIoResult
    {
        std::unique_ptr<juce::XmlElement> parameterStateXml;
    };
}
```

Why this exact shape is needed:

- the editor needs a user-facing message on failure,
- save and load need different result payloads,
- and parser failures need to be explicit so malformed files fail softly instead of quietly mutating state.

#### 1.4.2 Processor-Owned State Helpers

The processor should expose reusable parameter-state helpers instead of duplicating XML logic in the editor or the patch module.

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    std::unique_ptr<juce::XmlElement> createParameterStateXml() const;
    bool applyParameterStateXml(const juce::XmlElement& stateXml);
    void resetAutomatableParametersToDefaults();
    juce::String getParameterStateTypeName() const;

private:
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                      float normalizedValue);
};
```

Rules:

- `createParameterStateXml()` is the one place that should call `parameters.copyState()` for patch export.
- `applyParameterStateXml(...)` is the one place that should call `parameters.replaceState(...)` for patch import and for `setStateInformation(...)` reuse.
- `resetAutomatableParametersToDefaults()` must loop the current parameter objects and call `getDefaultValue()` on each one. It must not duplicate the default values from `ParameterLayout.cpp`.
- `applyNormalizedParameterValue(...)` should centralize gesture + `setValueNotifyingHost()` behavior so init-patch writes do not invent a second host-write path.

#### 1.4.3 Editor Action State

The editor only needs minimal new state.

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
private:
    void triggerInitPatch();
    void triggerSavePatch();
    void triggerLoadPatch();
    void launchPatchSaveChooser();
    void launchPatchLoadChooser();
    void handlePatchSaveSelection(const juce::File& selectedFile);
    void handlePatchLoadSelection(const juce::File& selectedFile);
    void showPatchError(juce::String message);

    juce::TextButton initPatchButton { "Init Patch" };
    juce::TextButton savePatchButton { "Save Patch" };
    juce::TextButton loadPatchButton { "Load Patch" };
    std::unique_ptr<juce::FileChooser> activePatchChooser;
    bool patchActionsVisible = false;
};
```

Rules:

- keep chooser lifetime in the editor so async callbacks remain valid,
- keep all file I/O initiated from the message thread,
- and keep patch UI state separate from MIDI learn state.

Do not add for Phase 14:

- unsaved-change tracking,
- patch-name text fields,
- patch list widgets,
- or a standalone settings-store dependency.

### 1.5 State Ownership and Boundary Rules

| State Surface | Owner | Persistence Path | Phase 14 Rule |
| --- | --- | --- | --- |
| Synth parameter state | `SynthAudioProcessor::parameters` | APVTS XML, plugin state, patch file | Shared and allowed |
| Standalone audio configuration | JUCE standalone holder + `audioSetup` | Standalone settings file | Must not appear in patch files |
| Standalone MIDI-device selection | `StandaloneSettingsStore` | Standalone settings file | Must not appear in patch files |
| Learned MIDI mappings | `MidiLearnManager` + `StandaloneSettingsStore` | Standalone settings file | Must not appear in patch files |
| Patch action buttons | Shared editor, standalone visibility gate | No persistence | Standalone-only in Phase 14 |

Critical boundary:

- It is acceptable for a loaded patch to change any of the 11 automatable parameters.
- It is not acceptable for a patch load to touch `StandaloneSettingsStore`, `StandaloneMidiInputController`, or standalone audio-device setup.

### 1.6 Runtime Flow

#### 1.6.1 Init Patch Flow

Required flow:

1. The user clicks `Init Patch`.
2. The editor calls `processor.resetAutomatableParametersToDefaults()` on the message thread.
3. The processor iterates every current `juce::RangedAudioParameter` and applies its normalized default value with host-notifying writes.
4. Existing APVTS attachments move the waveform selector, knobs, and fader automatically.
5. The editor calls `refreshValueDisplays()` once so the textual value labels do not wait for the next timer tick.

Behavioral rule:

- `Init Patch` resets parameter state only.
- It does not clear learned mappings.
- It does not change the selected MIDI device.
- It does not change the audio device.
- It does not call panic automatically.

#### 1.6.2 Save Patch Flow

Required flow:

1. The user clicks `Save Patch`.
2. The editor launches an async save chooser in standalone mode with a `*.cspatch` filter.
3. If the user cancels, the action becomes a no-op.
4. If the user omits the extension, the editor appends `.cspatch`.
5. The editor requests `processor.createParameterStateXml()`.
6. The patch module wraps the returned APVTS state XML in the `COOLSYNTH_PATCH` document.
7. The patch module writes the document to disk using `juce::TemporaryFile` so overwrite failure does not leave a half-written patch.
8. On failure, the editor shows a message-box error and leaves all runtime state untouched.

#### 1.6.3 Load Patch Flow

Required flow:

1. The user clicks `Load Patch`.
2. The editor launches an async open chooser in standalone mode with a `*.cspatch` filter.
3. If the user cancels, the action becomes a no-op.
4. The patch module reads the XML file and validates:
   - root tag,
   - supported `formatVersion`,
   - exactly one APVTS state child,
   - expected `stateType`.
5. The editor passes the extracted APVTS child to `processor.applyParameterStateXml(...)`.
6. APVTS attachments update the waveform selector and all sliders automatically.
7. The editor calls `refreshValueDisplays()` once so text labels update immediately.
8. Learned mapping badges remain as they were because learned mappings are separate state.
9. On any load error, the editor shows a message-box error and leaves the current parameter state unchanged.

#### 1.6.4 Plugin-State Reuse Rule

`getStateInformation()` and `setStateInformation()` should delegate to the new processor helpers rather than keeping a second copy of the APVTS XML logic.

Required consequence:

- plugin-state save and patch save use the same exported APVTS child XML,
- plugin-state restore and patch load use the same APVTS child import logic,
- and future parameter-state regressions have one correction point.

### 1.7 Required Function Signatures

#### 1.7.1 New Patch Module Surface

Add `src/presets/PatchState.h` and `.cpp` with these signatures.

```cpp
namespace coolsynth::presets
{
    std::unique_ptr<juce::XmlElement> createWrappedPatchXml(const juce::XmlElement& parameterStateXml,
                                                            juce::StringRef stateType);

    PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                         juce::StringRef expectedStateType);

    PatchIoResult writePatchFile(const juce::File& destination,
                                 const juce::XmlElement& patchXml);

    PatchLoadResult readPatchFile(const juce::File& source,
                                  juce::StringRef expectedStateType);
}
```

Parser policy:

- reject unsupported wrapper versions,
- reject wrong root tags,
- reject missing or duplicate APVTS child nodes,
- and ignore unknown future wrapper attributes only if one valid APVTS child still exists.

#### 1.7.2 Processor Helper Surface

Extend `src/plugin/SynthAudioProcessor.h` and `.cpp`.

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    std::unique_ptr<juce::XmlElement> createParameterStateXml() const;
    bool applyParameterStateXml(const juce::XmlElement& stateXml);
    void resetAutomatableParametersToDefaults();
    juce::String getParameterStateTypeName() const;

private:
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                      float normalizedValue);
};
```

Expected implementation policy:

- `getStateInformation()` should call `createParameterStateXml()` and then `copyXmlToBinary(...)`.
- `setStateInformation()` should decode the binary XML and then call `applyParameterStateXml(...)`.
- `resetAutomatableParametersToDefaults()` should iterate the processor's current parameters so any future parameter additions naturally participate unless explicitly excluded in a later design change.

#### 1.7.3 Editor Surface

Extend `src/plugin/SynthAudioProcessorEditor.h` and `.cpp`.

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
private:
    void triggerInitPatch();
    void triggerSavePatch();
    void triggerLoadPatch();
    void launchPatchSaveChooser();
    void launchPatchLoadChooser();
    void handlePatchSaveSelection(const juce::File& selectedFile);
    void handlePatchLoadSelection(const juce::File& selectedFile);
    void showPatchError(juce::String message);

    juce::TextButton initPatchButton { "Init Patch" };
    juce::TextButton savePatchButton { "Save Patch" };
    juce::TextButton loadPatchButton { "Load Patch" };
    std::unique_ptr<juce::FileChooser> activePatchChooser;
    bool patchActionsVisible = false;
};
```

Layout rule:

- add the patch buttons to the existing header row near `All Notes Off` rather than creating a new patch-management panel.

Why this is enough:

- the phase is about state actions, not preset browsing,
- and the existing editor already has the correct global-action surface.

## 2. File-Level Strategy

### 2.1 Exact Files to Touch

This is the exact file list for a surgical Phase 14 implementation.

| File | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Add `src/presets/PatchState.cpp` to the plugin target and add `tests/PatchStateTests.cpp` to the existing test target. |
| `src/presets/PatchState.h` | Define patch-format constants, error/result structs, and the patch file I/O/parsing API. |
| `src/presets/PatchState.cpp` | Implement wrapper creation, XML parsing, version validation, atomic writes, and file reads. |
| `src/plugin/SynthAudioProcessor.h` | Declare reusable parameter-state export/import helpers and the init-patch reset helper. |
| `src/plugin/SynthAudioProcessor.cpp` | Implement the helper surface, factor existing APVTS XML logic through it, and centralize normalized parameter writes for init patch. |
| `src/plugin/SynthAudioProcessorEditor.h` | Add standalone-only patch buttons, chooser lifetime state, and action methods. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Wire the patch buttons, show async file choosers, call the patch module, refresh value displays after init/load, and surface errors safely. |
| `tests/PatchStateTests.cpp` | Add focused JUCE unit tests for wrapper validation, reset-to-defaults, patch round-trip, and state-boundary exclusion. |
| `README.md` | Document the patch workflow, the `.cspatch` format, standalone-only visibility for Phase 14, and exactly what patch files do and do not contain. |
| `TODO.md` | Refresh from the six Phase 14 checklist items before implementation starts. |
| `DONE.md` | Record the milestone only after all manual and automated checks pass. |

### 2.2 Files to Leave Alone Unless a Local Defect Forces a Change

These files should remain untouched in the normal Phase 14 path.

- `src/standalone/SettingsStore.h` and `.cpp`
  - These files already define the standalone-only persistence boundary.
  - Touching them would risk mixing patch content with runtime settings.

- `src/standalone/StandaloneAudioSupport.*`
  - Patch save/load does not depend on audio-device enumeration or restore logic.

- `src/standalone/StandaloneMidiInput.*`
  - Patch save/load does not depend on MIDI-device selection or disconnect handling.

- `src/midi/MidiLearn.*`
  - Learned mappings must survive patch load unchanged.
  - Phase 14 should consume that boundary, not modify it.

- `src/parameters/ParameterIDs.h`
  - Parameter IDs are already persistent API.
  - Any rename here would create an avoidable patch-compatibility regression.

- `src/parameters/ParameterLayout.cpp`
  - Defaults already exist here.
  - `Init Patch` must use parameter APIs to read defaults rather than rewriting them.

- `src/ui/*`
  - No dedicated patch browser or patch strip should be added in this phase.
  - Existing editor header actions are sufficient.

- `src/synth/**`
  - Synth DSP and voice code are out of scope for a patch-state workflow.

## 3. Atomic Execution Steps

Each item below expands one high-level `Phase 14` checkbox into a concrete `Plan -> Act -> Validate` cycle.

### 3.1 TODO Item 1: Add an Init-Patch Action That Resets Automatable Synth Parameters to Defaults

Plan:

- Reuse the existing APVTS parameter inventory instead of building a second list of hard-coded defaults.
- Centralize host-notifying parameter writes so init-patch behavior stays consistent with the existing mapped-CC write path.

Act:

- Add `SynthAudioProcessor::applyNormalizedParameterValue(...)` as a small helper around `beginChangeGesture()`, `setValueNotifyingHost(...)`, and `endChangeGesture()`.
- Add `SynthAudioProcessor::resetAutomatableParametersToDefaults()` that iterates all current `juce::RangedAudioParameter` instances and applies `getDefaultValue()`.
- Add an `Init Patch` button to the editor header row, visible only in standalone mode for Phase 14.
- Wire `Init Patch` to call the new processor reset helper and then `refreshValueDisplays()`.

Validate:

- Move every control away from default, trigger `Init Patch`, and confirm all 11 automatable parameters return to defaults.
- Confirm waveform returns to `saw`, delay mix returns to `0.0`, and master gain returns to `-12 dB` through the current value displays.
- Confirm learned mapping badges remain intact after init patch.
- Confirm audio-device and MIDI-device selections remain unchanged.

### 3.2 TODO Item 2: Add Patch Save for Synth Parameter State

Plan:

- Introduce one narrow patch module that knows how to wrap APVTS XML and write it safely to disk.
- Keep the processor responsible for producing the APVTS child XML.

Act:

- Add `src/presets/PatchState.h` and `.cpp`.
- Define `COOLSYNTH_PATCH` wrapper constants, result structs, and `.cspatch` extension handling.
- Implement `createWrappedPatchXml(...)` that adds the APVTS child under the patch root.
- Implement `writePatchFile(...)` using `juce::TemporaryFile` to avoid partial overwrite damage.
- Add `Save Patch` button wiring in the editor and launch an async save chooser.
- Append `.cspatch` when the user saves without an extension.

Validate:

- Save a patch and verify the file exists.
- Open the file and confirm it is XML with a `COOLSYNTH_PATCH` root and a single `CoolSynthState` child.
- Confirm the serialized file does not contain `audioSetup`, `midiInputIdentifier`, `midiInputName`, or `midiLearnMappings`.
- Confirm canceling the save chooser produces no file and no parameter changes.

### 3.3 TODO Item 3: Add Patch Load for Synth Parameter State

Plan:

- Parse and validate the wrapper document before touching APVTS.
- Reuse a processor-owned APVTS import helper so patch load and plugin-state restore do not drift.

Act:

- Implement `readPatchFile(...)` and `parseWrappedPatchXml(...)` in the new patch module.
- Validate the root tag, format version, exact APVTS child count, and expected state type before returning success.
- Add `SynthAudioProcessor::applyParameterStateXml(...)` and route `setStateInformation(...)` through the same helper.
- Add `Load Patch` button wiring in the editor and launch an async open chooser.
- On successful parse, apply the returned APVTS child XML and refresh the value text immediately.
- On failure, show a message-box error and leave the current parameter state untouched.

Validate:

- Save a patch, move every parameter to a clearly different state, then load the saved patch and confirm the original sound returns.
- Load a malformed XML file and confirm the app stays running and surfaces a clear error.
- Load a well-formed XML file with the wrong root tag and confirm it is rejected.
- Confirm canceling the load chooser is a no-op.

### 3.4 TODO Item 4: Restore UI State When a Saved Patch Is Loaded

Plan:

- Rely on the existing APVTS attachments for UI restoration rather than inventing a second editor-state snapshot.
- Only add the smallest explicit refresh needed for the textual value labels.

Act:

- Ensure patch load applies state through APVTS on the message thread.
- Do not set slider values or combo-box state manually in parallel with attachments.
- After a successful load, call `refreshValueDisplays()` once so the label strings update immediately instead of waiting for the next timer tick.
- Keep `paint()` and layout code unchanged except for the new button placement.

Validate:

- After patch load, confirm the waveform combo box, all knob positions, the master fader position, and all displayed text values match the saved sound.
- Confirm there is no stale value-label lag after load.
- Confirm loading a patch does not break existing learn badges or the standalone status bar.

### 3.5 TODO Item 5: Keep Standalone Device Settings and Learned Mappings Out of Patch Files

Plan:

- Preserve the existing state boundary by design: patch write paths use processor APVTS state only and never call `StandaloneSettingsStore`.
- Add automated coverage that inspects the generated patch XML rather than trusting separation implicitly.

Act:

- Keep the new patch module free of any dependency on `src/standalone/**` or `src/midi/MidiLearn.*`.
- Do not add any patch-related keys to `StandaloneSettingsStore`.
- Keep patch buttons and chooser logic inside the editor only.
- Add a focused test that verifies the generated patch document contains only the patch wrapper and APVTS state child.
- Add a focused test that confirms loading a patch does not require or mutate learned mappings.

Validate:

- Before patch load, create or retain one learned CC mapping and a remembered MIDI-device selection.
- Load a patch and confirm the learned mapping still works afterward.
- Confirm the selected MIDI input and audio-device status remain unchanged after load.
- Confirm the patch XML contains no standalone keys by string inspection in test coverage.

### 3.6 TODO Item 6: Verify Patch Save and Load Restore Sound Correctly

Plan:

- Add one small automated round-trip test for parameter equivalence and one manual sound-check flow for audible confirmation.
- Keep validation focused on patch-state correctness, not on preset-browser UX.

Act:

- Add `tests/PatchStateTests.cpp` with a JUCE `UnitTest` class.
- Cover:
  - init-patch default reset,
  - wrapper creation and parse validation,
  - patch file round-trip into a fresh processor instance,
  - malformed-file rejection,
  - boundary exclusion for standalone keys.
- Update `CMakeLists.txt` so the patch tests build with the current console-test executable.

Validate:

- A saved patch loaded into a fresh processor instance reproduces the saved normalized parameter values.
- Manual listening confirms a deliberately distinctive patch restores audibly after loading.
- Existing MIDI learn tests still pass alongside the new patch tests.
- The project still builds cleanly with no new warnings.

## 4. Edge Case and Boundary Audit

These are the concrete failure modes and logic traps relevant to Phase 14.

- User cancels the save chooser.
  - Required behavior: no file is written and no state changes.

- User cancels the load chooser.
  - Required behavior: no file is read and no state changes.

- User saves without a file extension.
  - Required behavior: append `.cspatch` automatically.

- Existing file is overwritten and the write fails midway.
  - Required behavior: use `juce::TemporaryFile` so the original file is preserved on failure.

- File exists but is not valid XML.
  - Required behavior: reject with a clear error and keep the current synth state untouched.

- XML is valid but root tag is not `COOLSYNTH_PATCH`.
  - Required behavior: reject as an incompatible patch document.

- Wrapper root is valid but the APVTS state child is missing.
  - Required behavior: reject.

- Wrapper root is valid but multiple matching APVTS state children exist.
  - Required behavior: reject as ambiguous.

- Wrapper version is newer than the loader supports.
  - Required behavior: reject instead of guessing.

- Wrapper root advertises the wrong `stateType` or the APVTS child tag does not match the processor state type.
  - Required behavior: reject.

- Patch load occurs while notes are currently sounding.
  - Required behavior: parameter state updates live and safely; seamless morphing is not required for Phase 14.

- Patch load is attempted while no audio device is active.
  - Required behavior: state still loads, UI updates, and the app remains open.

- Patch load is attempted while the remembered MIDI device is missing.
  - Required behavior: patch still loads because device selection is orthogonal to synth state.

- Learned mappings exist when the patch is saved.
  - Required behavior: they keep working after patch load because the patch file does not touch them.

- `Init Patch` is triggered after the user learned custom CC mappings.
  - Required behavior: only parameter values reset; mappings remain.

- Future parameters are added after Phase 14.
  - Required behavior for this phase: `Init Patch` automatically includes them because it iterates current parameters.
  - Compatibility rule: patch load accepts only same-format wrapper documents and same-state-type APVTS payloads; no migration layer is required in Phase 14.

- `copyState()` or `replaceState()` are called from the audio thread.
  - Required behavior: this must never happen; keep all patch export/import actions on the message thread only.

- A developer is tempted to persist the last loaded patch path in `SettingsStore`.
  - Required behavior: do not do this in Phase 14 because `Last loaded patch` is explicitly deferred.

- A developer is tempted to create a patch browser or preset strip.
  - Required behavior: reject the scope expansion; the existing header action row is sufficient.

## 5. Verification Protocol

### 5.1 Automated Validation Checklist

1. Build the project in Debug and confirm the new patch module and test file compile without warnings.
2. Run the existing JUCE unit-test executable and confirm the new patch-state tests pass alongside the existing MIDI learn tests.
3. Confirm the automated suite includes at least these patch tests:
   - `init_patch_resets_all_automatable_parameters_to_defaults`
   - `patch_wrapper_contains_expected_root_and_single_apvts_child`
   - `patch_round_trip_restores_parameter_values_in_fresh_processor`
   - `patch_loader_rejects_wrong_root_or_missing_state_child`
   - `patch_xml_excludes_standalone_and_midi_learn_keys`
4. Inspect the serialized patch XML in a test and confirm it does not contain `audioSetup`, `midiInputIdentifier`, `midiInputName`, or `midiLearnMappings`.
5. Confirm `getStateInformation()` and `setStateInformation()` still work by routing through the new processor helpers rather than diverging from them.

### 5.2 Manual UX Checklist

1. Launch the standalone app and confirm `Init Patch`, `Save Patch`, and `Load Patch` are visible in the editor header.
2. Set a deliberately distinctive sound:
   - waveform to `square` or `sine`,
   - attack and release clearly non-default,
   - cutoff reduced,
   - resonance increased,
   - delay mix above zero,
   - master gain adjusted.
3. Save that sound to a `.cspatch` file.
4. Change every control to a clearly different sound.
5. Load the saved `.cspatch` file.
6. Confirm the audible sound matches the saved patch again.
7. Confirm the waveform selector, knob positions, fader position, and value text all match the restored sound.
8. Trigger `Init Patch` and confirm the editor returns to the default sound and default visual state.
9. Create or retain one learned MIDI CC mapping, then load a patch and confirm the learned mapping still works.
10. Note the active standalone audio backend/device and active MIDI input, then load a patch and confirm both remain unchanged.
11. Cancel the save chooser and confirm nothing changes.
12. Cancel the load chooser and confirm nothing changes.
13. Try loading a deliberately invalid XML file and confirm the app stays open and reports the error.
14. Restart the standalone app and confirm no last-loaded patch is auto-restored, because that persistence is still deferred.

### 5.3 Exit Gate

The phase is ready to move out of `TODO.md` only when all of the following are true:

- `Init Patch` resets every automatable parameter to its current parameter-object default.
- Patch save and load round-trip the intended synth sound.
- Patch load restores the visible editor control state without manual control resynchronization hacks.
- Standalone device settings and learned mappings remain outside the patch file and unchanged by patch load.
- Malformed or incompatible patch files fail softly.
- The docs explain exactly what a patch file contains and what it does not contain.

## 6. Code Scaffolding

The following scaffolding is intentionally minimal and aligned with the current codebase style.

### 6.1 `src/presets/PatchState.h`

```cpp
#pragma once

#include <memory>

#include <juce_core/juce_core.h>

namespace coolsynth::presets
{
    inline constexpr char patchRootTag[] = "COOLSYNTH_PATCH";
    inline constexpr int patchFormatVersion = 1;
    inline constexpr char defaultPatchExtension[] = ".cspatch";

    enum class PatchStateError : uint8_t
    {
        none,
        fileReadFailed,
        fileWriteFailed,
        invalidXmlDocument,
        invalidRootTag,
        unsupportedFormatVersion,
        missingParameterState,
        ambiguousParameterState,
        unexpectedStateType,
        applyFailed,
    };

    struct PatchIoResult
    {
        PatchStateError error = PatchStateError::none;
        juce::String message;

        bool succeeded() const noexcept
        {
            return error == PatchStateError::none;
        }
    };

    struct PatchLoadResult : PatchIoResult
    {
        std::unique_ptr<juce::XmlElement> parameterStateXml;
    };

    std::unique_ptr<juce::XmlElement> createWrappedPatchXml(const juce::XmlElement& parameterStateXml,
                                                            juce::StringRef stateType);

    PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                         juce::StringRef expectedStateType);

    PatchIoResult writePatchFile(const juce::File& destination,
                                 const juce::XmlElement& patchXml);

    PatchLoadResult readPatchFile(const juce::File& source,
                                  juce::StringRef expectedStateType);
}
```

### 6.2 `src/presets/PatchState.cpp`

```cpp
#include "PatchState.h"

namespace coolsynth::presets
{
    std::unique_ptr<juce::XmlElement> createWrappedPatchXml(const juce::XmlElement& parameterStateXml,
                                                            juce::StringRef stateType)
    {
        auto patchXml = std::make_unique<juce::XmlElement>(patchRootTag);
        patchXml->setAttribute("formatVersion", patchFormatVersion);
        patchXml->setAttribute("product", "CoolSynth");
        patchXml->setAttribute("stateType", stateType);
        patchXml->addChildElement(parameterStateXml.createCopy());
        return patchXml;
    }

    PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                         juce::StringRef expectedStateType)
    {
        if (!patchXml.hasTagName(patchRootTag))
            return { PatchStateError::invalidRootTag, "Not a CoolSynth patch file.", nullptr };

        const int version = patchXml.getIntAttribute("formatVersion", 0);
        if (version != patchFormatVersion)
            return { PatchStateError::unsupportedFormatVersion, "Unsupported CoolSynth patch version.", nullptr };

        const juce::XmlElement* matchingChild = nullptr;

        for (auto* child : patchXml.getChildIterator())
        {
            if (!child->hasTagName(expectedStateType))
                continue;

            if (matchingChild != nullptr)
                return { PatchStateError::ambiguousParameterState, "Patch file contains multiple parameter-state payloads.", nullptr };

            matchingChild = child;
        }

        if (matchingChild == nullptr)
            return { PatchStateError::missingParameterState, "Patch file does not contain the expected parameter state.", nullptr };

        PatchLoadResult result;
        result.parameterStateXml.reset(matchingChild->createCopy());
        return result;
    }

    PatchIoResult writePatchFile(const juce::File& destination,
                                 const juce::XmlElement& patchXml)
    {
        juce::TemporaryFile tempFile(destination);

        if (auto stream = tempFile.getFile().createOutputStream())
        {
            patchXml.writeTo(*stream, {});
            stream->flush();

            if (tempFile.overwriteTargetFileWithTemporary())
                return {};
        }

        return { PatchStateError::fileWriteFailed, "Failed to write patch file." };
    }

    PatchLoadResult readPatchFile(const juce::File& source,
                                  juce::StringRef expectedStateType)
    {
        auto xml = juce::XmlDocument::parse(source);
        if (xml == nullptr)
            return { PatchStateError::invalidXmlDocument, "Failed to parse patch XML.", nullptr };

        return parseWrappedPatchXml(*xml, expectedStateType);
    }
}
```

### 6.3 `SynthAudioProcessor` Additions

```cpp
std::unique_ptr<juce::XmlElement> SynthAudioProcessor::createParameterStateXml() const
{
    auto state = parameters.copyState();
    return state.createXml();
}

bool SynthAudioProcessor::applyParameterStateXml(const juce::XmlElement& stateXml)
{
    if (!stateXml.hasTagName(parameters.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml(stateXml);
    if (!tree.isValid())
        return false;

    parameters.replaceState(tree);
    return true;
}

void SynthAudioProcessor::applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                                        float normalizedValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(normalizedValue);
    parameter.endChangeGesture();
}

void SynthAudioProcessor::resetAutomatableParametersToDefaults()
{
    for (auto* parameterBase : getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(parameterBase))
            applyNormalizedParameterValue(*parameter, parameter->getDefaultValue());
}
```

### 6.4 `SynthAudioProcessorEditor` Patch Action Wiring

```cpp
if (juce::JUCEApplicationBase::isStandaloneApp())
{
    patchActionsVisible = true;

    initPatchButton.onClick = [this] { triggerInitPatch(); };
    savePatchButton.onClick = [this] { triggerSavePatch(); };
    loadPatchButton.onClick = [this] { triggerLoadPatch(); };

    addAndMakeVisible(initPatchButton);
    addAndMakeVisible(savePatchButton);
    addAndMakeVisible(loadPatchButton);
}

void SynthAudioProcessorEditor::triggerInitPatch()
{
    processor.resetAutomatableParametersToDefaults();
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::triggerSavePatch()
{
    launchPatchSaveChooser();
}

void SynthAudioProcessorEditor::triggerLoadPatch()
{
    launchPatchLoadChooser();
}

void SynthAudioProcessorEditor::handlePatchSaveSelection(const juce::File& selectedFile)
{
    auto destination = selectedFile;
    if (!destination.hasFileExtension(coolsynth::presets::defaultPatchExtension))
        destination = destination.withFileExtension(coolsynth::presets::defaultPatchExtension);

    auto stateXml = processor.createParameterStateXml();
    if (stateXml == nullptr)
    {
        showPatchError("Failed to capture synth parameter state.");
        return;
    }

    auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                              processor.getParameterStateTypeName());
    auto result = coolsynth::presets::writePatchFile(destination, *patchXml);
    if (!result.succeeded())
        showPatchError(result.message);
}

void SynthAudioProcessorEditor::handlePatchLoadSelection(const juce::File& selectedFile)
{
    auto result = coolsynth::presets::readPatchFile(selectedFile,
                                                    processor.getParameterStateTypeName());
    if (!result.succeeded() || result.parameterStateXml == nullptr)
    {
        showPatchError(result.message);
        return;
    }

    if (!processor.applyParameterStateXml(*result.parameterStateXml))
    {
        showPatchError("Patch file contained incompatible parameter state.");
        return;
    }

    refreshValueDisplays();
}
```

### 6.5 `tests/PatchStateTests.cpp`

```cpp
#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/PatchState.h"

class PatchStateTests final : public juce::UnitTest
{
public:
    PatchStateTests() : juce::UnitTest("PatchState", "CoolSynth") {}

    void runTest() override
    {
        beginTest("init_patch_resets_all_automatable_parameters_to_defaults");
        {
            SynthAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            apvts.getParameter("delayMix")->setValueNotifyingHost(1.0f);
            apvts.getParameter("masterGainDb")->setValueNotifyingHost(1.0f);

            processor.resetAutomatableParametersToDefaults();

            expectWithinAbsoluteError(apvts.getParameter("delayMix")->getValue(),
                                      apvts.getParameter("delayMix")->getDefaultValue(),
                                      0.0001f);
        }

        beginTest("patch_round_trip_restores_parameter_values_in_fresh_processor");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            auto stateXml = source.createParameterStateXml();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      source.getParameterStateTypeName());
            auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml,
                                                                   target.getParameterStateTypeName());

            expect(parsed.succeeded());
            expect(parsed.parameterStateXml != nullptr);
            expect(target.applyParameterStateXml(*parsed.parameterStateXml));
        }
    }
};
```

## Review Trigger

Review this blueprint before any implementation starts.

The key design questions to resolve at review time are:

1. Is the standalone-only patch UI gate acceptable for Phase 14, with the patch-state engine kept shared and reusable?
2. Is the minimal `.cspatch` wrapper format sufficient, or does the reviewer want additional metadata despite the current scope guardrails?
3. Is the existing console test target an acceptable host for the new patch tests, or should Phase 14 also rename or split the test executable?

If those answers remain unchanged, implementation can proceed without reopening the architecture.
