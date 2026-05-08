# DESIGN.md

# CoolSynth Design

This document describes the software design for a JUCE, C++, and CMake synthesizer project targeting Windows 11. The synth is designed for the Arturia MiniLab 3 MIDI controller, but the core engine should remain generic enough to work with any MIDI controller or DAW host.

The project is intentionally structured from day one to support both a standalone application and a VST3 instrument plugin. The standalone application is the primary development and debugging target. The VST3 plugin is a secondary target that should share the same processor, parameter model, audio engine, and UI where practical.

# Design Summary

The project uses a JUCE `AudioProcessor` as the central object. This is the correct architectural center because JUCE audio plugins are built around `AudioProcessor`, and standalone builds can also use the same processor. This avoids building a standalone-only design that later needs to be rewritten for VST3.

The main design is:

```text
Standalone wrapper or VST3 wrapper
  -> SynthAudioProcessor
     -> AudioProcessorValueTreeState parameters
     -> SynthEngine
        -> juce::Synthesiser
           -> SynthVoice instances
           -> SynthSound
        -> global effects
     -> MidiMappingEngine
     -> runtime status model
  -> SynthAudioProcessorEditor
     -> hardware-style JUCE UI
     -> parameter attachments
     -> MIDI monitor view for standalone/debug
```

The synth should use JUCE-provided DSP and audio infrastructure where practical. The first version should not implement custom oscillator, filter, envelope, or delay DSP from scratch. The goal is to keep the code clean and playable while still exposing the architecture of a real synth.

# Main Design Goals

The design shall optimize for:

- Clean separation between audio, MIDI, UI, and state.
- Shared standalone and VST3 architecture.
- Safe real-time audio behavior.
- Simple but practical synthesis.
- A hardware-synth-like UI inspired by the MiniLab 3 layout.
- Fixed MiniLab 3 mapping first, MIDI learn later.
- Maintainable code that can grow without immediate redesign.

The design shall not optimize for:

- Deep custom DSP implementation in the first version.
- Complex modulation.
- Commercial preset management.
- Advanced anti-aliasing.
- Pixel-perfect MiniLab 3 visual replication.
- Full DAW workflow polish in the first milestones.

# External Technical Basis

The design relies on these JUCE concepts:

- `juce_add_plugin` with `FORMATS Standalone VST3` for building both standalone and plugin targets from CMake.
- `juce::AudioProcessor` as the shared audio processing class for plugin and standalone operation.
- `juce::AudioProcessorValueTreeState` as the central plugin-compatible parameter and state container.
- `juce::Synthesiser`, `juce::SynthesiserVoice`, and `juce::SynthesiserSound` for polyphonic MIDI-triggered voices.
- JUCE DSP classes for oscillators, filters, smoothing, and delay where suitable.
- JUCE components and parameter attachments for the UI.

Reference material:

- JUCE CMake API: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- JUCE audio plugin CMake example: https://github.com/juce-framework/JUCE/blob/master/examples/CMake/AudioPlugin/CMakeLists.txt
- JUCE `AudioProcessor`: https://docs.juce.com/master/classAudioProcessor.html
- JUCE `AudioProcessorValueTreeState`: https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html
- JUCE MIDI synthesizer tutorial: https://juce.com/tutorials/tutorial_synth_using_midi_input/
- JUCE save/load plugin state tutorial: https://juce.com/tutorials/tutorial_audio_processor_value_tree_state/

# Target Product Modes

# Standalone Application

The standalone app is the primary development target.

Responsibilities:

- Own audio device setup.
- Own MIDI hardware input setup.
- Show audio and MIDI device status.
- Show a MIDI monitor.
- Allow testing the MiniLab 3 without a DAW.
- Use the same synth processor and editor as the plugin where practical.

The standalone app should expose JUCE audio device settings. Since the target hardware is motherboard line-out to active speakers, the app should be usable with WASAPI. ASIO should not be required.

# VST3 Plugin

The VST3 plugin is a shared-code target.

Responsibilities:

- Receive MIDI from the DAW host.
- Render audio through the same `SynthAudioProcessor`.
- Expose plugin parameters through the same parameter model.
- Show the same synth UI, except standalone-only device controls should be hidden or disabled.

