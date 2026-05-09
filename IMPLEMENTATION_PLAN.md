<!-- markdownlint-disable MD024 MD025 -->

# Overview

CoolSynth is a Windows 11 JUCE synthesizer project with two product targets from day one: a standalone desktop application and a VST3 instrument plugin built from one shared processor codebase. The practical delivery path is standalone first, with the VST3 path kept valid from the start so host loading, automation, and state recall can be added without redesign.

- Project name: `CoolSynth`
- Project type: learning-oriented software synthesizer with shared standalone and VST3 targets
- Goal: deliver a clean, playable standalone synth first, then complete the deferred MIDI learn, preset, and CI milestones without reworking the shared architecture
- Constraints: Windows 11 only, JUCE, C++20, CMake, VS Code workflow, MSVC 2022 toolchain, standalone plus VST3 from day one, WASAPI shared mode first, one active MIDI input device at a time, fixed MiniLab 3 mapping before MIDI learn, real-time-safe audio callback rules, no unit-test requirement in the initial milestones
- Existing documents available: `REQUIREMENTS.md`, `DESIGN.md`
- Deployment target: local Windows standalone artifact and local VST3 artifact only
- Risk tolerance: low

This plan uses the milestone intent already captured in `REQUIREMENTS.md`, tightens the ordering where implementation risk needs isolation, and defines each phase as a review-sized unit with explicit tests, file targets, TODO refresh entries, and binary exit criteria.

# Assumptions

- The repository currently contains planning documents only, so the source tree, CMake files, and implementation paths listed below are proposed paths rather than existing files.
- Review cadence is one focused implementation review cycle per phase. A phase should fit into one small pull request or equivalent review chunk and one focused validation pass.
- Local manual validation is the primary acceptance mechanism until the CI phase lands. No separate automated unit-test framework is assumed before then.
- `Ableton Live Lite` is the required Windows VST3 smoke-test host for planning and validation.
- No installer, store packaging, cloud deployment, or cross-platform work is in scope for this plan.
- The JUCE dependency will be added as a git submodule under `external/JUCE`, pinned to a specific commit, and documented in `README.md`.
- The documented default local build path will use the `Visual Studio 17 2022` generator; Ninja may remain an optional local override, but the docs and validation flow will target the Visual Studio generator first.
- `Phase 2` will keep audio bring-up silent. A dedicated test-tone path is intentionally out of scope because it adds non-essential DSP behavior before the first playable synth slice.
- The developer's MiniLab 3 is available in its intended default template state for message capture and mapping validation.
- If the MiniLab 3 default template differs from the preferred control map in `REQUIREMENTS.md`, the verified hardware behavior becomes the implementation source of truth and the relevant docs must be updated.
- The verified MiniLab message table will be stored in both isolated profile code and developer-facing documentation.
- MIDI learn will use a per-parameter context action on eligible controls for both learn and clear-mapping actions, rather than permanent dedicated buttons on the main panel.
- Patch files will use an XML wrapper around APVTS state serialization so patches stay inspectable and consistent with JUCE's documented state model.
- The first CI phase will provide build-only validation from a clean checkout; release artifact publishing is explicitly deferred.
- Plugin metadata is fixed for planning as follows unless legal or business constraints require a documented change before `Phase 11`: product name `CoolSynth`, manufacturer display name `tomcoolpxl`, manufacturer code `Tcpx`, plugin code `Csyn`.
- New compiler warnings introduced by current work are defects and must be cleared before work moves from `TODO.md` to `DONE.md`.

# Delivery strategy

This plan uses a hybrid delivery strategy.

- Early phases use thin vertical slices because the highest-value risks are integration risks: CMake plus JUCE setup, audio-device bring-up, MIDI-device bring-up, shared processor wiring, and a first audible synth path all need to work together to expose real problems early.
- Later phases isolate higher-risk layered work that can destabilize the core slices if bundled together: MiniLab 3 message capture, fixed controller mapping, VST3 smoke validation, standalone persistence, MIDI learn, presets, and CI.
- This strategy fits a standalone-first desktop audio project with low risk tolerance because each review cycle ends in a demonstrable state: a build, a device shell, audible playback, verified hardware control, a host-loaded plugin, persisted settings, or a green CI run.
- This also fits the expected review cadence because each phase has one primary goal, one acceptance surface, and one set of binary exit checks. Oversized mixed-purpose phases are explicitly disallowed.

# Phase list

- `Phase 1`: Reproducible JUCE build skeleton
- `Phase 2`: Standalone audio device shell
- `Phase 3`: Standalone MIDI input shell and monitor
- `Phase 4`: Playable sine synth voice path
- `Phase 5`: Parameter-driven core synth UI
- `Phase 6`: Verified MiniLab 3 message table
- `Phase 7`: Fixed MiniLab core controller mapping
- `Phase 8`: Per-voice low-pass filter slice
- `Phase 9`: Global delay slice
- `Phase 10`: Hardware-style UI refinement
- `Phase 11`: Host-validated VST3 smoke path
- `Phase 12`: Standalone device persistence
- `Phase 13`: MIDI learn workflow
- `Phase 14`: Patch save/load workflow
- `Phase 15`: Windows CI build pipeline
- `Phase 16`: Final stabilization and release review

# Detailed phases

## Phase 1: Reproducible JUCE build skeleton

### Goal

Establish a reproducible JUCE and CMake project that builds shared-code standalone and VST3 targets on Windows and opens a placeholder standalone window.

### Scope

- Add the JUCE git submodule under `external/JUCE`, pin it to a known commit, and document the bootstrap steps.
- Create the top-level CMake project and shared JUCE plugin target with `FORMATS Standalone VST3`.
- Add the initial shared `AudioProcessor`, placeholder editor, and stable APVTS parameter layout covering the first-release parameter set.
- Document the baseline Windows configure and build commands for VS Code and command-line use.

### Expected files to change

- `CMakeLists.txt`
- `CMakePresets.json` or `cmake/*.cmake`
- `external/JUCE/*` or another documented JUCE acquisition path
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/parameters/ParameterIDs.h`
- `src/parameters/ParameterLayout.{h,cpp}`
- `README.md`
- `IMPLEMENTATION_PLAN.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- Windows 11 build environment with CMake 3.22 or newer and MSVC 2022 available.
- The `external/JUCE` git submodule must be initialized before the configure and build checks can run.
- No earlier project phase exists; this is the root dependency for the full plan.

### Risks

- Medium risk: a weak JUCE acquisition choice can make clean-checkout builds fragile.
- Medium risk: unstable plugin metadata or parameter IDs introduced here will create avoidable rework later.
- Low risk: the placeholder editor itself is simple once the build is correct.

### Tests and checks to run

