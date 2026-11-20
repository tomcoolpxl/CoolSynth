# The night of 20 November 2026

What was done while you slept, and every decision taken on your behalf, written as the pick list you would have seen.
The recommended option is always first; "Taken" says which one was chosen.
Decisions that need a real discussion (the interface above all) were not taken: they wait at the end, marked as waiting.

You cancelled the 06:15 resume task before going to sleep, so the work stops when this session's credits run out.

## What was done

- 0.4.0 is published, after a third release run: https://github.com/tomcoolpxl/CoolSynth/releases/tag/v0.4.0
  - Installer, VST3, CLAP and app zips, checksums.
  - The first fix for the failing test was based on a wrong guess; the real cause and fix are below.
- The Chip generator is built (D231): every chip and wave, the frame tables, band-limited, tested.
- Arcade is revoiced with it: five presets, drafts for your ears.
- The Kick generator is built (D232): 808, 909 and Modern.
- Succession 808 is drafted in Engine Room, for your ears.
- The SVF has the SEM's morph (Mode LP>HP with a Morph knob), the SID's LP+BP and BP+HP, and a Character knob: Clean (as before, to the bit), SID 6581, SID 8580 (D233).
  - Saved sounds and DAW projects are converted on load (format versions 17 and 16).
- Seven research reports in docs/research; the ones that touch the interface wait at the end of this file.
- A small fix: the info row showed the wrong tempo in a stopped DAW.
- The full test suite ran on the build with all of it: 579 tests, all passing but one SVF check that measured in the wrong place; that check was fixed and its group, the voice cap and the scenarios ran again and passed.
- None of this is in 0.4.0: the generators and the SVF characters wait for the next release.

For your ears, when you have a moment:
- the Chip generator on each chip (any sound, G1 type Chip; Arcade's five);
- Succession 808 (Engine Room), played at C4, A flat 3, F3 and G3;
- the Kick generator's 909 and Modern models;
- the SVF's SID 6581 and 8580 characters and the LP>HP morph (P1 type SVF, Character and Mode; Morph on a macro or LFO).

One slip: twice tonight a shell command of mine contained a heredoc, against your rule. Both did nothing (no-ops), but they should not have been written.

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

Taken: watch HeapAlloc in release builds. The tag moved to that commit (76a0f39), the run passed, and 0.4.0 is published: https://github.com/tomcoolpxl/CoolSynth/releases/tag/v0.4.0 (installer, VST3, CLAP and app zips, checksums). The Chip and Kick generators came after it and are not in 0.4.0.

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

### The Kick generator (D232)

You chose a kick generator (D229). The research is in docs/research/kick-generator.md.

Question: what kind of kick generator?

- One generator with a Model switch, 808, 909 and Modern, over twelve shared knobs (Recommended)
  - The Succession 808, the 909 and today's kicks in one slot, as the Chip generator shares its knobs across chips.
  - Some knobs mean slightly different things per model.
- The 808 circuit model alone
  - The real 808; nothing else.
- A pitch-envelope kick alone (909 and modern)
  - Quick; an 808 only imitated, without its accent, sigh or hits on a ringing drum.

Taken: one generator with a Model switch.

Question: how far to model the 808?

- A struck resonator that does what Werner, Abel and Smith measured: the attack shift, the level-dependent sigh, the accent, hits on a ringing drum, the clipped pulse as click (Recommended)
  - Built and tested tonight; calibrated to the paper's numbers (a sigh of about 2.4 semitones over a quarter second).
- Werner's full circuit model in wave digital filters
  - The most faithful; long work, best calibrated against reference recordings you would listen to.
- A pitch-envelope imitation
  - Cheapest; not the 808's behaviour.

Taken: the struck resonator; the full circuit model stays possible if your ears want more.

Question: the Drive curve?

- A cubic soft clip with first-order anti-aliasing (Recommended)
  - Brought the kick from 0.9 % to 0.33 % of a block a voice, with the pitch moved to every fourth sample.
- tanh with the same anti-aliasing
  - A slightly rounder clip at about three times the cost.

Taken: the cubic.

Question: the Succession preset?

- Succession 808 in Engine Room, from the research's sketch: the 808 at C2 when C4 is played, a 1.3 s boom, a sigh of 2.7 semitones, dark, driven, mono so each hit strikes the ringing drum (Recommended)
  - A sound-alike: no source gives the theme's settings, nor that its 808s are tuned to the bass (D229 said so; corrected in D232).
- Tuned to C1 for big speakers
- In the Night Bus room

Taken: Succession 808 in Engine Room, levelled, a draft for your ears.

Question: a click test flagged Polyrhythm Pad, a preset I did not touch. The Brightness and Space test checks every third preset, and adding Succession 808 moved that choice onto Polyrhythm Pad for the first time. Its Brightness sweep read 1.75 times the sound's own roughness, but the profile shows no spike: the pad simply stays brighter. What to do?

- Give that test the rule the macros' test already has (D224): the reference also holds the sound's roughness just after the change, at its level during it (Recommended)
  - A click comes and goes; a brighter sound stays rough. With the rule it reads 0.92.
- Smooth Brightness in the engine
  - Engine work for a change nobody hears as a click.
- Change Polyrhythm Pad
  - Changes a sound to please a test.

Taken: the test's rule.

### The SVF: the SEM morph and the SID characters (D233)

You chose the SEM's morph on the SVF (D229) and the SID's filter as an SVF character (D230). The research is in docs/research/svf-sem-sid.md.

Question: how to add the SEM morph?

- A new mode at the end of the list, LP>HP, with a new Morph knob (Recommended)
  - Morph runs from the low-pass through the notch to the high-pass, as the SEM's mode knob does; both ends stay at 0 dB, the middle is today's notch.
  - Saved sounds and DAW projects need a conversion (as for Brown noise, D221): done in the loader, format versions 17 and 16. A DAW's automation of an SVF's Mode, if any, would need redrawing.
- Mode itself turns into a continuous knob
  - No conversion; but the in-between shapes are not the SEM's, and the knob shows numbers instead of names.
- An SEM character with the SEM's own colour
  - More than you asked for; it can come later on top.

Taken: the LP>HP mode and Morph. The SID's two other sums, LP+BP and BP+HP, joined the list in the same step, so one conversion covers all three.

Question: how to add the SID?

- A Character knob, Clean, SID 6581, SID 8580, and a Curve knob from dark to bright (Recommended)
  - The names match the Chip generator's; Clean is today's filter to the bit (a test checks every sample).
- One SID character with a 6581 or 8580 switch
  - Two knobs for one choice, and a switch that means nothing in Clean.
- A SID processor of its own
  - Closest to the chip; repeats the SVF's code, and you chose a character (D230).

Taken: Character and Curve.

Question: how should the 6581 read the Cutoff knob?

- As the register an 8580 would need, then through the 6581's measured curve (reSID's points: 220 Hz at the bottom, the drop from 6 kHz to 4.6 kHz at the middle, 18 kHz at the top) (Recommended)
  - Sweeps have the 6581's dead bottom and its kink; the knob's Hz is the 8580's.
