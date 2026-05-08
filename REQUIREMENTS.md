# Requirements Document: CoolSynth

# Purpose

This document defines the requirements for a custom software synthesizer built with JUCE, C++, and CMake on Windows 11. The project is intended as a learning-oriented but cleanly designed synthesizer that connects to an Arturia MiniLab 3 MIDI controller, responds to notes and hardware controls, and provides simple synthesis building blocks such as oscillators, envelopes, filters, and delay.

The project should be structured from day one so the same core engine can support both a standalone application and a VST3 plugin. The first usable build should still prioritize the standalone application because it is easier to test audio devices, MIDI input, and controller behavior outside a DAW.

The goal is both educational and practical: the software should teach how a synthesizer is structured while remaining simple, maintainable, and playable.

# Project Goals

The software shall:

- Run on Windows 11.
- Be built using C++ and JUCE.
- Use CMake as the build system.
- Be structured to support both standalone and VST3 targets from day one.
- Initially focus on the standalone target for easier testing.
- Receive MIDI input from the Arturia MiniLab 3.
- Respond to keyboard note input with playable synthesized sound.
- Support basic oscillator types: sine, square, sawtooth, and optionally triangle/noise later.
- Use JUCE DSP building blocks where practical instead of implementing DSP from scratch.
- Prioritize simplicity, correctness, and clean architecture over deep custom DSP implementation.
- Support polyphonic playback.
- Provide a graphical user interface inspired by a hardware synth layout and the MiniLab 3 control layout.
- Provide initial fixed mappings between MiniLab 3 hardware controls and synth parameters.
- Add MIDI learn later, after the core synth works.
- Include an ADSR amplitude envelope.
- Include at least one low-pass filter with cutoff and resonance.
- Include a simple delay effect.
- Include a MIDI event monitor for early development and debugging.
- Include a panic/all-notes-off control.
- Avoid unsafe real-time audio practices such as blocking, heap allocation, or mutex locking in the audio callback.

# Non-Goals for the First Version

The first version shall not attempt to provide:

- Custom DSP implementation from scratch.
- Commercial-grade preset management.
- Complex modulation matrix.
- Wavetable synthesis.
- FM synthesis.
- Granular synthesis.
- MPE support.
- Advanced DAW automation behavior beyond basic plugin parameters.
- AU/AAX plugin formats.
- Advanced oversampling.
- Fully band-limited custom oscillators.
- Hardware-specific Arturia integration beyond normal MIDI messages.
- Cloud sync or online functionality.
- Electron, web UI, or browser-based audio.
- CI setup in the first version.
- Unit tests in the first version.

These may be considered later after the standalone and VST3 architecture is stable.

# Target Platform

## Operating System

- Windows 11, 64-bit.

## Development Environment

Recommended development setup:

- Visual Studio Code as the editor.
- Visual Studio 2022 Build Tools or full Visual Studio 2022 for the MSVC compiler and Windows SDK.
- CMake.
- Git.
- JUCE.
- Optional: Ninja build system.

Visual Studio Code should use the CMake Tools extension or equivalent CMake workflow.

## Audio Hardware Assumption

The initial user environment is:

- No external audio interface.
- No dedicated ASIO hardware driver.
- Motherboard line-out connected to active speakers.
- Latency target: as small as practical on this hardware.

This means the app should not assume ASIO is available. WASAPI should be the primary practical Windows audio backend. ASIO support can remain available through JUCE if installed/configured later, but it is not required for the first milestone.

## Audio Backends

The application should support the audio device types available through JUCE on Windows.

Preferred practical runtime order for the current hardware:

- WASAPI, if available and stable.
- DirectSound only as fallback.
- ASIO later if an external audio interface becomes available.

The app shall expose an audio device settings panel so the user can choose device type, output device, sample rate, and buffer size.

# Product Form

## Targets From Day One

The project shall be structured around a JUCE `AudioProcessor` so the same synth engine can be used for:

- Standalone application.
- VST3 plugin.

The repository and CMake setup should define both targets from the start if this does not add excessive complexity.

The standalone target shall be the first target tested and used.

## First Usable Target

The first usable deliverable shall be a standalone desktop application.

Rationale:

- Easier to debug than a plugin.
- No DAW required.
- Easier to add a MIDI monitor.
- Easier to expose audio/MIDI device settings.
- Lower friction during learning.

## VST3 Target

The VST3 target should exist early, but it does not need to be fully polished in the first milestones.

The VST3 target should eventually:

- Receive MIDI from the DAW.
- Expose synth parameters to the DAW.
- Render the same sound engine as the standalone app.
- Share the same UI where practical.

The VST3 plugin should not be the primary testing target until the standalone version works reliably.

# High-Level Architecture

The application should be organized around a reusable audio processor architecture:

```text
Standalone app / VST3 plugin
  -> SynthAudioProcessor
  -> parameter state
  -> MIDI handling
  -> controller mapping
  -> synth engine
  -> voice allocator
  -> JUCE DSP oscillators
  -> JUCE envelope/filter helpers where practical
  -> effects
  -> output
```

The standalone app and VST3 plugin shall share the same processor, parameter model, synth engine, and UI components where possible.

The user interface shall interact with the synth engine through JUCE parameters and safe state transfer, not by directly mutating audio-thread state unsafely.

# Architectural Principles

The codebase should be designed cleanly from the start.

The architecture should be modular, but not over-engineered.

Core principles:

- Keep UI code separate from audio processing code.
- Keep MiniLab-specific mappings separate from generic MIDI handling.
- Keep synth voice logic separate from plugin wrapper code.
- Prefer JUCE DSP classes for oscillators, filters, envelopes, and effects where suitable.
- Prefer explicit parameter definitions with clear ranges and units.
- Use an `AudioProcessorValueTreeState`-style parameter model where appropriate.
- Avoid custom framework abstractions until a real need appears.
- Optimize for readable, maintainable code rather than cleverness.

# Core Functional Requirements

# MIDI Input Requirements

## MIDI Device Selection

For the standalone app, the app shall display available MIDI input devices.

The app shall allow the user to select the Arturia MiniLab 3 as a MIDI input device.

The app should remember the last selected MIDI input device between runs.

If the previous MIDI device is unavailable at startup, the app should show a clear disconnected state and allow selecting a new device.

For the VST3 plugin, MIDI input will normally be provided by the DAW. The plugin does not need its own hardware MIDI device selector.

## MIDI Message Handling

The app shall handle:

- Note on.
- Note off.
- Velocity.
- Control Change messages.
- Pitch bend, optional for first version.
- Program change, optional for first version.

The app shall treat note-on with velocity zero as note-off.

The app shall ignore unsupported MIDI message types safely.

The app shall support configurable MIDI channel behavior:

- Omni mode: accept notes and CCs from all MIDI channels.
- Single-channel mode: accept only one selected channel.

Omni mode should be the default for the first version.

## MIDI Event Monitor

The standalone app shall include a MIDI monitor panel during early development.

For each event, the monitor should show:

- Message type.
- MIDI channel.
- Note number or CC number.
- Note name, where applicable.
- Velocity or CC value.
- Timestamp or relative order.

The monitor shall be useful for discovering what the MiniLab 3 sends from each key, knob, fader, pad, encoder, or touch strip.

The MIDI monitor may be hidden behind a debug/developer panel in later versions if it clutters the hardware-style UI.

# MiniLab 3 Controller Mapping Requirements

## Mapping Strategy

The first version shall use a fixed MiniLab 3 mapping.

MIDI learn shall be added later.

The initial assumption is that the MiniLab 3 will use its default MIDI template. Because the exact default CC values may need to be verified on the user’s unit, the MIDI monitor shall be used to confirm the actual CCs before hardening the default map.