- `git submodule update --init --recursive`
- `cmake -S . -B build -G "Visual Studio 17 2022"`
- `cmake --build build --config Debug`
- Launch the standalone artifact and confirm a window opens without crashing.
- Confirm the build outputs include a standalone artifact and a VST3 artifact.
- Review build output for new warnings.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers build skeleton, metadata, parameter layout, and placeholder UI, not early synth features.
- Trace every introduced parameter ID and plugin-target decision back to `REQUIREMENTS.md` and `DESIGN.md`.
- Review regression risk around build reproducibility, target naming, and shared-code ownership.
- Confirm `README.md` documents the chosen JUCE acquisition method and the baseline Windows configure and build steps.
- Confirm any unresolved follow-up work is written back to `TODO.md` rather than left implicit.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add the JUCE git submodule under `external/JUCE` and pin it to a known commit.
- [ ] Create the top-level CMake project that builds standalone and VST3 outputs from one shared plugin target.
- [ ] Add `SynthAudioProcessor` and `SynthAudioProcessorEditor` placeholder classes.
- [ ] Define the initial APVTS parameter layout with the stable IDs from `REQUIREMENTS.md`.
- [ ] Document the Windows configure and build workflow in `README.md` using `Visual Studio 17 2022` as the default generator.
- [ ] Verify a Debug build produces a launchable standalone artifact and a VST3 artifact.

### Exit criteria for moving items to `DONE.md`

- The JUCE git submodule is committed or otherwise reproducibly referenced, and `README.md` describes the exact bootstrap path.
- `CMakeLists.txt` configures a shared JUCE plugin target with standalone and VST3 formats, and a Debug build succeeds locally.
- The placeholder processor and editor compile, and the standalone target opens a window without crashing.
- The parameter layout contains the required first-release parameter IDs exactly as specified.
- The documented build steps are complete enough for another reviewer to follow from a clean checkout.
- Review is complete, no new warnings are accepted, and unfinished follow-up items are recorded in `TODO.md`.

## Phase 2: Standalone audio device shell

### Goal

Deliver a standalone audio shell that exposes device selection and status while remaining stable when no usable output device is available.

### Scope

- Add standalone audio-device configuration UI or equivalent JUCE-native controls.
- Show the active backend, output device, sample rate, and buffer size in the standalone UI.
- Default to WASAPI shared mode when available, with safe fallback behavior if another backend is needed locally.
- Ensure audio-device changes trigger safe reprepare behavior without crashing the app.

### Expected files to change

- `src/standalone/AudioDevice*.{h,cpp}`
- `src/standalone/StandaloneRuntime*.{h,cpp}`
- `src/ui/StatusStrip*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 1` must be complete.
- The standalone target from `Phase 1` must launch successfully before device-shell work begins.
- The exact JUCE standalone-wrapper strategy chosen in `Phase 1` must remain stable through this phase.

### Risks

- Medium risk: audio-device restart and buffer-size changes can expose lifecycle bugs.
- Medium risk: standalone-only device code can leak into shared processor responsibilities if boundaries are not kept strict.
- Low risk: status-strip display work is straightforward once device state is available.

### Tests and checks to run

- `cmake --build build --config Debug`
- Launch the standalone app with no usable audio device and confirm it stays open.
- Select a WASAPI output device and confirm the UI reports the active backend and device.
- Change output device, sample rate, and buffer size and confirm the app remains stable.
- Review code paths to ensure device enumeration and settings logic are not in the audio callback.

### Review check before moving work to `DONE.md`

- Confirm the phase only introduces standalone audio-shell behavior and does not pull synth-engine work forward.
- Trace UI and lifecycle behavior back to the standalone audio requirements and the shared-versus-standalone separation rules.
- Review regression risk around device restarts, backend switching, and app startup with no audio device.
- Confirm `README.md` reflects any user-facing audio bring-up steps or known backend assumptions.
- Confirm any unresolved audio-shell follow-up items are written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add standalone audio-device selection controls.
- [ ] Add standalone status display for backend, output device, sample rate, and buffer size.
- [ ] Default standalone startup to WASAPI shared mode when available.
- [ ] Handle output-device, sample-rate, and buffer-size changes without crashing.
- [ ] Keep the standalone app running when no valid output device is available.
- [ ] Verify the audio shell build and manual bring-up checks.

### Exit criteria for moving items to `DONE.md`

- The standalone UI exposes audio-device selection and active audio status.
- The app can start and remain open even if no usable output device is available.
- Device, sample-rate, and buffer-size changes complete without crashes on the development machine.
- Standalone-only device logic remains outside the shared synth core by code review.
- The manual audio-shell checks are recorded as passing, docs are current, and review is complete.

## Phase 3: Standalone MIDI input shell and monitor

### Goal

Deliver one-active-device MIDI input handling in standalone mode together with a bounded MIDI monitor suitable for bring-up and debugging.

### Scope

- List available MIDI input devices and allow the user to select one active input.
- Show connected, disconnected, or unavailable state in the standalone UI.
- Add a bounded recent-history MIDI monitor that captures lightweight event records only.
- Ignore unsupported MIDI message types safely and keep the app running through disconnects.

### Expected files to change

- `src/standalone/MidiInput*.{h,cpp}`
- `src/midi/MidiMonitor*.{h,cpp}`
- `src/ui/MidiMonitorPanel*.{h,cpp}`
- `src/ui/StatusStrip*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 1` and `Phase 2` must be complete.
- The standalone app must already launch and expose the status area used by the MIDI shell.
- Access to the developer's MIDI controller is needed for full manual validation but not for the basic shell implementation.

### Risks

- Medium risk: MIDI monitor buffering can violate the real-time constraints if it formats strings or allocates in the wrong thread.
- Medium risk: device-disconnect behavior can leave stale state behind if boundaries are unclear.
- Low risk: simple device-selection UI is straightforward.

### Tests and checks to run

- `cmake --build build --config Debug`
- Launch the standalone app with no MIDI controller connected and confirm the disconnected state is visible.
- Select the MiniLab 3 as the active MIDI input and confirm note and CC messages appear in the monitor.
- Send unsupported MIDI messages if available and confirm they are ignored safely.
- Disconnect the selected MIDI device and confirm the app remains running.
- Review monitor code to confirm event capture stores data records only and does not format strings in the audio callback.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers standalone MIDI shell and monitor behavior, not controller mapping.
- Trace the selection, monitor, and safety behavior back to the MIDI requirements and monitor design notes.
- Review regression risk around device disconnects, unsupported messages, and bounded storage.
- Confirm any monitor limitations or hardware assumptions are documented in `README.md` or carried in `TODO.md`.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add standalone MIDI input selection for one active device at a time.
- [ ] Add standalone MIDI device status reporting.
- [ ] Add a bounded MIDI monitor that shows timestamp or order, type, channel, and message values.
- [ ] Ignore unsupported MIDI messages safely.
- [ ] Keep the app running when the selected MIDI device disconnects.
- [ ] Verify MiniLab 3 note and CC events appear in the monitor.

