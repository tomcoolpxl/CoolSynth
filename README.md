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

**Phase 12: Standalone Device Persistence** is complete.

- Persisted the last valid standalone audio backend, output device, sample rate, and buffer size.
- Persisted the last valid standalone MIDI input selection.
- Restored persisted standalone device settings when still available.
- Supported and displayed missing remembered devices gracefully.

## Standalone Persistence

CoolSynth remembers these standalone-only settings between runs:

- audio backend
- output device
- sample rate
- buffer size
- one selected MIDI input device

If a remembered audio or MIDI device is unavailable at startup, the app stays open and shows the unavailable state in the standalone UI.

CoolSynth does not currently persist:

- window size or position
- preset files
- recent files
- MIDI monitor UI state
