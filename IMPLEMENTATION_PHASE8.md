<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 8 Blueprint: Per-Voice Low-Pass Filter Slice

## Phase Selection

Selected phase: `Phase 8 - Per-voice low-pass filter slice`

Selection basis:

- `TODO.md` is currently pinned to `Phase 8`, and every checkbox for that phase is still open.
- `DONE.md` records `Phase 7` as complete, so the next execution slice is the filter milestone.
- `IMPLEMENTATION_PLAN.md` explicitly isolates the filter slice from the later delay and VST3 milestones.
- The repository already has `IMPLEMENTATION_PHASE1.md` through `IMPLEMENTATION_PHASE7.md`, and `IMPLEMENTATION_PHASE8.md` does not exist yet.

## Scope Guardrails

In scope for this phase:

- Add one JUCE low-pass filter per synth voice.
- Drive filter cutoff and resonance from the existing APVTS parameter model.
- Add filter cutoff and resonance controls to the shared editor.
- Extend the fixed MiniLab mapping so `Knob 2` drives cutoff and `Knob 3` drives resonance.
- Keep the audio-thread behavior real-time safe and sample-rate stable.

Explicitly out of scope for this phase:

- A global filter substitute.
- Filter envelope, velocity-to-filter modulation, key tracking, or drive.
- Delay controls or delay MIDI mappings.
- A generic controller-mapping refactor.
- VST3 host-validation work beyond preserving the shared editor and parameter model.
- Standalone MIDI input-pipeline rewrites.
- New abstraction layers above JUCE.

Decision gates before implementation:

- `src/parameters/ParameterIDs.h` already contains `filterCutoffHz` and `filterResonance`; do not rename IDs, change defaults, or alter ranges in a way that breaks saved state.
- `src/standalone/StandaloneMidiInput.cpp` already forwards controller events off the callback thread; do not move parameter mutation into `processBlock(...)`.
- `docs/minilab3-default-messages.md` currently defers `Knob 2` and `Knob 3`, and `src/midi/Minilab3Profile.cpp` does not yet promote them into the binding table. The mapping checkbox is not complete until those controls are recorded in the profile used by code.

## Current Code Anchors

The blueprint should stay anchored to the code that currently owns the behavior:

- `src/synth/SynthVoice.h` and `src/synth/SynthVoice.cpp` currently implement oscillator plus ADSR only. There is no filter state, smoothing, or note-reset logic for filter coefficients.
- `src/synth/SynthEngine.h` and `src/synth/SynthEngine.cpp` already own the 8-voice pool, waveform fan-out, envelope fan-out, and global master-gain ramp.
- `src/synth/SynthParameters.h` already defines the render-time parameter snapshot types, but it carries no filter data yet.
- `src/plugin/SynthAudioProcessor.cpp` already binds raw parameter atomics for waveform, ADSR, and master gain. It does not bind or forward `filterCutoffHz` or `filterResonance` into the synth engine.
- `src/plugin/SynthAudioProcessorEditor.h` and `src/plugin/SynthAudioProcessorEditor.cpp` currently expose `Oscillator`, `Envelope`, and `Output` sections only. There is no `Filter` section, no filter attachments, and no filter value-refresh path.
- `src/midi/MidiMappingEngine.h` and `src/midi/MidiMappingEngine.cpp` currently support seven active bindings: waveform, ADSR, master gain, and panic. There are no targets for cutoff or resonance.
- `src/midi/Minilab3Profile.h` and `src/midi/Minilab3Profile.cpp` currently define only the `Phase 7` logical targets and binding rows.
- `src/parameters/ParameterLayout.cpp` already defines the correct cutoff and resonance parameters, including the logarithmic cutoff range, but resonance has no explicit value-text formatter yet.

## Exact `TODO.md` Entries This Blueprint Expands

These are the `Phase 8` checklist items that the execution plan below expands into concrete work:

- [ ] Add a per-voice low-pass filter to each synth voice.
- [ ] Wire cutoff and resonance to the shared parameter model.
- [ ] Add cutoff and resonance controls to the editor.
- [ ] Extend fixed MiniLab mapping so Knob 2 controls cutoff and Knob 3 controls resonance.
- [ ] Verify filter stability at 44.1 kHz and 48 kHz.
- [ ] Verify cutoff and resonance are audibly reflected in standalone playback.