### Exit criteria for moving items to `DONE.md`

- The standalone app can select one active MIDI input device and show its state.
- The MIDI monitor displays the required event fields from a bounded history buffer.
- Unsupported messages do not crash the app or corrupt the monitor.
- Disconnecting the selected MIDI device leaves the app running.
- Manual checks are recorded, docs are current, and review is complete.

## Phase 4: Playable sine synth voice path

### Goal

Deliver the first audible synth path: 8 preallocated voices, sine playback, per-voice ADSR amplitude control, smoothed master gain, and panic.

### Scope

- Add the shared synth engine wrapper around `juce::Synthesiser` with a fixed 8-voice pool.
- Implement per-voice sine oscillator playback, per-voice ADSR amplitude envelope, and velocity-to-amplitude scaling only.
- Implement release-first then oldest-active voice stealing.
- Add the panic path and ensure held-note state is cleared when panic is triggered.

### Expected files to change

- `src/synth/SynthEngine.{h,cpp}`
- `src/synth/SynthVoice*.{h,cpp}`
- `src/synth/SynthSound*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/parameters/ParameterLayout.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 1`, `Phase 2`, and `Phase 3` must be complete.
- The APVTS parameter layout from `Phase 1` must already expose the ADSR and master-gain parameters.
- Standalone MIDI input from `Phase 3` must already reach the shared processor.

### Risks

- Medium-high risk: the first audible path can expose callback-order, voice-lifetime, and stuck-note bugs.
- Medium risk: voice stealing can drift from the documented release-first policy if JUCE defaults are used blindly.
- Medium risk: panic handling can miss held-note bookkeeping if ownership boundaries are unclear.

### Tests and checks to run

- `cmake --build build --config Debug`
- Play notes from the MiniLab 3 or another MIDI source and confirm audible sine-wave output.
- Play at least a four-note chord and confirm notes sound simultaneously.
- Exceed eight voices and confirm the documented stealing behavior.
- Release notes and confirm sound stops cleanly under normal use.
- Trigger panic and confirm active notes stop immediately.
- Review the audio path for no heap allocation, no logging, and no locking in the callback.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers the playable sine synth core and does not pull waveform UI, filter, or delay work forward.
- Trace the voice model, ADSR behavior, master gain, and panic behavior back to the synth-engine requirements.
- Review regression risk around real-time safety, stuck notes, and voice stealing.
- Confirm `README.md` notes any manual setup needed to hear the first playable path.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add `SynthEngine` with 8 preallocated voices.
- [ ] Implement per-voice sine oscillator playback.
- [ ] Implement per-voice ADSR amplitude envelope and velocity-to-amplitude scaling.
- [ ] Implement release-first then oldest-active voice stealing.
- [ ] Add panic handling that clears active voices and held-note state.
- [ ] Verify standalone MIDI input produces audible notes and chord playback.

### Exit criteria for moving items to `DONE.md`

- The shared synth engine compiles and renders audible sine notes in standalone mode.
- A fixed eight-voice pool is in place and voice stealing matches the documented policy.
- ADSR and master-gain behavior are connected to the shared parameter model.
- Panic stops active notes immediately and clears held-note bookkeeping.
- Manual playback checks pass, real-time constraints are reviewed, docs are current, and review is complete.

## Phase 5: Parameter-driven core synth UI

### Goal

Deliver a functional core control surface that drives waveform, ADSR, and master gain through the canonical parameter model.

### Scope

- Add the first hardware-synth-like UI sections for oscillator, envelope, and output.
- Implement waveform support for sine, square, and saw and bind it to the discrete waveform parameter.
- Attach UI controls to APVTS parameters with meaningful value formatting.
- Ensure UI changes are audibly reflected and remain responsive during playback.

### Expected files to change

- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/HardwareKnob*.{h,cpp}`
- `src/ui/HardwareFader*.{h,cpp}`
- `src/ui/SynthSection*.{h,cpp}`
- `src/synth/SynthVoice*.{h,cpp}`
- `src/parameters/ParameterLayout.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 4` must be complete.
- The stable parameter IDs from `Phase 1` must remain unchanged.
- The synth voice implementation from `Phase 4` must already be audible before UI binding begins.

### Risks

- Medium risk: attachment lifetime bugs can desynchronize UI and processor state.
- Medium risk: waveform switching can produce small clicks or incorrect voice updates if handled carelessly.
- Low risk: value-label formatting is straightforward once parameter ranges exist.

### Tests and checks to run

- `cmake --build build --config Debug`
- Change waveform between sine, square, and saw and confirm audible differences.
- Adjust attack, decay, sustain, release, and master gain and confirm audible changes.
- Confirm the UI shows values in meaningful units.
- Confirm the UI stays responsive during playback.
- Review code to confirm UI interacts through parameter attachments or other thread-safe messaging, not direct voice mutation.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers core synth controls and the minimum supporting engine work required for them.
- Trace the waveform, envelope, and output behavior back to the parameter and UI requirements.
- Review regression risk around parameter attachments, waveform switching behavior, and UI responsiveness.
- Confirm any changed user-facing behavior is reflected in `README.md`.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add waveform support for sine, square, and saw.
- [ ] Add APVTS-backed UI controls for waveform, ADSR, and master gain.
- [ ] Add meaningful value formatting for envelope times, sustain, and master gain.
- [ ] Organize the first editor layout into oscillator, envelope, and output sections.
- [ ] Verify UI changes are audible and remain responsive during playback.
- [ ] Verify hardware-style control attachments stay in sync with the shared parameter model.

### Exit criteria for moving items to `DONE.md`

- The standalone editor exposes working controls for waveform, ADSR, and master gain.
- The waveform parameter produces audible sine, square, and saw output.
- Value labels display meaningful units and update correctly.
- UI interactions remain parameter-driven and do not bypass the shared state model.
- Manual checks pass, docs are current, and review is complete.

## Phase 6: Verified MiniLab 3 message table

### Goal

Capture and record the actual MiniLab 3 default messages needed for the fixed mapping milestone.

### Scope

- Use the standalone MIDI monitor to capture the real note, knob, fader, pad, and encoder messages emitted by the developer's MiniLab 3.
- Record the verified mapping table in both isolated profile code and documentation, with explicit notes on any deviations from the preferred mapping in `REQUIREMENTS.md`.
- Decide and document whether pads and the main encoder remain deferred based on actual observed behavior.
- Do not yet apply parameter changes from the controller; this phase is evidence gathering and binding-table definition only.

### Expected files to change

- `src/midi/Minilab3Profile*.{h,cpp}` or `docs/minilab3-default-messages.md`
- `README.md`
- `REQUIREMENTS.md` if actual hardware behavior changes accepted mapping assumptions
- `DESIGN.md` if the mapping-model design needs clarification after capture
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 3` must be complete because the MIDI monitor is the capture tool.
- Access to the developer's actual MiniLab 3 in its intended template state is assumed available and required for validation.
- `Phase 5` is not a hard technical dependency for message capture, but by default the plan keeps core sound and UI in place first so the later mapping phase is immediately reviewable.