The plugin should not open hardware MIDI devices directly. In plugin mode, MIDI comes from the host.

# Shared Versus Mode-Specific Code

Shared code:

- Synth engine.
- Voices and sound model.
- Parameters.
- UI controls.
- UI layout, except device panels.
- Fixed MiniLab mapping logic for MIDI CCs.
- Preset/state logic.

Standalone-only code:

- Hardware MIDI input selection.
- Audio device configuration UI.
- MIDI monitor input source display.
- Runtime device-disconnect handling.

Plugin-only code:

- Host-specific validation assumptions.
- DAW automation behavior.
- Plugin category/metadata.
- Host-provided MIDI routing.

# Build Design

# CMake Strategy

The project should use CMake only. It should not require Projucer.

The JUCE dependency acquisition method is an implementation detail, but it must be reproducible from a clean checkout and documented in the repository.

Recommended build strategy:

- Use a documented, reproducible JUCE acquisition method.
- Pin JUCE to a known tag or commit once the first skeleton builds.
- Use `juce_add_plugin` to create one target with `FORMATS Standalone VST3`.

The first CMake design should avoid unnecessary complexity. A single plugin target with both formats is preferable to separate manually maintained app and plugin targets.

# Compiler And Editor

Development environment:

- Windows 11.
- Visual Studio Code.
- CMake Tools extension.
- MSVC compiler from Visual Studio 2022 Build Tools or full Visual Studio 2022.
- Optional Ninja generator.

The project should build from the command line as well as from VS Code.

# Plugin Metadata

The plugin should be configured as an instrument.

Important JUCE plugin settings:

```text
IS_SYNTH TRUE
NEEDS_MIDI_INPUT TRUE
NEEDS_MIDI_OUTPUT FALSE
IS_MIDI_EFFECT FALSE
FORMATS Standalone VST3
```

The plugin should avoid MIDI output initially. MIDI output can be revisited later if arpeggiator or MIDI tools are added.

# Directory Layout Guidance

The exact repository tree is not a design requirement. The implementation should keep these concerns separated enough for independent review:

- Shared processor and state.
- Synth engine, voices, and DSP.
- MIDI mapping and monitor support.
- UI components and editor wiring.
- Standalone shell concerns such as device selectors and persisted settings.

Folder names are an implementation detail. If implementation becomes too fragmented, small files may be merged as long as the separation of concerns remains clear.

# Processor Design

# SynthAudioProcessor

`SynthAudioProcessor` is the central class. It owns audio state and exposes plugin-compatible behavior.

Responsibilities:

- Define the audio bus layout.
- Own `AudioProcessorValueTreeState`.
- Own `SynthEngine`.
- Own global effects or delegate them to `SynthEngine`.
- Receive MIDI buffers in `processBlock`.
- Apply MiniLab CC mapping to parameters.
- Render audio.
- Save and restore plugin state.
- Expose editor creation.

It should not:

- Contain UI layout logic.
- Contain MiniLab CC constants directly.
- Contain detailed DSP implementation beyond routing.
- Write logs or perform slow operations in `processBlock`.

# Processor Lifecycle

Expected lifecycle:

```text
constructor
  -> create parameter layout
  -> initialize APVTS
  -> initialize SynthEngine and MIDI mapping objects

prepareToPlay(sampleRate, maxBlockSize)
  -> prepare SynthEngine
  -> prepare voices
  -> prepare delay/effects
  -> reset smoothing objects

processBlock(audioBuffer, midiBuffer)
  -> clear or prepare output buffer
  -> process MIDI CC mapping
  -> pass note MIDI to SynthEngine
  -> render voices
  -> process global effects
  -> apply master gain

releaseResources()
  -> release or reset temporary resources if needed

getStateInformation()
  -> serialize APVTS state

setStateInformation()
  -> restore APVTS state
```

# Audio Bus Layout

The synth is an instrument. It does not need audio input initially.

Initial bus design:

```text
input buses: none
output buses: stereo
```

The processor should reject unsupported layouts where practical.

Supported output:

- Mono may be allowed for compatibility.
- Stereo should be the normal target.

