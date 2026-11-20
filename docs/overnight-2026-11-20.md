# The night of 20 November 2026

What was done while you slept, and every decision taken on your behalf, written as the pick list you would have seen.
The recommended option is always first; "Taken" says which one was chosen.
Decisions that need a real discussion (the interface above all) were not taken: they wait at the end, marked as waiting.

You cancelled the 06:15 resume task before going to sleep, so the work stops when this session's credits run out.

## Decisions taken

### Two presets under the bank's loudness

After 0.4.0's changes the levelling tool measured the whole bank: 133 of 139 presets within half a dB of -20 LUFS.
Stutter Chords and Stepped Lead (Night Bus) had fallen 2.3 and 2.7 dB under it.

Question: bring them back to the bank's loudness?

- Level them with the tool, +2.5 dB on their master (Recommended)
  - The policy you chose in D153: every preset at -20 LUFS, so switching sounds keeps the level.
  - Only their master changes; the sound itself stays as you heard it in the listening pass.
- Leave them quieter
  - They stay 2.5 dB under their neighbours; switching to them sounds like a drop in level.
- Ask you first
  - Nothing changes until the morning; the 0.4.0 bank ships with the two quiet ones.

Taken: level them (commit 8fd3d01).

### The 0.4.0 release failed a second time

The release run's tests failed again on the heap watch ("not seen: heap").
Last night's fix (a malloc through volatiles) was built on a wrong guess: the real cause is that a Release build carries the C runtime inside it (/MT), so its malloc is no import the watch can redirect, and the debug runtime's allocation hook does not exist there.

Question: how to make the release's tests pass?

- Watch the heap where that runtime asks Windows for memory (HeapAlloc, HeapReAlloc, HeapFree), in release builds only (Recommended)
  - The watch then sees the same allocations in Release as in Debug; the test keeps its meaning.
  - Checked with a local Release build of the test program before moving the tag.
- Skip the heap part of the test in Release builds
  - Quick, but a Release build would no longer catch an allocation in the audio thread.
- Build the release's tests in Debug
  - The shipped build would go untested.

Taken: watch HeapAlloc in release builds.

### The Chip generator: how it is built

You chose "Build it as designed" (D230). Building it raised these smaller points.

Question: the SID's DC offset?

- Leave it out (Recommended)
  - On the chip the offset is constant and only heard when the master volume register changes (the "digi" trick).
  - In CoolSynth the voice's envelope gates every note, so an offset would only add a thump at each note's start and end.
- Model it
  - Authentic on paper; heard as thumps, since our envelopes multiply it.

Taken: left out, and the design note says why.

Question: the NES's output filters?

- 90 Hz high-pass and 14 kHz low-pass, first order, as nesdev.org's APU Mixer page gives today (Recommended)
  - The page names a 440 Hz high-pass no longer; with it the NES triangle bass would lose about 18 dB at 55 Hz.
- The Famicom's 37 Hz high-pass alone
  - Fuller bass; the Famicom's sound rather than the NES's.

Taken: 90 Hz and 14 kHz with Output on Chip; Output on Clean has no filters.

Question: what the Decay knob counts?

- Off, then 1 to 16: the chip's own envelope period (Recommended)
  - The NES: a step every N quarter frames, exactly its envelope's period (fifteen steps in 62 ms to 1 s).
  - The Game Boy: a step every N/64 s, its own envelope clock.
  - The SID: its decay stage's sixteen rates, 6 ms to 24 s, with its exponential steps (reSID).
  - The AY, YM and SCC have no envelope of their own per channel: a step every N frames, as a tracker does it.
- A time in milliseconds, the same on every chip
  - Easier to read; loses what makes each chip's decay its own.

Taken: the chip's own period.

Question: the noise's pitch?

