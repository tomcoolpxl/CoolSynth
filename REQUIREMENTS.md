# Requirements Document: CoolSynth

## 1. Purpose

This document defines the product and implementation requirements for CoolSynth, a learning-oriented software synthesizer for Windows 11 built with JUCE, C++20, and CMake.

The project has two product targets from day one:

- Standalone desktop application.
- VST3 plugin.

The first practical target is the standalone application. It is the primary environment for bringing up audio, MIDI, controller mapping, and the first playable synth behavior. The architecture shall still be shared from the start so the same processor and synth engine can later run as a VST3 plugin without a redesign.

The project is intended to be educational, cleanly structured, and practically playable. Simplicity and correct behavior take priority over feature breadth.

## 2. Fixed Project Constraints

The following decisions are fixed and are not open for redesign in the first planning cycle:

- Operating system: Windows 11, 64-bit.
- Framework: JUCE.
- Language: C++20.
- Build system: CMake.
- Editor workflow: Visual Studio Code.
- Compiler and SDK: MSVC via Visual Studio 2022 Build Tools or Visual Studio 2022.
- Product targets from day one: Standalone and VST3.
- First runtime target: Standalone.
- Initial audio hardware: motherboard line-out to active speakers.
- External audio interface: not available.
- Practical first Windows backend: WASAPI shared mode.
- ASIO: optional later, not required for the first functional release.
- MIDI controller: Arturia MiniLab 3.
- Controller strategy: fixed MiniLab mapping first, MIDI learn later.
- UI direction: hardware-synth-like, loosely inspired by the MiniLab 3 layout.
- Initial synth scope: one oscillator per voice.
- Priority order: core synth first, LFO and broader modulation later.
- DSP strategy: prefer JUCE-provided DSP and infrastructure over custom DSP from scratch.
- Unit tests: not required initially.
- CI: not required initially.

## 3. Scope

### 3.1 In Scope for the First Functional Release

The first functional release shall provide:

- A JUCE-based standalone application that can produce audible synth output on Windows 11.
- A JUCE-based VST3 target built from the same shared processor codebase.
- Polyphonic note playback from MIDI input.
- One oscillator per voice with sine, square, and saw waveforms.
- A per-voice ADSR amplitude envelope.
- A per-voice low-pass filter with cutoff and resonance control.
- A global delay effect with time, feedback, and mix control.
- A fixed MiniLab 3 mapping for the core synth parameters once the controller's default messages have been confirmed on the actual device.
- A hardware-synth-like UI built with JUCE components.
- A standalone MIDI monitor for bring-up and debugging.
- A panic or all-notes-off control.
- Real-time-safe audio behavior appropriate for JUCE audio callbacks.

### 3.2 Explicitly Deferred

The following features are out of scope for the first functional release and shall not drive the initial architecture:

- Custom oscillator DSP and custom anti-aliased oscillator design.
- Wavetable, FM, granular, and sample-based synthesis.
- MPE, aftertouch handling, and advanced expressive MIDI support.
- MIDI learn.
- Preset browser and commercial-grade preset management.
- Modulation matrix.
- LFO.
- AU and AAX targets.
- Advanced host automation behavior beyond exposing stable plugin parameters.
- ASIO-specific optimization work.
- Oversampling and advanced anti-aliasing.
- Multi-controller management.
- Multiple simultaneous MIDI input devices in standalone mode.
- Cloud, network, or online features.
- Unit test suite.
- CI pipeline.

Pitch bend support is also deferred until the core synth, fixed mapping, and VST3 smoke test are stable.

## 4. Target Separation

This project must separate shared synth behavior from standalone-specific and VST3-specific responsibilities.

| Area | Shared Core | Standalone | VST3 |
| --- | --- | --- | --- |
| Audio rendering | Required | Uses shared core | Uses shared core |
| Parameter model | Required | Uses shared core | Uses shared core |
| Synth engine and voices | Required | Uses shared core | Uses shared core |
| Fixed MiniLab mapping logic | Required | Consumes hardware MIDI | Consumes host-provided MIDI if present |
| Audio device selection | Not part of shared core | Required | Not applicable |
| MIDI device selection | Not part of shared core | Required | Not applicable |
| MIDI monitor UI | Optional event capture support only | Required in early milestones | Not required |
| Host automation | Not part of standalone shell | Not applicable | Required |
| Host state save and restore | Shared processor responsibility | Used indirectly | Required |
| Standalone app settings persistence | Not part of shared core | Required later | Not applicable |

