# Overview

`CoolSynth` V2 is a Windows 11 JUCE/C++20 instrument that ships as both a standalone application and a VST3 plugin from one shared codebase. The V2 goal is to replace the current learning-oriented single-oscillator synth engine with a Prophet-5-inspired, Stranger Things / S U R V I V E-adjacent subtractive synth while preserving the existing standalone/plugin shell, MIDI learn concepts, patch workflow, and Windows release path.

This plan assumes the authoritative product direction comes from `REQUIREMENTS_V2.md` and `DESIGN_V2.md`, with the current codebase used as the migration base rather than a throwaway prototype. The implementation order prioritizes dry synth quality first, then performance behavior, then arpeggiation and effects, then UI/control-surface cutover, then compatibility/release work.

The project is delivered locally on Windows as:

- `build/CoolSynth_artefacts/<Config>/Standalone/CoolSynth.exe`
- `build/CoolSynth_artefacts/<Config>/VST3/CoolSynth.vst3`
- GitHub release packages produced by `.github/workflows/windows-release.yml` and `scripts/ci/*.ps1`

Hard constraints that shape the plan:

- no heap allocation in normal audio rendering
- no locking in the audio path
- no UI-thread coupling into live DSP objects
- shared standalone and VST3 behavior must remain intact
- V1 patch/state compatibility may break, but the break must be explicit and tested
- each reviewable unit must fit the repo rule that one `TODO.md` item stays small enough for one review cycle

# Assumptions

- `REQUIREMENTS_V2.md`, `DESIGN_V2.md`, `README.md`, `TODO.md`, and `DONE.md` are the authoritative project documents for V2 work.
- Risk tolerance is treated as medium: V2 may intentionally break V1 semantics, but realtime regressions, unstable builds, or unclear release behavior are not acceptable.
- Windows 11 remains the only required delivery platform for the first V2 release.
- The existing manual validation and tag-driven release workflows remain the release path; the plan does not add always-on CI triggers.
- The first V2 release includes the mandatory global FX set only: drive, chorus or ensemble, delay, and reverb.
- V2 ships with 8 voices by default, and a selectable 5-voice vintage-limited mode is not part of the first V2 release.
- A single review cycle should be able to absorb one phase worth of work after it is refreshed into atomic `TODO.md` entries.
- Manual VST3 release sign-off uses both Ableton Live Lite and REAPER on Windows.
- V2 keeps the `.cspatch` extension while intentionally breaking V1 patch and state compatibility through a new explicit version boundary.

# Delivery strategy

This plan uses `hybrid delivery`.

- It starts with two foundation phases because the V2 parameter contract, processor seam, and custom allocator are architectural risks that should be isolated before feature work spreads across the repo.
- After that, it switches to thin vertical musical slices: playable dry voice core, tone shaping, performance behavior, arpeggiation, effects, UI, controller integration, and state/release handling.
- This fits the current review cadence because each phase ends in a buildable, testable, reviewable outcome that can be refreshed into a small `TODO.md` batch rather than one long-running branch.
- This reduces delivery risk by keeping the highest-risk refactors separate, delaying UI churn until the core engine is stable, and reserving release/compatibility work for dedicated late phases instead of mixing it into core DSP changes.

# Phase list

- `Phase 1` — V2 parameter contract and processor seam
- `Phase 2` — Custom allocator with basic V2 note playback
- `Phase 3` — Dual-oscillator mixer voice core
- `Phase 4` — Filter and dual-envelope tone path
- `Phase 5` — Performance modulation and play modes
- `Phase 6` — Host-aware arpeggiator
- `Phase 7` — Global V2 effects rack
- `Phase 8` — V2 editor and panel workflow
- `Phase 9` — MIDI learn and controller integration for the V2 surface
- `Phase 10` — V2 patch/state boundary and compatibility handling
- `Phase 11` — Stabilization, release validation, and documentation reconciliation

# Detailed phases

## Phase 1: V2 parameter contract and processor seam

### Goal

Establish the stable V2 APVTS parameter surface and a processor-to-engine seam that later phases can build on without rewriting parameter IDs or editor bindings again.

### Scope

Included:

- define the V2 parameter IDs, groups, defaults, and ranges from `REQUIREMENTS_V2.md`
- rebuild the APVTS layout around the V2 surface
- extend the processor-side parameter binding and block snapshot plumbing for V2
- introduce a V2 engine seam that can coexist with the current engine during migration
- add test coverage for parameter registration, uniqueness, and default serialization

Explicitly out of scope:

- custom voice allocation
- finished V2 DSP behavior
- UI redesign
- controller remapping
- final patch/state compatibility handling

### Expected files to change

