<!-- markdownlint-disable MD024 MD025 -->

# Phase 5 Blueprint: Parameter-Driven Core Synth UI

## Phase Selection

Selected phase: `Phase 5 - Parameter-driven core synth UI`

Selection basis:

- `TODO.md` is already staged to `Phase 5`, with no later phase promoted.
- `README.md` records `Phase 4` as complete.
- `SynthAudioProcessorEditor` is still a Phase 4 placeholder layout with a title, a phase-specific status label, a panic button, and standalone runtime panels only.
- `SynthVoice` still renders a fixed sine oscillator even though the stable `waveform` parameter already exists in the APVTS.
- `ParameterLayout.cpp` already defines meaningful text formatters for envelope times, sustain, and master gain, so the main Phase 5 gap is exposing those values in the editor rather than redefining the parameter model.
- `CMakeLists.txt` uses an explicit `target_sources(...)` list, so any new UI modules must be registered there or the build will fail.

This blueprint therefore assumes the next implementation review cycle is the first shared synth-control surface plus the minimum synth-engine extension required to make those controls audible. It is not the fixed MiniLab mapping phase, the filter phase, the delay phase, or the VST3 validation phase.

## Scope Guardrails

In scope for this phase:

- Add waveform support for `sine`, `square`, and `saw` using the existing stable `waveform` parameter.
- Add APVTS-backed editor controls for waveform, ADSR, and master gain.
- Organize the shared editor into the first hardware-style sections: `Oscillator`, `Envelope`, and `Output`.
- Display meaningful value text for envelope times, sustain, and master gain in the editor.
- Preserve the existing panic action as a shared synth control.
- Preserve the existing standalone-only audio, MIDI input, and MIDI monitor panels below the new shared synth controls.
- Keep all UI-to-audio communication parameter-driven.

Explicitly out of scope for this phase:

- Filter DSP or filter UI.
- Delay DSP or delay UI.
- Fixed MiniLab 3 parameter mapping.
- MIDI learn.
- Preset management.
- Standalone persistence changes.
- Plugin-host smoke validation beyond compiling the shared editor for VST3.
- Heavy custom graphics or photorealistic skinning.
- Editing JUCE sources under `external/JUCE`.

## Source Anchors

This blueprint is grounded in the current codebase surface:

- `src/plugin/SynthAudioProcessor.cpp` already snapshots ADSR and master gain into `BlockRenderParameters` once per block.
- `src/plugin/SynthAudioProcessor.h` already exposes `getValueTreeState()`, which is the correct editor attachment seam.
- `src/synth/SynthParameters.h` already centralizes block render data and cached raw parameter pointers.
- `src/synth/SynthEngine.cpp` already pushes per-block envelope data to voices and applies smoothed master gain after voice rendering.
- `src/synth/SynthVoice.cpp` currently initializes one `juce::dsp::Oscillator<float>` with a sine lambda and therefore contains the exact narrow seam that must become waveform-aware.
- `src/parameters/ParameterIDs.h` already defines `WaveformChoice` with `sine`, `square`, and `saw` in stable order.
- `src/parameters/ParameterLayout.cpp` already implements `formatMs`, `formatPercent`, and `formatDb`, so Phase 5 should reuse that formatting behavior rather than inventing parallel UI-only formatters.
- `src/plugin/SynthAudioProcessorEditor.cpp` already gates standalone-only UI via `juce::JUCEApplicationBase::isStandaloneApp()`, which must remain the mode split.
- `CMakeLists.txt` currently contains no wildcard source discovery, so every new UI module is a deliberate build-system change.

## Exact `TODO.md` Entries This Blueprint Expands

- [ ] Add waveform support for sine, square, and saw.
- [ ] Add APVTS-backed UI controls for waveform, ADSR, and master gain.
- [ ] Add meaningful value formatting for envelope times, sustain, and master gain.
- [ ] Organize the first editor layout into oscillator, envelope, and output sections.
- [ ] Verify UI changes are audible and remain responsive during playback.
- [ ] Verify hardware-style control attachments stay in sync with the shared parameter model.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Keep the shared editor fully parameter-driven and keep the audio thread fully block-snapshot-driven.

Required consequences:

- The editor writes to APVTS parameters through `SliderAttachment` and `ComboBoxAttachment` only.
- `SynthAudioProcessor::processBlock()` remains the only place that converts raw parameter atomics into plain render state.
- `SynthEngine` remains the only owner of live voice objects.
- `SynthVoice` becomes waveform-aware, but it still never receives direct calls from the UI.
- Value labels in the editor must display actual parameter text, not duplicated hard-coded unit logic.

This is the narrowest change that satisfies both the requirements and the current codebase:

- It reuses the APVTS and attachment seam already present.
- It avoids speculative UI framework layers.
- It preserves real-time safety by keeping all audio-thread reads atomic and allocation-free.
- It sets up later MiniLab mapping and future host automation reflection without prematurely implementing either one.

### 1.2 Runtime Object Graph After Phase 5

```text
Standalone wrapper or VST3 wrapper
  -> SynthAudioProcessor
     -> AudioProcessorValueTreeState
     -> ParameterValuePointers
     -> SynthEngine
        -> ReleaseFirstSynthesiser
           -> 8 x SynthVoice
           -> 1 x SynthSound
  -> SynthAudioProcessorEditor
     -> Oscillator section
        -> waveform label
        -> waveform combo box
     -> Envelope section
        -> attack knob
        -> decay knob
        -> sustain knob
        -> release knob
     -> Output section
        -> master-gain fader
        -> panic button
     -> standalone-only audio status panel
     -> standalone-only MIDI input panel
     -> standalone-only MIDI monitor panel
```

Ownership rules:

- `SynthAudioProcessorEditor` owns controls and attachments.
- Attachments own parameter binding, not the parameter objects.
- `SynthAudioProcessor` owns all synth state and parameter state.
- `SynthVoice` owns only per-voice oscillator and envelope state.
- Standalone-only panels remain editor-owned and mode-gated.

### 1.3 Required Data Structures and State Definitions

#### 1.3.1 Extend the Existing Block Render Snapshot With Waveform

Phase 4 already established the right pattern: snapshot once per block, then render. Phase 5 should extend that pattern rather than creating a second parameter-read path.

Recommended `src/synth/SynthParameters.h` shape:

```cpp
namespace coolsynth::synth
{
    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
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
        std::atomic<float>* masterGainDb = nullptr;
    };
}
```

Rules:

- Do not rename parameter IDs.
- The waveform raw value must be clamped to the valid `WaveformChoice` range before casting.
- The parameter default is already `saw`, so activating the waveform parameter will intentionally change the default audible startup sound from the Phase 4 sine tone to `saw`. That is required behavior, not a regression.

Recommended decode helper in `SynthAudioProcessor.cpp` or an unnamed namespace nearby:

```cpp
static coolsynth::parameters::WaveformChoice decodeWaveformChoice(float rawValue) noexcept;
```

Implementation rule:

- Clamp `rawValue` to `[0.0f, 2.0f]`, round or cast to `int`, then map to `WaveformChoice`.

#### 1.3.2 Make `SynthVoice` Waveform-Aware Without Reinitializing the Oscillator

The current `juce::dsp::Oscillator<float>` member should stay, but its lambda must become waveform-aware.

Recommended `src/synth/SynthVoice.h` additions:

```cpp
class SynthVoice final : public juce::SynthesiserVoice
{
public:
    void setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept;

private:
    static float renderWaveSample(float phase,
                                  coolsynth::parameters::WaveformChoice waveform) noexcept;

    coolsynth::parameters::WaveformChoice currentWaveform =
        coolsynth::parameters::WaveformChoice::saw;
};
```

Recommended constructor strategy in `src/synth/SynthVoice.cpp`:

```cpp
SynthVoice::SynthVoice()
{
    oscillator.initialise([this](float phase)
    {
        return renderWaveSample(phase, currentWaveform);
    });
}
```

Required waveform implementation policy:

- `sine`: `std::sin(phase)`
- `square`: return `1.0f` for positive half cycle and `-1.0f` for negative half cycle
- `saw`: map phase linearly to a bipolar ramp

Reasons this is the preferred implementation for this phase:

- The oscillator remains prepared once in `prepare()`.
- Waveform changes do not require calling `initialise()` again from the audio path.
- Phase continuity is preserved by the existing oscillator state.
- The code change stays inside the already-existing voice abstraction.

Do not do any of the following in this phase:

- Do not allocate lookup tables during playback.
- Do not create one oscillator object per waveform unless validation proves the single-lambda design is insufficient.
- Do not let the editor call `setWaveform()` directly.