## 1. Architectural Design

### 1.1 Controlling Design Decision

The filter belongs inside `SynthVoice`, not in `SynthEngine` and not in `SynthAudioProcessor`.

Required consequences:

- Every active note has its own filter state.
- Voice stealing and note retriggering must reset filter state so old note energy does not leak into the new note.
- `SynthEngine` remains a coordinator that pushes immutable per-block parameters into preallocated voices.
- `SynthAudioProcessor` remains the owner of canonical parameter state, but does not perform filter DSP itself.
- The fixed MiniLab mapping continues to target APVTS parameters only, not live voice objects.

### 1.2 Filter Primitive Choice

Use `juce::dsp::StateVariableTPTFilter<float>` configured as `lowpass`.

Why this is the right primitive for this phase:

- It is JUCE-native, which matches the project requirement to prefer JUCE DSP.
- JUCE documents the TPT structure as better behaved for cutoff changes than simpler IIR coefficient swaps.
- It supports `prepare(...)`, `reset()`, `setCutoffFrequency(...)`, `setResonance(...)`, and `processSample(...)`, which aligns with the existing per-sample `SynthVoice` render loop.
- JUCE also notes that cutoff modulation may still need additional smoothing, so this phase must add cutoff smoothing rather than trusting the filter alone.

### 1.3 Signal Flow for One Voice

The intended audio path for each voice in this phase is:

```text
MIDI note
  -> oscillator sample
  -> per-voice low-pass filter
  -> per-voice amp envelope
  -> velocity gain
  -> mixed into SynthEngine output
  -> global master-gain smoothing
```

This order keeps the existing amplitude-envelope behavior intact while making the filter audibly meaningful for subtractive-style playback.

### 1.4 Required Data Structures

#### 1.4.1 Render-Time Filter Parameters

Add a render-time filter payload in `src/synth/SynthParameters.h`.

```cpp
namespace coolsynth::synth
{
    struct FilterParameters
    {
        float cutoffHz = 10000.0f;
        float resonanceNormalized = 0.1f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        FilterParameters filter;
        coolsynth::parameters::WaveformChoice waveform =
            coolsynth::parameters::WaveformChoice::saw;
        float masterGainLinear = 1.0f;
    };

    struct ParameterValuePointers
    {
        std::atomic<float>* waveform = nullptr;
        std::atomic<float>* ampAttackMs = nullptr;
        std::atomic<float>* ampDecayMs = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampReleaseMs = nullptr;
        std::atomic<float>* filterCutoffHz = nullptr;
        std::atomic<float>* filterResonance = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
    };
}
```

Design intent:

- `filterCutoffHz` stays in real Hz because APVTS already owns the logarithmic range mapping.
- `resonanceNormalized` stays in the user-facing 0.0 to 1.0 domain because the internal DSP mapping belongs in `SynthVoice`, not in UI code and not in the parameter layout.

#### 1.4.2 Voice-Local Filter State

Add the following private members to `SynthVoice`:

```cpp
juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHzSmoother;
FilterParameters nextFilterParameters;
float lastAppliedResonanceQ = 0.0f;
```

State rules:

- `lowPassFilter` is prepared once and reused.
- `cutoffHzSmoother` is reset in `prepare(...)` using the current sample rate and a short smoothing time.
- `nextFilterParameters` is written by the engine before rendering the next block.
- `lastAppliedResonanceQ` avoids redundant `setResonance(...)` calls when the normalized value has not changed.

Recommended cutoff smoothing time:

- `0.02` seconds.

Rationale:

- It matches the existing `masterGainRampSeconds` value.
- It is long enough to soften zipper noise and short enough to keep the control feeling immediate.

### 1.5 Required Function Signatures

Add or extend the following function signatures.

In `src/synth/SynthVoice.h`:

```cpp
void setNextFilterParameters(const FilterParameters& parameters) noexcept;
```

Private helpers in `src/synth/SynthVoice.cpp`:

```cpp
static float mapNormalizedResonanceToQ(float normalized) noexcept;
float clampCutoffToPreparedRange(float cutoffHz) const noexcept;
void primeFilterForCurrentTargets() noexcept;
void resetVoiceState() noexcept;
```

In `src/synth/SynthEngine.h`:

```cpp
void pushFilterParametersToVoices(const FilterParameters& parameters) noexcept;
```

