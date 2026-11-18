# CoolSynth

![CoolSynth playing Sea Pad, a two-layer sound from the Night Bus room](screenshot.png)

CoolSynth is a two-layer software synthesizer for Windows, as a standalone app and a VST3 plugin.
It is built around the Arturia MiniLab 3: the app drives the MiniLab's screen, pads and knobs. Any
MIDI keyboard plays it, and the mouse reaches everything on screen.

How it is built, with a map of the engine: [the architecture page](docs/architecture.md).

## What is in it

- **Two layers**, each a whole sound, played stacked, split across the keyboard, or by velocity.
- **Nine sound sources**, three per layer at once: virtual analogue, noise, two- and four-operator
  FM, wavetables, a plucked string, struck bars and bells, recorded instruments, and grains.
- **Seven filters and shapers**, three per voice: a ladder and a state-variable filter, a bit
  crusher, a wavefolder, drive, a vowel filter and a comb.
- **Movement**: three envelopes, two LFOs per voice and two for the whole sound, a 16-step
  modulator, a 24-slot modulation matrix, four macros per sound, chord memory and an arpeggiator
  with Euclidean rhythms, ratchets and chance.
- **Thirteen effects** in six slots per layer and six on the master: drive, lo-fi, phaser, flanger,
  chorus, tape and ping-pong delay, plate reverb with shimmer, room, compressor, EQ, pump, grains,
  and a pitch shifter with a pitch-stepping delay.
- **136 presets in 14 rooms**, from synthwave and techno to the Night Bus room of laptop-made sounds,
  the Museum's rebuilt classic synths and the Conservatory's 40 recorded acoustic instruments.
- **Your own sounds**: a WAV file played across the keys, cut into slices (in equal pieces or at
  its hits, one per key), or a folder of files played as a kit.

## Install

1. Download the latest release from [Releases](https://github.com/tomcoolpxl/CoolSynth/releases):
   the VST3 zip, the app zip, or both. Neither needs anything else installed.
2. **VST3**: copy `CoolSynth.vst3` into `C:\Program Files\Common Files\VST3`, then let your DAW
   rescan its plugins. CoolSynth is listed as an instrument.
3. **App**: unzip anywhere and run `CoolSynth.exe`. Windows may warn that the app is from an
   unknown publisher (it is not code-signed): choose **More info**, then **Run anyway**.
4. **Recorded instruments** (optional, 2.6 GB): the Conservatory's 40 instruments, and the presets
   that use them, need the instrument library, linked from the release notes. Put its folders in
   `C:\ProgramData\CoolSynth\Library`. Without it everything else plays; those sounds stay silent.

Requires 64-bit Windows. Made and tested on Windows 11.

## Your own sounds

CoolSynth reads your files from the `User` folder inside the library folder
(`C:\ProgramData\CoolSynth\Library\User`; CoolSynth makes it the first time you look). In the
Sound section, set a slot to **Sample**: turning its first knob past the instruments lists your
files and folders, and a press loads one.

- **Slices**: on the Sample slot's fourth page set **Slices** to **Equal** (with a **Count**) or
  **Attacks** (with a **Sensitivity**). Each piece gets its own key from **First Key** up, plays at
  its own speed and plays to its end. With the layer in Mono, a new key cuts the last piece.
- **Kits**: choose a folder instead of a file: each file in it gets a key, in name order.

## MiniLab 3

The app takes over the MiniLab 3's DAW mode: its screen shows the sound and the knob you turn, its
pads open the sections, and its knobs turn the four settings on screen. In a DAW, the host owns
the MiniLab: its keys, wheels and pressure play CoolSynth, and its knobs can be tied to
CoolSynth's settings with the host's own MIDI mapping.

## Build from source

Prerequisites: Windows 11, Git, CMake 3.22 or newer, and Visual Studio 2022 (or its Build Tools).

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth.git
cd CoolSynth
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The Debug build is compiled optimised, so it plays in real time. Configure with
`-DCOOLSYNTH_ENABLE_VST3_USER_INSTALL=ON` to have each build copy the VST3 into your own VST3
folder (`%LOCALAPPDATA%\Programs\Common\VST3`). For a build to share, use `--config Release`: it
carries Microsoft's C++ runtime inside it, so it runs on a computer with nothing installed.

Outputs: `build/CoolSynth_artefacts/<Config>/Standalone/CoolSynth.exe` and
`build/CoolSynth_artefacts/<Config>/VST3/CoolSynth.vst3`.

Releases are built by `.github/workflows/windows-release.yml` when a tag such as `v0.3.0` is pushed.

## Licence

CoolSynth's own code is under the MIT licence (`LICENSE`). It is built with
[JUCE](https://juce.com) 8, used under the free Starter tier of the
[JUCE 8 licence](https://juce.com/legal/juce-8-licence/). The recorded
instruments come from Versilian Studios' VSCO 2 Community Edition and the Versilian Community
Sample Library, both released into the public domain (CC0).