# Parameter Design

# Central Parameter Model

All user-visible synth controls should be JUCE parameters where practical. This makes them compatible with VST3 automation and makes UI attachment simpler.

Use `juce::AudioProcessorValueTreeState`.

Use a dedicated `createParameterLayout()` function, probably in `ParameterLayout.cpp`.

Every parameter should have:

- Stable ID.
- Human-readable name.
- Range.
- Default value.
- Unit or display formatter.
- Mapping behavior, linear or skewed.

Parameter IDs must be treated as persistent API. Renaming an ID can break saved plugin state and presets.

# Initial Parameters

```text
waveform
ampAttackMs
ampDecayMs
ampSustain
ampReleaseMs
filterCutoffHz
filterResonance
delayTimeMs
delayFeedback
delayMix
masterGainDb
```

Panic is an explicit action, not an automatable parameter. The UI should call a safe processor method that schedules all voices to stop and clears held-note state.

# Parameter Ranges

Recommended ranges:

```text
waveform: choice, sine/square/saw, default saw
ampAttackMs: 1 ms to 5000 ms, skewed, default 10 ms
ampDecayMs: 5 ms to 5000 ms, skewed, default 200 ms
ampSustain: 0.0 to 1.0, default 0.8
ampReleaseMs: 5 ms to 5000 ms, skewed, default 300 ms
filterCutoffHz: 20 Hz to 20000 Hz, logarithmic/skewed, default 10000 Hz
filterResonance: 0.0 to 1.0, default 0.1, mapped internally to a stable JUCE-appropriate range
delayTimeMs: 1 ms to 1000 ms, skewed, default 250 ms
delayFeedback: 0.0 to 0.85, default 0.25
delayMix: 0.0 to 1.0, default 0.0
masterGainDb: -60 dB to 0 dB, default -12 dB
```

The first release should not add separate filter-enable or delay-enable parameters unless a clear functional need appears. High cutoff and zero delay mix provide the practical bypass behavior.

# Parameter Reading In Audio Code

The audio code should cache raw parameter pointers during construction or preparation.

Example design:

```text
ParameterAccess
  -> std::atomic<float>* waveform
  -> std::atomic<float>* attack
  -> std::atomic<float>* cutoff
  -> ...
```

Do not repeatedly search parameters by string inside `processBlock`.

For parameters that can click or zipper, use smoothing:

- Master gain.
- Filter cutoff.

Delay parameters may be updated at control rate if stable. Delay-time changes do not need seamless modulation behavior in the first release, but they must remain real-time safe.

# MIDI Design

# MIDI Input Flow

There are two MIDI flows:

```text
Standalone hardware MIDI
  -> standalone wrapper / MIDI input callback
  -> processor MIDI queue or MIDI buffer
  -> processBlock

VST3 host MIDI
  -> host
  -> processBlock midiBuffer
```

The synth engine should not care whether MIDI came from the MiniLab 3 directly or from a DAW.

# MIDI Message Split

During processing, MIDI should be split conceptually:

```text
MIDI notes
  -> SynthEngine / juce::Synthesiser

MIDI CCs
  -> MidiMappingEngine
  -> APVTS parameter changes

Other MIDI
  -> ignore or future handling
```

Do not send CC messages directly to voices unless a specific voice-level feature requires it. For now, CCs modify parameters.

For the first functional release:

- Support note on, note off, note-on with velocity zero treated as note-off, velocity, and CC messages.
- Ignore pitch bend, aftertouch, program change, and other deferred MIDI safely.
- Accept notes and CCs on any MIDI channel.
- Defer user-selectable channel filtering until later.

# Fixed MiniLab 3 Mapping

The first mapping is fixed by MiniLab control role, with exact CC values verified through the MIDI monitor.

`Minilab3Profile` should contain:

- Device name match hints.
- Verified CC-to-parameter bindings for the implemented control set.
- Deferred pad and main-encoder status until the actual device messages are captured.
- Reserved controls.

Example design:

```cpp
struct ControlBinding
{
    int controllerNumber;   // CC number
    const char* parameterID;
    MappingCurve curve;
};
```

