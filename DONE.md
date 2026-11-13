# DONE

## Engine quality pass 2 — oversampling, delay, vintage, drive, phaser, clipper, presets

Completed on 2026-11-12, directly after pass 1. `DESIGN_V2.md` sections 6.1, 6.2, 7.3, 9.4, 10.1 and 10.3 record the shipped designs.

- [x] Voices render at 2x the host rate; the voice sum is decimated by `HalfBandDecimator2x`, a polyphase IIR elliptic half-band (about -96 dB stop band, 6 % transition, coefficients from an elliptic design computed once at first use). The engine measures the decimator's group delay with an impulse and the processor reports it to the host. Saw aliasing measured about -105 dB at A4 and -106 dB at A7; the alias regressions now demand -80 and -70 dB instead of -50 and -30.
- [x] Ladder resonance law changed from k = 4r to k = 4 sqrt(r) so the audible peak grows evenly across the knob; the triangle is rendered naively at full level now that the voice is oversampled.
- [x] `GlobalDelay` rewritten: own line with Hermite reads, an 80 ms time slew that glides the repeats, and a feedback path with 5.5 kHz low-pass, 110 Hz high-pass and tanh. Replaces `juce::dsp::DelayLine`.
- [x] Vintage extended from a fixed per-voice pitch offset to slow random-walk pitch drift (up to 7 cents, eased over 350 ms toward a new target every 0.4 to 1.2 s), a fixed per-voice cutoff offset (up to 6 %) and envelope-time scale (up to 12 %), all under the existing `vintageAmount` knob and seeded deterministically.
- [x] Drive shapes at 2x through `HalfBandUpsampler2x` and `HalfBandDecimator2x`, mixes at 2x so partial mix never comb-filters, and closes a one-pole low-pass from 20 kHz to 8 kHz with amount. Phaser now has six stages (three notches). A soft clipper after master gain is transparent below 0.8 and never exceeds 1.0.
- [x] Factory presets retuned to the ladder law in both the YAML source and `FactoryPresets.cpp` by keeping each preset's cutoff-to-passband ratio equal to what its old state-variable Q produced.
- [x] Added regressions `halfband_decimator_passes_band_and_rejects_images`, `master_soft_clip_bounds_output_below_unity` and `vintage_drift_stays_bounded_and_moves_smoothly`; the alias helper now logs its measurement.
- [x] Verified `cmake --build --preset build-debug --config Debug` (no warnings) and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-11-12.

## Engine quality pass — custom DSP in place of stock JUCE components

Completed on 2026-11-12. Sound quality took precedence over the earlier document wording; `DESIGN_V2.md` sections 7.3, 7.4, 10.2 and 10.4 now record the shipped designs.

- [x] Replaced the single 2-pole `juce::dsp::StateVariableTPTFilter` with `VoiceLadderFilter`, a zero-delay-feedback 4-pole transistor ladder (24 dB/octave) with a tanh saturator on the ladder input and partial bass makeup. Resonance 0..1 maps to feedback k = 4r and self-oscillates at the top; per-sample cutoff modulation (envelope, LFO, Poly Mod) is preserved.
- [x] Replaced `juce::ADSR` with `AnalogEnvelope`, exponential RC-style segments behind the same public surface; a retrigger continues from the current level.
- [x] Replaced `juce::dsp::Reverb` with `PlateReverb` (Dattorro plate, delay lengths scaled from the 29.761 kHz reference) and `juce::dsp::Chorus` with `StereoChorus` (Juno-style anti-phase taps, Hermite interpolation). `GlobalFxRack` owns no JUCE effect objects any more.
- [x] Compressor makeup now follows a 100 ms average of the applied gain reduction, capped at +6 dB, so compressed peaks never leave louder than bypass. The former fixed `amount * 6 dB` makeup failed the existing monotonicity regression as soon as the ladder changed the crest factor of the source.
- [x] Added regressions `filter_slope_is_four_pole`, `reverb_tail_rings_and_decays_after_note_off` and `chorus_decorrelates_left_and_right`; reworded the resonance-bound test comments.
- [x] Verified `cmake --build --preset build-debug --config Debug` (no warnings) and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-11-12.

## UI/UX review follow-up — resizable window, accessibility, labels, undo

Completed on 2026-11-02 from the open items in `UI_UX_REVIEW.md`.

- [x] Made the editor user-resizable (50% to 200%, fixed aspect ratio) by hosting all controls in a scaled content panel; added a right-click zoom menu, Ctrl+plus / Ctrl+minus / Ctrl+0, a persisted standalone scale, and fit-to-display on first launch.
- [x] Made knobs and faders keyboard-focusable with a focus ring in every custom paint path, gave sliders, toggles, segment cells, sections and icon-only buttons accessible titles and help text, and contained Tab traversal inside the Advanced overlay.
- [x] Reworked the label system: 13 px widget titles, shortened long labels, icon-only cells below 32 x 44 px, LFO wave as 2 x 2, and `A` / `B` tags on the oscillator rows.
- [x] Added processor-owned snapshot undo/redo driven by parameter gestures and bulk edits, Undo / Redo buttons and shortcuts, a patch name with dirty marker in the title row, a category-grouped preset selector with prev / next that clears on edit, and processor-state format `8` carrying the patch identity.
- [x] Added regressions for uniform scaling and clamping, focusable and named controls, gesture and bulk undo/redo, patch identity state round trip, and preset selector clearing and undo.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-11-02, plus offscreen renders of the panel at 100% and 60% and of the Advanced overlay.

