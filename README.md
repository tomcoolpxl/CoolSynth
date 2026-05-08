# CoolSynth

A JUCE-based synthesizer with Standalone and VST3 targets.

## Build

### Prerequisites

- Windows 11
- Git
- CMake 3.22 or newer
- Visual Studio 2022 Build Tools or Visual Studio 2022

### Bootstrap

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

### Expected Artifacts

- `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe`
- `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3`

### Current Status

**Phase 4: Playable Sine Synth Voice Path** is complete.

- The shared synth engine is now implemented with a fixed 8-voice pool.
- Audible sine-wave rendering is driven from incoming MIDI note events.
- Per-voice ADSR and velocity-to-amplitude scaling are active.
- A custom voice-stealing policy (release-first, then oldest-active) is enforced.
- A global panic action is available in the UI and automatically triggers on MIDI device disconnect.
- Both Standalone and VST3 targets remain valid and build successfully.