- `src/parameters/ParameterIDs.h`
- `src/parameters/ParameterLayout.h`
- `src/parameters/ParameterLayout.cpp`
- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/synth/SynthParameters.h`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `tests/PatchStateTests.cpp`
- `CMakeLists.txt`

### Dependencies

- upstream product direction from `REQUIREMENTS_V2.md`
- upstream architecture decisions from `DESIGN_V2.md`
- no earlier implementation phase
- unresolved naming choice for any new V2-specific parameter helper file is not blocking as long as IDs stay stable

### Risks

- Medium risk because parameter IDs, ranges, and defaults become the automation, MIDI learn, and patch contract for all later work.
- Likely failure modes are unstable IDs, missing required parameter categories, or a layout that forces another incompatible rewrite later.

### Tests and checks to run

- `cmake --preset vs2022-debug`
- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- manual inspection that every required V2 parameter category exists in APVTS

### Review check before moving work to `DONE.md`

Reviewer must confirm that the V2 parameter surface matches `REQUIREMENTS_V2.md`, that no required category is missing, that code quality is acceptable, that regression risk to existing build targets is understood, that any document changes are updated, that build and test results are attached, that no hidden scope creep was added, and that any unfinished follow-up work is written back to `TODO.md`. The reviewer must explicitly confirm that the phase outcome is only the parameter contract and seam, not partial feature work from later phases.

### Exact `TODO.md` entries to refresh from this phase

`Phase 1`

- [ ] Add the stable V2 parameter IDs and grouped parameter definitions required by `REQUIREMENTS_V2.md`.
- [ ] Rebuild the APVTS layout around the V2 oscillator, mixer, filter, envelope, modulation, performance, arp, FX, and output groups.
- [ ] Extend processor-side raw-parameter binding and block snapshot decoding for the V2 surface.
- [ ] Introduce a V2 engine seam in `SynthAudioProcessor` without deleting the current engine path yet.
- [ ] Add or update tests that verify V2 parameter uniqueness, defaults, and serializable APVTS state.
- [ ] Verify Debug configure, build, and `ctest` pass after the V2 parameter-surface change.

### Exit criteria for moving items to `DONE.md`

- V2 parameter IDs and APVTS layout exist in the expected files.
- Every required V2 parameter category from `REQUIREMENTS_V2.md` is present.
- The project builds in Debug.
- `ctest --test-dir build -C Debug --output-on-failure` passes.
- Review is complete and any deferred items are written back to `TODO.md`.

## Phase 2: Custom allocator with basic V2 note playback

### Goal

Deliver a custom V2 event and allocation path that drives basic playable note output without depending on the current `juce::Synthesiser`-style behavior.

### Scope

Included:

- define explicit engine note and performance event records with sample offsets
- implement a deterministic V2 allocator with note-on, note-off, sustain, panic, and voice-steal rules
- route `processBlock()` through the new event boundary and V2 engine render path
- replace callback-lock-dependent mapped-action behavior on the live engine path with a non-blocking bridge compatible with the new event model
- keep the generated sound intentionally simple so the allocator can be reviewed in isolation

Explicitly out of scope:

- finished dual-oscillator voice architecture
- filter, envelopes, modulation, arp, and FX behavior
- UI redesign

### Expected files to change

- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthVoice.h`
- `src/synth/SynthVoice.cpp`
- `src/midi/MidiMappingEngine.h`
- `src/midi/MidiMappingEngine.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `tests/MidiLearnTests.cpp`
- `CMakeLists.txt`

### Dependencies

- `Phase 1`
- current standalone/plugin wrappers staying intact while the render path changes
- shipped default voice count is fixed at 8, but the allocator should still be implemented with configurable internal voice count to avoid brittle logic

### Risks

- High risk because this phase changes the live note-dispatch core and removes implicit behavior currently provided by JUCE helpers.
- Likely failure modes are stuck notes, wrong sustain behavior, broken in-block event timing, or a new control path that reintroduces locking or host-thread coupling.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual smoke test for note on/off, sustain pedal, repeated notes, and panic
- plugin manual smoke test for note playback and silence after transport stop or panic

### Review check before moving work to `DONE.md`

Reviewer must confirm that the new allocator and event model are the only primary outcome, that code quality is acceptable, that requirement traceability exists for note behavior and realtime rules, that regression risk to standalone and plugin note handling is called out, that related docs are updated if behavior changes were made explicit, that build and test results are attached, that no hidden feature work from later phases is bundled in, and that any unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 2`

- [ ] Add explicit sample-offset engine event records for note, bend, mod wheel, and sustain input.
- [ ] Implement deterministic V2 note allocation with sustain, panic, and musically defensible voice stealing.
- [ ] Route `SynthAudioProcessor::processBlock()` through the new V2 event path and render contract.
- [ ] Replace callback-lock-dependent live mapped-action handling with a non-blocking control bridge compatible with the new engine path.
- [ ] Add tests for in-block event ordering, sustain behavior, panic, and voice stealing.
- [ ] Verify standalone and plugin basic note playback on the new allocator path.

### Exit criteria for moving items to `DONE.md`

- The processor renders through the new V2 event/allocation path.
- Basic note playback works in standalone and plugin mode.
- Sustain, panic, and note release behavior are covered by tests.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 3: Dual-oscillator mixer voice core

### Goal

Deliver the V2 dual-oscillator plus noise mixer voice architecture as the default dry sound source.

