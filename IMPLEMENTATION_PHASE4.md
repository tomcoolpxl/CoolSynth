<!-- markdownlint-disable MD024 MD025 -->

# Phase 4 Blueprint: Playable Sine Synth Voice Path

## Phase Selection

Selected phase: `Phase 4 - Playable sine synth voice path`

Selection basis:

- `TODO.md` shows `Phase 1`, `Phase 2`, and `Phase 3` completed, with no later phase staged.
- `IMPLEMENTATION_PLAN.md` defines `Phase 4` as the next dependency-bearing milestone after standalone MIDI bring-up.
- The current shared processor is still intentionally silent: `SynthAudioProcessor::processBlock()` only clears the output buffer.
- There is no `src/synth/` directory yet, so the synth-engine slice has not started.
- The standalone editor still advertises the Phase 3 runtime shell, which means the next review-sized change should be the first audible path rather than broader UI or controller mapping work.

This blueprint therefore assumes the next implementation review cycle is the first shared audio-rendering slice, not waveform UI, controller mapping, filter work, or delay.

## Scope Guardrails

In scope for this phase:

- Add one shared synth engine wrapper around `juce::Synthesiser`.
- Preallocate exactly 8 voices.
- Render audible sine-wave notes from incoming MIDI.
- Apply per-voice ADSR amplitude shaping.
- Scale note loudness by velocity only.
- Apply smoothed master gain after voice mixing.
- Add a panic path that is safe to trigger from the UI and from standalone MIDI disconnect handling.
- Close the current standalone disconnect gap by converting selected-device disconnects into a processor panic request.

Explicitly out of scope for this phase:

- Square and saw rendering, even though the stable `waveform` parameter already exists.
- APVTS-backed synth controls beyond consuming the existing ADSR and master-gain parameters.
- Filter DSP, delay DSP, MiniLab fixed mapping, MIDI learn, presets, or standalone persistence.
- Refactoring the standalone audio shell or MIDI monitor beyond the minimum disconnect callback needed to prevent stuck notes.
- Introducing a large custom DSP framework above JUCE.
- Editing JUCE source files under `external/JUCE`.

## Source Anchors

This blueprint is grounded in the current codebase surface:

- `src/plugin/SynthAudioProcessor.cpp` currently constructs `AudioProcessorValueTreeState`, stays silent in `processBlock()`, and already owns processor state serialization.
- `src/parameters/ParameterIDs.h` and `src/parameters/ParameterLayout.cpp` already define the stable ADSR and master-gain parameters needed by this phase.
- `src/standalone/StandaloneMidiInput.cpp` already routes selected-device MIDI into JUCE's `AudioDeviceManager` path and into the standalone monitor, but it does not currently notify the processor when the selected device disconnects.
- `src/plugin/SynthAudioProcessorEditor.cpp` is already the shared composition point for standalone-only UI, so it is the right place for a minimal panic control.
- JUCE `juce::Synthesiser` already gives the correct sample-accurate MIDI splitting behavior inside `renderNextBlock()`, but its default `findVoiceToSteal()` policy protects top and bottom notes, which does not match the required `release-first then oldest-active` rule.
- JUCE `juce::dsp::Oscillator<float>` and `juce::ADSR` are already available through the existing JUCE dependency and are sufficient for the first playable slice.

## Exact `TODO.md` Entries This Blueprint Expands

- [ ] Add `SynthEngine` with 8 preallocated voices.
- [ ] Implement per-voice sine oscillator playback.
- [ ] Implement per-voice ADSR amplitude envelope and velocity-to-amplitude scaling.
- [ ] Implement release-first then oldest-active voice stealing.
- [ ] Add panic handling that clears active voices and held-note state.
- [ ] Verify standalone MIDI input produces audible notes and chord playback.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Use one shared `SynthEngine` as the audio-rendering boundary between the processor and JUCE voice objects.

Required design choices:

- Keep `SynthAudioProcessor` responsible for parameter ownership, state serialization, and cross-thread command intake.
- Keep `SynthEngine` responsible for voice allocation, voice rendering, and final master-gain application.
- Keep `SynthVoice` responsible for one oscillator, one ADSR envelope, one velocity scalar, and one currently playing note.
- Keep standalone disconnect handling outside the shared engine. It should only request panic through the processor.
- Do not introduce a separate MIDI-normalization layer in this phase. `juce::Synthesiser` already handles note on and note off from the shared MIDI buffer.

