# REVIEW V2 — Implementation Plan

**Source of work:** every issue, recommendation and improvement in [`CODE_REVIEW_V2.md`](./CODE_REVIEW_V2.md).
**Compatibility stance:** per `GEMINI.md`, V2 has not shipped; no V1 patch/state/parameter-ID/CC-mapping compatibility is required. Breaking changes are encouraged where they yield the cleanest V2.
**Verification preset (every step):** `cmake --preset vs2022-debug && cmake --build --preset build-debug --config Debug && ctest --test-dir build -C Debug --output-on-failure`. Each step is "Done" only when this passes AND the listed acceptance criteria are met AND the diff is reviewed.

This plan is split into work-items (WIs). Each WI is small enough for one review cycle, in the spirit of `GEMINI.md`. WIs are grouped into five tracks that can be sequenced largely independently inside their track:

- **Track A — Critical RT-safety fixes** (release-blocking, ship first)
- **Track B — Audio quality / DSP correctness**
- **Track C — Performance**
- **Track D — Code hygiene & dead code**
- **Track E — Tests & CI**

Dependencies between WIs are spelled out where they exist. Effort estimates use S (≤ half day), M (half-to-one day), L (one-to-two days), XL (two-to-three days).

---

## Sequencing summary

```
Track A (RT safety)         Track D (hygiene)             Track E (tests)
  A1 visualizer FIFO ──┐      D1 delete V1 dead code        E1 split test files
  A2 FFT double buffer ┤      D2 collapse legacy IDs        E2 dsp correctness suite
  A3 mapped dispatcher ┘      D3 magic-number constants     E3 thread-stress harness
       │                      D4 dedupe MIDI translation
       │                      D5 tooltip extraction         (E1 enables faster CI)
       ▼
Track B (quality)           Track C (perf)
  B1 PolyBLEP saw/pulse       C1 voice loop refactor (block vs sample)
  B2 PolyBLEP-aware sync      C2 exp2 over pow
  B3 filter mod combine       C3 sub-rate LFO
  B4 resonance compensation   C4 drive normalizer hoist
  B5 headroom strategy        C5 SmoothedValue on FX
  B6 unison vintage UX        C6 cached choice decodes
```

**Recommended ship order for V2 release:** A1 → A2 → A3 → B1 → B2 → C1 → C2 → C4 → C5 → B5 → B3 → B4 → B6 → D1 → D2 → D3 → D4 → E1 → E2 → D5 → C3 → C6 → E3.

A and the first half of B/C/D are pre-release. The rest can land in patch releases.

---

# Track A — Critical RT-safety fixes

These three items address use-after-free and host-incompatibility hazards. They are non-negotiable for a public release.

## WI-A1 — Move sample push out of the audio→editor reach (CODE_REVIEW §3.1)

**Effort:** M. **Risk:** Low. **Blocks:** A2 (logical grouping); does not strictly block B/C.

### Problem
`SynthAudioProcessor::processBlock` (lines 257-258) does:
```cpp
if (auto* editor = dynamic_cast<SynthAudioProcessorEditor*>(getActiveEditor()))
    editor->getVisualizer().pushSamples(buffer.getReadPointer(0), buffer.getNumSamples());
```
The editor is owned by the message thread; `getActiveEditor()` can return a pointer that is freed between check and dereference. `dynamic_cast` on the audio thread is also gratuitous overhead. This is the JUCE-canonical pull-from-processor pattern violation.

### Target design
1. The processor owns a fixed-size lock-free ring buffer (`std::array<float, kProcessorScopeFifoSize>` + `juce::AbstractFifo`).
2. Inside `processBlock`, the processor writes the post-FX mono mix into that ring (after master gain).
3. The editor's visualizer pulls samples on its 30 Hz timer.

### Detailed steps
1. **New file `src/synth/ProcessorScopeFifo.h`** (~40 lines) — wraps a `juce::AbstractFifo` over a `std::array<float, 16384>` for a single mono channel. Public surface:
   ```cpp
   class ProcessorScopeFifo final {
   public:
       void write(const float* samples, int numSamples) noexcept;   // audio thread
       int  read (float* destination,   int maxSamples) noexcept;   // UI thread
       int  available() const noexcept;
       void clear() noexcept;
   private:
       std::array<float, 16384> buffer {};
       juce::AbstractFifo fifo { 16384 };
   };
   ```
   16384 / 48000 ≈ 340 ms of headroom — far more than a 30 Hz UI refresh needs (~1600 samples / tick).
2. **Add a `ProcessorScopeFifo scopeFifo;` member to `SynthAudioProcessor`** (header).
3. **In `SynthAudioProcessor::processBlock`**, after the FX chain and master gain, write the mono mix to `scopeFifo`. If the bus is stereo, mix `(L+R) * 0.5f` into a `juce::HeapBlock<float>` scratch — no, **use a fixed `std::array<float, MAX_BLOCK>` member** (we already declare `samplesPerBlock` at prepare time; preallocate). Replace the dynamic_cast block entirely.
4. **Expose** `ProcessorScopeFifo& getScopeFifo() noexcept` on the processor.
5. **Refactor `SignalChainVisualizer`** (header + .cpp):
   - Drop the audio-thread `pushSamples(const float*, int)` API. Replace with a private `pullFromProcessor()` called inside the timer callback. The visualizer constructor now takes a `ProcessorScopeFifo&` in addition to the APVTS reference.
   - The visualizer's UI timer pulls up to N samples per tick, advances its own internal scope buffer, then renders.
6. **Update `SynthAudioProcessorEditor` ctor** to pass `processor.getScopeFifo()` into the visualizer.
7. **Delete** `#include "SynthAudioProcessorEditor.h"` from `SynthAudioProcessor.cpp`.

### Acceptance criteria
- `SynthAudioProcessor.cpp` has zero references to `getActiveEditor()` or `SynthAudioProcessorEditor`.
- No `dynamic_cast` from the audio thread.
- Manually: open the editor, play notes, close the editor while audio is running, reopen. No crash, no glitch beyond a brief visual gap.
- All existing tests pass.

### Risks & mitigations
- **Scope FIFO overflow** under very small UI refresh intervals → write path drops oldest silently; add a debug counter (see A4 below).
- **Resize while running** — `prepareToPlay` should call `scopeFifo.clear()` to drop stale pre-resize samples.

---

## WI-A2 — Double-buffer the FFT staging (CODE_REVIEW §3.2)

