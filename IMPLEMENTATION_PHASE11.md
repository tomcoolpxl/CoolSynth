<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 11 Blueprint: Host-Validated VST3 Smoke Path

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 11 - Host-validated VST3 smoke path`

Selection basis:

- `IMPLEMENTATION_PHASE1.md` through `IMPLEMENTATION_PHASE10.md` already exist, and no `IMPLEMENTATION_PHASE11.md` file exists yet.
- `TODO.md` says all current implementation phases in the plan are complete, so the next uncovered blueprint must come from the next unfinished phase in `IMPLEMENTATION_PLAN.md`.
- `IMPLEMENTATION_PLAN.md` defines `Phase 11` as the first host-facing validation milestone: load the VST3 in `Ableton Live Lite`, confirm host MIDI notes, confirm automation, and confirm state restore.
- The current codebase already contains the shared JUCE plugin target, the APVTS parameter model, VST3 artefact generation, plugin-mode editor gating, and APVTS state serialization. That makes a host-validation phase the next correct review-sized slice rather than more standalone work.

## Working Hypothesis

The current shared processor path is already close to host-ready. The most likely failure point for this phase is not missing synth architecture; it is repeatable host discovery and smoke-test workflow on Windows.

Cheap disconfirming check before changing processor or editor code:

1. Build the Release VST3 target.
2. Confirm the built bundle contains `Contents/Resources/moduleinfo.json`.
3. Place the built `CoolSynth.vst3` bundle in a host-scanned Windows VST3 folder.
4. Scan in `Ableton Live Lite`.

If the plugin still does not appear, load, or reopen correctly, only then harden `CMakeLists.txt` or `SynthAudioProcessor`.

## Scope Guardrails

In scope for this phase:

- Validate the shared VST3 target in one real Windows host environment.
- Freeze and verify plugin metadata, instrument classification, and supported bus layouts.
- Verify that host-routed MIDI note input reaches the existing shared synth engine.
- Verify that host automation reaches the same APVTS-backed parameters already used by the UI.
- Verify that plugin state save and restore uses only the shared synth parameter state path.
- Document a repeatable local smoke-test workflow, including build, deployment, rescan, insertion, automation, and reopen steps.
- Add only the smallest supporting build or processor hardening required to make the smoke path repeatable.

Explicitly out of scope for this phase:

- New synth parameters, new DSP, new modulation, or any sound redesign.
- Host-provided CC remapping or plugin-side MIDI learn.
- Standalone persistence, preset files, or any standalone-only settings changes.
- Refactoring working standalone UI surfaces.
- New controller abstractions, background services, or plugin-specific shadow state.
- Changing stable parameter IDs, changing `versionHint`, or changing plugin manufacturer or product codes unless a host-loading defect proves they are wrong.

Non-negotiable constraints:

- The VST3 smoke path must continue to use the same shared `SynthAudioProcessor`, APVTS parameter set, and `SynthAudioProcessorEditor` already used by standalone mode.
- Standalone-only device selectors, status surfaces, settings dialogs, and MIDI-monitor UI must remain absent in plugin mode.
- Host automation must land in the canonical APVTS parameter model. Do not add controller-only or host-only mirror values.
- Shared plugin state must serialize only the synth parameters from Section 6 of `REQUIREMENTS.md`. Do not serialize standalone device choices or monitor state.

## Current Code Anchors

The blueprint should stay anchored to the code that already owns the VST3 contract:

- `CMakeLists.txt` already defines one JUCE plugin target with `FORMATS Standalone VST3`, `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT FALSE`, `IS_MIDI_EFFECT FALSE`, `VST3_CATEGORIES Instrument Synth`, and `COPY_PLUGIN_AFTER_BUILD FALSE`.
- `src/plugin/SynthAudioProcessor.h` and `src/plugin/SynthAudioProcessor.cpp` already own the plugin bus layout, `acceptsMidi()`, `producesMidi()`, `isMidiEffect()`, `createEditor()`, `processBlock()`, and APVTS state serialization.
- `src/plugin/SynthAudioProcessorEditor.h` and `src/plugin/SynthAudioProcessorEditor.cpp` already gate standalone-only runtime UI behind `juce::JUCEApplicationBase::isStandaloneApp()`.
- `src/parameters/ParameterIDs.h` already defines the stable parameter IDs and `versionHint = 1`.
- `src/parameters/ParameterLayout.cpp` already defines all first-release automatable parameters and their formatting.
- The built VST3 artefact already has the expected JUCE bundle structure under `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3`, including `Contents/Resources/moduleinfo.json`.

## Exact `TODO.md` Entries This Blueprint Expands

These are the `Phase 11` checklist items from `IMPLEMENTATION_PLAN.md` that this blueprint expands into concrete execution work:

- [ ] Validate VST3 metadata and bus layout in `Ableton Live Lite` on Windows.
- [ ] Verify host MIDI note input produces sound.
- [ ] Verify the plugin editor opens correctly in the host.
- [ ] Verify host automation works for cutoff and master gain.
- [ ] Verify plugin state saves and restores through the APVTS path.
- [ ] Document the local `Ableton Live Lite` VST3 smoke-test workflow.

## 1. Architectural Design

### 1.1 Controlling Design Decision

This phase is a host-contract validation phase, not a new architecture phase.

Required consequence:

- Prefer documentation and the smallest build or processor hardening over any new runtime abstraction.
- Treat the current shared plugin path as the primary implementation surface.
- Make no changes to synth behavior unless a host smoke test exposes a concrete defect.

### 1.2 Host Contract Invariants

The implementation for this phase must preserve these invariants.

#### 1.2.1 Plugin Target Invariants

The JUCE plugin target must remain an instrument with these effective properties:

```text
PRODUCT_NAME: CoolSynth
PLUGIN_NAME: CoolSynth
PLUGIN_MANUFACTURER_CODE: Tcpx
PLUGIN_CODE: Csyn
FORMATS: Standalone VST3
IS_SYNTH: true
NEEDS_MIDI_INPUT: true
NEEDS_MIDI_OUTPUT: false
IS_MIDI_EFFECT: false
VST3_CATEGORIES: Instrument Synth
```

These values are now part of the host-facing contract. After this phase begins, they are frozen unless a host-loading failure proves one is incorrect.

#### 1.2.2 Bus Layout Invariants

The processor must continue to behave as an instrument with no audio input buses.

Supported layouts:

```text
input buses: none
main output bus: mono or stereo only
```

Host-smoke implication:

- If `Ableton Live Lite` rejects the plugin as insertable on a MIDI track, inspect this contract before touching DSP or editor code.

#### 1.2.3 State Serialization Invariants

The shared synth state for VST3 save and restore is exactly the APVTS parameter state. No more and no less.

State root:

```text
APVTS state tree type: CoolSynthState
```

Serialized parameter set:

```text
waveform
ampAttackMs
ampDecayMs
ampSustain
ampReleaseMs
filterCutoffHz
filterResonance
delayTimeMs
delayFeedback
delayMix
masterGainDb
```

State rules:

- Restoring plugin state must restore every parameter above.
- Standalone-only settings such as audio device choice, MIDI device choice, monitor visibility, or status text must never enter plugin state.
- `setStateInformation()` must fail safely when given malformed binary data or an XML root that does not match `CoolSynthState`.

#### 1.2.4 Automation Invariants

This phase needs only one automation smoke set:

```text
filterCutoffHz
masterGainDb
```

Automation rules:

- Host automation must drive the same APVTS parameters used by the existing UI attachments.
- The plugin editor must reflect host automation without any standalone MIDI path being active.
- No new automation-only state or polling layer is allowed.

#### 1.2.5 Editor Separation Invariants

The plugin editor must remain a synth-only editor.

Allowed in plugin mode:

- Oscillator, filter, envelope, delay, output, and global action controls.

Forbidden in plugin mode:

- Standalone settings dialog.
- Standalone status bar.
- Audio-device selectors.
- Hardware MIDI selectors.
- MIDI monitor surfaces.

### 1.3 Required Data Structures and State Definitions

Default path for this phase: no new runtime data structures are required if the current host-smoke checks pass.

If host validation reveals a defect and a small hardening refactor becomes necessary, limit the new code to contract-local helpers only.

Recommended helper state definitions if hardening is needed:

```cpp
namespace coolsynth::plugin::contract
{
    inline constexpr char stateTreeType[] = "CoolSynthState";

