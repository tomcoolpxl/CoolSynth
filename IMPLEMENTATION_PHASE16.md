<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 16 Blueprint: Final Stabilization and Release Review

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 16 - Final stabilization and release review`

Selection basis:

- `TODO.md` already contains the six `Phase 16` checklist items from `IMPLEMENTATION_PLAN.md`.
- `IMPLEMENTATION_PLAN.md` explicitly limits this phase to validation, release-blocking fixes, and documentation alignment.
- The repository already contains the expected stabilization surfaces:
  - shared runtime code under `src/**`
  - standalone/device/persistence code under `src/standalone/**`
  - controller, MIDI-learn, and monitor code under `src/midi/**`
  - patch/state code under `src/presets/**`
  - manual/tag-only CI surfaces under `.github/workflows/**` and `scripts/ci/**`
- Current repository evidence shows non-trivial reconciliation work before the project can be declared fully shipped:
  - `DONE.md` does not currently list plan phases `6`, `8`, or `11`, while the codebase and docs already contain those surfaces.
  - `docs/minilab3-default-messages.md` still says `Capture surface: Simulated/Researched Arturia Mode CCs`, which does not satisfy the current requirement that the actual MiniLab 3 default messages on the developer device be captured.
  - The MiniLab mapping described in `REQUIREMENTS.md`, `docs/minilab3-default-messages.md`, and `src/midi/Minilab3Profile.cpp` is not aligned.
  - `docs/vst3-smoke-test.md` exists and should be treated as the host-validation source of truth, but it must still be reconciled against the final release-candidate behavior and any updated workflow.

## Working Hypothesis

Phase 16 is not a feature phase.

The dominant risk is evidence drift:

- docs claiming behavior that is not what the code currently ships
- code shipping behavior that was never verified against the final requirements
- earlier milestone outcomes being effectively complete in code but not reflected cleanly in `DONE.md`
- controller and host validation evidence not being strong enough to justify a final release sign-off

The smallest correct `Phase 16` execution is:

1. Prove the standalone app, VST3 path, and CI workflows at the release-candidate commit.
2. Treat every failing required check as either:
   - a release-blocking bug to fix immediately, or
   - an accepted deferral to document explicitly.
3. Reconcile every authoritative document to the proven shipped behavior.
4. Refuse any opportunistic improvement that is not directly traceable to a failing validation step or an authoritative-doc mismatch.

## Current Code Anchors

These are the real implementation surfaces that control the behavior `Phase 16` is allowed to validate and, if necessary, correct.

- `CMakeLists.txt`
  - Defines the shared JUCE plugin target `CoolSynth` with `FORMATS Standalone VST3`.
  - Defines the current automated test target `CoolSynthMidiLearnTests`.
  - Is the build-system source of truth for which shared sources and tests exist.

- `CMakePresets.json`
  - Defines local presets (`vs2022-debug`, `vs2022-release`) and CI-isolated presets (`ci-debug`, `ci-release`).
  - Fixes the release-candidate CI build roots to `build/ci-debug` and `build/ci-release`.

- `.github/workflows/windows-manual-validation.yml`
  - Defines the manual-only validation workflow.
  - Calls `scripts/ci/BuildAndTest.ps1` and optionally `scripts/ci/PackageRelease.ps1`.
  - Uploads diagnostics and optional packages.

- `.github/workflows/windows-release.yml`
  - Defines the tag-only release workflow.
  - Builds, tests, packages, and publishes release assets with generated notes.

- `scripts/ci/Common.ps1`
  - Owns CI bootstrap, configure, build, test, packaging, checksum, and manifest helpers.
  - Is the root owner for any release-candidate packaging or workflow defect.

- `src/plugin/SynthAudioProcessor.*`
  - Owns shared parameter state, controller-event application, panic, plugin state serialization, and render-parameter assembly.

- `src/plugin/SynthAudioProcessorEditor.*`
  - Owns the main synth UI, patch actions, standalone-only learn affordances, and the plugin/standalone editor split.

- `src/midi/Minilab3Profile.*`
  - Owns the embedded MiniLab 3 control definitions and fixed-binding table.

- `src/midi/MidiMappingEngine.*`
  - Owns fixed-binding translation, learned-binding shadowing, waveform translation, and takeover behavior.

- `src/midi/MidiMonitor.*` and `src/ui/MidiMonitorPanel.cpp`
  - Own the current monitor event model and display path.
  - Currently only record note-on, note-off, and CC events.

- `src/standalone/StandaloneAudioSupport.*`, `src/standalone/StandaloneMidiInput.*`, `src/standalone/SettingsStore.*`
  - Own standalone device selection, status snapshots, learned-binding persistence, and missing-device recovery.

- `src/presets/PatchState.*`
  - Owns patch wrapping, parsing, and state-boundary enforcement.

- `docs/minilab3-default-messages.md`
  - Is supposed to hold the controller-capture evidence.
  - Currently contains wording that suggests research/simulation rather than actual-device capture.

- `docs/vst3-smoke-test.md`
  - Is the current host-validation runbook.

## Execution Guardrails

- No new features.
- No broad refactors.
- No directory-structure changes.
- No edits under `external/JUCE/**`.
- Every code edit must name:
  - the failing validation scenario
  - the owning file
  - the smallest possible repair
  - the post-fix validation step
- Every documentation edit must name:
  - the specific stale claim being corrected
  - the evidence source that justifies the correction
- If the actual MiniLab 3 device capture cannot be performed, the phase cannot honestly claim final controller verification; that gap must remain explicitly deferred.
- If the VST3 smoke test cannot be re-run at the release-candidate state, the phase cannot honestly claim fresh host validation; that gap must remain explicit.
- `Phase 16` may touch multiple areas, but only for release-blocking defects or documentation alignment.

## Blocking Clarifications Before Execution

These are not blockers to writing this blueprint, but they must be resolved before `Phase 16` closes.

1. `MiniLab 3` verification source.

   - `REQUIREMENTS.md` requires actual device capture.
   - `docs/minilab3-default-messages.md` currently says the capture is simulated/researched.
   - `src/midi/Minilab3Profile.cpp` already encodes one concrete mapping table.
   - `Phase 16` must decide whether:
     - the code table is the shipped truth and the requirements/docs need correction, or
     - the requirements are the truth and the code/docs need correction plus fresh device capture.

2. Historical phase ledger.

   - `DONE.md` currently skips plan phases `6`, `8`, and `11`.
   - `Phase 16` must decide whether those were intentionally collapsed into later phases or whether `DONE.md` is stale.
   - The final release review summary cannot leave that ambiguous.

3. Final release review summary location.

   - Default recommendation for implementation: place the authoritative summary in `DONE.md` under the `Phase 16` section, with longer evidence details left in `docs/**`.
   - If a dedicated release-review document is preferred instead, confirm that before implementation begins.

## Exact `TODO.md` Entries This Blueprint Expands

- [ ] Run the full manual validation matrix and capture the results.
- [ ] Fix any release-blocking defects found during stabilization.
- [ ] Reconcile `README.md`, `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` with shipped behavior.
- [ ] Confirm every unfinished item is either in `TODO.md` or explicitly deferred.
- [ ] Verify the Windows CI workflow is green at the release candidate state.
- [ ] Prepare the final release review summary for the local standalone and VST3 artifacts.

## 1. Architectural Design

### 1.1 Phase-Level Working State

`Phase 16` should not add new product architecture unless a validation failure forces it.

The default working state for this phase is review-state, not runtime-state. Track it explicitly during execution using a tight evidence matrix with the following logical shapes:

```cpp
enum class ValidationSurface : uint8_t
{
    standalone,
    vst3Host,
    ciManualWorkflow,
    ciReleaseWorkflow,
    documentation
};

enum class ValidationStatus : uint8_t
{
    notRun,
    passed,
    failed,
    deferredAccepted
};

enum class ReleaseBlockerKind : uint8_t
{
    runtimeBug,
    stateBoundaryBug,
    controllerMappingBug,
    packagingBug,
    workflowBug,
    documentationMismatch,
    missingEvidence
};

struct ValidationScenarioResult
{
    std::string scenarioId;      // e.g. "req15-standalone-no-midi"
    ValidationSurface surface;
    std::string buildConfig;     // "Debug", "Release", or "n/a"
    ValidationStatus status;
    std::string evidenceSource;  // log path, host project note, screenshot note, or doc path
    std::string notes;
};

struct ReleaseBlocker
{
    std::string blockerId;
    ReleaseBlockerKind kind;
    std::string sourceScenarioId;
    std::string owningFile;
    std::string rootCause;
    std::string fixPlan;
    bool requiresCodeChange;
};

struct DocAlignmentItem
{
    std::string documentPath;
    std::string staleClaim;
    std::string shippedTruth;
    std::string evidenceSource;
};
```

Recommended implementation rule:

- keep this state in the `Phase 16` working notes or the draft `DONE.md` section while executing the phase
- do not add code for these models unless a helper becomes unavoidable

### 1.2 Runtime Owners That May Be Modified

If `Phase 16` exposes product bugs, fixes must stay inside the existing owner boundaries below.

| Concern | Primary owner | State already present | High-risk APIs |
| --- | --- | --- | --- |
| Shared plugin and standalone parameter state | `src/plugin/SynthAudioProcessor.*` | `APVTS parameters`, `ParameterValuePointers`, `panicRequested` | `processBlock`, `getStateInformation`, `setStateInformation`, `createParameterStateXml`, `applyParameterStateXml`, `handleStandaloneControllerEvent` |
| Standalone shell and options entry point | `src/standalone/CoolSynthStandaloneApp.cpp` | `ApplicationProperties`, `StandaloneSettingsStore`, custom `Options` button wiring | `initialise`, `shutdown`, `createPluginHolder` |
| Standalone audio snapshot and persisted audio selection | `src/standalone/StandaloneAudioSupport.*` | `AudioDeviceSnapshot`, `BackendSelectionResult`, `PersistedAudioSelection` | `captureCurrentAudioDeviceSnapshot`, `maybeApplyPreferredAudioBackend` |
| Standalone MIDI device state and last-event summary | `src/standalone/StandaloneMidiInput.*` | `MidiInputSnapshot`, `LastMidiEventSnapshot`, `AtomicLastMidiEventState`, `pendingControllerEvents` | `refreshDevices`, `selectDeviceByIdentifier`, `clearSelection`, `handleIncomingMidiMessage`, `drainControllerEvents` |
| Learned binding persistence | `src/standalone/SettingsStore.*` | persisted MIDI device selection, learned mappings, CC-label preference | `loadLearnedMidiMappings`, `saveLearnedMidiMappings`, `clearLearnedMidiMappings` |
| Fixed mapping and learned-binding takeover | `src/midi/MidiMappingEngine.*` | `BindingWithTarget`, `LearnedBindingWithTarget`, `TakeoverState`, `activeBindings`, `learnedBindings` | `translate`, `setLearnedBindings`, `clearLearnedBinding` |
| Learn session state | `src/midi/MidiLearn.*` | `MidiLearnSession`, `LearnCaptureOutcome`, `LearnedCcBinding` | `beginLearning`, `handleIncomingEvent`, `replaceBindings`, `clearBinding` |
| Controller profile truth table | `src/midi/Minilab3Profile.*` | `Minilab3ControlDefinition`, `VerifiedMidiSignature`, `Minilab3Binding` | `getVerifiedMinilab3Profile`, `getPhase7Bindings`, `validateMinilab3Profile` |
| MIDI monitor event capture | `src/midi/MidiMonitor.*`, `src/ui/MidiMonitorPanel.cpp` | `MidiMonitorEvent`, `MidiMonitorBuffer`, `MidiMonitorMessageType` | `pushMessage`, `drainPending`, `formatMonitorMessageType`, UI render columns |
| Synth engine and render path | `src/synth/SynthEngine.*`, `src/synth/SynthVoice.*`, `src/synth/GlobalDelay.*` | `BlockRenderParameters`, smoothing, delay, voice allocation | `prepare`, `render`, `panic`, `renderNextBlock`, `startNote`, `stopNote` |
| Patch format and state boundary | `src/presets/PatchState.*` | `PatchStateError`, `PatchLoadResult`, wrapped APVTS XML contract | `createWrappedPatchXml`, `parseWrappedPatchXml`, `writePatchFile`, `readPatchFile` |
| Editor surface split | `src/plugin/SynthAudioProcessorEditor.*` | standalone-only patch buttons, standalone-only learn state, shared synth controls | `mouseUp`, `showMidiLearnMenu`, `refreshMidiLearnVisuals`, patch-trigger methods |

### 1.3 Existing Function Signatures That Bound Phase 16 Work

These are the existing entry points `Phase 16` should use as its repair boundaries instead of inventing new cross-cutting layers.

```cpp
// src/plugin/SynthAudioProcessor.h
void prepareToPlay(double sampleRate, int samplesPerBlock) override;
void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
void getStateInformation(juce::MemoryBlock& destData) override;
void setStateInformation(const void* data, int sizeInBytes) override;
std::unique_ptr<juce::XmlElement> createParameterStateXml();
bool applyParameterStateXml(const juce::XmlElement& stateXml);
void setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
void clearLearnedMidiBinding(juce::StringRef parameterId);
void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);
void requestPanic() noexcept;

// src/midi/MidiMappingEngine.h
void setLearnedBindings(std::span<const LearnedCcBinding> bindings);
bool clearLearnedBinding(juce::StringRef parameterId);
const LearnedCcBinding* findLearnedBinding(juce::StringRef parameterId) const noexcept;
MappedAction translate(const ControllerMidiEvent& event) const noexcept;

// src/standalone/StandaloneMidiInput.h
const MidiInputSnapshot& getSnapshot() const noexcept;
LastMidiEventSnapshot getLastMidiEventSnapshot() const noexcept;
void refreshDevices();
bool selectDeviceByIdentifier(const juce::String& deviceIdentifier);
void clearSelection();

// src/standalone/SettingsStore.h
std::optional<PersistedAudioSelection> loadPersistedAudioSelection() const;
std::optional<PersistedMidiInputSelection> loadPersistedMidiInputSelection() const;
std::vector<coolsynth::midi::LearnedCcBinding> loadLearnedMidiMappings() const;
void saveLearnedMidiMappings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);

// src/presets/PatchState.h
PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                     juce::StringRef expectedStateType);
PatchIoResult writePatchFile(const juce::File& destination,
                             const juce::XmlElement& patchXml);
PatchLoadResult readPatchFile(const juce::File& source,
                              juce::StringRef expectedStateType);
```

### 1.4 Invariants That Must Still Hold After Stabilization

These are hard boundaries. A `Phase 16` fix that breaks one of them is not an acceptable stabilization fix.

1. Only automatable synth parameters live inside plugin/APVTS state.

   - Learned MIDI bindings, standalone audio settings, standalone MIDI device selection, and standalone UI preferences remain outside patch files and plugin state.

2. The plugin editor remains free of standalone-only controls.

   - No settings dialog entry point, no standalone status bar, no MIDI monitor, no standalone patch buttons in the VST3 editor.

3. Parameter IDs remain stable.

   - `filterCutoffHz`, `filterResonance`, `delayMix`, `masterGainDb`, and every other existing ID must remain unchanged.

4. Controller-driven parameter writes stay off the audio thread.

   - `processBlock` continues to read parameter atomics and render audio.
   - No new host-notifying parameter writes are allowed inside the audio callback.

5. CI remains trigger-limited.

   - No branch push triggers.
   - No pull-request triggers.
   - No schedule triggers.
   - Manual validation remains `workflow_dispatch` only.
   - Release remains tag-only.

6. Packaging outputs remain stable.

   - Standalone zip, VST3 zip, checksum file, and manifest stay rooted in `build/ci-*/packages` and retain their current naming shape.

7. If actual MiniLab validation requires raw or unsupported message capture, extend the existing monitor path surgically.

   - Do not add ad hoc logging in the audio or device callback.
   - Extend `MidiMonitorMessageType`, `MidiMonitorEvent`, and `formatMonitorMessageType` only if that evidence is actually required.

## 2. File-Level Strategy

### 2.1 Files That Must Be Touched During Phase 16 Execution

| File | Responsibility in Phase 16 | Required outcome |
| --- | --- | --- |
| `TODO.md` | Live phase checklist and leftover-work sink | Reflect only active `Phase 16` items while the phase is in progress; after closure, retain only genuine remaining work |
| `DONE.md` | Verified completion ledger and final release review summary | Record only checks that were actually rerun and verified; resolve the current phase-number drift explicitly |
| `README.md` | User-facing shipped behavior and build/release docs | Match the shipped product, current CI trigger rules, and real artifact outputs without roadmap language |
| `IMPLEMENTATION_PLAN.md` | Final plan-level alignment | Correct any lingering plan wording that no longer matches the accepted shipped state or accepted deferrals |
| `REQUIREMENTS.md` | Final accepted-scope truth source | Align any changed requirements, explicit deferrals, and acceptance wording to what is actually being shipped |
| `DESIGN.md` | Final architecture truth source | Match the actual owner boundaries, standalone/plugin separation, and CI/CD design already in the repo |
| `docs/minilab3-default-messages.md` | Controller evidence document | Either become a true device-capture record or be rewritten to state the remaining gap explicitly |
| `docs/vst3-smoke-test.md` | Host-validation evidence document | Match the actual smoke path, host version, deployment method, and final expected results |

### 2.2 Files To Touch Only If the Corresponding Validation Fails

| File | Touch condition | Specific responsibility of the change |
| --- | --- | --- |
| `.github/workflows/windows-manual-validation.yml` | Manual validation workflow fails or drifts from documented trigger rules | Repair workflow inputs, artifact upload paths, or trigger/permission mistakes without widening automation scope |
| `.github/workflows/windows-release.yml` | Release workflow fails, packages wrong assets, or violates tag-only policy | Repair publish, artifact download, file list, or trigger behavior without redesigning CI |
| `scripts/ci/Common.ps1` | Configure/build/test/package logic is wrong or packaging misses artifacts | Fix path resolution, bootstrap, manifest/checksum generation, or assertion logic |
| `scripts/ci/BuildAndTest.ps1` | Local or CI candidate build/test orchestration is wrong | Fix configuration selection or test gating only |
| `scripts/ci/PackageRelease.ps1` | Release bundle names or package generation are wrong | Fix package invocation and tag-name handling only |
| `CMakePresets.json` | CI preset drift or configuration mismatch blocks candidate builds | Correct preset cache variables or build dirs without changing the local documented path unnecessarily |
| `CMakeLists.txt` | Build/test target definition is wrong for shipped behavior | Correct target wiring, test registration, or release packaging assumptions only |
| `src/plugin/SynthAudioProcessor.cpp` | Shared parameter state, panic, controller application, or plugin state restore fails validation | Fix APVTS state handling, controller action application, or render-parameter assembly |
| `src/plugin/SynthAudioProcessor.h` | A shared-state bug requires a minimal signature or member adjustment | Keep API change narrowly scoped to the discovered blocker |
| `src/plugin/SynthAudioProcessorEditor.cpp` | UI state, patch actions, learn badges, or plugin/standalone surface separation fails validation | Repair editor wiring, layout, or standalone-only visibility |
| `src/plugin/SynthAudioProcessorEditor.h` | Editor fix requires a small state or helper addition | Keep ownership local to the editor |
| `src/midi/Minilab3Profile.cpp` | Verified device messages differ from the embedded table | Update the authoritative control definitions and fixed-binding mapping only after fresh evidence |
| `src/midi/Minilab3Profile.h` | A mapping correction requires enum or signature-shape updates | Keep all MiniLab-specific constants centralized here |
| `src/midi/MidiMappingEngine.cpp` | Fixed mapping, learned-binding shadowing, encoder behavior, or takeover behavior is wrong | Fix only the translation path for the failing scenario |
| `src/midi/MidiMappingEngine.h` | Mapping repair needs a minimal state-model change | Preserve the current owner boundary; do not leak mapping logic into synth code |
| `src/midi/MidiLearn.cpp` | Learn-mode acceptance, replacement, or persistence semantics fail validation | Repair capture and normalization logic only |
| `src/midi/MidiLearn.h` | Learn bug requires a minimal state or outcome extension | Keep changes local to the learn model |
| `src/midi/MidiMonitor.cpp` | Actual-device verification requires capture of unsupported messages | Extend capture types without adding callback logging or allocations |
| `src/midi/MidiMonitor.h` | Monitor repair needs event-model expansion | Extend `MidiMonitorMessageType` and `MidiMonitorEvent` surgically |
| `src/ui/MidiMonitorPanel.cpp` | Monitor UI no longer reflects the captured event model correctly | Update columns or formatting to show the new evidence correctly |
| `src/standalone/StandaloneAudioSupport.cpp` | Audio status, preferred backend selection, or missing-device handling fails | Repair snapshot generation or fallback behavior |
| `src/standalone/StandaloneAudioSupport.h` | Audio-fix state model needs a minimal field change | Keep it as the standalone audio snapshot boundary |
| `src/standalone/StandaloneMidiInput.cpp` | Device selection, disconnect handling, controller-event routing, or last-event status fails | Repair callback-to-UI state handoff or selection logic only |
| `src/standalone/StandaloneMidiInput.h` | Standalone MIDI bug needs a small snapshot/state adjustment | Keep the controller boundary here |
| `src/standalone/SettingsStore.cpp` | Device or learned-binding persistence fails | Repair persistence keys, XML shape, or invalid-entry filtering |
| `src/standalone/SettingsStore.h` | Persistence fix needs a minimal interface change | Keep settings ownership local |
| `src/presets/PatchState.cpp` | Patch wrapping/parsing or state-boundary enforcement fails | Fix only the patch contract or error handling relevant to the failing case |
| `src/presets/PatchState.h` | Patch repair needs a minimal result/error extension | Keep patch state separate from standalone settings |
| `src/synth/SynthEngine.cpp` | Render path, panic, gain smoothing, or delay ordering fails validation | Fix only the failing render responsibility |
| `src/synth/SynthEngine.h` | Audio fix requires a small helper/member change | Preserve the current synth-engine boundary |
| `src/synth/SynthVoice.cpp` | Voice lifecycle, filter behavior, or sample-rate stability fails validation | Repair only the owning voice code |
| `src/synth/SynthVoice.h` | Voice fix requires a small declaration change | Keep filter and envelope behavior inside voice ownership |
| `tests/MidiLearnTests.cpp` | A learned-binding or mapping regression is found | Add a focused regression that proves the bug and its fix |
| `tests/PatchStateTests.cpp` | A patch/state-boundary regression is found | Add a focused regression for the failing state path |

### 2.3 Files That Should Not Be Touched in Phase 16 Unless the Review Scope Changes

- `external/JUCE/**`
- generated build artifacts under `build/**`
- phase blueprints `IMPLEMENTATION_PHASE1.md` through `IMPLEMENTATION_PHASE15.md`

## 3. Atomic Execution Steps

Every `Plan-Act-Validate` cycle below maps directly to one `TODO.md` checkbox. No item moves to `DONE.md` until its `Validate` block is satisfied.

### 3.1 Checkbox 1: Run the full manual validation matrix and capture the results

#### Plan

- Build one master validation ledger from:
  - `REQUIREMENTS.md` Section `15` manual validation scenarios
  - any still-advertised shipped behaviors in `README.md`
  - the host runbook in `docs/vst3-smoke-test.md`
  - the controller runbook in `docs/minilab3-default-messages.md`
- Split the ledger by validation surface:
  - local Debug build
  - local Release build
  - standalone UX and runtime behavior
  - controller mapping and MIDI learn
  - patch/persistence boundaries
  - VST3 host smoke
  - CI/release workflow state
- Predeclare result categories for every row:
  - `pass`
  - `fail`
  - `deferredAccepted`
- Decide up front which rows require hardware or host access so any environment gap is visible immediately.

#### Act

1. Run the local Debug build:

   ```powershell
   cmake --build build --config Debug
   ```

2. Run local Debug tests:

   ```powershell
   ctest --test-dir build -C Debug --output-on-failure
   ```

3. Run the local Release build:

   ```powershell
   cmake --build build --config Release
   ```

4. Run local Release tests:

   ```powershell
   ctest --test-dir build -C Release --output-on-failure
   ```

5. Launch the standalone app and execute every applicable scenario from `REQUIREMENTS.md` Section `15`:
   - no MIDI controller connected
   - no usable audio device if reproducible on the machine
   - MiniLab input selection
   - note playback
   - CC capture
   - MIDI learn bind, persist, and reload
   - 4-note and voice-limit behavior
   - panic
   - patch save/load boundary
   - device disconnect handling
   - sample-rate or buffer-size change stability

6. Run the VST3 smoke procedure from `docs/vst3-smoke-test.md` against the release-candidate build:
   - deploy the plugin copy actually being tested
   - rescan the host
   - insert `CoolSynth`
   - verify note input
   - verify editor opens
   - verify cutoff and master-gain automation
   - verify project save/reopen restores state

7. Compare the actual MiniLab behavior seen in the standalone app against both:
   - `src/midi/Minilab3Profile.cpp`
   - `docs/minilab3-default-messages.md`

8. Record every scenario with:
   - environment used
   - build configuration used
   - pass/fail/deferred result
   - evidence source
   - blocker id if failed

#### Validate

- Every required scenario has one explicit result row.
- No scenario is marked `pass` without a concrete evidence source.
- Any scenario that could not be run is marked `deferredAccepted` only after the docs are updated to say so explicitly.
- The blocker list is complete before any code change begins.

### 3.2 Checkbox 2: Fix any release-blocking defects found during stabilization

#### Plan

- Convert the failures from `3.1` into a strict blocker list.
- Classify each blocker as one of:
  - runtime bug
  - state-boundary bug
  - controller-mapping bug
  - packaging/workflow bug
  - documentation mismatch
  - missing evidence gap
- For each blocker, name the single owning file first.
- For each code blocker, define the cheapest falsifying post-fix validation before editing.

#### Act

1. Fix blockers in priority order:
   - crash or no-sound issues
   - state corruption issues
   - wrong-controller-routing issues
   - packaging/release issues
   - documentation mismatches

2. For every code fix:
   - edit only the owner file and its immediate test if possible
   - avoid cross-cutting cleanup
   - add or extend a focused automated regression if the failure is automatable

3. For controller-evidence blockers:
   - update `Minilab3Profile.*` and `docs/minilab3-default-messages.md` together
   - if the actual device emits unsupported messages, extend the monitor path rather than guessing

4. For patch or persistence blockers:
   - preserve the boundary that synth parameter state remains separate from standalone settings and learned bindings

5. For CI/package blockers:
   - repair scripts or workflow paths, then rerun the same candidate command or workflow

#### Validate

- Every fixed blocker has a before/after trace to the original failing scenario.
- The focused validation that originally failed now passes.
- If a new regression test was possible, it exists and passes.
- No blocker fix widens scope into feature work.

### 3.3 Checkbox 3: Reconcile `README.md`, `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` with shipped behavior

#### Plan

- Diff the final claims made by every authoritative document against:
  - the codebase
  - the validation ledger
  - the release workflow/package outputs
- Resolve one contradiction at a time.
- Prefer correcting stale claims over rewriting the whole document.

#### Act

1. Reconcile product-facing claims in `README.md`:
   - current feature set
   - current standalone-only features
   - current VST3 expectations
   - current build outputs
   - current CI/release trigger rules

2. Reconcile acceptance truth in `REQUIREMENTS.md`:
   - explicit deferrals
   - controller mapping claims
   - actual-device capture requirements
   - release-workflow wording

3. Reconcile architecture truth in `DESIGN.md`:
   - processor ownership
   - standalone/plugin split
   - CI/CD description

4. Reconcile project-progress truth in `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md`:
   - correct any stale phase-language or completed-phase gaps
   - ensure `DONE.md` reflects only verified work
   - ensure `TODO.md` carries only genuine remaining work

5. Reconcile supporting evidence docs:
   - `docs/minilab3-default-messages.md`
   - `docs/vst3-smoke-test.md`

#### Validate

- No authoritative document contradicts the code or the final validation ledger.
- No authoritative document contradicts another authoritative document.
- `README.md` remains product-first and does not regress into roadmap/status narration.
- Any accepted gap is explicitly called deferred instead of being implied as shipped.

### 3.4 Checkbox 4: Confirm every unfinished item is either in `TODO.md` or explicitly deferred

#### Plan

- Treat this as a hidden-scope audit, not a wording pass.
- Search for unresolved work markers across the repository.
- Cross-check the manual validation ledger against the docs and `DONE.md`.

#### Act

1. Search for unfinished-work markers:

   ```powershell
   rg -n "TODO|FIXME|XXX|defer|deferred|later|follow-up|not yet|remaining" README.md REQUIREMENTS.md DESIGN.md IMPLEMENTATION_PLAN.md TODO.md DONE.md docs src tests .github scripts
   ```

2. Review whether each marker is one of:
   - genuine remaining work that must stay in `TODO.md`
   - accepted deferral that must be named in `REQUIREMENTS.md` or `README.md`
   - stale wording that should be removed

3. Resolve the `DONE.md` phase-number gap explicitly.

4. Ensure there is no hidden core work left inside language such as `polish`, `refinement`, or `later`.

#### Validate

- `TODO.md` contains only real remaining work after `Phase 16`.
- `DONE.md` contains only verified completed work.
- Every unfinished behavior is either explicitly deferred in the docs or still listed in `TODO.md`.
- No hidden scope remains behind vague wording.

### 3.5 Checkbox 5: Verify the Windows CI workflow is green at the release candidate state

#### Plan

- Pick one concrete release-candidate commit or tag.
- Revalidate both repository-owned CI paths against that exact state.
- Compare the GitHub-produced packages to the locally produced packages.

#### Act

1. Re-run the local CI-style release command path:

   ```powershell
   pwsh ./scripts/ci/BuildAndTest.ps1 -Configuration Release -RunTests $true
   pwsh ./scripts/ci/PackageRelease.ps1 -Configuration Release -TagName vX.Y.Z-rc.N
   ```

2. Re-run the GitHub manual validation workflow against the release-candidate ref.

3. If the candidate is taggable, run or inspect the tag-triggered release workflow for that same candidate state.

4. Compare these items between local and GitHub outputs:
   - standalone zip name
   - VST3 zip name
   - checksum file name and presence
   - release manifest presence
   - `ctest` results presence

5. Review the uploaded diagnostics and build logs for new warnings.

#### Validate

- The manual validation workflow is green for the candidate state.
- The release workflow is green for the candidate state if a release-candidate tag was exercised.
- Artifact names and contents match the expected packaging contract.
- No new warnings are silently accepted.

### 3.6 Checkbox 6: Prepare the final release review summary for the local standalone and VST3 artifacts

#### Plan

- The summary must let a reviewer reconstruct what was shipped and what evidence supports it.
- Keep the summary concise, but make it auditable.

#### Act

1. Draft the final summary with these fields:
   - candidate commit or tag
   - local Debug build result
   - local Release build result
   - local test results
   - standalone validation summary
   - MiniLab validation summary
   - VST3 host validation summary
   - CI/manual workflow summary
   - release workflow summary
   - accepted deferrals
   - remaining `TODO.md` items, if any

2. Link the summary to the authoritative evidence docs and package names.

3. Place the summary in `DONE.md` by default unless the reviewer explicitly wants a dedicated release-review document.

#### Validate

- A reviewer can answer all of the following from the final summary without reopening the whole repository history:
  - what exact build was reviewed
  - which required behaviors were revalidated
  - which artifacts were produced
  - what remains deferred or unresolved

## 4. Edge Case and Boundary Audit

| Failure mode or trap | Where it manifests | Why it matters in Phase 16 | Required containment |
| --- | --- | --- | --- |
| MiniLab evidence is still simulated rather than device-captured | `docs/minilab3-default-messages.md`, `src/midi/Minilab3Profile.cpp` | The current requirement explicitly demands actual-device capture | Either recapture from the real device or document the gap as deferred; do not silently keep the simulated wording |
| Requirements, docs, and code disagree on the fixed mapping | `REQUIREMENTS.md`, `docs/minilab3-default-messages.md`, `src/midi/Minilab3Profile.cpp` | Final release review cannot bless contradictory controller behavior | Pick one shipped truth and align all three sources to the evidence |
| Encoder waveform behavior is described as deferred but code maps `CC 114` | `docs/minilab3-default-messages.md`, `src/midi/Minilab3Profile.cpp`, `src/midi/MidiMappingEngine.cpp` | This is a concrete behavior/document mismatch | Validate actual hardware behavior and either keep the feature as shipped or defer it explicitly |
| MIDI monitor drops unsupported message kinds | `src/midi/MidiMonitor.cpp` | Actual-device proof may be impossible if the monitor cannot display what the controller emits | Extend the monitor only if actual capture proves it necessary |
| `DONE.md` skips phases `6`, `8`, and `11` | `DONE.md` vs `IMPLEMENTATION_PLAN.md` | The final ledger currently looks incomplete even if the code is not | Resolve whether the skip is clerical or intentional and document it cleanly |
| Plugin editor accidentally exposes standalone-only UI | `src/plugin/SynthAudioProcessorEditor.*` | Violates target separation requirements and makes VST3 smoke invalid | Recheck plugin-editor surface during host validation |
| Patch load overwrites standalone settings or learned bindings | `src/presets/PatchState.*`, `src/plugin/SynthAudioProcessor.cpp`, `src/standalone/SettingsStore.*` | Breaks the required state boundary | Validate patch load before and after restart; add regression coverage if broken |
| Plugin state restore fails in host even though local APVTS XML works | `src/plugin/SynthAudioProcessor.cpp`, host smoke path | Local patch validation is not a substitute for host state restore | Re-run host save/reopen and treat failure as release-blocking |
| Late fix moves host-notifying parameter writes into the audio thread | `src/plugin/SynthAudioProcessor.cpp`, `src/synth/**` | Violates real-time constraints | Keep all host-notifying writes on the UI/standalone controller side only |
| Voice/filter changes become unstable at `44.1 kHz` or `48 kHz` | `src/synth/SynthVoice.cpp`, `src/synth/SynthEngine.cpp` | A late DSP regression is still a release blocker | Recheck filter and note playback at both supported rates |
| Delay-time, filter, or master-gain changes click or destabilize after a late fix | `src/synth/GlobalDelay.cpp`, `src/synth/SynthVoice.cpp`, `src/synth/SynthEngine.cpp` | Stabilization cannot introduce audible regressions | Re-run focused manual playback checks after any DSP-touching fix |
| Selected MIDI device disconnect leaves hanging notes | `src/standalone/StandaloneMidiInput.cpp`, `src/plugin/SynthAudioProcessor.cpp` | Violates the standalone disconnect requirement | Revalidate disconnect and panic after any standalone MIDI fix |
| Learned binding shadowing masks fixed mapping unexpectedly | `src/midi/MidiMappingEngine.cpp` | Controller validation may appear to pass for the wrong reason | Validate both factory mapping and learned-override behavior in clean settings and persisted-settings runs |
| CI workflows are green but package contents are stale or wrong | `.github/workflows/**`, `scripts/ci/**`, `build/ci-release/packages` | A green badge alone is not enough for release sign-off | Compare file names and package contents, not just job status |
| Host loads a stale deployed VST3 copy | `docs/vst3-smoke-test.md`, local machine VST3 folders | Smoke results can be false positives or false negatives | Verify the exact deployed bundle path before testing |
| New warnings appear in build logs | local build logs, GitHub diagnostics | The requirements treat new warnings as defects | Review logs explicitly instead of assuming success means warning-free |
| CI scripts rely on undocumented local state | `scripts/ci/Common.ps1`, workflows, README | Violates the clean-checkout requirement | Revalidate from the documented bootstrap path only |
| Final docs still contain vague words like `later`, `polish`, or `eventually` without explicit defer status | authoritative docs | Hidden unfinished core work is not allowed in the final phase | Replace vague language with either shipped truth or explicit deferral |

## 5. Verification Protocol

### 5.1 Automated Local Verification

1. Configure and build local Debug successfully.
2. Run local Debug `ctest` successfully.
3. Configure and build local Release successfully.
4. Run local Release `ctest` successfully.
5. Run the CI-style Release build/test script successfully.
6. Run the CI-style packaging script successfully.
7. Confirm the local package directory contains:
   - `CoolSynth-windows-x64-standalone-<tag>.zip`
   - `CoolSynth-windows-x64-vst3-<tag>.zip`
   - `CoolSynth-windows-x64-sha256-<tag>.txt`
   - `release-manifest.json`

### 5.2 Manual Standalone UX Checklist

1. Launch the standalone app with no MIDI controller connected.
2. Verify the app opens and shows a disconnected or unavailable MIDI state.
3. If feasible on the machine, launch with no usable audio device and verify a soft failure.
4. Select the MiniLab 3 as the active MIDI input.
5. Play keys and verify audible note playback plus monitor activity.
6. Play a 4-note chord and confirm the voice count behaves as expected.
7. Exceed the voice limit and confirm the voice-steal policy still behaves musically.
8. Move mapped controls and confirm UI updates plus audible changes.
9. Arm MIDI learn on a continuous control, bind a CC, restart the app, and verify persistence.
10. Save a patch, change parameters, load the patch, and verify synth-state restore without overwriting standalone settings or learned mappings.
11. Change sample rate or buffer size and verify the app remains stable.
12. Unplug the selected MIDI device during playback and verify the app remains running and clears held-note state.
13. Press panic and verify active notes stop immediately.

### 5.3 Manual MiniLab Mapping Checklist

1. Verify the real device and template being tested are recorded in the evidence note.
2. Compare each mapped physical control against the current code table in `src/midi/Minilab3Profile.cpp`.
3. Compare the same controls against the public docs in `docs/minilab3-default-messages.md`.
4. Confirm any unmapped control is harmless.
5. If a control emits a message not currently visible in the monitor, treat that as an evidence blocker instead of guessing.

### 5.4 Manual VST3 Host Checklist

1. Build or package the Release VST3 actually being tested.
2. Deploy exactly that bundle to the host-visible VST3 location.
3. Rescan the host.
4. Insert `CoolSynth` on a MIDI track.
5. Verify note input produces sound.
6. Open the editor and confirm there are no standalone-only panels.
7. Automate cutoff and master gain and verify both sound and UI movement.
8. Save and reopen the host project and verify parameter state restore.

### 5.5 CI and Release Candidate Checklist

1. Re-run or inspect the manual validation workflow for the release-candidate ref.
2. Re-run or inspect the tag-triggered release workflow for the release-candidate tag if exercised.
3. Download diagnostics and package artifacts.
4. Verify package names, checksum file, manifest, and `ctest` results presence.
5. Review configure, build, and test logs for new warnings.

### 5.6 Documentation and Exit Checklist

1. `README.md` matches shipped behavior and remains product-first.
2. `REQUIREMENTS.md` reflects the accepted final scope and any changed requirements.
3. `DESIGN.md` matches the actual owner boundaries.
4. `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` are mutually consistent.
5. `docs/minilab3-default-messages.md` and `docs/vst3-smoke-test.md` match real evidence.
6. Any remaining work is either in `TODO.md` or explicitly deferred in the docs.

## 6. Code Scaffolding

Use these only if a new module or regression harness becomes unavoidable. The preferred `Phase 16` path is to extend the existing owner file plus a focused test.

### 6.1 Focused JUCE Regression Test Skeleton

```cpp
#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"

class Phase16RegressionTests final : public juce::UnitTest
{
public:
    Phase16RegressionTests()
        : juce::UnitTest("Phase16Regression", "CoolSynth")
    {
    }

    void runTest() override
    {
        beginTest("describe_the_exact_release_blocker_here");
        {
            SynthAudioProcessor processor;

            // Arrange
            // Act
            // Assert
        }
    }
};

static Phase16RegressionTests phase16RegressionTests;
```

Use this pattern when:

- the blocker is automatable
- the owning code is shared logic rather than a hardware-only surface
- the fix would otherwise rely only on manual proof

### 6.2 Minimal Owner-Local Helper Pattern

```cpp
namespace
{
    bool isReleaseCandidateMismatch(juce::StringRef expected,
                                    juce::StringRef actual) noexcept
    {
        return expected != actual;
    }
}
```

Use this pattern when:

- the fix needs one small file-local helper
- introducing a new cross-module utility would be overkill

### 6.3 Minimal Header/Source Class Pattern If a New Owner Is Truly Required

```cpp
// Example header
class ReleaseValidationHelper final
{
public:
    void reset() noexcept;
    bool isValid() const noexcept;

private:
    bool valid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReleaseValidationHelper)
};
```

Rules if this pattern is used:

- put the class in the narrowest existing folder that already owns the behavior
- prefer plain structs and file-local helpers over introducing another manager class
- do not create a new abstraction layer to wrap JUCE

### 6.4 `DONE.md` Release Review Summary Template

```md
## Phase 16: Final stabilization and release review

- Candidate commit/tag: `<value>`
- Local Debug build: `pass|fail`
- Local Release build: `pass|fail`
- Local tests: `pass|fail`
- Standalone validation: `pass|fail|deferredAccepted`
- MiniLab validation: `pass|fail|deferredAccepted`
- VST3 host validation: `pass|fail|deferredAccepted`
- Manual workflow validation: `pass|fail`
- Release workflow validation: `pass|fail|not-run`
- Release assets reviewed: `<list>`
- Accepted deferrals: `<list or none>`
- Remaining TODO items: `<list or none>`
```

## 7. Closure Conditions

`Phase 16` closes only when all of the following are true:

1. The full validation matrix has been rerun or any remaining gap is explicitly documented as deferred and accepted.
2. Every release-blocking issue discovered during stabilization is either fixed and revalidated or kept open explicitly outside the release claim.
3. The controller mapping story is internally consistent across code, evidence docs, and requirements.
4. The VST3 smoke path is either revalidated at the release-candidate state or explicitly documented as not rerun.
5. The manual validation workflow is green for the candidate state, and the release workflow is green if a candidate tag was exercised.
6. `README.md`, `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` all match the shipped behavior.
7. No hidden unfinished core work remains behind vague wording or undocumented assumptions.