## ARP Expansion Phase F — Format boundary and curated preset-bank slice

Completed on 2026-05-17. The code and data portion of Phase F is implemented and locally verified; only the manual preset audition remains open in `TODO.md`.

- [x] Bumped the wrapped `.cspatch` and wrapped processor-state format versions from `6` to `7`, intentionally rejecting older saved patches or state under the full expanded arp parameter contract.
- [x] Extended the factory-preset source pipeline so the YAML source and generated `FactoryPresets.cpp` now both carry the full exposed preset surface, including the new arp expansion fields plus the already-shipped `timbre` / `excite` / phaser / compressor entries.
- [x] Re-emitted the YAML-backed preset data with explicit defaults for the expanded arp surface and preserved the explicit full-parameter contract for every factory preset.
- [x] Added a larger curated `Arp` bank covering the new random, random-walk, order-mode, Euclidean, chance, ratchet, and accent combinations, including `Random Walk Pluck`, `Random Walk Pad`, `Random Sparks`, `Trance Gate`, `Stuttered Chord`, `Tresillo Bass`, `Cinquillo Lead`, `Polymeter Stab`, `Converge Bell`, `Inside Orbit`, `Outside Plucker`, and `Diverge Sweep`.
- [x] Added preset regressions that assert full parameter coverage across the factory bank and round-trip every factory preset through the current strict wrapped patch-load path.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-17 after the Phase F implementation slice.

## ARP Expansion Phase E — Advanced overlay UI

Completed on 2026-05-17. The Phase E overlay slice is implemented, build-test clean, and keeps the expanded arp surface inside the existing one-page editor.

- [x] Added `ArpAdvancedOverlay` as a full-editor dimmed overlay with a centered advanced arp panel, `Escape` dismissal, close button, and shared APVTS-backed controls for rhythm, Euclidean shape, chance, ratchet, accent, and standalone-only internal tempo.
- [x] Removed the temporary Phase C/D advanced arp controls from the main band and replaced them with an `Advanced...` entry point plus a compact status strip that summarizes non-default overlay state.
- [x] Added a dedicated Euclidean-cycle helper so the overlay visualizer and arp engine share the same bounded cycle builder and rotation behavior.
- [x] Added editor-level regressions covering overlay show/hide behavior, Euclidean control visibility under `Straight` versus `Euclidean`, and non-default advanced-summary text.
- [x] Corrected the Phase D `E(7,12)` acceptance example from an impossible eight-pulse string to the engine's current valid rotated Euclidean necklace, and verified the necklace orientation against Toussaint's published `E(7,12)` reference.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-17 after the Phase E change.

## ARP Expansion Phase D — Euclidean rhythm

Completed on 2026-05-17. The Phase D rhythm slice is implemented, build-test clean, and intentionally breaks older saved patch/state compatibility.

- [x] Added `arpRhythm`, `arpEuclideanPulses`, `arpEuclideanSteps`, and `arpEuclideanRotation` across `ParameterIDs`, the APVTS layout, raw parameter binding, and processor block decoding.
- [x] Added cached Euclidean-cycle state to `Arpeggiator` and integrated it into the step loop so Euclidean rests advance only the rhythm slot while the melodic walk advances only on firing pulses.
- [x] Implemented a bounded Bjorklund-style cycle builder with rotation support and host-synced bar-reset behavior on the current Phase D contract.
- [x] Added deterministic `V2Arpeggiator` coverage for known Euclidean patterns, pulse-vs-slot melodic advancement, and host-synced cycle reset at the bar boundary.
- [x] Kept the editor temporary for this slice: the new Euclidean parameters are in the parameter contract and processor path now, while the planned overlay UI remains deferred to Phase E.
- [x] Bumped wrapped `.cspatch` and wrapped processor-state format versions from `5` to `6` so older saved patches/state are rejected cleanly under the expanded arp parameter contract.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-17 after the Phase D change.

## ARP Expansion Phase C — Ratchet + Accent

Completed on 2026-05-17. The Phase C arp modifier slice is implemented, build-test clean, and intentionally breaks older saved patch/state compatibility.

- [x] Added `arpRatchetCount`, `arpRatchetChance`, `arpAccentEvery`, and `arpAccentAmount` end-to-end across `ParameterIDs`, the APVTS layout, raw parameter binding, processor block decoding, and the shared editor's temporary arp surface.
- [x] Updated the arp engine so ratchet expands a step into deterministic sub-step retriggers, chord mode ratchets the whole chord stab, and accent scales note velocity on emitted-step boundaries rather than raw pattern advances.
- [x] Kept the overflow behavior explicit for expanded chord steps: ratcheted chord stabs are emitted atomically or dropped entirely so the event buffer never receives a partial burst.
- [x] Added deterministic `V2Arpeggiator` coverage for ratchet offsets/counts, emitted-step accent grid behavior under chance skipping, chord-mode ratchet-plus-accent application, and ratcheted chord overflow dropping.
- [x] Bumped wrapped `.cspatch` and wrapped processor-state format versions from `4` to `5` so older saved patches/state are rejected cleanly under the expanded arp parameter contract.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `ctest --test-dir build -C Debug --output-on-failure` pass on 2026-05-17 after the Phase C change.

## ARP Expansion Phase B — Swing + Chance