### Scope

Included:

- implement oscillator A and oscillator B voice behavior
- add pulse, saw, and triangle-capable wave support where required by the design
- add per-oscillator tuning, pulse width, sync, and oscillator B low-frequency mode as required by the V2 surface
- add per-voice noise source and explicit mixer levels
- add bounded pre-filter overload behavior if needed for the target tone

Explicitly out of scope:

- final filter and envelope tone-shaping behavior
- LFO and Poly Mod routing
- mono/unison/glide behavior
- arpeggiator

### Expected files to change

- `src/synth/SynthVoice.h`
- `src/synth/SynthVoice.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthParameters.h`
- `tests/StabilityAndDisconnectTests.cpp`
- `CMakeLists.txt`

### Dependencies

- `Phase 2`
- V2 parameter contract from `Phase 1`
- no unresolved blocker if one strong internal oscillator implementation can cover the required wave categories

### Risks

- High risk because oscillator design can balloon quickly and because sync, pulse width, and low-frequency oscillator B behavior are easy places to overcomplicate the engine.
- Likely failure modes are unstable sync resets, aliasing-prone behavior that is not musically useful, or an overly generic oscillator design that drifts away from the product target.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual audition of single-oscillator, dual-oscillator, noise-blended, detuned, and sync-style dry tones
- manual regression check that panic and note release still behave correctly

### Review check before moving work to `DONE.md`

Reviewer must confirm that the phase outcome is the dry sound-source architecture only, that the implementation matches the required oscillator and mixer categories, that regression risks around note stability and realtime behavior are addressed, that any required docs are updated, that build and test results are attached, that no later-phase filter/modulation/FX work is hidden inside the diff, and that any unfinished follow-up tasks are written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 3`

- [ ] Implement oscillator A controls and waveform behavior required by the V2 parameter surface.
- [ ] Implement oscillator B controls, low-frequency mode, and sync-support behavior required by the V2 parameter surface.
- [ ] Add per-voice noise generation and explicit oscillator/noise mixer levels.
- [ ] Add bounded pre-filter overload or gain staging that supports the target dry tone without destabilizing render.
- [ ] Add tests or render assertions for pulse-width limits, sync reset behavior, detune behavior, and mixer stability.
- [ ] Verify dry dual-oscillator and noise-mixed tones in standalone playback.

### Exit criteria for moving items to `DONE.md`

- Oscillator A, oscillator B, and noise are all audible and independently controllable.
- Required waveform categories, tuning controls, and sync-related controls exist and function.
- Tests or render checks cover the highest-risk oscillator behaviors.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 4: Filter and dual-envelope tone path

### Goal

Deliver the Prophet-adjacent tone-shaping path: dedicated filter envelope, dedicated amp envelope, and a stable per-voice resonant low-pass filter.

### Scope

Included:

- add per-voice filter ADSR and amp ADSR
- implement the V2 low-pass filter path with cutoff, resonance, envelope amount, and keyboard tracking
- wire envelope retrigger, repeated-note behavior, and voice-steal reset behavior into the allocator and voice path
- make the dry voice core musically useful for bass, brass, pluck, and pad programming from init

Explicitly out of scope:

- LFO, Poly Mod, pitch bend, glide, mono/unison, vintage behavior, or arp logic
- global effects

### Expected files to change

- `src/synth/SynthVoice.h`
- `src/synth/SynthVoice.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `CMakeLists.txt`

### Dependencies

- `Phase 3`
- filter character decision from `DESIGN_V2.md` that first release ships one strong default mode
- unresolved optional future filter character switches are not blocking if they stay deferred

### Risks

- High risk because the filter and envelope path define whether V2 sounds materially better than V1.
- Likely failure modes are unstable resonance, envelopes that do not retrigger correctly on voice reuse, or a filter response that feels like a neutral utility filter instead of a defining musical component.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual audition of bass, brass, pluck, pad, and filter-sweep patches from init
- regression check for repeated notes, voice steals, and long release tails

### Review check before moving work to `DONE.md`

