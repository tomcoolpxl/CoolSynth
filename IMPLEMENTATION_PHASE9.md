<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 9 Blueprint: Global Delay Slice

## Phase Selection

Selected phase: `Phase 9 - Global delay slice`

Selection basis:

- `TODO.md` currently shows `Phase 7` and `Phase 8` complete, with no newer phase promoted into the active checklist yet.
- `DONE.md` records verified completion through `Phase 8`.
- `IMPLEMENTATION_PLAN.md` explicitly places `Phase 9` next and isolates it from the later UI-refinement, VST3 smoke, persistence, and MIDI-learn work.
- The current source tree already includes the `Phase 8` filter slice, but there is no shared delay module in `src/synth`, no delay plumbing in the render snapshot, and no editor or MiniLab wiring for delay yet.
- The repository already contains `IMPLEMENTATION_PHASE1.md` through `IMPLEMENTATION_PHASE8.md`, so the next blueprint file should be `IMPLEMENTATION_PHASE9.md`.

## Scope Guardrails

In scope for this phase:

- Add one shared global delay stage after voice mixing and before master gain.
- Use JUCE-native DSP primitives and preallocated state only.
- Drive delay time, feedback, and mix from the existing APVTS parameter model.
- Add delay controls to the shared editor without redesigning the whole UI.
- Extend the fixed MiniLab 3 mapping so `Knob 8` controls `delayMix`, `Fader 2` controls `delayFeedback`, and `Fader 3` controls `delayTimeMs` once their message facts are verified.
- Keep audio-thread behavior real-time safe during manual parameter changes and sample-rate changes.

Explicitly out of scope for this phase:

- Ping-pong delay, stereo widening, cross-feedback, tempo sync, dotted/triplet modes, or tap tempo.
- Delay modulation, ducking, filtering in the feedback path, saturation, or any effect chain redesign.
- A separate delay-enable parameter.
- Clearing the delay tail on panic.
- A general UI redesign beyond making the delay controls visible and usable.
- MIDI learn, preset behavior, standalone persistence, or host-focused automation polish beyond preserving the shared parameter model.
- Refactoring the existing fixed-mapping API names just because `getPhase7Bindings()` is now carrying later fixed mappings too.

Decision gates before implementation:

- `src/parameters/ParameterIDs.h` already contains `delayTimeMs`, `delayFeedback`, and `delayMix`; do not rename IDs or alter defaults without a requirements change.
- `src/parameters/ParameterLayout.cpp` already defines the correct ranges and value-text formatters for all three delay parameters; do not touch it unless a concrete defect is found.
- `juce::dsp::DelayLine::setMaximumDelayInSamples(...)` may allocate internally, so it must only run in `prepare(...)` or another non-audio-thread lifecycle path.
- Because delay time can change while audio is running, the implementation should use `pushSample(...)` and `popSample(...)`, not `DelayLine::process(...)`.
- `docs/minilab3-default-messages.md` still defers `Knob 8`, `Fader 2`, and `Fader 3`. The mapping checkbox is not complete until the code and docs record verified facts for those controls.

## Current Code Anchors

The blueprint should stay anchored to the code that currently owns adjacent behavior:

- `src/synth/SynthEngine.h` and `src/synth/SynthEngine.cpp` already own voice preparation, render ordering, and final master-gain smoothing.
- `src/synth/SynthVoice.h` and `src/synth/SynthVoice.cpp` already own per-voice oscillator, ADSR, and filter DSP. Delay does not belong there.
- `src/synth/SynthParameters.h` already carries render-time envelope and filter state, plus raw parameter pointers, but no delay state.
- `src/plugin/SynthAudioProcessor.cpp` already binds raw APVTS atomics and builds a `BlockRenderParameters` snapshot once per block. It currently ignores the three delay parameters even though they exist in APVTS.
- `src/plugin/SynthAudioProcessorEditor.h` and `src/plugin/SynthAudioProcessorEditor.cpp` already expose `Oscillator`, `Filter`, `Envelope`, and `Output` sections. There is no `Delay` section, no delay attachments, and no delay value-refresh path.
- `src/midi/MidiMappingEngine.h` and `src/midi/MidiMappingEngine.cpp` already translate fixed MiniLab events into APVTS writes on the non-audio standalone path. There are no logical targets for delay mix, feedback, or time yet.
- `src/midi/Minilab3Profile.h` and `src/midi/Minilab3Profile.cpp` already isolate MiniLab-specific constants and bindings. The current verified table ends at `Knob 7`, `Fader 1`, and `Pad 8`.
- `CMakeLists.txt` uses an explicit `target_sources(...)` list, so any new `.cpp` file must be added there or the build will fail.

## Exact `TODO.md` Entries This Blueprint Expands

These are the `Phase 9` checklist items from `IMPLEMENTATION_PLAN.md` that the execution plan below expands into concrete work:

- [ ] Add the global delay effect after voice mixing.
- [ ] Wire delay time, feedback, and mix to the shared parameter model.
- [ ] Add delay controls to the editor.
- [ ] Extend fixed MiniLab mapping for delay mix, feedback, and time.
- [ ] Clamp feedback to the safe maximum.
- [ ] Verify manual delay-time changes remain stable and real-time safe.