    inline constexpr std::array<const char*, 11> serializedParameterIds {
        "waveform",
        "ampAttackMs",
        "ampDecayMs",
        "ampSustain",
        "ampReleaseMs",
        "filterCutoffHz",
        "filterResonance",
        "delayTimeMs",
        "delayFeedback",
        "delayMix",
        "masterGainDb",
    };
}
```

Purpose of this optional helper:

- Keep the serialization contract explicit in one place if state-restore hardening is needed.
- Avoid silent drift between the APVTS constructor tag, restore validation, and smoke-test documentation.

### 1.4 Required Function Signatures

Existing host-critical functions that own this phase:

```cpp
bool SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const override;
void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
juce::AudioProcessorEditor* SynthAudioProcessor::createEditor() override;
void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData) override;
void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes) override;
```

If host-smoke failures force a local refactor, limit the new signatures to these helpers inside `SynthAudioProcessor`:

```cpp
private:
    static juce::AudioProcessor::BusesProperties createBusProperties();
    static bool isSupportedMainOutputLayout(const juce::AudioChannelSet& layout) noexcept;
    juce::ValueTree copySerializableState() const;
    bool restoreSerializableState(const juce::ValueTree& incomingState);
```

Why these helpers are acceptable:

- They isolate bus-layout and state-restore policy without creating new cross-module abstractions.
- They keep the smoke-phase changes inside the owning class instead of pushing host policy into unrelated files.

### 1.5 Build and Deployment Contract for Windows Host Smoke

The host must see the built bundle from a scanned VST3 directory.

Relevant Windows VST3 locations:

```text
Per-user VST3 folder: %LOCALAPPDATA%/Programs/Common/VST3
Global VST3 folder:   %ProgramFiles%/Common Files/VST3
```

Execution choice for this phase:

- Prefer the per-user VST3 folder for local smoke testing to avoid elevated permissions.
- Do not rely on the raw build artefact path alone, because hosts scan standard VST3 locations, not arbitrary build folders.
- Avoid duplicate deployed copies of `CoolSynth.vst3` in multiple scanned locations. Hosts may pick the higher-priority copy for the same processor UID and make reopen debugging ambiguous.

## 2. File-Level Strategy

### 2.1 Required Edit Set

These files should be the default implementation touch list for `Phase 11`.

| File | Responsibility in this phase |
| --- | --- |
| `CMakeLists.txt` | Add an opt-in, developer-safe VST3 deployment path for repeatable host smoke testing; preserve and explicitly review plugin metadata; do not change IDs casually. |
| `src/plugin/SynthAudioProcessor.h` | Declare only the smallest host-contract helper functions if bus-layout or state-restore hardening is required. |
| `src/plugin/SynthAudioProcessor.cpp` | Keep the state path APVTS-only, harden malformed-state handling if needed, and centralize bus-layout policy if host validation exposes ambiguity. |
| `docs/vst3-smoke-test.md` | Document the exact local workflow for building, deploying, rescanning, inserting, automating, and reopening the plugin in `Ableton Live Lite`. |
| `TODO.md` | Refresh with the active `Phase 11` checklist before implementation begins. |
| `DONE.md` | Record completion only after all host checks, documentation, and review steps are complete. |

### 2.2 Conditional Edit Set

These files should be edited only if host validation exposes a concrete defect.

| File | Touch only if... |
| --- | --- |
| `src/plugin/SynthAudioProcessorEditor.h` | The host exposes an editor-lifecycle, size, or plugin-mode composition bug that cannot be fixed in the `.cpp` alone. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | The editor opens incorrectly in host, or standalone-only surfaces leak into plugin mode despite the current guard. |
| `README.md` | A short pointer to `docs/vst3-smoke-test.md` is needed for discoverability after the detailed smoke document is added. |

### 2.3 Audit-Only Files That Should Not Change Without Proof

These files must be reviewed during the phase, but they should not be edited unless a host defect proves they are wrong.

| File | Reason to keep frozen |
| --- | --- |
| `src/parameters/ParameterIDs.h` | Parameter IDs and `versionHint` are part of the host-facing compatibility contract. |
| `src/parameters/ParameterLayout.cpp` | The first-release parameter set is already stable and must not drift during smoke validation. |

## 3. Atomic Execution Steps

Each step below expands one high-level `TODO.md` checkbox into a strict `Plan -> Act -> Validate` loop.

### 3.1 Validate VST3 Metadata and Bus Layout in `Ableton Live Lite`

#### Plan

- Treat host discovery as the first discriminating check.
- Make the Release bundle reachable from a scanned VST3 folder before changing processor code.
- Freeze the existing metadata and bus contract before opening the host.

#### Act

1. Review `CMakeLists.txt` and confirm these settings remain intact: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT FALSE`, `IS_MIDI_EFFECT FALSE`, `VST3_CATEGORIES Instrument Synth`, `PLUGIN_MANUFACTURER_CODE Tcpx`, and `PLUGIN_CODE Csyn`.
2. Add one opt-in CMake path for local host deployment if the team wants repeatable smoke testing without manual copy. Use the per-user VST3 folder, not the protected global folder.
3. Build the Release plugin target.
4. Confirm the built bundle still contains `Contents/Resources/moduleinfo.json`.
5. Deploy the Release `CoolSynth.vst3` bundle into `%LOCALAPPDATA%/Programs/Common/VST3` either via the new CMake option or via an explicit manual copy documented in the smoke-test guide.
6. Rescan plugins in `Ableton Live Lite` and check whether `CoolSynth` appears under VST3 instruments.
7. If the host rejects the plugin or lists it in a failed-scan path, inspect only metadata and bus-layout code first.