Pad actions are deferred for the first functional release. If later the default pad messages are simple and reliable, they should be modeled as commands rather than hardwired processor branches:

```text
Pad event
  -> MidiMappingEngine
  -> Command
  -> Processor or UI-safe command handler
```

If pad support is added later, the first candidate commands should be waveform direct-select and panic. The main encoder should remain unassigned initially.

# MIDI Monitor Design

The MIDI monitor should be useful during development, but it must not compromise the audio thread.

Design:

- Store a small ring buffer of recent MIDI event summaries.
- Event summaries should be plain data, not large strings created in the audio callback.
- UI converts summaries to text on the message thread.
- Limit size, for example 128 recent events.

For a simple first version, the monitor can be updated from the standalone MIDI callback, not from `processBlock`. In plugin mode it can be hidden or show host MIDI events if easy.

# MIDI Learn Later

The initial design should leave room for MIDI learn.

Future mapping model:

```text
Default profile
  + user overrides
  + persisted mapping file
```

MIDI learn should not require changing the synth engine. It should only update `MidiMappingEngine` configuration.

# Synth Engine Design

# SynthEngine

`SynthEngine` owns and coordinates voice rendering.

Responsibilities:

- Own `juce::Synthesiser`.
- Add voices and sounds.
- Prepare voices with sample rate and block size.
- Render voices into the output buffer.
- Apply or delegate global effects.
- Stop all voices on panic.

It should not:

- Own UI components.
- Know about MiniLab hardware.
- Know about CMake targets.
- Directly read UI controls.

# Voice Model

Use JUCE `SynthesiserVoice`.

Each voice should contain:

- One oscillator.
- One amplitude envelope.
- One per-voice low-pass filter.
- Current note frequency.
- Current velocity-derived gain scalar.
- Temporary mono voice buffer if needed.

Initial voice signal path:

```text
MIDI note
  -> voice startNote
  -> oscillator frequency set
  -> ADSR noteOn

per sample or block
  -> oscillator
  -> filter
  -> amplitude envelope
  -> velocity scaling, amplitude only
  -> add to output buffer

noteOff
  -> ADSR noteOff
  -> clear voice when envelope is inactive
```

# Voice Count

Default voice count:

```text
8 voices
```

This matches the first playable milestone and keeps the initial engine simple while still supporting practical polyphony.

Voice stealing should prefer a voice already in release. If none are available, steal the oldest active voice. Panic should clear active voices and held-note state immediately.

# Oscillator Design

The first version uses one oscillator per voice.

Waveforms:

- Sine.
- Square.
- Sawtooth.

Design choices:

- Use JUCE DSP oscillator or another JUCE-supported oscillator path.
- Do not implement custom oscillator DSP initially.
- Accept that simple square/saw behavior may not be as polished as a commercial synth.

Waveform switching should be controlled by a choice parameter.

Important design issue:

- Changing oscillator waveform while notes are held can create clicks.

Acceptable first behavior:

- Immediate switch with possible small click.

Better later behavior:

- Smooth crossfade between oscillator shapes.
- Apply switch only on next note.

# Envelope Design

Use JUCE ADSR or a JUCE-compatible envelope design.

The ADSR parameters are shared by all voices but applied per voice.

Parameter updates:

- Voices should update ADSR parameters at block boundaries.
- Updating ADSR during active notes should be supported but does not need perfect analog-synth behavior initially.

# Filter Design

Required design:

```text
per-voice low-pass filter
```

Why per voice:

- More synth-like behavior.
- Notes do not all share one cutoff state.
- It matches the first functional release architecture.

Filter parameters:

- Cutoff.
- Resonance.

There is no separate filter-enable parameter in the first release. A high cutoff should act as the practical bypass behavior.

Resonance should be exposed as a normalized 0.0 to 1.0 parameter and mapped internally to a stable JUCE-appropriate range.

Cutoff should be clamped to a safe range and smoothed before applying it.

# Delay Design

The delay is global, after all voices are mixed.

Signal path:

```text
voice mix
  -> delay effect
  -> master gain
  -> output
```

Global delay is simpler and cheaper than per-voice delay. It also matches typical synth effect design.

