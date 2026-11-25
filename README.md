# CoolSynth

![CoolSynth with both layers loaded, each shown as its chain of slots](screenshot.png)

CoolSynth is a bi-timbral, multi-engine hybrid synthesizer.
Two layers play at once, stacked, split or by velocity, and each is a chain of slots: three sound engines, three filters or shapers, and six effects, every slot taking any engine or effect of its kind.

It runs as a standalone app or as a VST3 or CLAP plugin in your DAW.

It was designed around the Arturia MiniLab 3 and uses its screen, pads and knobs, but any MIDI keyboard works.

Its siblings, built on the same sound engine: [CoolSynth 303](https://github.com/tomcoolpxl/CoolSynth303), a bass line
synthesizer modelled on the TB-303's circuit, and [CoolSynth 808](https://github.com/tomcoolpxl/CoolSynth808), a drum
machine modelled on the TR-808's.

## Download

Get the latest `CoolSynth-Setup` installer from the [Releases page](https://github.com/tomcoolpxl/CoolSynth/releases): the `.exe` for Windows, the `.pkg` for a Mac.

CoolSynth runs on 64-bit Windows 10 or 11, and on Macs with Apple silicon (M1 or newer) with macOS 13 Ventura or newer.

### Windows

Windows may warn that the publisher is unknown, because the installer isn't code-signed. Click **More info**, then **Run anyway**.

The installer puts the VST3 in `C:\Program Files\Common Files\VST3`, the CLAP in `C:\Program Files\Common Files\CLAP` and the app in the Start menu. It also downloads the sampled instruments you select from the [instrument library](https://huggingface.co/datasets/tomccargo/coolsynth-instruments), 2.6 GB for all of them.

### Mac

Open the `.pkg`. The installer isn't signed, so macOS blocks it once: go to **System Settings > Privacy & Security** and click **Open Anyway**.

It installs for your user only, without an administrator password: the app in `~/Applications`, the plugins in `~/Library/Audio/Plug-Ins`, and the instruments you select in `~/Music/CoolSynth`.

## Sounds

There are 139 presets in 14 rooms: synthwave, techno, ambient, lo-fi, classic synths rebuilt in the Museum, a stock TR-808 kit, and 40 sampled acoustic instruments in the Conservatory.

CoolSynth is **bi-timbral**. A preset can use layer A and layer B, stacked on every key, split across the keyboard, or switched by velocity. Right-click a layer on the screen to copy, swap or double it.

What the engine has:

- Sound sources
  - virtual analog
  - noise
  - 2- and 4-operator FM
  - wavetables
  - Karplus-Strong string
  - modal synthesis (bars, bells)
  - multisampled instruments
  - granular
  - sound chips: the NES, Game Boy, C64 SID, AY and YM, Konami's SCC, with their frame-rate arpeggios and drums
  - a tuned kick drum: the TR-808's as its circuit, and a modern kick
  - the TB-303 bass line: saw to square through its diode ladder, with its accent and slide
  - the TR-808's bass drum, toms or congas and rim shot or claves as their circuits, played as a kit on one voice
  - the TR-808's cowbell, cymbal and open and closed hi-hats as their circuits, on its six shared oscillators
  - the TR-808's snare, hand clap and maracas as their circuits, the clap's four claps and its reverb tail
- Filters and shapers
  - ladder and state-variable filters; the state-variable one also morphs as the Oberheim SEM and has the C64 SID's 6581 and 8580 characters
  - bit crusher
  - wavefolder
  - drive
  - formant filter (vowels)
  - comb filter
- Modulation
  - envelopes and LFOs
  - a 16-step mod sequencer
  - a 24-slot modulation matrix
  - four macros per sound
- Arpeggiator and chords
  - Euclidean rhythms, ratchets and chance
  - chord memory
- Effects
  - drive, lo-fi, phaser, flanger and chorus
  - tape and ping-pong delay
  - plate reverb with shimmer, and a room reverb
  - compressor, EQ and tempo-synced ducking
  - granular, pitch shifter and stutter

Random parts (LFOs, arpeggios, chance) start again with the transport, so an exported bounce sounds exactly like what you heard.

Every preset is levelled to the same loudness, with room to spare in a mix. For more, push the Master fader past its 0 dB mark, on the screen or with your controller's fader 4: up to 12 dB louder at the top.

## Your own samples

Put WAV files in `C:\ProgramData\CoolSynth\Library\User`, or on a Mac in `~/Music/CoolSynth/Library/User`. In the Sound section, set a slot to Sample and turn its first knob past the built-in instruments to find your files. Press the knob to load one.

A sample can be sliced across the keys, in equal pieces or at its hits, on the slot's fourth page. A folder loads as a kit with one file per key.

## Controllers

Any MIDI keyboard plays CoolSynth.

Set your keyboard's size in **Prefs > Your keyboard** to see its keys marked on the screen's.

In a DAW, the simplest way to put your controller's knobs on CoolSynth is the DAW's own mapping (MIDI Map in Live, Link to controller in FL Studio, Quick Controls in Cubase, learn in Bitwig or Reaper). CoolSynth lists its four macros, Glide, Brightness, Space and the master volume first, and the macros carry the sound's own names, such as "A1 Bite".

CoolSynth can also learn your controller itself, so its knobs, faders and pads follow the panel's pages the way the MiniLab does. The Akai MPK mini mk3, Novation Launchkey Mini MK3 and MK4, Arturia KeyLab Essential mk3 and Native Instruments Komplete Kontrol M32 and A-series set themselves up when plugged in. For anything else, open **Prefs > Controller setup > Set up step by step**, or right-click a control and choose **Learn**. In FL Studio this needs your keyboard on a numbered MIDI port, the same in FL's MIDI settings and in the plugin wrapper's **Settings** tab.

In the app, **Prefs > Claude (MCP)** lets Claude Code read and set the sound and play notes while it helps you. It is off until you switch it on, and only this computer can reach it.

## Arturia MiniLab 3

CoolSynth takes over the MiniLab's screen, pads and knobs, in the app and in a DAW: the screen shows the sound and the knob you're turning, and the pads open the sections. With CoolSynth on several tracks, the window you opened last gets it. In a DAW it needs Windows 11 24H2 or later; on a Mac it hasn't been tried yet.

The installer adds a MiniLab script to Ableton Live, Bitwig Studio and FL Studio, so the pads don't also play notes there. If your DAW already used another MiniLab setup, see [the MiniLab in a DAW](docs/minilab.md) for the one setting to change.

## Building from source

On Windows, you need Git, CMake 3.22 or newer, and Visual Studio 2022 or its Build Tools.

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth.git
cd CoolSynth
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Debug builds are optimized, so they play in real time. Use `--config Release` for a build you want to give to someone else. Add `-DCOOLSYNTH_ENABLE_VST3_USER_INSTALL=ON` to copy the VST3 into your user VST3 folder after every build.

On a Mac with Apple silicon, you need the Xcode command line tools, CMake 3.22 or newer and Ninja (`brew install cmake ninja`):

```bash
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth.git
cd CoolSynth
cmake --preset macos
cmake --build --preset macos-debug
ctest --test-dir build -C Debug --output-on-failure
```

`scripts/ci/BuildAndTestMac.sh Release --package <name>` builds, tests and makes the installer and zips as the release does.

How the engine works is described in [docs/architecture.md](docs/architecture.md).

## License

CoolSynth's own code is MIT licensed (see `LICENSE`). It's built with [JUCE](https://juce.com) 9 under the free [Starter licence](https://juce.com/legal/juce-9-licence/). The sampled instruments come from Versilian Studios' VSCO 2 Community Edition and the Versilian Community Sample Library, both CC0. The TB-303 generator is rewritten from [Open303](https://github.com/RobinSchmidt/Open303) by Robin Schmidt, MIT licensed; its notice is in `THIRD_PARTY_NOTICES.txt`, which the installer ships beside `LICENSE`.