#### Validate

- The plugin appears as an instrument, not an audio effect.
- The plugin can be inserted on a MIDI track.
- No host error indicates unsupported bus layout or invalid plugin classification.
- If the host still does not show the plugin, resolve deployment path ambiguity before editing processor code.

### 3.2 Verify Host MIDI Note Input Produces Sound

#### Plan

- Use the existing shared `processBlock()` and synth-engine path.
- Confirm the plugin makes sound from host MIDI without any standalone controller infrastructure.

#### Act

1. Insert `CoolSynth` on a fresh MIDI track in `Ableton Live Lite`.
2. Arm or monitor-enable the track.
3. Feed MIDI notes from a host keyboard, computer keyboard, or a clip containing note events.
4. Leave all standalone hardware settings untouched; the plugin must not depend on them.
5. If playback is silent, inspect the host routing first, then inspect only the shared plugin note path in `processBlock()` and `SynthEngine::render()`.

#### Validate

- A default patch produces audible output from host-routed MIDI notes.
- Audio meters move in the host.
- Playback works without any standalone MIDI device selection.
- No plugin-only workaround is introduced for note routing.

### 3.3 Verify the Plugin Editor Opens Correctly in the Host

#### Plan

- Reuse the current `createEditor()` path.
- Prove that plugin mode remains synth-only.