Delay parameters:

- Time in milliseconds.
- Feedback.
- Mix.

There is no separate delay-enable parameter in the first release. Delay mix at 0.0 should behave as effectively dry.

Stereo delay is preferred. If the implementation starts mono internally, it should still output stereo correctly.

Delay safety:

- Feedback must be capped at or below 0.85.
- Delay buffers must be prepared outside the audio callback or during `prepareToPlay`.
- Delay buffer resizing must not occur during normal audio processing.

Delay time changes:

- First version can use direct user-control changes if stable.
- Brief artifacts during manual delay-time changes are acceptable.
- Delay-time changes must not allocate in the audio thread, crash the app, or cause runaway feedback.

# Master Output Design

Master stage:

```text
audio after effects
  -> smoothed gain
  -> optional soft limit later
  -> output buffer
```

Use dB parameter for user-facing master gain.

Avoid default 0 dB because polyphonic synth voices can sum and clip. A default around -12 dB is safer.

No limiter is required initially, but a simple meter is useful later.

# Panic Design

Panic should stop all voices and clear held-note state.

Design:

- UI button calls a processor method on the message thread.
- Processor sets an atomic flag or posts a command.
- `processBlock` sees the flag and calls `SynthEngine::panic()` safely.

Avoid directly manipulating audio objects from the UI thread while the audio thread is processing.

Panic behavior:

- Stop all active voices.
- Clear pending note state.

Clearing the residual delay tail is not required for the first release.

Recommended first behavior:

- Stop voices.
- Leave the delay tail alone unless a later full-silence command is added explicitly.

# Audio Thread And Real-Time Safety

# Audio Callback Rules

Inside `processBlock` and voice rendering, do not:

- Allocate memory during normal operation.
- Open files.
- Write logs.
- Build strings.
- Lock mutexes.
- Wait on condition variables.
- Call UI methods.
- Use unbounded containers that may allocate.
- Perform device enumeration.

Allowed:

- Read atomic parameter values.
- Process preallocated buffers.
- Use prepared JUCE DSP objects.
- Use small fixed-size local variables.
- Apply pending flags that are atomic or lock-free.

# Thread Boundaries

Threads involved:

```text
Audio thread
  -> processBlock
  -> voice rendering
  -> effects

Message/UI thread
  -> components
  -> sliders/buttons
  -> parameter attachments

Standalone MIDI callback thread or JUCE MIDI path
  -> hardware MIDI input
  -> MIDI monitor capture
  -> MIDI buffer forwarding
```

The design should avoid sharing mutable objects across these threads without a clear synchronization strategy.

# UI Design

# UI Philosophy

The UI should feel like a small hardware synth/controller panel rather than a generic plugin form.

It should be loosely modeled on the MiniLab 3 layout:

- Knobs grouped in the upper area.
- Fader-like or linear controls where they help readability.
- A dedicated output/action area for master gain and panic.
- Clear device/status strip.
- Optional debug MIDI monitor in standalone mode.

It should not try to exactly clone Arturia's visual design. That may create unnecessary visual work and possible trademark/design concerns. The goal is functional similarity, not visual copying.

# Main Layout

Recommended layout:

```text
+--------------------------------------------------------------------+
| CoolSynth            MIDI: MiniLab 3        Audio: WASAPI 256       |
+--------------------------------------------------------------------+
| Oscillator       Filter             Envelope            Delay       |
| [Waveform]       [Cutoff knob]      [Attack knob]       [Time]      |
|                  [Res knob]         [Decay knob]        [Feedback]  |
|                                     [Sustain knob]      [Mix knob]  |
|                                     [Release knob]                   |
+--------------------------------------------------------------------+
| Output                                    Actions                  |
| [Master gain fader]                       [Panic]                  |
+--------------------------------------------------------------------+
| MIDI Monitor, collapsible in standalone builds                      |
+--------------------------------------------------------------------+
```

# UI Components

Custom UI components:

```text
HardwareKnob
  -> wraps juce::Slider in rotary mode
  -> label
  -> value text
  -> optional mapped CC display later

HardwareFader
  -> wraps juce::Slider in linear vertical mode
  -> label
  -> value text

StatusStrip
  -> MIDI device status
  -> audio backend status
  -> sample rate and buffer size

MidiMonitorPanel
  -> small scrolling list of recent messages
```