## 1. Architectural Design

### 1.1 Controlling Design Decision

The delay belongs in shared synth-core orchestration, not inside each voice and not inside standalone UI code.

Required consequences:

- `SynthVoice` stays responsible for per-note sound generation only.
- `SynthEngine` owns the global effect ordering and should own a new `GlobalDelay` helper.
- `SynthAudioProcessor` remains the canonical source of parameter state, but it does not implement the delay DSP itself.
- Standalone controller input continues to target APVTS parameters only. Hardware messages must not reach `GlobalDelay` directly.
- The VST3 target stays viable because the delay sits in shared processor-rendered audio, not in the standalone shell.

### 1.2 JUCE Primitive Choice

Use `juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>` inside a new shared module, for example `src/synth/GlobalDelay.{h,cpp}`.

Why this is the right primitive for this phase:

- It is JUCE-native and matches the project requirement to prefer JUCE DSP.
- JUCE explicitly documents `pushSample(...)` and `popSample(...)` as the right API when delay time changes during playback or when feedback is involved.
- It allows the delay time to be expressed in fractional samples, which keeps manual delay-time sweeps stable enough for first-release use.
- It avoids inventing a custom circular-buffer layer for a problem JUCE already solves.

Why `Linear` interpolation is the right starting point:

- It is the simplest JUCE interpolation mode that supports real-time delay changes.
- The phase goal is stable manual control, not premium modulation tone.
- If later listening tests show unacceptable dulling in the feedback path, interpolation quality can be revisited after the first delay slice is working.

### 1.3 Signal Flow After the Change

The intended render path for this phase is:

```text
Audio thread
  -> SynthAudioProcessor::processBlock(...)
     -> clear output buffer
     -> process panic flag if set
     -> read raw APVTS atomics for waveform, ADSR, filter, delay, master gain
     -> build BlockRenderParameters snapshot
     -> SynthEngine::render(...)
        -> push envelope to voices
        -> push filter params to voices
        -> push waveform to voices
        -> synthesiser.renderNextBlock(...)
        -> globalDelay.process(outputBuffer, parameters.delay)
        -> apply master gain smoothing

Standalone controller path (non-audio thread)
  -> Standalone MIDI callback queue
  -> SynthAudioProcessor::handleStandaloneControllerEvent(...)
  -> MidiMappingEngine::translate(...)
  -> APVTS parameter write via beginChangeGesture / setValueNotifyingHost / endChangeGesture
```

Strict rules:

- `processBlock(...)` remains read-only with respect to parameters.
- Delay processing must happen after the voice mix is already in the output buffer.
- Delay processing must happen before master gain so `masterGainDb` stays the final output stage.
- Panic stops voices only. The delay tail is allowed to decay naturally in this phase.

### 1.4 Required Data Structures

#### 1.4.1 Render-Time Delay Parameters

Extend `src/synth/SynthParameters.h` with render-time delay state.

```cpp
namespace coolsynth::synth
{
    struct DelayParameters
    {
        float timeMs = 250.0f;
        float feedback = 0.25f;
        float mix = 0.0f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        FilterParameters filter;
        DelayParameters delay;
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
        std::atomic<float>* delayTimeMs = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayMix = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
    };
}
```

Design intent:

- Keep delay time in milliseconds until the DSP layer converts it to samples.
- Keep feedback and mix in normalized user-facing units because the effect module should own the final safety clamps.
- Do not add an enable flag. `mix == 0.0f` is the practical bypass behavior.

#### 1.4.2 Global Delay State

Add a new `GlobalDelay` class in `src/synth/GlobalDelay.{h,cpp}` with the following core state:

```cpp
class GlobalDelay final
{
public:
    void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer,
                 const DelayParameters& parameters) noexcept;

private:
    using DelayLine =
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

    static constexpr float minDelayTimeMs = 1.0f;
    static constexpr float maxDelayTimeMs = 1000.0f;
    static constexpr float maxFeedback = 0.85f;
    static constexpr double parameterSmoothingSeconds = 0.02;

    float clampDelayMs(float timeMs) const noexcept;
    float clampFeedback(float feedback) const noexcept;
    float clampMix(float mix) const noexcept;
    float delayMsToSamples(float timeMs) const noexcept;
    void updateTargets(const DelayParameters& parameters) noexcept;

    DelayLine delayLine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamplesSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    double currentSampleRate = 0.0;
    int preparedOutputChannels = 0;
    int maximumDelaySamples = 0;
    bool prepared = false;
};
```

State rules:

- `delayLine` is prepared once per audio-device configuration and reused during normal playback.
- `delaySamplesSmoother`, `feedbackSmoother`, and `mixSmoother` are reset in `prepare(...)`, then target-updated once per block.
- `maximumDelaySamples` is recomputed from the current sample rate in `prepare(...)` only.
- `preparedOutputChannels` must match the processor output layout so mono and stereo buses both work.

Recommended maximum-delay calculation:

```text
maximumDelaySamples = ceil(sampleRate * 1.0 seconds) + 4
```

Why the extra padding:

- It leaves room for interpolation edge handling.
- It avoids an avoidable off-by-one boundary at the exact maximum setting.

### 1.5 Required Function Signatures

Add or extend the following signatures.

In `src/synth/GlobalDelay.h`:

```cpp
void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
void reset() noexcept;
void process(juce::AudioBuffer<float>& buffer,
             const DelayParameters& parameters) noexcept;
```

Private helpers in `src/synth/GlobalDelay.cpp`:

```cpp
float clampDelayMs(float timeMs) const noexcept;
float clampFeedback(float feedback) const noexcept;
float clampMix(float mix) const noexcept;
float delayMsToSamples(float timeMs) const noexcept;
void updateTargets(const DelayParameters& parameters) noexcept;
```

In `src/synth/SynthEngine.h`:

```cpp
// No new public method required.
// Add only a private GlobalDelay member and lifecycle calls.
```

In `src/plugin/SynthAudioProcessor.cpp`:

```cpp
// No new public processor API required.
// Extend bindParameterPointers(...) and makeBlockRenderParameters() only.
```

Conditional signature if the reviewer wants host-tail correctness now rather than in `Phase 11`:

```cpp
double getTailLengthSeconds() const override;
```

Recommendation:

- Keep this conditional. It is technically correct to revisit `getTailLengthSeconds()` once delay exists, but it is not required to make the standalone-first delay slice work.

### 1.6 Parameter Mapping and Safety Rules

#### 1.6.1 Delay Time

Processor-side clamp before building the render snapshot:

```text
timeMs = jlimit(1.0f, 1000.0f, rawTimeMs)
```

DSP-side clamp before converting to samples:

```text
timeMs = clamp to [1.0, 1000.0]
delaySamples = timeMs * sampleRate / 1000.0
delaySamples = min(delaySamples, maximumDelaySamples - 1)
```

Required rule:

- Do not resize the delay line when time changes. Only change the read position through `popSample(..., delayInSamples)`.

#### 1.6.2 Feedback

Required clamp at two layers:

```text
processor snapshot: jlimit(0.0f, 0.85f, rawFeedback)
GlobalDelay::clampFeedback(...): jlimit(0.0f, 0.85f, feedback)
```

Why both layers:

- APVTS already constrains the parameter, but the DSP layer should remain safe even if a future call path bypasses the UI or mapping assumptions.

#### 1.6.3 Mix

Required clamp:

```text
mix = jlimit(0.0f, 1.0f, rawMix)
```

Behavior rule:

- `mix == 0.0f` must produce a dry output signal.
- Do not add a separate bypass state machine in this phase.
- Do not clear the delay line just because mix returns to zero.

#### 1.6.4 Smoothing

Use control-rate smoothing for all three delay parameters.

Recommended smoothing times:

- delay samples: `0.02` seconds, linear
- feedback: `0.02` seconds, linear
- mix: `0.02` seconds, linear

Rationale:

- Mix changes become less click-prone.
- Feedback changes are less abrupt at high values.
- Delay-time changes still may produce brief pitch-smear artifacts, which is acceptable in this phase, but smoothing reduces the worst discontinuities.

### 1.7 Processing Algorithm

The intended per-sample algorithm inside `GlobalDelay::process(...)` is:

```text
for each sample:
  read current smoothed delaySamples, feedback, mix
  for each output channel:
    dry = buffer[channel][sample]
    wet = delayLine.popSample(channel, delaySamples)
    feedbackInput = dry + (wet * feedback)
    delayLine.pushSample(channel, feedbackInput)
    output = dry + ((wet - dry) * mix)
    buffer[channel][sample] = output
```

Important consequences:

- The delay is inserted in-place over the already-rendered voice mix.
- Feedback is built from the delayed signal only, not from the post-mix output.
- `mix` crossfades between dry and wet at the final output stage.
- The effect remains dual-mono or stereo-correct according to the current bus layout without introducing cross-channel behavior.

### 1.8 Lifecycle Rules

Prepare path:

- `SynthEngine::prepare(...)` must call `globalDelay.prepare(sampleRate, samplesPerBlock, outputChannelCount)` after the output format is known.
- `GlobalDelay::prepare(...)` must compute and allocate its maximum internal delay size outside the callback.
- After `prepare(...)`, reset the delay line and seed all three smoothers with the current parameter defaults.

Release path:

- `SynthEngine::releaseResources()` should call `globalDelay.reset()` and mark the engine unprepared.
- `GlobalDelay::reset()` should clear internal history and leave the object safe for the next `prepare(...)`.

Panic path:

- `SynthEngine::panic()` should continue to stop voices only.
- Do not clear the delay tail in this phase.

Sample-rate change path:

- Recompute `maximumDelaySamples` on every `prepare(...)`.
- Reset the delay line during audio-device reprepare.
- Validate both `44.1 kHz` and `48 kHz` after any code that touches sample-to-time conversion.

## 2. File-Level Strategy

### Required Files to Touch