#### Act

1. Open the plugin editor from inside `Ableton Live Lite`.
2. Close and reopen the editor several times.
3. Verify the visible control surface contains only the synth sections and the panic action.
4. Inspect `SynthAudioProcessorEditor.cpp` only if the host reveals incorrect size, bad lifecycle behavior, or standalone-only UI leakage.

#### Validate

- The editor opens without crashing.
- The editor can be reopened cleanly.
- No standalone settings dialog button, status bar, device selector, or MIDI monitor appears in plugin mode.
- Control labels and value text remain readable in the host window.

### 3.4 Verify Host Automation Works for Cutoff and Master Gain

#### Plan

- Use `filterCutoffHz` and `masterGainDb` as the minimum automation smoke set.
- Rely on the existing APVTS attachments and parameter model; do not add a host-automation adapter layer.

#### Act

1. Create automation lanes in `Ableton Live Lite` for filter cutoff and master gain.
2. Hold or sequence notes so the automation effect is audible.
3. Draw or record obvious automation ramps for both parameters.
4. Observe the plugin editor while the host plays back automation.
5. If the UI does not reflect automation, inspect the APVTS attachment path before changing any parameter definitions.

#### Validate

- Cutoff automation changes the sound audibly and visibly.
- Master-gain automation changes the output level audibly and visibly.
- The editor updates while automation is playing.
- No standalone MIDI mapping path is required for automation to work.

### 3.5 Verify Plugin State Saves and Restores Through the APVTS Path

#### Plan

- Treat save and restore as an APVTS contract check, not a preset-system phase.
- Harden `setStateInformation()` only if malformed or incomplete restore behavior is observed.

#### Act

1. Change multiple parameters away from defaults, including `waveform`, `filterCutoffHz`, `filterResonance`, `delayTimeMs`, `delayFeedback`, `delayMix`, and `masterGainDb`.
2. Save the `Ableton Live Lite` project.
3. Close the project and reopen it.
4. Compare the reopened plugin state against the saved state.
5. If restore fails or restores partially, isolate the defect to `getStateInformation()` and `setStateInformation()` before touching any other subsystem.
6. If hardening is needed, keep it local: validate the root tag, reject malformed XML, and replace APVTS state atomically.