Do not over-invest in custom drawing in the first version. A basic LookAndFeel can provide a coherent hardware-like feel without turning the UI into the main project.

# Parameter Attachments

UI controls should use JUCE parameter attachments where possible.

Examples:

```text
SliderAttachment for knobs/faders
ComboBoxAttachment for waveform selection
```

Panic should be an explicit button action, not a normal parameter attachment. If an Init Patch action is added later, it should also call an explicit command that resets automatable parameters to their defaults.

# Standalone UI Differences

Standalone mode should show:

- MIDI input selector.
- Audio device settings button or panel.
- MIDI monitor.
- Device status.

# Plugin UI Differences

Plugin mode should hide or simplify:

- Hardware MIDI input selector.
- Audio backend/device controls.
- MIDI monitor.

Plugin mode should still show:

- Synth controls.
- Panic action.

# State And Persistence Design

# Plugin State

Plugin state should be based on `AudioProcessorValueTreeState`.

Save:

```text
APVTS state
  -> XML or binary block through getStateInformation
```

Restore:

```text
binary block
  -> ValueTree/XML
  -> APVTS replaceState
```

This supports DAW session recall and future presets.

# Standalone Settings

Standalone-specific settings are not the same as synth/plugin state.

Standalone settings:

- Last MIDI input device.
- Last audio device configuration, if supported through JUCE device manager.

Deferred standalone persistence:

- Window size and position.
- Last loaded patch.
- Recent files.

These should not become VST3 plugin state unless there is a clear reason.

# Presets

Presets should be added later.

Design now so presets can be implemented as parameter state snapshots.

Preset should include:

- Synth parameters.

Preset should not necessarily include:

- Audio device.
- MIDI device.

If an Init Patch action exists before full preset support, it should simply reset the automatable parameters to their default values.

Controller mapping should probably be separate from sound presets.

# Error Handling Design

# Standalone Errors

Handle gracefully:

- No MIDI device found.
- MiniLab 3 not connected.
- MIDI device disconnected.
- Audio device unavailable.
- Unsupported sample rate.
- Buffer size too small for stable audio.

UI should show status, not crash.

# Plugin Errors

Handle gracefully:

- Host gives no MIDI.
- Unsupported bus layout.
- State restore data is invalid.
- Parameter values are outside expected range after corrupted state load.

# Logging

Logging must not occur in the audio callback.

Acceptable logging locations:

- Startup.
- Device selection.
- State load/save.
- UI actions.
- Debug builds outside audio callback.

# Latency Design

Current hardware target:

- Motherboard line-out.
- Active speakers.
- No ASIO interface.

Practical plan:

- Use WASAPI first.
- Try 256 samples first.
- Try 128 samples if stable.
- Accept 512 samples if crackling occurs.

The design should expose sample rate and buffer size so latency experiments are visible.

Avoid features that increase latency in the first version:

- Lookahead limiter.
- Convolution reverb.
- Oversampling.
- Heavy analysis.

# Clean Architecture Rules

# Dependencies Between Modules

Allowed dependencies:

```text
processor -> synth
processor -> midi
processor -> parameters
processor -> effects
editor -> ui
editor -> parameters
ui -> parameters
synth -> effects, if effects are inside synth
midi -> parameters by ID or mapping result
```

Avoid dependencies:

```text
synth -> ui
synth -> MiniLab profile
voice -> processor
midi profile -> ui
parameters -> ui
```

# MiniLab Isolation

MiniLab-specific knowledge should live in:

```text
midi/Minilab3Profile.h
midi/Minilab3Profile.cpp
```

The synth should not care whether a CC came from MiniLab 3, another controller, or a DAW automation lane.

# Parameter ID Isolation

Parameter IDs should live in one file:

```text
parameters/ParameterIDs.h
```

This reduces typo risk and makes future refactoring safer.

# Design Risks

# Risk: Standalone And VST3 Complexity Too Early

Supporting both formats from day one may add initial complexity.

