# CoolSynth

![CoolSynth playing Sea Pad, a two-layer sound from the Night Bus room](screenshot.png)

CoolSynth is a two-layer software synthesizer for Windows, as a standalone app and a VST3 plugin.
It is built around the Arturia MiniLab 3: CoolSynth drives the MiniLab's screen, pads and knobs. Any
MIDI keyboard plays it, any controller's knobs and pads can work its panel, and the mouse reaches
everything on screen.

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

Download `CoolSynth-Setup-<version>.exe` from [Releases](https://github.com/tomcoolpxl/CoolSynth/releases)
and run it. Windows may warn that it is from an unknown publisher (it is not code-signed): choose
**More info**, then **Run anyway**. The installer puts:

- the **VST3** where every DAW looks (`C:\Program Files\Common Files\VST3`); let your DAW rescan its
  plugins and CoolSynth is listed as an instrument;
- the **app**, with a Start menu entry;
- the **recorded instruments** you tick (2.6 GB for all seven families), downloaded from the
  [instrument library](https://huggingface.co/datasets/tomccargo/coolsynth-instruments) and checked
  before they are unpacked. Without them everything else plays; those sounds stay silent.

Without the installer: the release also has the VST3 and the app as zips (copy `CoolSynth.vst3` into
`C:\Program Files\Common Files\VST3`), and the instrument families as zips on the instrument library's
page (their folders go in `C:\ProgramData\CoolSynth\Library`). Requires 64-bit Windows. Made and tested
on Windows 11.

## Your controller

Any MIDI keyboard plays CoolSynth. Its knobs, faders, pads and buttons can work CoolSynth's panel as
well: a knob turns knob 3 of whatever page is open, a pad opens a section.

- **Known controllers** (Akai MPK mini mk3, Novation Launchkey Mini MK3 and MK4, Arturia KeyLab
  Essential mk3, NI Komplete Kontrol M32 and A series) are set up by themselves when plugged in; the
  screen says so.
- **Any other**: **Prefs > Controller setup... > Set up step by step** asks for each knob, fader and
  pad in turn: move the one you want.
- **One at a time**: right-click a knob, fader or pad on the panel and choose **Learn**; on a knob you
  can also tie a control to the setting it shows, on every page.

The setup is kept for the computer: the app and the plugin in every project share it.

**FL Studio** passes a controller's knobs to a plugin only on a MIDI port the two share:
in **Options > MIDI settings** select your keyboard and set its **Port** to a number (say 1); then in
CoolSynth's window open the plugin wrapper's settings, the **Settings** tab, and set **MIDI > Input
port** to the same number. This is the same for every plugin in FL.

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

CoolSynth uses the MiniLab 3's screen, pads and knobs, in the app and in a DAW: the screen shows the
sound and the knob you turn, the pads open the sections, and the knobs turn the settings on screen.
With CoolSynth on several tracks, the one whose window you opened last has the MiniLab. Sharing the MiniLab with the DAW needs
Windows MIDI Services (Windows 11 24H2 or later, through Windows Update).

So that the pads do not also play notes in a DAW, the installer adds a small script for each:
**FL Studio** links it to the MiniLab by itself; in **Ableton Live** choose **CoolSynth_MiniLab** once
as a Control Surface (Settings > Link, Tempo & MIDI) with Input **Minilab3 MIDI** and Output **None**,
and leave no other MiniLab control surface selected. To use the MiniLab as a plain controller
instead, switch **Prefs > Controller setup... > MiniLab 3 driver** to **Off**.

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