Completed on 2026-05-17. The Phase B arp modifier slice is implemented, build-test clean, and intentionally breaks older saved patch/state compatibility.

- [x] Added `arpSwing` and `arpChance` end-to-end across `ParameterIDs`, the APVTS layout, raw parameter binding, processor block decoding, and the main arp band in the shared editor.
- [x] Updated the arp engine so swing delays every other step by a rate-relative fraction of half a step, while chance skips steps after the melodic pattern advances, preserving the pattern walk instead of stalling it.
- [x] Kept chord-mode chance semantics phase-correct: one probability roll gates the whole chord step, matching the approved Phase B decision.
- [x] Added deterministic `V2Arpeggiator` coverage for internal-clock swing offsets, host-synced rate-relative swing offsets, and seeded 50% chance behavior over a 1000-step run.
- [x] Bumped wrapped `.cspatch` and wrapped processor-state format versions from `3` to `4` so older saved patches/state are rejected cleanly under the new arp parameter contract.
- [x] Verified `cmake --build --preset build-debug --config Debug` and `build\CoolSynthMidiLearnTests_artefacts\Debug\CoolSynthMidiLearnTests.exe` pass on 2026-05-17 after the Phase B change.

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


## Engine pass 3: glitch fixes (2026-11-12)

- [x] Replaced the per-active-voice headroom (1/sqrt(N)) with a fixed voice headroom of 0.5, which removed the level pumping heard when chords changed under a held note.
- [x] Added a 1.5 ms steal fade with a pending note start: a stolen voice fades its old note out before the new note starts, which removed the clicks on voice steal.
- [x] Applied the plate reverb decay once per tank half, as in the Dattorro paper, instead of twice.
- [x] Raised the Master Gain range to +6 dB.
- [x] Adjusted the stolen-voice test window and verified the full suite green; committed as 6b5973f.

## V3 sound bank design and engine requirements (2026-11-12)

- [x] Wrote `SOUND_BANK_V3.md`: the "Places" bank of 11 rooms and 68 presets (Bedroom, Arcade, Outrun, Observatory, Greenhouse, Basement, Studio 1982, Main Stage, Engine Room, Clock Tower, Workshop), each preset with recipe, needed modules and a Now / Now~ / placeholder status; 39 buildable on V2, 29 placeholders.
- [x] Wrote `REQUIREMENTS_V3.md`: module catalogue (generators, processors, local and global modulators, note layer, parts, effects), traceability from the bank with priorities, a slot-chain engine shape compared with a free graph and with extending V2, an eight-phase order, and the open decision.
- [x] Checked the module catalogue against Plaits, MiniFreak, OP-1 field, SH-4d, NTS-1 mk2, Dirtywave M8, Serum 2 and Vital; sources listed in both documents.
- [x] Refreshed `TODO.md` for V3 and marked which V2 backlog items V3 absorbs.

## Polyphony raised to 16 voices (2026-11-12)

- [x] Set `defaultVoiceCount` to 16 in `src/synth/SynthParameters.h`; the processor builds the engine with that default.
- [x] Sized the dense note-on click test to its eight literal events so it no longer depends on the voice count.
- [x] Updated the V2 requirement, plan and design statements from 8 to 16 voices.
- [x] Verified Debug build without warnings and `ctest` green.

## V3 design and implementation plan (2026-11-12)

- [x] Wrote `DESIGN_V3.md`: module interface and descriptors, slot variants with arena memory and crossfaded type changes, cost classes and voice cap, the voice with three generator and three processor slots and combine modes, sub-block rendering, local and global modulators, the 24-slot matrix, note layer with chord memory, two parts, effect slots, parameter IDs by slot path, state version 8, realtime rules, test plan, migration table from V2.
- [x] Wrote `IMPLEMENTATION_PLAN_V3.md`: phases 0 to 8 in review-sized chunks, each with files, tests and the rooms of `SOUND_BANK_V3.md` it ships.
- [x] Refreshed `TODO.md` with the first four chunks and `GEMINI.md` with the new authoritative files.

## V3 phase 0.1: module interface and slots (2026-11-12)

- [x] Added `src/synth/v3/Module.h`: `ParameterDescriptor`, `ModuleDescriptor`, `NoteContext`, `SlotParams`, the `SlotMemory` float arena, the `Module` concept, and `Slot<std::variant<...>>` with descriptor lookup by type index, arena sizing from the largest type, pending-type requests from any thread, a 5 ms crossfade on type change with the old module running on a copy of the input, note hand-over to the new module, per-module parameter clamping, and testing accessors.
- [x] Added `tests/V3ModuleTests.cpp` (nine tests with dummy generator and processor types): descriptor table and arena sizing, arena spans, generator and processor crossfades with the step bound, same-type no-op, request during a fade waits, note hand-over and reset, prepare applies a pending type at once, descriptor clamping. Registered in the test target.
- [x] Verified Debug build without warnings, `ctest` green, measured crossfade step 0.004167 against the 0.004177 bound.

## V3 phase 0.2: sub-block modulation context (2026-11-13)

- [x] Added `src/synth/v3/Modulation.h`: `ModSource` and `ModSources`, `SlotOffsets`, `SlotBase` with linear per-sub-block smoothing in normalised space (pan and pitch scaled by their spans, targets clamped, snap on prepare and reset), `descriptorToUnits` and `unitsToNormalised` with linear and logarithmic skew, and `buildSlotParams` (base plus offsets, clamped, mapped to units, non-modulatable parameters ignore offsets).
- [x] Added `tests/V3ModulationTests.cpp` (six tests): mapping round trips, smoothing continuity and landing, pan and pitch step scaling, prepare snapping and wide sub-blocks, parameter building, source vector. Registered in the test target.
- [x] Verified Debug build without warnings and `ctest` green.