| File | Responsibility in `Phase 9` |
| --- | --- |
| `CMakeLists.txt` | Register the new `src/synth/GlobalDelay.cpp` source file because the build uses an explicit source list. |
| `src/synth/SynthParameters.h` | Add `DelayParameters`, extend `BlockRenderParameters`, and bind raw APVTS pointers for the three delay parameters. |
| `src/synth/GlobalDelay.h` | Declare the new shared delay module, its lifecycle, safety constants, and helper signatures. |
| `src/synth/GlobalDelay.cpp` | Implement delay preparation, reset, smoothing, safety clamps, and in-place sample processing with JUCE `DelayLine`. |
| `src/synth/SynthEngine.h` | Add the `GlobalDelay` member and include the new module. |
| `src/synth/SynthEngine.cpp` | Prepare the delay, run it after the voice mix and before master gain, and reset it during resource release. |
| `src/plugin/SynthAudioProcessor.cpp` | Bind `delayTimeMs`, `delayFeedback`, and `delayMix` raw atomics and include them in `makeBlockRenderParameters()`. |
| `src/plugin/SynthAudioProcessorEditor.h` | Add delay parameter references, three `HardwareKnob` controls, `SynthSection delaySection`, and attachments. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Wire delay controls, refresh value text, and expand the shared editor layout so the delay section is visible in standalone and VST3 builds. |
| `src/midi/MidiMappingEngine.h` | Increase active binding capacity and preserve the fixed-mapping surface for new delay targets. |
| `src/midi/MidiMappingEngine.cpp` | Route the three new logical targets to the existing APVTS delay parameters using normalized writes only. |
| `src/midi/Minilab3Profile.h` | Add logical targets for `delayMix`, `delayFeedback`, and `delayTime`. |
| `src/midi/Minilab3Profile.cpp` | Add verified control-definition rows and fixed-binding rows for `Knob 8`, `Fader 2`, and `Fader 3`. |
| `docs/minilab3-default-messages.md` | Record the verified hardware facts used by the new delay mapping and remove the corresponding deferred status. |
| `README.md` | Update the current status and document any known first-release behavior limits, especially minor artifacts during manual delay-time changes if they are observed. |

### Conditional Files

| File | When to touch it |
| --- | --- |
| `src/plugin/SynthAudioProcessor.h` | Touch only if this phase also corrects `getTailLengthSeconds()` now that the shared processor contains a real delay tail. |
| `src/parameters/ParameterLayout.cpp` | Touch only if the repo has drifted away from the required delay parameter ranges or display text. Based on the current code, this file should stay unchanged. |
| `TODO.md` | Touch only after implementation and validation succeed. This blueprint should not preemptively update it. |
| `DONE.md` | Touch only after review and verification are complete. |

### Files to Leave Untouched

These files should remain untouched unless a concrete blocker appears:

| File | Why it should remain untouched |
| --- | --- |
| `src/parameters/ParameterIDs.h` | The delay parameter IDs already exist and must remain stable. |
| `src/parameters/ParameterLayout.h` | The declaration surface does not need expansion for this phase. |
| `src/synth/SynthVoice.h` and `src/synth/SynthVoice.cpp` | Delay is global, not per voice. Do not smuggle global effect logic into voice code. |
| `src/standalone/StandaloneMidiInput.h` and `src/standalone/StandaloneMidiInput.cpp` | The existing off-audio-thread controller path is already correct. Only the mapping tables need extension. |
| `src/ui/SynthSection.h` and `src/ui/SynthSection.cpp` | The existing section container is sufficient for adding a delay area. Phase 10 handles broader UI refinement. |

## 3. Atomic Execution Steps

### 3.1 `[ ] Add the global delay effect after voice mixing.`

Plan:

- Keep the delay in a new shared-core helper owned by `SynthEngine`.
- Reuse the existing render order instead of introducing a new DSP graph abstraction.
- Process the effect in place over the output buffer after the voice mix is complete.

Act:

- Add `src/synth/GlobalDelay.{h,cpp}`.
- In `GlobalDelay::prepare(...)`, compute `maximumDelaySamples`, call `delayLine.setMaximumDelayInSamples(...)`, prepare the line for the current output channel count, reset internal state, and seed all smoothers.
- In `SynthEngine.h`, add `GlobalDelay globalDelay;`.
- In `SynthEngine::prepare(...)`, prepare the global delay once the output channel count is known.
- In `SynthEngine::render(...)`, keep the current voice fan-out, call `synthesiser.renderNextBlock(...)`, then call `globalDelay.process(outputBuffer, parameters.delay)`, then apply master gain.
- In `SynthEngine::releaseResources()`, reset the delay state and clear the prepared flag.

Validate:

- `cmake --build build --config Debug` succeeds with the new source file added to `CMakeLists.txt`.
- Code review confirms the delay stage exists exactly once in shared synth-core code and sits after voice mixing, not inside `SynthVoice` and not inside standalone-only code.
- With `delayMix > 0`, repeats remain audible after the note is released.

### 3.2 `[ ] Wire delay time, feedback, and mix to the shared parameter model.`

Plan:

- Preserve the existing APVTS parameter IDs, ranges, defaults, and value text.
- Extend only the raw-pointer binding and block-snapshot path.
- Keep the processor read-only with respect to parameters during `processBlock(...)`.

Act:

- Extend `ParameterValuePointers` with `delayTimeMs`, `delayFeedback`, and `delayMix`.
- Extend `BlockRenderParameters` with a `DelayParameters delay` field.
- In `SynthAudioProcessor::bindParameterPointers(...)`, bind the three new raw parameter atomics.
- In `SynthAudioProcessor::makeBlockRenderParameters()`, load, clamp, and forward the three values.
- Recommended processor-side clamps:
  - `timeMs`: `juce::jlimit(1.0f, 1000.0f, rawTimeMs)`
  - `feedback`: `juce::jlimit(0.0f, 0.85f, rawFeedback)`
  - `mix`: `juce::jlimit(0.0f, 1.0f, rawMix)`

Validate:

- There is no diff in `src/parameters/ParameterIDs.h`.
- There is no diff that changes delay parameter defaults or ranges in `src/parameters/ParameterLayout.cpp` unless a real defect was found first.
- `processBlock(...)` still only reads atomics and forwards a render snapshot.
- Grep confirms all four plumbing surfaces were touched: parameter binding, render snapshot, engine call site, and effect module.

### 3.3 `[ ] Add delay controls to the editor.`

Plan:

- Reuse `HardwareKnob`, `SynthSection`, and APVTS attachments.
- Add a dedicated `Delay` section without turning Phase 9 into the full layout-refinement milestone.
- Keep the current timer-driven value display path so the new controls behave like the existing ones.

Act:

- In `SynthAudioProcessorEditor.h`, add these members:

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
    juce::RangedAudioParameter* delayTimeMs = nullptr;
    juce::RangedAudioParameter* delayFeedback = nullptr;
    juce::RangedAudioParameter* delayMix = nullptr;
    juce::RangedAudioParameter* masterGainDb = nullptr;
};

