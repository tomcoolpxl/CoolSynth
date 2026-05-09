# DONE

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
