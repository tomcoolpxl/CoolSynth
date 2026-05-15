# CoolSynth Project Rules

This is the main repo or agent memory file. If you need to adapt your memory file, adapt this file.

We are in "V2" phase right now.

## Compatibility Stance

- No backward compatibility is required anywhere in this repo unless the user explicitly asks for it.
- Nothing has been released yet, so prefer the cleanest current V2 behavior over preserving older patch, state, parameter-ID, or controller-mapping contracts.
- If a future task would keep backward compatibility only out of caution or inertia, do not do that by default.

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
- In this workspace, keep terminal commands profile-free (`login: false` or PowerShell `-NoProfile`) to avoid irrelevant profile-side access-denied noise in the transcript.

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
  - `SynthVoice` now renders a true dual-oscillator plus noise source path with pulse, triangle, saw, and later sine wave shapes; per-oscillator octave and fine tuning; per-oscillator pulse width; oscillator A hard sync to oscillator B; oscillator B low-frequency mode support; and bounded pre-filter overload gain staging ahead of the existing temporary filter path.
  - The previously observed dense note-start click is now directly tracked as dry voice-core work and mitigated in code through a short de-click ramp plus per-voice randomized start phase, without changing allocator timing or host event offsets.
  - Added automated regressions for pulse-width limits, sync-enabled render divergence, dual-oscillator detune divergence, dense note-start transient containment, and full-mixer stability.
  - Local verification passed with `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- Late V2 oscillator scope update landed on 2026-05-15:
  - V2 now exposes a real sine oscillator option again on both oscillator wave selectors without changing the dual-oscillator architecture or adding new controls.
  - The oscillator wave enum, APVTS choice lists, processor-side waveform decoding, editor segmented controls, and legacy V1 sine-mapping seam were all updated consistently.
  - Added a live-engine regression that confirms the sine wave path renders audibly and remains finite and bounded.
- V2 `Phase 6` completed on 2026-05-15:
  - Voice rendering now applies global LFO (sine/triangle/square), Poly Mod (Osc B and filter envelope to pitch, pulse width, filter cutoff), pitch bend, mod wheel, per-voice vintage drift, glide, and velocity-to-amp/filter modulation per sample. The cutoff smoother smooths the base cutoff per block while modulation acts directly per sample.
  - `SynthEngineV2` now tracks `globalLfoPhase` and advances it across in-block event spans, broadcasts modulation parameters and the current mod-wheel value to all voices each render span, maintains a bounded held-note list, and supports poly/mono/unison play modes with Last/Low/High key priority.
  - Mono mode retriggers from the held-note set on note-off using key priority; unison stacks all voices on the same note with deterministic vintage detune and balance-style pan spread.
  - The voice's L/R pan is balance-style (1.0 at center, 0 at full opposite side) rather than equal-power, which preserves the prior dry-tone loudness with no spread while still producing real stereo motion under `panSpread > 0`.
  - Added `V2Performance` test suite covering bend range, mono priority, mono low-priority fallback, unison stack count, glide rendering divergence, vintage variance bounds, and mod-wheel-driven LFO.
  - Editor wiring was verified intact — all LFO/Poly Mod/Performance knobs already bind to the right V2 parameter IDs and lay out in the existing one-page panel.
  - Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.
- V2 `Phase 7` completed on 2026-05-15:
  - `SynthEngineV2` now includes an internal arpeggiator that tracks held notes, latched notes, pattern order, octave cycling, gate timing, and pending cross-block note-offs without audio-thread allocation.
  - `SynthAudioProcessor::processBlock()` now reads plugin playhead tempo, PPQ, and transport state when available and passes a compact transport snapshot into the engine; when host timing is incomplete, the arp falls back to its internal tempo source instead of stalling.
  - Arp-generated note on/off events now route through the existing V2 allocator with sample offsets, so arp playback shares the same timing and voice-steal path as ordinary note play.
  - Added `V2Arpeggiator` regressions covering pattern ordering (`Up`, `Down`, `Up/Down`, `As Played`), octave range, latch behavior, gate timing, internal-tempo fallback, host-PPQ alignment, transport-stop release, arp-disable release, and panic clearing.
  - A Windows patch-save failure found during manual Phase 7 validation was fixed by closing the temporary output stream before replacing the destination file, and patch-state coverage now includes a real file write/read round trip.
  - Manual standalone arp validation passed on 2026-05-15 using a simplified dry test patch: internal tempo, pattern switching, octave range, gate length, latch behavior, patch save, and patch load all worked as expected.
  - Ableton Live Lite VST3 arp-host validation was intentionally deferred to the final release-validation phase by user request after host workflow issues during bring-up.
- V2 `Phase 8` completed on 2026-05-15:
  - The V2 render path now runs through a dedicated `GlobalFxRack` with the documented fixed order `drive -> chorus -> delay -> reverb -> master gain`.
  - The new drive stage is a bounded soft-saturation pass with mix control; the chorus stage is voiced as a compact ensemble-style widener using the existing V2 rate, depth, and mix parameters; the existing delay now lives inside the rack; and a new global reverb stage completes the first-release FX set.
  - Rack disable behavior is now explicit for the time-based stages: disabling delay or reverb clears buffered tails instead of keeping stale audio around for a later re-enable.
  - Host tail reporting is now rack-aware rather than delay-only, so one-shot delay tails, feedback delay tails, and reverb tails all contribute to a conservative nonzero tail estimate when audible.
  - Added automated regressions covering dry-path preservation with zero-mix FX, extreme full-rack boundedness, time-based effect disable clearing, processor reset silence with the full rack enabled, and rack-aware tail reporting.
  - Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.
  - Manual validation passed on 2026-05-15 in both standalone and VST3 host use: dry baseline, drive, chorus, delay, reverb, full-rack playback, effect disable clearing, panic silence, and stop/reset tail behavior all worked as expected.
- V2 `Phase 9` completed on 2026-05-15:
  - MIDI learn now accepts the full exposed V2 parameter surface, including discrete controls such as waveform, play mode, and arp toggles, while still rejecting reserved performance controllers such as mod wheel, sustain, all-notes-off, and all-sound-off semantics.
  - The bundled MiniLab 3 Arturia-mode factory profile was retargeted toward the V2 core surface: cutoff, resonance, filter envelope amount, oscillator B fine tune, amp ADSR, oscillator A and B levels, master gain, oscillator A wave, and panic.
  - Factory-profile precedence remains deliberate: learned bindings still shadow the active factory profile by target and by CC signature so user overrides win predictably in standalone and plugin workflows.
  - Patch actions are now visible in the VST3 editor as well as standalone, and the async patch file choosers are parented to the editor component so manual save or load validation can happen directly inside the plugin UI.
  - The built-in init patch defaults were retuned toward a more useful dry dual-saw baseline with a moderated cutoff, positive filter-envelope contour, slight oscillator B detune, and balanced levels while keeping FX disabled.
  - Channel aftertouch was evaluated for Phase 9 and intentionally deferred because V2 still has no explicit aftertouch destination or parameter contract; adding hidden aftertouch behavior here would broaden scope rather than stay low risk.
  - Added automated regressions for full-surface learn eligibility, discrete learned-binding XML and plugin-state round trip, factory-profile signature shadowing by learned bindings, updated factory-profile runtime mappings, and init-default reset expectations.
  - Manual validation passed in both standalone and VST3 use: the updated MiniLab 3 profile behaved as expected, learned CC overrides worked, plugin learned mappings restored correctly, VST3 patch buttons were visible, and patch save/load worked in the plugin workflow.
  - Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.
- V2 `Phase 10` completed on 2026-05-15:
  - Wrapped `.cspatch` files and wrapped processor state now use explicit V2 format version `2` boundaries while keeping the `.cspatch` extension.
  - Plugin state no longer accepts bare APVTS XML fallback; restore now requires the wrapped V2 processor-state root, expected state type, and a single matching parameter-state child.
  - Parameter-state application now requires a complete current V2 parameter set, which prevents partial overlap from older or malformed payloads from silently mutating only the still-matching controls.
  - Added automated regressions for legacy patch-version rejection, wrong-state-type rejection, incomplete and partial-overlap parameter-state rejection, and processor-state rejection for unwrapped or legacy wrapped payloads.
  - Refreshed a factory-profile regression to assert the intended soft-takeover behavior of the oscillator-level faders under the current V2 init defaults.
  - Manual validation passed in standalone and VST3 use: valid V2 patches reloaded identically, and legacy-version, bare-state, and partial-overlap payloads were rejected cleanly without mutating the active synth state.
  - Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.
- `TODO.md` now points to Phase 11.
- Phase 11 Track A completed on 2026-05-15:
  - `ProcessorScopeFifo` (heap-allocated lock-free FIFO) introduced in `src/plugin/ProcessorScopeFifo.h`. `SynthAudioProcessor::processBlock` writes the post-FX mono mix to `scopeFifo`; the visualizer pulls samples on its UI timer. The `getActiveEditor()` + `dynamic_cast<SynthAudioProcessorEditor*>` call is gone from the audio path (WI-A1).
  - `SignalChainVisualizer` no longer has a `static int fftWriteIdx` or `std::atomic<bool> nextFFTBlockReady`; FFT scratch is a per-instance `std::vector<float>` filled on the UI thread only, so two plugin instances have independent spectra (WI-A2).
  - `PluginMappedActionDispatcher` (`juce::Thread` calling host gesture APIs off the message thread) replaced by `PluginMappedActionAsyncBridge` (`juce::AsyncUpdater`). `triggerAsyncUpdate()` is the only audio-thread call; dispatch happens on the message thread (WI-A3).
  - Dropped-event counters added to both controller event enqueue paths; `V2ScopeFifoTests` added (WI-A4).
  - `MidiMappingEngine::translate()` locking contract documented in the header (WI-A5).
  - Local verification passed: `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, `ctest --test-dir build -C Debug --output-on-failure`.