## V3 phase 0.3: descriptor-driven slot parameters (2026-11-13)

- [x] Added `src/parameters/v3/SlotParameters.h/.cpp`: slot paths and stable IDs (`pA.g1.type`, `pA.g1.p1`.., `f6.p8`), `GenericSlotParameter` whose name, text and parsing follow the selected type's descriptor, unit formatting and parsing (Hz with kHz, ms with s, dB, semitones, cents, percent, choice), `makeSlotGroup` (type, eight generics, generator fixed parameters), `bindSlotPointers`, `readSlotTargets` and `applySlotTargets`. Added `SlotTypeTable` and `Slot::typeTable()` to `Module.h`.
- [x] Added `tests/V3ParameterTests.cpp` (four tests, minimal processor harness): ID builder, a 258-parameter layout with unique IDs and the designed counts, name and text switching with the type including round trips, pointer table to targets to `SlotBase`. Registered source and tests in CMake.
- [x] Verified Debug build without warnings and `ctest` green. Phase 0 complete.

## V3 phase 1.1: VA generator (2026-11-13)

- [x] Added `src/synth/v3/VaGenerator.h`: the V2 PolyBLEP oscillator as a slot module with pulse, triangle, saw and sine, pulse width, unison 1 to 7 with detune, spread and 1/sqrt(N) compensation, a sine sub, own level and constant-power pan, and the combine modes sync (wrap track in, PolyBLEP-corrected reset), ring, AM and FM.
- [x] Extended the slot contract: `Combine` and `SlotParams::frequencyHz` / `combine` in `Module.h`, `memoryFloats(sampleRate, maxSubBlock)`; `CombineMode` in the parameter layer is now an alias of `Combine`.
- [x] Added `tests/V3GeneratorTests.cpp` (nine tests): descriptor, tuning within one cent for every wave at three pitches, alias floor after decimation at A4 and A7 against the V2 thresholds, unison level compensation, pan and spread, sync periodicity against a free-running slave, ring and AM scaling, sub level, FM boundedness.
- [x] Verified Debug build without warnings and `ctest` green; measured tuning error at most 0.03 cents, alias -101 dB (A4) and -86 dB (A7), unison stack -0.4 dB, sub 0.0 dB.

## V3 phase 1.2: ladder processor and envelope (2026-11-13)

- [x] Added `src/synth/v3/LadderProcessor.h`: the V2 zero-delay-feedback ladder as a stereo slot module with cutoff, resonance, drive and key tracking, per-sample ramps of cutoff and feedback across the sub-block, and a resonance law that self-oscillates at the top (feedback up to 4.3).
- [x] Added `src/synth/v3/Envelope.h`: the V2 exponential ADSR with per-stage curve, velocity sensitivity, loop and one-shot.
- [x] Added `tests/V3ProcessorTests.cpp` (twelve tests): four-pole slope, passband gain, self-oscillation frequency and level, key tracking, drive harmonics and bound, extreme settings, attack landing time for three curves, curve shape, loop period, one-shot behaviour, velocity scaling, retrigger continuity. Registered in CMake.
- [x] Verified Debug build without warnings and `ctest` green; noted in the bank that Siren needs a trace of noise to start the oscillation.

## V3 phase 1.3: voice, with the generator, ladder and envelope rebuilt (2026-11-13)

- [x] Added `src/synth/v3/Voice.h/.cpp`: three generator slots at unit level with links between neighbours, a per-sample level ramp in the mixer, three processor slots, ENV1 per sample with click-free minimum times, ENV2 and ENV3 per sub-block, hardwired ENV2 and velocity to P1, pitch in semitones with constant-time glide, bend and per-voice drift, local and global modulation sources, voice pan, and a self-contained handover (1.5 ms fade of a stolen note, clean start of the waiting one, snap of every smoothed value on a fresh note). Added `OffModules.h` and `DspMath.h`.
- [x] Rewrote `VaGenerator` from first principles: continuous shape morph, event-based 4-point B-spline BLEP and BLAMP with two samples of latency, two-sided exact hard sync, FM on the modulated trajectory, JP-8000-style unison with mix and phase rand, links carrying the mono signal and a reference wrap track. Fixed a double-counted crossing when a sync reset lands exactly on the saw jump.
- [x] Rewrote `LadderProcessor`: per-stage transistor nonlinearity solved without a unit delay in two linearised passes, LP24, LP12, BP and HP12 taps level-matched at resonance, bass compensation on the low-pass taps, self-oscillation from a -110 dB floor and kept in tune above the threshold.
- [x] Rewrote `Envelope`: delay, attack, hold, decay, sustain, release with exact segment times and bipolar curves, velocity as peak, jump-free retrigger, key time tracking, loop, one-shot.
- [x] Extended the slot contract: `GeneratorLink`, `Slot::setLink`, `link` and `switchTypeNow`, `SlotParams::combineAmount`, `Skew::squared`, `SlotBase::step(n)` and combine amount, and the generator Combine Amount parameter.
- [x] Tests: generator suite rewritten (alias energy against a 2-point polyBLEP yardstick, latency, morph linearity, unison, sync, ring, AM, FM, sub, phase rand); new ladder and envelope suites; new voice suite (16 tests: pitch, offsets, levels, ring, sync, FM, ladder and ENV2 sweep, release, steal handover, type crossfade, glide and legato, sources, drift, pan, minimum amp times).
- [x] Verified Debug build without warnings and `ctest` green. Measured: saw alias 26 to 27 dB below the 2-point polyBLEP (-88.8 dB at 439 Hz), hard-synced saw -87.4 dB, ladder self-oscillation within 3.3 cents from 250 Hz to 4 kHz, steal handover step equal to the steady-state step.

