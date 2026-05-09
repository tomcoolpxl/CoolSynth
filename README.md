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

**Phase 9: Global delay slice** is complete.

- Added a shared global delay effect stage after voice mixing.
- Wired delay time, feedback, and mix to the existing APVTS parameters.
- Added dedicated "Delay" section to the UI with attached knobs.
- Extended fixed MiniLab 3 mapping: Knob 8 to Delay Mix, Fader 2 to Delay Feedback, Fader 3 to Delay Time.
- Hard-clamped feedback to 0.85 for stability and safety.
- Verified manual delay-time changes remain stable and real-time safe (via build validation).
- Adjusted editor layout and default dimensions to accommodate the new controls.