### Risks

- Medium risk: the controller's actual default template may not match the preferred control map.
- Medium risk: undocumented capture results can cause later controller logic to drift from observed behavior.
- Low risk: the phase is intentionally narrow and should not destabilize audio behavior.

### Tests and checks to run

- `cmake --build build --config Debug` if code changes are made.
- Run the standalone app and capture notes, knobs, faders, pads, and encoder traffic from the MiniLab 3.
- Compare the recorded table against observed monitor events.
- Confirm the documented table distinguishes implemented controls from deferred controls.

### Review check before moving work to `DONE.md`

- Confirm the phase records evidence only and does not smuggle in controller-mapping behavior.
- Trace the captured table back to observed monitor output and the fixed-mapping requirements.
- Review risk around incorrect assumptions, deferred pad behavior, and future mapping isolation.
- Confirm docs were updated wherever the captured hardware behavior differs from prior assumptions.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Capture the MiniLab 3 default note and CC messages for the controls planned for the fixed mapping milestone.
- [ ] Record the verified message table in isolated profile code and developer-facing documentation.
- [ ] Document any deviations from the preferred mapping assumptions.
- [ ] Decide whether pads remain deferred based on observed default behavior.
- [ ] Decide whether the main encoder remains deferred based on observed default behavior.
- [ ] Verify the recorded table matches actual monitor output.

### Exit criteria for moving items to `DONE.md`

- A reviewer can inspect committed code or docs and see the exact verified MiniLab 3 message table.
- The table clearly distinguishes required mappings from deferred controls.
- Any requirement or design drift caused by observed hardware behavior has been documented.
- No controller-to-parameter behavior has been added in this phase.
- Validation notes are recorded, docs are current, and review is complete.

## Phase 7: Fixed MiniLab core controller mapping

### Goal

Apply the verified MiniLab 3 core mappings for waveform, ADSR, master gain, and panic through the shared parameter model without violating the real-time rules.

### Scope

- Implement `MidiMappingEngine` and the standalone control-thread path that mutates APVTS parameters off the audio thread.
- Route verified MiniLab knob and fader messages to waveform, ADSR, and master-gain parameters.
- Route the panic action through an explicit command path rather than an automatable parameter.
- Keep all controller-specific constants isolated from synth-engine and voice code.

### Expected files to change

- `src/midi/MidiMappingEngine.{h,cpp}`
- `src/midi/Minilab3Profile.{h,cpp}`
- `src/standalone/MidiInput*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 5` must be complete because the target parameters and UI feedback already need to exist.
- `Phase 6` must be complete because verified MiniLab messages are the input contract for this phase.
- The off-audio-thread parameter-update approach defined in `DESIGN.md` must remain viable in the chosen standalone wrapper.

### Risks

- High risk: parameter writes performed in the wrong thread or path can break real-time safety and host behavior.
- Medium risk: controller-specific constants can leak into shared synth classes if isolation is not enforced.
- Medium risk: panic handling can diverge between UI and controller input if command routing is split.

### Tests and checks to run

- `cmake --build build --config Debug`
- Play notes and change mapped controls from the MiniLab 3 in standalone mode.
- Verify Knob 1 changes waveform, Knobs 4 through 7 change ADSR, and Fader 1 changes master gain.
- Verify the UI reflects controller-driven parameter changes.
- Review code to confirm `setValueNotifyingHost()` or equivalent host-notifying parameter writes are not called from `processBlock`.
- Review code to confirm MiniLab constants do not appear in synth-engine or voice files.

### Review check before moving work to `DONE.md`

- Confirm the phase only adds the verified core mapping path and does not pull filter or delay mappings forward.
- Trace each mapping to the verified MiniLab table and the fixed-mapping requirements.
- Review regression risk around thread boundaries, panic routing, and UI synchronization.
- Confirm the docs explain any known controller assumptions or deferred controls.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add `MidiMappingEngine` for fixed MiniLab 3 controller routing.
- [ ] Route verified MiniLab waveform, ADSR, and master-gain controls to APVTS parameters.
- [ ] Route the panic action through an explicit command path.
- [ ] Keep MiniLab-specific constants isolated from synth-engine and voice code.
- [ ] Verify controller-driven parameter changes update the UI in standalone mode.
- [ ] Verify no host-notifying parameter writes occur in the audio callback.

### Exit criteria for moving items to `DONE.md`

- MiniLab waveform, ADSR, and master-gain controls work in standalone mode using the shared parameter model.
- Panic can be triggered from the mapped control path and clears active notes safely.
- MiniLab constants remain isolated to the MIDI mapping area by code review.
- The audio callback remains free of host-notifying parameter writes.
- Manual hardware checks pass, docs are current, and review is complete.

## Phase 8: Per-voice low-pass filter slice

### Goal

Add the required per-voice low-pass filter with audible cutoff and resonance control in both UI and fixed MiniLab mapping.

### Scope

- Implement a per-voice JUCE low-pass filter inside each synth voice.
- Expose cutoff and resonance through the shared parameter model with stable mapping behavior.
- Add cutoff and resonance UI controls and value display.
- Extend the fixed MiniLab mapping so Knob 2 controls cutoff and Knob 3 controls resonance.

### Expected files to change

- `src/synth/SynthVoice*.{h,cpp}`
- `src/parameters/ParameterLayout.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/SynthSection*.{h,cpp}`
- `src/midi/MidiMappingEngine.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 4`, `Phase 5`, and `Phase 7` must be complete.
- The filter parameters created in `Phase 1` must still be stable and unchanged.
- The standalone fixed-mapping path must already be working before filter mappings are added.

### Risks

- Medium-high risk: filter stability and resonance behavior can become unstable at supported sample rates if parameter mapping is poor.
- Medium risk: abrupt cutoff changes can click without smoothing or safe update timing.
- Low risk: UI exposure is straightforward after the DSP behavior is stable.

### Tests and checks to run

- `cmake --build build --config Debug`
- Confirm cutoff changes are clearly audible across the supported range.
- Confirm resonance changes are clearly audible and remain stable.
- Verify filter behavior at both 44.1 kHz and 48 kHz.
- Verify Knob 2 controls cutoff and Knob 3 controls resonance.
- Review code to confirm the filter is per voice, not a temporary global substitute.

### Review check before moving work to `DONE.md`