## V3 phase 1.4a: part and engine (2026-11-13)

- [x] Added `src/synth/v3/Part.h/.cpp`: a pool of 16 voices at twice the host rate with fixed headroom and one decimator per part; a new allocator (a sounding key reuses its voice; then least recently used silent voice, quietest released, quietest pedal-held, oldest held; voices above the polyphony limit finish and rest); mono and legato from a note stack with last, low or high priority and return to held keys; unison of 2 to 8 voices spread in pitch and pan; glide optionally limited to overlapping keys; sustain pedal, bend with range, wheel, all notes off, all sound off, reset controllers; the existing arpeggiator with its absorb-before and release-after merge rules; blocks longer than prepared are split.
- [x] Added `src/synth/v3/EngineV3.h/.cpp`: one part, the existing effect rack, a smoothed master gain and a tanh soft clipper; mono output buffers get the sum.
- [x] Voice: retrigger and legato glide only when the part passes a note to glide from.
- [x] Added `tests/V3PartTests.cpp` (17 tests across part and engine). Verified Debug build without warnings and `ctest` green; measured one-voice peak 0.354 (headroom 0.5), legato amp held at 0.300 while mono restarts to 0.931, eight saws at +12 dB master peaking at 1.000, latency 3 host samples.

## V3 phase 1.4b: the product runs V3 (2026-11-13)

- [x] Added `src/parameters/v3/PartParameters.h/.cpp`: part A's 129 parameters (three generator and three processor slots with per-slot default types, G1 VA and P1 ladder; three DAHDSR envelopes with curves, velocity, key time, loop and one-shot; env2-to-P1, velocity-to-P1, glide and drift; play mode, priority, voices, unison count, detune and spread, bend range, legato-only glide), the pointer table and the lock-free reader into `PartSettings`.
- [x] Pruned `ParameterIDs.h` and `SynthParameters.h` to the arp, effect and output parameters and the engine event types; the layout builds part A plus those groups.
- [x] The processor renders `EngineV3`; JUCE's generic editor replaces the V2 editor (UI is throwaway until plan phase 9); the standalone Options button opens JUCE's audio and MIDI settings.
- [x] Deleted the V2 engine, voice, ladder, envelope and sound classes, the V2 editor, arp overlay, tooltips, standalone MIDI dialog and signal-chain view, and the V2 bank with its YAML and preset scripts; the factory bank is Init only until phase 1.5.
- [x] Remapped the MiniLab 3 profile to V3 IDs; MIDI learn accepts part IDs; a relative encoder on a continuous parameter now moves 1/64 per detent (it moved nothing before).
- [x] Fixed the patch and state loader to validate against every registered parameter instead of the static list, which would have dropped every part parameter; All Sound Off now also clears decimator memory and effect tails; processor state format 9, patch format 8.
- [x] Ported tests: `tests/EngineTestAdapter.h` runs the 35 arpeggiator and 10 effect-rack regressions on V3; the half-band check has its own suite; V2-only suites and the six editor tests removed; MIDI-learn and patch tests use V3 IDs.
- [x] Verified Debug build without warnings, `ctest` green (173 tests), Release standalone and VST3 built, loudness tool builds and runs on the new engine.

## V3 phase 1.5: the bank-file pipeline and the first 14 presets (2026-11-13)

- [x] Added `src/presets/BankFile.h/.cpp`: plain-text bank files, one per room, one line per slot or section, mapped to parameter IDs by descriptor key, with unknown keys, types and settings reported by file and line.
- [x] Rewrote `src/presets/FactoryPresets`: the bank files in `resources/presets` are embedded (`CoolSynthFactoryBank`) and parsed once; applying a preset resets every parameter, loads a named slot type's defaults, and lets each parameter read its value from text, reporting what it cannot read or what lies out of range.
- [x] Descriptor parameters carry option names (VA shapes, ladder modes) for bank files and the generic editor; a percent value is a fraction unless written with `%` (bare numbers were divided by 100 before, which made `res=0.2` silently 0.002).
- [x] The loudness tool reads bank entries, refuses a bank with errors, and with `--apply` writes each preset's master line back into its file; two passes converge.
- [x] The master gain starts at its setting after prepare instead of ramping from unity, which made the first notes after loading louder; the master default is now -6 dB.
- [x] Authored 14 presets: Workshop (Init Saw, Init Pulse, Init Sine, Init Arp), Studio 1982 (Prophet Brass, OB Strings, Moog Bass, Clav Keys, Sync Lead), Engine Room (Pure Sub, Donk, Fat Analog), Main Stage (Reese, Supersaw), normalised to -15 to -18.6 LUFS.
- [x] Added `tests/V3BankTests.cpp` (mapping, error reports, type defaults on apply, unreadable values, every preset sounds and stays within full scale), a clean-bank patch test and a master-priming engine test. Verified Debug build without warnings, `ctest` green, Release standalone and VST3 built.

