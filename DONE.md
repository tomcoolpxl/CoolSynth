# DONE

## Phase 11 Track D — Code hygiene and dead code

Completed on 2026-05-16. All eight WIs landed, builds clean, all tests pass.

- [x] **WI-D1**: `SynthEngine.h/.cpp` deleted; `WaveformChoice` enum removed from `ParameterIDs.h`; `SynthVoice::setWaveform(WaveformChoice)` declaration and implementation removed; V1-only test include removed.
- [x] **WI-D2**: All parameter version hints flipped to V2 (`makeLegacyParameterId` → `makeV2ParameterId` for filter/amp-ADSR/delay/master-gain groups); `makeLegacyParameterId` helper, `legacyVersionHint` constant, and `JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING` compile definition deleted.
- [x] **WI-D3**: Eight magic numbers in `SynthVoice.cpp` hoisted to named anon-namespace `inline constexpr` constants. Duplicate `lfoSubRateSamples` in `SynthParameters.h` removed. Both `0.85f` literals in `SynthAudioProcessor.cpp` replaced with `GlobalDelay::maxFeedback`.
- [x] **WI-D4**: `src/midi/MidiToControllerEvent.h` added with canonical `coolsynth::midi::toControllerMidiEvent()`. `makeControllerMidiEvent` in `SynthAudioProcessor.cpp` and inline translation block in `StandaloneMidiInput.cpp` both removed and replaced with calls to the shared translator.
- [x] **WI-D5**: `SynthAudioProcessorEditor` constructor reduced from ~773 lines to ~18 lines via 8 private helper methods. Tooltip helpers and all tooltip assignments extracted to `EditorTooltips.cpp`. `EditorTooltips.cpp` added to both main and test `target_sources` in `CMakeLists.txt`.
- [x] **WI-D6**: `kParamPtrBindings` table (65-entry `inline const std::array<std::pair<const char*, ParamMemberPtr>, …>`) added to `SynthParameters.h`; `bindParameterPointers` body reduced to 4 lines; `static_assert` enforces exhaustiveness at compile time.
- [x] **WI-D7**: ~40 redundant APVTS-enforced `jlimit` calls removed from `makeBlockRenderParameters`. Retained: envelope-time `jmax` floors, delay time `jlimit(1, 1000)`, delay feedback cap.
- [x] **WI-D8**: `SignalChainVisualizer::paneWidth()` added (`(getWidth() - 36) / 5.0f`). Three inconsistent pane-width expressions in `timerCallback()` and `updateIdealWaveforms()` replaced with `paneWidth()`.

## Phase 11 Track C — Performance optimisations

Completed on 2026-05-16. All six WIs landed, builds clean, all tests pass.

- [x] **WI-C4**: Hoisted `std::tanh(preGain)` normalizer out of `applyDriveShape` inner sample loop in `GlobalFxRack`. Changed from recomputing per sample to computing once per block; `applyDriveShape` now takes the pre-computed `normalizer` as a parameter.
- [x] **WI-C1+C2**: Refactored `SynthVoice::renderNextBlock` per-sample pitch-ratio computation. Pre-loop: replaced three `std::pow(2.0f, …)` (bend, vintage, key-tracking) with `std::exp2` and combined bend+vintage into a single `voicePitchCarrierRatio` constant, initialized glide as a multiplicative running state (`currentGlideRatio * glideStepRatioPerSample` per sample instead of `pow` per sample). Per-sample: replaced the two remaining `std::pow(2.0f, …)` (LFO pitch and osc A total pitch) with `std::exp2`, folding LFO and polyMod semitones together before the single exp2 for osc A. `computeOscillatorFrequencyHz` also switched to `std::exp2`. Zero `std::pow(2.0f, …)` hits remain in any `.cpp` under `src/`.
- [x] **WI-C3**: Sub-rate LFO with linear interpolation. `lfoSubRateSamples = 32` constant added to `SynthParameters.h`. LFO wave is evaluated once per 32 samples and linearly interpolated between updates, saving a `sin`/wave call per sample in the inner voice loop.
- [x] **WI-C5**: Added `juce::SmoothedValue` FX wet/dry smoothing to `GlobalFxRack`. Drive mix is smoothed per sample using a pre-computed `mixRampScratch`. Chorus and reverb use a dry-buffer approach: the JUCE DSP object is run at full wet, then the output is blended back with the original dry signal using a per-sample smoothed mix coefficient. When `enabled` turns false the smoother snaps immediately (no ramp), preserving the original clear-on-disable behavior. `dryBuffer` and `mixRampScratch` are pre-sized in `prepare()` — no runtime allocations.
- [x] **WI-C6**: Cached `AudioParameterChoice*` pointers for 8 choice parameters (`oscAWave`, `oscBWave`, `filterKeyTracking`, `lfoWave`, `playMode`, `keyPriority`, `arpRateDivision`, `arpPattern`) in a new `ChoiceParameterPointers` struct on `SynthAudioProcessor`. `makeBlockRenderParameters` now calls `getIndex()` directly instead of `std::round(float)` + `jlimit`. Removed the 7 now-unused `decodeXxx` helper functions from `SynthAudioProcessor.cpp`.