**Effort:** S. **Risk:** Low. **Depends on:** A1 (clean ownership boundary makes A2 simpler).

### Problem
`SignalChainVisualizer::pushSamples` (lines 73-82) uses `static int fftWriteIdx`, which is shared across plugin instances. Worse, the FFT buffer is mutated in place by `multiplyWithWindowingTable` and `performFrequencyOnlyForwardTransform` (in `updateSpectralData`) while the audio thread may already be refilling it.

### Target design
- **Audio side disappears entirely** once A1 lands — samples now arrive from `ProcessorScopeFifo` on the UI thread. The race goes away because both filling and FFT happen on the same thread.
- The "FFT-block ready" atomic is no longer needed.

### Detailed steps
1. In `SignalChainVisualizer`:
   - Remove `fftData[fftSize * 2]` audio-thread-shared buffer's atomic flag and the static local.
   - Maintain a single private `std::vector<float> fftScratch` sized to `fftSize * 2`.
   - On each timer tick, pull samples from `ProcessorScopeFifo` into `fftScratch[fftWriteIdx]`. When `fftWriteIdx >= fftSize`, run windowing+FFT on `fftScratch` in place, render spectraPath, reset `fftWriteIdx = 0`.
   - All happens on the UI thread.
2. Replace the scope FIFO inside the visualizer with the same pulled-from-processor data; one source.

### Acceptance criteria
- Two plugin instances in a host show independent spectra (manual: open the same plugin twice on different tracks playing different notes; spectra must not interfere).
- No `static` storage inside `SignalChainVisualizer.cpp`.
- No `std::atomic` member needed for FFT readiness.
- ThreadSanitizer or Address Sanitizer clean (see E3).

### Risk
None substantial — this is a code-shrinking change.

---

## WI-A3 — Route plugin mapped CC actions through the message thread (CODE_REVIEW §3.3)

**Effort:** M. **Risk:** Medium (host-touching change). **Independent of A1/A2.**

### Problem
`SynthAudioProcessor::PluginMappedActionDispatcher` (lines 908-929) is a worker thread that, every 4 ms, drains the mapped-CC FIFO and calls `beginChangeGesture / setValueNotifyingHost / endChangeGesture` from a non-message thread. Per JUCE forum, this misbehaves in FL Studio and similar hosts.

### Target design
Drop the worker thread. Use `juce::AsyncUpdater` (already used in the standalone path) to bounce drain work to the message thread.