The mapping layer shall be designed so the fixed map can later be replaced or extended by MIDI learn without rewriting the synth engine.

## Default Mapping

The app shall provide an initial fixed mapping suitable for the Arturia MiniLab 3.

Proposed initial mapping by physical control role:

| MiniLab 3 Control | Synth Parameter |
|---|---|
| Keyboard notes | MIDI note on/off |
| Velocity | Voice amplitude and optional filter influence |
| Knob 1 | Oscillator waveform |
| Knob 2 | Filter cutoff |
| Knob 3 | Filter resonance |
| Knob 4 | Amp attack |
| Knob 5 | Amp decay |
| Knob 6 | Amp sustain |
| Knob 7 | Amp release |
| Knob 8 | Delay mix |
| Fader 1 | Master volume |
| Fader 2 | Delay feedback |
| Fader 3 | Delay time |
| Fader 4 | Reserved for future feature, likely modulation or effect amount |
| Pads | Feature toggles, preset actions, or waveform/effect shortcuts |
| Main encoder | Reserved, likely preset selection or focused parameter control |

The exact CC numbers shall be discovered with the MIDI monitor and then encoded into the fixed MiniLab profile.

## Pads

The pads shall be treated as feature controls rather than drums in the first design.

Candidate pad actions:

| Pad Role | Candidate Function |
|---|---|
| Pad 1 | Select sine waveform |
| Pad 2 | Select square waveform |
| Pad 3 | Select sawtooth waveform |
| Pad 4 | Toggle filter on/off |
| Pad 5 | Toggle delay on/off |
| Pad 6 | Init patch |
| Pad 7 | Panic/all notes off |
| Pad 8 | Reserved |

This mapping may change after the actual MiniLab 3 default pad behavior is observed.

## MIDI Learn

The app should support MIDI learn in a later milestone.

MIDI learn behavior:

- User activates MIDI learn for a parameter.
- User moves a hardware control.
- The app captures the incoming CC number and channel.
- The app binds that CC to the selected parameter.
- The app exits MIDI learn mode for that parameter.

The app should show the currently mapped CC number for each assignable parameter.

The app should allow clearing a MIDI mapping.

MIDI learn shall not bind note-on/note-off events to continuous parameters in the first implementation.

## Parameter Scaling

Incoming MIDI CC values range from 0 to 127.

The app shall scale CC values to parameter ranges.

Examples:

- Filter cutoff: logarithmic mapping, for example 20 Hz to 20,000 Hz.
- Resonance: linear or musically useful range.
- ADSR times: nonlinear mapping, for example 1 ms to 10 s.
- Sustain: linear 0.0 to 1.0.
- Delay mix: linear 0.0 to 1.0.
- Delay feedback: limited to a safe range, for example 0.0 to 0.95.
- Master volume: limited to avoid clipping, with dB scaling preferred.

# Audio Requirements

## Audio Device Configuration

The standalone app shall expose audio settings through a JUCE audio device selector.

The user shall be able to select:

- Audio backend/device type.
- Output device.
- Sample rate.
- Buffer size.

The app should display the current sample rate and buffer size.

The app should handle sample-rate changes correctly.

The app should handle audio-device restarts without crashing.

For VST3, audio device configuration is handled by the host DAW.

## Latency Requirements

The app should be playable with the lowest practical latency on motherboard audio.

Target practical latency:

- Try 128 or 256 sample buffers first if stable.
- Accept 512 samples if needed for stable motherboard audio.
- Do not require ASIO for the first version.

The app shall not block the audio callback.

The app shall not allocate memory in the audio callback during normal operation.

The app shall avoid locks in the audio callback.

The app shall use parameter smoothing where abrupt parameter changes can cause clicks.

# Synth Engine Requirements

## Polyphony

The synth shall support polyphonic playback.

Initial polyphony target:

- Preferred: 16 voices.
- Acceptable early implementation: 8 voices.

The voice allocator shall handle:

- Free voice selection.
- Note release.
- Voice stealing when all voices are active.
- All-notes-off behavior.

Voice stealing should prefer voices that are already released or lowest amplitude.

## Notes and Tuning

The synth shall convert MIDI note numbers to frequency using standard equal temperament tuning.

A4 shall default to 440 Hz.

A4 tuning may be made configurable later.

The synth shall support velocity-sensitive note triggering.

Velocity should initially affect both:

- Voice amplitude.
- Filter behavior, such as cutoff modulation or envelope amount, if simple to implement.

If this complicates the first version, velocity shall affect amplitude first, and filter velocity influence shall be added later.

## Oscillators

The synth shall support one oscillator per voice in the first version.

Required waveforms:

- Sine.
- Square.
- Sawtooth.

Optional later waveforms:

- Triangle.
- Noise.

The oscillator should use JUCE DSP functionality where practical.

The project shall not prioritize writing oscillator DSP from scratch in the first version.

The initial oscillator may accept the limitations of simple waveform generation, but should not spend significant time on advanced anti-aliasing until the rest of the synth works.

## Amplitude Envelope

Each voice shall include an ADSR amplitude envelope.

Parameters:

- Attack.
- Decay.
- Sustain.
- Release.

The envelope shall be applied per voice.

The envelope shall avoid clicks during note on/off.

The envelope shall support retriggering behavior when a voice is reused.

JUCE ADSR may be used.

Proposed ranges:

| Parameter | Range |
|---|---|
| Attack | 1 ms to 10 s |
| Decay | 1 ms to 10 s |
| Sustain | 0.0 to 1.0 |
| Release | 1 ms to 10 s |

## Filter

The synth shall include a low-pass filter.

The first implementation shall use a JUCE DSP filter rather than a custom filter.

Initial design choice:

- One filter per voice, if straightforward with the selected voice architecture.
- A global filter is acceptable temporarily if it simplifies the first implementation, but per-voice filtering is preferred for a synth-like result.

Required parameters:

- Cutoff frequency.
- Resonance.

Proposed cutoff range:

- 20 Hz to 20,000 Hz.

The cutoff control should use logarithmic scaling.

The filter should remain stable across common sample rates and cutoff/resonance settings.

## Delay Effect

The synth shall include a simple delay effect.

Initial design choice:

- Global delay effect after voice mixing.

The delay may use JUCE DSP delay functionality or a simple JUCE-based implementation.

Required parameters:

- Delay time.
- Feedback.
- Wet/dry mix.

Proposed ranges:

| Parameter | Range |
|---|---|
| Delay time | 1 ms to 2000 ms |
| Feedback | 0.0 to 0.95 |
| Mix | 0.0 to 1.0 |

The delay shall avoid runaway feedback.

The delay shall handle sample-rate changes correctly.

Delay time changes should be smoothed or handled carefully to avoid zipper noise and pitch artifacts.

## LFO and Modulation

LFO support shall not be included before the core synth works.

A later version may add:

- One global LFO.
- LFO rate.
- LFO amount.
- LFO target selection.
- Mapping from Fader 4 to LFO rate or amount.

## Master Output

The synth shall include a master output gain parameter.

The app shall avoid excessive clipping where practical.

A simple output meter may be added later.

The app shall include a panic button that:

- Stops all active voices.
- Clears stuck notes.
- Optionally resets delay buffers.

# UI Requirements

## General UI Requirements

The GUI shall be built using JUCE components.

The UI shall be more hardware-synth-like than a generic desktop form.

The UI shall be visually inspired by the MiniLab 3 layout where practical:

- Keyboard/controller status area.
- Top-row knob-like controls.
- Fader-like controls where appropriate.
- Pad-like buttons for feature actions.
- Clear grouping into oscillator, filter, envelope, delay, and master sections.

The UI shall still prioritize clarity and maintainability over heavy custom graphics.

The UI shall show the current state of core synth parameters.

The UI shall update when parameters change from hardware MIDI controls.

