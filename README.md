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

**Phase 3: Standalone MIDI Shell and Monitor** is complete.

- The standalone app now exposes one-device MIDI input selection.
- The last selected MIDI input is remembered by device identifier when available.
- Missing remembered devices show an unavailable state instead of silently falling back.
- The standalone app now includes a bounded MIDI monitor for note and CC bring-up.
- Unsupported MIDI message types are ignored safely.