## V3 audition editor and programs (2026-11-13)

- [x] The processor lists the factory bank as host programs named "Room: Preset"; choosing one applies it and sets the patch name.
- [x] Added the throwaway `src/plugin/AuditionEditor`: a factory preset menu grouped by room above JUCE's generic editor (phase 1.6, replaced in phase 9).
- [x] Tests: the bank is the program list and applying a program sets its parameters; the editor builds and lays out headlessly. Verified Debug build without warnings, `ctest` green, Release standalone and VST3 rebuilt.

## Fix: the previous preset leaking into new notes (2026-11-13)

- [x] Reported: after a preset change, notes for several seconds carried pieces of the previous preset, and some notes were silent for a while. Cause: a note on a silent voice started its envelopes before the voice read the new patch, so the first note on each of the 16 voices ran the previous preset's attack, curve and velocity response; coming from a slow-attack preset, short notes on fresh voices were nearly inaudible. Every note now starts inside `Voice::render()` after the patch is read.
- [x] Found with it: the generators' two-sample output delay met an envelope that had already started, so a zero-attack pulse onset stepped 0.20; the envelopes now wait for the same two samples at a fresh note (step 0.074). `generatorLatencySamples` in `Module.h` fixes that delay for every generator type.
- [x] Added a voice regression test (2 ms after a slow-to-fast patch change the note is at full level; it was at 1.1 %), a performance fuzz over the factory presets with preset switches (930 key states checked: every key has its voice, mono stacks play the priority key, nothing hangs after release), and an opt-in CPU report (`COOLSYNTH_CPU_REPORT=1`).
- [x] Measured CPU: Release stays below 36 % of real time for eight-note chords on every preset; Debug exceeds real time on Supersaw, Clav Keys, OB Strings and Prophet Brass. Verified Debug build without warnings and `ctest` green.

## Optimised Debug build (2026-11-13)

- [x] The plain Debug build was too slow to play (Supersaw 154 % of real time for an eight-note chord, Clav Keys 115 %, OB Strings and Prophet Brass about 90 % with worst blocks over 100 %), and every Debug build installs the VST3 the user plays. CoolSynth targets now use `coolsynth_config_flags` instead of JUCE's recommended config flags: Debug compiles with /O2 /Ob2 /Zi without /RTC1, JUCE's Debug assertions stay on.
- [x] Measured: optimised Debug matches Release (Supersaw 27.9 %, Clav Keys 25 %, everything else below 17 %); the test suite runs in 4 s instead of 20 s. Verified Debug build without warnings and `ctest` green.

## V3 phase 2.1: LFOs (2026-11-13)

- [x] Added `src/synth/v3/Lfo.h`: sine, triangle, saw up, saw down, square, sample-and-hold, smoothed random and a 16-level staircase; rate in Hz or one of 16 tempo divisions; start phase; retrigger; delay and straight-line fade-in; one-shot; rate scale for the matrix; song-position lock for synced free-running LFOs while the host plays.
- [x] Voices carry LFO1 and LFO2 as sources (restart on new and retriggered notes, legato keeps them, the mod wheel scales LFO1 through `pA.voice.wheelToLfo1`); the part runs GLFO1 and GLFO2 all the time, hands the voices one value per sub-block, and restarts them at the first note after silence. Tempo is the host's, else the arp's internal tempo.
- [x] 37 new part parameters (`pA.lfo1`, `pA.lfo2`, `pA.glfo1`, `pA.glfo2`, nine fields each, and `pA.voice.wheelToLfo1`) and the bank sections `lfo1`, `lfo2`, `glfo1`, `glfo2`.
- [x] Added `tests/V3LfoTests.cpp` (13 tests): shape start points and range, sample-and-hold and step counts, smoothness, cycle length at all 16 divisions at two tempos (within one sample), song-position lock, retrigger and free running, delay and fade, one-shot, rate scale, voice sources and the wheel, mono restart versus legato, the shared global LFO and its restart rule, global values inside a block, the tempo source, bank sections.
- [x] Juno Pad moved to phase 2.2 with the matrix (decision D1 in `DECISIONS_LOG.md`). Verified Debug build without warnings and `ctest` green.

## V3 phase 2.2: modulation matrix, Juno Pad, 808 Boom, Sync Lead sweep (2026-11-13)