## Phase 11 Track B — Audio quality and DSP correctness

Completed on 2026-05-15. All six WIs landed in a single commit, builds clean, all tests pass.

- [x] **WI-B1**: Replaced naive aliased oscillators (saw, pulse, triangle) with PolyBLEP band-limited rendering. Triangle uses a per-voice leaky integrator (`triState`) to produce DC-stable output from an integrated bandlimited square wave. `OscillatorState` gains `phaseIncrement` (cached for PolyBLEP) and `triState` fields. `renderWaveSample` renamed to `renderNaiveWaveSample` (still used by LFO); new `renderBandlimitedWaveSample` and `polyBlep` helpers live in an anonymous namespace.
- [x] **WI-B2**: Hard-sync discontinuity correction. When oscB resets oscA mid-sample, a sub-sample fractional time `bWrapFrac` is computed and an additive PolyBLEP correction is applied to oscA output to reduce the sync alias spike.
- [x] **WI-B3**: Clamped cutoff-mod sum (env + LFO + poly-mod + velocity) to ±120 semitones before exponentiation. Replaced `std::pow(2.0f, …)` with `std::exp2`. Prevents silent overflow to 524288× at extreme settings.
- [x] **WI-B4**: Resonance input-gain compensation. `filterInputGain = min(1, 1/√Q)` applied to the voice sample before `StateVariableTPTFilter::processSample`. At Q=25 this is −14 dB, bounding the resonant peak amplitude; at Q≤1 the gain is 1.0 (no attenuation).
- [x] **WI-B5**: Per-voice-count master gain scaling. `applyMasterGain` now counts active voices and applies `1/√(activeCount)` instead of the hardcoded `0.35` scalar. Single notes play at full level; 8-voice unison yields ≈ 0.354 (same as before).
- [x] **WI-B6**: Removed unison vintage floor. `jmax(vintageAmount, 0.35f)` → `vintageAmount` in `allocateUnisonNote`. Vintage=0 now produces clean phase-aligned unison voices; the hidden 0.35-cent drift floor is gone.
- [x] Added `V2AudioQualityTests` class (9 tests) to `tests/StabilityAndDisconnectTests.cpp`: `cutoff_mod_clamp_finite`, `filter_stability_q25`, `filter_loudness_vs_Q`, `unison_vintage_zero_produces_valid_audio`, `aliasing_A4_saw`, `aliasing_A7_saw`, `triangle_no_dc`, `hard_sync_alias_bounded`, `polyphony_loudness_consistency`. All pass.

## Phase 11 Track A — Critical RT-safety fixes

Completed on 2026-05-15. All five WIs landed, builds clean, all tests pass.

- [x] **WI-A1**: Introduced `ProcessorScopeFifo` (lock-free `AbstractFifo`-backed ring buffer, heap-allocated). `SynthAudioProcessor::processBlock` now writes the post-FX mono mix to `scopeFifo` instead of dynamic-casting to the editor on the audio thread. `SignalChainVisualizer` pulls samples from the FIFO on its 30 Hz UI timer. `getActiveEditor()` and `dynamic_cast<SynthAudioProcessorEditor*>` are gone from the audio path.
- [x] **WI-A2**: Removed the `static int fftWriteIdx` (shared across plugin instances) and the `std::atomic<bool> nextFFTBlockReady` race from `SignalChainVisualizer`. FFT scratch is now a `std::vector<float>` filled on the UI thread only; two plugin instances have fully independent spectra.
- [x] **WI-A3**: Replaced `PluginMappedActionDispatcher` (a `juce::Thread` calling host gesture APIs off the message thread) with `PluginMappedActionAsyncBridge` (`juce::AsyncUpdater`). `enqueuePluginMappedControllerEvent` now calls `triggerAsyncUpdate()` (RT-safe atomic exchange) and dispatch happens on the message thread. Added `flushMappedControllerEventsSync()` for tests.
- [x] **WI-A4**: Added `droppedControllerEventCount` and `droppedMappedControllerEventCount` atomics to `SynthAudioProcessor`. Both enqueue paths increment their respective counter instead of silently discarding. Added `V2ScopeFifoTests` covering round-trip, graceful overflow, and clear.
- [x] **WI-A5**: Added Doxygen locking contract to `MidiMappingEngine::translate()` and `NOLINTNEXTLINE` annotations on the `mutable` per-binding takeover fields.

