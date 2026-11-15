# Sample memory and a possible track mode

Decision D162 (2026-11-15). What the sample memory (D155) must allow so that a 4-track recorder
inside CoolSynth stays possible later, what such a mode would cost, and how DAWs and hardware do
it. Nothing of the track mode is built; it is decided after the engine, presets, arp and
interface.

## The rules that hold now

1. **One sample memory per program.** `SampleLibrary::instance()` is a static, so every engine in
   one process reads the same memory: tracks inside the app, and plugin copies in a host that
   loads plugins into one process. An instrument used on two tracks is in memory once.
2. **An instrument that a loaded sound names is never reused while that sound is loaded**, however
   long it has not played: a quiet part B, or a track that rests for a minute, keeps its
   instrument and does not stall when it comes back in. When the memory is full, only instruments
   that no loaded sound names are reused, the one played longest ago first. When a sound (later a
   song) needs more than the memory holds, CoolSynth says which instrument does not fit; it never
   stalls silently.
3. **The engine keeps no globals besides the sample memory**, so several engines can run side by
   side, one per track.

The size stays D155's memory check: half the free memory at start, at most the whole library,
with a warning when the computer is too small. On the development PC (32 GB, 19 GB free) that is
the whole library, 3.84 GB: every instrument stays in and nothing reloads. The size is one
constant in the memory-bank code.

## What a song would need in memory

A song keeps every track's instruments in memory at once: up to eight (two parts per track).
Sizes held at their own depth (D155): Steinway 0.73 GB; Old Trombone about 0.3 GB, Cellos and
Tenor Sax about 0.22 GB each (estimated from their FLAC size, 2.9 times smaller on disk); the
library 3.84 GB. The Steinway and the Trombone together already pass 1 GB, which is why a fixed
1 GB was not chosen. The memory shared by all tracks on hardware: Digitakt II 0.4 GB, Kronos X
about 2 GB (plus streaming from its SSD), Montage M 3.8 GB of user flash.

Tracks themselves cost almost nothing: a track holds notes, not audio. A frozen track as audio
costs about 70 MB per 3 minutes (stereo, 32-bit float, 48 kHz; 52 MB at 24-bit).

## What it would cost the processor

Measured on the development PC (AMD Ryzen 7 3700X, 8 cores, 16 threads), from the flight
recorder: one CoolSynth playing 16 to 17 voices took at most 2.64 ms of its 10.88 ms block
(480 samples at 44.1 kHz), about a quarter of one core; the heaviest preset with 32 note-ons in
one block took 5.9 ms (D161).

- Four tracks on one thread: about 10.6 ms a block for ordinary presets, at the limit: dropouts.
- Four tracks on four cores: each a quarter of its core, about an eighth of the machine.
- Adding the four tracks together costs nothing worth measuring.

