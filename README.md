# CoolSynth

CoolSynth is a JUCE-based software synthesizer for Windows 11. It is built as both a standalone desktop instrument and a VST3 plugin, with the same synth engine and parameter set in both modes.

The instrument is intentionally compact and direct: one oscillator per voice, a subtractive signal path, and a hardware-style control surface that stays close to playing and sound shaping instead of menu-heavy workflow. It is designed to be understandable, usable with a MIDI keyboard right away, and cleanly shared between standalone and plugin builds.

## What It Does

- Polyphonic MIDI note playback.
- One oscillator per voice with sine, square, and saw waveforms.
- Per-voice ADSR amplitude envelope.
- Per-voice resonant low-pass filter.
- Global delay with time, feedback, and mix controls.
- Master output gain.
- Panic / all-notes-off control.

The same core sound engine drives both targets, so the standalone app and the VST3 plugin expose the same main synth controls.

## Standalone and VST3

The standalone application is the self-contained instrument. It handles audio device setup, MIDI input selection, and the extra runtime tools that make the synth easier to play and inspect outside a DAW.

In standalone mode, CoolSynth provides:

- audio backend, output device, sample rate, and buffer-size configuration
- one active MIDI input device at a time
- a MIDI monitor for incoming events
- remembered standalone settings for audio configuration, selected MIDI input, and learned MIDI CC mappings
- clear unavailable-state reporting if a remembered audio or MIDI device is missing when the app starts

The VST3 build uses the same synth engine and parameters inside a DAW. Audio routing, MIDI routing, and session management are handled by the host instead of the standalone UI.

## MIDI Learn

In standalone mode, continuous controls such as knobs and faders support MIDI learn from a right-click context menu.

- `Learn MIDI CC` arms the control and waits for an incoming CC message.
- `Cancel MIDI Learn` exits the armed state without changing the mapping.
- `Clear MIDI CC Mapping` removes the learned assignment for that control.

Learned mappings are stored with standalone settings and apply to the standalone instrument only.

## Patch Workflow

CoolSynth includes a minimal patch format for synth parameter state.

- `Init Patch` resets synth parameters to their defaults.
- `Save Patch` writes a `.cspatch` XML file containing synth parameter state.
- `Load Patch` restores that parameter state without changing standalone audio settings, MIDI device selection, or learned MIDI mappings.

## Build

### Prerequisites

- Windows 11
- Git
- CMake 3.22 or newer
- Visual Studio 2022 Build Tools or Visual Studio 2022

### Configure and Build

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

To build a release configuration instead, change `Debug` to `Release` in the last command.

### Run Tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

### Build Outputs

- `build/CoolSynth_artefacts/<Config>/Standalone/CoolSynth.exe`
- `build/CoolSynth_artefacts/<Config>/VST3/CoolSynth.vst3`

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
