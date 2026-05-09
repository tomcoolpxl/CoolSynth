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

**Phase 13: MIDI learn workflow** is complete.

- Implemented per-parameter MIDI learn mode for continuous controls in the standalone app.
- Added visual states for armed and mapped controls via a context menu.
- Persisted learned MIDI mappings separately from the synth parameter state.
- Maintained precedence between learned CCs and the default MiniLab mappings.

## Standalone Persistence

CoolSynth remembers these standalone-only settings between runs:

- audio backend
- output device
- sample rate
- buffer size
- one selected MIDI input device
- custom MIDI CC learned mappings

If a remembered audio or MIDI device is unavailable at startup, the app stays open and shows the unavailable state in the standalone UI.

## MIDI Learn

In standalone mode, right-clicking on any continuous control (like knobs and faders) will open a context menu with options to:
- **Learn MIDI CC:** Click this, then move a knob/fader on your MIDI controller to map it.
- **Cancel MIDI Learn:** If you changed your mind while the parameter is armed (highlighted yellow).
- **Clear MIDI CC Mapping:** To restore the default mapping behavior for this parameter.

*Note: Learned mappings only apply in the Standalone application and are not shared with the VST3 plugin.*

CoolSynth does not currently persist:

- window size or position
- preset files
- recent files
- MIDI monitor UI state