coolsynth::ui::SynthSection delaySection { "Delay" };
coolsynth::ui::HardwareKnob delayTimeKnob { "Time" };
coolsynth::ui::HardwareKnob delayFeedbackKnob { "Feedback" };
coolsynth::ui::HardwareKnob delayMixKnob { "Mix" };
```

- Add `SliderAttachment` members for all three delay controls.
- In the editor constructor, fetch the new APVTS parameters, add the controls, and create attachments.
- In `refreshValueDisplays()`, update the new knobs using canonical parameter text.
- In `resized()`, insert the `Delay` section between `Envelope` and `Output` or widen the synth row enough to preserve readable spacing.
- Recommended minimum editor sizes for this phase:
  - standalone: `1280 x 850`
  - plugin: `1280 x 420`

Validate:

- The standalone and plugin editors open without overlapping or clipped controls.
- `Time`, `Feedback`, and `Mix` show live value text.
- Existing filter, envelope, waveform, master-gain, and panic controls retain their current behavior.

### 3.4 `[ ] Extend fixed MiniLab mapping for delay mix, feedback, and time.`

Plan:

- Reuse the existing non-audio-thread APVTS mapping path.
- Do not add controller-specific logic in synth or UI code.
- Do not guess the new CC numbers. Verify them first with the existing MIDI-monitor workflow.

Act:

- In `Minilab3Profile.h`, add `delayMix`, `delayFeedback`, and `delayTime` to `Minilab3LogicalTarget`.
- In `Minilab3Profile.cpp`, add verified control-definition rows for:
  - `knob8 -> delay mix`
  - `fader2 -> delay feedback`
  - `fader3 -> delay time`
- Extend the fixed binding table with those three controls once their actual message facts are confirmed.
- Increase `MidiMappingEngine::activeBindings` capacity from `9` to `12`.
- Extend the target-to-parameter switch in `MidiMappingEngine.cpp`:
  - `delayMix -> parameters::ids::delayMix`
  - `delayFeedback -> parameters::ids::delayFeedback`
  - `delayTime -> parameters::ids::delayTimeMs`
- Keep `mapControllerValue(...)` linear. Let the APVTS parameter object own any logarithmic time mapping.

Validate:

- `Knob 8`, `Fader 2`, and `Fader 3` move the correct editor controls.
- Existing mappings for waveform, filter, ADSR, master gain, and panic still work.
- Grep still shows no host-notifying parameter writes in `src/synth` or `src/standalone`.

### 3.5 `[ ] Clamp feedback to the safe maximum.`

Plan:

- Treat feedback safety as a hard invariant, not a UI convenience.
- Use defense in depth: parameter layer, processor snapshot, and DSP module should all agree that `0.85` is the hard ceiling.
- Do not add a limiter or hidden auto-ducking to compensate for unclamped feedback.

Act:

- Preserve the existing `0.0 .. 0.85` APVTS range in `ParameterLayout.cpp`.
- Clamp the raw delay-feedback atomic again in `makeBlockRenderParameters()`.
- Clamp feedback a third time in `GlobalDelay::clampFeedback(...)` before applying it.
- Ensure the feedback write uses:

```text
feedbackInput = dry + (wet * feedback)
```

not a recursive expression that can exceed the intended bound more aggressively.

Validate:

- Maxed feedback decays or sustains musically, but does not run away indefinitely.
- Repeated notes at maximum feedback remain bounded at both `44.1 kHz` and `48 kHz`.
- The maximum effective feedback value in the DSP path cannot exceed `0.85` even if future call paths bypass the UI.

### 3.6 `[ ] Verify manual delay-time changes remain stable and real-time safe.`

Plan:

- Solve this at the root: no buffer resizing, no allocations, and no block-level `DelayLine::process(...)` shortcut that assumes fixed time.
- Accept small pitch-smear or click artifacts during manual delay-time changes as long as the app stays stable and bounded.
- Validate both audio safety and user-facing control behavior.

Act:

- In `GlobalDelay::prepare(...)`, call `setMaximumDelayInSamples(...)` exactly once per prepare cycle.
- In `GlobalDelay::process(...)`, use only `pushSample(...)`, `popSample(...)`, and smoothed targets.
- Do not allocate scratch buffers in the audio callback.
- Sweep the time control through its full range from the editor and from the mapped MiniLab control during sustained playback.
- Repeat the sweep at `44.1 kHz` and `48 kHz`.

Validate:

- No crash, exception, or callback allocation occurs during manual delay-time changes.
- The app may produce brief control-change artifacts, but it must remain responsive and bounded.
- Delay repeats remain audible after time changes rather than vanishing because of state corruption.

### Closeout Rule After All Six Checks Pass

Only after all six validations pass:

- update `TODO.md`
- move the completed items to `DONE.md`
- update `README.md` and `docs/minilab3-default-messages.md` with the verified behavior actually shipped

## 4. Edge Case and Boundary Audit

| Failure mode | Why it can happen in the current codebase | Required mitigation in `Phase 9` |
| --- | --- | --- |
| Delay added inside `SynthVoice` | The voice already owns other DSP state, so it is tempting to add delay there for convenience. | Reject that shortcut. Delay must be global and live in shared engine orchestration. |
| Delay added in standalone-only code | The standalone shell already owns hardware panels and could become a dumping ground for audio behavior. | Keep the delay module in `src/synth`, not in `src/standalone`. |
| `DelayLine::setMaximumDelayInSamples(...)` called from the callback | JUCE documents that it may allocate internally. | Restrict it to `prepare(...)` only. |
| Using `DelayLine::process(...)` while time changes | Block processing assumes a fixed delay during processing. | Use `pushSample(...)` and `popSample(...)` for this phase. |
| Delay processed after master gain | Current `SynthEngine` already owns the final gain stage, and a careless insertion could place delay after it. | Keep the order `voice mix -> delay -> master gain`. |
| Feedback runaway | High feedback plus repeated notes can escalate quickly. | Hard-clamp feedback to `0.85` in both snapshot and DSP code. |
| Mix-zero still leaks audible wet output | Crossfade logic can be implemented incorrectly. | Use `dry + ((wet - dry) * mix)` and validate `mix == 0` as effectively dry. |
| Delay line resized during time sweep | A naive implementation may rebuild or resize buffers when time changes. | Convert milliseconds to samples against a preallocated maximum and move only the read tap. |
| Sample-rate change leaves stale max delay | `maximumDelaySamples` depends on sample rate. | Recompute and reprepare the delay on every `prepare(...)`. |
| Mono/stereo channel mismatch | The processor supports mono and stereo outputs. | Prepare the delay for the actual channel count and iterate only the buffer's live channels. |
| Panic incorrectly clears the tail | It is easy to reset all state when adding a new effect. | Do not reset `GlobalDelay` from `panic()` in this phase. |
| Editor wiring works but audio path does not | The APVTS controls may update visually even if the render snapshot ignores them. | Validate the full path: editor or hardware -> APVTS -> processor snapshot -> delay DSP. |
| Hardware mapping guessed instead of verified | The current docs still mark the delay controls as deferred. | Verify `Knob 8`, `Fader 2`, and `Fader 3` first, then encode the facts in code and docs. |
| New `.cpp` file forgotten in CMake | The project uses explicit sources, not source globs. | Update `CMakeLists.txt` in the same edit slice as `GlobalDelay.cpp`. |
| Delay-time smoothing too abrupt | Unsmooth time changes can click badly. | Use a short linear smoother on delay samples. |
| Delay-time smoothing too long | Over-smoothing can make the control feel unresponsive. | Keep smoothing short, around `0.02` seconds, and validate by ear. |
| Dry signal disappears at high mix | A mistaken formula can turn the delay into a wet-only send effect. | Use an explicit dry/wet crossfade and test across the full mix range. |
| Host tail length remains stale at `0.0` | `SynthAudioProcessor.h` currently returns zero tail length. | Decide explicitly whether to fix this now or track it as a Phase 11 follow-up. Do not ignore the mismatch silently. |

## 5. Verification Protocol

### 5.1 Preflight Checklist

Before coding begins:

1. Confirm `Phase 8` remains green and the current standalone synth, filter, and MiniLab mapping behavior still work.
2. Confirm `delayTimeMs`, `delayFeedback`, and `delayMix` still exist unchanged in `src/parameters/ParameterIDs.h` and `src/parameters/ParameterLayout.cpp`.
3. Confirm there is still no existing shared delay implementation in `src/synth` so the blueprint does not collide with parallel work.
4. Confirm whether `Knob 8`, `Fader 2`, and `Fader 3` already have verified message facts. If not, record them with the MIDI monitor before claiming the fixed-mapping checkbox complete.

### 5.2 Mandatory Automated Checks Available in the Current Repo

The repo does not currently have a standing test harness, so the required automated validation for this phase is build-plus-regression inspection.

1. Run `cmake --build build --config Debug`.
2. Run `rg "GlobalDelay|DelayLine" src/synth CMakeLists.txt` and confirm the new module exists and is registered in the build.
3. Run `rg "delayTimeMs|delayFeedback|delayMix" src/plugin/SynthAudioProcessor.cpp src/synth/SynthParameters.h src/synth/SynthEngine.cpp src/plugin/SynthAudioProcessorEditor.cpp src/midi` and confirm all required plumbing surfaces changed.
4. Run `rg "setMaximumDelayInSamples" src` and confirm the only live call site is in `GlobalDelay::prepare(...)`, not in audio processing.
5. Run `rg "setValueNotifyingHost|beginChangeGesture|endChangeGesture" src` and confirm no new host-notifying call sites appeared in `src/synth` or `src/standalone`.
6. Check the build output for new warnings. New warnings fail the phase.

### 5.3 Optional Microtests if a Lightweight Harness Is Approved

These are worth adding only if the reviewer explicitly approves a tiny local test harness. Do not create a full test subsystem as part of this phase.

1. `MidiMappingEngine` translation test: verified `Knob 8`, `Fader 2`, and `Fader 3` messages map to `delayMix`, `delayFeedback`, and `delayTimeMs` respectively.
2. `GlobalDelay` helper test: `clampFeedback(1.0f)` returns `0.85f`, `clampMix(-0.5f)` returns `0.0f`, and `delayMsToSamples(250.0f)` stays finite for both `44.1 kHz` and `48 kHz`.
3. `GlobalDelay` bypass test: with `mix == 0.0f`, output remains dry within a small float tolerance.

### 5.4 Manual UX Checks

Run these checks in order.

1. Launch the standalone app and confirm a visible `Delay` section appears without overlapping the existing sections.
2. Set waveform to `saw`, hold one note, set `delayMix` above zero, and confirm delayed repeats are audible.
3. Sweep `delayMix` from `0` to `100%` and confirm the sound moves from effectively dry toward fully wet.
4. Hold a note or short repeating phrase, then sweep `delayFeedback` toward maximum and confirm repeats intensify but remain bounded.
5. With `delayMix` above zero, sweep `delayTimeMs` from minimum to maximum and confirm the app remains stable even if brief artifacts are audible.
6. Repeat the previous three checks with a 4-note chord.
7. Repeat the previous checks at `44.1 kHz`.
8. Repeat the previous checks at `48 kHz`.
9. Use `Knob 8`, `Fader 2`, and `Fader 3` on the MiniLab and confirm the editor controls and audible result move together.
10. Trigger panic during sustained playback and confirm active notes stop immediately while the delay tail behavior matches the phase decision.

### 5.5 Exit Gate

The phase is ready to move to `DONE.md` only when all of the following are true:

- The delay effect lives in the shared post-voice signal path and works from both the editor and the mapped hardware controls.
- `delayMix == 0.0` is effectively dry.
- Feedback is hard-bounded at `0.85` or lower.
- Manual delay-time changes do not crash, allocate in the callback, or corrupt the effect state.
- No new warnings were introduced.
- The MiniLab profile artifacts record the actual hardware facts used by the code.
- If `getTailLengthSeconds()` was not corrected in this phase, the follow-up is written down explicitly rather than left implicit.

## 6. Code Scaffolding

These snippets are structural templates, not copy-paste final code. Keep the implementation minimal and aligned with the current codebase.

### 6.1 `SynthParameters.h` Template

```cpp
namespace coolsynth::synth
{
    struct DelayParameters
    {
        float timeMs = 250.0f;
        float feedback = 0.25f;
        float mix = 0.0f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        FilterParameters filter;
        DelayParameters delay;
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
        std::atomic<float>* delayTimeMs = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayMix = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
    };
}
```

### 6.2 `GlobalDelay.h` Template

```cpp
class GlobalDelay final
{
public:
    void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer,
                 const DelayParameters& parameters) noexcept;