Additional separation rules:

- The plugin shall not expose audio-device configuration UI.
- The plugin shall not expose a hardware MIDI device selector.
- Standalone-only status panels may include audio and MIDI device state.
- Shared UI components may be reused, but the standalone shell and plugin editor wrapper do not need to be identical.
- The VST3 target must share the same sound engine and parameter definitions as the standalone target.

## 5. Shared Architecture Requirements

- A single JUCE AudioProcessor implementation shall own the synth parameter model, MIDI processing, synth engine, global effects, and state serialization.
- The build shall generate both Standalone and VST3 outputs from the same shared processor codebase.
- All sound-shaping controls that need UI access or DAW automation shall be represented as stable parameters from day one.
- Debug state, device-selection state, and monitor visibility are not synth parameters.
- The project shall use JUCE's parameter infrastructure, with AudioProcessorValueTreeState or an equivalent JUCE-native parameter model, for all plugin-visible parameters.
- The architecture shall keep the following concerns separate:
  - Shared processor and state management.
  - Synth voice and DSP code.
  - MIDI normalization and controller mapping.
  - UI components and editor wiring.
  - Standalone shell concerns such as device selectors.
- The project shall avoid speculative framework layers or custom abstraction stacks above JUCE.
- The exact directory layout is an implementation detail. The requirement is separation of concerns, not a specific folder tree.
- UI-to-audio interaction shall occur through parameters or preallocated thread-safe messaging only.

## 6. Parameter Requirements

The first parameter set shall be intentionally small and stable.

Required automatable parameters:

| Parameter ID | Type | User-Facing Range or Values | Default | Notes |
| --- | --- | --- | --- | --- |
| waveform | Choice | sine, square, saw | saw | Exposed as a discrete choice parameter |
| ampAttackMs | Float | 1 ms to 5000 ms | 10 ms | Skewed or logarithmic control mapping |
| ampDecayMs | Float | 5 ms to 5000 ms | 200 ms | Skewed or logarithmic control mapping |
| ampSustain | Float | 0.0 to 1.0 | 0.8 | Linear |
| ampReleaseMs | Float | 5 ms to 5000 ms | 300 ms | Skewed or logarithmic control mapping |
| filterCutoffHz | Float | 20 Hz to 20000 Hz | 10000 Hz | Logarithmic mapping |
| filterResonance | Float | 0.0 to 1.0 | 0.1 | Mapped internally to a stable filter-specific range |
| delayTimeMs | Float | 1 ms to 1000 ms | 250 ms | Control-rate parameter, not an audio-rate modulation target |
| delayFeedback | Float | 0.0 to 0.85 | 0.25 | Hard-clamped to a safe maximum |
| delayMix | Float | 0.0 to 1.0 | 0.0 | 0 means effectively bypassed |
| masterGainDb | Float | -60 dB to 0 dB | -12 dB | Final output gain |

Parameter rules:

- Parameter IDs shall be treated as stable once VST3 smoke testing begins.
- Waveform shall be a discrete choice parameter, not a free-running float interpreted ad hoc.
- Panic is an action, not an automatable parameter.
- Audio device settings are not plugin parameters.
- MIDI monitor state is not a plugin parameter.
- The first version shall not add separate filter-enable or delay-enable parameters unless a clear functional need appears. High cutoff and zero delay mix are sufficient for the first version.

## 7. Audio Requirements

### 7.1 Standalone Audio Configuration

The standalone application shall:

- Expose a JUCE audio-device configuration panel or an equivalent JUCE-native control surface.
- Allow the user to select:
  - Audio backend or device type.
  - Output device.
  - Sample rate.
  - Buffer size.
- Display the currently active backend, output device, sample rate, and buffer size.
- Default to WASAPI shared mode when available.
- Continue to run if no valid output device is currently available.

DirectSound may be used as a fallback backend if WASAPI is unavailable or unusable on the development machine.

ASIO support is not an acceptance requirement for the first functional release.

### 7.2 Audio Performance and Practical Latency

The project shall optimize for reliable manual playability on motherboard audio, not for benchmark latency numbers.

The practical acceptance target is:

- Stable playback at 44.1 kHz or 48 kHz.
- Stable playback at 256-sample or 512-sample buffers on the development machine.

128-sample buffers are desirable if stable, but they are not a release requirement.

The application shall handle sample-rate changes and device restarts without crashing.

## 8. MIDI Requirements

### 8.1 Standalone MIDI Device Handling