### Detailed steps
1. **Replace `PluginMappedActionDispatcher`** with a `private class PluginMappedActionAsyncBridge : public juce::AsyncUpdater` inner class on `SynthAudioProcessor` whose `handleAsyncUpdate()` calls `owner.dispatchPendingMappedControllerEvents()`.
2. In `SynthAudioProcessor::enqueuePluginMappedControllerEvent` (called from audio thread), after `pendingPluginMappedControllerEventQueue.finishedWrite(1)`, call `mappedActionBridge.triggerAsyncUpdate()`. `triggerAsyncUpdate()` is documented as RT-safe (it's a single atomic exchange).
3. Delete the `juce::Thread`-based dispatcher and its `wait(4)` polling.
4. **Update destructor:** call `mappedActionBridge.cancelPendingUpdate()` before the FIFO is destroyed.

### Acceptance criteria
- No `juce::Thread` subclass in `SynthAudioProcessor.cpp`.
- In FL Studio (manual test, opt-in): rotate a learned-CC knob; the parameter follows without dropped messages or DAW protests.
- In Ableton Live Lite + REAPER: behaviour unchanged from current.
- Test: existing learn round-trip tests pass; new test asserts that a CC arriving from `enqueuePluginMappedControllerEvent` followed by `juce::MessageManager::getInstance()->runDispatchLoopUntil(50)` results in the parameter being updated.

### Risk
- `triggerAsyncUpdate` is documented as RT-safe but allocates only its first call ever. We can guarantee that by triggering it once in `prepareToPlay` then immediately `cancelPendingUpdate()`-ing — primes the lazy initialization.

---

## WI-A4 — Diagnostic counter on FIFO drops (CODE_REVIEW §3.4)

**Effort:** S. **Risk:** None.

### Problem
`enqueuePluginControllerEvent` and `enqueuePluginMappedControllerEvent` silently drop events when the 128-slot queue is full.

### Detailed steps
1. Add `std::atomic<uint64_t> droppedControllerEventCount {0};` (one per queue) on the processor.
2. In the `else` branch of each enqueue, do `droppedControllerEventCount.fetch_add(1, std::memory_order_relaxed);`.
3. Expose a getter for tests and debug overlays.
4. Add a `[Stress]` test that fires 1024 CCs in one block and asserts both that the FIFO grew, and that `droppedControllerEventCount` accounts for any overflow.

### Acceptance criteria
- New unit test passes.
- No behaviour change in normal use.

---

## WI-A5 — Document `MidiMappingEngine::translate()` locking contract (CODE_REVIEW §3.5)

**Effort:** S. **Risk:** None.

Add a Doxygen comment to `MidiMappingEngine::translate(...)` in the header:
> /** Must only be called while the owner holds the mapping state lock; this method mutates per-binding takeover state via `mutable` fields. */

Also add an `assert(midiMappingStateLock.tryEnter() == false)` style debug check — but JUCE's `CriticalSection` doesn't support that introspection, so just stick with the comment plus a single `// NOLINTNEXTLINE: per-binding mutable state requires external lock` near the mutable fields.

---

# Track B — Audio quality / DSP correctness

These items improve perceptual quality. PolyBLEP is the largest perceptual lift; do it first.

## WI-B1 — PolyBLEP-bandlimit saw and pulse (CODE_REVIEW §4.1)

**Effort:** L. **Risk:** Medium (audio path change — needs thorough listening). **Dependencies:** none.

### Problem
`SynthVoice::renderWaveSample` outputs naïve trivial waves for saw/pulse/triangle. At fundamentals ≥ ~880 Hz the alias content is audibly metallic. This is the single biggest blocker for "release-quality VA synth".

### Reference implementations (consulted)
- [Martin Finke — Making Audio Plugins Part 18: PolyBLEP Oscillator](https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/)
- [CCRMA — Virtual Analog Oscillators (Nam)](https://ccrma.stanford.edu/~juhan/vas.html)
- [LV2 tutorials — Synthesis, Aliasing & PolyBLEP](https://sjaehn.github.io/lv2tutorial/1_12_synthesis_aliasing_polyblep/)

### Target design
- Implement `polyBlep(t, dt)` once as a static helper:
  ```cpp
  // t in [0,1); dt = phaseIncrement = freq / sampleRate. Returns the correction
  // term to ADD to a naive sample at the discontinuity.
  static float polyBlep(float t, float dt) noexcept {
      if (t < dt) {
          t /= dt;
          return t + t - t*t - 1.0f;
      }
      if (t > 1.0f - dt) {
          t = (t - 1.0f) / dt;
          return t*t + t + t + 1.0f;
      }
      return 0.0f;
  }
  ```
- **Saw:** `sample = (2*t - 1) - polyBlep(t, dt);`
- **Pulse:** compute the two PolyBLEPs at the two discontinuities — at `t==0` (or wrap) and at `t==pw`:
  ```cpp
  float pulseValue = (t < pw) ? (1.0f - pw) : -pw;
  pulseValue += polyBlep(t, dt);                       // discontinuity at 0/1
  pulseValue -= polyBlep(std::fmod(t - pw + 1.0f, 1.0f), dt);  // discontinuity at pw
  ```
  (The sign of the second term is negative because the pulse falls at `t==pw`.)
- **Triangle:** integrate the bandlimited square. Cheapest correct approach:
  ```cpp
  float square = (t < 0.5f) ? 1.0f : -1.0f;
  square += polyBlep(t, dt);
  square -= polyBlep(std::fmod(t + 0.5f, 1.0f), dt);
  // Leaky integrator for triangle:
  triState = 4.0f * dt * square + (1.0f - 4.0f * dt) * triState;
  return triState * 0.8f;  // empirical level match
  ```
  The leaky integrator state is per-oscillator and per-voice and must be reset in `resetVoiceState`.
- **Sine:** unchanged.

### Detailed steps
1. **Add `polyBlep` static helper** in `SynthVoice.cpp` anon namespace.
2. **Change `OscillatorState`** to carry `float phaseIncrement = 0.0f;` and `float triState = 0.0f;` instead of recomputing increment inside `renderOscillatorSample`. The increment is set per render span (or per sample where `oscillator.frequencyHz` changes due to per-sample modulation — see C1 dependency note).
3. **Rewrite `renderWaveSample`** to take `phase, dt, shape, pw` as a static function and return the bandlimited sample. Update both call sites (`renderOscillatorSample` for audio, and `SignalChainVisualizer::updateIdealWaveforms` for the UI cartoon — but the cartoon can stay naïve; mark that explicitly with a comment).
4. **Update `resetVoiceState`** to zero `oscAState.triState` and `oscBState.triState`.
5. **LFO wave usage** (`SynthVoice::renderNextBlock` line 233 + `lfoShapeToOscShape`) — the LFO doesn't need anti-aliasing (its phase rate is sub-audio). Keep LFO on the naïve path by calling `renderNaiveWaveSample(...)` (introduce a sibling function name) or by passing `dt = 0` which collapses PolyBLEP to zero.

### Acceptance criteria
- New test (in E2) — Render 1 s of A4 (440 Hz) saw at 48 kHz, FFT, assert: harmonic energy at frequencies above (sampleRate/2 - 50 Hz) is ≤ -60 dB relative to the fundamental. With current code this would be much louder.
- Render 1 s of A7 (3520 Hz) saw, assert no harmonic above Nyquist - 50 Hz is > -50 dB relative to fundamental.
- Listening: long sustained C7 saw → no metallic artefacts; harmonic spectrum sweep sweep upward sounds smooth.
- No regression in tests that count output samples or check finiteness.

### Risks
- Triangle integrator can drift over many minutes — the `(1 - 4*dt)` leak factor avoids that. Test that 5 min of continuous note holds no DC accumulation > 1e-3.
- Pulse-width modulation may cause double-trigger correction if PW crosses `t` discontinuously. Apply PW changes smoothed (already smoothed via the per-sample `oscAState.pulseWidth = jlimit(...)` — but PW jumps from PolyMod are not smoothed). Document this; not a blocker.

---

## WI-B2 — PolyBLEP-aware hard sync (CODE_REVIEW §4.1 second paragraph)

**Effort:** L. **Risk:** Medium. **Depends on:** B1.

### Problem
Current hard sync (`SynthVoice.cpp:274-276` with `nextOscillatorAParameters.syncEnabled && oscBWrapped`) phase-resets oscillator A whenever oscillator B's phase wraps. The reset is a step discontinuity at an arbitrary fractional sample; PolyBLEP can fix this if we know the exact sub-sample reset position.

### Target design
- Track the exact sub-sample fractional time at which oscillator B wrapped this sample (call it `bWrapFrac`).
- After the naïve oscillator A sample is computed AND its own self-discontinuity PolyBLEP applied, add a third PolyBLEP residual scaled by the magnitude of the step at the reset point and offset by `bWrapFrac`.

### Detailed steps
1. **Restructure `oscBWrapped` detection** so we compute `float bWrapFrac` (the fractional sample at which B crossed 1.0). Done as `(oscBState.phase + dtB) - 1.0f` divided by `dtB` after adding the increment.
2. **At reset:** capture `float sampleBeforeReset = renderNaiveWaveSample(oscAState.phase, oscA shape, pw);` and `float sampleAfterReset = renderNaiveWaveSample(0.0f, oscA shape, pw);` for the step magnitude.
3. **Apply** `polyBlepAtFractional(bWrapFrac, dtA) * (sampleAfterReset - sampleBeforeReset)` to the current sample value.
4. Reference: ["Modulating (poly)BLEP hard-sync saw" KVR thread](https://www.kvraudio.com/forum/viewtopic.php?t=425054).

### Acceptance criteria
- New test: enable sync, set oscA to ~880 Hz, sweep oscB 220→1760 Hz, FFT residual harmonics above Nyquist - 50 Hz remain ≤ -50 dB.
- Listening: pitch sweeps on classic hard-sync lead patches sound smooth, not crunchy.

### Risk
- Edge case where oscA also has its own wrap on the same sample as oscB reset. The two PolyBLEPs are additive (linearity of impulse-response convolution), so it works; document and test.

---

## WI-B3 — Sum cutoff modulation in log-space first, then exponentiate once (CODE_REVIEW §4.3, also feeds C1)

**Effort:** M. **Risk:** Low. **Independent.**

### Problem
`SynthVoice.cpp:290-300` computes
```cpp
polyModCutoffSemis + lfoCutoffSemis + envCutoffSemis + velFilterSemis  // in semitones
cutoffModRatio = std::pow(2, sum / 12);
lowPassFilter.setCutoffFrequency(clampCutoff(baseCutoffSmoothed * cutoffModRatio));
```
The sum can exceed ±18 octaves. `pow(2, 18) ≈ 262144` × base 20 kHz = 5.2 GHz — clamped at the boundary, so wasteful and a one-sample-overshoot risk near the clamp.

### Detailed steps
1. Sum the four semitone contributions.
2. Clamp the sum to a sensible range, e.g. `[-120, 120]` semitones (±10 octaves) BEFORE exponentiating. The downstream clamp `clampCutoffToPreparedRange` still applies. Pick the range so the multiplicative result stays in `[2^-10, 2^10]`, which combined with base cutoff covers the legal 20 Hz → 20 kHz interval.
3. Use `std::exp2(sum / 12.0f)` instead of `std::pow(2.0f, sum / 12.0f)`. (Feeds C2.)

### Acceptance criteria
- Sweep all modulation sources to their max simultaneously; cutoff stays inside the prepared range and no `inf`/`nan` ever reaches the filter.
- Test: feed `pow_combined_modulation_clamped` random params for 1000 iterations; assert finite output.

---

## WI-B4 — Resonance compensation: attenuate filter input as Q rises (CODE_REVIEW §4.4)

**Effort:** M. **Risk:** Medium (changes loudness behaviour).

### Problem
Q up to 25.0 means the resonance peak can boost a single frequency by ~28 dB. The current code does nothing to compensate, relying on the global `polyphonyHeadroom = 0.35` (§4.5) to keep things from clipping. As a result:
- The synth is quieter than necessary at low resonance.
- At high resonance, a sustained note can still spike well above 0 dBFS internally.

### Reference: Zavalishin TPT SVF with input gain compensation. Common form is to attenuate input by `1 / sqrt(Q)` or by a fraction proportional to `(Q - Q0)/Q`.

### Detailed steps
1. **Add to `SynthVoice::renderNextBlock` inner loop:** compute `filterInputGain = juce::jmin(1.0f, 1.0f / std::sqrt(currentQ));` (currentQ already smoothed). Apply: `filteredValue = lowPassFilter.processSample(0, oscValue * filterInputGain);`
2. **A/B-test loudness:** record short renders at Q=0.7 and Q=25; the peak headroom should be roughly equal between the two.

### Acceptance criteria
- New test: render a saw at 440 Hz, cutoff 1 kHz, for Q ∈ {0.7, 5, 15, 25}; assert peak abs sample is within ±3 dB across the four runs.
- Reduces the need for `polyphonyHeadroom`; combined with B5 we can raise the master-gain effective scaling.

### Risk
- Changes perceived loudness vs current. Mitigate via B5 (drop `polyphonyHeadroom`) so net output level is roughly unchanged.

---

## WI-B5 — Replace `polyphonyHeadroom = 0.35` with per-voice-count scaling (CODE_REVIEW §4.5)

**Effort:** M. **Risk:** Medium. **Depends on:** B4 (because raising headroom is only safe after B4 stops the resonance peaks).

### Problem
Hardcoded magic 0.35 multiplier on master gain. Documented nowhere. Users perceive synth as quiet (-9 dB hidden tax on top of the -12 dB default).

### Target design
Scale by `1 / sqrt(max(1, activeVoiceCount))`. For 8 voices that's ≈ 0.354 (matches current). For 1 voice it's 1.0 (much louder). This is the standard "incoherent mix" assumption.

### Detailed steps
1. **In `SynthEngineV2`**, track `int activeVoiceCountCached;` updated at the start of each `render()` call by counting `voices[i].voice.isActive()`.
2. **In `applyMasterGain`** replace the magic 0.35 with `1.0f / std::sqrt(static_cast<float>(juce::jmax(1, activeVoiceCountCached)))`.
3. **Hoist a constant** `voicePolyphonyExponent = 0.5f` (the "incoherent mix" exponent) to `SynthParameters.h` so a future change to coherent (`exponent = 1.0`) is one line.
4. **Document in `DESIGN_V2.md`** that the synth uses incoherent voice-summing assumption.

### Acceptance criteria
- One held note at 0 dB master, peak abs sample is approximately the same as 8 unison voices at 0 dB master (within 3 dB).
- The `masterGainLinear` SmoothedValue prevents step changes when active voice count changes.

### Risk
- Voice count changes during a block (note-on, note-off). The smoothed master gain absorbs the step. Verify with a step-response test.

---

## WI-B6 — Unison vintage: stop overriding user control (CODE_REVIEW §4.6)

**Effort:** S. **Risk:** Low.

### Problem
`SynthEngineV2.cpp:568` `unisonVintage = juce::jmax(vintageAmount, 0.35f)` silently raises Vintage to 0.35 in Unison mode regardless of user setting.

### Pick one:

**Option A (preferred):** Remove the floor. Document that "Unison with Vintage = 0 produces phase-aligned voices; raise Vintage for analog character." This honours user intent.

**Option B:** Introduce a separate `unisonDetuneCents` parameter (range 0-50 cents, default 8 cents) that adds detune independently of Vintage. More controls but more explicit.

**Recommendation:** Option A for V2, Option B post-V2 if user feedback asks for it.

### Detailed steps (Option A)
1. Delete the `jmax(..., 0.35f)` in `allocateUnisonNote`.
2. Update `GEMINI.md` "late V2 doc decisions" with the change.
3. Add a tooltip to the Vintage knob noting "raise this for analog detune in unison".

### Acceptance criteria
- Test `V2Performance.unisonVintageZero`: set vintage to 0 in unison mode; assert voice 0 and voice 1 have identical vintage drift (i.e., both 0).

---

# Track C — Performance optimisations

These reduce CPU on the audio thread substantially. Without them, low-buffer playback on weaker machines will glitch.

## WI-C1 — Split per-block vs per-sample work in `SynthVoice::renderNextBlock` (CODE_REVIEW §4.2)

**Effort:** XL. **Risk:** Medium (touches the audio hot path). **Depends on:** B1 (so we don't have to re-do this for the PolyBLEP rewrite).

### Problem
`SynthVoice.cpp:218-325` recomputes inside the inner per-sample loop:
- `std::pow(2, glideOffsetLog2)` — but `glideOffsetLog2` advances by a fixed `glideStepLog2PerSample` each sample.
- `std::pow(2, vintageDriftCents / 1200)` — but `vintageDriftCents` is constant for the block.
- `std::pow(2, keyTrackingRatio / 12)` — constant for the block.
- `std::pow(2, currentPitchBendSemitones / 12)` — constant for the block (engine handles pitch-bend events at block boundaries within a span).
- `cutoffModRatio = std::pow(2, sum/12)` — needs the per-sample modulation sum.

### Target design
For each render span:
- **Block-rate (pre-loop):** vintage ratio, key tracking ratio, pitch-bend ratio, base osc frequencies. Combine the three constants into one `voicePitchCarrierRatio = vintageRatio * keyTrackingRatio * bendRatio` so the inner loop only multiplies by it once. Compute `baseOscAFreqRaw * voicePitchCarrierRatio` and `baseOscBFreqRaw * voicePitchCarrierRatio` once.
- **Sample-rate (inner loop):**
  - Sum all *semitone* modulations (LFO pitch, poly-mod pitch, glide offset, etc.) into a `totalPitchSemis` scalar.
  - Single `std::exp2(totalPitchSemis / 12.0f)` per oscillator.
  - Sum the four cutoff modulation sources in semitones, clamp (B3), single `std::exp2`.
- **Glide:** since `glideOffsetLog2` is updated additively, multiply each sample by `glideStepRatio = exp2(glideStepLog2PerSample)` once per sample rather than `pow(2, glideOffsetLog2)`. That collapses one `pow` into one multiply.

### Detailed steps
1. **Refactor `renderNextBlock`** to compute the block-constant ratios and store them in locals before the loop.
2. **Combine pitch sources** in semitones first, then exponentiate once per oscillator per sample.
3. **For glide:** maintain `currentGlideRatio` as a running multiplicative state; multiply by `glideStepRatioPerSample` each sample. Initialise `currentGlideRatio = std::exp2(glideOffsetLog2)` once at block start, then update multiplicatively. This replaces O(N) `pow` calls with O(1) `exp2` + O(N) multiplies.
4. **For cutoff:** as B3 specifies, sum semitones then one `exp2`.
5. **LFO sample:** if `lfoOscShape == sine`, use `juce::dsp::FastMathApproximations::sin` (already in JUCE) instead of `std::sin` — or, even better, a 4096-entry sine table indexed by phase. Choice belongs to C3.

### Acceptance criteria
- CPU profile (Tracy / Optick / VTune / Visual Studio profiler — pick one): voice render time reduced ≥ 40% vs baseline on a held 4-note chord with all FX off.
- Bit-exact-or-near output: existing tests pass with at most 1 ULP per-sample difference (driven by floating-point reordering — document the tolerance).
- New microbenchmark in tests (E2) measures samples-per-millisecond for an 8-voice unison hold.

### Risk
- Reordering of math may produce small numeric drift in stability tests. Use `EXPECT_NEAR` instead of `EXPECT_EQ` for affected tests.

---

## WI-C2 — Use `std::exp2` not `std::pow(2.0f, …)` everywhere (CODE_REVIEW §4.11)

**Effort:** S. **Risk:** None. **Bundled with C1.**

Bulk replace. Performance: `std::exp2` is one specialised path; `std::pow(2.0, x)` may dispatch through `pow` generic depending on STL. Verified faster in glibc, MSVC CRT, and libc++. Apply to: `SynthVoice.cpp`, `Arpeggiator.cpp` (none currently), all places.

### Acceptance criteria
- `grep -nE 'std::pow\\s*\\(\\s*2\\.0f?\\s*,' src/` returns zero hits.

---

## WI-C3 — Sub-rate LFO with linear interpolation (CODE_REVIEW §4.2 third bullet)

**Effort:** M. **Risk:** Low. **Depends on:** C1.

### Problem
LFO at >5 Hz updated per-sample (48000 updates/sec for a 5 Hz wave) is severe overkill. Anything below the audio band can update every 32 samples with linear interp without perceptible loss.

### Detailed steps
1. **Constants:** `inline constexpr int lfoSubRateSamples = 32;` in `SynthParameters.h`.
2. **In `SynthVoice::renderNextBlock`**, maintain `float currentLfoValue, nextLfoValue, lfoInterpStep, lfoStepsRemaining`. Every `lfoSubRateSamples`, compute `nextLfoValue` from the new phase, derive `lfoInterpStep = (nextLfoValue - currentLfoValue) / lfoSubRateSamples`, advance phase by `lfoPhaseIncrement * lfoSubRateSamples`. Per-sample: `currentLfoValue += lfoInterpStep`. (Saves a `sin`/`render` call per sample.)
3. **Engine-side `globalLfoPhase`** advance is already done per render span — leave alone.

### Acceptance criteria
- Render 5 s of a sustained note with LFO routed to cutoff at 3 Hz; FFT spectrum is indistinguishable from current (within 1 dB) in the audible range.
- CPU measurement: saw amount measurable but small (~5% on a held chord).

---

## WI-C4 — Hoist drive normalizer out of inner loop (CODE_REVIEW §4.7)

**Effort:** S. **Risk:** None.

`GlobalFxRack::processDrive` (lines 116-141): compute `const float normalizer = std::tanh(preGain);` once OUTSIDE the sample loop. Same with `const float inverseNormalizer = (normalizer > 0.0f) ? 1.0f / normalizer : 1.0f;`. Inner loop becomes one multiplication + one tanh.

### Acceptance criteria
- Drive output bit-exact (within ULP) compared to current implementation.
- Microbenchmark: drive stage CPU halves.

---

## WI-C5 — SmoothedValue on FX wet/dry parameters (CODE_REVIEW §4.8)

**Effort:** M. **Risk:** Low.

### Problem
Drive mix, chorus rate/depth/mix, reverb size/damping/mix all jump at block boundaries → audible zipper/clicks when modulated.

### Target design
- Each FX block keeps `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>` smoothers for its scalar wet/dry, mix, and the slowly-modulated params (chorus rate, reverb size).
- Ramp time: **15 ms** (covers a single block at 96 kHz × 1024 = 10.7 ms with margin, and feels instant to a user).
- Smoothers' `reset(sampleRate, 0.015)` called from `prepare()`.
- Each block: `setTargetValue(currentParam)` once, then drain `getNextValue()` per sample where needed.

### Detailed steps
1. **`GlobalFxRack` members:** add smoothers for `driveMix, drivePreGain, chorusMix, chorusDepth, chorusRate, reverbWetLevel, reverbRoomSize, reverbDamping`. Reverb is block-rate (`reverb.setParameters` is the input), but we can re-set parameters every 32 samples with smoothed values for finer interpolation. Cheap.
2. **`processDrive`:** read `mix` from `driveMixSmoothed.getNextValue()` per sample; pull `preGain` from `drivePreGainSmoothed`.
3. **`processChorus`:** call `chorus.setRate/Depth/Mix` on a 32-sample sub-rate inside the chorus's existing block loop. Or accept block-rate for chorus rate (modulation depth change is slow anyway) and only smooth `chorusMix`.
4. **`processReverb`:** same approach as chorus — smooth at 32-sample sub-rate.

### Acceptance criteria
- E2 test "drive mix step response": toggle `driveMix` 0→1 between blocks, assert no sample-to-sample discontinuity > 0.05 normalized.
- Listening: rapid wheel automation of FX mix produces no clicks.

---

## WI-C6 — Cache choice-parameter decodes per block (minor, follow-up to C1)

**Effort:** S. **Risk:** None.

`SynthAudioProcessor::makeBlockRenderParameters` calls `decodeOscillatorWave / decodeFilterKeyTracking / …` etc. Each does `std::round` and a cast. Not expensive (call site is per block) but ugly. Replace with `static_cast<int>` of the parameter's natural integer index — `AudioParameterChoice::getIndex()` is already an integer atomic.

### Detailed steps
1. Switch `parameterValues.oscAWave` and friends to point at the parameter object's index, not the rounded float, by using `AudioParameterChoice*` directly where applicable. Keep the atomic float fast path for floats; switch to integer-typed accessor for choices.
2. This removes 12+ `std::round` calls per block.

---

# Track D — Code hygiene & dead code

These are non-functional but pay off in long-term maintenance.

## WI-D1 — Delete V1 dead code (CODE_REVIEW §9)

**Effort:** S. **Risk:** Medium (test file references).

### Files to delete
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthEngine.h`

### Files to edit
- `src/synth/SynthParameters.h` — remove `BlockRenderParameters`, `ParameterValuePointers`, `FilterParameters`, `DelayParameters` (V1 structs).
- `src/parameters/ParameterIDs.h` — remove `WaveformChoice` enum.
- `src/synth/SynthVoice.h` and `.cpp` — remove `setWaveform`.
- `CMakeLists.txt` — remove `src/synth/SynthEngine.cpp` from both targets.
- `tests/StabilityAndDisconnectTests.cpp` — port or delete tests that reference `SynthEngine` (V1). Per `GEMINI.md` no-back-compat stance, deletion is acceptable.

### Acceptance criteria
- `grep -rn 'SynthEngine[^V]' src/` returns nothing except the V2 class.
- Build clean; all remaining tests pass.

---

## WI-D2 — Flip all parameter version hints to V2 (CODE_REVIEW §5.1)

**Effort:** S. **Risk:** Low (V2 hasn't shipped; no automation to break).

### Detailed steps
1. **`src/parameters/ParameterLayout.cpp`:** replace all `makeLegacyParameterId(...)` calls with `makeV2ParameterId(...)`. Affected: `filterCutoffHz`, `filterResonance`, `ampAttackMs/Decay/Sustain/Release`, `delayTimeMs/Feedback/Mix`, `masterGainDb`.
2. **`src/parameters/ParameterIDs.h`:** delete `legacyVersionHint` constant. Rename `v2VersionHint` to `parameterVersionHint`.
3. **`CMakeLists.txt`:** delete `JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1`.
4. **Delete `makeLegacyParameterId` helper.**

### Acceptance criteria
- Build clean without the warning suppression.
- Patch round-trip test still passes (state restore uses the wrapped V2 format, independent of version hints).

---

## WI-D3 — Hoist magic numbers to named constants (CODE_REVIEW §8)

**Effort:** M. **Risk:** None.

For each row of the magic-number table, introduce a named `inline constexpr` in the most local relevant header. Comments should state UNITS and RATIONALE (not just rename).

### Mapping (where each constant should live)
| Constant | Home | Comment |
|---|---|---|
| `polyphonyHeadroom` (deleted by B5) | — | gone |
| `voicePolyphonyExponent` | `SynthParameters.h` | "incoherent voice mix exponent: 0.5 = 1/√N" |
| `maxVintageDriftCents` | already in `SynthEngineV2.cpp` anon ns | add unit-suffix comment |
| `overloadDrive` multiplier `1.75` | `SynthVoice.cpp` anon ns | `kPreFilterOverloadDriveSlope = 1.75f; // soft-clip drive once mix exceeds 1.0` |
| `polyModCutoffRangeOctaves = 5` | `SynthVoice.cpp` anon ns | |
| `envCutoffRangeOctaves = 7` | `SynthVoice.cpp` anon ns | |
| `lfoPitchRangeSemitones = 12` | `SynthVoice.cpp` anon ns | |
| `velocityToFilterRangeSemitones = 24` | `SynthVoice.cpp` anon ns | |
| `qMin = 1/sqrt(2)`, `qMax = 25.0f` | `SynthVoice.cpp` anon ns | |
| Note-start ramp extras | `SynthVoice.cpp` anon ns | `kNoteStartRampExtraPulseMs = 0.8f; // tames pulse-induced click` etc. |
| `chorusCentreDelayMs`, `chorusFeedback`, `maxDrivePreGain`, `conservativeReverbTailSeconds`, `conservativeDelayFeedbackTailSeconds` | already at top of `GlobalFxRack.cpp` — add unit suffix comments |
| `GlobalDelay.cpp:11` literal `1.0` | replace with `static_cast<double>(maxDelayTimeMs) / 1000.0` |
| `SynthVoice.cpp:430` pink noise `0.2` | rename to `kPinkNoiseKelletNormalisation = 0.2f` |
| `SynthAudioProcessor.cpp:646` feedback 0.85 | reference `coolsynth::synth::GlobalDelay::maxFeedback` (make that constant public-readable) |

### Acceptance criteria
- `grep -nE '\\b(0\\.35|0\\.85|1\\.75|0\\.2f|\\* 60\\.0f|\\* 84\\.0f|\\* 24\\.0f)\\b' src/synth/` returns near-zero hits in *.cpp.

---

## WI-D4 — Single MIDI-to-controller-event translator (CODE_REVIEW §6.5)

**Effort:** S. **Risk:** None.

### Detailed steps
1. **New file `src/midi/MidiToControllerEvent.h`** with one function:
   ```cpp
   inline std::optional<ControllerMidiEvent> toControllerMidiEvent(const juce::MidiMessage&) noexcept;
   ```
2. Replace the three duplicate copies in:
   - `SynthAudioProcessor.cpp:29-60` (anon-ns `makeControllerMidiEvent`).
   - `SynthAudioProcessor.cpp:511-528` (`enqueuePluginControllerEvent` body).
   - `StandaloneMidiInput.cpp:181-220` (`enqueueControllerEvent` body).
3. Each callsite becomes `if (auto e = toControllerMidiEvent(msg)) { … push e … }`.

### Acceptance criteria
- Build clean. MIDI Learn round-trip tests pass.

---

## WI-D5 — Extract editor tooltips and tooltip wrapping (CODE_REVIEW §7.5)

**Effort:** M. **Risk:** None.

### Detailed steps
1. New file `src/plugin/EditorTooltips.cpp` defines a function `void applyTooltipsToEditor(SynthAudioProcessorEditor& editor)` that calls all the `setParameterTooltip(...)` / `setOptionTooltip(...)` currently inline in the editor constructor.
2. Move the `makeTooltipText` and `wrapTooltipBody` helpers there.
3. Editor constructor shrinks to ~150 lines.

### Acceptance criteria
- Editor constructor `< 200` lines.
- Tooltips remain unchanged visually.

---

## WI-D6 — Replace `ParameterValuePointersV2` hand-list with a constexpr table (CODE_REVIEW §5.3)

**Effort:** M. **Risk:** Low.

### Target design
Define a `static constexpr std::array<std::pair<const char*, std::atomic<float>* ParameterValuePointersV2::*>, N>` mapping parameter IDs to pointer-to-member. The binder becomes a loop. Adding a parameter forces compile-time update of the table.

### Detailed steps
1. In `SynthParameters.h`, declare the mapping table after `ParameterValuePointersV2`.
2. Replace 70-line manual loop in `bindParameterPointers` with a `for` loop that does `state.getRawParameterValue(id)` and assigns.
3. Add `static_assert(allParameterIds.size() == parameterPointerMap.size())` to catch additions to one list but not the other.

### Acceptance criteria
- `bindParameterPointers` is ≤ 10 LoC.
- Compilation fails if a parameter ID is added to `allParameterIds` without adding to the pointer map.

---

## WI-D7 — Pick one source of truth for parameter clamping (CODE_REVIEW §5.2)

**Effort:** M. **Risk:** Medium (changes wire-protocol clamp behaviour).

**Recommendation:** Trust APVTS range; drop the redundant `jlimit` calls in `makeBlockRenderParameters`. The APVTS already enforces the range.

### Detailed steps
1. In `makeBlockRenderParameters`, remove `juce::jlimit(min, max, …)` for parameters whose `NormalisableRange` already covers `[min, max]`.
2. Keep clamps for values that need extra business-logic transformation (e.g. `ampAttackMs * 0.001f` then `jmax(0.001f, …)`).
3. The choice decodes already have their own range-of-enum jlimit — leave those.

### Acceptance criteria
- Existing tests pass.
- Code size in `makeBlockRenderParameters` drops noticeably.

### Risk
- A bug elsewhere could pass an out-of-range value. Add a `jassert(parameter->getNormalisableRange().contains(value))` in debug to catch regressions.

---

## WI-D8 — Acknowledge approximate viz width / fix offsets (CODE_REVIEW §7.3)

**Effort:** S.

In `SignalChainVisualizer.cpp:149, 173`, replace the `(getWidth() - 40)` approximation with the exact pane-width math we now know from the 4:3 layout: `(getWidth() - 4 - 32) / 5`. Move into a `paneWidth()` private method.

### Acceptance criteria
- SOURCE/FILTER/REALITY/SPECTRA paths align with their pane borders pixel-perfectly.

---

# Track E — Tests & CI

Cheap, high-leverage additions that catch the bug classes we fixed.

## WI-E1 — Split the 2188-line stability test file (CODE_REVIEW §2.5)

**Effort:** M. **Risk:** None. **No `tests/StabilityAndDisconnectTests.cpp` file should be deleted; only re-partitioned.**

### Detailed steps
1. Create new test files:
   - `tests/V2EngineTests.cpp` — voice allocation, sustain, panic, prepare/reset
   - `tests/V2ArpeggiatorTests.cpp` — patterns, host sync, internal clock, latch, gate
   - `tests/V2VoiceTests.cpp` — oscillator output, filter behaviour, glide, modulation
   - `tests/V2FxRackTests.cpp` — drive/chorus/delay/reverb behaviour and bypass
   - `tests/V2ProcessorTests.cpp` — MIDI ingest, state save/load, plugin/standalone path
2. Move existing tests by subsystem. The original file keeps stability/disconnect-only tests.
3. Update `CMakeLists.txt` test target.

### Acceptance criteria
- Per-file line count ≤ 800.
- All tests still pass.

---

## WI-E2 — DSP correctness suite (CODE_REVIEW §11)

**Effort:** L. **Risk:** None.

Add the six recommended tests (and a few more derived from earlier WIs):

| Test | Asserts | WI that needs it to pass |
|---|---|---|
| `V2VoiceTests.aliasing_A4_saw` | After PolyBLEP, harmonic energy above Nyquist - 50 Hz ≤ -60 dB ref fundamental | B1 |
| `V2VoiceTests.aliasing_A7_saw` | Same at 3520 Hz | B1 |
| `V2VoiceTests.hard_sync_no_alias` | Sync residual at high pitches stays bounded | B2 |
| `V2VoiceTests.filter_stability_q25` | Sweep cutoff 20→18 kHz at Q=25, peak abs < 4.0 | B4 |
| `V2VoiceTests.filter_loudness_vs_Q` | Peak output within ±3 dB across Q=0.7,5,15,25 | B4 |
| `V2VoiceTests.master_gain_ramp` | 0 dB → -60 dB step, no sample-to-sample drop > X | (existing — formalise) |
| `V2FxRackTests.idempotent_with_fx_disabled` | All FX disabled = byte-equal to dry render | (existing — formalise) |
| `V2FxRackTests.drive_mix_smoothing` | Drive mix 0→1 step, no sample-to-sample > 0.05 | C5 |
| `V2FxRackTests.chorus_mix_smoothing` | Same | C5 |
| `V2ProcessorTests.mapped_dispatch_async` | After `enqueuePluginMappedControllerEvent`, parameter is updated within 50 ms of dispatch loop | A3 |
| `V2VoiceTests.cutoff_mod_clamp_finite` | Random extreme modulation → no inf/nan ever | B3 |
| `V2EngineTests.scope_fifo_smoke` | Push 100 ms of audio, read it from FIFO without loss in normal conditions | A1 |
| `V2VoiceTests.unison_vintage_zero` | Vintage = 0 in unison → voices have identical detune (= 0 cents) | B6 |
| `V2VoiceTests.triangle_no_dc` | 5-minute triangle hold → DC offset < 1e-3 | B1 |
| `V2VoiceTests.polyphony_loudness_consistency` | 1 voice vs 8 voices, peak within 3 dB | B5 |

### Acceptance criteria
- All listed tests live in their respective files.
- All pass after the corresponding WI is complete.

---

## WI-E3 — Thread-stress harness with sanitizers (CODE_REVIEW §3 generally)

**Effort:** L. **Risk:** Low.

### Detailed steps
1. Add a CMake option `COOLSYNTH_BUILD_TSAN_TEST` (Linux/Mac builds — TSAN not great on MSVC) that builds the test target with `-fsanitize=thread`.
2. New test `V2ConcurrencyTests`:
   - Spawn an audio-rate thread calling `processor.processBlock` in a loop.
   - Spawn a UI-rate thread calling `editor.timerCallback` (or visualizer poll).
   - Spawn a MIDI thread enqueueing standalone CCs.
   - Run for 5 seconds.
   - Pass if no TSAN report.
3. Add to CI as an opt-in nightly job.

### Acceptance criteria
- Test exits clean under TSAN after A1, A2, A3.

---

## WI-E4 — Microbenchmarks for the voice loop (CODE_REVIEW §4.2)

**Effort:** S.

Simple test that times rendering 1 second of 8-voice unison with the FX rack on. Logs `samples/ms`. Not a pass/fail (CPU-dependent), but a baseline before and after C1 to confirm the optimisation.

---

# Cross-cutting concerns

## Sanity guard: no behaviour change without a test
Every WI that changes audio output must add or reuse a test that asserts the new behaviour. The "before vs after" comparison goes in the PR description, not in code.

## Sanity guard: no commit to `DONE.md` until reviewed
Per `GEMINI.md` rules: each WI is one `TODO.md` item, moves to `DONE.md` only after diff review + tests pass + docs updated.

## Documentation impact
WIs that affect user-visible behaviour update `REQUIREMENTS_V2.md` and `DESIGN_V2.md`:
- B5 — document "voice mix uses incoherent (1/√N) summing assumption".
- B6 — document "Vintage in Unison no longer has a hidden floor".
- D2 — note the parameter-version-hint contract.

## What this plan does NOT change
- Voice count (8) and arp behaviour are not modified.
- The instrument panel layout from Phase 5 stays as-is.
- The `.cspatch` extension and wrapped V2 format are unchanged.
- The MIDI Learn UX is unchanged.
- The factory profile (MiniLab 3 Arturia) bindings are unchanged.

---

# Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| B5 loudness change surprises users | Medium | Low | Document in `DESIGN_V2.md`; bundle with B4 so net loudness near-constant |
| C1 reorders math, breaks bit-exact tests | High | Low | Switch to `near`-equality in those tests |
| A3 worker→async change hides a latent race | Low | Medium | Cover with E3 TSAN run |
| B2 hard-sync residual math is subtle | Medium | Medium | Reference-implementation port; FFT-based correctness test |
| B1 triangle integrator drift | Low | Low | Test holds for 5 min; leaky integrator removes DC |
| D7 removes a guard that's catching real bad input | Low | Medium | Add debug assert with the range check |

---

# Done criteria for "Phase 11 complete"

Phase 11 is considered complete when:

1. All WIs in tracks A, B, C, D-pre-release are landed and tests pass.
2. Manual smoke (standalone + Ableton Live Lite + REAPER on Windows):
   - Open synth.
   - Play single notes, chords, hold note.
   - Sweep cutoff at Q=25 → no clipping audible.
   - Engage unison with Vintage 0 → phase-aligned.
   - Sweep all FX mix knobs rapidly → no clicks.
   - Sustained high note (≥ A6) on saw and pulse → no metallic aliasing.
3. `cmake --preset vs2022-debug && cmake --build --preset build-debug && ctest …` clean.
4. TSAN harness (E3) clean.
5. `DONE.md` updated; `TODO.md` cleared of Phase 11 items.
6. `GEMINI.md` "Current Project State" updated to reflect Phase 11 closure.

After step 6 the synth is ready for the release-validation phase referenced in `IMPLEMENTATION_PLAN_V2.md`.

---

# Sources consulted while drafting this plan

- [Martin Finke — Making Audio Plugins Part 18: PolyBLEP Oscillator](https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/)
- [KVR — Modulating (poly)BLEP hard-sync saw](https://www.kvraudio.com/forum/viewtopic.php?t=425054)
- [LV2 tutorials — Synthesis, Aliasing & PolyBLEP](https://sjaehn.github.io/lv2tutorial/1_12_synthesis_aliasing_polyblep/)
- [CCRMA Juhan Nam — Virtual Analog Oscillators](https://ccrma.stanford.edu/~juhan/vas.html)
- [JUCE forum — Lock Free Queue data copy between Processor and Editor](https://forum.juce.com/t/lock-free-queue-data-copy-between-processor-and-editor/47895)
- [JUCE forum — Async signalling from Audio Processor to FIFO reader (Editor)](https://forum.juce.com/t/async-signalling-from-audio-processor-to-fifo-reader-editor-probably-a-question-for-thevinn-and-co/15228)
- [JUCE forum — General Audio Visualiser Implementation](https://forum.juce.com/t/general-audio-visualiser-implementation/18696)
- [JUCE docs — SmoothedValue](https://docs.juce.com/master/classSmoothedValue.html)
- [JUCE forum — Still glitchy/zipping noises after using smoothedvalue](https://forum.juce.com/t/still-glitchy-zipping-noises-after-using-smoothedvalue/59589)
- [Louis Pearse — Virtual Analogue Lowpass Resonant Filter (USyd thesis)](https://ses.library.usyd.edu.au/bitstream/handle/2123/13497/DESC9115FinalLouisPearseHawkins.pdf?sequence=2&isAllowed=y)
- [JordanTHarris/VAStateVariableFilter — reference VA SVF implementation](https://github.com/JordanTHarris/VAStateVariableFilter)