- Confirm the phase delivers a per-voice filter and does not substitute a global filter for convenience.
- Trace cutoff and resonance behavior back to the synth and UI requirements.
- Review regression risk around filter instability, smoothing, and parameter-range mapping.
- Confirm `README.md` or other user-facing docs mention the new controls if needed.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add a per-voice low-pass filter to each synth voice.
- [ ] Wire cutoff and resonance to the shared parameter model.
- [ ] Add cutoff and resonance controls to the editor.
- [ ] Extend fixed MiniLab mapping so Knob 2 controls cutoff and Knob 3 controls resonance.
- [ ] Verify filter stability at 44.1 kHz and 48 kHz.
- [ ] Verify cutoff and resonance are audibly reflected in standalone playback.

### Exit criteria for moving items to `DONE.md`

- Each synth voice contains the required low-pass filter implementation.
- Cutoff and resonance parameters work from the UI and the mapped MiniLab controls.
- Filter behavior is stable at the supported sample rates.
- No global-filter shortcut has been used in place of the required per-voice architecture.
- Manual checks pass, docs are current, and review is complete.

## Phase 9: Global delay slice

### Goal

Add the required global delay effect with safe time, feedback, and mix control and the corresponding fixed MiniLab mappings.

### Scope

- Implement a global post-voice delay with preallocated buffers and real-time-safe control updates.
- Expose delay time, feedback, and mix through the shared parameter model.
- Add delay UI controls and value display.
- Extend fixed MiniLab mapping so Knob 8 controls mix, Fader 2 controls feedback, and Fader 3 controls time.

### Expected files to change

- `src/dsp/Delay*.{h,cpp}` or `src/synth/GlobalDelay*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/parameters/ParameterLayout.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/SynthSection*.{h,cpp}`
- `src/midi/MidiMappingEngine.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 4`, `Phase 5`, and `Phase 7` must be complete.
- This phase does not technically require `Phase 8`, but the plan keeps one major DSP feature per review cycle, so it follows the filter phase by design.
- Delay parameters defined in `Phase 1` must remain stable.

### Risks

- Medium risk: delay-time changes can introduce artifacts or unsafe buffer handling if implemented carelessly.
- Medium risk: feedback must be bounded or the effect can run away.
- Low risk: delay UI and mapped controls are straightforward after the DSP path is stable.

### Tests and checks to run

- `cmake --build build --config Debug`
- Verify delay mix above zero produces an audible delayed signal.
- Verify delay mix at zero behaves as effectively dry.
- Verify feedback remains bounded and does not run away.
- Manually change delay time and confirm the app stays stable with no audio-thread allocation.
- Verify Knob 8, Fader 2, and Fader 3 control the intended delay parameters.

### Review check before moving work to `DONE.md`

- Confirm the phase only adds the global delay slice and does not hide unrelated UI or persistence work.
- Trace delay controls and control mappings back to the effect requirements.
- Review regression risk around feedback safety, delay-time changes, and callback allocation behavior.
- Confirm docs mention any known behavior limits, such as small transient artifacts during manual delay-time changes.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add the global delay effect after voice mixing.
- [ ] Wire delay time, feedback, and mix to the shared parameter model.
- [ ] Add delay controls to the editor.
- [ ] Extend fixed MiniLab mapping for delay mix, feedback, and time.
- [ ] Clamp feedback to the safe maximum.
- [ ] Verify manual delay-time changes remain stable and real-time safe.

### Exit criteria for moving items to `DONE.md`

- The delay effect is in the shared post-voice signal path and works from the UI and mapped hardware controls.
- Delay mix at zero is effectively dry and feedback is capped safely.
- Manual delay-time changes do not crash, allocate in the callback, or cause runaway feedback.
- Manual effect checks pass, docs are current, and review is complete.

## Phase 10: Hardware-style UI refinement

### Goal

Refine the standalone and plugin editors into the intended hardware-synth-like layout, keep the primary synth surface focused on sound controls, and consolidate standalone-only utility surfaces into one settings workflow.

### Scope

- Keep the primary editor layout focused on oscillator, filter, envelope, delay, output, and global action surfaces, with panic presented as a standalone action rather than embedded in the output section.
- Move standalone-only audio backend selection, output-device configuration, MIDI input selection, and MIDI monitor presentation into one dedicated settings surface instead of placing them in the main synth panel.
- Replace the large standalone status panel with a compact bottom status bar that shows audio state, MIDI-device state, and a live-updating summary of the most recent MIDI event.
- Remove redundant standalone entry points so one settings workflow owns audio and MIDI utility controls.
- Ensure the plugin editor omits standalone-only device selectors, settings entry points, status surfaces, and monitor surfaces.
- Improve readability of labels and value displays without introducing heavy custom graphics or skinning systems.

### Expected files to change

- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/StatusBar*.{h,cpp}` or `src/ui/StatusStrip*.{h,cpp}`
- `src/ui/MidiMonitorPanel*.{h,cpp}`
- `src/ui/StandaloneSettings*.{h,cpp}`
- `src/ui/StandaloneMidiInputPanel*.{h,cpp}`
- `src/ui/StandaloneAudioStatusPanel*.{h,cpp}` or a replacement settings component
- `src/ui/Hardware*.{h,cpp}`
- `src/ui/SynthSection*.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 2`, `Phase 3`, `Phase 5`, `Phase 8`, and `Phase 9` must be complete.
- Shared parameter and effect controls must already exist so this phase remains layout-focused.
- Plugin and standalone editor boundaries must already be understood from earlier phases.

### Risks

- Low-medium risk: UI refinement can sprawl into open-ended polish if the scope is not held tightly.
- Medium risk: standalone-only panels can accidentally leak into the plugin editor.
- Low risk: the required layout changes are mostly composition and presentation work.

### Tests and checks to run

- `cmake --build build --config Debug`
- Launch the standalone app and confirm the UI reads as a synth panel with clear section grouping.
- Open the standalone settings workflow and confirm audio backend or device controls, MIDI input selection, and the MIDI monitor all live there rather than in the main synth panel.
- Confirm the bottom status bar stays visible during normal use and updates when audio or MIDI device state changes.
- Press keys and move MiniLab controls and confirm the bottom status bar updates a last-MIDI-event summary without requiring the monitor to stay open.
- Load the VST3 editor in a host or plugin preview path if available and confirm standalone-only panels are absent.
- Confirm labels and value displays remain readable without relying on color alone.

### Review check before moving work to `DONE.md`

- Confirm the phase is layout refinement only and does not introduce new synth features.
- Trace the grouped layout, settings-surface consolidation, and mode-specific editor surfaces back to the UI requirements.
- Review regression risk around editor divergence and accidental standalone-only UI in the plugin.
- Confirm any user-facing layout changes are reflected in docs if needed.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Refine the editor into grouped oscillator, filter, envelope, delay, output, and global action sections.
- [ ] Move standalone audio and MIDI utility controls into one dedicated settings surface.
- [ ] Replace the large standalone status panel with a compact bottom status bar.
- [ ] Add live last-MIDI-event status text to the standalone status bar.
- [ ] Remove redundant standalone audio or MIDI settings entry points.
- [ ] Ensure the plugin editor omits standalone-only device, settings, status, and monitor UI.
- [ ] Improve control labels and value readability.
- [ ] Verify the refined UI remains usable during playback.

### Exit criteria for moving items to `DONE.md`

- The standalone UI reads as a synth panel rather than a generic form.
- Standalone-only audio and MIDI utilities live behind one settings workflow rather than occupying the primary synth surface.
- The standalone status bar exposes current audio state, MIDI-device state, and a live last-MIDI-event summary.
- Plugin-only editors omit standalone settings, status, and monitor surfaces.
- Labels and value displays are readable and meaningful.
- No new features were bundled into the refinement phase.
- Manual layout checks pass, docs are current, and review is complete.

## Phase 11: Host-validated VST3 smoke path

### Goal

Validate the shared VST3 path in a real host, including host MIDI note input, automation, editor loading, and plugin state restore.

### Scope

- Finalize VST3 metadata, bus layout, and shared processor behavior needed for host loading.
- Ensure APVTS state save and restore covers the required first-release parameters.
- Verify host MIDI note input produces sound in the plugin.
- Verify host automation for at least cutoff and master gain and document the local smoke-test workflow.

### Expected files to change

- `CMakeLists.txt`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/parameters/ParameterIDs.h`
- `src/parameters/ParameterLayout.{h,cpp}`
- `README.md` or `docs/vst3-smoke-test.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 10` must be complete.
- Stable parameter IDs from `Phase 1` must not change during this phase.
- Filter and master-gain behavior must already exist so host automation has meaningful targets.
- Access to `Ableton Live Lite` on Windows is a blocker for final validation.