Reviewer must confirm that this phase delivers only the dry tone-shaping path, that filter and envelope behavior match the V2 requirements, that regression risk around stability and retrigger logic is addressed, that docs are updated where behavior became explicit, that build and test results are attached, that no unrelated modulation/FX/UI work is bundled in, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 4`

- [ ] Implement dedicated filter ADSR and amp ADSR per voice.
- [ ] Implement the V2 resonant low-pass filter with cutoff, resonance, filter-envelope amount, and keyboard tracking.
- [ ] Wire envelope retrigger and voice-steal reset behavior into repeated-note and stolen-voice cases.
- [ ] Add tests for filter stability across supported sample rates and for envelope restart behavior.
- [ ] Manually verify init-patch bass, brass, pluck, and pad programming on the new dry voice path.
- [ ] Verify Debug build and `ctest` still pass after the filter/envelope cutover.

### Exit criteria for moving items to `DONE.md`

- The voice path has separate filter and amp envelopes.
- Filter cutoff, resonance, envelope amount, and keyboard tracking all function.
- Repeated notes and voice steals do not leave envelopes or filter state corrupted.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 5: Performance modulation and play modes

### Goal

Deliver the V2 performance layer: LFO, Poly Mod, pitch bend, mod wheel, glide, mono/unison behavior, key priority, vintage variance, and pan spread.

### Scope

Included:

- add one global LFO with the selected constrained waveform set
- add Poly Mod-style sources and destinations from the design
- add pitch bend and mod wheel in the engine performance state
- add glide, mono, unison, and key-priority behavior in the allocator
- add bounded vintage/slop and pan-spread behavior

Explicitly out of scope:

- arpeggiator
- global effects
- full UI redesign
- patch/state compatibility handling

### Expected files to change

- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthVoice.h`
- `src/synth/SynthVoice.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `CMakeLists.txt`

### Dependencies

- `Phase 4`
- `Phase 2` allocator/event model
- shipped default voice count is fixed at 8, but stack count and internal voice count should still be parameterized enough to avoid brittle allocator logic
- aftertouch is deferred from the core performance phase and should only be added later if the controller and host path remains low-risk

### Risks

- High risk because this phase combines the most expressive player-facing behavior with the most allocator-sensitive logic.
- Likely failure modes are broken mono priority, unpleasant or unstable glide behavior, chaotic modulation ranges, or vintage behavior that is too random to review reliably.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual checks for bend range, mod wheel response, mono priority, unison stack behavior, glide, and vintage/pan-spread behavior
- plugin manual checks for bend and mod wheel automation or live MIDI input

### Review check before moving work to `DONE.md`

Reviewer must confirm that the phase outcome is the performance behavior layer only, that requirement traceability exists for LFO, Poly Mod, bend, glide, mono/unison, and vintage behavior, that regression risk to note handling is explicitly reviewed, that docs are updated where needed, that build and test results are attached, that no arp/FX/UI work is hidden in the phase, and that any unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 5`

- [ ] Add the global LFO waveforms, rate control, and routed depth controls required by the V2 parameter contract.
- [ ] Implement constrained Poly Mod sources and destinations for oscillator pitch, pulse width, and filter cutoff.
- [ ] Add pitch bend and mod wheel handling in the engine performance state.
- [ ] Implement glide, mono, unison, and key-priority behavior in the allocator.
- [ ] Add bounded vintage/slop and pan-spread behavior with deterministic testable state.
- [ ] Add tests for bend range, mono priority, unison stack count, glide behavior, and vintage variance bounds.

### Exit criteria for moving items to `DONE.md`

- Bend, mod wheel, glide, mono, unison, key priority, and vintage/pan-spread behavior all function.
- LFO and Poly Mod produce the required destinations without adding a general modulation matrix.
- High-risk performance behaviors are covered by tests or explicit manual checks.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 6: Host-aware arpeggiator

### Goal

Deliver the V2 built-in arpeggiator with standalone internal timing and plugin host-sync behavior.

### Scope

Included:

- add held-note tracking, latch handling, pattern logic, octave cycling, and gate timing
- read plugin host timing from `AudioPlayHead::getPosition()` inside `processBlock()`
- add deterministic fallback internal-rate behavior when host tempo or transport data is incomplete
- route arp-generated note events through the same allocator/event path as live keyboard play

Explicitly out of scope:

- global effects
- UI redesign beyond the minimum controls needed for testing if any temporary surface is used
- controller remapping

### Expected files to change

- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthVoice.h`
- `src/synth/SynthVoice.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `docs/vst3-smoke-test.md`
- `CMakeLists.txt`

### Dependencies

- `Phase 2`
- `Phase 5`
- JUCE playhead timing rules from current official docs
- early arp bring-up can use one host, but final release sign-off must still cover both Ableton Live Lite and REAPER

### Risks

- High risk because host transport behavior differs by DAW and because timing bugs are easy to miss in code review alone.
- Likely failure modes are stalled arp playback when timing data is partial, missed note-offs, transport-stop confusion, or timing drift between standalone and plugin behavior.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual arp checks for tempo, latch, gate length, octave range, and bypass behavior
- Ableton Live Lite VST3 manual checks for host tempo sync, transport start/stop behavior, and internal-rate fallback behavior
- REAPER VST3 manual checks during release-signoff once the arp path is otherwise stable

### Review check before moving work to `DONE.md`