private:
    using DelayLine =
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

    static constexpr float minDelayTimeMs = 1.0f;
    static constexpr float maxDelayTimeMs = 1000.0f;
    static constexpr float maxFeedback = 0.85f;
    static constexpr double parameterSmoothingSeconds = 0.02;

    float clampDelayMs(float timeMs) const noexcept;
    float clampFeedback(float feedback) const noexcept;
    float clampMix(float mix) const noexcept;
    float delayMsToSamples(float timeMs) const noexcept;
    void updateTargets(const DelayParameters& parameters) noexcept;

    DelayLine delayLine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamplesSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    double currentSampleRate = 0.0;
    int preparedOutputChannels = 0;
    int maximumDelaySamples = 0;
    bool prepared = false;
};
```

### 6.3 `GlobalDelay::process(...)` Skeleton

```cpp
void GlobalDelay::process(juce::AudioBuffer<float>& buffer,
                          const DelayParameters& parameters) noexcept
{
    if (!prepared)
        return;

    updateTargets(parameters);

    const auto numChannels = juce::jmin(buffer.getNumChannels(), preparedOutputChannels);
    const auto numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto delaySamples = delaySamplesSmoother.getNextValue();
        const auto feedback = feedbackSmoother.getNextValue();
        const auto mix = mixSmoother.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample(channel, sample);
            const float wet = delayLine.popSample(channel, delaySamples);
            const float feedbackInput = dry + (wet * feedback);

            delayLine.pushSample(channel, feedbackInput);
            buffer.setSample(channel, sample, dry + ((wet - dry) * mix));
        }
    }
}
```

### 6.4 `SynthEngine::render(...)` Skeleton

```cpp
void SynthEngine::render(juce::AudioBuffer<float>& outputBuffer,
                         juce::MidiBuffer& midiMessages,
                         const BlockRenderParameters& parameters)
{
    if (!prepared)
        return;

    pushEnvelopeParametersToVoices(parameters.ampEnvelope);
    pushFilterParametersToVoices(parameters.filter);
    pushWaveformToVoices(parameters.waveform);

    synthesiser.renderNextBlock(outputBuffer, midiMessages, 0, outputBuffer.getNumSamples());
    globalDelay.process(outputBuffer, parameters.delay);
    applyMasterGain(outputBuffer, parameters.masterGainLinear);
}
```

### 6.5 Editor Wiring Template

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
    juce::RangedAudioParameter* delayTimeMs = nullptr;
    juce::RangedAudioParameter* delayFeedback = nullptr;
    juce::RangedAudioParameter* delayMix = nullptr;
    juce::RangedAudioParameter* masterGainDb = nullptr;
};

coolsynth::ui::SynthSection delaySection { "Delay" };
coolsynth::ui::HardwareKnob delayTimeKnob { "Time" };
coolsynth::ui::HardwareKnob delayFeedbackKnob { "Feedback" };
coolsynth::ui::HardwareKnob delayMixKnob { "Mix" };

std::unique_ptr<SliderAttachment> delayTimeAttachment;
std::unique_ptr<SliderAttachment> delayFeedbackAttachment;
std::unique_ptr<SliderAttachment> delayMixAttachment;
```