Mitigation:

- Use one JUCE plugin target with `FORMATS Standalone VST3`.
- Keep mode-specific UI small.
- Test standalone first.

# Risk: JUCE Synthesiser Is Convenient But Limiting

`juce::Synthesiser` is useful and appropriate for learning, but may become limiting for advanced modulation or sample-accurate parameter changes.

Mitigation:

- Use it for the first implementation.
- Keep `SynthEngine` as a wrapper so a future custom voice manager can replace it if needed.

# Risk: MIDI CC Updating Parameters From Audio Thread

If CC mapping is handled inside `processBlock`, changing APVTS parameters directly from the audio thread may be questionable depending on implementation details.

Mitigation:

- For the first version, process CCs conservatively.
- Prefer host-compatible parameter update paths where possible.
- Consider storing controller-derived values separately if APVTS mutation from audio processing proves unsafe or awkward.

Practical first decision:

- Use CC mapping in standalone for learning.
- Keep DAW automation and UI attachments as the main parameter control path.
- Revisit the cleanest CC-to-parameter method during implementation.

# Risk: Hardware Default CC Map Unknown

The MiniLab 3 default CC map may vary by template or user configuration.

Mitigation:

- Use the MIDI monitor first.
- Encode a profile only after observing actual CCs.
- Add MIDI learn later.

# Risk: Motherboard Audio Latency

Motherboard audio may not provide ideal low latency.

Mitigation:

- Use WASAPI.
- Make buffer size visible and configurable.
- Avoid heavy DSP.
- Do not promise ASIO-like performance without ASIO hardware.

# Rejected Alternatives

# Electron UI

Rejected for this project stage.

Reason:

- Adds web/runtime complexity.
- Does not help VST3 development.
- Less suitable for real-time audio learning.

# Custom DSP From Scratch

Rejected for first version.

Reason:

- User explicitly wants no DSP from scratch initially.
- JUCE DSP keeps the first version simpler and more reliable.

# Standalone-Only Architecture

Rejected.

Reason:

- The project should support VST3 from day one.
- A standalone-only architecture risks rewrite later.

# Unit Tests From Day One

Rejected for first version.

Reason:

- User does not want unit tests initially.
- Architecture should still make future tests possible.

# Implementation Order

Recommended implementation order:

```text
Create CMake/JUCE skeleton
  -> build Standalone and VST3 targets
  -> create SynthAudioProcessor and editor
  -> create APVTS parameter layout
  -> create placeholder hardware-style UI
  -> verify standalone app launches
  -> verify VST3 builds
  -> add audio device/status UI for standalone
  -> add MIDI monitor
  -> add juce::Synthesiser with 8 preallocated voices
  -> add oscillator/envelope parameters
  -> add filter
  -> add delay
  -> add fixed MiniLab profile
  -> add VST3 smoke test
  -> add persistence/presets
  -> add MIDI learn
  -> add CI
```

# First Skeleton Acceptance Criteria

The first code milestone should be considered successful when:

- The project configures with CMake on Windows 11.
- The project builds from VS Code.
- The standalone app opens.
- The VST3 target builds.
- The editor shows a hardware-style placeholder layout.
- An initial APVTS parameter layout exists for the first-release parameter set.
- No actual sound is required yet, unless a test tone is added for convenience.

# Current Open Technical Decisions

These decisions can be made during implementation:

| Topic | Default Decision |
|---|---|
| Exact JUCE version | Use latest stable release or pin after skeleton works |
| CMake generator | Ninja if available, Visual Studio generator otherwise |
| Per-voice filter | Required for the first functional release |
| Stereo delay | Preferred |
| Full virtual keyboard | Not initially |
| Metering | Later |
| MIDI learn | Later |
| Presets | Later |
| CI | Later |

# Final Design Position

The design should start as a real JUCE instrument plugin project, not as a throwaway standalone experiment. The standalone app is the main development surface, but the `AudioProcessor`, APVTS parameter model, and CMake plugin target keep the VST3 path open from the first commit.

The core principle is simple: build the smallest clean architecture that can play notes, expose parameters, map the MiniLab 3, and later survive being loaded in a DAW.