### Risks

- High risk: plugin metadata or bus-layout mistakes can prevent the host from loading the plugin.
- High risk: weak state serialization can break reopen behavior.
- Medium risk: host-specific quirks can tempt implementation of out-of-scope host CC remapping.

### Tests and checks to run

- `cmake --build build --config Debug`
- `cmake --build build --config Release`
- Load the VST3 artifact in `Ableton Live Lite` on Windows.
- Play host-routed MIDI notes and confirm audible output.
- Automate cutoff and master gain and confirm the editor reflects the changes.
- Save and reopen the host project and confirm the plugin state restores correctly.
- Confirm standalone-only device selectors and MIDI monitor are absent in the plugin editor.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers the VST3 smoke path and does not smuggle in deferred host CC remapping or extra plugin features.
- Trace host loading, automation, and state restore behavior back to the plugin requirements.
- Review regression risk around metadata, state serialization, and host compatibility.
- Confirm the chosen host and smoke-test steps are documented.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Validate VST3 metadata and bus layout in `Ableton Live Lite` on Windows.
- [ ] Verify host MIDI note input produces sound.
- [ ] Verify the plugin editor opens correctly in the host.
- [ ] Verify host automation works for cutoff and master gain.
- [ ] Verify plugin state saves and restores through the APVTS path.
- [ ] Document the local `Ableton Live Lite` VST3 smoke-test workflow.

### Exit criteria for moving items to `DONE.md`

- The VST3 artifact loads in `Ableton Live Lite` on Windows and opens its editor.
- Host MIDI notes produce sound and host automation works for the required parameters.
- Saving and reopening the host project restores plugin parameter state correctly.
- No standalone-only device UI appears in the plugin editor.
- Manual host checks pass, docs are current, and review is complete.

## Phase 12: Standalone device persistence

### Goal

Persist the last valid standalone audio and MIDI device selections without mixing standalone settings into plugin state.

### Scope

- Persist the last valid audio backend, output device, sample rate, and buffer size.
- Persist the last valid MIDI input device.
- Restore available settings on restart and show unavailable state when saved devices are missing.
- Clear held-note state when the selected MIDI device disconnects during playback.

### Expected files to change

- `src/standalone/SettingsStore*.{h,cpp}`
- `src/standalone/AudioDevice*.{h,cpp}`
- `src/standalone/MidiInput*.{h,cpp}`
- `src/ui/StatusStrip*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 2`, `Phase 3`, and `Phase 4` must be complete.
- The separation between plugin parameter state and non-parameter runtime state must already be respected by `Phase 11`.
- This phase follows `Phase 11` intentionally so plugin-state boundaries are already validated before standalone persistence is added.

### Risks

- Medium risk: invalid persisted settings can cause confusing startup states or device-selection bugs.
- Medium risk: it is easy to accidentally leak standalone runtime settings into shared plugin-state code.
- Medium risk: disconnect handling can still leave stale held-note bookkeeping if only the UI is updated.

### Tests and checks to run

- `cmake --build build --config Debug`
- Select audio and MIDI devices, restart the standalone app, and confirm the selections restore when still available.
- Start the app with missing stored devices and confirm the app stays open and shows unavailable state.
- Disconnect the active MIDI device during playback and confirm held-note state is cleared.
- Change sample rate or buffer size and confirm the app remains stable after the reprepare path.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers standalone persistence and disconnect recovery behavior.
- Trace persistence fields and failure behavior back to the standalone state requirements.
- Review regression risk around state-boundary mistakes and missing-device startup behavior.
- Confirm the docs describe what is persisted and what is intentionally not persisted.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Persist the last valid standalone audio backend, output device, sample rate, and buffer size.
- [ ] Persist the last valid standalone MIDI input selection.
- [ ] Restore persisted standalone device settings when still available.
- [ ] Show unavailable state when saved devices are missing.
- [ ] Clear held-note state when the active MIDI device disconnects during playback.
- [ ] Verify standalone restart and missing-device behavior.

### Exit criteria for moving items to `DONE.md`

- The standalone app restores valid saved device selections on restart.
- Missing saved devices do not crash the app and are surfaced as unavailable.
- MIDI disconnect during playback clears held-note state.
- Plugin state and standalone runtime settings remain separated by code review.
- Manual persistence checks pass, docs are current, and review is complete.

## Phase 13: MIDI learn workflow

### Goal

Add a per-parameter MIDI learn workflow for CC-based continuous control mapping with clear mapping and persistence support.

### Scope

- Add a user-triggered learn path for continuous parameters only, exposed through a per-parameter context action on eligible controls.
- Capture CC messages for learned mappings and reject note-on and note-off events as continuous bindings.
- Add a clear-mapping action through the same per-parameter context action path.
- Persist learned mappings separately from synth parameter values.

### Expected files to change