## V2 Phase 10: Patch/state boundary and compatibility handling

Completed on 2026-05-15. The V2 patch/state boundary is now explicit, test-covered, and manually signed off in standalone and VST3 workflows.

- [x] Bumped the wrapped `.cspatch` format and wrapped processor-state format to explicit V2 version `2` boundaries while retaining the `.cspatch` extension.
- [x] Tightened patch parsing so the wrapper's declared `stateType` must match the current APVTS state type before any parameter state is considered loadable.
- [x] Removed the old bare-APVTS plugin-state restore fallback; processor state now restores only from the wrapped V2 root with the expected version and parameter-state child.
- [x] Tightened parameter-state application so load succeeds only when a complete current V2 parameter set is present exactly once, preventing partial overlap from old or malformed payloads from mutating the synth silently.
- [x] Added automated regressions for legacy patch-version rejection, wrong-state-type rejection, incomplete or partial-overlap parameter-state rejection, and wrapped-vs-unwrapped processor-state boundary behavior.
- [x] Updated the factory-profile regression for oscillator-level faders so it now asserts the intended soft-takeover behavior under the newer V2 init defaults instead of relying on stale first-touch assumptions.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-15 after the Phase 10 boundary change.
- [x] Manual validation passed in standalone and VST3 use: saved V2 patches reloaded identically, and legacy-version, bare-state, and partial-overlap payloads were rejected cleanly without mutating the current synth state.

## V2 Phase 9: MIDI learn and controller integration

Completed on 2026-05-15. The V2 controller and patch-workflow slice is now implemented, validated by automated coverage, and manually signed off in standalone and VST3 use.

- [x] Expanded MIDI learn eligibility from the old narrow continuous subset to the full exposed V2 panel surface, including discrete toggles and selector-style parameters.
- [x] Preserved notes, pitch bend, mod wheel, sustain, and host-safety controller semantics outside generic MIDI learn and runtime controller remapping.
- [x] Kept standalone settings and plugin-state learned-binding persistence working for the broader V2 surface, including discrete parameters such as `oscAWave`.
- [x] Retargeted the bundled MiniLab 3 Arturia-mode factory profile toward the V2 core surface: cutoff, resonance, filter-envelope amount, oscillator B fine tune, amp ADSR, oscillator A and B levels, master gain, oscillator A wave, and panic.
- [x] Preserved controller-profile override precedence so learned bindings still shadow the factory profile by both target parameter and CC signature.
- [x] Exposed `Init Patch`, `Save Patch`, and `Load Patch` in the shared plugin editor so the VST3 build now surfaces the same patch actions as standalone, and parented the async file choosers to the editor component.
- [x] Retuned the built-in init defaults to a more musically useful dry V2 baseline while keeping the first-load patch clean and FX-free.
- [x] Evaluated channel aftertouch for Phase 9 and intentionally deferred it because there is still no explicit aftertouch destination or parameter contract in V2; forcing one here would have added hidden scope rather than staying low risk.
- [x] Added regressions for full-surface learn eligibility, discrete learned-binding persistence, factory-profile signature shadowing, updated factory-profile runtime mappings, and init-default reset expectations.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-15 after the Phase 9 slice.
- [x] Manual validation passed on 2026-05-15 in both standalone and VST3 use: the updated MiniLab 3 profile behaved as expected, learned CC overrides worked, plugin learned mappings restored correctly, VST3 `Init Patch`/`Save Patch`/`Load Patch` buttons were visible, and patch save/load worked in the plugin workflow.

## V2 Phase 8: Global FX rack

Completed on 2026-05-15. The fixed-order V2 FX rack is now implemented and verified locally and manually in standalone and VST3 host use.

- [x] Added a dedicated `GlobalFxRack` on the V2 path so the engine now processes the documented fixed order `drive -> chorus -> delay -> reverb -> master gain` instead of bolting new stages directly into `SynthEngineV2`.
- [x] Implemented the global drive stage as a bounded soft-saturation pass with mix control and no render-path allocation.
- [x] Implemented the global chorus stage with the existing V2 rate/depth/mix parameters and a fixed ensemble-style voicing suited to the first-release rack.
- [x] Reworked the existing delay into the shared V2 rack and preserved real-time-safe smoothed delay-time and bounded feedback behavior.
- [x] Implemented the global reverb stage and kept output gain handling at the end of the rack through the existing master-gain smoothing path.
- [x] Added explicit bypass or disable behavior for the time-based stages so disabling delay or reverb clears latent tails instead of letting stale buffered audio reappear later.
- [x] Moved host tail reporting from the old delay-only shortcut to a rack-level estimate that now covers one-shot delay tails, feedback delay tails, and reverb tails conservatively.
- [x] Added regressions for zero-mix dry-path preservation, extreme full-rack stability, time-based effect disable clearing, processor reset silence with the full rack enabled, and rack-aware tail reporting.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-15 after the Phase 8 FX-rack change.
- [x] Manual standalone FX-rack smoke passed on 2026-05-15: dry baseline, drive, chorus, delay, reverb, full-rack playback, delay disable clearing, reverb disable clearing, and panic silence all worked as expected.
- [x] Manual VST3 host FX-rack smoke passed on 2026-05-15: dry vs effected playback, effect disable clearing, and stop/reset tail behavior worked as expected in host use.