#### Validate

- All Section 6 parameters restore correctly.
- Reopened automation still targets the same parameter IDs.
- No standalone-only settings are serialized into plugin state.
- Malformed or foreign state data is ignored safely rather than crashing or corrupting the processor.

### 3.6 Document the Local `Ableton Live Lite` VST3 Smoke-Test Workflow

#### Plan

- Write one dedicated smoke-test document instead of bloating `README.md`.
- Document the exact deployment and scan workflow chosen during implementation.

#### Act

1. Create `docs/vst3-smoke-test.md`.
2. Record the exact Release build command.
3. Record the exact deployed VST3 path used for smoke testing.
4. Record the exact `Ableton Live Lite` actions needed to rescan, insert, play notes, automate cutoff and master gain, and save-reopen the set.
5. Add a short troubleshooting section for duplicate plugin copies, host rescans, and missing plugin visibility.
6. Add a brief pointer from `README.md` only if reviewers think the dedicated doc is too easy to miss.

#### Validate

- Another reviewer can repeat the smoke test from the written steps.
- The document matches the final build-system choice: documented manual copy or documented CMake-assisted deploy, not an unresolved mix of both.
- The document names the exact acceptance checks for this milestone.

## 4. Edge Case and Boundary Audit

The implementation and review for this phase must actively check these failure modes.

### 4.1 Host Discovery and Deployment Risks

- The built VST3 bundle exists in `build/CoolSynth_artefacts/...`, but the host never scans that path.
- `COPY_PLUGIN_AFTER_BUILD TRUE` without a user-safe `VST3_COPY_DIR` can target a protected system folder and fail silently or require elevation.
- Multiple deployed copies of `CoolSynth.vst3` in scanned directories can make the host load a stale build because VST3 hosts choose the first matching processor UID from higher-priority locations.
- The host may cache failed scans until a manual rescan is triggered.

### 4.2 Metadata and Bus-Layout Risks

- A wrong synth or effect classification can prevent insertion on a MIDI track.
- A bus-layout contract that accidentally permits input buses can confuse host routing.
- A bus-layout contract that rejects mono or stereo incorrectly can block loading in some host contexts.

### 4.3 Parameter Compatibility Risks

- Changing `ParameterIDs.h` or `versionHint` during this phase can break automation lanes and saved host projects.
- Renaming parameter display names is lower risk than changing IDs, but it can still make smoke results harder to compare; do not change names casually during this phase.

### 4.4 State-Restore Risks

- `setStateInformation()` may receive malformed data, foreign data, or older data.
- Partial restore behavior can be masked if only one parameter is spot-checked.
- Restoring standalone-only state into the plugin would violate the separation requirements and create inconsistent host reopen behavior.

### 4.5 Editor-Separation Risks

- A plugin-mode editor could accidentally instantiate standalone-only runtime state if the standalone guard is weakened.
- A host-specific sizing defect could tempt a broad editor rewrite. Do not widen scope. Fix only the host-visible defect.

### 4.6 Non-Goal Traps

- Host-provided CC remapping is explicitly deferred. Do not add plugin CC mapping just because the host can send CCs.
- Preset file format work belongs to the later patch workflow phase, not this phase.
- Standalone persistence work belongs to the next phase, not this one.

## 5. Verification Protocol

There is no dedicated automated unit-test suite in scope for this project yet. For this phase, the executable verification stack is build validation, artefact inspection, and manual host smoke testing.

### 5.1 Automated Build and Artefact Checks

Run these checks in order:

1. `cmake --build build --config Debug --target CoolSynth_VST3`
2. `cmake --build build --config Release --target CoolSynth_VST3`
3. Confirm the Release artefact exists at `build/CoolSynth_artefacts/Release/VST3/CoolSynth.vst3`.
4. Confirm the built bundle contains `Contents/Resources/moduleinfo.json`.
5. If CMake-assisted deployment is enabled, confirm the deployed copy timestamp updates in `%LOCALAPPDATA%/Programs/Common/VST3`.
6. Review the build output for new warnings introduced by the phase.

### 5.2 Manual Host Smoke Checklist

All checks below must pass before the phase can move to `DONE.md`.