The UI shall remain responsive during audio playback.

## Main UI Sections

The main window should contain the following sections:

- Device/status strip.
- Oscillator section.
- Filter section.
- Envelope section.
- Delay section.
- Master/output section.
- Pad/function section.
- MIDI monitor, initially visible or available in a collapsible debug area.
- Panic button.

Proposed rough layout:

```text
+----------------------------------------------------------------+
| MiniLab Synth              MIDI: MiniLab 3     Audio: WASAPI   |
+----------------------------------------------------------------+
| [ Oscillator ] [ Filter ] [ Envelope ] [ Delay ] [ Master ]    |
|   Knob 1       Knob 2     Knob 4      Knob 8    Fader 1       |
|                Knob 3     Knob 5      Fader 2                 |
|                           Knob 6      Fader 3                 |
|                           Knob 7                              |
+----------------------------------------------------------------+
| [ Pad 1 ] [ Pad 2 ] [ Pad 3 ] [ Pad 4 ] [ Pad 5 ] [ Panic ]   |
+----------------------------------------------------------------+
| MIDI Monitor / Debug                                           |
+----------------------------------------------------------------+
```

## Controls

The UI shall use a mix of control types inspired by the MiniLab 3:

| Parameter | UI Control |
|---|---|
| Waveform | Pad buttons, combo box, or segmented selector |
| Cutoff | Rotary slider |
| Resonance | Rotary slider |
| Attack | Rotary slider |
| Decay | Rotary slider |
| Sustain | Rotary slider |
| Release | Rotary slider |
| Delay time | Vertical fader or rotary slider |
| Delay feedback | Vertical fader or rotary slider |
| Delay mix | Rotary slider |
| Master volume | Vertical fader |
| Panic | Pad-like button |
| MIDI learn | Hidden initially; later button per parameter or context action |

The UI should display exact values where this helps learning and debugging.

At minimum, these should show meaningful units:

- Cutoff in Hz/kHz.
- Delay time in ms.
- ADSR times in ms/s.
- Sustain as percentage.
- Master volume in dB or normalized value.

## Virtual Keyboard

A virtual on-screen keyboard is optional.

It is not required in the first version because the MiniLab 3 is the primary input device.

A small note activity display may be more useful than a full virtual keyboard in early versions.

# State and Preset Requirements

## Parameter Model

The synth shall use a central parameter model suitable for both standalone and VST3.

Recommended approach:

- `juce::AudioProcessorValueTreeState` for plugin-compatible parameters.
- Parameter attachments for UI controls.
- Safe parameter reading in the audio processor.

The parameter model shall define:

- Parameter ID.
- Display name.
- Range.
- Default value.
- Unit/display conversion.

## Initial State

The app shall start with a safe, audible initial patch.

Proposed initial patch:

- Waveform: sawtooth or sine.
- Attack: 10 ms.
- Decay: 200 ms.
- Sustain: 0.8.
- Release: 300 ms.
- Filter cutoff: 10 kHz.
- Resonance: low.
- Delay mix: 0.
- Master volume: moderate.

## Presets

Preset management is optional for the first version, but the internal parameter model should not prevent it.

A later version should support:

- Init patch.
- Save preset.
- Load preset.
- Preset file stored as JUCE ValueTree or XML/JSON.
- MIDI mappings saved with or separate from presets.

Recommended distinction:

- Synth preset: sound design parameters.
- Controller mapping: hardware CC assignments.

# Persistence Requirements

The app should remember basic application settings:

- Last selected MIDI device.
- Last selected audio device configuration, where possible.
- Last window size and position, optional.
- MIDI mappings, once MIDI learn is implemented.
- Last loaded patch, optional.

Persistence may use JUCE `ApplicationProperties`, ValueTree state, or another JUCE-friendly configuration approach.

# Real-Time Safety Requirements

The audio callback shall not:

- Perform heap allocations during normal processing.
- Open files.
- Write logs.
- Update UI directly.
- Block on mutexes.
- Wait on condition variables.
- Perform expensive string operations.
- Call slow system APIs.

Communication from UI/MIDI threads to the audio thread shall use safe mechanisms such as:

- JUCE parameter values.
- Atomic parameter access.
- Lock-free queues if needed.
- Preallocated buffers.

Parameter changes that affect sound should be smoothed where necessary.

# Error Handling Requirements

The app shall handle these conditions gracefully:

- No MIDI device connected.
- MiniLab 3 disconnected while the app is running.
- Audio device unavailable.
- Audio device changes sample rate.
- Invalid or unsupported MIDI message.
- MIDI mapping conflict.
- Preset/config file missing or malformed.
- VST3 loaded without MIDI input from host.

The app should show clear status messages rather than crashing.

# Testing Requirements

## Manual Test Cases

The following manual tests should be possible:

- Launch app with no MIDI controller connected.
- Launch app with MiniLab 3 connected.
- Select MiniLab 3 as MIDI input.
- Press each key and verify note messages appear in the monitor.
- Press keys and hear sound.
- Release keys and verify sound stops cleanly.
- Move each knob/fader and verify CC messages appear.
- Verify fixed mapped controls change parameters.
- Play multiple notes at once.
- Exceed voice count and verify voice stealing works.
- Change audio buffer size and verify continued playback.
- Press panic and verify stuck notes stop.
- Disconnect MIDI controller and verify app does not crash.
- Build the VST3 target.
- Load the VST3 in a DAW later and verify MIDI note input produces sound.

## Automated Unit Tests

Unit tests are not required at the beginning.

The code should still be structured so unit tests can be added later for:

- MIDI note-to-frequency conversion.
- MIDI CC scaling to parameter values.
- Voice allocation and stealing logic.
- Preset serialization/deserialization.
- Parameter range conversion.

# Build and Repository Requirements

## Build System

The project shall use CMake.

The repository should build without requiring the JUCE Projucer.

Recommended approach:

- Use CMake FetchContent for JUCE initially.

Rationale:

- Easier for a VS Code workflow.
- Less manual setup than requiring a local JUCE path.
- Avoids managing a Git submodule in the first project stage.

If reproducibility becomes a concern, JUCE can later be pinned to a specific tag or changed to a Git submodule.

The CMake project shall generate:

- A standalone application target.
- A VST3 plugin target, if practical from the start.

## Repository Structure

A modular structure is preferred from the start.

Proposed structure:

```text
minilab-synth/
  CMakeLists.txt
  README.md
  docs/
    requirements.md
  assets/
  src/
    processor/
      SynthAudioProcessor.h
      SynthAudioProcessor.cpp
      SynthAudioProcessorEditor.h
      SynthAudioProcessorEditor.cpp
    synth/
      SynthEngine.h
      SynthEngine.cpp
      SynthVoice.h
      SynthVoice.cpp
      SynthSound.h
      SynthSound.cpp
    midi/
      MidiMapper.h
      MidiMapper.cpp
      Minilab3Profile.h
      Minilab3Profile.cpp
      MidiMonitor.h
      MidiMonitor.cpp
    parameters/
      ParameterIDs.h
      ParameterLayout.h
      ParameterLayout.cpp
    ui/
      MainSynthView.h
      MainSynthView.cpp
      KnobControl.h
      KnobControl.cpp
      PadButton.h
      PadButton.cpp
      MeterView.h
      MeterView.cpp
    util/
      NoteUtils.h
      ValueFormatters.h
```

The exact structure may be simplified during implementation if it becomes too heavy, but the separation between processor, synth, MIDI, parameters, and UI should remain.

# Code Quality Requirements

The code shall:

- Use modern C++20 where appropriate.
- Avoid unnecessary inheritance outside JUCE requirements.
- Keep DSP code separate from UI code.
- Keep MIDI mapping separate from synth voice logic.
- Keep MiniLab-specific assumptions isolated in a profile file.
- Prefer explicit parameter ranges and units.
- Use clear names rather than clever abstractions.
- Avoid global mutable state.
- Minimize audio-thread risk.
- Compile cleanly on Windows 11.
- Support a VS Code plus CMake workflow.

Warnings should be treated seriously.

# Milestones

## Milestone 1: Build Skeleton

Deliverables:

- JUCE CMake project builds on Windows 11.
- VS Code workflow works.
- Standalone target builds.
- VST3 target builds if practical.
- Standalone window opens.
- Basic parameter model exists.
- Basic app status is shown.

Acceptance criteria:

- App launches without crashing.
- Project builds from CMake.
- Processor/editor structure is in place.

## Milestone 2: Audio and Device Setup

Deliverables:

- Audio device selector in standalone app.
- Current audio backend, output device, sample rate, and buffer size visible.
- WASAPI output tested on motherboard audio.

Acceptance criteria:

- App can produce test audio or silence reliably through selected output.
- User can change buffer size/sample rate where supported.

## Milestone 3: MIDI Monitor

Deliverables:

- MIDI input device selector for standalone app.
- MIDI monitor panel.
- MiniLab 3 events visible in the app.

Acceptance criteria:

- Pressing keys shows note events.
- Moving knobs/faders shows CC events.
- Unsupported events do not crash the app.

## Milestone 4: Basic Polyphonic Synth

Deliverables:

- One oscillator per voice.
- Sine waveform initially.
- 8 to 16 voice polyphony.
- Note on/off handling.
- Basic amplitude envelope.
- Panic button.

Acceptance criteria:

- Pressing MiniLab keys produces audible tones.
- Releasing keys stops tones cleanly.
- Multiple notes can be played simultaneously.
- Panic button stops sound.

## Milestone 5: Hardware-Style UI

Deliverables:

- Hardware-synth-like layout.
- Rotary controls for oscillator/filter/envelope parameters.
- Fader-style controls for master and delay-related values.
- Pad-like buttons for feature controls.

Acceptance criteria:

- UI resembles a simple controller/synth panel rather than a generic form.
- Parameter controls update the synth.
- UI remains responsive during playback.

## Milestone 6: Oscillator Waveforms

Deliverables:

- Sine, square, and sawtooth waveforms.
- UI control for waveform.
- Fixed MiniLab mapping for waveform selection/change.

Acceptance criteria:

- User can switch waveform from UI.
- Hardware control or pad can change waveform if mapping is confirmed.

## Milestone 7: ADSR and Velocity

Deliverables:

- Attack, decay, sustain, release controls.
- Velocity affects amplitude.
- Optional velocity influence on filter if simple.

Acceptance criteria:

- Envelope changes are audible.
- Release phase works correctly.
- Velocity changes are audible.

## Milestone 8: Filter

Deliverables:

- Low-pass filter using JUCE DSP.
- Cutoff and resonance controls.
- Fixed MiniLab mapping for cutoff and resonance.

Acceptance criteria:

- Cutoff changes are audible.
- Resonance changes are audible.
- Filter remains stable across tested settings.

## Milestone 9: Delay

Deliverables:

- Global delay effect.
- Delay time, feedback, and mix controls.
- Safe feedback limiting.

Acceptance criteria:

- Delay effect is audible.
- Feedback does not run away uncontrollably.
- Delay can be bypassed or mixed out.

## Milestone 10: Fixed MiniLab 3 Profile

Deliverables:

- Confirmed default MiniLab 3 CC map.
- Encoded MiniLab 3 profile.
- Pad feature assignments decided and implemented.

Acceptance criteria:

- Default MiniLab controls operate the intended synth parameters.
- The profile is isolated from generic synth code.

## Milestone 11: VST3 Smoke Test

Deliverables:

- VST3 target builds.
- Plugin loads in at least one DAW or plugin host.
- MIDI note input from host triggers sound.
- UI opens in host.