Reviewer must confirm that the arpeggiator is the only primary outcome, that timing behavior matches the V2 design, that regression risk to ordinary non-arp keyboard play is addressed, that docs are updated where host behavior is now defined, that build and test results are attached, that no FX/UI/controller work is bundled in, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 6`

- [ ] Add held-note, latched-note, pattern, octave, and gate state for the V2 arpeggiator.
- [ ] Read plugin transport and tempo in `processBlock()` and pass a transport snapshot into the engine.
- [ ] Implement deterministic internal-rate fallback behavior for missing host timing.
- [ ] Route arp-generated note events through the allocator with sample offsets.
- [ ] Add tests for pattern ordering, latch behavior, gate timing, and tempo fallback.
- [ ] Verify standalone and Ableton Live Lite VST3 arp behavior manually before final multi-host release validation.

### Exit criteria for moving items to `DONE.md`

- The arp works in standalone mode with an internal tempo source.
- The arp syncs to host tempo and transport when available and falls back cleanly when not.
- Arp-disabled behavior leaves ordinary note play intact.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 7: Global V2 effects rack

### Goal

Deliver the mandatory first-release global V2 effects chain: drive, chorus or ensemble, delay, reverb, and master output handling.

### Scope

Included:

- add fixed-order global FX modules for drive, chorus or ensemble, delay, reverb, and master gain
- evolve the current delay implementation to fit the V2 rack and voicing
- add reset and tail behavior appropriate for the longer FX path
- expose the required enable, bypass, mix, and core voicing controls for the first-release rack

Explicitly out of scope:

- optional later effects such as phaser, flanger, or tremolo
- patch compatibility handling
- UI redesign beyond what is minimally required to test the rack if temporary controls are needed

### Expected files to change

- `src/synth/GlobalDelay.h`
- `src/synth/GlobalDelay.cpp`
- `src/synth/SynthEngine.h`
- `src/synth/SynthEngine.cpp`
- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `CMakeLists.txt`

### Dependencies

- `Phase 4`
- `Phase 5` for performance-sensitive interaction with the dry voice path
- optional UI and controller work is not required before the rack exists

### Risks

- Medium to high risk because the FX rack can obscure a weak dry engine or destabilize reset and tail behavior if added too early or too loosely.
- Likely failure modes are runaway feedback, excessive CPU cost, broken reset semantics, or patches that only sound good with large ambience hiding source-tone weaknesses.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual audition of dry bypassed patches and effected soundtrack-style patches
- manual reset, panic, and tail-behavior checks in standalone and plugin builds

### Review check before moving work to `DONE.md`

Reviewer must confirm that the mandatory first-release FX rack is the only primary outcome, that dry tone remains strong with FX bypassed, that regression risk around reset/tail behavior is addressed, that docs are updated where signal-flow behavior is now explicit, that build and test results are attached, that no UI/controller/state work is bundled into the phase, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 7`

- [ ] Implement the global drive stage for the V2 output path.
- [ ] Implement the global chorus or ensemble stage for the V2 output path.
- [ ] Rework the existing delay into the fixed-order V2 FX rack.
- [ ] Implement the global reverb stage and output gain handling for the V2 rack.
- [ ] Add reset, bypass, mix, and tail-handling behavior for the full rack.
- [ ] Add tests or render checks for bounded feedback, stable reset behavior, and no obvious dry-path regression.

### Exit criteria for moving items to `DONE.md`

- Drive, chorus or ensemble, delay, reverb, and master output controls all exist and function.
- The dry path remains musically strong with FX bypassed.
- Reset, panic, and tail behavior are stable.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 8: V2 editor and panel workflow

### Goal

Deliver the V2 editor layout so the full synth can be programmed coherently from the UI in standalone and plugin form.

### Scope

Included:

- redesign the editor around the V2 one-page section model from `DESIGN_V2.md`
- keep the full synth reachable on one page if reviewable layout density can be achieved
- add control attachments, labels, and value displays for the V2 parameter surface
- preserve the runtime split between standalone-only utility UI and plugin-only editor behavior

Explicitly out of scope:

- controller remapping
- patch/state compatibility handling
- new release workflow behavior

### Expected files to change

- `src/plugin/SynthAudioProcessorEditor.h`
- `src/plugin/SynthAudioProcessorEditor.cpp`
- `src/ui/SynthSection.h`
- `src/ui/SynthSection.cpp`
- `src/ui/HardwareKnob.h`
- `src/ui/HardwareKnob.cpp`
- `src/ui/HardwareFader.h`
- `src/ui/HardwareFader.cpp`
- `src/ui/StandaloneStatusBar.h`
- `src/ui/StandaloneStatusBar.cpp`
- `src/ui/StandaloneSettingsDialog.h`
- `src/ui/StandaloneSettingsDialog.cpp`
- `src/ui/MidiMonitorPanel.h`
- `src/ui/MidiMonitorPanel.cpp`

### Dependencies

- `Phase 1`
- `Phase 4`
- `Phase 5`
- `Phase 6`
- `Phase 7`
- one-page layout is the target; fallback paging is allowed only if review shows the one-page version is materially unusable

### Risks

- Medium risk because UI churn can hide missing parameter wiring, confuse review, or accidentally reintroduce standalone-only components into the plugin editor.
- Likely failure modes are incomplete attachments, unusable dense layout, or divergence between standalone and plugin editor behavior.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual UI checks for init-patch programming flow, section clarity, and status/settings behavior
- plugin manual UI checks for parameter automation feedback and absence of standalone-only widgets

### Review check before moving work to `DONE.md`