- [ ] `Ableton Live Lite` discovers `CoolSynth` as a VST3 instrument.
- [ ] The plugin inserts on a MIDI track without a host error.
- [ ] The editor opens and reopens without crashing.
- [ ] The plugin editor contains no standalone-only settings, device, status, or monitor UI.
- [ ] Host-routed MIDI notes produce audible sound.
- [ ] Filter cutoff automation is audible and visible in the editor.
- [ ] Master-gain automation is audible and visible in the editor.
- [ ] Saving and reopening the host project restores all parameter values.
- [ ] Reopened automation still targets the correct parameters.
- [ ] No duplicate stale plugin copy is being loaded during the smoke test.

### 5.3 Failure Triage Order

If a manual check fails, use this triage order and do not widen scope prematurely.

1. Deployment path and duplicate-copy check.
2. Host plugin rescan and browser visibility check.
3. Plugin metadata and bus-layout check.
4. Editor mode-gating check.
5. APVTS automation and state-restore check.
6. Only after the above: local processor-code hardening.

## 6. Code Scaffolding

This phase should add only minimal scaffolding. Use the templates below if new code becomes necessary.

### 6.1 CMake Scaffolding for User-Safe VST3 Deployment

Use this pattern if the team chooses an opt-in post-build deploy step for repeatable host smoke testing.

```cmake
option(COOLSYNTH_ENABLE_VST3_USER_INSTALL
    "Copy CoolSynth.vst3 to the per-user Windows VST3 folder after build"
    OFF)

if (WIN32)
    file(TO_CMAKE_PATH
        "$ENV{LOCALAPPDATA}/Programs/Common/VST3"
        COOLSYNTH_VST3_USER_DIR)
endif()

juce_add_plugin(CoolSynth
    COMPANY_NAME "tomcoolpxl"
    PRODUCT_NAME "CoolSynth"
    PLUGIN_NAME "CoolSynth"
    PLUGIN_MANUFACTURER_CODE Tcpx
    PLUGIN_CODE Csyn
    FORMATS Standalone VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${COOLSYNTH_ENABLE_VST3_USER_INSTALL}
    VST3_COPY_DIR "${COOLSYNTH_VST3_USER_DIR}")
```

Rules for this scaffold:

- Keep the option default `OFF` so normal builds do not unexpectedly deploy into a scanned folder.
- Use the per-user VST3 folder, not the global system folder.
- Do not add a second custom copy mechanism if JUCE's built-in copy step is sufficient.

### 6.2 Processor Helper Scaffolding for State Hardening

Use this pattern only if state-restore defects appear in host validation.

```cpp
juce::ValueTree SynthAudioProcessor::copySerializableState() const
{
    return parameters.copyState();
}

bool SynthAudioProcessor::restoreSerializableState(const juce::ValueTree& incomingState)
{
    if (!incomingState.isValid())
        return false;

    if (incomingState.getType() != parameters.state.getType())
        return false;

    parameters.replaceState(incomingState);
    return true;
}
```

Rules for this scaffold:

- Keep the state path APVTS-only.
- Reject invalid or foreign state early.
- Do not add standalone-only state branches.

### 6.3 Documentation Scaffolding for `docs/vst3-smoke-test.md`

Use this structure for the smoke-test document.

````md
# CoolSynth VST3 Smoke Test

## Preconditions

- Windows 11
- Ableton Live Lite installed
- Release VST3 built successfully

## Build

```powershell
cmake --build build --config Release --target CoolSynth_VST3
```

## Deploy

- Copy or deploy `build/CoolSynth_artefacts/Release/VST3/CoolSynth.vst3`
- Destination: `%LOCALAPPDATA%/Programs/Common/VST3`

## Ableton Live Lite Steps

1. Rescan plug-ins.
2. Insert `CoolSynth` on a MIDI track.
3. Play host MIDI notes.
4. Open the editor.
5. Automate cutoff and master gain.
6. Save and reopen the project.

## Expected Results

- Plugin loads as an instrument.
- Notes produce sound.
- Automation moves the editor and sound.
- Reopen restores parameter state.

## Troubleshooting

- Duplicate deployed copies
- Missing rescan
- Wrong VST3 folder
- Stale build vs deployed build mismatch
````

## Exit Condition for Phase Review

The phase is ready for implementation review only when the planned work remains narrowly bounded to:

- one repeatable deployment path,
- one shared processor contract,
- one shared editor path,
- one APVTS-only state path,
- one documented `Ableton Live Lite` smoke workflow.

If the proposed implementation starts adding host-specific features, new synth behavior, or parameter-ID churn, the phase has already drifted beyond scope.