Resulting runtime object graph:

```text
JUCE standalone wrapper or VST3 wrapper
  -> SynthAudioProcessor
     -> juce::AudioProcessorValueTreeState
     -> ParameterValuePointers
     -> std::atomic<bool> panicRequested
     -> coolsynth::synth::SynthEngine
        -> ReleaseFirstSynthesiser
           -> 8 x SynthVoice
           -> 1 x SynthSound
     -> SynthAudioProcessorEditor
        -> Panic button
        -> existing standalone audio panel
        -> existing standalone MIDI panel
        -> existing MIDI monitor panel
```

### 1.2 State Definitions

#### 1.2.1 Processor-Facing Parameter Handles

Cache raw APVTS parameter pointers once in the processor constructor so `processBlock()` does not perform repeated string lookups.

```cpp
namespace coolsynth::synth
{
    struct ParameterValuePointers
    {
        std::atomic<float>* ampAttackMs = nullptr;
        std::atomic<float>* ampDecayMs = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampReleaseMs = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
    };
}
```

Rules:

- Do not cache pointers for filter, delay, or waveform in this phase unless they are immediately consumed.
- Do not cache parameter values themselves as mutable shadow state in the processor.
- The APVTS remains the single source of truth.

#### 1.2.2 Block-Level Render Snapshot

Convert atomics into plain floats once per block before rendering.

```cpp
namespace coolsynth::synth
{
    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        float masterGainLinear = 1.0f;
    };
}
```

Conversion rules:

- Convert milliseconds to seconds in the processor, not in each voice.
- Convert dB to linear gain in the processor with `juce::Decibels::decibelsToGain()`.
- Clamp sustain to `[0.0f, 1.0f]` even though the parameter already enforces it.
- Do not smooth attack, decay, sustain, or release in Phase 4. Only master gain needs explicit smoothing here.

#### 1.2.3 Voice State

Each voice should keep only the state required to render and stop cleanly.

```cpp
namespace coolsynth::synth
{
    class SynthVoice final : public juce::SynthesiserVoice
    {
    public:
        SynthVoice();

        void prepare(const juce::dsp::ProcessSpec& spec);
        void setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;

        bool canPlaySound(juce::SynthesiserSound* sound) override;
        void startNote(int midiNoteNumber,
                       float velocity,
                       juce::SynthesiserSound* sound,
                       int currentPitchWheelPosition) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int newPitchWheelValue) override;
        void controllerMoved(int controllerNumber, int newControllerValue) override;
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples) override;

    private:
        juce::dsp::Oscillator<float> oscillator;
        juce::ADSR ampEnvelope;
        EnvelopeParameters nextEnvelopeParameters;
        double currentSampleRate = 0.0;
        float currentFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
    };
}
```

Voice rules:

- Oscillator waveform is fixed to sine for the entire phase.
- `velocityGain` is the only velocity-derived modulation term in this phase.
- Do not add a filter member yet. The design requires it later, but it is not part of this slice.
- Do not add per-voice heap buffers unless a concrete rendering issue forces it. Mono sample generation plus channel fan-out is enough here.

#### 1.2.4 Engine State

`SynthEngine` owns the synth-wide rendering state.

```cpp
namespace coolsynth::synth
{
    inline constexpr int defaultVoiceCount = 8;
    inline constexpr double masterGainRampSeconds = 0.02;

    class SynthEngine final
    {
    public:
        SynthEngine();

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiMessages,
                    const BlockRenderParameters& parameters);
        void panic() noexcept;

    private:
        void prepareVoices(double sampleRate, int samplesPerBlock);
        void pushEnvelopeParametersToVoices(const EnvelopeParameters& parameters) noexcept;
        void applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept;

        ReleaseFirstSynthesiser synthesiser;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainLinear;
        int outputChannels = 0;
        bool prepared = false;
    };
}
```

Engine rules:

- Preallocate all 8 voices in the constructor and never resize voice count during playback.
- Add exactly one `SynthSound` instance that accepts all notes and channels.
- Prepare voices in `prepareToPlay`, not lazily on first note.
- Apply master gain after all voices are mixed.
- Keep `panic()` synchronous and allocation-free.

### 1.3 Function Signatures And Module Boundaries