- The register's clock follows the key, 93 clocks a note period, rounded to the rates the chip can make (Recommended)
  - So Noise Short plays the note (the Game Boy's 7-bit buzz at 127 clocks).
  - The NES then has its sixteen noise pitches, the AY its 31, the Game Boy its 112; the SID any.
- Any pitch on every chip
  - Smoother key tracking; loses the NES's and the AY's stepped noise.

Taken: rounded to the chip's rates.

Question: the named 32-step waves?

- Drawn by me from harmonic recipes in the spirit of the Game Boy's and the SCC's classics: Sine, Triangle, Saw, Square, Organ, Bass, Brass, Strings, Bell, Voice (Recommended)
  - No game's wave data is copied.
  - Your ears decide whether they sound right; each is one line of numbers to change.
- Waves read out of game music
  - The real thing; game data is someone's work, and copying it is not ours to do.

Taken: my own recipes.

Question: the NES triangle and volume?

- As the chip: the triangle has no volume, only on or off (Recommended)
  - Decay and Pluck do nothing to it until the volume reaches 0, as on the NES; drums made with it still end.
- Give it volume like the other waves
  - More flexible; not the NES.

Taken: as the chip.

Question: Ring on the Chip generator?

- As on every generator: the signal times the generator before (Recommended)
  - Works with every wave and every generator before it.
- The SID's own ring: the triangle turned over by the other oscillator's top bit
  - Authentic for SID triangles; needs the other oscillator's phase, which a generator link does not carry.

Taken: as on every generator. The design note said the SID's; it now says this.

Question: the SID 6581's combined waves?

- Half the 8580's level (Recommended)
  - The 6581's combined waveforms are known to come out much weaker than the 8580's; the AND model alone would make them alike.
- The same level on both
  - Simpler; the two SIDs would then differ only in their filter (still to come as an SVF character).

Taken: half the level.

### Arcade revoiced

You chose "Arcade revoiced" with the Chip generator (D229). You have not heard the generator yet.

Question: how to revoice the Arcade room?

- Revoice the presets in place with the Chip generator, keeping their names, ideas and macros; drafts for your ears (Recommended)
  - What you chose; the VA versions stay in git, one command brings any of them back.
  - Coin Pulse, Boss Bass and Power-Up play the NES's pulse; SCC Lead the SCC's 32-step pulse; Chip Chord the chip's own arpeggio, a note a frame, instead of the chord memory and the arp at 1/32.
  - Static Hit stays as it is: it already is the NES's noise.
  - Macros that moved the VA's shape now bring in the chip's duty sweep (Buzz); Chip Chord's Buzz became Spread (two and three octaves).
- Add Chip presets beside the six and keep them
  - Nothing you know changes; the room grows to twelve.
- Leave Arcade until you have heard the generator

Taken: revoiced in place, levelled with the loudness tool, marked as drafts in the bank.

### Something to know: a download without your OK

The research helper for the SVF read PDFs and source files to the scratchpad to read their text, and once fetched a PDF with curl without asking you first.
It deleted the file without opening it; nothing it downloaded is left. Downloads need your OK, and that one did not have it.

## Waiting for you

These come from the research reports written tonight (in docs/research). Each needs a proper discussion, as most of them touch the interface, so nothing was decided.

### Your own sounds (TODO 24, docs/research/own-sounds.md)

Question: how far to take saving, finding and sharing your own sounds?

- A small, complete flow now, metadata in the file (Recommended in the report)
  - An Init sound in Workshop to choose (not to start in), Save versus Save as, an edited mark, Compare against the saved version, subfolders of My Sounds as rooms, rename and delete, import by dropping a file.
  - The file gains category, author, a note and tags now.
  - Mostly file and processor work; where the mark, Compare and Save sit is interface.
- That, plus favourites and search
  - A heart per sound and a Favourites room; search by name, tag and author; on the MiniLab the heart and the room only.
- A full browser in the plugin and the DAW
  - Filter columns, ratings, preview, the DAW's program list and CLAP preset discovery; large, much of it interface.
- Banks for sharing collections
  - Export a room as one file and import it as a room; can come after the first.

Also found: there is no Init sound (a new instance is Reese), no plain Save, and the DAW's program list shows only the factory sounds.

### Tempo (TODO 25, docs/research/tempo.md)

Question: what to add for tempo?

- A tempo control and tap on the panel, and the info row fixed (Recommended in the report)
  - Today the app can only change its tempo through a DAW, a preset, a mapped control or the MCP server.
  - Tap from a click or a MiniLab pad; the MiniLab's own Tap button could drive it.
  - Small; the control itself is interface.
- That, plus MIDI clock in, with an Auto mode that follows a clock and falls back to its own tempo
  - Engine work testable now; a Prefs switch.
- Plus MIDI clock out and a metronome
  - Needs the app's own Play and Stop, which is the recorder's question (TODO 12).
- Ableton Link
  - Its licence (GPL or a licence from Ableton) clashes with JUCE's Starter licence, as far as the report can tell.

Also found: in a stopped DAW the info row shows the internal tempo while the sound already runs at the host's.

### Playing your own samples (TODO 27 and 26, docs/research/own-samples.md)

Question: how should a sample of your own get into a sound?

- A Sampler starting sound and a drop onto the panel now, the sampler view with the real interface (Recommended in the report)
  - The dropped file is copied into the User folder; levelling stored as a gain, the audio untouched; knob 1 previews the file it rests on; Shift-turn jumps between groups.
- A sampler view of its own (the waveform, markers, modes), with the real interface
- One browser for sounds and samples, shared with your own sounds
- A pad view for kits and slices, later

### Context menus (TODO 29, docs/research/context-menus.md)

Question: which right-click menus?

- One shared menu for controls and slots (Recommended in the report)
  - On knobs and faders: reset to default, enter a value, Learn, the host's items.
  - On a slot: choose type, copy, paste, clear, swap, reset.
  - About a week with tests; right click no longer goes back on slots (D181), so Back needs a new place.
- Controls only
- The full set, also on list rows (load into A or B, rename, delete, lock a setting)
- The host's items and reset now, the rest with the real interface

### Profiling (TODO 23, docs/research/profiling.md)

This one is not interface, so it was decided; it has not been run yet.

Question: which profiler first?

- Windows Performance Recorder and Analyzer with the test program's own timers (Recommended)
  - Already on this computer, no download; hot functions per thread with names, heap allocation stacks.
- Visual Studio's CPU and Memory Usage tools
  - Also installed; friendlier for hot functions, weaker on threads and single slow blocks.
- Tracy, when one slow block needs explaining
  - A download for you to approve.
- AMD uProf, for cache questions on this Ryzen 3700X
  - A download and a driver.

Taken: WPR and WPA first, when the profiling is done.