No new public `SynthAudioProcessor` API is required for this phase. Keep the processor interface stable and extend only the existing private parameter binding and render-snapshot plumbing.

### 1.6 Parameter Mapping Rules

#### 1.6.1 Cutoff

The cutoff parameter comes from APVTS in Hz and should be clamped inside the voice before being applied to the TPT filter.

Recommended clamp:

```text
minCutoffHz = 20.0f
maxCutoffHz = min(20000.0f, 0.45f * currentSampleRate)
```

Why `0.45 * sampleRate`:

- It stays comfortably below Nyquist.
- At `44.1 kHz`, the effective maximum is `19845 Hz`, which is effectively indistinguishable from `20000 Hz` in this phase.
- At `48 kHz`, the user-facing maximum remains `20000 Hz`.

#### 1.6.2 Resonance

The user-facing resonance parameter stays `0.0` to `1.0`, but the voice maps it internally to a stable TPT `Q` range.

Recommended mapping:

```text
qMin = 1 / sqrt(2) ~= 0.70710678
qMax = 8.0
r = clamp(normalized, 0.0, 1.0)
q = qMin + (qMax - qMin) * (r * r)
```

Why square the normalized value:

- It gives more usable resolution at the low-resonance end.
- It avoids making the default `0.1` setting too sharp.
- It still reaches a clearly audible high-resonance region for manual testing.

Do not expose `Q` directly as a user parameter in this phase.

### 1.7 Voice Lifecycle Rules

The filter state must be reset in every path that reuses or hard-clears a voice.

Required reset points:

- `prepare(...)`
- `startNote(...)`
- `stopNote(..., allowTailOff = false)`
- the moment `renderNextBlock(...)` detects that the envelope has fully finished and the voice is being cleared

Reset behavior:

- `lowPassFilter.reset()`
- seed `cutoffHzSmoother` with the current cutoff target using `setCurrentAndTargetValue(...)`
- apply the current resonance immediately so the next rendered sample starts from correct coefficients

This is the protection against voice-steal bleed and stale filter memory when a released voice is reused.

### 1.8 Processor and Engine Flow After the Change

The intended Phase 8 render path is:

```text
Audio thread
  -> SynthAudioProcessor::processBlock(...)
     -> read raw APVTS atomics for waveform, ADSR, cutoff, resonance, master gain
     -> build BlockRenderParameters snapshot
     -> SynthEngine::render(...)
        -> push envelope to voices
        -> push filter params to voices
        -> push waveform to voices
        -> synthesiser.renderNextBlock(...)
        -> apply global master gain

Standalone controller path (non-audio thread)
  -> Standalone MIDI callback queue
  -> SynthAudioProcessor::handleStandaloneControllerEvent(...)
  -> MidiMappingEngine::translate(...)
  -> APVTS parameter write via beginChangeGesture / setValueNotifyingHost / endChangeGesture
```

Strict rule:

- `processBlock(...)` remains read-only with respect to parameters.

## 2. File-Level Strategy

### Required Files to Touch

| File | Responsibility in `Phase 8` |
| --- | --- |
| `src/synth/SynthParameters.h` | Add render-time filter data and raw parameter pointers for cutoff and resonance. |
| `src/synth/SynthVoice.h` | Declare the per-voice filter member state and the `setNextFilterParameters(...)` entry point. |
| `src/synth/SynthVoice.cpp` | Prepare, reset, parameter-map, smooth, and process the per-voice low-pass filter inside the existing render loop. |
| `src/synth/SynthEngine.h` | Declare `pushFilterParametersToVoices(...)`. |
| `src/synth/SynthEngine.cpp` | Fan out filter parameters to the 8 preallocated voices before render. |
| `src/plugin/SynthAudioProcessor.cpp` | Bind `filterCutoffHz` and `filterResonance` raw parameter pointers and include them in `makeBlockRenderParameters()`. |
| `src/plugin/SynthAudioProcessorEditor.h` | Add filter parameter references, `HardwareKnob` members, `SynthSection filterSection`, and attachments. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Wire filter controls, refresh filter value text, and adjust the layout so the filter section is visible in both standalone and VST3 modes. |
| `src/midi/MidiMappingEngine.h` | Extend the logical target surface and active binding capacity to include cutoff and resonance. |
| `src/midi/MidiMappingEngine.cpp` | Route `Knob 2` and `Knob 3` to the existing APVTS parameters using normalized values. |
| `src/midi/Minilab3Profile.h` | Add logical targets for cutoff and resonance. |
| `src/midi/Minilab3Profile.cpp` | Add the verified `Knob 2` and `Knob 3` profile rows and binding rows without moving MiniLab constants into synth code. |
| `src/parameters/ParameterLayout.cpp` | Add explicit resonance display formatting only. Keep ranges, defaults, and IDs stable. |

