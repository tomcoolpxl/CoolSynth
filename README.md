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

**Phase 5: Parameter-Driven Core Synth UI** is complete.

- The editor now features a hardware-style layout with dedicated sections for Oscillator, Envelope, and Output.
- Added waveform support for sine, square, and saw, selectable via the UI.
- All controls (waveform, ADSR, master gain) are linked to the APVTS parameter model via thread-safe attachments.
- Real-time value displays show meaningful units (ms, s, %, dB) driven directly by parameter metadata.
- Both standalone and VST3 targets build and function correctly with the new shared control surface.