## V2 Phase 7: Host-aware arpeggiator

Completed on 2026-05-15. The V2 arpeggiator is now implemented and locally verified, and the remaining Ableton Live Lite VST3 host-sync smoke has been intentionally deferred to the final release-validation phase so Phase 8 can proceed.

- [x] Added a dedicated `Arpeggiator` inside `SynthEngineV2` with bounded held-note, latched-note, and pending ringing-note state; pattern walking for `Up`, `Down`, `Up/Down`, and `As Played`; octave cycling; and gate timing that carries note-offs across block boundaries when needed.
- [x] Updated `SynthAudioProcessor::processBlock()` to read host tempo, PPQ, and transport state from `AudioPlayHead::getPosition()` in plugin mode and pass a compact transport snapshot into the V2 engine.
- [x] Implemented deterministic internal-tempo fallback when host timing is incomplete, and explicit host-transport stop handling so host-synced arp playback releases ringing notes instead of stalling ambiguously.
- [x] Routed arp-generated note on/off events through the same sample-offset allocator path as ordinary keyboard play so arp timing, stealing, and release behavior share the existing V2 event model.
- [x] Added `V2Arpeggiator` regressions in `tests/StabilityAndDisconnectTests.cpp` for pattern ordering, octave range, latch behavior, gate timing, internal-rate fallback, host-PPQ alignment, transport-stop release, arp-disable release, and panic clearing.
- [x] Fixed a Windows patch-save failure found during Phase 7 manual validation by closing the temporary output stream before replacing the destination file, and added a real patch file write/read round-trip regression in `tests/PatchStateTests.cpp`.
- [x] Manual standalone validation passed on 2026-05-15 using a simplified dry arp test patch: internal tempo, pattern changes, octave range, gate length, latch behavior, patch save, and patch load all worked as expected.
- [x] Deferred Ableton Live Lite VST3 arp host-sync smoke to the final release-validation phase by user request after workflow issues during host bring-up.

## V2 Phase 6: Performance modulation and play modes

Completed on 2026-05-15. The full V2 performance layer is now wired end-to-end: global LFO, Poly Mod cross-modulation, pitch bend and mod wheel, glide, mono/unison play modes with key priority, bounded vintage drift, and balance-style pan spread. The dry voice rendering loop was unified so all modulation sources mix per-sample without breaking the cutoff smoother.

- [x] Global LFO routed per-sample through `SynthVoice` with sine/triangle/square waveforms, rate, pitch/PW/cutoff depths, and mod-wheel depth; engine now advances `globalLfoPhase` across event spans so all voices share a single coherent phase.
- [x] Poly Mod sources (Osc B audio, filter envelope) reach the constrained destinations (osc pitch, pulse width, filter cutoff) per-sample, gated by the V2 Poly Mod depth controls.
- [x] Pitch bend converts to semitone offset using the V2 `pitchBendRangeSemitones` parameter; mod wheel is now broadcast to every voice each render span instead of being silently ignored.
- [x] Implemented mono and unison play modes with held-note tracking (bounded `std::array`, no audio-thread allocation) and key-priority fallback (Last/Low/High). Mono retriggers from the held-note set on release; unison stacks all voices on the same note with deterministic per-voice detune.
- [x] Bounded vintage drift: per-voice deterministic cents offset scaled by `vintageAmount` (max ±25 cents). Pan spread uses balance-style L/R attenuation so center voices stay at unity, full-spread voices pan hard.
- [x] Added Phase 6 regressions in `tests/StabilityAndDisconnectTests.cpp` (`V2Performance` suite): bend-range affects render, mono-mode keeps one active voice, mono low-priority falls back to lowest held note, unison stacks all voices on one note, glide produces different rendered output than no-glide, vintage drift stays finite/bounded, mod-wheel changes LFO modulation.
- [x] UI verified: `SynthAudioProcessorEditor` already binds LFO/Poly Mod/Performance knobs to the correct V2 parameter IDs and `addAndMakeVisible`s them, so no editor changes were needed.
- [x] Local verification passed with `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` on 2026-05-15.