- [x] Added `src/synth/v3/ModMatrix.h`: 24 slots of source, destination, amount, via and curve (linear, unipolar, exponential, logarithmic, S-curve) over 66 destinations (pitch, VCA, pan, LFO rates, every generator's pitch, level, pan, combine amount and P1..P8, every processor's P1..P8). Each voice evaluates the active slots once per sub-block; the VCA gain ramps across the sub-block.
- [x] 120 new part parameters (`pA.mod1` .. `pA.mod24`: src, dst, amount, via, curve); bank lines `mod` (next free slot) and `mod1` .. `mod24`, with destinations named by the slot type's keys (`g1.pw`, `p1.cutoff`) and sources by short names (`lfo1`, `env3`, `wheel`).
- [x] Presets: Studio 1982 Juno Pad (global LFO on the pulse width, chorus), Engine Room 808 Boom (one-shot sine with a 24-semitone drop in 60 ms, legato slides), the Sync Lead pitch sweep (ENV2 to the slave's pitch). All 16 presets renormalised; the masters moved 0.5 to 2 dB because the tool had not been rebuilt since phase 1.5 (decision D12).
- [x] Fixed a hung note found by the fuzz test: a one-shot envelope that ignored its note-off waited at sustain forever when a preset change turned one-shot off before its decay ended. It now releases at the end of the decay. Regression test added.
- [x] Added `tests/V3MatrixTests.cpp` (11 tests): curves, slot arithmetic and via scaling, empty slots and names, pitch transposition by amount, VCA scaling and click-free gating, filter and pan routes, LFO rate route, full modulation into every destination without non-finite output, bank routes and their errors, an applied route reaching the voice settings. The fuzz test now waits two seconds for releases and lets one-shot presets fall silent while a key is down.
- [x] Measured CPU unchanged (Supersaw 27.5 %, Juno Pad 16.2 %, 808 Boom 1.0 % for eight-note chords). Verified Debug build without warnings and `ctest` green.

## V3 phase 2.3: global envelope, step modulator, macros, chaos; Wobble, Riser (2026-11-13)

- [x] The part runs a global envelope (DAHDSR with curves and loop; trigger First Note or Every Note; synced times written as at 120 BPM) whose gate follows the played keys before the arp, the pedal and latched arp notes.
- [x] Added `src/synth/v3/GlobalModulators.h`: `StepModulator` (16 values, per-step slide with a shared glide, division, length 1..16, retrigger or song-position lock) and `ChaosSource` (a smoothed random walk at a rate). Four macros, smoothed over 10 ms. All reach the voices as one frame per sub-block, with the global LFOs.
- [x] Matrix: ENV1..3 time destinations (69 destinations now), read once per note from its velocity, key, random value and the part's sources; `Envelope::noteOn` takes a time factor.
- [x] 53 new part parameters (`pA.genv.*`, `pA.step.*`, `pA.macro1..4`, `pA.chaos.rate`); bank sections `genv` (times in ms or `8bars` / `2beats`), `step` (`values=` and `slides=` lists), `macros`, `chaos`, and `env1.time` .. `env3.time` destinations.
- [x] Presets: Engine Room Wobble (1/8 synced LFO on a resonant ladder, the wheel doubles the rate), Main Stage Riser (eight synced bars of pitch, filter, width and an accelerating pulse; the noise layer waits for 3.1). The loudness tool measures presets that route the global envelope after its rise.
- [x] Added `tests/V3GlobalModulatorTests.cpp` (10 tests): step timing and wrap, slide, song-position lock, chaos range and smoothness, first-note versus every-note trigger, the pedal holding the gate, synced times at 60 BPM, sources reaching the voices with smoothed macros, an envelope-time route stretching the attack, bank sections and their errors. Verified Debug build without warnings and `ctest` green; Riser 18 %, Wobble 2.6 % of real time for eight-note chords.

## V3 phase 2.4: delay sync and ping-pong; Outrun, Clock Tower, Pluck Drop (2026-11-13)

- [x] The delay gained Sync (14 note values, 1/32 to one bar) and Mode (Tape, Ping-Pong); the engine sets a synced time from the host tempo, else the arp's internal tempo; the line holds 4 s. Ping-pong sums to mono into the left line and cross-couples the lines per sample through the tape feedback path. Three new parameters (`delaySync`, `delayDivision`, `delayMode`) and bank keys `sync`, `division`, `mode`.
- [x] Fixed: after prepare or switching on, the delay time glided from 250 ms and the mix ramped from zero, bending the first echoes and leaking dry signal at full mix; it now starts at its settings. The tail-clearing test that relied on the leak renders long enough for the first echo.
- [x] Presets: Outrun (Neon Brass, Gated Pad, Night Drive, Laser Lead, Sunset Strings) in `03-outrun.csbank`, Clock Tower (Euclid Pulse, Ratchet Run, Step Filter, Random Walk, Polyrhythm Pad) in `10-clock-tower.csbank`, Main Stage Pluck Drop. The bank holds 29 presets, all normalised.
- [x] Added `tests/V3DelayTests.cpp` (4 tests): division lengths at three tempos, tap spacing at 1/8 and dotted 1/8 within 3 samples, ping-pong alternating left and right with falling levels and a single left echo without feedback, the bank delay keys. The fuzz test checks 1089 key states across the bank without a failure.
- [x] Verified Debug build without warnings and `ctest` green; heaviest presets for eight-note chords: Supersaw about 28 %, Clav Keys 24 %, Sunset Strings 21.5 % of real time.

## V3 phase 3.1: noise generator and its presets (2026-11-13)

- [x] Added `src/synth/v3/NoiseGenerator.h`, generator type 2: White, Pink (seven shelves designed at the running sample rate), Band (state-variable band-pass on the note, level-compensated for note and resonance), Chip (the NES 15-bit register, Long 32767 or Short 93 steps, clocked at 93 times the note, Clock in semitones, every step band-limited). Tone, Width, constant-power pan, ring and AM, two samples of latency like the VA, a distinct noise per instance. The VA's BLEP residual moved to `dsp::blepResidual`.
- [x] Presets: Workshop Init Noise; Bedroom Tape Flute (`01-bedroom.csbank`); Arcade Coin Pulse, Boss Bass, Power-Up, Static Hit (`02-arcade.csbank`); Observatory Solar Wind (`04-observatory.csbank`); Riser's noise layer. 36 presets.
- [x] The loudness tool also waits for slow amplifier attacks before measuring. The data-only libraries build without debug info.
- [x] Added `tests/V3NoiseTests.cpp` (9 tests): descriptor; every kind within 4 dB of a saw after decimation (measured -1.7 to -3.2 dB); pink within 0.67 dB per octave from 62 Hz to 16 kHz and white +3 dB per octave; band centre on the note at 440 Hz and 1760 Hz; register periods 32767 and 93; Short mode periodic at the note within 0.001; band-limited steps at a 372 kHz clock; width decorrelation and tone; distinct noise per voice. Verified Debug build without warnings and `ctest` green.

## V3 phase 3.2: two-operator FM and its presets (2026-11-13)

- [x] Added `src/synth/v3/FmGenerator.h` ("FM2", generator type 3): sine modulator at Ratio with Fine phase-modulating a sine carrier at Carrier times the note; Index, averaged Feedback, Mix; an index limit from the Bessel tail that keeps aliases out of the audible band; per-sample ramps of index, feedback and ratios; double-precision phases that restart at a fresh note; signal and carrier wrap track for the next generator; combine FM, ring, AM.
- [x] Presets: Workshop Init FM; Greenhouse Glass Bell, Rain Chime, Crystal Pluck (`05-greenhouse.csbank`); Engine Room FM Knock. 41 presets.
- [x] Added `tests/V3FmTests.cpp` (8 tests): a pure sine at index 0 (harmonics below -90 dB); sidebands at ratio 3.5 matching J1(1)/J0(1) within 0.3 dB; ratio and fine; no alias above -70 dB below 20 kHz at index 10 on a 2 kHz carrier (measured -75.4 dB); full feedback bounded with -72 dB above 40 kHz; one wrap per carrier cycle; an index jumping every sub-block without steps; combine FM. Verified Debug build without warnings and `ctest` green; Glass Bell 8.7 %, FM Knock 2 % of real time for eight-note chords.
- [x] Found and documented: the bank library's link errors come from CMake reconfiguring mid-build after a new bank file; configure first, or delete the library (TODO standing notes, decision D29).

## V3 phase 3.3: four-operator FM and Grid Keys (2026-11-13)

- [x] Added `src/synth/v3/Fm4Generator.h` ("FM4", generator type 4): four sine operators with six algorithms (stack, two into one, stack plus one, two pairs, one into three, organ), ratios and levels for operators 2 to 4, averaged feedback on operator 4, carriers normalised by their total level, FM2's alias limit on every modulation, per-sample ramps, double-precision phases, signal and wrap track for the next generator, combine FM, ring and AM.
- [x] Preset: Outrun Grid Keys, the DX electric piano on two pairs (1:1 body with velocity bark, 1:14 tine). 42 presets; Outrun complete.
- [x] Added `V3Fm4Tests` (4 tests): the routing tables; Two Pairs with one pair silent equal to FM2 within 1e-5; the organ's four partials equal within 0.1 dB; the alias limit per driven operator and every algorithm bounded at full levels and feedback. Verified Debug build without warnings and `ctest` green; Grid Keys 10.6 % of real time for eight-note chords.

## V3 phase 3.4: wavetables (2026-11-13)

- [x] Added `src/synth/v3/Wavetables.h/.cpp`: the factory bank, four tables (Classic, Digital, Vocal, Chip) of 16 frames, each frame 2048 samples at 11 octave band limits, built once from spectra with JUCE's FFT and warmed in `Part::prepare`.
- [x] Added `src/synth/v3/WavetableGenerator.h` (generator type 5): position morph with per-sample ramps, band limit per copy (harmonics under 0.75 fs), Hermite reads, unison 1 to 5 with detune and spread, phase rand, wrap track, combine FM, ring, AM.
- [x] Presets: Workshop Init Wavetable, Arcade SCC Lead (stepped pulse width through the step modulator), Observatory Nebula Pad. 45 presets; phase 3 complete.
- [x] Added `tests/V3WavetableTests.cpp` (7 tests): frame 0 is a sine within 1e-4 and every band limit keeps its band (-100 dB outside); the level choice; the A7 saw's largest alias after decimation -118.6 dB; tuning within 0.05 cents; a position sweep without steps; five unison copies within 1.5 dB of one and random start phases; bank lines. Test fixes: the unknown-type example, a longer window for random arps, and the fuzz test waiting each preset's own release.
- [x] Verified Debug build without warnings and `ctest` green; Nebula Pad 21.5 %, Init Wavetable 14.2 % of real time for eight-note chords.

## V3 phase 4.1: state-variable filter (2026-11-13)

- [x] Added `src/synth/v3/SvfProcessor.h` (processor type 2): trapezoidal SVF with LP, HP, BP and notch, resonance Q 0.5 to 40, a soft limit on the band state, drive, key tracking, per-sample ramps.
- [x] Presets: Supersaw high-passed by the SVF at 120 Hz (and 28 % down to 20 % of real time), Nebula Pad's low-pass on the SVF, Observatory Deep Field. 46 presets.
- [x] Added `tests/V3SvfTests.cpp` (4 tests): the four response shapes (-6 dB at the cutoff and 40 dB a decade beyond at Q 0.5, band-pass unity at the centre, notch below -40 dB), the resonance peak equal to Q, full resonance with full drive bounded on noise with a jumping cutoff and an impulse ring that decays, key tracking. Verified Debug build without warnings and `ctest` green.