Reviewer must confirm that the phase outcome is the UI cutover only, that core synth sections remain primary, that requirement traceability exists for grouped controls and workflow, that regression risk around shared standalone/plugin editor behavior is addressed, that docs are updated where UI workflow changed materially, that build and test results are attached, that no controller/state/release work is hidden in the phase, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 8`

- [ ] Redesign `SynthAudioProcessorEditor` as a one-page grouped V2 instrument panel first.
- [ ] Add V2 control groups for oscillators, mixer, filter, filter envelope, amp envelope, modulation, performance, arp, FX, and output.
- [ ] Rebind editor attachments, value displays, and labels to the V2 parameter contract.
- [ ] Preserve standalone-only status, settings, and monitor surfaces outside the plugin editor.
- [ ] Manually verify init-patch programming flow and plugin automation feedback in the V2 editor.
- [ ] Verify Debug build and `ctest` still pass after the editor cutover.

### Exit criteria for moving items to `DONE.md`

- The full V2 parameter surface is reachable from the editor.
- The plugin editor contains no standalone-only utility controls.
- The standalone editor still exposes required device and status utilities.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 9: MIDI learn and controller integration for the V2 surface

### Goal

Deliver controller-profile, MIDI learn, optional late-stage aftertouch, and plugin live-CC behavior that works across the expanded V2 control surface without colliding with reserved performance controls.

### Scope

Included:

- update bundled controller-profile behavior for the V2 parameter set
- preserve reserved performance inputs such as notes, bend, mod wheel, and sustain outside generic learn mapping
- extend standalone and plugin MIDI learn behavior to the V2 controls
- persist and restore V2 learned mappings in the correct standalone and plugin state paths
- add channel aftertouch only if it layers into the stabilized MIDI and controller path without forcing broader rework

Explicitly out of scope:

- patch-format compatibility boundary
- release packaging changes

### Expected files to change

- `src/midi/MidiMappingEngine.h`
- `src/midi/MidiMappingEngine.cpp`
- `src/midi/MidiLearn.h`
- `src/midi/MidiLearn.cpp`
- `src/midi/Minilab3Profile.h`
- `src/midi/Minilab3Profile.cpp`
- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `src/standalone/SettingsStore.h`
- `src/standalone/SettingsStore.cpp`
- `resources/controller_profiles/minilab3_arturia_mode.json`
- `tests/MidiLearnTests.cpp`
- `tests/StabilityAndDisconnectTests.cpp`

### Dependencies

- `Phase 1`
- `Phase 8`
- stable V2 performance-control definitions from `Phase 5`
- exact final hardware-mapping layout is not blocking as long as the reserved-input rules stay fixed and aftertouch remains low-risk

### Risks

- Medium risk because the parameter surface is larger and because learned mappings must coexist cleanly with reserved performance input, optional aftertouch, and host automation behavior.
- Likely failure modes are learned mappings hijacking bend or sustain semantics, stale bindings after reload, controller-profile updates that silently miss major V2 controls, or late aftertouch work destabilizing the input path.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual checks with the bundled MiniLab 3 profile or equivalent controller workflow
- plugin manual checks for live CC learn, saved-session restore, and reserved-control behavior
- manual aftertouch checks if aftertouch is added in this phase

### Review check before moving work to `DONE.md`

Reviewer must confirm that the controller and learn behavior remains the only primary outcome, that any aftertouch addition stayed small and low-risk, that requirement traceability exists for reserved performance controls and learned mapping persistence, that regression risk around controller disconnects and host automation is addressed, that docs are updated where control-surface behavior changed, that build and test results are attached, that no patch-format or release work is hidden in the phase, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 9`

- [ ] Update the bundled controller profile data and runtime mapping logic for the V2 parameter surface.
- [ ] Preserve notes, pitch bend, mod wheel, and sustain as reserved performance inputs outside generic MIDI learn.
- [ ] Extend standalone and plugin MIDI learn to the V2 controls.
- [ ] Persist and restore V2 learned mappings in standalone settings and plugin state.
- [ ] Add channel aftertouch only if it fits the stabilized MIDI/controller path without broader redesign.
- [ ] Add tests for learned-CC round-trip, controller-profile override precedence, and disconnect handling.
- [ ] Verify controller workflows manually in standalone and plugin mode.

### Exit criteria for moving items to `DONE.md`

- V2 learnable controls can be mapped and restored in standalone and plugin mode.
- Reserved performance inputs still behave as performance controls rather than generic learned CCs.
- Controller-profile and disconnect regressions are covered by tests or explicit manual checks.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 10: V2 patch/state boundary and compatibility handling

### Goal

Deliver explicit V2 patch and processor-state behavior, including the intentional compatibility break from V1.

### Scope

Included:

- define the V2 processor-state version and patch-format boundary
- update patch save/load to the V2 parameter surface only
- explicitly reject incompatible V1 state or patch payloads unless an approved import path is added
- document the compatibility break and resulting patch behavior

Explicitly out of scope:

- new sound features
- new UI layout work
- release-candidate bug fixing beyond issues directly caused by the V2 state boundary

### Expected files to change

- `src/presets/PatchState.h`
- `src/presets/PatchState.cpp`
- `src/plugin/SynthAudioProcessor.h`
- `src/plugin/SynthAudioProcessor.cpp`
- `tests/PatchStateTests.cpp`
- `README.md`
- `docs/vst3-smoke-test.md`

### Dependencies

- `Phase 1`
- `Phase 8`
- `Phase 9`
- `.cspatch` retention is fixed; only the V2 versioned state boundary remains to be implemented

### Risks

- Medium risk because this phase defines what users can save, reload, and reopen, and because V1 rejection must be clear instead of silently wrong.
- Likely failure modes are unstable serialization, hidden reuse of semantically incompatible V1 IDs, or ambiguous behavior when an old patch is loaded.

### Tests and checks to run

- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- standalone manual save/load check with multiple V2 patches
- plugin manual save/reopen check inside the smoke-test DAW host

### Review check before moving work to `DONE.md`

Reviewer must confirm that the phase outcome is the V2 state boundary only, that requirement traceability exists for the intentional V1 compatibility break, that regression risk around save/load and host session restore is addressed, that documentation updates are complete, that build and test results are attached, that no unrelated feature work is bundled in, and that unfinished follow-up work is written back to `TODO.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 10`

- [ ] Define the V2 processor-state version and patch-format boundary for the new parameter contract.
- [ ] Update patch save and load logic to round-trip only V2 parameter state.
- [ ] Reject incompatible V1 patch or processor-state payloads explicitly unless an import path is later approved.
- [ ] Add tests for V2 patch round-trip, malformed payload rejection, and V1 compatibility-boundary behavior.
- [ ] Update `README.md` and patch workflow documentation to describe the V2 compatibility break.
- [ ] Verify saved V2 patches reload identically in standalone and plugin workflows.

### Exit criteria for moving items to `DONE.md`

- V2 patch and processor state serialize and reload successfully.
- Incompatible V1 state or patches are rejected explicitly rather than loaded silently.
- Tests cover round-trip and malformed or incompatible payload cases.
- Relevant documentation is updated.
- Debug build succeeds and `ctest` passes.
- Review is complete and deferred items are recorded in `TODO.md`.

## Phase 11: Stabilization, release validation, and documentation reconciliation

### Goal

Produce a release-candidate V2 build that passes the local and GitHub validation workflow, with docs and task tracking reconciled to shipped behavior.

### Scope

Included:

- run the full Debug and Release validation matrix
- fix release-blocking regressions uncovered by the full matrix
- verify standalone and VST3 manual smoke flows
- verify GitHub manual validation and release packaging still work for V2
- reconcile project docs and task tracking with the final shipped state

Explicitly out of scope:

- new product features
- speculative post-V2 enhancements

### Expected files to change

- `tests/MidiLearnTests.cpp`
- `tests/PatchStateTests.cpp`
- `tests/StabilityAndDisconnectTests.cpp`
- `.github/workflows/windows-manual-validation.yml`
- `.github/workflows/windows-release.yml`
- `scripts/ci/BuildAndTest.ps1`
- `scripts/ci/PackageRelease.ps1`
- `scripts/ci/Common.ps1`
- `README.md`
- `REQUIREMENTS_V2.md`
- `DESIGN_V2.md`
- `IMPLEMENTATION_PLAN_V2.md`
- `TODO.md`
- `DONE.md`
- `docs/vst3-smoke-test.md`

### Dependencies

- `Phase 1` through `Phase 10`
- any unresolved first-release scope decisions must be closed or explicitly deferred before this phase starts

### Risks

- Medium risk because this phase is where cross-cutting defects, packaging regressions, and documentation drift surface together.
- Likely failure modes are release artifacts that no longer match docs, manual host validation failures, lingering realtime issues that only appear in Release builds, or untracked deferred work that gets lost between `TODO.md` and `DONE.md`.

### Tests and checks to run

- `cmake --preset vs2022-debug`
- `cmake --build --preset build-debug --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`
- `cmake --preset vs2022-release`
- `cmake --build --preset build-release --config Release`
- `pwsh ./scripts/ci/BuildAndTest.ps1 -Configuration Debug -RunTests $true`
- `pwsh ./scripts/ci/BuildAndTest.ps1 -Configuration Release -RunTests $true`
- `pwsh ./scripts/ci/PackageRelease.ps1 -Configuration Release -TagName manual-release-candidate`
- full standalone manual smoke test
- full `docs/vst3-smoke-test.md` manual smoke test in Ableton Live Lite
- full `docs/vst3-smoke-test.md` manual smoke test in REAPER
- GitHub `Windows Manual Validation` workflow run with diagnostics review

### Review check before moving work to `DONE.md`

Reviewer must confirm that the phase outcome is stabilization and release readiness only, that code quality is acceptable across final fixes, that requirement traceability and regression risk have been revisited after the full matrix, that documentation updates are complete, that local and GitHub build and test results are attached, that no hidden scope creep or opportunistic feature work was added, and that every unfinished follow-up item is explicitly written back to `TODO.md` before anything moves to `DONE.md`.