#### 1.3.3 Extend the Existing Engine Push Model

`SynthEngine` already pushes envelope parameters to voices per block. Phase 5 should extend that same push path with waveform.

Recommended `src/synth/SynthEngine.h` additions:

```cpp
private:
    void pushWaveformToVoices(coolsynth::parameters::WaveformChoice waveform) noexcept;
```

Required `render(...)` order in `src/synth/SynthEngine.cpp`:

```text
render
  -> pushEnvelopeParametersToVoices(parameters.ampEnvelope)
  -> pushWaveformToVoices(parameters.waveform)
  -> synthesiser.renderNextBlock(...)
  -> applyMasterGain(...)
```

Rules:

- The waveform setter must run on the audio thread before voice rendering for that block.
- The master-gain smoothing path stays unchanged.
- No new voice allocation is allowed.

#### 1.3.4 Add Reusable Hardware-Style Editor Primitives

The implementation plan already anticipates three UI primitives. Phase 5 should add them as small JUCE components rather than pushing all paint and layout logic into the editor.

Recommended `src/ui/HardwareKnob.h` shape:

```cpp
namespace coolsynth::ui
{
    class HardwareKnob final : public juce::Component
    {
    public:
        explicit HardwareKnob(juce::String labelText);

        juce::Slider& slider() noexcept { return knob; }
        void setValueText(const juce::String& text);

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider knob;
        juce::Label valueLabel;
    };
}
```

Recommended `src/ui/HardwareFader.h` shape:

```cpp
namespace coolsynth::ui
{
    class HardwareFader final : public juce::Component
    {
    public:
        explicit HardwareFader(juce::String labelText);

        juce::Slider& slider() noexcept { return fader; }
        void setValueText(const juce::String& text);

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider fader;
        juce::Label valueLabel;
    };
}
```

Recommended `src/ui/SynthSection.h` shape:

```cpp
namespace coolsynth::ui
{
    class SynthSection final : public juce::Component
    {
    public:
        explicit SynthSection(juce::String titleText);

        juce::Rectangle<int> getContentBounds() const noexcept;

        void paint(juce::Graphics& g) override;

    private:
        juce::String title;
    };
}
```

UI primitive rules:

- Keep styling light and JUCE-native.
- Each control must have a text label.
- Hide the slider text boxes; value text is displayed by the component-owned value label instead.
- The section component should be presentational only. It should not own child controls.

#### 1.3.5 Add Editor-Side Parameter References and Attachments

The editor needs one narrow layer of parameter metadata so value labels can show actual parameter text without repeated string lookups or duplicated unit rules.

Recommended `src/plugin/SynthAudioProcessorEditor.h` shape:

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
private:
    using SliderAttachment = SynthAudioProcessor::APVTS::SliderAttachment;
    using ComboBoxAttachment = SynthAudioProcessor::APVTS::ComboBoxAttachment;

    struct ParameterRefs
    {
        juce::RangedAudioParameter* waveform = nullptr;
        juce::RangedAudioParameter* ampAttackMs = nullptr;
        juce::RangedAudioParameter* ampDecayMs = nullptr;
        juce::RangedAudioParameter* ampSustain = nullptr;
        juce::RangedAudioParameter* ampReleaseMs = nullptr;
        juce::RangedAudioParameter* masterGainDb = nullptr;
    };

    void resized() override;
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

    void refreshValueDisplays();
    juce::String getCurrentParameterText(juce::RangedAudioParameter* parameter) const;

    ParameterRefs parameterRefs;

    coolsynth::ui::SynthSection oscillatorSection { "Oscillator" };
    coolsynth::ui::SynthSection envelopeSection { "Envelope" };
    coolsynth::ui::SynthSection outputSection { "Output" };

    juce::Label waveformLabel;
    juce::ComboBox waveformSelector;
    coolsynth::ui::HardwareKnob attackKnob { "Attack" };
    coolsynth::ui::HardwareKnob decayKnob { "Decay" };
    coolsynth::ui::HardwareKnob sustainKnob { "Sustain" };
    coolsynth::ui::HardwareKnob releaseKnob { "Release" };
    coolsynth::ui::HardwareFader masterGainFader { "Master" };
    juce::TextButton panicButton { "Panic" };

    std::unique_ptr<ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> masterGainAttachment;
};
```

Attachment rules:

- Declare attachments after the controls so attachments are destroyed first.
- Create attachments only after combo-box items and slider styles are fully configured.
- Every interactive control gets exactly one attachment.
- The editor must not call `setValueNotifyingHost()` directly for any continuous parameter.

#### 1.3.6 Replace the Phase-Specific Placeholder Copy

The current `statusLabel` text is hard-coded to `Phase 4 playable sine synth voice path`. That must not survive this phase.

Required change:

- Remove the phase-specific status label entirely, or replace it with neutral product copy that does not describe an implementation milestone.

Recommended simplest option:

- Remove `statusLabel` and reclaim the vertical space for the sectioned control layout.

### 1.4 Required Render and UI Update Flow

Required render flow after Phase 5:

```text
editor interaction
  -> APVTS attachment writes parameter value
  -> processBlock snapshots waveform + ADSR + master gain
  -> SynthEngine pushes waveform + ADSR to voices
  -> voices render with current waveform
  -> SynthEngine applies smoothed master gain
