# TODO

## Phase 1: Reproducible JUCE build skeleton

- [x] Add the JUCE git submodule under `external/JUCE` and pin it to a known commit.
- [x] Create the top-level CMake project that builds standalone and VST3 outputs from one shared plugin target.
- [x] Add `SynthAudioProcessor` and `SynthAudioProcessorEditor` placeholder classes.
- [x] Define the initial APVTS parameter layout with the stable IDs from `REQUIREMENTS.md`.
- [x] Document the Windows configure and build workflow in `README.md` using `Visual Studio 17 2022` as the default generator.
- [x] Verify a Debug build produces a launchable standalone artifact and a VST3 artifact.

## Phase 2: Standalone audio device shell

- [x] Add standalone audio-device selection controls.
- [x] Add standalone status display for backend, output device, sample rate, and buffer size.
- [x] Default standalone startup to WASAPI shared mode when available.
- [x] Handle output-device, sample-rate, and buffer-size changes without crashing.
- [x] Keep the standalone app running when no valid output device is available.
- [x] Verify the audio shell build and manual bring-up checks.

## Phase 3: Standalone MIDI input shell and monitor

- [x] Add standalone MIDI input selection for one active device at a time.
- [x] Add standalone MIDI device status reporting.
- [x] Add a bounded MIDI monitor that shows timestamp or order, type, channel, and message values.
- [x] Ignore unsupported MIDI messages safely.
- [x] Addressed runtime disconnects and missing remembered devices.
- [x] Verify MiniLab 3 note and CC events appear in the monitor.