- `src/midi/MidiLearn*.{h,cpp}`
- `src/midi/MidiMappingEngine.{h,cpp}`
- `src/standalone/SettingsStore*.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/Learn*.{h,cpp}` or editor sections carrying learn affordances
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 7` must be complete because MIDI learn extends the mapping engine rather than replacing it.
- `Phase 12` must be complete because mappings need a persistence path separate from synth state.
- The per-parameter context-action UX is fixed before implementation begins in this phase.

### Risks

- Medium-high risk: learned mappings can conflict with fixed defaults if precedence rules are unclear.
- Medium risk: accepting note events by mistake would violate a hard requirement.
- Medium risk: persistence can drift into preset or plugin state if boundaries are not explicit.

### Tests and checks to run

- `cmake --build build --config Debug`
- Enter learn mode for a continuous parameter and bind a CC by moving a hardware control.
- Clear an existing mapping and confirm it is removed.
- Restart the app and confirm learned mappings persist.
- Attempt to learn with note events and confirm the mapping is rejected.
- Review code to confirm learned mappings are stored separately from synth parameter values.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers MIDI learn and does not add broader controller-management features.
- Trace learn-mode behavior, CC-only capture, and persistence back to the MIDI learn requirements.
- Review regression risk around mapping precedence, rejected note events, and state separation.
- Confirm the docs explain how learn mode is entered, cleared, and persisted.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add per-parameter MIDI learn mode for continuous parameters.
- [ ] Capture only CC messages for learned mappings.
- [ ] Reject note events as continuous-parameter mappings.
- [ ] Add a clear-mapping action.
- [ ] Persist learned mappings separately from synth parameter state.
- [ ] Verify learned mappings survive an app restart.

### Exit criteria for moving items to `DONE.md`

- A user can enter learn mode, bind a CC, and clear the mapping.
- Note events cannot be learned as continuous parameter mappings.
- Learned mappings persist across app restart.
- Mapping persistence remains separate from synth parameter state.
- Manual learn-flow checks pass, docs are current, and review is complete.

## Phase 14: Patch save/load workflow

### Goal

Add init patch, save patch, and load patch support for synth parameter state without overwriting standalone device settings or learned mappings.

### Scope

- Add an explicit init-patch action that resets automatable synth parameters to defaults.
- Add patch save and patch load for synth parameter state only, using an XML wrapper around APVTS state serialization.
- Restore both sound and UI state from a saved patch.
- Keep standalone device settings and learned mappings outside patch files.

### Expected files to change

- `src/presets/PatchState*.{h,cpp}`
- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/ui/Patch*.{h,cpp}` or action controls in existing editor sections
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 11` must be complete because APVTS state save and restore is already validated in a host context.
- `Phase 12` and `Phase 13` should be complete first so state-boundary rules are already enforced before patch files are added.
- Stable parameter IDs from `Phase 1` must remain unchanged.

### Risks

- Medium risk: patch files can accidentally absorb standalone runtime or mapping state if boundaries are not enforced.
- Medium risk: future compatibility depends on parameter ID stability.
- Low risk: init-patch behavior is simple once state boundaries are clear.

### Tests and checks to run

- `cmake --build build --config Debug`
- Trigger init patch and confirm all automatable parameters return to defaults.
- Save a patch, change the sound, then load the patch and confirm sound and UI state restore.
- Confirm patch load does not overwrite standalone audio or MIDI selections.
- Confirm patch load does not overwrite learned mappings.

### Review check before moving work to `DONE.md`

- Confirm the phase only delivers patch-state workflows and does not expand into a full preset browser.
- Trace init, save, and load behavior back to the preset requirements.
- Review regression risk around state separation and parameter ID stability.
- Confirm the docs explain what patch files contain and what they do not contain.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add an init-patch action that resets automatable synth parameters to defaults.
- [ ] Add patch save for synth parameter state.
- [ ] Add patch load for synth parameter state.
- [ ] Restore UI state when a saved patch is loaded.
- [ ] Keep standalone device settings and learned mappings out of patch files.
- [ ] Verify patch save and load restore sound correctly.

### Exit criteria for moving items to `DONE.md`

- Init patch resets the automatable parameter set to defaults.
- Patch save and load restore the intended synth sound and UI state.
- Patch files do not overwrite standalone device settings or learned mappings.
- Manual patch-flow checks pass, docs are current, and review is complete.

## Phase 15: Windows CI build pipeline

### Goal

Add a reproducible Windows CI build that validates the project from a clean checkout.

### Scope

- Add a Windows CI workflow that configures and builds the project.
- Reuse the documented JUCE acquisition path rather than introducing a second bootstrap path.
- Run the standalone and VST3 build targets in CI.
- Publish logs sufficient to review build results; release artifact publishing is deferred beyond this phase.

### Expected files to change

- `.github/workflows/windows-build.yml`
- `CMakePresets.json` or `scripts/ci/*.ps1`
- `README.md`
- `TODO.md`
- `DONE.md`

### Dependencies

- `Phase 1` must be complete because CI depends on the documented clean-checkout build path.
- By plan order, this phase follows the major feature phases so the pipeline validates a near-complete project rather than a moving skeleton.
- The chosen JUCE acquisition method must work non-interactively in CI.

### Risks

- Low-medium risk: CI failures are usually environmental rather than architectural, but they can block delivery if the bootstrap path is brittle.
- Medium risk: Windows plugin builds can fail in CI if local-only assumptions leaked into the project.
- Low risk: workflow structure is straightforward once the local build path is stable.

### Tests and checks to run

- Run the CI workflow on a branch and confirm the Windows job succeeds.
- Confirm CI runs the documented configure and build commands from a clean checkout.
- Confirm standalone and VST3 targets are both built in CI.
- Review logs to confirm failures are actionable and not hidden by the workflow.

### Review check before moving work to `DONE.md`

- Confirm the phase only adds CI build validation and does not bundle unrelated code changes.
- Trace the workflow back to the build-tooling requirements.
- Review regression risk around hidden local dependencies and artifact paths.
- Confirm docs explain how CI relates to the local build flow.
- Confirm unfinished follow-up work is written back to `TODO.md`.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Add a Windows CI workflow for clean-checkout configure and build.
- [ ] Run standalone and VST3 targets in CI.
- [ ] Reuse the documented JUCE bootstrap path in CI.
- [ ] Publish enough CI logs to review build results without relying on release artifacts.
- [ ] Verify the CI workflow succeeds on a branch.
- [ ] Document CI expectations in `README.md`.

### Exit criteria for moving items to `DONE.md`

- A Windows CI workflow exists in the repository and runs the project build from a clean checkout.
- CI builds both standalone and VST3 targets successfully.
- The workflow does not rely on undocumented local machine state.
- CI documentation is current and review is complete.

## Phase 16: Final stabilization and release review

### Goal

Close the project in a fully reviewed, validated, and documented state that matches the actual delivered behavior.

### Scope

- Run the full manual validation matrix from `REQUIREMENTS.md` across standalone and VST3 behavior.
- Fix only release-blocking defects or documentation mismatches discovered during stabilization.
- Reconcile `IMPLEMENTATION_PLAN.md`, `TODO.md`, `DONE.md`, `README.md`, `REQUIREMENTS.md`, and `DESIGN.md` with shipped behavior.
- Ensure any remaining deferred work stays explicitly deferred rather than partially implemented.

### Expected files to change

- `TODO.md`
- `DONE.md`
- `README.md`
- `IMPLEMENTATION_PLAN.md`
- `REQUIREMENTS.md`
- `DESIGN.md`
- Any small release-blocking fix under `src/**` discovered during stabilization

### Dependencies

- All earlier phases must be complete.
- CI from `Phase 15` should already be green so stabilization is not compensating for an unstable build pipeline.
- The validation matrix in `REQUIREMENTS.md` must already be current before this phase begins.

### Risks

- Medium risk: hidden regressions or doc drift can surface late.
- High risk: stabilization can become an unbounded polish phase if new features are allowed in.
- Low risk: documentation reconciliation is straightforward when scope is held firmly.

### Tests and checks to run

- `cmake --build build --config Debug`
- `cmake --build build --config Release`
- Run the full standalone manual validation matrix from `REQUIREMENTS.md`.
- Re-run the VST3 host smoke checks.
- Confirm the Windows CI workflow is green.
- Review build logs to confirm there are no accepted new warnings.

### Review check before moving work to `DONE.md`

- Confirm the phase only closes release-blocking gaps and aligns docs; no new features are allowed here.
- Trace every stabilization fix back to a failing validation step or a documented mismatch.
- Review regression risk around any late code changes.
- Confirm all authoritative docs reflect actual delivered behavior.
- Confirm every unfinished item is either written to `TODO.md` or explicitly marked deferred in the docs.
- Reviewer must explicitly confirm that the phase outcome matches the stated goal and scope.

### Exact `TODO.md` entries to refresh from this phase

- [ ] Run the full manual validation matrix and capture the results.
- [ ] Fix any release-blocking defects found during stabilization.
- [ ] Reconcile `README.md`, `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` with shipped behavior.
- [ ] Confirm every unfinished item is either in `TODO.md` or explicitly deferred.
- [ ] Verify the Windows CI workflow is green at the release candidate state.
- [ ] Prepare the final release review summary for the local standalone and VST3 artifacts.

### Exit criteria for moving items to `DONE.md`

- The full validation matrix passes or any remaining gaps are explicitly documented as deferred and accepted.
- Any defects fixed in stabilization are release-blocking fixes only and are traceable to failing validation.
- `TODO.md` contains only genuine remaining work, and `DONE.md` contains only verified completed work.
- All authoritative docs match shipped behavior.
- Build, host smoke, and CI checks pass, and review is complete.

# Dependency notes

- `Phase 1` is the root phase. All code phases depend on the build skeleton and stable parameter IDs created there.
- `Phase 2` and `Phase 3` are the standalone bring-up base. They must exist before audible synth work because audio and MIDI shell behavior are acceptance requirements in their own right.
- `Phase 4` and `Phase 5` form the core playable vertical slice. They create the minimum shared processor, synth-engine, and UI behavior needed before controller-specific work.
- `Phase 6` is intentionally isolated from `Phase 7` because hardware-message uncertainty is a real delivery risk. If the controller is unavailable, only controller-mapping work should block, not the rest of the synth bring-up.
- `Phase 8` and `Phase 9` stay separate even though both are DSP phases. They touch different risk surfaces and should not share one review cycle.
- `Phase 10` depends on completed UI and effect surfaces so it stays a layout phase, not a hidden feature phase.
- `Phase 11` depends on parameter IDs remaining stable from `Phase 1` onward. Any parameter ID drift before host validation should be treated as a plan-level change and documented immediately.
- `Phase 12` follows `Phase 11` by plan order because standalone persistence must not be confused with plugin state. The VST3 state boundary should be validated first.
- `Phase 13` depends on the fixed mapping engine from `Phase 7` and the standalone persistence path from `Phase 12`.
- `Phase 14` depends on validated APVTS state handling from `Phase 11` and the state-separation work from `Phase 12` and `Phase 13`.
- `Phase 15` could technically be started earlier, but the plan places it late so the CI workflow stabilizes around a near-final project rather than churning with every early architectural decision.
- `Phase 16` is the only phase allowed to touch multiple areas at once, and even then only for release-blocking fixes or documentation alignment.

# Review policy

- Expected review size is one phase per review cycle.
- A phase must be split before implementation starts if it has more than one primary deliverable, requires more than one independent acceptance demo, or cannot be validated with one focused build and test pass.
- A phase must also be split before implementation starts if it mixes any two of the following without a hard dependency: new DSP behavior, infrastructure changes, persistence changes, VST3 host changes, or UI refinement.
- Oversized phases are not allowed to proceed unchanged. `IMPLEMENTATION_PLAN.md` must be updated first, then `TODO.md` must be refreshed from the smaller replacement phases.
- Review evidence for each phase must include the completed checks, requirement traceability, regression-risk review, documentation update confirmation, scope-creep review, and confirmation that unfinished work was written back to `TODO.md`.
- No item moves to `DONE.md` until the reviewer confirms that the delivered outcome matches the stated phase goal and scope.

# Definition of done for the plan

The overall project is done when all planned phases are complete and the delivered behavior matches the accepted scope in `REQUIREMENTS.md` and `DESIGN.md`.

- The repository builds from a clean Windows checkout with the documented CMake and JUCE workflow.
- The shared codebase produces a standalone artifact and a VST3 artifact.
- The standalone app provides the required audio shell, MIDI shell, MIDI monitor, first-release synth behavior, fixed MiniLab 3 mapping, hardware-style UI, panic action, graceful no-device handling, and standalone persistence.
- The VST3 plugin loads in at least one Windows host, accepts host MIDI notes, exposes the required parameters, responds to automation for the required controls, and restores state correctly.
- The deferred later milestones that are still part of this project plan are complete: MIDI learn, patch save and load, and Windows CI.
- The manual validation matrix passes, or any accepted exceptions are explicitly documented.
- `README.md`, `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, `TODO.md`, and `DONE.md` are all aligned with the shipped behavior.
- No new warnings introduced by the project are left unresolved.
- There is no hidden unfinished core work inside a so-called polish pass; any remaining work is explicitly tracked in `TODO.md` or explicitly deferred in the docs.

# Open questions

## Blocking unknowns

- None. Planning is now fixed to use a JUCE git submodule, `Ableton Live Lite` for the VST3 smoke target, an available MiniLab 3 for message capture, and plugin metadata `CoolSynth` / `tomcoolpxl` with codes `Tcpx` and `Csyn`.

## Non-blocking questions

- None at the planning level. The remaining implementation defaults are fixed as follows unless implementation evidence forces a documented change: `Visual Studio 17 2022` as the default local generator, silent audio bring-up in `Phase 2`, MiniLab mapping recorded in both code and docs, per-parameter context actions for MIDI learn, XML-wrapped APVTS patch files, and build-only CI without release artifact publishing.