- Keep the knob's Hz and only add the 6581's floor
  - Honest readings; loses the kink and the dead bottom.

Taken: through the measured curve.

Question: the 6581's top resonance step?

- Held at Q 16, a long ring that still decays, as D38 keeps every SVF (Recommended)
- Let it sing at whatever level the clipping allows

Taken: held at Q 16.

Question: its place in the voice cap?

- A cost of its own for the 6581 (0.40, measured), Clean and the 8580 at today's 0.17 (Recommended)
  - Polyphony falls only for sounds that use the 6581.
- One cost for every SVF at the 6581's price
  - Every sound with an SVF would lose voices.

Taken: a cost of its own.

### Something to know: a download without your OK

The research helper for the SVF read PDFs and source files to the scratchpad to read their text, and once fetched a PDF with curl without asking you first.
It deleted the file without opening it; nothing it downloaded is left. Downloads need your OK, and that one did not have it.

## Waiting for you

Answered by you the next day (D235), asked again as pick lists with more detail: a full browser, one browser for sounds and samples, samples levelled by default, the full set of context menus with Back in every menu, tempo and tap plus MIDI clock in following tempo and beat, the app's tempo with sounds able to opt in, the Museum set the Museum's way, the deep presets with you one at a time, profiling with WPR, CPU first, before the Mac port. You also changed one night decision: the 808 is to become Werner's full circuit model (docs/research/kick-808-wdf.md).

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

### Not started: the Museum's ARP and Oberheim set, and six deep presets

Both were chosen (D229). They are sound design that only your ears can judge, so I left them rather than draft them blind.

Question: how to make the Museum's ARP and Oberheim set (OB-Xa Jump, OB-X stabs, an SEM notch pad with the new morph, the Odyssey's Chameleon bass, an Odyssey sync lead)?