```

Required value-display flow:

```text
editor timer callback
  -> read current parameter text from APVTS parameters
  -> update knob/fader value labels
  -> waveform combo box already reflects the current choice through the attachment
```

Timer rules:

- Use a modest refresh rate such as `15 Hz` to `30 Hz`.
- The timer is for display text only, not for parameter transport.
- Do not add parameter listeners unless the timer approach proves insufficient.

### 1.5 Layout Strategy

Use one fixed shared synth-control row above the standalone runtime panels.

Recommended standalone layout:

```text
title row
shared synth row
  -> oscillator section
  -> envelope section
  -> output section
standalone audio panel
standalone MIDI input panel
standalone MIDI monitor panel
```

Recommended plugin layout:

```text
title row
shared synth row
  -> oscillator section
  -> envelope section
  -> output section
```

Sizing guidance:

- Increase the editor width from the current `900` if needed so the synth row is not cramped.
- Use the same editor class for standalone and VST3.
- Only the overall height should differ materially between standalone and plugin mode.

Section-content guidance:

- `Oscillator`: waveform label + waveform selector.
- `Envelope`: four rotary controls in one row.
- `Output`: one master-gain fader and the panic button.

## 2. File-Level Strategy

### 2.1 Required Code-Change Set

`CMakeLists.txt`

- Add the new `src/ui/HardwareKnob.cpp`, `src/ui/HardwareFader.cpp`, and `src/ui/SynthSection.cpp` entries to `target_sources(...)`.

`src/plugin/SynthAudioProcessor.h`

- No public API redesign.
- Keep `getValueTreeState()` as the editor attachment seam.
- If helper declarations are added for waveform decoding, keep them private.

`src/plugin/SynthAudioProcessor.cpp`

- Bind the `waveform` raw parameter pointer.
- Extend `makeBlockRenderParameters()` to include waveform.
- Keep ADSR and master-gain snapshot behavior intact.

`src/synth/SynthParameters.h`

- Add waveform to `BlockRenderParameters`.
- Add waveform to `ParameterValuePointers`.

`src/synth/SynthEngine.h`

- Declare a narrow `pushWaveformToVoices(...)` helper.

`src/synth/SynthEngine.cpp`

- Push waveform to voices each block before rendering.
- Leave voice count, master-gain smoothing, and panic logic unchanged.

`src/synth/SynthVoice.h`

- Add waveform state and the narrow setter needed by the engine.
- Add a waveform sample helper declaration.

`src/synth/SynthVoice.cpp`

- Replace the fixed sine-only oscillator lambda with a waveform-aware lambda.
- Keep note start, note stop, ADSR, and velocity-scaling behavior intact.

`src/plugin/SynthAudioProcessorEditor.h`

- Replace placeholder-only members with section, control, and attachment members.
- Add timer support for value-label refresh.

`src/plugin/SynthAudioProcessorEditor.cpp`

- Build the new shared synth row.
- Preserve the existing standalone panel gating.
- Populate the waveform selector in the same order as `WaveformChoice`.
- Create attachments.
- Remove or neutralize the phase-specific status copy.

`src/ui/HardwareKnob.h` and `src/ui/HardwareKnob.cpp`

- Add the reusable rotary control with title and value text.

`src/ui/HardwareFader.h` and `src/ui/HardwareFader.cpp`

- Add the reusable vertical control with title and value text.

`src/ui/SynthSection.h` and `src/ui/SynthSection.cpp`

- Add the titled section backdrop and inner-bounds helper.

### 2.2 Audit-First or Conditional Files

`src/parameters/ParameterLayout.cpp`

- Audit first.
- Do not edit unless the current parameter text cannot be consumed cleanly by the editor value-label path.
- If edited, keep parameter IDs, ranges, defaults, and choice order unchanged.

### 2.3 Explicit Non-Owners For This Phase

These files should remain untouched unless validation exposes a concrete local defect:

- `src/ui/StandaloneAudioStatusPanel.*`
- `src/ui/StandaloneMidiInputPanel.*`
- `src/ui/MidiMonitorPanel.*`
- `src/standalone/*`
- `src/midi/*`

Reason:

- They are integration surfaces the new synth row must coexist with, but they do not own Phase 5 behavior.

### 2.4 Closeout-Only Files After Validation

These are not part of the first implementation pass, but they must be updated after the code is verified and reviewed:

- `README.md`: update the current status and mention the new shared control surface.
- `TODO.md`: move only the completed Phase 5 checklist items.
- `DONE.md`: add a verified Phase 5 summary only after the full validation protocol passes.

## 3. Atomic Execution Steps

### 3.1 TODO: Add waveform support for sine, square, and saw

#### Plan

- Extend the existing block snapshot path so the waveform parameter reaches the audio thread the same way ADSR and master gain already do.
- Reuse `WaveformChoice` from `ParameterIDs.h` as the canonical waveform enum.
- Make the existing oscillator waveform-selectable without reinitializing it during playback.

#### Act

1. Add `waveform` to `ParameterValuePointers` and bind it in `SynthAudioProcessor::bindParameterPointers(...)`.
2. Add `waveform` to `BlockRenderParameters` and populate it in `makeBlockRenderParameters()` via a narrow decode helper.
3. Add `setWaveform(...)` to `SynthVoice` and `pushWaveformToVoices(...)` to `SynthEngine`.
4. Replace the fixed sine lambda in `SynthVoice` with a lambda that routes through `renderWaveSample(...)`.
5. Implement the three waveforms with simple bipolar sample generation and no allocation.
6. Leave note-on frequency, ADSR, and velocity handling unchanged.

#### Validate

- Build the project in `Debug`.
- Launch the standalone app.
- Confirm the default waveform is now `saw`, because that is already the APVTS default.
- Play repeated notes while cycling `sine`, `square`, and `saw`; confirm clearly different timbres.
- Hold a sustained note and change waveform; the sound should change within the next render block or at minimum on the next retrigger, and the app must not crash or hang.
- Confirm master gain and ADSR still behave as before after the waveform change lands.

### 3.2 TODO: Add APVTS-backed UI controls for waveform, ADSR, and master gain

#### Plan

- Introduce three reusable UI primitives rather than embedding all controls directly into the editor.
- Use `ComboBoxAttachment` for waveform and `SliderAttachment` for numeric controls.
- Keep panic as a shared action button, not a parameter.

#### Act

1. Add `HardwareKnob`, `HardwareFader`, and `SynthSection` components under `src/ui/`.
2. Add one waveform selector, four ADSR knobs, one master-gain fader, and the existing panic button to the editor.
3. Populate the waveform selector in parameter order: `sine`, `square`, `saw`.
4. Construct exactly one APVTS attachment per interactive control.
5. Remove the old phase-specific placeholder status label or replace it with neutral copy.
6. Keep standalone audio, MIDI input, and MIDI monitor panels gated exactly as they are now.

#### Validate

- Build both shared targets from the existing project.
- Open the standalone app and verify every control renders and is interactable.
- Move each ADSR control and confirm it does not snap back or desynchronize.
- Move the master-gain fader during playback and confirm the sound level changes smoothly.
- Confirm the panic button still stops active notes.
- Confirm the VST3 target still compiles with the same shared editor code.

### 3.3 TODO: Add meaningful value formatting for envelope times, sustain, and master gain

#### Plan

- Reuse the existing parameter string formatters from `ParameterLayout.cpp`.
- Update value labels from actual parameter text, not duplicated math in the UI.
- Keep formatting display-only and message-thread-only.

#### Act

1. Add `ParameterRefs` to the editor and bind them once in the constructor.
2. Add `refreshValueDisplays()` and a low-frequency `Timer` callback.
3. In each timer tick, read the current parameter text and set it on the corresponding `HardwareKnob` or `HardwareFader`.
4. Leave `ParameterLayout.cpp` unchanged unless a concrete formatting gap is found.
5. If a formatting gap is found, fix it in the parameter layer so all future UI consumers benefit.

#### Validate

- Confirm attack, decay, and release values display `ms` below one second and `s` above that threshold.
- Confirm sustain displays as a percentage.
- Confirm master gain displays in `dB`.
- Move controls slowly across boundary values such as `999 ms` to `1.0 s` and confirm the display stays coherent.
- Confirm value labels continue updating while notes are playing.

### 3.4 TODO: Organize the first editor layout into oscillator, envelope, and output sections

#### Plan

- Replace the current placeholder stacking layout with one shared synth-control row.
- Keep the standalone-only runtime panels below that row.
- Avoid creating a separate plugin-only editor.

#### Act

1. Add three section components to the editor: `Oscillator`, `Envelope`, and `Output`.
2. Put the waveform selector inside `Oscillator`.
3. Put the four ADSR knobs inside `Envelope`.
4. Put the master-gain fader and panic button inside `Output`.
5. Update `resized()` to lay out the synth row first, then standalone panels if present.
6. Adjust `setSize(...)` so the synth row is readable without crowding.

#### Validate

- Confirm no control overlaps or clips at the default editor size.
- Confirm standalone mode still shows the audio panel, MIDI input panel, and monitor beneath the synth row.
- Confirm plugin mode excludes standalone-only panels while keeping the synth row intact.
- Confirm every user-facing control has a visible text label.
- Confirm section titles remain legible with the existing black-background styling.

### 3.5 TODO: Verify UI changes are audible and remain responsive during playback

#### Plan

- Preserve the existing audio-thread ownership model.
- Keep all control movement feeding the APVTS only.
- Let the processor continue block-snapshot reads and let the engine continue audio rendering.

#### Act

1. Do not add direct editor-to-voice calls.
2. Keep waveform, ADSR, and master gain reads inside `makeBlockRenderParameters()`.
3. Preserve smoothed master gain in `SynthEngine`.
4. Keep all value-label updates on the editor timer only.
5. Avoid parameter listeners, locks, or message-thread callbacks in the audio path.

#### Validate

- Hold a three- or four-note chord and move ADSR controls; the editor should remain responsive and the app should remain stable.
- Move the waveform selector during playback and confirm the app remains responsive.
- Move the master-gain fader rapidly and confirm audible level changes remain smooth enough for the phase.
- Confirm no new warnings or obvious UI stutters appear during normal manual use.
- Confirm there is no new logging, file I/O, allocation, or locking in the render path introduced by the phase.

### 3.6 TODO: Verify hardware-style control attachments stay in sync with the shared parameter model

#### Plan

- Let attachments own transport.
- Let the editor timer own display-text refresh.
- Treat external-origin sync as architectural in Phase 5 and revalidate it later under controller mapping and host automation phases.

#### Act

1. Ensure every interactive editor control has exactly one APVTS attachment.
2. Keep attachments declared after controls so destruction order is safe.
3. Use control callbacks only for local repaint behavior if needed; do not write parameters manually from those callbacks.
4. Drive displayed values from the live parameter objects, not cached UI-local values.
5. Confirm no control stores a direct `SynthEngine` or `SynthVoice` reference.

#### Validate

- Code review check: every control is attachment-backed and there are zero direct parameter writes from the editor outside the attachment objects.
- Runtime check: moving a control updates both the control position and its displayed value text.
- Runtime check: reopening the standalone window in the same session, if exercised, must still reflect the current APVTS values rather than default UI-local state.
- Deferred cross-check note: true external-origin sync from MiniLab mapping and host automation is revalidated in `Phase 7` and `Phase 11`; Phase 5 must establish the architecture that makes those later validations possible.

## 4. Edge Case and Boundary Audit

| Failure mode | Why it matters | Required guard |
| --- | --- | --- |
| Waveform raw value restored out of range from state | Invalid enum cast can select undefined behavior | Clamp before converting to `WaveformChoice` |
| Default sound changes from sine to saw after Phase 5 | Manual testers may think the change is a regression | Document it in `README.md` and treat it as the required consequence of activating the existing default waveform parameter |
| Combo-box item order drifts from `WaveformChoice` order | UI selection will target the wrong waveform | Add items in `sine`, `square`, `saw` order and never insert a placeholder entry |
| Attachments outlive controls | Destruction-time crashes or dangling listeners | Declare attachments after control members |
| Value labels only update on local drag | Future hardware or host changes would not be reflected | Refresh value labels from the live parameter state on a timer |
| UI duplicates formatting rules instead of reusing parameter text | Unit displays drift from the canonical parameter model | Pull display text from parameter objects, not hand-written UI math |
| `SynthVoice` reinitializes the oscillator when waveform changes | Risk of allocation or unnecessary reconfiguration in the audio path | Initialize once and switch waveform inside the lambda |
| Waveform switch causes a small click mid-note | Timbral change may introduce a discontinuity | Accept a small artifact for this phase if bounded, but require no crash, stall, or stuck voice |
| Square or saw appear much louder than sine | Perceived output jumps can surprise validation | Validate with default `masterGainDb = -12 dB` and confirm output remains practical |
| Editor writes parameters manually through `setValueNotifyingHost()` | Can introduce listener-lock behavior and duplicate transport paths | Use attachments only |
| Standalone panels stop fitting after the synth row is added | Phase 3 functionality becomes unusable | Increase editor size and lay out shared synth controls before standalone panels |
| Plugin editor accidentally shows standalone device panels | Violates target separation requirements | Keep the existing standalone gate in the editor constructor and layout |
| `CMakeLists.txt` misses one new UI source file | Build fails even though code is correct | Update `target_sources(...)` in the same implementation slice |
| Parameter text refresh timer is too frequent | Unnecessary UI work during playback | Use a modest display refresh rate only |
| Removing the old status label without reclaiming the space cleanly | Dead vertical padding wastes layout room | Rebuild the top-level layout rather than patching the old rectangles |
| Future Phase 7 hardware mapping changes parameter values off the UI thread | UI could go stale if display text depends only on local control callbacks | Keep the timer-based value-display path in place from Phase 5 onward |

## 5. Verification Protocol

### 5.1 Automated Checks

There is no dedicated unit-test harness in scope yet. For Phase 5, the required automated validation is build- and diagnostics-based.

Checklist:

1. Configure remains unchanged; use the existing build tree and generator.
2. Build `Debug` successfully from the current workspace.
3. Confirm both standalone and VST3 outputs still compile from the shared target.
4. Run file diagnostics on all touched implementation files and clear any new errors.
5. Review compiler output for new warnings in touched files.
6. Review the final diff and confirm there are no unrelated standalone or MIDI-monitor edits.

Minimum commands or equivalent VS Code actions:

```text
cmake --build build --config Debug
```

Optional but useful if the environment is already configured:

```text
Build_CMakeTools default target
```

### 5.2 Manual UX Checks

Checklist:

1. Launch the standalone app and confirm the synth row is visible above the existing standalone panels.
2. Confirm the waveform selector shows `saw` on startup because that is the current parameter default.
3. Play notes and confirm startup sound is audible.
4. Switch to `sine`, retrigger notes, and confirm a smoother, less bright timbre.
5. Switch to `square`, retrigger notes, and confirm a brighter hollow timbre.
6. Move `Attack` from very short to long and confirm note onset changes audibly.
7. Move `Decay` and `Sustain` and confirm the held-note contour changes audibly.
8. Move `Release` from short to long and confirm note tails lengthen audibly.
9. Move `Master` while holding notes and confirm output level changes smoothly.
10. Press `Panic` and confirm active notes stop immediately.
11. While notes are playing, move multiple controls and confirm the editor remains responsive.
12. Confirm the standalone audio panel, MIDI input panel, and MIDI monitor still render and remain usable.

### 5.3 Structural Review Checks

Checklist:

1. Every interactive synth control is attachment-backed.
2. The editor does not hold a direct reference to `SynthEngine` or `SynthVoice`.
3. `processBlock()` is still the single parameter-snapshot site for the audio path.
4. No parameter IDs, ranges, defaults, or choice order changed unintentionally.
5. No new real-time hazards were introduced into `SynthVoice`, `SynthEngine`, or `SynthAudioProcessor`.

## 6. Code Scaffolding

The following scaffolding is intentionally partial. It is meant to keep the implementation slice idiomatic and consistent with the current codebase, not to paste in a full finished feature.

### 6.1 `HardwareKnob` Scaffold

```cpp
// src/ui/HardwareKnob.h
namespace coolsynth::ui
{
    class HardwareKnob final : public juce::Component
    {
    public:
        explicit HardwareKnob(juce::String labelText);

        juce::Slider& slider() noexcept { return knob; }
        void setValueText(const juce::String& text)
        {
            valueLabel.setText(text, juce::dontSendNotification);
        }

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag,
                            juce::Slider::NoTextBox };
        juce::Label valueLabel;
    };
}
```

### 6.2 `HardwareFader` Scaffold

```cpp
// src/ui/HardwareFader.h
namespace coolsynth::ui
{
    class HardwareFader final : public juce::Component
    {
    public:
        explicit HardwareFader(juce::String labelText);

        juce::Slider& slider() noexcept { return fader; }
        void setValueText(const juce::String& text)
        {
            valueLabel.setText(text, juce::dontSendNotification);
        }

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider fader { juce::Slider::LinearVertical,
                             juce::Slider::NoTextBox };
        juce::Label valueLabel;
    };
}
```

### 6.3 `SynthSection` Scaffold

```cpp
// src/ui/SynthSection.h
namespace coolsynth::ui
{
    class SynthSection final : public juce::Component
    {
    public:
        explicit SynthSection(juce::String titleText)
            : title(std::move(titleText))
        {
        }

        juce::Rectangle<int> getContentBounds() const noexcept
        {
            auto area = getLocalBounds().reduced(12);
            area.removeFromTop(24);
            return area;
        }

        void paint(juce::Graphics& g) override;

    private:
        juce::String title;
    };
}
```

### 6.4 Editor Wiring Scaffold

```cpp
// inside SynthAudioProcessorEditor constructor
parameterRefs.waveform = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::waveform);
parameterRefs.ampAttackMs = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::ampAttackMs);
parameterRefs.ampDecayMs = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::ampDecayMs);
parameterRefs.ampSustain = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::ampSustain);
parameterRefs.ampReleaseMs = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::ampReleaseMs);
parameterRefs.masterGainDb = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::masterGainDb);

waveformSelector.addItemList({ "sine", "square", "saw" }, 1);

waveformAttachment = std::make_unique<ComboBoxAttachment>(processor.getValueTreeState(),
                                                          coolsynth::parameters::ids::waveform,
                                                          waveformSelector);
attackAttachment = std::make_unique<SliderAttachment>(processor.getValueTreeState(),
                                                      coolsynth::parameters::ids::ampAttackMs,
                                                      attackKnob.slider());
// repeat for decay, sustain, release, master gain

startTimerHz(20);
refreshValueDisplays();
```

### 6.5 Waveform-Aware Voice Scaffold

```cpp
void SynthVoice::setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept
{
    currentWaveform = waveform;
}

float SynthVoice::renderWaveSample(float phase,
                                   coolsynth::parameters::WaveformChoice waveform) noexcept
{
    switch (waveform)
    {
        case coolsynth::parameters::WaveformChoice::sine:
            return std::sin(phase);

        case coolsynth::parameters::WaveformChoice::square:
            return phase < 0.0f ? -1.0f : 1.0f;

        case coolsynth::parameters::WaveformChoice::saw:
            return juce::jmap(phase,
                              -juce::MathConstants<float>::pi,
                              juce::MathConstants<float>::pi,
                              -1.0f,
                              1.0f);
    }

    return std::sin(phase);
}
```

### 6.6 Editor Value Refresh Scaffold

```cpp
void SynthAudioProcessorEditor::timerCallback()
{
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::refreshValueDisplays()
{
    attackKnob.setValueText(getCurrentParameterText(parameterRefs.ampAttackMs));
    decayKnob.setValueText(getCurrentParameterText(parameterRefs.ampDecayMs));
    sustainKnob.setValueText(getCurrentParameterText(parameterRefs.ampSustain));
    releaseKnob.setValueText(getCurrentParameterText(parameterRefs.ampReleaseMs));
    masterGainFader.setValueText(getCurrentParameterText(parameterRefs.masterGainDb));
}
```

## Recommended Review Boundary

Keep Phase 5 to one review-sized slice with exactly two technical themes:

1. Make the existing synth path waveform-aware through the existing parameter snapshot flow.
2. Replace the placeholder editor with the first parameter-driven control surface.

Do not pull filter, delay, controller mapping, or persistence work into this review cycle. Those are later phases with their own acceptance surfaces.