#### 1.3.1 New Shared Synth Module

Create a new `src/synth/` directory with these modules:

- `SynthEngine.h/.cpp`
- `SynthVoice.h/.cpp`
- `SynthSound.h`

Recommended `SynthSound` shape:

```cpp
namespace coolsynth::synth
{
    class SynthSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };
}
```

Keep `SynthSound` header-only unless implementation complexity appears later.

#### 1.3.2 Custom Voice-Stealing Synthesiser

Do not rely on JUCE's default voice stealing. Override it explicitly.

```cpp
namespace coolsynth::synth
{
    class ReleaseFirstSynthesiser final : public juce::Synthesiser
    {
    protected:
        juce::SynthesiserVoice* findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                                int midiChannel,
                                                int midiNoteNumber) const override;
    };
}
```

Required stealing algorithm:

1. Gather all active voices that can play the target sound.
2. Sort them by `wasStartedBefore()` so the oldest voice is first.
3. First pass: return the oldest voice where `isPlayingButReleased()` is true.
4. Second pass: return the oldest remaining active voice.
5. Do not protect top or bottom notes.
6. Do not special-case same-pitch note reuse in this phase.

This is the exact rule required by `REQUIREMENTS.md`, and it intentionally differs from JUCE's default protection-based behavior.

#### 1.3.3 Processor Command And Render API

Extend `SynthAudioProcessor` with these private helpers and public command entry points:

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    void requestPanic() noexcept;

private:
    coolsynth::synth::BlockRenderParameters makeBlockRenderParameters() const noexcept;
    static coolsynth::synth::ParameterValuePointers bindParameterPointers(APVTS& state);

    APVTS parameters;
    coolsynth::synth::ParameterValuePointers parameterValues;
    coolsynth::synth::SynthEngine synthEngine;
    std::atomic<bool> panicRequested { false };
};
```

Processor rules:

- `requestPanic()` must only set `panicRequested = true`.
- `processBlock()` must be the place that consumes the flag and calls `synthEngine.panic()`.
- `processBlock()` must clear the MIDI buffer after consuming a pending panic so a same-block note-on does not immediately retrigger sound.
- `getStateInformation()` and `setStateInformation()` should remain structurally unchanged.

#### 1.3.4 Minimal Shared UI Additions

Add one shared panic button to the existing editor instead of creating a temporary Phase 4-only control panel.

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
private:
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton panicButton { "Panic" };
};
```

UI rules:

- The panic button is allowed in both standalone and plugin builds because it is a shared synth action, not a hardware-device action.
- The button must not call the engine directly. It only calls `processor.requestPanic()`.
- Do not add waveform or ADSR controls in this phase.

#### 1.3.5 Standalone Disconnect Callback Bridge

Close the existing runtime gap with a narrow standalone-only callback.

Recommended API shape:

```cpp
namespace coolsynth::standalone
{
    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        using DisconnectCallback = std::function<void()>;

        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer,
                                      DisconnectCallback onSelectedDeviceDisconnected = {});
    };
}

class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    explicit StandaloneMidiInputPanel(std::function<void()> onSelectedDeviceDisconnected = {});
};
```

Callback rules:

- Invoke the callback only on the transition from `connected` to `disconnected`.
- Do not invoke it when a remembered device is missing at startup.
- The callback must only request panic through the processor.
- The callback must not call synth code directly.

### 1.4 Render Flow

Required `processBlock()` flow:

```text
processBlock
  -> clear output buffer
  -> if panicRequested.exchange(false):
       -> synthEngine.panic()
       -> midiMessages.clear()
  -> build BlockRenderParameters from cached APVTS atomics
  -> synthEngine.render(buffer, midiMessages, blockParameters)
```

Required `SynthEngine::render()` flow:

```text
render
  -> push block envelope parameters to all voices as "next note" settings
  -> juce::Synthesiser::renderNextBlock()
  -> apply smoothed master gain to the mixed output buffer
```

Required `SynthVoice::renderNextBlock()` flow:

```text
voice render
  -> for each sample
       -> oscillator.processSample(0.0f)
       -> multiply by ampEnvelope.getNextSample()
       -> multiply by velocityGain
       -> add same sample to each output channel
  -> if ampEnvelope is no longer active:
       -> clearCurrentNote()
       -> oscillator.reset()
```

### 1.5 Envelope Latching Strategy