## V2 Phase 4: Filter and dual-envelope tone path

Completed on 2026-05-15. The dry Prophet-adjacent tone-shaping path is fully implemented, introducing the dedicated filter envelope, keyboard tracking, and the new 4-pole low-pass behavior.

- [x] Implement dedicated filter ADSR and amp ADSR per voice.
- [x] Implement the V2 resonant low-pass filter with cutoff, resonance, filter-envelope amount, and keyboard tracking.
- [x] Wire envelope retrigger and voice-steal reset behavior into repeated-note and stolen-voice cases.
- [x] Add tests for filter stability across supported sample rates and for envelope restart behavior.
- [x] Manually verify init-patch bass, brass, pluck, and pad programming on the new dry voice path.
- [x] Verify Debug build and `ctest` still pass after the filter/envelope cutover.

## V2 Phase 3: Dual-oscillator mixer voice core

Completed on 2026-05-14. The DSP voice core is fully implemented, and the manual dry-tone audition has been deferred to Phase 8 when the UI becomes available.

- [x] Replaced the temporary single-source V2 voice output with a per-voice dual-oscillator plus noise source path inside `SynthVoice`, while deliberately keeping the existing filter and amp-envelope path isolated for Phase 4.
- [x] Implemented oscillator A and oscillator B source controls on the live V2 path: pulse, triangle, saw, and later sine wave shapes; octave and fine tune; pulse width; oscillator A hard-sync behavior; and oscillator B low-frequency mode support.
- [x] Added bounded pre-filter overload gain staging on the per-voice mixer path so stacked source levels can push the dry tone harder without destabilizing render.
- [x] Added a short deterministic note-start de-click ramp plus per-voice phase randomization so dense simultaneous note-ons no longer produce the previously observed outsized startup transient under aggressive settings.
- [x] Added render-level regressions for pulse-width edge settings, sync-enabled render divergence, dual-oscillator detune divergence, dense note-start transient containment, and full-mixer stability.
- [x] Verified `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-14 after the Phase 3 voice-core change.

## Late V2 UI update: Signal Chain Visualizer

Completed on 2026-05-15. Added a high-signal "Visual Laboratory" to the title bar that provides interactive feedback on waveform construction and subtractive filtering.

- [x] Implemented `SignalChainVisualizer` with three panes: Source (ideal), Filter (preview), and Output (real-time).
- [x] Integrated the visualizer into the title bar next to the logo, occupying 500x48 pixels.
- [x] Wired the "Source" and "Filter" panes to update mathematically based on oscillator and filter parameters on the UI thread.
- [x] Implemented a lock-free FIFO bridge to feed live audio samples from `SynthAudioProcessor::processBlock` to the "Output" oscilloscope.
- [x] Verified that the visualizer is performant and correctly reflects all parameter changes (waveforms, PW, levels, cutoff, resonance).
- [x] Updated `CMakeLists.txt` and verified the build and existing `CoolSynthMidiLearnTests` suite pass.

## Late V2 scope update: sine oscillator and LFO waveform

Completed on 2026-05-15. V2 now exposes a true sine oscillator option again across both main oscillators and the global LFO without changing the dual-oscillator architecture or growing the control count.

- [x] Added `sine` to the V2 oscillator and LFO wave-shape enums, APVTS choice surface, processor-side waveform decoding, and all wave selectors in the one-page editor.
- [x] Implemented real sine sample generation in `SynthVoice` for both oscillator and LFO modulation paths.
- [x] Restored legacy V1-to-V2 sine mapping so the older compatibility seam no longer aliases sine to triangle.
- [x] Added tooltips and editor icons for the new sine options in the oscillator and LFO sections.
- [x] Added a sine-wave render regression to confirm the new shape stays audible, finite, and bounded on the live V2 engine path for both oscillators and LFO.
- [x] Fixed a broken factory profile test in `MidiLearnTests.cpp` that was caused by the changed number of waveform options.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-15.

## V2 Phase 1: V2 parameter contract and processor seam

- [x] Added the stable V2 parameter IDs and grouped APVTS definitions for oscillators, mixer, filter, filter envelope, amp envelope, LFO, Poly Mod, performance, arp, drive, chorus, delay, reverb, and output.
- [x] Rebuilt `ParameterLayout.cpp` around `juce::AudioProcessorParameterGroup` with a deliberate V2 compatibility split: semantically equivalent legacy parameters kept their original IDs and version hints, while new V2-only controls use new IDs and a higher version hint.
- [x] Extended processor-side raw-parameter binding and V2 block snapshot decoding to cover the full Phase 1 parameter surface.
- [x] Introduced `SynthEngineV2` as a seam in `SynthAudioProcessor` that preserves the current playable legacy engine underneath while Phase 2 replaces note dispatch and allocation.
- [x] Updated controller-profile/runtime references and the existing editor subset to the new `oscAWave` V2 ID so the current shell stays operable during the staged cutover.
- [x] Added tests for V2 parameter registration uniqueness and V2-state serialization through patch round-trip and sanitized APVTS restore paths.
- [x] Verified `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-14.