- As every Museum sound: from published settings, fitted against reference recordings you approve side by side, waiting in resources/museum-candidates until admitted (Recommended)
  - The Museum's rule (D88, D103); needs recordings and your ears for each.
- Drafts from published settings alone, straight into the Museum
  - Quicker; breaks the room's rule, and a first Prophet-5 version built that way "sounded like a midi pc sound card" to you.

Question: the six deep presets (both layers, many effect slots)?

- Design them with you, one room at a time, each heard before the next (Recommended)
- Drafted by me in one go, levelled, for you to judge afterwards

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

## The afternoon: the 808 as its circuit (D236)

You left saying to start with what needs no permission, the 808 first.
Done while you were away; the decisions follow as pick lists, the recommended option first.

- The Kick generator's 808 is now Werner's circuit: the trigger's pulse shaper, the envelope generator's two transistors, the bridged-T resonator with Q43 and D52, the feedback buffer with the Decay pot, the tone, level and output buffer.
  - Checked stage by stage against ngspice renders of the whole schematic: the output within 2.5 % rms (1.2 % accented), the sigh within 0.25 Hz, the decay within 3 %.
- It costs about half of what its first version did this morning: 3.1 % of a block a voice on average, 6.6 % in its hits, against 0.33 % for the old struck resonator.
  - The voice cap prices the 808 on its own; the 909 and Modern stay cheap (0.41 %).
- A slot switching to the Kick no longer solves the circuit on the audio thread: its rest state is solved once at startup.
- Compared with Michael Fischer's 1994 recordings of a real 808 (analysis only, never shipped), 25 bass drums at five Tone and five Decay marks:
  - the tail's pitch: the recordings 49.6 to 50.4 Hz, the model 49.3 to 49.6 Hz;
  - the sigh: within about 1 Hz at Decay 5 and up;
  - the decay: the recorded unit rings 25 to 35 % longer at the long marks (3.7 s against 2.7 s at full); the Kick's Decay knob reads seconds, so it reaches both;
  - the brightness of the first 20 ms: the model about 15 % duller at every mark, perhaps the real trigger's sharper edges; for your ears.
- The Tone knob now turns VR5 as the 808's C-taper pot does (the service notes' parts list), fitted to the recordings.
- Succession 808 is levelled again: master from 0.5 to 2.0 dB (the circuit is quieter, the tapered Tone a little brighter).

For your ears:
- Succession 808 (Engine Room), at C4, A flat 3, F3 and G3, as before;
- any Kick on Model 808: Punch, Punch Time, Sigh, Click, Shape, Accent, Tone and Decay now each move a part of the circuit.

One slip: a shell command of mine held an empty heredoc, against your rule; it only created an empty file, deleted at once.

### R164

Question: the thesis's parts table gives R164 as 4.7 kOhm. With it the feedback loop's gain is about 8 and the drum oscillates on its own; it rings forever only above 0.94. Which value?

- 47 kOhm, as in Werner's DAFx 2014 paper and the service notes (Recommended)
  - The drum then matches the thesis's own figures.
- 4.7 kOhm as the table says, with VR6 held low enough to stay stable
  - Follows what looks like a typo; the Decay's range changes.

Taken: 47 kOhm.

### chowdsp_wdf

Question: you chose chowdsp_wdf with our own root for the nonlinear parts. In the 808 every element ends up under one root (the op-amps, Q43 and D52 tie the bridged-T and the feedback into one network that no series-parallel tree splits), so chowdsp's adaptors have nothing left to do. What then?

- Our own root holds every element; chowdsp_wdf is not added (Recommended)
  - No dependency, nothing downloaded for it; the pulse shaper keeps the thesis's alpha transform, as chowdsp's CapacitorAlphaT has it.
- Add chowdsp_wdf for the pulse shaper and the output filter alone
  - A dependency for two small trees the root already solves.

Taken: our own root; chowdsp_wdf is not in the repository.

### The output buffer Q44

Question: how to model the output transistor Q44?

- An ideal emitter follower (Recommended)
  - It only buffers; the thesis treats the output stage as linear.
- A transistor solved like Q43
  - More cost for a difference below the comparison's noise.

Taken: the ideal follower.

### Oversampling

Question: should the circuit run faster than the voice?

- No: the voices already run at 96 kHz (Recommended)
  - Within 2.5 % of SPICE as it is; the rails' soft clip sits outside the feedback loop.