JUCE's stock `ADSR` is simplest and safest in this phase if the envelope parameters are latched for the next note instead of rewritten continuously during active playback.

Phase 4 rule:

- `setNextEnvelopeParameters()` stores the next envelope settings on the voice.
- `startNote()` converts the stored values into `juce::ADSR::Parameters`, calls `ampEnvelope.setParameters()`, and then calls `ampEnvelope.noteOn()`.
- `renderNextBlock()` does not repeatedly call `setParameters()` on an active envelope.

Why this is the right tradeoff now:

- There is no Phase 4 UI for live envelope editing yet.
- It avoids fighting the documented edge cases in JUCE's stock `ADSR` implementation.
- It keeps the playable-audio slice small and predictable.

Implication to document in the code review:

- ADSR parameter changes take effect on newly triggered notes in Phase 4.
- More aggressive live-update behavior, if still desired after Phase 5 UI work lands, can be revisited without destabilizing the first audible path.

### 1.6 Master Gain Strategy

Use the existing `masterGainDb` parameter as the final output stage.

Exact implementation rules:

- Convert dB to linear once per block in the processor.
- Reset the smoothed value in `SynthEngine::prepare()` with a short ramp, for example `20 ms`.
- Use `applyGainRamp()` per channel after voice rendering.
- Do not apply master gain inside each voice.

This keeps gain behavior global and avoids per-voice double-scaling later when filter and delay are added.

### 1.7 Explicit Design Rejections

Do not choose any of the following:

- Reading APVTS values by parameter ID string inside every voice render call.
- Adding waveform switching now just because the parameter already exists.
- Using JUCE's default `findVoiceToSteal()` and assuming it matches the requirement.
- Calling engine panic directly from the editor or from standalone MIDI code.
- Building a custom envelope class before the first audible path is proven necessary.
- Creating an additional processor-side note map if `juce::Synthesiser` voice state already provides the required held-note clearing for this phase.

## 2. File-Level Strategy

Exact file set for the Phase 4 implementation:

| Path | Change Type | Responsibility |
| --- | --- | --- |
| `CMakeLists.txt` | update | Add the new `src/synth/*.cpp` translation units to the existing `CoolSynth` target. No new libraries or targets. |
| `src/synth/SynthEngine.h` | new | Declare block parameter snapshots, the engine API, and constants such as `defaultVoiceCount`. |
| `src/synth/SynthEngine.cpp` | new | Construct the fixed voice pool, own the custom synthesiser, prepare voices, render blocks, apply master gain, and implement panic. |
| `src/synth/SynthVoice.h` | new | Declare the per-voice oscillator, envelope, velocity-scaling state, and JUCE voice overrides. |
| `src/synth/SynthVoice.cpp` | new | Implement sine rendering, note start and stop behavior, envelope latching, and voice cleanup. |
| `src/synth/SynthSound.h` | new | Provide the trivial all-notes/all-channels sound object used by the shared synth engine. |
| `src/plugin/SynthAudioProcessor.h` | update | Add `requestPanic()`, cached parameter pointers, `SynthEngine`, and the atomic panic flag. |
| `src/plugin/SynthAudioProcessor.cpp` | update | Bind parameter pointers, prepare and release the synth engine, consume panic requests in `processBlock()`, build block render parameters, and replace the current silent path with synth rendering. |
| `src/plugin/SynthAudioProcessorEditor.h` | update | Add the shared panic button member. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | update | Wire the panic button to `requestPanic()`, update the phase status text, and make the layout accommodate the new control without disturbing the standalone panels more than necessary. |
| `src/standalone/StandaloneMidiInput.h` | update | Add a narrow disconnect callback hook to the controller and panel constructor. |
| `src/standalone/StandaloneMidiInput.cpp` | update | Fire the callback only on a real connected-to-disconnected transition. |
| `src/ui/StandaloneMidiInputPanel.h` | update | Accept and store the processor panic callback only if needed by the chosen constructor shape. |
| `src/ui/StandaloneMidiInputPanel.cpp` | update | Pass the disconnect callback into the controller from the shared editor path. |
| `README.md` | update after verification | Document how to hear the first synth sound, how to trigger panic, and the current Phase 4 limitations. |
| `TODO.md` | update before implementation | Refresh to the `Phase 4` checklist from `IMPLEMENTATION_PLAN.md`. |
| `DONE.md` | update after verification | Record only verified Phase 4 outcomes and checks. |