### Conditional File

| File | When to touch it |
| --- | --- |
| `docs/minilab3-default-messages.md` | Touch only if the repo still lacks an explicit verified record for `Knob 2` and `Knob 3` when the implementation starts. Record the exact message facts before claiming the mapping checkbox complete. |

### Files to Leave Untouched

These files should stay untouched unless a concrete blocker appears:

| File | Why it should remain untouched |
| --- | --- |
| `src/parameters/ParameterIDs.h` | The required filter IDs already exist and must remain stable. |
| `src/plugin/SynthAudioProcessor.h` | No new public processor API is needed for this slice. |
| `src/parameters/ParameterLayout.h` | The declaration surface does not need expansion. |
| `src/standalone/StandaloneMidiInput.h` and `src/standalone/StandaloneMidiInput.cpp` | The standalone controller-ingress path already routes parameter changes off the callback thread. Phase 8 only adds new logical targets. |
| `src/ui/SynthSection.h` and `src/ui/SynthSection.cpp` | The existing section component is sufficient; use it rather than broadening the UI layer. |
| `CMakeLists.txt` | No new `.cpp` files are planned for this phase, so `target_sources(...)` should not change. |

## 3. Atomic Execution Steps

### 3.1 `[ ] Add a per-voice low-pass filter to each synth voice.`

Plan:

- Keep the filter entirely inside `SynthVoice`.
- Reuse the existing per-sample render loop rather than introducing a block-processing abstraction mid-project.
- Reset filter state whenever a voice is prepared, retriggered, or hard-cleared.
- Keep the filter mono per voice because the voice currently renders a mono signal copied to all output channels.

Act:

- Extend `SynthParameters.h` with `FilterParameters`.
- Add `lowPassFilter`, `cutoffHzSmoother`, `nextFilterParameters`, and `lastAppliedResonanceQ` to `SynthVoice`.
- In `prepare(...)`, call `lowPassFilter.prepare(spec)`, `lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass)`, `lowPassFilter.reset()`, and `cutoffHzSmoother.reset(spec.sampleRate, 0.02)`.
- Add `setNextFilterParameters(...)` to cache the block's latest filter targets.
- In `startNote(...)`, set oscillator frequency, reset oscillator state, reset the filter, prime the smoother to the current cutoff, apply resonance immediately, then start the envelope.
- In `renderNextBlock(...)`, insert the filter between oscillator generation and amplitude scaling.
- After the sample loop, call `lowPassFilter.snapToZero()`.

Validate:

- `cmake --build build --config Debug` succeeds.
- Code review confirms there is exactly one low-pass filter per `SynthVoice` and none in `SynthEngine` or `SynthAudioProcessor`.
- A stolen or retriggered voice does not inherit audible filter residue from the previous note.

### 3.2 `[ ] Wire cutoff and resonance to the shared parameter model.`

Plan:

- Preserve the existing APVTS parameter IDs, ranges, and defaults.
- Extend only the raw-parameter binding and render-snapshot path.
- Keep cutoff in Hz and resonance normalized until the voice maps resonance internally.

Act:

- Extend `ParameterValuePointers` with `filterCutoffHz` and `filterResonance`.
- In `SynthAudioProcessor::bindParameterPointers(...)`, bind the two new atomics.
- In `SynthAudioProcessor::makeBlockRenderParameters()`, load, clamp, and forward the new values.
- Recommended processor-side clamping:
  - cutoff: `juce::jlimit(20.0f, 20000.0f, rawCutoff)`
  - resonance: `juce::jlimit(0.0f, 1.0f, rawResonance)`
- In `ParameterLayout.cpp`, add `formatPercent(...)` formatting to the resonance parameter so the editor does not display an opaque raw float string.

Validate:

- There is no diff in `src/parameters/ParameterIDs.h`.
- There is no diff that changes cutoff or resonance parameter ranges or defaults in `ParameterLayout.cpp`.
- `processBlock(...)` still only reads atomics and forwards a render snapshot.
- The editor can show meaningful value text for both filter controls.

### 3.3 `[ ] Add cutoff and resonance controls to the editor.`

Plan:

- Reuse `HardwareKnob`, `SynthSection`, and APVTS attachments.
- Insert a dedicated `Filter` section between `Oscillator` and `Envelope` so the layout tracks the product requirement.
- Adjust the editor size instead of cramming controls into the existing width.

Act:

- Add these members in `SynthAudioProcessorEditor.h`:

```cpp
coolsynth::ui::SynthSection filterSection { "Filter" };
coolsynth::ui::HardwareKnob cutoffKnob { "Cutoff" };
coolsynth::ui::HardwareKnob resonanceKnob { "Resonance" };
```

- Extend `ParameterRefs` with `filterCutoffHz` and `filterResonance`.
- Add `SliderAttachment` members for cutoff and resonance.
- In the editor constructor, fetch the parameters from APVTS, add the controls, and create attachments.
- In `refreshValueDisplays()`, add `cutoffKnob` and `resonanceKnob`.
- In `resized()`, lay out four top-level sections: `Oscillator`, `Filter`, `Envelope`, and `Output`.
- Recommended minimum editor sizes:
  - standalone: `1040 x 850`
  - plugin: `1040 x 420`

Validate:

- Standalone and plugin editors open with no overlapping or truncated controls.
- `Cutoff` and `Resonance` show live value text and remain responsive during playback.
- Existing controls retain their positions and continue to function.

### 3.4 `[ ] Extend fixed MiniLab mapping so Knob 2 controls cutoff and Knob 3 controls resonance.`

Plan:

- Extend the existing mapping surface only where needed.
- Do not move any mapping logic into synth or UI code.
- Do not refactor the standalone MIDI pipeline.
- Use the existing normalized `0.0` to `1.0` parameter-write path. The APVTS parameter object already handles logarithmic cutoff mapping.

Act:

- In `Minilab3Profile.h`, add `filterCutoff` and `filterResonance` to `Minilab3LogicalTarget`.
- In `Minilab3Profile.cpp`, add control-definition rows and binding rows for `knob2` and `knob3` once their message facts are recorded.
- Expand `MidiMappingEngine::activeBindings` from `7` to `9`.
- Extend the target-to-parameter switch in `MidiMappingEngine.cpp`:
  - `filterCutoff -> parameters::ids::filterCutoffHz`
  - `filterResonance -> parameters::ids::filterResonance`
- Keep the existing `mapControllerValue(...)` linear. Do not add a second cutoff curve in the mapping engine.
- Preserve the current rule that host-notifying parameter writes occur only in `SynthAudioProcessor::applyParameterChange(...)`.

Validate:

- `Knob 2` moves the cutoff parameter and `Knob 3` moves the resonance parameter.
- Existing mappings for waveform, ADSR, master gain, and panic still work.
- `rg "setValueNotifyingHost" src` shows no new host-notifying call sites in `src/synth` or `src/standalone`.
- `processBlock(...)` still contains no parameter mutation.

### 3.5 `[ ] Verify filter stability at 44.1 kHz and 48 kHz.`

Plan:

- Stress the exact combinations that can expose instability: high resonance, low cutoff, repeated note retriggers, voice stealing, and sample-rate changes.
- Treat this as a DSP validation step, not a doc-only check.
- If instability appears, fix the cutoff clamp or reset strategy first before adding more complexity.

Act:

- Run the standalone app at `44.1 kHz` and `48 kHz`.
- At each sample rate, test these parameter extremes:
  - cutoff minimum plus resonance maximum
  - cutoff sweep from minimum to maximum while holding a note
  - resonance sweep from minimum to maximum at low-mid cutoff
  - an 8-note chord plus a ninth note to force voice stealing
- Trigger panic during sustained high-resonance playback.
- Change sample rate while the app is idle, then repeat the playback checks.

Validate:

- No crash during sample-rate changes or playback.
- No runaway output, NaNs, denormal-related stalls, or persistent DC-like blast.
- Filter behavior remains finite and musical across the supported parameter range.
- Voice stealing and panic still behave correctly with the filter enabled.

### 3.6 `[ ] Verify cutoff and resonance are audibly reflected in standalone playback.`

