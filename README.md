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

**Phase 10: Hardware-style UI refinement** is complete.

- Refined the editor into grouped oscillator, filter, envelope, delay, output, and global action sections.
- Moved standalone audio and MIDI utility controls into one dedicated settings dialog.
- Replaced the large standalone status panel with a compact bottom status bar.
- Added live last-MIDI-event status text to the standalone status bar.
- Removed redundant standalone audio or MIDI settings entry points.
- Ensured the plugin editor omits standalone-only device, settings, status, and monitor UI.
- Improved control labels and value readability.
