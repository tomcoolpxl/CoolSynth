# CoolSynth

![CoolSynth with both layers loaded, each shown as its chain of slots](screenshot.png)

CoolSynth is a bi-timbral, multi-engine hybrid synthesizer.
Two layers play at once, stacked, split or by velocity, and each is a chain of slots: three sound engines, three filters or shapers, and six effects, every slot taking any engine or effect of its kind.

It runs as a standalone app or as a VST3 or CLAP plugin in your DAW.

It was designed around the Arturia MiniLab 3 and uses its screen, pads and knobs, but any MIDI keyboard works.

## Download

Get the latest `CoolSynth-Setup` installer from the [Releases page](https://github.com/tomcoolpxl/CoolSynth/releases).

Windows may warn that the publisher is unknown, because the installer isn't code-signed. Click **More info**, then **Run anyway**.

The installer puts the VST3 in `C:\Program Files\Common Files\VST3`, the CLAP in `C:\Program Files\Common Files\CLAP` and the app in the Start menu. It also downloads the sampled instruments you select from the [instrument library](https://huggingface.co/datasets/tomccargo/coolsynth-instruments), 2.6 GB for all of them.

CoolSynth runs on 64-bit Windows 10 or 11 for now; macOS is planned.

## Sounds

There are 138 presets in 14 rooms: synthwave, techno, ambient, lo-fi, classic synths rebuilt in the Museum, and 40 sampled acoustic instruments in the Conservatory.

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
  - a tuned kick drum: the TR-808 and the TR-909 as their circuits, and a modern kick
  - the TB-303 bass line, after Open303, with its accent and slide
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

A song plays the same each time you start it, and its bounce matches what you heard: the random parts start again with the transport.

Every preset is levelled to the same loudness, with room to spare in a mix. For more, push the Master fader past its 0 dB mark, on the screen or with your controller's fader 4: up to 12 dB louder at the top.

## Your own samples

Put WAV files in `C:\ProgramData\CoolSynth\Library\User`. In the Sound section, set a slot to Sample and turn its first knob past the built-in instruments to find your files. Press the knob to load one.

A sample can be sliced across the keys, in equal pieces or at its hits, on the slot's fourth page. A folder loads as a kit with one file per key.

## Controllers

Any MIDI keyboard plays CoolSynth.

Set your keyboard's size in **Prefs > Your keyboard** to see its keys marked on the screen's.

In the app, **Prefs > Claude (MCP)** lets Claude Code read and set the sound, play notes, read and click the panel while it helps you; it is off until you switch it on, and only this computer can reach it.

In a DAW, the simplest way to put your controller's knobs on CoolSynth is the DAW's own mapping: MIDI Map in Ableton Live, Link to controller in FL Studio, Quick Controls in Cubase, or the learn function of Bitwig or Reaper. CoolSynth lists its four macros, Glide, Brightness, Space and the master volume first, so DAWs that give a controller a plugin's first eight controls start there. The macros carry the playing sound's names for them, such as "A1 Bite".

CoolSynth can also learn your controller itself, so its knobs, faders and pads work the panel the way the MiniLab does, following the page you're on:

- These set themselves up when you plug them in: Akai MPK mini mk3, Novation Launchkey Mini MK3 and MK4, Arturia KeyLab Essential mk3, Native Instruments Komplete Kontrol M32 and A-series.
- For anything else, open **Prefs > Controller setup > Set up step by step**, or right-click a knob, fader or pad and choose **Learn**.
- The setup is saved for the whole computer, so the app and every plugin instance share it.

In FL Studio, CoolSynth's own learning only gets your knobs through a shared MIDI port. In **Options > MIDI settings**, give your keyboard a port number. Then in CoolSynth's plugin window, open the wrapper's **Settings** tab and set **MIDI > Input port** to the same number.

## Arturia MiniLab 3

CoolSynth takes over the MiniLab's screen, pads and knobs, both in the app and in a DAW. The screen shows the sound and the knob you're turning, and the pads open the sections.

With CoolSynth on several tracks, the window you opened last gets the MiniLab. Using it inside a DAW needs Windows 11 24H2 or later.

The MiniLab sends its pads on MIDI channel 10 and its keys on channel 1. To keep the pads from also playing notes in your DAW:

- In Ableton Live, the installer adds the CoolSynth_MiniLab control surface and Live loads it by itself. It also arms the MIDI track you select, so the keys play it. If **Settings > Link, Tempo & MIDI** shows another control surface on the MiniLab (set up before CoolSynth was installed), choose CoolSynth_MiniLab there instead, with input and output Minilab3 MIDI. Or set the CoolSynth track's **MIDI From** channel to **Ch. 1**.
- In Bitwig Studio, the installer adds a CoolSynth MiniLab 3 controller script. Bitwig's own MiniLab 3 controller launches clips with the pads: in **Settings > Controllers**, remove the Arturia MiniLab 3 controller if Bitwig set it up, and add CoolSynth MiniLab 3 under the detected MiniLab.
- In FL Studio, the installer adds the CoolSynth MiniLab 3 script and FL picks it for the MiniLab by itself. If you used the MiniLab in FL before, FL keeps your earlier choice: in **Options > MIDI settings**, set the MiniLab's controller type to CoolSynth MiniLab 3 once.

To use the MiniLab as an ordinary MIDI controller instead, turn off **Prefs > Controller setup > MiniLab 3 driver**. It then works in its Arturia program and in its DAW program.

## Building from source

You need Windows, Git, CMake 3.22 or newer, and Visual Studio 2022 or its Build Tools.

```powershell
git clone --recurse-submodules https://github.com/tomcoolpxl/CoolSynth.git
cd CoolSynth
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Debug builds are optimized, so they play in real time. Use `--config Release` for a build you want to give to someone else. Add `-DCOOLSYNTH_ENABLE_VST3_USER_INSTALL=ON` to copy the VST3 into your user VST3 folder after every build.

How the engine works is described in [docs/architecture.md](docs/architecture.md).

## License

CoolSynth's own code is MIT licensed (see `LICENSE`). It's built with [JUCE](https://juce.com) 9 under the free [Starter licence](https://juce.com/legal/juce-9-licence/). The sampled instruments come from Versilian Studios' VSCO 2 Community Edition and the Versilian Community Sample Library, both CC0. The TB-303 generator is rewritten from [Open303](https://github.com/RobinSchmidt/Open303) by Robin Schmidt, MIT licensed; its notice is in `THIRD_PARTY_NOTICES.txt`, which the installer ships beside `LICENSE`.