To keep the first version simple, the standalone app shall support one active MIDI input device at a time.

The standalone app shall:

- List available MIDI input devices.
- Allow the user to select one active MIDI input device.
- Remember the last selected MIDI input device between runs when the stored device is still available.
- Show a disconnected or unavailable state if the remembered device is missing at startup.
- Remain running if the selected device is unplugged during use.
- Clear held-note state when the selected MIDI device is disconnected during playback.

### 8.2 Supported MIDI Messages for the First Functional Release

The first functional release shall support:

- Note on.
- Note off.
- Note-on with velocity zero treated as note-off.
- Velocity.
- Control Change messages.

The first functional release shall not require support for:

- Pitch bend.
- Channel aftertouch.
- Poly aftertouch.
- Program change.

Unsupported or deferred MIDI messages shall be ignored safely.

To reduce early complexity, the first functional release shall accept notes and CCs on any MIDI channel. User-selectable channel filtering is deferred.

### 8.3 MIDI Monitor

The standalone application shall include a MIDI monitor during early development and the fixed-mapping milestone.

The MIDI monitor shall display, for each captured event:

- Event order or timestamp.
- Message type.
- MIDI channel.
- Primary data value.
- Secondary data value.
- Note name for note events.
- CC number for control-change events.

The monitor shall keep a bounded recent-history buffer. A fixed-size ring buffer is sufficient.

The plugin editor does not need a MIDI monitor.

### 8.4 MiniLab 3 Fixed Mapping

The first controller strategy is a fixed MiniLab 3 mapping.

The mapping milestone is not complete until the actual MiniLab 3 default template messages used on the developer's device have been captured with the MIDI monitor and recorded in code or documentation.

The preferred first fixed mapping is:

| MiniLab 3 Control | Target | Status for First Release |
| --- | --- | --- |
| Keyboard | Note on and note off | Required |
| Velocity | Voice amplitude | Required |
| Knob 1 | Waveform selection | Required |
| Knob 2 | Filter cutoff | Required |
| Knob 3 | Filter resonance | Required |
| Knob 4 | Amp attack | Required |
| Knob 5 | Amp decay | Required |
| Knob 6 | Amp sustain | Required |
| Knob 7 | Amp release | Required |
| Knob 8 | Delay mix | Required after delay milestone |
| Fader 1 | Master gain | Required |
| Fader 2 | Delay feedback | Required after delay milestone |
| Fader 3 | Delay time | Required after delay milestone |
| Fader 4 | Unassigned | Deferred |
| Pads | Optional fixed assignments after actual pad message behavior is confirmed | Deferred |
| Main encoder | Unassigned | Deferred |

Pad mapping rules:

- Pads shall not block the first playable synth milestone.
- If the MiniLab 3 default pads emit simple note or CC messages on the developer's device, the preferred first pad assignments are waveform direct-select and panic.
- If the default pad behavior is not simple or reliable, pad mapping remains deferred rather than forcing controller-specific workarounds into the core synth architecture.

Mapping rules:

- MiniLab-specific CC and note constants shall be isolated from generic synth code.
- The synth engine and voice code shall not contain controller-specific message numbers.
- Velocity shall affect amplitude only in the first functional release.
- Velocity-to-filter modulation is deferred.

### 8.5 MIDI Learn

MIDI learn is a later milestone.

When implemented, MIDI learn shall:

- Bind only CC messages to continuous parameters.
- Not bind note-on or note-off events to continuous parameters.
- Allow clearing an existing mapping.
- Persist mappings between runs.
- Store mappings separately from synth parameter values.

## 9. Synth Engine Requirements

### 9.1 Voice Model

- The synth shall be polyphonic.
- The first playable implementation shall preallocate 8 voices.
- Each voice shall contain:
  - One oscillator.
  - One ADSR amplitude envelope.
  - One low-pass filter.
- Delay and master gain shall be global post-voice stages.
- MIDI note numbers shall be converted to frequency using equal temperament with A4 = 440 Hz.
- Velocity shall affect amplitude only in the first functional release.

Voice-allocation rules:

- Voice count shall remain fixed during playback.
- Voice stealing shall prefer a voice that is already in release.
- If no released voice is available, the oldest active voice shall be stolen.
- Panic shall clear active voices and held-note state immediately.

### 9.2 Oscillator

- The first functional release shall use one oscillator per voice.
- Required waveforms are:
  - Sine.
  - Square.
  - Saw.