Acceptance criteria:

- VST3 can be loaded and played.
- Shared processor behaves similarly to standalone version.

## Milestone 12: MIDI Learn and Persistence

Deliverables:

- MIDI learn mode.
- Mappings stored between runs.
- Configurable default mappings.

Acceptance criteria:

- User can map a MiniLab control to a parameter.
- Mapping remains after app restart.

## Milestone 13: Presets

Deliverables:

- Save and load synth patches.
- Init patch.
- Basic preset selector.

Acceptance criteria:

- User can save a sound.
- User can reload it later.
- Preset loading updates UI and sound engine.

## Milestone 14: CI

Deliverables:

- GitHub Actions workflow or equivalent CI.
- Windows build verification.

Acceptance criteria:

- Repository builds automatically on commit or pull request.

# Initial Technical Decisions

The following decisions are currently accepted:

| Area | Decision |
|---|---|
| Language | C++20 |
| Framework | JUCE |
| Build system | CMake |
| Editor | Visual Studio Code |
| Compiler/toolchain | MSVC via Visual Studio 2022 Build Tools or Visual Studio 2022 |
| First usable target | Standalone app |
| Architecture target | Standalone plus VST3 from day one |
| UI | JUCE Components, hardware-synth-like layout |
| MIDI | JUCE MIDI input for standalone, host MIDI for VST3 |
| Audio | JUCE audio device system |
| Primary Windows backend | WASAPI on motherboard audio |
| Oscillator | JUCE DSP/simple JUCE-supported implementation |
| Envelope | JUCE ADSR or JUCE-compatible implementation |
| Filter | JUCE DSP filter |
| Delay | JUCE DSP delay or simple JUCE-based delay |
| Presets | ValueTree/XML/JSON later |
| MiniLab mapping | Fixed default profile first, MIDI learn later |
| Unit tests | Not initially |
| CI | Later |
| Code layout | Modular from the start |

# Resolved User Decisions

The following decisions have been made:

- Scope should support both standalone and VST3 from day one.
- The project should be both educational and practically playable.
- DSP should not be implemented from scratch initially.
- Simplicity should be prioritized.
- No external audio interface is currently available.
- Audio output is motherboard line-out to active speakers.
- Latency should be as small as practical on that hardware.
- MiniLab 3 default template should be assumed initially.
- Fixed MiniLab mapping should come first.
- MIDI learn should come later.
- Pads should be used for features rather than drums initially.
- One oscillator per voice should be used initially.
- LFO/modulation should wait until the core synth works.
- UI should be more hardware-synth-like and modeled loosely on the MiniLab 3 layout.
- Controls should use a mix similar to the MiniLab 3, including knobs, faders, and pads.
- Visual Studio Code should be the editor.
- The assistant may decide the JUCE dependency strategy.
- A modular architecture should be used from the start.
- Unit tests are not required initially.
- CI can be added later.
- Complete code should be provided at each implementation milestone.
- DSP math explanations are not required unless needed.
- Code should be designed cleanly from the start.

# Remaining Design Defaults

Some items are not yet fully decided. The following defaults shall be used unless changed:

| Question | Default |
|---|---|
| MiniLab pads | Feature toggles and shortcuts |
| Main encoder | Reserved initially |
| Velocity routing | Amplitude first, filter influence later if simple |
| Filter placement | Per voice preferred; global acceptable temporarily |
| Delay channel mode | Stereo global delay preferred |
| MIDI monitor | Visible in early builds, collapsible later |
| Exact value display | Show exact values for learning/debugging |
| Virtual keyboard | Not required initially |

# Next Implementation Step

The next step is to create the initial repository skeleton:

- CMake project.
- JUCE fetched through CMake.
- Standalone target.
- VST3 target if practical.
- `SynthAudioProcessor` and editor classes.
- Basic hardware-style placeholder UI.
- No sound engine yet, unless a minimal test tone is useful for verifying audio output.