- 2x inside the circuit
  - About twice the cost, for the hardest Shape settings only.

Taken: no oversampling; to revisit if the hardest Shape aliases to your ears.

### The knobs on the circuit

Question: how do the Kick's twelve knobs reach the 808's circuit? Each knob's default is the stock 808.

- Each knob moves the part of the circuit that does that job (Recommended)
  - Decay: VR6, through a table measured on the model so the knob still reads seconds; past VR6's 500 kOhm an extended range up to 740 kOhm, to reach the knob's 10 s.
  - Punch: R166, the attack's pitch jump; Punch Time: C38, how long the envelope generator holds Q43 on; Sigh: R161, the leak that bends the pitch.
  - Click: the trigger's width, 0.1 ms to the stock 1 ms to 10 ms; Accent: the trigger from 4 V unaccented to 14 V.
  - Shape: the op-amps' rails from 15 V down to 4 V, a soft clip at the resonator's output; Tone: VR5.
- Only the 808's own controls (Decay, Tone, Accent), the other knobs without effect on this model
  - The real panel; the bends gone.

Taken: each knob on its part.

### The Tone knob's taper

Question: the service notes' parts list gives VR5, the bass drum's Tone pot, as 10 kOhm C taper (Decay's VR6 is B, linear). Turned linearly, the model's brightness barely moves until the top of the knob; Fischer's recordings step evenly. How should the Kick's Tone turn VR5?

- As the C taper, fitted to the recordings: about a quarter of VR5 left at mid-travel (Recommended)
  - The model's brightness then steps as the recordings do (0, 0.21, 0.50, 0.79, 1 of its range against 0, 0.11, 0.52, 0.74, 1).
  - The default Tone, 0.5, sounds a little brighter than before; Succession 808 was levelled again.
- Linear, as the thesis parametrises it
  - Most of the knob does little.

Taken: the C taper.

### The shortest decay

Question: at the stock tuning the circuit's shortest decay is 0.17 s to -60 dB (Fischer's recorded unit: 0.27 s); the Decay knob goes down to 50 ms. What below 0.17 s?

- The circuit's floor: the 808 stays at 0.17 s (Recommended)
  - Faithful; the knob's lowest stretch does nothing on this model. Higher notes reach shorter, their capacitors being smaller.
- A fade after the circuit for the shorter settings
  - Reaches the whole knob; not the circuit any more.

Taken: the floor; say so if short 808s should be possible.

### A hit on a ringing drum

Question: a kick test checks that a hit on a ringing drum makes no step: no sample-to-sample move above 0.02. With the tapered Tone the 808's circuit reached 0.0201, its own attack (on a silent drum the same hit moves 0.0192). What to do?

- For the 808, compare with the same hit on a silent drum, plus the ring's own slope (Recommended)
  - Tests what the check is for, a jump in the state; the circuit has no state to jump.
- Raise the fixed limit for all models
  - Loosens the 909's and Modern's check for no reason.
- Make the 808's attack softer
  - Changes the circuit to please a test.

Taken: the comparison with a silent drum; the 909 and Modern keep 0.02.

### The accent's level

Question: the old model's soft hit (velocity 0.3 with Accent full) was under half the full hit's level. The circuit gives about two thirds, as Werner measures: the accent grows the attack more than the ring. What to do?

- Keep the circuit's accent; the test now expects the soft hit under 0.8 of the full one (Recommended)
- Scale the output with velocity on top
  - More contrast; no longer the circuit.

Taken: the circuit's accent.

### The voice cap's price for the 808

Question: the cap counts a voice at its average cost, because most modules' 99th percentile drowns in the timer's noise. The 808's does not: 3.1 % a voice on average, 6.6 % in its hits. Which price?

- Its hits, 6.6 %, as the effects that spike are priced (Recommended)
  - Seven 808 voices fit a sound's 50 % budget, and even all of them struck at once stay inside it. A mono kick never notices.
- Its average, 3.1 %
  - Fourteen voices, but fourteen hits struck together could take a whole block.

Taken: its hits.

### The solver's tolerance

Question: how exactly must the circuit's Newton solver settle each sample?

- To 100 nV, some 150 dB under the drum's volts, starting from where the last samples point (Recommended)
  - Half the work in the hit, a third less in the tail; the comparison with SPICE is unchanged to every digit shown.
- To 10 nV, as this morning
  - About twice the cost of each hit.

Taken: 100 nV.