Files intentionally not touched in Phase 4:

- `src/parameters/ParameterIDs.h`
- `src/parameters/ParameterLayout.h`
- `src/parameters/ParameterLayout.cpp`
- `src/midi/MidiMonitor.h`
- `src/midi/MidiMonitor.cpp`
- `src/standalone/StandaloneAudioSupport.*`
- `external/JUCE/**`

Rationale for leaving parameter files alone:

- The stable ADSR and master-gain parameters already exist and already match the requirements.
- Phase 4 consumes them but does not change their IDs, ranges, defaults, or formatting.

## 3. Atomic Execution Steps

Each Phase 4 checkbox should be executed as its own `Plan -> Act -> Validate` cycle.

### 3.1 Checkbox: Add `SynthEngine` with 8 preallocated voices

Plan:

- Add a minimal shared synth module rather than pushing rendering logic into `SynthAudioProcessor`.
- Preallocate voices in the engine constructor so no voice objects are allocated during playback.
- Keep the sound model trivial: one `SynthSound`, eight `SynthVoice` instances.

Act:

1. Update `TODO.md` to the Phase 4 checklist before touching code.
2. Create `src/synth/SynthEngine.h/.cpp`, `src/synth/SynthVoice.h/.cpp`, and `src/synth/SynthSound.h`.
3. Add the new synth source files to `CMakeLists.txt`.
4. In `SynthEngine` constructor:
   - create one `SynthSound`,
   - create eight `SynthVoice` objects,
   - call `setNoteStealingEnabled(true)` on the custom synthesiser.
5. In `SynthAudioProcessor`, add a `SynthEngine` member and call `prepare()` and `releaseResources()` at the existing lifecycle boundaries.

Validate:

- Build Debug successfully.
- Confirm there are exactly eight voices in code and no runtime voice-allocation path remains.
- Review the diff and verify that no synth rendering code leaked into the standalone-only modules.

### 3.2 Checkbox: Implement per-voice sine oscillator playback

Plan:

- Keep waveform generation fixed to sine for this phase.
- Use JUCE's oscillator support rather than handwritten phase accumulation.
- Render mono per voice and fan the sample out to the processor's output channels.

Act:

1. In `SynthVoice` constructor, initialise `juce::dsp::Oscillator<float>` with a sine lambda.
2. In `prepare()`, set up the oscillator with a mono `juce::dsp::ProcessSpec` and reset its phase.
3. In `startNote()`:
   - convert MIDI note to frequency with `juce::MidiMessage::getMidiNoteInHertz()`,
   - force-set oscillator frequency,
   - reset oscillator state if needed.
4. In `renderNextBlock()`, generate samples from the oscillator and add them to all output channels.
5. Keep `pitchWheelMoved()` and `controllerMoved()` as no-op overrides in this phase.

Validate:

- Build Debug successfully.
- Launch the standalone app, select an output device and MIDI input, and confirm a single held note produces a steady sine tone.
- Confirm the sound is present in mono and stereo output layouts.
- Review the voice render loop for no heap allocation, no logging, and no string creation.

### 3.3 Checkbox: Implement per-voice ADSR amplitude envelope and velocity-to-amplitude scaling

Plan:

- Use the existing ADSR parameters from APVTS.
- Latch envelope parameters per note start for Phase 4.
- Multiply oscillator output by both envelope sample value and velocity scalar.

Act:

1. Add cached APVTS raw-parameter pointers to the processor for attack, decay, sustain, release, and master gain.
2. Add `makeBlockRenderParameters()` that:
   - loads the current parameter atomics,
   - converts milliseconds to seconds,
   - converts master gain dB to linear.
3. Add `SynthVoice::setNextEnvelopeParameters()` to cache the next-note ADSR values.
4. In `SynthEngine::render()`, push the current envelope snapshot to all voices before calling `renderNextBlock()`.
5. In `SynthVoice::startNote()`, translate the cached envelope values into `juce::ADSR::Parameters`, call `setParameters()`, and then `noteOn()`.
6. In `SynthVoice::stopNote()`, use `noteOff()` for tail-off and hard reset for immediate stop.
7. In `renderNextBlock()`, multiply each generated sample by `ampEnvelope.getNextSample() * velocityGain`.
8. In `SynthEngine::applyMasterGain()`, ramp from the current gain to the target gain across the block.

