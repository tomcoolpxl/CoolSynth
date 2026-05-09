# CoolSynth VST3 Smoke Test

## Preconditions

- Windows 11
- Ableton Live Lite installed
- Release VST3 built successfully

## Build

```powershell
cmake --build build --config Release --target CoolSynth_VST3
```

## Deploy

We use an opt-in CMake step for local deployment.
To configure CMake for auto-deploy to the per-user VST3 folder:
```powershell
cmake -B build -S . -DCOOLSYNTH_ENABLE_VST3_USER_INSTALL=ON
cmake --build build --config Release --target CoolSynth_VST3
```

Alternatively, copy `build/CoolSynth_artefacts/Release/VST3/CoolSynth.vst3` manually.
Destination: `%LOCALAPPDATA%/Programs/Common/VST3/CoolSynth.vst3`

## Ableton Live Lite Steps

1. Rescan plug-ins in Preferences -> Plug-Ins.
2. Insert `CoolSynth` on a MIDI track.
3. Play host MIDI notes (e.g. using computer keyboard or MIDI clip).
4. Open the editor.
5. Automate cutoff and master gain.
6. Save and reopen the project.

## Expected Results

- Plugin loads as an instrument.
- Notes produce sound.
- Automation moves the editor knobs and alters the sound.
- Reopen restores all parameter states.
- The UI contains no standalone-specific components (Settings, Status Bar, MIDI Monitors).

## Troubleshooting

- Duplicate deployed copies: Ensure no other version exists in `%ProgramFiles%/Common Files/VST3`.
- Missing rescan: Check `Ableton Live Lite` settings and force a rescan.
- Wrong VST3 folder: Ensure the `.vst3` bundle (the folder, not just the `.vst3` file if it's a directory) is at `%LOCALAPPDATA%/Programs/Common/VST3`.
- Stale build vs deployed build mismatch: Ensure `COOLSYNTH_ENABLE_VST3_USER_INSTALL` was configured if auto-deploying.