### Exact `TODO.md` entries to refresh from this phase

`Phase 11`

- [ ] Run the full local Debug and Release build and test matrix for V2.
- [ ] Fix any release-blocking defects found during full-matrix validation.
- [ ] Run the standalone and VST3 manual smoke workflows and capture the results.
- [ ] Run the GitHub `Windows Manual Validation` workflow and review diagnostics and packaged artifacts.
- [ ] Reconcile `README.md`, `REQUIREMENTS_V2.md`, `DESIGN_V2.md`, `IMPLEMENTATION_PLAN_V2.md`, `TODO.md`, and `DONE.md` with shipped V2 behavior.
- [ ] Confirm every unfinished item is explicitly deferred in `TODO.md` before closing the release review.

### Exit criteria for moving items to `DONE.md`

- Debug and Release local validation succeed.
- GitHub manual validation succeeds or any failure is resolved and rerun successfully.
- Standalone and VST3 smoke tests pass.
- Release packaging commands produce the expected artifacts.
- Docs and task trackers reflect shipped behavior accurately.
- Review is complete and no unfinished release-blocking work remains outside `TODO.md`.

# Dependency notes

- `Phase 1` is the hard prerequisite for every other phase because it defines the V2 parameter contract.
- `Phase 2` is the hard prerequisite for `Phase 3`, `Phase 5`, and `Phase 6` because allocator and event timing rules must exist before advanced voice behavior and arp routing.
- `Phase 3` and `Phase 4` form the dry synth backbone; later musical validation is not meaningful until both are complete.
- `Phase 5` depends on `Phase 4` because performance modulation should target the finished dry voice path, not a temporary one.
- `Phase 6` depends on `Phase 2` and `Phase 5` because arp output must reuse the allocator and existing performance context cleanly.
- `Phase 7` should not start before `Phase 4` because effects must be judged against a stable dry engine, not compensate for an unfinished one.
- `Phase 8` intentionally waits until most engine behavior is present so the editor does not churn repeatedly while DSP contracts are still moving.
- `Phase 9` depends on `Phase 8` because controller and learn behavior need the finalized exposed V2 control surface.
- `Phase 10` depends on `Phase 9` because the V2 state boundary should lock in only after the parameter, UI, and controller surface is finalized.
- `Phase 11` is a stabilization gate only. Net-new feature work may not enter that phase unchanged.

# Review policy

- Expected review size is one phase at a time after it is refreshed into atomic `TODO.md` entries, typically 4 to 6 independently reviewable checklist items plus matching tests and doc updates.
- A phase must be split before implementation starts if it contains more than one primary goal, spans multiple high-risk categories at once, or cannot end in a buildable and testable repo state.
- High-risk categories for forced splitting are: parameter/state contract changes, allocator/event model changes, core DSP voice-path changes, UI architecture changes, and release workflow changes.
- A phase must also be split if its review would require reasoning about both a foundational refactor and multiple unrelated user-facing features in one pass.
- Oversized phases may not proceed unchanged. `IMPLEMENTATION_PLAN_V2.md` and the phase-specific `TODO.md` refresh must be revised first.

# Definition of done for the plan

- The shipped V2 build provides the required dual-oscillator plus noise voice core, dedicated filter and amp envelopes, resonant low-pass filter, global LFO, constrained Poly Mod, pitch bend, mod wheel, glide, mono/unison behavior, vintage or slop behavior, pan spread, arpeggiator, mandatory global FX rack, and master output stage defined by `REQUIREMENTS_V2.md`.
- All intended V2 sound-shaping controls exist as stable APVTS parameters and work through UI editing, host automation, preset or patch recall, MIDI learn, and controller mapping where applicable.
- Standalone and VST3 outputs build successfully in Debug and Release, local `ctest` passes, and the documented VST3 smoke flow passes in the chosen host.
- Realtime expectations remain intact: no normal render-path heap allocation, no render-path locking, no UI-thread coupling into live DSP objects, and no callback-lock regression for mapped controller writes.
- V2 patch and processor-state behavior is explicit, tested, and documented, including the intentional compatibility boundary with V1.
- Release packaging and GitHub validation still work for the V2 artifacts.
- `README.md`, `REQUIREMENTS_V2.md`, `DESIGN_V2.md`, `IMPLEMENTATION_PLAN_V2.md`, `TODO.md`, `DONE.md`, and `docs/vst3-smoke-test.md` all match shipped behavior, and no unfinished release-blocking work is hidden outside `TODO.md`.

# Open questions

## Blocking unknowns

- None at this time.

## Non-blocking open questions

- Which optional filter character switch should be first if the base 4-pole mode lands early enough to safely extend?
- Which immediate follow-on FX should be first after the base V2 release: phaser, flanger, or tremolo?
- If the first one-page editor review proves too dense, which sections are the least harmful candidates for a fallback secondary view?
