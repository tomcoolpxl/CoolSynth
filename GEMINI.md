# CoolSynth Project Rules

This is the main repo or agent memory file. If you need to adapt your memory file, adapt this file.

We are in "V2" phase right now.

Authoritative project files:

- `REQUIREMENTS.md`, `REQUIREMENTS_V2.md`
- `DESIGN.md`, `DESIGN_V2.md`
- `IMPLEMENTATION_PLAN.md`, `IMPLEMENTATION_PLAN_V2.md`
- `TODO.md`
- `DONE.md`

Project rules:

- Keep one `TODO.md` item small enough for one review cycle.
- Refresh `TODO.md` from the current phase in `IMPLEMENTATION_PLAN.md`.
- Update `TODO.md` before starting a new implementation chunk.
- Update `TODO.md` and `DONE.md` after implementation.
- Move an item to `DONE.md` only after the required checks, review, and doc updates are complete.
- `DONE.md` holds only verified work.
- Update `REQUIREMENTS.md` and `DESIGN.md` when scope or acceptance criteria change.
- Update `IMPLEMENTATION_PLAN.md` when the order or grouping of work changes.
- For narrow tasks, pass the exact authoritative files in the prompt instead of retyping context.
- Ask before making a large refactor, changing the directory structure, or removing tests.
- Before moving work to `DONE.md`, review the diff, run the required checks, and update docs if the change affected scope or structure.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

## Current Project State

- V2 `Phase 5` is complete: the editor has been redesigned into a 1400x600 single-page V2 instrument panel accommodating all 67 knobs and 1 fader for the new parameters, without paging. Standalone UI separation remains intact.
- `Phase 15` is complete: the repository now has a manual-only Windows validation workflow and a tag-only Windows release workflow backed by `scripts/ci/*.ps1` and isolated CI CMake presets.
- Live proof completed on GitHub on 2026-05-09:
  - Manual validation run `25614177697` passed and produced diagnostics plus packaged artifacts.
  - Release run `25614334076` passed, published a disposable prerelease, and a rerun updated that same release rather than creating a duplicate.
- The workflow action pins now target the Node 24-compatible lines:
  - `actions/checkout` pinned to `v6.0.2`
  - `actions/upload-artifact` pinned to `v7.0.0`
  - `actions/download-artifact` pinned to `v8.0.1`
  - `softprops/action-gh-release` pinned to `v3`
- Node-runtime validation completed on 2026-05-10:
  - Manual validation run `25614878512` passed with the updated action pins and no Node 20 deprecation annotation.
  - Release run `25625463636` passed with the updated release workflow pins and no Node 20 deprecation annotation.
- Late V2 doc decisions fixed on 2026-05-14:
  - V2 ships with 8 voices by default.
  - A selectable 5-voice vintage-limited mode is not required for the first V2 release.
  - V2 keeps the `.cspatch` extension while intentionally breaking V1 patch/state compatibility.
  - First-release VST3 manual sign-off uses both Ableton Live Lite and REAPER on Windows.
  - The UI target is a one-page instrument panel first, with paging only as a fallback.
  - Aftertouch is a late V2 addition only if it remains straightforward after the main MIDI/controller path is stable.