### 6.6 MiniLab Binding Template

Do not guess CC numbers in final code. Verify them first.

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
    delayMix,
    delayFeedback,
    delayTime,
    masterGain,
    panic,
};

constexpr std::array<Minilab3Binding, 12> fixedBindings {{
    { "knob1",  1, 74, 1,  Minilab3LogicalTarget::waveform,      true },
    { "knob2",  1, 71, 1,  Minilab3LogicalTarget::filterCutoff,  true },
    { "knob3",  1, 76, 1,  Minilab3LogicalTarget::filterResonance, true },
    { "knob4",  1, 77, 1,  Minilab3LogicalTarget::ampAttack,     true },
    { "knob5",  1, 93, 1,  Minilab3LogicalTarget::ampDecay,      true },
    { "knob6",  1, 18, 1,  Minilab3LogicalTarget::ampSustain,    true },
    { "knob7",  1, 19, 1,  Minilab3LogicalTarget::ampRelease,    true },
    { "knob8",  1, /* verified CC */, 1, Minilab3LogicalTarget::delayMix, true },
    { "fader1", 1, 82, 1,  Minilab3LogicalTarget::masterGain,    true },
    { "fader2", 1, /* verified CC */, 1, Minilab3LogicalTarget::delayFeedback, true },
    { "fader3", 1, /* verified CC */, 1, Minilab3LogicalTarget::delayTime, true },
    { "pad8",   2, 43, 10, Minilab3LogicalTarget::panic,         true },
}};
```

Implementation note:

- Keep the existing table name if the smallest diff matters more than nomenclature cleanup.
- Do not spend this phase renaming fixed-mapping APIs unless the reviewer explicitly asks for it.

## 7. Summary of the Smallest Correct Change

The smallest correct `Phase 9` implementation is:

1. Add a new shared `GlobalDelay` module in `src/synth` and register it in `CMakeLists.txt`.
2. Extend the render snapshot with `DelayParameters` and three raw APVTS parameter pointers.
3. Run the delay after voice mixing and before master gain in `SynthEngine`.
4. Add a `Delay` section to the shared editor with `Time`, `Feedback`, and `Mix` controls.
5. Extend the fixed MiniLab mapping for the three delay targets once the hardware facts are verified.
6. Validate that feedback is capped and manual delay-time changes stay stable at `44.1 kHz` and `48 kHz`.

Anything larger than that is probably scope drift.