Validate:

- Build Debug successfully.
- Play very low and very high velocity notes and confirm the louder note is audibly stronger.
- Play short staccato notes and long held notes and confirm attack and release are audible rather than hard clicks.
- Confirm the default master-gain setting is audible but not excessively hot.
- Review the code and confirm ADSR parameter conversion happens once per block, not per sample.

### 3.4 Checkbox: Implement release-first then oldest-active voice stealing

Plan:

- Override JUCE's stealing hook instead of trying to steer the default behavior indirectly.
- Use one deterministic age ordering only.
- Favor simplicity over clever note protection heuristics.

Act:

1. Add `ReleaseFirstSynthesiser` as a small internal subclass in the synth module.
2. Override `findVoiceToSteal()`.
3. Build a local array of active, sound-compatible voices.
4. Sort them by `wasStartedBefore()` so the oldest voice comes first.
5. Return the oldest released voice if any voice reports `isPlayingButReleased()`.
6. If no released voice exists, return the oldest active voice.
7. Do not copy JUCE's top-note and low-note protection logic.

Validate:

- Build Debug successfully.
- Hold eight notes, release one so it enters release, then play a ninth note and confirm the released voice is stolen first.
- Hold eight notes without releasing any, then play a ninth note and confirm the earliest-started active note is replaced.
- Confirm there is no path that silently disables note stealing and drops notes instead.

### 3.5 Checkbox: Add panic handling that clears active voices and held-note state

Plan:

- Follow the existing design guidance: UI and standalone shell request panic, audio thread executes it.
- Use one processor-owned atomic flag.
- Clear same-block MIDI after consuming panic so a queued note-on cannot immediately retrigger.

Act:

1. Add `SynthAudioProcessor::requestPanic() noexcept` that only sets `panicRequested = true`.
2. In `processBlock()`, consume `panicRequested` with `exchange(false)`.
3. If panic was requested:
   - call `synthEngine.panic()`,
   - clear the incoming `MidiBuffer`.
4. Implement `SynthEngine::panic()` with `allNotesOff(0, false)` or an equivalent immediate-stop path.
5. Add a shared `panicButton` to the editor that calls `processor.requestPanic()`.
6. Add a standalone-only disconnect callback so `StandaloneMidiInputController` requests panic when the selected device drops after having been connected.
7. Ensure the disconnect callback fires only on a real connection loss transition.

Validate:

- Build Debug successfully.
- Hold multiple notes, click `Panic`, and confirm sound stops immediately.
- Hold notes and disconnect the selected MIDI device; confirm sound stops and the app stays running.
- Confirm no panic path directly manipulates synth voices from the message thread.
- Confirm repeated presses of `Panic` while idle are harmless.

### 3.6 Checkbox: Verify standalone MIDI input produces audible notes and chord playback

Plan:

- Treat this as a focused bring-up and regression pass, not as open-ended sound design.
- Verify the exact standalone integration seams already in the repo: audio shell, MIDI shell, monitor, shared processor, and new synth engine.

Act:

1. Build Debug.
2. Launch the standalone artifact.
3. Select a working audio output and confirm the status panel reports readiness.
4. Select the MiniLab 3 or another MIDI input device.
5. Confirm the MIDI monitor still shows note-on and note-off traffic.
6. Play single notes, then three-note and four-note chords.
7. Exceed eight simultaneous notes to exercise stealing.
8. Trigger panic from the editor and then from a runtime disconnect scenario.
9. Update `README.md` with the exact Phase 4 bring-up steps and current limitations.
10. Move completed items into `DONE.md` only after all validation passes.

Validate:

- The standalone app produces audible notes from the selected MIDI input.
- Chords are audible, not just monophonic playback.
- The MIDI monitor still works while sound is playing.
- Panic and disconnect handling do not crash the app.
- The VST3 target still builds even if it is not deeply exercised in this phase.

## 4. Edge Case & Boundary Audit

Specific failure modes and logic traps for this phase:

1. `waveform` default mismatch: the stable parameter default is `saw`, but Phase 4 sound is intentionally fixed to sine. Do not "fix" this by changing the parameter default. Instead, document that waveform selection is dormant until Phase 5.
2. Same-block retrigger after panic: if `panicRequested` is consumed but the incoming `MidiBuffer` is not cleared, notes queued in the same block can retrigger immediately.
3. Disconnect status without audio stop: the current standalone MIDI code updates UI state on disconnect but does not yet stop sounding notes. The Phase 4 callback bridge must close that gap.
4. Voice cleanup omission: if `clearCurrentNote()` is not called when the envelope reaches idle, voices will remain logically active and stealing behavior will drift.
5. Hard-stop clicks: if `stopNote()` always resets immediately, note releases will click. Only hard-stop on panic or `allowTailOff == false`.
6. ADSR misuse: calling `juce::ADSR::setParameters()` repeatedly during an active note can create unstable behavior. Latch parameters per note in this phase.
7. Frequency reset bug: if the oscillator is not reset or force-set on `startNote()`, stolen voices can begin at stale phase or stale pitch.
8. Master-gain zipper noise: applying master gain as an unsmoothed per-block step can click during parameter changes.
9. Zero-sample buffer edge case: keep all render helpers tolerant of `buffer.getNumSamples() == 0`.
10. Mono-versus-stereo assumptions: the processor supports mono and stereo output layouts. Do not hardcode stereo-only voice writes.
11. Voice stealing drift: reusing JUCE's default `findVoiceToSteal()` will silently give the wrong policy because it protects top and bottom notes.
12. Idle panic race: `requestPanic()` can be called when audio is idle. This must stay safe even if no active voices exist.
13. Callback spam on missing device: the standalone disconnect callback must not fire every refresh while a remembered device is unavailable at startup.
14. Unbounded scope creep: do not pull square, saw, filter, or delay into this phase just because the first audible path exists.

## 5. Verification Protocol

### 5.1 Mandatory Automated Checks

Because the repository does not yet define a dedicated unit-test suite, the mandatory automated gate for Phase 4 is a focused build-and-errors pass on the touched slice.

Run in this order:

1. Configure if needed: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
2. Build Debug: `cmake --build build --config Debug`
3. Confirm both shared outputs still compile:
   - standalone artifact target
   - VST3 artifact target
4. Check the touched files for compiler or IntelliSense errors.
5. Review that no new warnings were introduced by the synth files, processor changes, or standalone disconnect callback path.

Recommended deterministic smoke cases if they fit the review budget without creating framework churn:

1. `VoiceStealPrefersReleasedVoice`
2. `VoiceStealFallsBackToOldestActive`
3. `PanicClearsQueuedMidiForCurrentBlock`
4. `MidiNote69MapsTo440Hz`

These smoke cases are worth adding only if they can live in a tiny local harness. Do not let a new test framework block the Phase 4 audio slice.

### 5.2 Manual UX Checklist

All of the following manual checks must pass before moving the phase to `DONE.md`:

1. Launch the standalone app with a valid output device and confirm it remains stable.
2. Select the intended MIDI input device and confirm the status panel reports it as connected.
3. Play a single note and confirm immediate audible sine output.
4. Play at least a three-note chord and confirm the notes sound simultaneously.
5. Play with low and high velocities and confirm audibly different loudness.
6. Release a note normally and confirm the tail decays rather than cutting abruptly.
7. Hold more than eight notes and confirm the documented stealing policy is audible and repeatable.
8. Click `Panic` while notes are sounding and confirm the voices stop immediately.
9. Disconnect the selected MIDI device while a note is sounding and confirm:
   - the app does not crash,
   - sound stops,
   - the MIDI status changes to disconnected,
   - reconnecting the device does not require restarting the app.
10. Confirm the standalone MIDI monitor still displays note traffic while the synth is audible.
11. Reopen the app and confirm the remembered MIDI device behavior from Phase 3 still works.

### 5.3 Exit Criteria

Do not move the phase to `DONE.md` until every item below is true:

- Shared synth rendering is audible in standalone mode.
- Voice count is fixed at eight and no runtime voice allocation remains.
- ADSR and master gain are driven from the existing APVTS parameters.
- Voice stealing is release-first then oldest-active, not JUCE default behavior.
- Panic works from the shared UI and from standalone device disconnects.
- Standalone MIDI monitor and selection behavior still work after the synth path lands.
- The standalone and VST3 targets both build successfully.
- `README.md`, `TODO.md`, and `DONE.md` are updated to match the verified state.

## 6. Code Scaffolding