- Triangle and noise are deferred.
- JUCE oscillator support is preferred.
- The first release may accept the aliasing limitations of simple waveform generation.
- Advanced band-limiting work is explicitly deferred.

### 9.3 Amplitude Envelope

- Each voice shall use a per-voice ADSR amplitude envelope.
- Envelope parameters shall correspond to the parameter table in Section 6.
- Reusing a voice for a new note shall retrigger the envelope correctly.
- Note release shall stop sound without obvious clicks under normal manual use.

### 9.4 Filter

- The synth shall use a per-voice low-pass filter.
- A temporary global filter is not an acceptable substitute for the first functional release because it changes the intended synth architecture and note behavior.
- The first filter implementation shall use JUCE DSP functionality.
- Required user-facing controls are cutoff and resonance.
- The user-facing resonance parameter may be normalized as long as the mapping remains stable and musically usable.
- The filter implementation shall remain stable at the supported sample rates and parameter ranges.

### 9.5 Delay

- The delay effect shall be global and applied after voice mixing.
- Delay parameters shall match the parameter table in Section 6.
- Feedback shall be hard-limited to the allowed maximum.
- Delay mix at 0.0 shall behave as effectively bypassed.
- Delay-time changes in the first release are treated as user control changes, not modulation targets.
- Seamless, artifact-free delay-time modulation is not required for the first release.
- Manual delay-time changes may produce a brief artifact, but they shall not crash the app, allocate in the audio thread, or cause runaway feedback.

### 9.6 Master Output and Panic

- Master gain shall be the final output stage.
- The default patch shall be audible without being excessively hot.
- The panic or all-notes-off control shall silence active notes immediately.
- Clearing the residual delay tail is not required for the first release.

## 10. UI Requirements

### 10.1 General UI Direction

- The UI shall be built with JUCE components.
- The UI shall be hardware-synth-like in layout and grouping.
- The UI shall be loosely inspired by the MiniLab 3 physical layout, but it shall not attempt to be a literal visual replica.
- Early milestones may use standard JUCE sliders, buttons, and labels with light styling.
- Heavy custom graphics, photorealistic controls, and skinning systems are out of scope.
- The first version may use a fixed-size window or editor.

### 10.2 Required UI Sections

The standalone UI shall include:

- Status strip for current audio and MIDI device state.
- Oscillator section.
- Filter section.
- Envelope section.
- Delay section.
- Output section.
- Panic action.
- MIDI monitor, visible or collapsible.

The VST3 editor shall include:

- Oscillator section.
- Filter section.
- Envelope section.
- Delay section.
- Output section.
- Panic action.

The plugin editor does not need standalone device selectors or hardware status panels.

### 10.3 Control and Display Rules

- Every user-facing control shall have a text label.
- The following values shall be shown in meaningful units:
  - Filter cutoff in Hz or kHz.
  - Envelope times in ms or s.
  - Sustain as a percentage or normalized value.
  - Delay time in ms.
  - Delay feedback as a percentage or normalized value.
  - Master gain in dB.
- The UI shall reflect parameter changes originating from hardware MIDI input.
- The VST3 editor shall reflect parameter changes originating from host automation.
- Color shall not be the only way to convey state.
- A full virtual keyboard is not required.
- A small activity indicator for incoming notes is optional.

## 11. State and Persistence Requirements

State handling must be separated by responsibility.

Shared synth state:

- All automatable synth parameters shall serialize through the AudioProcessor state mechanism.
- This is required before the VST3 smoke test milestone.
- VST3 save and restore shall restore all parameters in Section 6.

Standalone-only persisted settings:

- Last selected audio backend, device, sample rate, and buffer size, when the stored configuration is still valid.
- Last selected MIDI input device, when the stored device is still available.

Deferred standalone persistence:

- Window size and position.
- Last loaded patch.
- Recent files.

Preset rules:

- Preset file management is not required for the first functional release.
- If an Init Patch action exists before file-based presets, it shall simply reset all automatable parameters to their default values.
- Later preset files shall not implicitly overwrite standalone audio-device or MIDI-device selection.

## 12. Real-Time Audio Safety Requirements

The following are hard requirements for the audio thread and audio callback path:

- No heap allocation during normal playback in processBlock, voice rendering, or per-sample DSP code.
- No mutex locking.
- No condition-variable waits.
- No file I/O.
- No device enumeration.
- No logging.
- No expensive string formatting.
- No direct UI updates.

Additional real-time rules:

- Voices, filters, delay buffers, temporary processing buffers, and MIDI-monitor storage shall be preallocated during initialization or prepareToPlay.
- MIDI monitor collection shall store lightweight event records only. String formatting for the monitor shall happen on the message thread.
- UI code shall not mutate live voice objects directly.
- Audio-thread parameter reads shall use JUCE parameter APIs or atomics without requiring locks.
- Continuous parameters that can click when jumped, including master gain and filter cutoff, shall use smoothing or another safe control-rate strategy.
- Delay-time changes do not need seamless modulation behavior in the first release, but they must remain real-time safe.
- The audio callback shall not depend on device-selection or persistence code.
- Debug logging, if present elsewhere, shall be disabled from the audio thread.

## 13. Error Handling Requirements

The application shall fail softly in the following cases:

- No audio device available at startup.
- No MIDI device available at startup.
- Remembered MIDI device is missing.
- MIDI device is disconnected during playback.
- Audio device restarts or changes sample rate.
- Unsupported MIDI messages are received.
- Standalone settings file is missing or malformed.
- VST3 plugin is opened without incoming MIDI from the host.

Expected behavior:

- The app or plugin remains running.
- The user gets a clear status indication where a standalone status panel exists.
- Invalid settings fall back to safe defaults.
- The audio thread does not attempt recovery by doing heavy work directly inside the callback.

## 14. Build and Tooling Requirements

- The project shall configure and build on Windows 11 with CMake and MSVC.
- The project shall not depend on JUCE Projucer.
- The JUCE dependency acquisition method is an implementation detail, but it must be reproducible from a clean checkout and documented in the repository.
- Visual Studio Code with a CMake-based workflow shall be a supported development path.
- The build shall generate Standalone and VST3 outputs from the shared processor codebase.
- The codebase shall keep processor, synth, MIDI mapping, parameter, and UI concerns separated enough for independent review.
- New warnings introduced by current work shall be treated as defects.

## 15. Manual Validation Requirements

The following manual tests shall be possible by the time the relevant milestone is complete:

| Scenario | Expected Result |
| --- | --- |
| Launch standalone app with no MIDI controller connected | App opens, shows disconnected MIDI state, remains usable |
| Launch standalone app with no usable audio device | App opens, shows audio problem state, does not crash |
| Select MiniLab 3 as MIDI input | App reports MiniLab 3 as active input |
| Press MiniLab keys | MIDI monitor shows note events and audible notes are produced once the synth milestone is complete |
| Move MiniLab knobs and faders | MIDI monitor shows CC events |
| Play a 4-note chord | Notes sound simultaneously without stealing below the configured voice limit |
| Exceed available voices | Voice stealing occurs according to the documented policy |
| Change sample rate or buffer size | Audio engine reprepares and app remains stable |
| Press panic | Active notes stop immediately |
| Unplug MiniLab during playback | App remains running and clears held-note state |
| Build the VST3 target | VST3 binary is produced |
| Load the VST3 target in a host | Plugin loads, opens editor, and accepts host MIDI once the VST3 smoke milestone is reached |

## 16. Milestones

### Milestone 1: Build Skeleton

Deliverables:

- JUCE CMake project configures on Windows 11 with MSVC.
- Shared processor target exists.
- Standalone target builds.
- VST3 target builds.
- Placeholder editor or window opens.
- Initial parameter layout exists.

Acceptance criteria:

- A Debug build succeeds through the CMake workflow used in Visual Studio Code.
- Launching the standalone target opens a window without crashing.
- The VST3 binary is generated even though it is not yet host-validated.

### Milestone 2: Standalone Audio Shell

Deliverables:

- Standalone audio-device selector.
- Current backend, output device, sample rate, and buffer size display.
- WASAPI used as the first practical backend.
- Optional test tone or silence path for audio verification.

Acceptance criteria:

- The user can select a WASAPI output device.
- The app displays the currently active backend, output device, sample rate, and buffer size.
- Changing output device, sample rate, or buffer size does not crash the app.
- The app remains open if no usable output device is available.

### Milestone 3: Standalone MIDI Shell and Monitor

Deliverables:

- Standalone MIDI input selector.
- MIDI monitor with bounded history.
- Standalone MIDI status display.

Acceptance criteria:

- Selecting the MiniLab 3 as input causes incoming note events to appear in the monitor.
- Moving MiniLab knobs or faders causes CC events to appear in the monitor.
- Unsupported MIDI messages are ignored safely.
- Disconnecting the device does not crash the app.

