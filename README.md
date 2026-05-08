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

**Phase 2: Standalone Audio Device Shell** is complete.

- The standalone app now includes an audio-device status panel.
- It prefers **WASAPI (Windows Audio)** shared mode on first launch.
- You can configure the output device, sample rate, and buffer size via the **Audio Settings...** button.
- The app handles device changes and missing output devices gracefully.
- The VST3 target remains a silent placeholder for now.
