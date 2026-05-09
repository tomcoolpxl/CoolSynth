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

## CI/CD

GitHub automation is intentionally limited to manual validation and release tags.

- Manual validation workflow: `Windows Manual Validation`
- Workflow file: `.github/workflows/windows-manual-validation.yml`
- Trigger: `workflow_dispatch` only
- Branch pushes do not run CI automatically
- Release workflow file: `.github/workflows/windows-release.yml`
- Release trigger tags: `v*.*.*` and `v*.*.*-*`
- Public Windows release assets:
  - `CoolSynth-windows-x64-standalone-<tag>.zip`
  - `CoolSynth-windows-x64-vst3-<tag>.zip`
  - `CoolSynth-windows-x64-sha256-<tag>.txt`

Manual validation can be started from the GitHub Actions UI or with:

```powershell
gh workflow run windows-manual-validation.yml --ref <branch> -f configuration=Release -f run_tests=true -f package_assets=true
```

Publishing a Windows release is tag-driven:

```powershell
git tag v0.1.0-rc.1
git push origin v0.1.0-rc.1
```

Both workflows reuse the checked-in JUCE submodule path under `external/JUCE`; they do not download JUCE separately.

### Current Status

**Phase 15: Windows CI/CD build and release pipeline** is complete.

- Standalone and VST3 builds are validated through repository-owned PowerShell CI scripts.
- Manual validation runs build from a clean checkout, run `ctest`, and can package release assets without publishing a GitHub release.
- Tag pushes create or update GitHub releases with generated release notes plus standalone, VST3, and checksum downloads.

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

## Patches

CoolSynth supports a minimal patch workflow for synth parameter state:

- `Init Patch` resets automatable synth parameters to their defaults.
- `Save Patch` writes a `.cspatch` XML file containing APVTS parameter state only.
- `Load Patch` restores that parameter state immediately without affecting standalone device settings or learned MIDI mappings.

## Current Limits

CoolSynth does not currently persist:

- window size or position
- recent files
- MIDI monitor UI state
