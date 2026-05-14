# TODO

`Phase 1` — V2 parameter contract and processor seam

- [ ] Add the stable V2 parameter IDs and grouped parameter definitions required by `REQUIREMENTS_V2.md`.
- [ ] Rebuild the APVTS layout around the V2 oscillator, mixer, filter, envelope, modulation, performance, arp, FX, and output groups.
- [ ] Extend processor-side raw-parameter binding and block snapshot decoding for the V2 surface.
- [ ] Introduce a V2 engine seam in `SynthAudioProcessor` without deleting the current engine path yet.
- [ ] Add or update tests that verify V2 parameter uniqueness, defaults, and serializable APVTS state.
- [ ] Verify Debug configure, build, and `ctest` pass after the V2 parameter-surface change.

`Phase 2` — Custom allocator with basic V2 note playback

- [ ] Add explicit sample-offset engine event records for note, bend, mod wheel, and sustain input.
- [ ] Implement deterministic V2 note allocation with sustain, panic, and musically defensible voice stealing.
- [ ] Route `SynthAudioProcessor::processBlock()` through the new V2 event path and render contract.
- [ ] Replace callback-lock-dependent live mapped-action handling with a non-blocking control bridge compatible with the new engine path.
- [ ] Add tests for in-block event ordering, sustain behavior, panic, and voice stealing.
- [ ] Verify standalone and plugin basic note playback on the new allocator path.

`Phase 3` — Dual-oscillator mixer voice core

- [ ] Implement oscillator A controls and waveform behavior required by the V2 parameter surface.
- [ ] Implement oscillator B controls, low-frequency mode, and sync-support behavior required by the V2 parameter surface.
- [ ] Add per-voice noise generation and explicit oscillator/noise mixer levels.
- [ ] Add bounded pre-filter overload or gain staging that supports the target dry tone without destabilizing render.
- [ ] Add tests or render assertions for pulse-width limits, sync reset behavior, detune behavior, and mixer stability.
- [ ] Verify dry dual-oscillator and noise-mixed tones in standalone playback.

`Phase 4` — Filter and dual-envelope tone path

- [ ] Implement dedicated filter ADSR and amp ADSR per voice.
- [ ] Implement the V2 resonant low-pass filter with cutoff, resonance, filter-envelope amount, and keyboard tracking.
- [ ] Wire envelope retrigger and voice-steal reset behavior into repeated-note and stolen-voice cases.
- [ ] Add tests for filter stability across supported sample rates and for envelope restart behavior.
- [ ] Manually verify init-patch bass, brass, pluck, and pad programming on the new dry voice path.
- [ ] Verify Debug build and `ctest` still pass after the filter/envelope cutover.

`Phase 5` — Performance modulation and play modes

- [ ] Add the global LFO waveforms, rate control, and routed depth controls required by the V2 parameter contract.
- [ ] Implement constrained Poly Mod sources and destinations for oscillator pitch, pulse width, and filter cutoff.
- [ ] Add pitch bend and mod wheel handling in the engine performance state.
- [ ] Implement glide, mono, unison, and key-priority behavior in the allocator.
- [ ] Add bounded vintage/slop and pan-spread behavior with deterministic testable state.
- [ ] Add tests for bend range, mono priority, unison stack count, glide behavior, and vintage variance bounds.

`Phase 6` — Host-aware arpeggiator

- [ ] Add held-note, latched-note, pattern, octave, and gate state for the V2 arpeggiator.
- [ ] Read plugin transport and tempo in `processBlock()` and pass a transport snapshot into the engine.
- [ ] Implement deterministic internal-rate fallback behavior for missing host timing.
- [ ] Route arp-generated note events through the allocator with sample offsets.
- [ ] Add tests for pattern ordering, latch behavior, gate timing, and tempo fallback.
- [ ] Verify standalone and Ableton Live Lite VST3 arp behavior manually before final multi-host release validation.

`Phase 7` — Global V2 effects rack

- [ ] Implement the global drive stage for the V2 output path.
- [ ] Implement the global chorus or ensemble stage for the V2 output path.
- [ ] Rework the existing delay into the fixed-order V2 FX rack.
- [ ] Implement the global reverb stage and output gain handling for the V2 rack.
- [ ] Add reset, bypass, mix, and tail-handling behavior for the full rack.
- [ ] Add tests or render checks for bounded feedback, stable reset behavior, and no obvious dry-path regression.

`Phase 8` — V2 editor and panel workflow

- [ ] Redesign `SynthAudioProcessorEditor` as a one-page grouped V2 instrument panel first.
- [ ] Add V2 control groups for oscillators, mixer, filter, filter envelope, amp envelope, modulation, performance, arp, FX, and output.
- [ ] Rebind editor attachments, value displays, and labels to the V2 parameter contract.
- [ ] Preserve standalone-only status, settings, and monitor surfaces outside the plugin editor.
- [ ] Manually verify init-patch programming flow and plugin automation feedback in the V2 editor.
- [ ] Verify Debug build and `ctest` still pass after the editor cutover.

`Phase 9` — MIDI learn and controller integration for the V2 surface

- [ ] Update the bundled controller profile data and runtime mapping logic for the V2 parameter surface.
- [ ] Preserve notes, pitch bend, mod wheel, and sustain as reserved performance inputs outside generic MIDI learn.
- [ ] Extend standalone and plugin MIDI learn to the V2 controls.
- [ ] Persist and restore V2 learned mappings in standalone settings and plugin state.
- [ ] Add channel aftertouch only if it fits the stabilized MIDI/controller path without broader redesign.
- [ ] Add tests for learned-CC round-trip, controller-profile override precedence, and disconnect handling.
- [ ] Verify controller workflows manually in standalone and plugin mode.

`Phase 10` — V2 patch/state boundary and compatibility handling

- [ ] Define the V2 processor-state version and patch-format boundary for the new parameter contract.
- [ ] Update patch save and load logic to round-trip only V2 parameter state.
- [ ] Reject incompatible V1 patch or processor-state payloads explicitly unless an import path is later approved.
- [ ] Add tests for V2 patch round-trip, malformed payload rejection, and V1 compatibility-boundary behavior.
- [ ] Update `README.md` and patch workflow documentation to describe the V2 compatibility break.
- [ ] Verify saved V2 patches reload identically in standalone and plugin workflows.

`Phase 11` — Stabilization, release validation, and documentation reconciliation

- [ ] Run the full local Debug and Release build and test matrix for V2.
- [ ] Fix any release-blocking defects found during full-matrix validation.
- [ ] Run the standalone and VST3 manual smoke workflows and capture the results.
- [ ] Run the GitHub `Windows Manual Validation` workflow and review diagnostics and packaged artifacts.
- [ ] Reconcile `README.md`, `REQUIREMENTS_V2.md`, `DESIGN_V2.md`, `IMPLEMENTATION_PLAN_V2.md`, `TODO.md`, and `DONE.md` with shipped V2 behavior.
- [ ] Confirm every unfinished item is explicitly deferred in `TODO.md` before closing the release review.