### Milestone 4: First Playable Synth

Deliverables:

- 8 preallocated voices.
- One sine oscillator per voice.
- ADSR amplitude envelope wired into note playback.
- Master gain.
- Panic action.

Acceptance criteria:

- Pressing MiniLab keys produces audible sine-wave notes.
- Releasing notes stops sound cleanly.
- At least four simultaneous notes can be played before voice stealing occurs.
- Panic silences active notes immediately.

### Milestone 5: Core Parameter UI

Deliverables:

- Functional UI controls for waveform, ADSR, and master gain.
- Parameter value display with meaningful units.
- Initial hardware-synth-like layout.

Acceptance criteria:

- Switching waveform changes between sine, square, and saw.
- Attack, decay, sustain, and release changes are audibly reflected.
- Master gain changes are audible and reflected in the UI.
- UI remains responsive during playback.

### Milestone 6: Fixed MiniLab 3 Mapping

Deliverables:

- Confirmed MiniLab 3 default message table for the controls used in the fixed mapping.
- Fixed mappings for the controls tied to already implemented parameters.
- Controller mapping code isolated from synth engine code.

Acceptance criteria:

- Knob 1 updates waveform selection.
- Knobs 4 through 7 update the ADSR parameters.
- Fader 1 updates master gain.
- Unmapped controls do not produce harmful behavior.
- The fixed mapping works without embedding MiniLab-specific message numbers in voice code.

### Milestone 7: Filter

Deliverables:

- Per-voice low-pass filter.
- Cutoff and resonance UI controls.
- MiniLab mapping for cutoff and resonance.

Acceptance criteria:

- Cutoff changes are clearly audible.
- Resonance changes are clearly audible.
- Knob 2 updates cutoff and Knob 3 updates resonance.
- The filter remains stable at 44.1 kHz and 48 kHz across the user-facing parameter range.

### Milestone 8: Delay

Deliverables:

- Global delay effect.
- Delay time, feedback, and mix controls.
- Delay-related MiniLab mapping for the confirmed controls.

Acceptance criteria:

- Delay mix above zero produces an audible delayed signal.
- Delay mix at zero produces effectively dry output.
- Feedback remains bounded and does not run away.
- Knob 8, Fader 2, and Fader 3 update the intended delay parameters.
- Manual delay-time changes do not crash the app.

### Milestone 9: Hardware-Style UI Refinement

Deliverables:

- Clear grouping for oscillator, filter, envelope, delay, and output sections.
- Standalone status strip.
- Collapsible or visually contained MIDI monitor.

Acceptance criteria:

- The standalone UI reads as a synth panel rather than a generic form.
- Labels are readable and values are understandable without inspecting raw parameter ranges.
- The plugin editor omits standalone-only device configuration panels.

### Milestone 10: VST3 Smoke Test

Deliverables:

- VST3 loads in at least one host or plugin host.
- Host MIDI note input produces sound.
- Editor opens in host.
- Host can automate at least cutoff and master gain.
- Plugin state save and restore works for the parameters in Section 6.

Acceptance criteria:

- The plugin loads and opens its editor in one host environment.
- Host MIDI note input produces audible sound.
- At least cutoff and master gain respond to host automation.
- Saving and reopening the host project restores the plugin parameter state.

### Milestone 11: Standalone Persistence

Deliverables:

- Standalone app remembers the last valid audio configuration.
- Standalone app remembers the last valid MIDI input selection.

Acceptance criteria:

- Restarting the app restores the last valid audio selection when available.
- Restarting the app restores the last valid MIDI input selection when available.
- Missing devices are handled gracefully and shown as unavailable rather than causing failure.

### Milestone 12: MIDI Learn

Deliverables:

- Per-parameter MIDI learn mode.
- Clear-mapping action.
- Mapping persistence.

Acceptance criteria:

- The user can enter learn mode for a parameter and bind a CC by moving a control.
- The learned mapping survives an app restart.
- Note events are not accepted as continuous-parameter mappings.

### Milestone 13: Presets

Deliverables:

- Init Patch action.
- Save patch.
- Load patch.

Acceptance criteria:

- The user can store the current synth parameter state as a patch.
- Loading a stored patch restores both sound and UI state.
- Preset loading does not overwrite standalone audio-device or MIDI-device selections.

### Milestone 14: CI

Deliverables:

- Windows CI build.

Acceptance criteria:

- The repository automatically builds the Windows target on the configured CI system.