## V2 Phase 2: Custom allocator and event-path cutover

- [x] Added explicit V2 engine event records with per-event sample offsets for note on/off, pitch bend, mod wheel, and sustain input.
- [x] Replaced the Phase 1 legacy-engine adapter with a real `SynthEngineV2` allocator that owns preallocated voice slots, release-first stealing, sustain state, panic reset, and in-block event-accurate rendering.
- [x] Reworked `SynthVoice` into a direct reusable voice primitive for the custom allocator while keeping the audible path intentionally simple for this phase.
- [x] Routed `SynthAudioProcessor::processBlock()` through the explicit V2 event path and render contract instead of forwarding raw MIDI into the old JUCE `Synthesiser` path.
- [x] Removed callback-lock-dependent runtime mapped-action handling. Plugin controller translation now uses a raw controller-event bridge off the audio thread, and standalone/controller-profile translation no longer locks the processor callback.
- [x] Added automated regressions for event-offset rendering, sustain hold-and-release behavior, panic silence, release-first voice stealing, oldest-held fallback stealing, processor-level note rendering through `processBlock()`, and reserved-performance-control learn rejection.
- [x] Hardened the Phase 2 MIDI boundary so host `All Notes Off`, `All Sound Off`, and `Reset All Controllers` messages now flow into the V2 engine instead of being ignored, and kept those reserved synth controllers out of generic MIDI learn and runtime CC mapping.
- [x] Fixed plugin controller-event FIFO wraparound handling and added regressions for queue wrap, reserved host-safety CC rejection, engine-level all-notes-off release, reset-controller sustain release, and processor-level all-sound-off silence.
- [x] Implemented `SynthAudioProcessor::reset()` for the V2 path so host reset requests explicitly panic the allocator, clear active tails, and reset keyboard state instead of relying on the JUCE no-op default.
- [x] Replaced the old hard-coded zero plugin tail report with a simple delay-aware V2 tail contract, and added processor-level regressions for reset-driven silence and nonzero tail reporting only when delay can actually ring.
- [x] Verified the current standalone artifact launches on Windows without immediate crash, and confirmed both Ableton Live 12 Lite and REAPER discover `CoolSynth` as a VST3 instrument through their host-side scan state (`Live-plugins-1.db`, `PluginScanner.txt`, `reaper-vstplugins64.ini`, and `reaper-fxtags.ini`).
- [x] Removed the environment blocker for first-release two-host VST3 smoke by downloading and installing REAPER from the official installer on 2026-05-14.
- [x] Added visible build identity stamping to the standalone status bar and the plugin editor footer so manual smoke can confirm the tested standalone and VST3 binaries match the current build.
- [x] Completed manual standalone and Ableton Live 12 Lite audible Phase 2 smoke on 2026-05-14: standalone note on or off, repeated notes, and panic passed; Ableton VST3 note playback, repeated notes, panic, and no stuck-note behavior passed.
- [x] Noted one non-blocking patch-dependent artifact during standalone smoke: dense simultaneous key presses could produce a small note-start click with aggressive settings, but it disappeared after safer knob settings and did not reproduce as an allocator or stuck-note failure.
- [x] Standalone sustain was not directly exercised during manual smoke because no CC64 pedal or equivalent controller source was available; existing automated sustain-path coverage remained in place, and the user accepted Phase 2 closure without a separate standalone sustain-pedal pass.
- [x] Closed V2 Phase 2 on 2026-05-14 and advanced `TODO.md` to Phase 3.
- [x] Verified `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug --clean-first`, and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-14.

## Phase 1: Reproducible JUCE build skeleton

- [x] Add the JUCE git submodule under `external/JUCE` and pin it to a known commit. (Commit 29396c22, JUCE 8.0.12)
- [x] Create the top-level CMake project that builds standalone and VST3 outputs from one shared plugin target.
- [x] Add `SynthAudioProcessor` and `SynthAudioProcessorEditor` placeholder classes.
- [x] Define the initial APVTS parameter layout with the stable IDs from `REQUIREMENTS.md`.
- [x] Document the Windows configure and build workflow in `README.md` using `Visual Studio 17 2022` as the default generator.
- [x] Verify a Debug build produces a launchable standalone artifact and a VST3 artifact.

## Phase 2: Standalone audio device shell