Plan:

- Validate audibility through both the UI and hardware control paths.
- Use a waveform with rich harmonics for cutoff tests. `Saw` is the most revealing default choice.
- Keep the test simple and repeatable.

Act:

- Set waveform to `saw`.
- Hold a single note, then a 4-note chord.
- Sweep cutoff from max to min using the editor control.
- Sweep resonance from min to max at a mid-low cutoff using the editor control.
- Repeat both sweeps using `Knob 2` and `Knob 3` on the MiniLab.

Validate:

- Lowering cutoff clearly darkens the sound.
- Increasing resonance clearly emphasizes the cutoff region.
- `Knob 2` and `Knob 3` update the editor and the audible output in lockstep.
- The shared editor path behaves the same in standalone and remains ready for later VST3 use.

### Closeout Rule After All Six Checks Pass

Only after all six validations pass:

- update `TODO.md`
- move the completed items to `DONE.md`
- update `docs/minilab3-default-messages.md` if new verified controller facts were established during the phase

## 4. Edge Case and Boundary Audit

| Failure mode | Why it can happen in the current codebase | Required mitigation in `Phase 8` |
| --- | --- | --- |
| Global filter shortcut | It is tempting to insert one filter after voice mixing because it is fewer lines of code. | Reject that shortcut in code review. The filter instance must live in `SynthVoice`. |
| Voice-steal timbre bleed | A reused voice can carry filter state from the previous note if `startNote(...)` does not reset it. | Reset and prime the filter every time a voice starts a new note. |
| Hard stop leaves stale filter state | `stopNote(..., false)` currently clears the envelope and note but has no filter cleanup. | Reset filter state on hard stop and on envelope-finished note clear. |
| Cutoff near Nyquist behaves poorly | `20000 Hz` is close to Nyquist at `44.1 kHz`. | Clamp cutoff in the voice to `min(20000, 0.45 * sampleRate)`. |
| Resonance max is unstable or unusable | A normalized `1.0` value mapped too aggressively can make the filter ring or clip badly. | Use a bounded internal `Q` mapping and validate it at both supported sample rates. |
| Cutoff zipper noise | Directly calling `setCutoffFrequency(...)` with abrupt parameter jumps can click. | Use multiplicative smoothing for cutoff. |
| Resonance display text is poor | The current parameter has no explicit formatter. | Add a resonance text formatter in `ParameterLayout.cpp`. |
| Filter state denormals | Sustained silence after high resonance can leave tiny state values. | Call `snapToZero()` and rely on `ScopedNoDenormals` in `processBlock(...)`. |
| Null raw parameter pointers | Forgetting to bind new APVTS parameters will crash or silently no-op later. | Extend `ParameterValuePointers` and `bindParameterPointers(...)` in the same edit slice. |
| UI control works but sound does not | Attachments can update UI text while audio-side snapshot plumbing remains incomplete. | Validate both UI value display and audible playback before closing the checkbox. |
| Hardware control works in UI only | `Knob 2` and `Knob 3` can update APVTS while the processor ignores filter atomics. | Validate the full path: hardware -> APVTS -> processor snapshot -> voice DSP. |
| Host-notifying writes leak into audio thread | A future shortcut could route incoming CCs through `processBlock(...)`. | Keep all MiniLab writes on the existing off-audio standalone path and grep for new call sites. |
| Layout crowding | The current `900 px` width is too narrow for a fourth synth section. | Widen the editor rather than stacking controls incoherently. |
| Knob 2 and Knob 3 guessed instead of verified | The repo currently records them as deferred, not verified. | Promote the real control facts into `Minilab3Profile.cpp` and docs before declaring success. |

## 5. Verification Protocol

### 5.1 Preflight Checklist

Before coding begins:

1. Confirm `Phase 7` remains green and the current standalone controller path works.
2. Confirm `filterCutoffHz` and `filterResonance` still exist unchanged in `src/parameters/ParameterIDs.h` and `src/parameters/ParameterLayout.cpp`.
3. Confirm whether `Knob 2` and `Knob 3` already have verified message facts. If not, record them first in the MiniLab profile artifacts before claiming the mapping checkbox complete.

### 5.2 Mandatory Automated Checks Available in the Current Repo

The repo does not currently have a persistent test harness, so the required automated validation for this phase is build-plus-regression inspection.