Use the following structural templates to keep the implementation idiomatic and consistent with the existing codebase.

### 6.1 `SynthEngine.h`

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace coolsynth::synth
{
    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        float masterGainLinear = 1.0f;
    };

    class SynthEngine final
    {
    public:
        SynthEngine();

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiMessages,
                    const BlockRenderParameters& parameters);
        void panic() noexcept;

    private:
        void prepareVoices(double sampleRate, int samplesPerBlock);
        void pushEnvelopeParametersToVoices(const EnvelopeParameters& parameters) noexcept;
        void applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept;

        ReleaseFirstSynthesiser synthesiser;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainLinear;
        int outputChannels = 0;
        bool prepared = false;
    };
}
```

### 6.2 `SynthVoice.h`

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "synth/SynthEngine.h"

namespace coolsynth::synth
{
    class SynthVoice final : public juce::SynthesiserVoice
    {
    public:
        SynthVoice();

        void prepare(const juce::dsp::ProcessSpec& spec);
        void setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;

        bool canPlaySound(juce::SynthesiserSound* sound) override;
        void startNote(int midiNoteNumber,
                       float velocity,
                       juce::SynthesiserSound* sound,
                       int currentPitchWheelPosition) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int newPitchWheelValue) override;
        void controllerMoved(int controllerNumber, int newControllerValue) override;
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples) override;

    private:
        juce::ADSR::Parameters makeJuceEnvelopeParameters() const noexcept;

        juce::dsp::Oscillator<float> oscillator;
        juce::ADSR ampEnvelope;
        EnvelopeParameters nextEnvelopeParameters;
        double currentSampleRate = 0.0;
        float currentFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
    };
}
```

### 6.3 `SynthAudioProcessor` additions

```cpp
void SynthAudioProcessor::requestPanic() noexcept
{
    panicRequested.store(true, std::memory_order_release);
}

coolsynth::synth::BlockRenderParameters SynthAudioProcessor::makeBlockRenderParameters() const noexcept
{
    coolsynth::synth::BlockRenderParameters params;
    params.ampEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.ampAttackMs->load() * 0.001f);
    params.ampEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.ampDecayMs->load() * 0.001f);
    params.ampEnvelope.sustainLevel = juce::jlimit(0.0f, 1.0f, parameterValues.ampSustain->load());
    params.ampEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.ampReleaseMs->load() * 0.001f);
    params.masterGainLinear = juce::Decibels::decibelsToGain(parameterValues.masterGainDb->load());
    return params;
}
```

### 6.4 `processBlock()` skeleton

```cpp
void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (panicRequested.exchange(false, std::memory_order_acq_rel))
    {
        synthEngine.panic();
        midiMessages.clear();
    }

    synthEngine.render(buffer, midiMessages, makeBlockRenderParameters());
}
```

### 6.5 Disconnect callback bridge

```cpp
StandaloneMidiInputPanel::StandaloneMidiInputPanel(std::function<void()> onSelectedDeviceDisconnected)
{
    if (auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager())
    {
        auto* settings = coolsynth::standalone::getStandaloneSettings();
        controller = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
            *deviceManager,
            settings,
            monitorBuffer,
            std::move(onSelectedDeviceDisconnected));
    }
}
```

Transition guard inside `StandaloneMidiInputController::refreshDevices()`:

```cpp
const bool wasConnected = selectedDeviceWasPresent;
const bool isConnectedNow = snapshot.selectedDevicePresent;

if (wasConnected && !isConnectedNow)
{
    selectedDeviceWasPresent = false;
    if (onSelectedDeviceDisconnected)
        onSelectedDeviceDisconnected();
}
else if (isConnectedNow)
{
    selectedDeviceWasPresent = true;
}
```

### 6.6 Editor panic button wiring

```cpp
panicButton.onClick = [this]
{
    processor.requestPanic();
};

if (juce::JUCEApplicationBase::isStandaloneApp())
{
    auto midiInputPanel = std::make_unique<StandaloneMidiInputPanel>([this]
    {
        processor.requestPanic();
    });
}
```

## Final Review Reminder

Phase 4 is complete only when the synth is audibly playable, the stealing policy is correct, and the disconnect-to-panic boundary is closed without polluting the shared engine with standalone device logic. Do not spend this review cycle on square or saw waveforms, filter placeholders, or broader UI restyling.