So a track mode needs the tracks rendered in parallel, one engine per core, as DAWs place one
instrument track per core (Craig Anderton's test), or the tracks not played frozen to audio.

## How the tracks would play (the leaning chosen)

**All tracks live, a track frozen to audio on demand.** Each track is its own engine with its
own patch; the song loads every track's sound once (Home glows once). A pad chooses which track
the MiniLab plays: that only moves the keys, so switching is instant, as on the Kronos, Montage
and Digitakt. A track can be frozen to audio to free its processor time; a frozen track's
instruments may then be released.

Not chosen: only the played track live and the rest always as audio (least processor and 1 GB
would do, but leaving a track renders it, about 45 s per 3 minutes on one core, and going back
reloads its instruments).

## How others do it

- **DAWs** compute every instrument track live, all the time, and add them. Ableton's Freeze
  renders a track to 32-bit audio files and plays those instead, reversibly (renamed Bounce Track
  in Place in Live 12.2). Cubase's ASIO-Guard computes tracks that are not record-enabled ahead
  in a much larger buffer and only the armed track in the small, low-latency one; REAPER's
  anticipative FX does the same. That is "three streams and one active", done in memory as it
  plays.
- **Several copies of one plugin**: statics are shared by every copy in one process (JUCE forum).
  Bitwig can host each copy in its own process ("Individually"), and then nothing is shared;
  "By plug-in" keeps copies of one plugin together. Kontakt copies do not share sample memory by
  default; Vienna's players do (VSL forum).
- **Workstations** load everything a performance or song needs when it loads. The Kronos plays
  16 timbres over 16 MIDI tracks from one sample memory with one voice pool; the Montage M has
  16 parts and its voice pools are per engine, shared by the parts; the Digitakt II's 16 tracks
  share 400 MB, a sample used on every track counted once. Choosing a track only moves the keys.
- **Streaming from disk** is how large samplers outgrow RAM: Kontakt keeps only the start of each
  recording in RAM (its preload buffer) and reads the rest from disk; the Kronos plays large
  samples from its SSD. D152 names it the later step for CoolSynth. With it, the memory holds
  starts of recordings instead of whole instruments.
- **Tiny boxes**: community firmwares for the M-VAVE FM-1 (12 voices, a small dual-core chip),
  such as KARMA, run four tracks. Their tracks hold notes and their engines (FM, models after
  Plaits and Elements) compute their sound from small tables, so neither tracks nor samples cost
  them memory; what costs is each voice's sound. CoolSynth's voices cost more by choice
  (quality first).

## Open for when the track mode is designed

- Voices: sixteen per track (64 in all), or one budget shared by all tracks, as the FM-1 and the
  Kronos have, which also caps the processor time.
- Effects: each track's own six master slots, or one master after the mix with sends.
- Threads: how the app renders the engines in parallel (a small real-time worker pool), and
  whether a frozen track's instruments are released.
- Plugin copies in separate processes each take their own memory by the memory check (here up to
  3.84 GB each); a copy could size its memory to its sounds instead.

## Sources

- Bitwig, plug-in handling: https://bitwig.com/userguide/latest/vst_plug-in_handling_and_options
- Ableton, Freeze: https://www.ableton.com/en/live-manual/11/computer-audio-resources-and-strategies/
- Ableton, Bounce Track in Place: https://help.ableton.com/hc/en-us/articles/22999165562396
- Steinberg, ASIO-Guard: https://helpcenter.steinberg.de/hc/en-us/articles/206103564
- Sound On Sound, REAPER's anticipative processing: https://www.soundonsound.com/techniques/running-multiple-plug-ins
- Craig Anderton, multitimbral instrument or several copies: https://craiganderton.org/multi-timbral-instrument-vs-multiple-instances-which-is-better/
- JUCE forum, statics shared between plugin copies: https://forum.juce.com/t/singleton-instance-shared-between-plugin-instances/39867
- VSL forum, Kontakt and Vienna sharing memory: https://forum.vsl.co.at/post/280196
- Native Instruments, what DFD is: https://support.native-instruments.com/support/solutions/articles/69000879768-what-is-dfd-
- ADSR, Kontakt DFD and preload: https://www.adsrsounds.com/kontakt-tutorials/how-to-use-and-optimize-kontakt-dfd
- Korg Kronos manual: https://www.manualslib.com/manual/1264657/Korg-Kronos.html?page=24
- Korg Kronos X, Virtual Memory Technology: https://www.korg.com/uk/products/synthesizers/kronos_x/page_2.php
- Yamaha Montage M: https://usa.yamaha.com/products/music_production/synthesizers/montagem/
- Sound On Sound, Digitakt II: https://www.soundonsound.com/reviews/elektron-digitakt-ii
- Elektronauts, a Digitakt sample on all tracks: https://www.elektronauts.com/t/introducing-digitakt/35326?page=63
- Sonicstate, KARMA for the FM-1: https://sonicstate.com/news/2026/11/02/karma-turns-m-vave-fm-1into-a-groovebox/
- Synthtopia, the FM-1: https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/