1. Run `cmake --build build --config Debug`.
2. Run `rg "StateVariableTPTFilter|lowPassFilter" src/synth src/plugin` and confirm the live filter object exists only in `src/synth/SynthVoice.*`.
3. Run `rg "setValueNotifyingHost|beginChangeGesture|endChangeGesture" src` and confirm the new phase does not introduce host-notifying parameter writes in `src/synth` or `src/standalone`.
4. Run `rg "filterCutoffHz|filterResonance" src/plugin/SynthAudioProcessor.cpp src/synth/SynthParameters.h src/plugin/SynthAudioProcessorEditor.cpp src/midi` and confirm all four plumbing surfaces were touched.
5. Check the build output for new warnings. New warnings fail the phase.

### 5.3 Optional Microtests if a Lightweight Harness Is Approved

These are worth adding only if the reviewer explicitly approves a tiny local test harness. Do not create a full testing subsystem as part of this phase.

1. `MidiMappingEngine` translation test: `Knob 2` message maps to `filterCutoffHz` and `Knob 3` maps to `filterResonance`.
2. `SynthVoice` helper test: `mapNormalizedResonanceToQ(0.0f)` returns approximately `0.7071`, and `mapNormalizedResonanceToQ(1.0f)` stays finite and bounded.
3. `SynthVoice` lifecycle test: `startNote(...)` after a hard stop or panic begins from a reset filter state.

### 5.4 Manual UX Checks

Run these checks in order.

1. Launch the standalone app and confirm the new `Filter` section appears without overlapping the existing sections.
2. Hold a `saw` note and sweep `Cutoff` from maximum to minimum using the editor.
3. Keep cutoff around the lower-middle range and sweep `Resonance` from minimum to maximum using the editor.
4. Repeat steps 2 and 3 with a 4-note chord.
5. Use `Knob 2` and confirm the cutoff knob and audible tone move together.
6. Use `Knob 3` and confirm the resonance knob and audible tone move together.
7. Switch the standalone device to `44.1 kHz`, repeat the sweeps, and confirm stability.
8. Switch the standalone device to `48 kHz`, repeat the sweeps, and confirm stability.
9. Force voice stealing with at least nine held notes while cutoff is low and resonance is high; confirm the app remains stable.
10. Trigger panic during sustained playback and confirm active notes stop immediately.

### 5.5 Exit Gate

The phase is ready to move to `DONE.md` only when all of the following are true:

- The filter object is per voice, not global.
- The editor exposes cutoff and resonance with clear value text.
- `Knob 2` and `Knob 3` map through the existing off-audio APVTS path.
- Playback is stable at `44.1 kHz` and `48 kHz` across the supported parameter range.
- No new warnings were introduced.
- The MiniLab profile artifacts record the actual hardware facts used by the code.

## 6. Code Scaffolding

These snippets are structural templates, not copy-paste final code. Keep changes inside existing files unless a reviewer explicitly asks for a new module.

### 6.1 `SynthParameters.h` Template

```cpp
namespace coolsynth::synth
{
    struct FilterParameters
    {
        float cutoffHz = 10000.0f;
        float resonanceNormalized = 0.1f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        FilterParameters filter;
        coolsynth::parameters::WaveformChoice waveform =
            coolsynth::parameters::WaveformChoice::saw;
        float masterGainLinear = 1.0f;
    };
}
```

### 6.2 `SynthVoice.h` Template

```cpp
class SynthVoice final : public juce::SynthesiserVoice
{
public:
    SynthVoice();

    void prepare(const juce::dsp::ProcessSpec& spec);
    void setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;
    void setNextFilterParameters(const FilterParameters& parameters) noexcept;
    void setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept;

    // existing SynthesiserVoice overrides...

private:
    static float mapNormalizedResonanceToQ(float normalized) noexcept;
    float clampCutoffToPreparedRange(float cutoffHz) const noexcept;
    void primeFilterForCurrentTargets() noexcept;
    void resetVoiceState() noexcept;

    juce::dsp::Oscillator<float> oscillator;
    juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHzSmoother;
    juce::ADSR ampEnvelope;
    EnvelopeParameters nextEnvelopeParameters;
    FilterParameters nextFilterParameters;
    float lastAppliedResonanceQ = 0.0f;
    // existing scalar fields...
};
```

### 6.3 `SynthVoice::renderNextBlock(...)` Skeleton

