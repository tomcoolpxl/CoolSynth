# CoolSynth V2 — Comprehensive Code Review

**Reviewer:** Claude (Opus 4.7) — independent read-through, no execution context from prior sessions.
**Scope reviewed:** Full V2 audio path (`src/synth/*`, `src/plugin/*`, `src/parameters/*`, `src/midi/*`, `src/standalone/*`, `src/ui/SignalChainVisualizer.*`), `CMakeLists.txt`, test inventory.
**Date:** 2026-05-15
**Phase context:** V2 Phase 10 complete, Phase 11 next per `GEMINI.md`.
**Stance:** Per `GEMINI.md`, no backward compatibility constraints. Cleanest current V2 is preferred over preservation.

This review is opinionated and prioritised. Items are tagged:
- **[BUG]** — wrong/unsafe behavior, fix before release.
- **[RT]** — real-time-safety / audio thread issue.
- **[PERF]** — measurable performance cost on the audio thread.
- **[QUALITY]** — audio quality / correctness from a DSP/synth perspective.
- **[SMELL]** — code-smell, dead code, or maintainability.
- **[NIT]** — minor.

---

## 1. Executive summary

CoolSynth V2 is a JUCE-based 8-voice subtractive synth with a Prophet-flavoured feature set (dual osc + noise → SVF lowpass → dual ADSR → LFO → poly mod → arp → drive/chorus/delay/reverb → master). The architecture is clean in shape: a deterministic `SynthEngineV2` owns preallocated `SynthVoice` slots; a `GlobalFxRack` runs after voice rendering; an `Arpeggiator` lives inside the engine; an `APVTS` carries the parameter contract. MIDI Learn and standalone-vs-plugin paths are well separated. Tests give reasonable coverage of MIDI/patch/state but very thin coverage of DSP correctness.

The **biggest concerns** are, in order:

1. **[BUG][RT]** Use-after-free hazard: `SynthAudioProcessor::processBlock` calls `dynamic_cast<SynthAudioProcessorEditor*>(getActiveEditor())->getVisualizer().pushSamples(...)` directly from the audio thread. The visualizer is owned by the editor, whose lifetime is owned by the message thread. (`src/plugin/SynthAudioProcessor.cpp:257-258`)
2. **[BUG][RT]** Visualizer's FFT staging buffer has a data race between audio writer and UI reader, plus a `static int` local that breaks multi-instance plugin sessions. (`src/ui/SignalChainVisualizer.cpp:73-82`, `:165-191`)
3. **[BUG][RT]** `PluginMappedActionDispatcher` calls `setValueNotifyingHost()` from a worker thread; this is known to cause issues in some hosts (notably FL Studio). (`src/plugin/SynthAudioProcessor.cpp:603-611`, `920-929`)
4. **[QUALITY]** No oscillator anti-aliasing. Naïve saw/pulse/triangle aliases brutally above ~2 kHz fundamentals — unacceptable for a release-quality VA synth. (`src/synth/SynthVoice.cpp:441-465`)
5. **[PERF]** Voice render does 3-4× `std::pow(2,…)`, 1× `std::tanh`, 1× `std::sin` (sine osc + sine LFO), and `setCutoffFrequency()`/`setResonance()` (each calls `std::tan` internally in JUCE's TPT filter) **per sample, per voice**. At 48 kHz × 8 voices that is roughly 8-12 expensive transcendentals × 384 k = a multi-million-op/s hot path that begs for caching/blocking.
6. **[QUALITY]** Hard sync is "phase reset on osc B wrap" — produces an audible alias bomb at high pitches.
7. **[SMELL]** Significant dead V1 code (`SynthEngine`, `BlockRenderParameters`, `ParameterValuePointers`, `WaveformChoice`) still compiles into the plugin contrary to the project's "no V1 compat" stance.

The remainder of the codebase (MIDI subsystem, APVTS wiring, FX rack structure, arp logic, voice allocator) is competent and largely correct. Lifecycle handling (`reset`, `panic`, tail estimation) is thoughtful. Test discipline around MIDI learn / patch state is good.

---

## 2. Architecture & repo organisation

### 2.1 Module layout

```
src/
  parameters/     APVTS layout + parameter IDs + V2 enums
  synth/          SynthEngineV2, SynthVoice, GlobalFxRack, GlobalDelay, Arpeggiator
                  + legacy SynthEngine (V1)  ← dead code
  plugin/         SynthAudioProcessor(.cpp/.h/Editor.cpp/.h)
  midi/           MidiLearn, MidiMappingEngine, ControllerProfile, Minilab3Profile, MidiMonitor
  standalone/     StandaloneApp, AudioSupport, MidiInput, SettingsStore
  ui/             Knobs, toggles, choice groups, sections, panels, visualizer
  presets/        PatchState (wrapped XML I/O)
```

Verdict: **clean and proportionate**. The boundary between `synth/` (audio-thread DSP), `plugin/` (host glue), `midi/` (controller domain), and `ui/` (presentation) is respected.

### 2.2 Header dependency graph

- `SynthEngineV2` includes `SynthVoice`, `GlobalFxRack`, `Arpeggiator`, `SynthParameters` — sensible.
- `SynthAudioProcessor` includes `MidiMappingEngine` and `SynthEngineV2` — sensible.
- **[SMELL]** `SynthAudioProcessor.cpp:9` `#include "SynthAudioProcessorEditor.h"` only to enable the audio-thread `dynamic_cast` in `processBlock`. This pulls the entire editor header (and transitively most UI headers) into the audio-processor TU. Removing the editor-cast (see §3.1) would let the processor drop this include, shorten compile, and break a circular ownership concept.

### 2.3 Build system (`CMakeLists.txt`)

- **[OK]** C++20, JUCE submodule, single `juce_add_plugin` target with Standalone+VST3, custom binary-data target for controller-profile JSON + logo.
- **[SMELL]** Lines 63 and 129 still list `src/synth/SynthEngine.cpp`. No reference in production code (verified via grep) — only `tests/StabilityAndDisconnectTests.cpp` and the legacy header reference it. Per `GEMINI.md` no-back-compat stance, this should be deleted.
- **[NIT]** The test target re-compiles 27 source files duplicating the plugin target — fine for a small project but slow. A `juce_add_module`-style internal library would deduplicate.
- **[SMELL]** `JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1` hides a real signal: the inconsistent `legacyVersionHint`/`v2VersionHint` mix (§5.1) is exactly what that warning catches. Resolve the inconsistency rather than silence the warning.

### 2.4 Documentation set

There are 15 phase docs plus `DESIGN`, `DESIGN_V2`, `REQUIREMENTS`, `REQUIREMENTS_V2`, `IMPLEMENTATION_PLAN(_V2)`, `TODO`, `DONE`, `AGENTS`, `GEMINI`, `CLAUDE`, `CC_MIDI_REFACTOR`, `NEW_UI_DESIGN_AND_IMPLEMENTATION_PLAN`. That's a lot of overlap; the `GEMINI.md` itself flags `REQUIREMENTS.md`, `DESIGN.md`, `IMPLEMENTATION_PLAN.md` as authoritative. **[NIT]** Consider archiving the per-phase files into a `docs/history/` folder after V2 release to make the root cleaner.

### 2.5 Tests

```
tests/MidiLearnTests.cpp                606 lines
tests/PatchStateTests.cpp               371 lines
tests/StabilityAndDisconnectTests.cpp  2188 lines  (catch-all for V2 engine tests)
```

**[SMELL]** The 2188-line "stability and disconnect" file is the catch-all for V2 engine tests (`V2Performance`, `V2Arpeggiator`, sustain, panic, etc.). Per the docs many tests live here. Splitting per subsystem (`V2EngineTests.cpp`, `V2ArpeggiatorTests.cpp`, `V2VoiceTests.cpp`, `V2FxRackTests.cpp`) would make CI failures easier to triage and let `StabilityAndDisconnectTests.cpp` focus on its original mandate.

**[GAP]** No tests for: oscillator output spectrum/aliasing, filter stability at extreme cutoffs, FX rack bit-exactness with `mix=0`, master-gain ramp behaviour, drive normalization. Given V2 is approaching release, at least one "render N samples → assert finite + bounded" test per DSP block would catch regressions cheaply.

---

## 3. Latency, real-time safety, and threading

A subtractive synth has two latency budgets: (a) host buffer pass-through (one block) and (b) plugin-internal added latency. CoolSynth reports `getLatencySamples() = 0` (default — `setLatencySamples` is never called). That's correct: there is no internal oversampling or lookahead. Good.

But **RT-safety** is more nuanced than latency, and there are real issues.

### 3.1 [BUG][RT] Audio thread reaches into editor (use-after-free hazard)

`src/plugin/SynthAudioProcessor.cpp:257-258`:

```cpp
if (auto* editor = dynamic_cast<SynthAudioProcessorEditor*>(getActiveEditor()))
    editor->getVisualizer().pushSamples(buffer.getReadPointer(0), buffer.getNumSamples());
```

Problems:

- **Lifetime:** `getActiveEditor()` returns a pointer that the message thread is free to invalidate. JUCE's official guidance (forum: "Is it safe to call getActiveEditor() in processBlock?") is *do not* let the processor hold or use an editor pointer — the editor can be deleted at any time without warning. The check + dereference is racy.
- **`dynamic_cast` on the audio thread:** RTTI walks the class hierarchy; cheap on flat hierarchies but never zero, and not allocation-free in all implementations.
- **Cross-thread coupling:** the visualizer is conceptually a UI concern; the processor shouldn't know it exists.

**Fix (recommended):** invert the dependency. The processor owns a small lock-free FIFO of recent samples (e.g. `juce::AbstractFifo` over a fixed `std::array<float, 4096>`). The editor's timer pulls from it. The processor never touches the editor. This is the JUCE-canonical pattern for visualisations (per JUCE forum threads on lock-free queues for visualization).

### 3.2 [BUG][RT] Visualizer FFT staging is racy

`src/ui/SignalChainVisualizer.cpp:73-82`:

```cpp
static int fftWriteIdx = 0;        // <- static local!
if (!nextFFTBlockReady.load()) {
    fftData[fftWriteIdx++] = val;
    if (fftWriteIdx >= fftSize) {
        fftWriteIdx = 0;
        nextFFTBlockReady.store(true);
    }
}
```

Then in `updateSpectralData()` (UI timer, `:165-191`):

```cpp
if (nextFFTBlockReady.load()) {
    window.multiplyWithWindowingTable(fftData, fftSize);            // (a) MUTATES fftData
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);       // (b) MUTATES fftData
    // ...read & build path...
    nextFFTBlockReady.store(false);                                  // (c) reopen for writer
}
```

Issues:
1. **Static local breaks multi-instance.** If a host opens two plugin instances, both share the same `fftWriteIdx`. Make it a member field.
2. **Data race on `fftData`.** Audio thread reads `nextFFTBlockReady=false` and starts writing `fftData[0..]` while the UI thread is still inside `multiplyWithWindowingTable` or `performFrequencyOnlyForwardTransform`. The two ops in (a)/(b) take many microseconds. The correct protocol is the standard double-buffer: the audio thread fills buffer A and flips a flag; the UI thread copies (or swaps pointers to) buffer B and processes B; B is independent of A.
3. **Scope FIFO write-then-publish ordering.** `writeIndex.store((idx+1) % fifoSize)` is published with default `memory_order_seq_cst`, which is correct but the per-sample fence is gratuitous. `memory_order_release` paired with `memory_order_acquire` in the reader is the standard idiom and may be slightly cheaper.

### 3.3 [BUG][RT] `setValueNotifyingHost` from worker thread

`SynthAudioProcessor::PluginMappedActionDispatcher` (lines 908-929) spawns a worker that calls `applyMappedAction → applyParameterChange`:

```cpp
change.parameter->beginChangeGesture();
change.parameter->setValueNotifyingHost(change.normalizedValue);
change.parameter->endChangeGesture();
```

Per JUCE forum threads ("setValueNotifyingHost() within worker threads"), this is known to misbehave in some DAWs (FL Studio specifically reported). The message thread is the intended caller. The current standalone path correctly routes through `AsyncUpdater` → message thread, but the plugin path bypasses this.

**Fix:** in the plugin, route the drained mapped events to the message thread via `juce::MessageManager::callAsync(...)` or a dedicated `juce::AsyncUpdater` member, the same way the standalone path does. The worker thread becomes unnecessary.

### 3.4 [RT] AbstractFifo write loses events on full queue

`src/plugin/SynthAudioProcessor.cpp:511-528` (and the mirror at `:530-547`):

```cpp
pendingPluginControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);
if (size1 > 0) { ... } else if (size2 > 0) { ... }
// else: event silently dropped
```

128-slot queue is generous, but the silent drop is a latent bug. **[NIT]** Add a debug assert or a counter incremented on drop so it's visible during stress.

### 3.5 [RT] Mapping engine `mutable` state under lock — OK

`MidiMappingEngine::translate()` writes to `mutable TakeoverState` / `initialHardwareValue` / `initialSoftwareValue`. This is acceptable because every caller (plugin worker, standalone msg-thread handler, processor's `handleStandaloneControllerEvent`) takes `midiMappingStateLock` first. **[NIT]** Add a comment on the class stating "translate() may only be called under `midiMappingStateLock`".

### 3.6 [OK] Engine event timing & arp interleave

`SynthEngineV2::render` correctly merges incoming MIDI events and arp-generated events by `sampleOffset` with a peek-min interleave loop (lines 128-174), advances the global LFO phase per span, and resolves coincident arp note-off-before-note-on. Voice spans are tight. The block-level FX rack runs after voice mixing. This part is well-designed.

### 3.7 [OK] Engine prepare/release lifecycle

`prepare()` resets master gain ramp, voices, FX rack and arp; `reset()` panics and clears keyboard state; `setStateInformation()` rejects bare APVTS, wrong version, wrong state type, partial parameter sets — strict and good. Tail estimation is parameter-driven via `GlobalFxRack::estimateTailLengthSeconds`. Solid.

---

## 4. Audio / DSP correctness

This section is where the most release-blocking quality issues sit.

### 4.1 [QUALITY] Naïve oscillators — no anti-aliasing

`src/synth/SynthVoice.cpp:441-465` — `renderWaveSample` is a textbook **naïve** trivial oscillator:

```cpp
case pulse:    return phase < pw ? (1 - pw) : -pw;     // DC-corrected, good — but aliasing
case triangle: return phase<0.5 ? 4p-1 : 3-4p;          // not bandlimited
case saw:      return 2p - 1;                            // not bandlimited
case sine:     return std::sin(2π * phase);              // clean
```

At a fundamental of 2 kHz the saw's 5th harmonic (10 kHz) is still inside Nyquist (24 kHz at 48 k SR) but the 12th (24 kHz) is exactly at Nyquist — every harmonic above that folds back as audible aliases. Holding any note above ~A5 (~880 Hz) on saw or pulse produces audibly metallic, inharmonic noise. For a Prophet-style VA synth this is the single biggest audio-quality gap.

**Fix:** PolyBLEP (polynomial bandlimited step). It is the standard, low-cost solution in modern VA synths (see Martin Finke's "Making Audio Plugins Part 18" and the CCRMA virtual-analog references). Applies to saw and pulse trivially; triangle is integrated PolyBLEP square. Cost is roughly 2-3 extra branches per sample per osc — small relative to the existing per-sample workload.

Hard sync (line 274) makes the situation worse: phase-resetting on osc B's wrap creates a step discontinuity at an arbitrary moment, which is exactly the alias-generating event PolyBLEP is designed to mask. A proper PolyBLEP-aware hard-sync writes the BLEP residual at the reset point.

**[QUALITY-related]** The visualizer's `SignalChainVisualizer::updateIdealWaveforms` uses the same naïve formulae for the SOURCE/FILTER preview — that's fine for the cartoon, but conceptually highlights that the same "ideal" math is being used as the audio path.

### 4.2 [PERF] Per-sample transcendentals in the voice render hot path

`SynthVoice::renderNextBlock` (lines 218-325), inner per-sample loop, calls (for **each** sample of each active voice):

| Location | Call | Cost |
|---|---|---|
| 228 | `std::pow(2, glideOffsetLog2)` | ~50-100 cy |
| 241 | `std::pow(2, lfoPitchSemitones/12)` | ~50-100 cy |
| 262 | `std::pow(2, oscATotalPitchSemis/12)` | ~50-100 cy |
| 295 | `std::pow(2, cutoffModulationTotal/12)` | ~50-100 cy |
| 233 | `std::sin(...)` (if LFO is sine) | ~30 cy |
| 274/391/459 | `std::sin(...)` (if oscA or oscB is sine) | ~30 cy each |
| 288 | `std::tanh(normalized * overloadDrive)` | ~40 cy |
| 300 | `lowPassFilter.setCutoffFrequency(...)` → internal `std::tan` | ~40 cy |
| 301 | `lowPassFilter.setResonance(...)` | recomputes |
| 303 | `lowPassFilter.processSample(0, ...)` | TPT — cheap |

At 48 kHz × 8 voices that is ~3.8 million expensive transcendental ops per second from this loop alone. The KVR-DSP consensus and JUCE forum guidance is *don't do this per-sample*; standard tactics:

- **Block-rate vs sample-rate split.** Per-block: compute pitch bend ratio, vintage ratio, key-tracking ratio, base cutoff. Per-sample: only the truly per-sample modulations (LFO, env, poly-mod). Combine in log-space (sum semitones) then take **one** `exp2` per sample, not 3-4 `pow`s.
- **`std::exp2`/`std::ldexp` are cheaper than `std::pow(2, x)`** in most STLs.
- **Sub-rate modulation.** LFO updates every 32-64 samples with linear interp loses no perceptible quality.
- **Filter coefficient updates** — `setCutoffFrequency` recomputes `tan(π·f/fs)`. JUCE's docs note the TPT is "designed for fast modulation" but you can still smooth in tan-space (the `g` parameter) cheaper than re-`tan`-ing every sample.

This is **not a "works on my machine" issue**: low-power laptops, host buffer underruns at 64-sample buffers, and 8-voice unison stacks (the spec target) will all stress this. Expected speedup from blocking + log-space combine + per-32-sample modulation refresh is 3-5× on the voice path.

### 4.3 [QUALITY] Filter mod-ratio dynamic range

Line 293: `envCutoffSemis = filterEnvValue * envelopeAmount * 84.0f` — 7 octaves of envelope-driven cutoff. Line 291: poly-mod cutoff is `… * 60.0f` (5 octaves). Line 292: LFO `… * 60.0f`. Line 294: velocity `… * 24.0f`. Sum can hit 18+ octaves of modulation before the smoothed base is clamped by `clampCutoffToPreparedRange` (line 300/207). The clamp is correct (final cutoff stays sane) but the **internal `ratio` exponent** (line 295-297) can be enormous; `std::pow(2, huge/12)` produces large floats that are immediately clamped, which is wasteful and risks one-sample overshoot. Sum the semitone modulation first, clamp to a sensible range (e.g. ±10 octaves), THEN exponentiate.

### 4.4 [QUALITY] Resonance Q range

Line 484-490: `qMin = 1/√2 ≈ 0.707, qMax = 25.0, mapping = r^3`. A Q of 25 at low cutoff is on the edge of self-oscillation for the SVF TPT. With no resonance compensation (no input attenuation as Q rises) the filter will pile up energy and clip downstream. The downstream `applyMasterGain` polyphonyHeadroom of 0.35 (line 657) is a hack-around for exactly this kind of issue. A standard fix is to attenuate filter input by `1/√Q` or similar so the resonance peak doesn't blow the headroom.

### 4.5 [QUALITY] Master gain "polyphony headroom" magic constant

`SynthEngineV2.cpp:657`:

```cpp
const float polyphonyHeadroom = 0.35f;
masterGainLinear.setTargetValue(targetLinearGain * polyphonyHeadroom);
```

This silently multiplies the user's master gain dB by 0.35 (≈ -9 dB). That means the user's "0 dB" knob is actually -9 dB. It's a band-aid for §4.4 and for naïve oscillator addition without per-voice compensation. Either:

- Document this as a calibration choice (a comment plus the dB parameter range now meaning "-9 dB to -69 dB" in practice), or
- Replace with **per-voice** scaling (`1/√N` for incoherent mix) where N is the active voice count, dynamically — which would also retain headroom under mono play and not waste it.

Also: this constant should not live inside `applyMasterGain` as a magic literal. Hoist to `SynthParameters.h` next to `defaultVoiceCount`.

### 4.6 [QUALITY] Unison vintage floor overrides user control

`SynthEngineV2.cpp:568`:

```cpp
const float unisonVintage = juce::jmax(parameters.performance.vintageAmount, 0.35f);
```

If the user sets Vintage to 0 in Unison mode, they still get 0.35. That's surprising — users will tweak Vintage to 0 expecting it to be 0. Either:
- Hide/lock the Vintage knob to "≥35% in Unison" in the UI with a tooltip, or
- Use a different mechanism (e.g. internal "unison detune" parameter independent of Vintage), or
- Remove the floor and accept that perfect-detune unison is what the user asked for.

### 4.7 [QUALITY] Drive normalizer recomputed per sample

`GlobalFxRack.cpp:25-33`:

```cpp
const float wet = std::tanh(input * preGain);
const float normalizer = std::tanh(preGain);   // <-- block-constant!
return wet / normalizer;
```

`preGain` is fixed for the entire block (it's derived from `parameters.amount` once). Therefore `std::tanh(preGain)` is constant across the block but is recomputed N×blockSize times. Hoist out of the inner loop. Cheap win.

### 4.8 [QUALITY] Drive/chorus/reverb/delay parameters not smoothed

`GlobalFxRack::processDrive` reads `mix` and `preGain` directly (lines 122-126). `processChorus` calls `chorus.setRate/setDepth/setMix` once per block (lines 159-163). When the user moves these knobs there will be **block-edge jumps** = audible zipper noise or clicks, especially Drive mix and Delay mix at large changes. The delay smoother is internal to `GlobalDelay` (good); the others need the same treatment.

Add `juce::SmoothedValue` for at least: `driveMix`, `drivePreGain`, `chorusMix`, `reverbWetLevel`/`dryLevel`. Block-rate `setTargetValue` is fine; ramp time ~10-20 ms.

### 4.9 [QUALITY] Pink noise is OK but bounded oddly

`SynthVoice::nextNoiseSample` (line 425-430) implements the Paul Kellet pink-noise filter. The coefficients are correct. **[NIT]** Line 430: `juce::jlimit(-1.0f, 1.0f, pink * 0.2f)` — the 0.2 scale brings RMS near unity and the limit defends against rare excursions. The constant 0.2 deserves a comment ("approximate Kellet pink normalisation").

### 4.10 [QUALITY] Note-start ramp + start-phase randomisation

`SynthVoice::computeNoteStartRampSamples` (line 360-383) plus the smoothstep (`consumeNoteStartRamp` line 403-414) plus `resetVoiceState` randomising phase (line 513-522) is a thoughtful mitigation for the "dense note-start click" mentioned in `GEMINI.md`. Good. The ramp duration adapts to pulse/sync/noise content. **[NIT]** Magic literals `0.8f, 0.5f, 1.2f, 0.4f` in `computeNoteStartRampSamples` lack rationale comments.

### 4.11 [QUALITY] Pitch bend & LFO use `std::pow`, not `juce::dsp::FastMathApproximations`

JUCE ships `FastMathApproximations::exp` etc.; an ad-hoc cents-to-ratio is `std::exp2(cents/1200)` not `std::pow(2.0f, cents/1200.0f)`. Compilers can sometimes special-case `pow(2.0f, …)` but it's not guaranteed.

### 4.12 [OK] Pitch bend range, MIDI conversions

The pitch-bend mapping in the processor (line 88-90) `(value - 8192) / 8192` is correct; clamp at ±1 is sane. The engine multiplies by `parameters.performance.pitchBendRangeSemitones` clamped 1..24. Correct.

### 4.13 [OK] Arpeggiator timing

The arpeggiator handles host BPM/PPQ alignment, internal-clock fallback, gate length, cross-block ringing notes, latch, panic and pattern walks (Up/Down/Up-Down/As-Played). The cross-block carry of `samplesUntilNextStep` and `ringingNotes` is correctly handled. `std::sort` of a stack `std::array<HeldEntry, 16>` inside `pickNextPatternNote` is RT-safe (bounded, no heap). Solid.

### 4.14 [BUG-minor] Arp first-step alignment edge case

`Arpeggiator.cpp:188-189`:

```cpp
if (rawIndex - nextStepIndex > ppqEpsilon)
    nextStepIndex = std::floor(rawIndex) + 1.0;
```

If you're *exactly* on a step boundary (`rawIndex == floor(rawIndex)` to within epsilon), this keeps `nextStepIndex == floor(rawIndex)` and the step fires at offset 0 — correct. But the `> ppqEpsilon` test means "if we are slightly past the boundary, wait for the next one". With `ppqEpsilon = 1e-9` and a typical block of 256 samples at 120 BPM, this is fine. **[NIT]** Add a one-line comment explaining the epsilon's purpose.

---

## 5. Parameter system

### 5.1 [SMELL] Mixed `legacyVersionHint=1` / `v2VersionHint=2` is inconsistent

`ParameterLayout.cpp` uses `makeLegacyParameterId` for: `filterCutoffHz`, `filterResonance`, `ampAttackMs/Decay/Sustain/Release`, `delayTimeMs/Feedback/Mix`, `masterGainDb`. Everything else is V2.

Given `GEMINI.md` says V2 explicitly breaks V1 patch/state compat, the legacy version-hint preservation has no functional value — the wrapped patch format already rejects V1 payloads (`SynthAudioProcessor.cpp:279`). The version-hint mix only exists to keep VST3 parameter IDs stable for V1-era automation that the project has declared irrelevant.

**Fix:** flip all to `v2VersionHint`. Drop `JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING`. Cleaner contract, no real loss.

### 5.2 [SMELL] Defence-in-depth clamping is duplicated

`SynthAudioProcessor::makeBlockRenderParameters` (lines 737-823) does `juce::jlimit(min, max, …)` on every parameter even though the `NormalisableRange` in the parameter layout already enforces the same min/max. The clamps are belt-and-suspenders; the cost is a tiny CPU hit and a maintenance hazard (two sources of truth for "what's the min cutoff"). Either:
- Trust the APVTS range and remove the redundant clamps, or
- Replace the parameter-side range with a wide range and rely solely on the processor clamps.

Pick one source of truth.

### 5.3 [SMELL] `ParameterValuePointersV2` is a 70-field struct

It's an explicit map from parameter ID to atomic float pointer. Works, but it's ~140 LoC of bookkeeping in `SynthParameters.h`. A `std::unordered_map<std::string, std::atomic<float>*>` would be RT-unfriendly, but a `constexpr std::array` of `{name, member-pointer}` pairs would let `bindParameterPointers` be a loop and would let you guarantee at compile time that every parameter in `allParameterIds` is bound. As of today the loop in `bindParameterPointers` (lines 826-900) is hand-written and could silently miss a parameter if someone adds one to `allParameterIds` without updating the binder.

### 5.4 [OK] Sanitised parameter-state restore

`buildSanitizedParameterStateTree` (line 347-414) requires all expected parameter IDs to be present, rejects duplicates and unknown IDs gracefully, and replaces state atomically. Combined with the wrapped-format check this is robust.

### 5.5 [QUALITY] Some defaults are unusual

- `filterCutoffHz` default 3200 Hz, `filterResonance` 0.08, `filterEnvAmount` 0.35, `filterSustain` 0.12 — yields a moderately bright, slightly-decaying patch. OK.
- `ampSustain` 0.72 — fine.
- `oscBFineCents` 4.0 (slight detune by default) — good for "thick dual saw" out of the box.
- `masterGainDb` default -12 dB — combined with the 0.35 polyphony headroom (§4.5) that's effectively about -21 dB on the bus. Users will perceive the synth as "quiet". Pick one approach; don't stack both.

---

## 6. MIDI subsystem

### 6.1 [OK] Reserved CC discipline is consistent

`isReservedSynthControllerNumber` (mod wheel/sustain/all-sound-off/reset-controllers/all-notes-off) is checked at every entry point: plugin MIDI ingest, plugin mapped-action dispatch, standalone handler, MIDI Learn capture. Good consistency.

### 6.2 [OK] Soft takeover algorithm

`MidiMappingEngine.cpp:33-79` implements first-touch + linear scaling + endpoint latch. The 0.05 threshold is tuneable. The `mutable` per-binding state is consistent with the design (single-translate-at-a-time under lock). This is solid.

### 6.3 [OK] Factory-profile shadowing by learned bindings

`isFactoryBindingShadowedByLearnedTarget` / `…BySignature` (lines 227-250) — learned bindings dominate the factory profile both by parameter target and by CC signature. This is the right precedence and is documented in `GEMINI.md`. Good.

### 6.4 [OK] Patch-state file write atomicity

`PatchState::writePatchFile` uses `juce::TemporaryFile` + `overwriteTargetFileWithTemporary` (lines 50-66) — proper atomic replace, fixed the Windows-replace bug per `GEMINI.md` Phase 7 note.

### 6.5 [BUG][SMELL] Plugin and standalone enqueue paths duplicate

Both `enqueuePluginControllerEvent` and `StandaloneMidiInputController::enqueueControllerEvent` are near-identical `MidiMessage` → `ControllerMidiEvent` translators. The actual conversion is also independently duplicated in `makeControllerMidiEvent` (lines 29-60 of the processor). Three implementations of the same mapping is a smell — diff drift will happen. Extract once into `midi/MidiToControllerEvent.h`.

---

## 7. UI thread / editor / visualizer

### 7.1 [BUG] Static local in `pushSamples`

`SignalChainVisualizer.cpp:73` `static int fftWriteIdx = 0;` — see §3.2. Move to instance field.

### 7.2 [BUG] FFT data race

See §3.2.

### 7.3 [SMELL] Reality path width is "approximate"

`SignalChainVisualizer.cpp:149,173`: `const float w = (getWidth() - 40) / 5.0f; // Approximate`. The actual pane width math (with the new 4:3 layout) is now `(getWidth() - 4 - 32) / 5`. The 40-magic-number predates the recent layout change and slightly mispositions the SPECTRA and REALITY paths. Cosmetic.

### 7.4 [NIT] `SignalChainVisualizer::updateIdealWaveforms` recomputes per-pixel `std::pow(2,…)` indirectly

The pixel formulas are fine (it's a 30 Hz UI update on a tiny pane), but `std::pow` shows up in waveform synthesis at draw time. UI side, no audio impact. Skip.

### 7.5 [OK] Editor structure

`SynthAudioProcessorEditor` wires 67 controls + 1 fader, tracks MIDI-learn arming visuals, supports factory-profile + learned-binding context menu, has tooltip wrapping, build-info footer, and exposes a clean Visualizer accessor. The 30 Hz timer drives value-display refresh and MIDI-learn polling. **[NIT]** The constructor is ~750 lines, mostly because tooltips are inlined. A `Tooltips.cpp` table would slim the constructor considerably and keep the tooltip copy together for proofreading.

---

## 8. Magic numbers — consolidated list (high-impact only)

| Where | Value | Should be |
|---|---|---|
| `SynthEngineV2.cpp:657` | `polyphonyHeadroom = 0.35f` | Named constant + comment, or replaced with per-voice scaling |
| `SynthEngineV2.cpp:568` | `unisonVintage floor = 0.35f` | Documented or removed (§4.6) |
| `SynthEngineV2.cpp:10` | `maxVintageDriftCents = 25.0f` | OK as is; comment "≈ ±25 cents max drift" |
| `SynthVoice.cpp:286` | `overloadDrive = 1 + max(0, totalLevel - 1) * 1.75` | Comment explaining the 1.75 |
| `SynthVoice.cpp:291` | poly-mod cutoff scale `* 60.0f` | Express as `polyModCutoffRangeOctaves = 5` |
| `SynthVoice.cpp:293` | env cutoff `* 84.0f` | Express as `envCutoffRangeOctaves = 7` |
| `SynthVoice.cpp:240` | LFO pitch `* 12.0f` | Express as `lfoPitchRangeSemitones = 12` |
| `SynthVoice.cpp:487` | `qMax = 25.0f` | Named + comment + consider input attenuation (§4.4) |
| `SynthVoice.cpp:367-378` | note-start-ramp extras `0.8f, 0.5f, 1.2f, 0.4f` ms | Named constants with rationale |
| `GlobalFxRack.cpp:9-13` | chorus/drive/tail constants | Already top-of-file — OK, but add unit suffixes |
| `GlobalDelay.cpp:11` | `maxDelaySamples = ceil(sr*1.0) + 4` | Use `maxDelayTimeMs` not literal 1.0 |
| `SynthVoice.cpp:430` | pink noise `* 0.2f` | Comment "Kellet pink normalisation" |
| `SynthAudioProcessor.cpp:646` | feedback clamp 0.85 | Reference `GlobalDelay::maxFeedback` instead of re-literal |

---

## 9. Dead code

- `src/synth/SynthEngine.cpp` + `.h` — V1 engine; only referenced by `tests/StabilityAndDisconnectTests.cpp` and docs.
- `synth::BlockRenderParameters` + `synth::ParameterValuePointers` + `synth::FilterParameters` + `synth::DelayParameters` (V1 versions) in `SynthParameters.h` lines 21-56.
- `coolsynth::parameters::WaveformChoice` enum + `SynthVoice::setWaveform` method (used only by the legacy mapping).
- Likely dead V1 helpers inside the test file that exercise the V1 path.

Per `GEMINI.md`'s "no back-compat" stance, delete and let the legacy tests die or be ported.

---

## 10. Latency-budget summary

| Source | Samples |
|---|---|
| Internal oversampling | 0 |
| Lookahead | 0 |
| MIDI-to-audio block latency | host buffer (typically 64-512) |
| Visualizer audio→UI delay | irrelevant (UI only) |
| Plugin reported `getLatencySamples()` | 0 (default) |

**Tail length** (`SynthAudioProcessor::getTailLengthSeconds`) reads APVTS atomics → caps at 48 s for feedback-delay-active, 12 s for reverb-active, ≈ `delayTime + 250 ms` for non-feedback delay. Plausible.

The synth has **no added latency**. Good. The only latency concern is whether the per-sample CPU cost forces the host to choose larger buffers — see §4.2.

---

## 11. Test gaps

Recommended additions (cheap, high-value):

1. **Aliasing assertion** — render 1 s of A4 saw, FFT, assert no harmonic energy above Nyquist - 50 Hz. Currently this would fail (§4.1).
2. **Filter stability** — sweep cutoff 20 Hz → 18 kHz with Q=25, assert peak abs sample < 4.0.
3. **FX rack idempotence** — render with all FX disabled, assert byte-equal to dry render.
4. **Master gain ramp** — start at 0 dB, drop to -60 dB on sample 1, assert smooth ramp not step.
5. **Drive mix smoothing** — toggle drive mix 0 → 1 between blocks, assert no sample-to-sample discontinuity > X. (Would currently fail per §4.8.)
6. **Visualizer multi-instance** — instantiate two visualizers, push samples into both, assert FFT state independent. (Would currently fail per §3.2.)

---

## 12. Recommended Phase 11 priorities

If I had to choose what to do *before* a public V2 release, in order:

1. **Fix audio-thread → editor reach** (§3.1). One-day change. Removes a real crash hazard.
2. **Fix visualizer FFT data race + static-local** (§3.2). Half-day change.
3. **Route plugin mapped-action dispatch through the message thread** (§3.3). One-day change.
4. **Add PolyBLEP to saw and pulse, integrated PolyBLEP for triangle** (§4.1). One- to two-day change. This is the single biggest perceptual improvement.
5. **Block-rate vs sample-rate split + log-space pitch combine + sub-rate LFO/env** (§4.2). Two- to three-day change. Halves CPU cost.
6. **Hoist drive normalizer + add SmoothedValue to drive/chorus/reverb mix params** (§4.7, §4.8). Half-day change.
7. **Delete V1 dead code, flip all parameter version hints to V2, remove the VST3-ID-mismatch ignore** (§5.1, §9). Half-day cleanup.
8. **Replace the global `polyphonyHeadroom = 0.35` band-aid with per-voice scaling or proper documented headroom** (§4.5).
9. **Split `StabilityAndDisconnectTests.cpp` and add DSP-correctness tests** (§2.5, §11).
10. **Document the unison vintage floor or remove it** (§4.6).

After these, the synth would be both faster and audibly substantially better, with no API/compat impact since V2 has not shipped.

---

## 13. Things that are genuinely good and worth preserving

- Clear ownership: engine owns voices/arp/FX; processor owns engine; editor is independent.
- Wrapped-patch format with strict version + state-type validation.
- `juce::AbstractFifo` used (mostly) correctly for SPSC plugin/standalone CC fan-out.
- Arpeggiator's host-sync + internal-clock fallback + cross-block ringing is well thought out.
- Voice allocation: release-first stealing, oldest-held fallback, sustain-pedal lifecycle, mono-priority retrigger from held set — Prophet-faithful.
- Note-start de-click ramp + per-voice phase randomisation — proper engineering for a known artefact.
- Tail estimation is parameter-driven, not a fixed worst case.
- Test coverage of MIDI-learn capture, factory-profile shadowing, patch save/load round-trip is genuinely thorough.
- Standalone vs plugin separation (CC bridging via FIFO, AsyncUpdater on the message thread) is correct in the standalone path.

---

## Sources consulted

- [JUCE forum — Is it safe to call getActiveEditor() in processBlock?](https://forum.juce.com/t/is-it-safe-to-call-getactiveeditor-in-processblock/49529)
- [JUCE forum — Thread safe way to pass an object from the plugin editor to the processor](https://forum.juce.com/t/thread-safe-way-to-pass-an-object-from-the-plugin-editor-to-the-processor/24093)
- [JUCE forum — Lock-free queues and visualization of data](https://forum.juce.com/t/lock-free-queues-and-visualization-of-data/20659)
- [JUCE forum — setValueNotifyingHost() within worker threads](https://forum.juce.com/t/setvaluenotifyinghost-within-worker-threads/35158)
- [JUCE forum — Is it OK to call beginChangeGesture() on the audio thread?](https://forum.juce.com/t/is-it-ok-to-call-audioprocessorparameter-beginchangegesture-on-the-audio-thread/36648)
- [JUCE docs — StateVariableTPTFilter](https://docs.juce.com/master/classjuce_1_1dsp_1_1StateVariableTPTFilter.html)
- [JUCE forum — StateVariableFilter Fast Modulation](https://forum.juce.com/t/statevariablefilter-fast-modulation/32668)
- [Martin Finke — Making Audio Plugins Part 18: PolyBLEP Oscillator](https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/)
- [KVR — Oscillator antialiasing (BLEP, polyBLEP, oversampling)](https://www.kvraudio.com/forum/viewtopic.php?t=437116)
- [Juhan Nam (CCRMA) — Virtual Analog Oscillators](https://ccrma.stanford.edu/~juhan/vas.html)
- [Will Pirkle — BLEP and PolyBLEP Applied to Phase Distortion Synthesis](http://www.willpirkle.com/Downloads/AppNote%20AN-11%20PDSynthesis.pdf)
- [KVR — Performance of log10 and pow](https://www.kvraudio.com/forum/viewtopic.php?t=52715)
- [KVR — gain cell with pow(x,y) just tooo slow](https://www.kvraudio.com/forum/viewtopic.php?t=82822)
- [KVR — Fast Exp2](https://www.kvraudio.com/forum/viewtopic.php?t=192434)
- [JUCE forum — AbstractFifo single consumer/single producer thread safety](https://forum.juce.com/t/abstractfifo-single-consumer-single-producer-thread-safety/50749)
- [timur.audio — Using locks in real-time audio processing, safely](https://timur.audio/using-locks-in-real-time-audio-processing-safely)