- [x] Expose standalone audio-device configuration using JUCE-native controls.
- [x] Show active backend, output device, sample rate, and buffer size in a new `StandaloneAudioStatusPanel`.
- [x] Implement a custom standalone app bootstrap to prefer WASAPI shared mode on Windows.
- [x] Ensure the editor layout adapts conditionally to standalone and plugin runtimes.
- [x] Verify that the app remains stable during audio device changes or when no device is available.
- [x] Preserve the shared processor and editor boundary so the VST3 target remains free of standalone hardware assumptions.

## Phase 3: Standalone MIDI input shell and monitor

- [x] Expose standalone MIDI input selection for one active device at a time.
- [x] Persist selected MIDI device by identifier and handle missing remembered devices gracefully.
- [x] Implement real-time MIDI status reporting (connected, disconnected, unavailable).
- [x] Create a bounded MIDI monitor for note-on, note-off, and CC events.
- [x] Use a thread-safe, lock-free queue for routing MIDI events from the callback to the UI monitor.
- [x] Verify that the VST3 target remains unaffected by standalone-only MIDI UI.

## Phase 4: Playable sine synth voice path

- [x] Added `SynthEngine` with 8 preallocated voices.
- [x] Implemented per-voice sine oscillator playback.
- [x] Implemented per-voice ADSR amplitude envelope and velocity-to-amplitude scaling.
- [x] Implemented release-first then oldest-active voice stealing.
- [x] Added panic handling that clears active voices and held-note state.
- [x] Verified that standalone MIDI input produces audible notes (via code review and build validation).
- [x] Implemented a bridge to request panic when a selected MIDI device is disconnected.

## Phase 5: Parameter-driven core synth UI

- [x] Added waveform support for sine, square, and saw in the synth engine.
- [x] Created reusable UI primitives: `HardwareKnob`, `HardwareFader`, and `SynthSection`.
- [x] Implemented a sectioned editor layout (Oscillator, Envelope, Output) with parameter-linked controls.
- [x] Used APVTS attachments for thread-safe UI-to-parameter communication.
- [x] Implemented a timer-based value display refresh using canonical parameter text.
- [x] Verified that UI changes are audible and the editor remains responsive during playback.
- [x] Confirmed the VST3 target remains compatible with the shared editor.

## Phase 7: Fixed MiniLab core controller mapping

- [x] Implemented `MidiMappingEngine` for fixed MiniLab 3 controller routing.
- [x] Routed verified MiniLab waveform, ADSR, and master-gain controls to APVTS parameters.
- [x] Routed the panic action through an explicit command path (Pad 8, Bank A).
- [x] Isolated MiniLab-specific constants in `Minilab3Profile.cpp`.
- [x] Verified controller-driven parameter changes update the UI in standalone mode via APVTS attachments.
- [x] Confirmed no host-notifying parameter writes occur in the audio callback.
- [x] Updated `TODO.md` and documented mappings in `docs/minilab3-default-messages.md`.

## Phase 9: Global delay slice

- [x] Added a shared global delay effect stage after voice mixing.
- [x] Wired delay time, feedback, and mix to the existing APVTS parameters.
- [x] Added dedicated "Delay" section to the UI with attached knobs.
- [x] Extended fixed MiniLab 3 mapping: Knob 8 to Delay Mix, Fader 2 to Delay Feedback, Fader 3 to Delay Time.
- [x] Hard-clamped feedback to 0.85 for stability and safety.
- [x] Verified manual delay-time changes remain stable and real-time safe (via build validation).
- [x] Adjusted editor layout and default dimensions to accommodate the new controls.

## Phase 10: Hardware-style UI refinement

- [x] Refined the editor into grouped oscillator, filter, envelope, delay, output, and global action sections.
- [x] Moved standalone audio and MIDI utility controls into one dedicated settings surface.
- [x] Replaced the large standalone status panel with a compact bottom status bar.
- [x] Added live last-MIDI-event status text to the standalone status bar.
- [x] Removed redundant standalone audio or MIDI settings entry points.
- [x] Ensured the plugin editor omits standalone-only device, settings, status, and monitor UI.
- [x] Improved control labels and value readability.
- [x] Verified the refined UI remains usable during playback.

## Phase 12: Standalone Device Persistence

- [x] Persist the last valid standalone audio backend, output device, sample rate, and buffer size.
- [x] Persist the last valid standalone MIDI input selection.
- [x] Restore persisted standalone device settings when still available.
- [x] Show unavailable state when saved devices are missing.
- [x] Clear held-note state when the active MIDI device disconnects during playback.
- [x] Verify standalone restart and missing-device behavior.

## Phase 13: MIDI learn workflow

- [x] Add per-parameter MIDI learn mode for continuous parameters.
- [x] Capture only CC messages for learned mappings.
- [x] Reject note events as continuous-parameter mappings.
- [x] Add a clear-mapping action.
- [x] Persist learned mappings separately from synth parameter state.
- [x] Verify learned mappings survive an app restart.