```cpp
void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                 int startSample,
                                 int numSamples)
{
    if (!isVoiceActive())
        return;

    ampEnvelope.setParameters(makeJuceEnvelopeParameters());

    const auto clampedCutoffHz = clampCutoffToPreparedRange(nextFilterParameters.cutoffHz);
    cutoffHzSmoother.setTargetValue(clampedCutoffHz);

    const auto nextQ = mapNormalizedResonanceToQ(nextFilterParameters.resonanceNormalized);
    if (nextQ != lastAppliedResonanceQ)
    {
        lowPassFilter.setResonance(nextQ);
        lastAppliedResonanceQ = nextQ;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        lowPassFilter.setCutoffFrequency(cutoffHzSmoother.getNextValue());

        const float raw = oscillator.processSample(0.0f);
        const float filtered = lowPassFilter.processSample(0, raw);
        const float env = ampEnvelope.getNextSample();
        const float sampleValue = filtered * env * velocityGain;

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample(channel, startSample + sample, sampleValue);

        if (!ampEnvelope.isActive())
        {
            resetVoiceState();
            clearCurrentNote();
            break;
        }
    }

    lowPassFilter.snapToZero();
}
```

### 6.4 Editor Wiring Template

```cpp
struct ParameterRefs
{
    juce::RangedAudioParameter* waveform = nullptr;
    juce::RangedAudioParameter* filterCutoffHz = nullptr;
    juce::RangedAudioParameter* filterResonance = nullptr;
    juce::RangedAudioParameter* ampAttackMs = nullptr;
    juce::RangedAudioParameter* ampDecayMs = nullptr;
    juce::RangedAudioParameter* ampSustain = nullptr;
    juce::RangedAudioParameter* ampReleaseMs = nullptr;
    juce::RangedAudioParameter* masterGainDb = nullptr;
};

coolsynth::ui::SynthSection filterSection { "Filter" };
coolsynth::ui::HardwareKnob cutoffKnob { "Cutoff" };
coolsynth::ui::HardwareKnob resonanceKnob { "Resonance" };

std::unique_ptr<SliderAttachment> cutoffAttachment;
std::unique_ptr<SliderAttachment> resonanceAttachment;
```

### 6.5 MiniLab Binding Template

Do not guess CC numbers in final code. Use placeholders until the verified facts are recorded.

```cpp
enum class Minilab3LogicalTarget : uint8_t
{
    waveform,
    filterCutoff,
    filterResonance,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    masterGain,
    panic,
};

constexpr std::array<Minilab3Binding, 9> phase7Bindings {{
    { "knob1", 1, 74, 1, Minilab3LogicalTarget::waveform, true },
    { "knob2", 1, /* verified CC */, 1, Minilab3LogicalTarget::filterCutoff, true },
    { "knob3", 1, /* verified CC */, 1, Minilab3LogicalTarget::filterResonance, true },
    { "knob4", 1, 77, 1, Minilab3LogicalTarget::ampAttack, true },
    { "knob5", 1, 93, 1, Minilab3LogicalTarget::ampDecay, true },
    { "knob6", 1, 18, 1, Minilab3LogicalTarget::ampSustain, true },
    { "knob7", 1, 19, 1, Minilab3LogicalTarget::ampRelease, true },
    { "fader1", 1, 82, 1, Minilab3LogicalTarget::masterGain, true },
    { "pad8", 2, 43, 10, Minilab3LogicalTarget::panic, true },
}};
```

Implementation note:

- Keep the existing table name if you want the smallest diff.
- Do not spend this phase renaming `getPhase7Bindings()` unless the reviewer explicitly asks for cleanup. The filter milestone is about behavior, not API cosmetics.

## 7. Summary of the Smallest Correct Change

The smallest correct `Phase 8` implementation is:

1. Extend the existing render snapshot with filter parameters.
2. Add one TPT low-pass filter plus cutoff smoothing inside `SynthVoice`.
3. Fan out filter parameters in `SynthEngine`.
4. Add a `Filter` section to the shared editor with two attached knobs.
5. Extend the existing MiniLab binding table and mapping switch for `Knob 2` and `Knob 3`.
6. Validate stability at `44.1 kHz` and `48 kHz` before touching `TODO.md` or `DONE.md`.

Anything larger than that is probably scope drift.
