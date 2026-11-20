# CoolSynth architecture

CoolSynth is a polyphonic software synthesizer for Windows, a VST3 and CLAP plugin and a standalone app, written in
C++20 with JUCE 9.0.5. This page describes how it is built: the engine, its threads, the interface, how sounds are
stored, and where each part lives in the source.

![CoolSynth V3 engine map: keys and MIDI feed two layers, each with a note layer, voices of three generator and three processor slots, modulation and six insert effects; both layers are mixed into six master effect slots, the master gain, a soft clipper and the stereo output.](architecture/engine-map.svg)

The map is drawn by [`tools/site/build_engine_map.py`](../tools/site/build_engine_map.py). A light version,
3200 pixels wide, is [engine-map.png](architecture/engine-map.png). When the engine changes, edit the facts in
that script, run `python tools/site/build_engine_map.py --png`, and commit `docs/architecture/`.

## Contents

- [The two layers and the master chain](#the-two-layers-and-the-master-chain)
- [Signal flow](#signal-flow)
- [Modulation](#modulation)
- [The note layer](#the-note-layer)
- [Effects](#effects)
- [Sounds, and how they are checked](#sounds-and-how-they-are-checked)
- [Threads](#threads)
- [The interface](#the-interface)
- [State and presets](#state-and-presets)
- [Source map](#source-map)
- [Tests and tools](#tests-and-tools)
- [Build and release](#build-and-release)

## The two layers and the master chain

A sound is two layers, A and B. Each layer is a whole sound of its own: its own patch, note layer and
arpeggiator, voices, modulation and six insert effect slots, I1 to I6. After the layers are mixed come six
master effect slots, F1 to F6, then the master gain. The master chain belongs to the combination; one-layer
presets keep their effects in layer A and leave the master slots empty.

The layer mode decides which layer the keys reach (`PartMode` in `src/synth/v3/Part.h`):

- **Stack** plays both layers on every key.
- **Split** gives each layer its own key range.
- **Velocity** chooses by how hard a key is struck: from the split velocity up (64 by default) layer B plays,
  with a fade between the two over a range of velocities (16 by default).

Each layer also has a transpose, and with both layers playing, a velocity range. A layer with every slot Off
is empty and is left out. Everything that is not a note, such as the wheels and the pedal, reaches both layers.

The two layers share 16 voices. Each layer may use what its patch asks for (its polyphony, one voice for mono
and legato, its unison count), is sure of 8, and gets more when the other layer asks for fewer
(`SoundUnit::voiceLimits` in `src/synth/v3/SoundUnit.h`). A heavy patch gets fewer: its voices and effects must
fit half a block on the development machine, by each module's measured worst case, so the count depends on the
patch alone, the same live and bounced (`src/synth/v3/VoiceCost.h`, D222); the screen shows it. A released voice
whose one-shot recording has ended frees itself once it has been silent for 100 ms, instead of running out its
release (`Voice::canEndEarly`).

In the code a layer is a `Part` (`src/synth/v3/Part.h`), and a whole sound, both layers with their inserts,
the master slots, the mix and the master gain, is a `SoundUnit` (`src/synth/v3/SoundUnit.h`). The two names
come from before the layers were called layers.

### Changing sound while notes ring

The engine (`EngineV3`, `src/synth/v3/EngineV3.h`) keeps four sound units: the one being played, up to two
earlier sounds still ringing, and one fading out. When the preset changes while notes sound, the notes held on
the keys or the pedal go on with the sound they started with, its effects included, and only new notes take
the new preset. An earlier sound hears only what ends or bends its notes: key releases, the sustain pedal,
pitch bend, All Notes Off. Five seconds after its last key and the pedal are up it fades out over half a
second, and so does the oldest when a third earlier sound would ring or when the processor runs short of
time; with none left and a dropout near, the playing sound's quietest released voice fades out. The units are
summed, take the Master fader and the Master Boost above it (0 to +12 dB, D225), and pass through one soft
clipper to the output.

Every random number, from the voices' drift to the effects' crackle, comes from a seed of its sound, layer,
voice and slot, and a reset or the host's transport starting takes them all back to it: a song plays and
bounces the same each time, bit for bit (D222); an arpeggiator's New Patterns switch keeps drawing new ones. Bouncing never lets a sound go for lack of time and waits for
the sampled instruments and the player's own sounds to load.

## Signal flow

Per voice:

1. **Three generator slots**, G1 to G3. Each holds any of 11 generator types (VA, Noise, FM2, FM4, Wavetable,
   String, Modal, Sample, Grain, Chip, Kick). A generator can sync, ring-modulate, frequency-modulate or
   amplitude-modulate the next one. Chip (`ChipGenerator.h`, D231) plays the NES, Game Boy, SID, AY, YM and SCC
   with their frame tables (`docs/design/CHIP_GENERATOR.md`); Kick (`KickGenerator.h`, D232) is a tuned kick drum,
   the TR-808 as a struck resonator, the 909 and a modern one.
2. The generators are summed into **three processor slots in series**, P1 to P3. Each holds any of 7
   processor types (Ladder, SVF, Comb, Formant, Folder, Crusher, Drive). Generator 3 can modulate the ladder's
   cutoff at audio rate. The SVF also morphs from low-pass to high-pass as the Oberheim SEM does, and takes the
   C64 SID's 6581 and 8580 characters (`SvfProcessor.h`, D233).
3. **The amplifier**, with pan.

A slot has a type and twelve generic values, named and ranged by its type's descriptor
(`src/synth/v3/Module.h`). Changing a slot's type under a held note crossfades in 5 ms; nothing else in the
patch moves.

Every voice is computed at twice the sample rate and brought down once per layer, so FM and hard sync fold
back less. FM2 and FM4 also hold the index, note by note, where the first sideband that would fold back into
the audible band stays under −80 dB. Modulation is computed in sub-blocks of at most 32 samples at the doubled
rate, which is 3,000 times a second at 48 kHz, and values ramp between updates.

Per layer, after the voices: the six insert slots, then the mix. Each layer has a level, a balance and an
effects share: that share passes through the master slots F1 to F6, and the rest joins after them, before the
master gain.

## Modulation

Per voice:

- **Three envelopes** with delay, attack, hold, decay, sustain and release, a curve per stage, times that
  shorten up the keyboard, loop and one-shot.
- **Two LFOs** with 8 shapes, sample-and-hold and a 16-step staircase among them, in hertz or 16 tempo
  divisions, with delay and fade-in.
- **Per note**: velocity, release velocity, key position, a random value, a slow drift, and key pressure (the
  keyboard's or the key's own).

Per layer, shared by its voices:

- **Two global LFOs** with one phase for all voices, which can lock to the song position.
- **A global envelope** that opens with a phrase's first key or with every key, and can loop.
- **A 16-step sequencer** with a slide per step, 1 to 16 steps, locked to the tempo.
- **Four macros**, **chaos** (a slow random walk), the mod wheel, pitch bend and the expression pedal (CC 11).

**The matrix** (`src/synth/v3/ModMatrix.h`) has 24 routings per layer. Each routing has a source, a
destination, an amount, a second source that scales it (via) and one of 6 curves; one curve snaps pitch to
semitones. Destinations are pitch, level and pan, every slot's values, each generator's pitch, level, pan and
combine amount, the LFO rates and the envelope times. Some connections are always made without a routing:
envelope 1 to the amplifier, envelope 2 to P1's cutoff, velocity to both, the wheel to LFO 1's depth, bend to
pitch, and key tracking per slot.

MPE, each note with its own bend, pressure and slide, is planned and not built.

## The note layer

Each layer has its own note layer, in this order (`Part.h`):

1. **Chord memory** (`src/synth/v3/ChordMemory.h`): one key plays a chord, one of 12 chord types or a chord
   of up to 6 notes learned from the keys, strummed up or down if wanted.
2. **The arpeggiator** (`src/synth/Arpeggiator.h`): 11 patterns, Euclidean rhythms
   (`src/synth/EuclideanRhythm.h`), ratchets of 2 to 4, chance, accent, swing and latch.
3. **The play mode**: poly, mono, legato or unison (2 to 8 voices on one note, spread in pitch and across the
   stereo field), with glide, key priority (last, low or high) and voice spread.
4. **The voice allocator.** In poly mode a key that is still sounding takes its own voice again, so repeated
   notes never stack; otherwise the least recently used silent voice, then the quietest released voice, then
   the quietest voice held only by the pedal, then the oldest held voice.

The tempo is the host's when it reports one, otherwise the arpeggiator's own, so the arpeggiator, synced LFOs
and synced delays always agree.

## Effects

There are 14 effect types, and any of them can sit in any insert slot or master slot, in any order
(`src/synth/v3/Inserts.h`): Drive, Lo-fi, Phaser, Flanger, Chorus, Delay, Plate reverb, Compressor, EQ, Pump
(tempo-synced ducking), Grain, Room, Pitch and Stutter. An empty slot costs nothing.

Ten of them run the code of the older fixed master rack (`src/synth/GlobalFxRack.h`, with one rack per slot
that has only that stage switched on), so an effect sounds the same in an insert slot as in a master slot.
Four have code of their own:

- **Grain** (`src/synth/v3/GrainInsert.h`) replays the last 4 seconds as a cloud of up to 32 grains, and can
  freeze.
- **Room** (`src/synth/v3/RoomInsert.h`): 24 early reflections of a box-shaped room into a 16-line feedback
  delay network.
- **Pitch** (`src/synth/v3/PitchInsert.h`, decision D205) shifts up to two octaves either way, by delay-line
  heads with correlation-matched splices or by a peak-shifting phase vocoder, and has a delay whose feedback
  returns into the shifter, so each repeat climbs or falls another step.
- **Stutter** (`src/synth/v3/StutterInsert.h`, decision D220) catches a slice on the beat, by chance or while
  Hold is on, and repeats it, each repeat optionally shorter, lower, quieter or backwards, as Ableton's Beat
  Repeat does.

When a slot's effect is switched, the old one fades out and the new one fades in along a raised cosine, with
no corner where a fade begins or ends.

## Sounds, and how they are checked

There are 140 factory presets in 14 rooms: Workshop, Bedroom, Arcade, Outrun, Observatory, Greenhouse,
Basement, Studio 1982, Main Stage, Engine Room, Clock Tower, Conservatory, Museum and Night Bus. The player's
own sounds appear as one more room, My Sounds, first in the browser.

- **The Conservatory** holds 40 recorded acoustic instruments in seven families, from free (CC0) sample
  libraries: 2,213 lossless recordings, every recorded pitch, dynamic layer and take, at full length, in stereo.
  They play through the Sample generator, so a piano or a cello section sits in a slot like any oscillator.
  The recordings are not in this repository; they are downloaded separately into
  `C:\ProgramData\CoolSynth\Library`.
- **The Museum** holds classic synth sounds rebuilt from the instruments' own patch data (DX7 voice data,
  Prophet-5 factory programs, a 1974 Minimoog sound chart, the TX81Z preset table), or from a record when no
  data exists. A sound is admitted only after a side-by-side listening test against a recording of the real
  instrument.

Every module has unit tests. Sounds are measured against recordings (harmonic levels, band levels, envelopes,
pitch) before they are judged by ear. Every factory preset is levelled to one loudness offline, and a click
detector renders every preset and looks for clicks.

## Threads

The audio thread never waits for another thread, never takes a lock and never allocates memory while it
renders. Work it cannot do in time, such as reading a file, is asked for and picked up in a later block.

- **The audio thread** (the host's, or the standalone app's audio device) runs `SynthAudioProcessor::processBlock`
  and the whole engine.
- **The audio helpers** (`src/synth/v3/AudioWorkers.h`, D164). When more than one sound unit is sounding, for
  example just after a preset change, the units render on helper threads inside the same block. There is one
  pool for the whole program, with at most the physical cores less two. The helpers register with Windows as
  "Pro Audio" threads and sleep until a block wakes them. The output is the same, bit for bit, with or without
  them.
- **The message thread** runs the editor, the interface model, the MiniLab driver, saving and loading sounds.
- **The sample loader** (`SampleLibrary`, `src/synth/v3/SampleLibrary.h`): the audio thread asks for an
  instrument's recordings and gets them if they are in memory and nothing otherwise; the asking alone makes
  this low-priority background thread read them in, with a pool of decoders on all cores but two.
- **The user library loader** (`UserLibrary`, `src/synth/v3/UserLibrary.h`) reads the player's own wavetables
  and samples from the User folder of the library in the same way.
- **The flight recorder** (`src/diagnostics/FlightRecorder.h`): every thread, the audio thread included, writes
  events into a lock-free ring buffer, and a background thread writes them to
  `%LOCALAPPDATA%\CoolSynth\logs` (one file per session, the last 20 kept).
- **Notes from outside the audio thread**, such as the on-screen keyboard and the MiniLab's keys, go through a
  lock-free queue and play one block later at their exact time (D159).

## The interface

The interface's logic is `InterfaceModel` (`src/interface/InterfaceModel.h`), independent of drawing: the
navigation, Home as the sound browser and live page, the type lists, the knobs, the faders with pickup, and
what the MiniLab's own screen and pads show. It runs on the message thread. Three things drive the same model
and read it back:

- **The on-screen panel**, `PanelEditor` (`src/plugin/PanelEditor.h`), which copies the Arturia MiniLab 3.
- **The MiniLab driver**, `MiniLab3Driver` (`src/interface/MiniLab3Driver.h`), in the app and in the plugin
  (D211). It opens the MiniLab's DAW port, turns its knobs, pads and faders into interface actions, and keeps
  its screen and pad colours in step with the model. In the app it also passes the keys, bend, wheel and
  pressure to the sound; in a plugin the host brings them. One CoolSynth drives it at a time: the one whose
  window opened last. A plugin shares the port with the host through Windows MIDI Services.
- **Any other controller**, through `ControllerInput` (`src/interface/ControllerInput.h`): the computer's
  controller setup (`src/midi/ControllerSetup.h`, kept in `%APPDATA%\CoolSynth\controller.json`) ties a
  controller's knobs, faders, pads and buttons to the panel's places (the main knob, knobs 1-8, faders, pads,
  Shift) or to one sound setting. Known controllers have ready setups (`src/midi/ControllerMap.cpp`); a guided
  setup and a right click's Learn make the rest. Linked messages reach the panel through a lock-free queue and
  never the sound.
- **In a DAW** (D216, [design/CONTROLLERS.md](design/CONTROLLERS.md)) the host's own mapping comes first: the
  first eight parameters are the panel's Home knobs, and the CLAP publishes remote control pages that follow
  the panel's page (`src/plugin/RemoteControlPages.h`).

The interface is specified in [interface-v3.md](interface-v3.md), and the MiniLab 3 protocol, as tested on the
device, in [minilab3-screen.md](minilab3-screen.md). `AuditionEditor` is an older editor that is still in the
sources but no longer used.

## State and presets

- **Parameters** are a JUCE `AudioProcessorValueTreeState`, laid out in `src/parameters`
  (`ParameterLayout`, and in `src/parameters/v3` the per-layer and per-slot parameters).
- **The processor state**, which a host saves in its project, is XML with the root `COOLSYNTH_PROCESSOR_STATE`,
  at format version 17. It holds the parameters, the sound's name and file, and the player's files a slot uses.
  The controller setup is not in it: it is the computer's (D211).
- **A patch file** (`.cspatch`, `src/presets/PatchState.h`) holds one sound, at format version 16. The player's
  own sounds are patch files in `Documents\CoolSynth\My Sounds` (`src/presets/MySounds.h`).
- **The factory presets** are text bank files, `resources/presets/*.csbank`, one per room, read by
  `src/presets/BankFile` and compiled into the binary.
- **Older formats** are carried forward by `src/presets/StateMigration`: processor states from version 9 (the
  first V3 state) and patches from version 8. Anything older is refused.

## Source map

| Folder | What is in it |
| --- | --- |
| `src/plugin` | `SynthAudioProcessor` (the JUCE processor: state, MIDI in, the audio callback), `PanelEditor`, the undo history |
| `src/synth/v3` | The V3 engine: `EngineV3`, `SoundUnit`, `Part` (a layer), `Voice`, every generator and processor, the modulators and matrix, the insert effects, the sample and user libraries, `AudioWorkers` |
| `src/synth` | Code from before V3 that V3 still uses: the arpeggiator, Euclidean rhythms, the effects of the master rack, the 2× half-band filter |
| `src/parameters` | The parameter layout and IDs; `v3/` holds the per-layer and per-slot parameters |
| `src/presets` | Bank files, the factory presets, patch files, My Sounds, state migration |
| `src/interface` | `InterfaceModel`, the interface's logic; the MiniLab driver; `ControllerInput`, any controller on the panel |
| `src/midi` | The controller map, ready setups and the computer's setup; the MiniLab 3 protocol; V2's mapping and the MIDI monitor, still used by the app's settings store |
| `src/standalone` | The standalone app, its MIDI input and settings, the MiniLab driver |
| `src/diagnostics` | The flight recorder and the audio thread's allocation alarm |
| `resources` | The preset banks, the instrument catalogue, the controller profile, images |
| `tests` | The test program's sources |
| `tools` | The command-line tools and the Python scripts |

## Tests and tools

**The app's MCP server** (`src/mcp`, D225). Switched on in the app's Prefs, the app answers the Model Context
Protocol on `http://127.0.0.1:47613/mcp` (Streamable HTTP, written with JUCE's sockets and JSON): Claude Code reads
and sets the sound by the values the screen shows, loads presets and files, plays notes and measures the output,
pictures the window or one part of it enlarged and reads the flight log, which now also logs every knob change
and file choice. It reads the panel as text and clicks its parts by name (D227): the panel describes itself from
what it draws (`PanelEditor::describeForMcp`) and a click is a mouse event at that part. Its tools run on the
message thread. The panel's knobs, faders, pads and buttons also have accessible names and values (JUCE's
accessibility handlers), which Windows Narrator and UI Automation read (`tools/uia-check.ps1`).

**Tests.** All tests are one program, `CoolSynthMidiLearnTests` (the name is older than what it tests), built
from `tests/*.cpp` and run by `ctest`. Set `COOLSYNTH_TEST_ONLY` to part of a test's name to run only those
tests. The test program replaces the global memory allocator and counts every allocation made inside an
audio block, and watches the C heap, locks, waits, sleeps and file calls there too; the audio scenario tests
expect none (`tests/AudioThreadAlarm.cpp`, D161, D222). The determinism test renders every preset fresh and
after a reset and expects the same bits (`tests/DeterminismTests.cpp`). The click detector (`tests/ClickDetectorTests.cpp`) renders every
factory preset; `COOLSYNTH_CLICK_ONLY` narrows it to presets whose names contain a given text. The test
program also hosts the built VST3 as a DAW does (`tests/HostedPluginTests.cpp`, D228): it plays, saves and
reopens its state, bounces twice and opens its panel; the hosted plugin, told by `COOLSYNTH_TEST_HOST`, leaves
the MiniLab and the flight log alone. Every parameter's typed text must read back as shown, and random settings
must come back after a reload (`tests/ParameterRoundTripTests.cpp`); a typed value is read by the parameter's own
display (`src/parameters/TextToValue.h`). Outside the tests, pluginval (VST3) and clap-validator (CLAP) check
the plugins, from `tools/validators` (downloaded, not committed).

**Command-line tools** (C++, built by target, not by the `build-debug` preset):

- `CoolSynthRender` (`tools/RenderPreset.cpp`) renders a preset playing given notes to a WAV file, through the
  real plugin signal path.
- `CoolSynthPresetNormalizer` (`tools/NormalizePresets.cpp`) gives every factory preset one loudness
  (BS.1770), and with `--apply` writes the result into the bank files.
- `CoolSynthPackLibrary` (`tools/PackLibrary.cpp`) packs the instrument recordings losslessly, one file per
  style, and checks every recording after packing.

Rebuild `CoolSynthRender` and `CoolSynthPresetNormalizer` after any change to a bank file or the engine.

**Scripts** (Python):

- `tools/audition` builds the listening pages and does the measuring and fitting behind the Museum and the
  Conservatory.
- `tools/release/package_library.py` packs the instrument library into one zip per family for download.
- `tools/site/build_engine_map.py` draws the map at the top of this page.

## Build and release

CMake with presets (`CMakePresets.json`) and Visual Studio 2022; JUCE is a submodule in `external/JUCE`. The
`CoolSynth` target builds the VST3, the CLAP (through clap-juce-extensions, `external/clap-juce-extensions`)
and the standalone app.

- **Debug** is compiled with optimisation (`/O2`), so the Debug plugin and app play in real time, and uses the
  debug runtime DLLs, which only a machine with Visual Studio has.
- **Release** links the C++ runtime statically (`/MT`), so the app and the VST3 need only Windows' own DLLs,
  and writes symbols to a `.pdb` beside each binary (`/Zi`, `/DEBUG /OPT:REF /OPT:ICF`). The shipped
  binaries are the same as without symbols.

GitHub Actions: `windows-release.yml` builds and publishes a release when a version tag (`v*.*.*`) is pushed;
`windows-manual-validation.yml` builds and checks on demand.