## Phase 14: Patch save/load workflow

- [x] Added an `Init Patch` action that resets all automatable parameters to defaults.
- [x] Implemented patch save and load using a minimal `.cspatch` XML format.
- [x] Ensured patch files contain only synth parameter state (APVTS).
- [x] Verified that standalone device settings and learned mappings are excluded from patches and survive loads.
- [x] Implemented immediate UI restoration upon patch load.
- [x] Added automated unit tests for patch wrapping, parsing, round-trip, and state boundary enforcement.
- [x] Wired patch actions to standalone editor buttons.

## Phase 15: Windows CI build pipeline

- [x] Added a manual-only Windows validation workflow for clean-checkout configure, build, test, and optional packaging.
- [x] Added isolated CI CMake presets for Debug and Release with `BUILD_TESTING=ON` and `COOLSYNTH_ENABLE_VST3_USER_INSTALL=OFF`.
- [x] Added repository-owned PowerShell CI scripts for bootstrap, configure, build, test, packaging, checksums, and manifest generation.
- [x] Added a tag-only Windows release workflow that publishes standalone, VST3, and checksum assets with generated GitHub release notes.
- [x] Verified the shared JUCE submodule bootstrap path is reused in automation without a second JUCE download path.
- [x] Verified local CI-style Release configure, build, `ctest`, and packaging using the new scripts.
- [x] Verified a live manual validation run on GitHub (`Windows Manual Validation`, run `25614177697`) and downloaded the diagnostics plus package artifacts.
- [x] Verified a live tag-triggered prerelease publish on GitHub (`Windows Release`, run `25614334076`), confirmed generated notes and assets, reran it successfully without creating a duplicate release, downloaded the published assets, and cleaned up the disposable prerelease tag.

## Phase 16: Final stabilization and release review

- [x] Fixed standalone MIDI disconnect handling so an unplugged active device reports `disconnected` instead of degrading immediately to `rememberedDeviceUnavailable`.
- [x] Cleared queued controller events and last-event snapshot state when the selected standalone MIDI device disconnects.
- [x] Hardened patch loading by sanitizing APVTS state to known parameter children and rejecting duplicate parameter IDs in crafted patch XML.
- [x] Added resonance smoothing in `SynthVoice` to reduce abrupt filter-control clicks during hardware or automation jumps.
- [x] Added automated regressions for disconnect recovery, crafted patch sanitization, supported-sample-rate filter stress, abrupt cutoff/resonance jumps, delay-time jumps, and master-gain jumps.
- [x] Verified the updated `CoolSynthMidiLearnTests` executable passes locally after the stabilization changes.

## Controller profile refactor foundation

- [x] Replaced hardcoded MiniLab runtime bindings with a bundled data-driven controller profile resource.
- [x] Added controller-profile selection persistence for standalone mode, including MiniLab 3 auto-detect and an explicit "no factory profile" option.
- [x] Extended the standalone MIDI settings tab with controller-profile selection and resolved-profile display.
- [x] Added host parameter context menus and `getControlParameterIndex()` support in the shared editor for host formats that expose them.
- [x] Verified the shared plugin target and `CoolSynthMidiLearnTests` build in Debug.
- [x] Verified `ctest --test-dir build -C Debug --output-on-failure` passes locally, including new controller-profile and factory-mapping tests.

## Plugin MIDI learn

- [x] Added plugin-mode MIDI learn for DAW-routed live CC input on learnable continuous controls.
- [x] Persisted plugin learned MIDI bindings in plugin state and restored them in `setStateInformation()`.
- [x] Added processor-owned lock-free queues for plugin controller-event capture and mapped-action dispatch.
- [x] Applied plugin learned CC mappings off the audio thread through a dedicated dispatcher thread, preserving standalone behavior.
- [x] Extended the plugin editor to load existing learned bindings, capture live plugin CC input, and apply the captured binding immediately.
- [x] Verified local tests for plugin learned CC runtime application and plugin-state round-trip persistence.

## Standalone MIDI settings reset

- [x] Added a `Reset MIDI Settings` action to the standalone `Options -> MIDI` tab.
- [x] Cleared persisted standalone MIDI input selection, learned MIDI CC mappings, controller-profile selection, and CC-label preference through one shared settings-store path.
- [x] Stripped stale `MIDIINPUT` nodes from the standalone `audioSetup` XML while preserving audio-device settings.
- [x] Reapplied the default live standalone MIDI state immediately after reset, without requiring an app restart.
- [x] Verified the settings-reset behavior with a dedicated `CoolSynthMidiLearnTests` unit test plus local Debug build and `ctest`.