- V2 `Phase 1` completed on 2026-05-14:
  - The APVTS surface now exposes the full grouped V2 parameter contract across oscillators, mixer, filter, dual envelopes, LFO, Poly Mod, performance, arp, drive, chorus, delay, reverb, and output.
  - `SynthAudioProcessor` now decodes a full V2 block snapshot and renders through a new `SynthEngineV2` seam that still delegates audible output to the current legacy engine path.
  - The current shared editor subset and bundled MiniLab 3 profile were retargeted from the old global `waveform` parameter to the new V2 `oscAWave` parameter so the shell remains usable during staged migration.
  - Added test coverage for V2 parameter-ID uniqueness and V2 parameter-state round-trip/sanitized restore behavior.
  - Local verification passed with `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- V2 `Phase 2` is implemented, manually smoke-tested, and closed on 2026-05-14:
  - `SynthAudioProcessor::processBlock()` now converts MIDI into explicit sample-offset V2 engine events for note on/off, pitch bend, mod wheel, and sustain, then renders through a real `SynthEngineV2` event/allocation path instead of the old JUCE `Synthesiser` dispatch.
  - `SynthEngineV2` now owns deterministic preallocated voice slots with release-first voice stealing, sustain handling, panic reset, and sample-accurate in-block event timing while keeping the audible voice intentionally simple for Phase 2.
  - The old callback-lock-dependent live mapped-action path was removed from runtime MIDI handling. Plugin CC translation now happens off the audio thread through a raw controller-event bridge, and standalone/controller-profile translation no longer blocks the processor callback.
  - Host-safety MIDI handling is now explicit on the V2 path: `All Notes Off`, `All Sound Off`, and `Reset All Controllers` feed the engine instead of being ignored, and those controller numbers remain reserved across MIDI learn, runtime mapping, and plugin queued-CC dispatch.
  - Host lifecycle behavior is now tighter on the V2 plugin path: `SynthAudioProcessor::reset()` explicitly panics the allocator, clears delay tail state, and resets keyboard state, and the plugin no longer reports a hard-coded zero tail while the active V2 delay can still ring.
  - Added automated regressions for V2 event timing, sustain release, panic, release-first stealing, oldest-held fallback stealing, processor-level note rendering, reserved-performance-control learn rejection, host-safety controller rejection, plugin controller FIFO wraparound, processor reset silence, and delay-aware tail reporting.
  - Windows host availability is no longer a blocker for Phase 2 sign-off: `CoolSynth.exe` launches, Ableton Live 12 Lite scans the custom CoolSynth VST3 path and persists `CoolSynth` in `Live-plugins-1.db`, and REAPER was installed from the official installer on 2026-05-14 and now persists `CoolSynth.vst3` in `reaper-vstplugins64.ini` and `reaper-fxtags.ini`.
  - Manual smoke for the current allocator path passed in standalone and Ableton Live 12 Lite on 2026-05-14. Standalone note on or off, repeated notes, and panic passed; Ableton VST3 note playback, repeated notes, panic, and no stuck-note behavior passed.
  - Standalone sustain was not directly exercised because no CC64 sustain-pedal source was available during manual smoke. Automated sustain-path regressions remain in place, and the user accepted Phase 2 closure without a separate standalone sustain-pedal pass.
  - A small dense note-start click was observed during large simultaneous standalone chords under aggressive settings, but it disappeared with safer knob settings and is currently treated as a patch-dependent transient rather than an allocator or host-lifecycle fault.
  - Build identity is now visible in both the standalone status bar and a dedicated plugin footer strip so manual host checks can confirm the tested standalone and VST3 binaries match.
  - Local verification passed with `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- V2 `Phase 3` has a verified implementation slice on 2026-05-14, but it is not fully closed yet because manual standalone dry-tone audition remains open in `TODO.md`:
  - `SynthVoice` now renders a true dual-oscillator plus noise source path with pulse, triangle, and saw wave shapes; per-oscillator octave and fine tuning; per-oscillator pulse width; oscillator A hard sync to oscillator B; oscillator B low-frequency mode support; and bounded pre-filter overload gain staging ahead of the existing temporary filter path.
  - The previously observed dense note-start click is now directly tracked as dry voice-core work and mitigated in code through a short de-click ramp plus per-voice randomized start phase, without changing allocator timing or host event offsets.
  - Added automated regressions for pulse-width limits, sync-enabled render divergence, dual-oscillator detune divergence, dense note-start transient containment, and full-mixer stability.
  - Local verification passed with `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- V2 `Phase 6` completed on 2026-05-15:
  - Voice rendering now applies global LFO (sine/triangle/square), Poly Mod (Osc B and filter envelope to pitch, pulse width, filter cutoff), pitch bend, mod wheel, per-voice vintage drift, glide, and velocity-to-amp/filter modulation per sample. The cutoff smoother smooths the base cutoff per block while modulation acts directly per sample.
  - `SynthEngineV2` now tracks `globalLfoPhase` and advances it across in-block event spans, broadcasts modulation parameters and the current mod-wheel value to all voices each render span, maintains a bounded held-note list, and supports poly/mono/unison play modes with Last/Low/High key priority.
  - Mono mode retriggers from the held-note set on note-off using key priority; unison stacks all voices on the same note with deterministic vintage detune and balance-style pan spread.
  - The voice's L/R pan is balance-style (1.0 at center, 0 at full opposite side) rather than equal-power, which preserves the prior dry-tone loudness with no spread while still producing real stereo motion under `panSpread > 0`.
  - Added `V2Performance` test suite covering bend range, mono priority, mono low-priority fallback, unison stack count, glide rendering divergence, vintage variance bounds, and mod-wheel-driven LFO.
  - Editor wiring was verified intact — all LFO/Poly Mod/Performance knobs already bind to the right V2 parameter IDs and lay out in the existing one-page panel.
  - Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.
- `TODO.md` now points to Phase 7.
