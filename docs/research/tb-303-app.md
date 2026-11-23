# CoolSynth 303: the app and its interface (parked)

Research for CoolSynth 303 (D275), on 2026-11-23. The interface is parked by the user until the engine is done;
this file keeps what was found and what the user asked for, so the design can start from it.

## What the user wants

- The original's layout and controls, drawn in CoolSynth 808's style.
- Programming that is completely new: the original's is widely disliked.
- Modelled on the software 303s whose usability reviewers rate best.
- A style the user loves (2026-11-23): TAL-Vocoder 3's panel.
  - The screenshot: `tools/references/ui-style/tal-vocoder-3.webp` (TAL's image, local, not in git).
  - A flat charcoal panel; each section in a rounded box drawn as a thin light outline, its name in plain
    capitals at the top left.
  - The product name in tall stencil capitals running up the left edge.
  - Black knobs with one orange pointer line, tick marks round them, the range's ends in small numbers, the label
    below in light grey capitals.
  - Vertical faders with dark caps and a coloured line, scale lines across the whole group.
  - Square buttons, light grey or orange when on, a small red lamp above each.
  - Thin brackets grouping controls under one label; an arrow between sections for the signal's way.
  - A top bar with the preset's name between two arrows, then new, save, undo and redo.
  - A footer line in grey at the bottom right: name, maker, version.

## ReBirth RB-338, which the user played

From its 2.0.1 owner's manual (archive.org) and Sound On Sound's 1997 review.
- Two 303s, an 808 and from 2.0 a 909, in one window, each with its own patterns.
- A step at a time with Step and Back: pitch, note or pause, octave up or down, accent and slide together.
  - Pitch Mode moves to the next step at each key; the computer's keyboard can enter everything.
  - No tie button: a slide over the same pitch holds the note.
- Pattern tools: shift left and right, Random (all, pitches, or accents and slides), Alter (shuffles what is
  there), Transpose, copy and paste. Random and Alter cannot be undone.
- Length 1 to 16 steps, changed while playing; Shuffle (swing), which the 303 never had.
- Distortion (amount and shape), the PCF (a filter played by a pattern), a compressor, a delay send.
- Song mode, up to 500 bars, records every knob move.
- Only a sync slave; exports audio; Mods swap the graphics and sounds.
- An open reimplementation exists: OpenRB-338, GPL-3.0 (github.com/pingw33n/openrb-338), released November 2026.

## How others program a 303

- The original: Pitch Mode, then Time Mode (note, tie, rest); a rest uses no pitch. 64 patterns, 7 tracks.
- Roland Cloud TB-303 (its manual): a step grid with rows for accent, slide, a pitch curve dragged, octave and
  gate.
  - Last step, triplet scales, 8 variations a pattern, play forward, back, both, inverted or random.
  - Generate and Modify with undo, density per accent, slide, pitch and gate, and a scale.
  - Drag a variation into a DAW as MIDI or audio.
- TB-03: the original's mode kept, a step recording mode added, tap writing, a triplet grid. Its manual warns
  that entering pitches again loses the accents and slides.
- AudioRealism ABL3 (Sound On Sound, January 2017): a classic edit and a pattern edit with a pitch lane; random,
  alter and force-to-scale; MIDI out. "More fun than the original", though not all its tricks.
- D16 Phoscyon 2: sequencer, arpeggiator from a riff template, a randomizer choosing which attributes change, drag
  to the DAW; no standalone (a con in MusicRadar's review).
- Behringer TD-3: the original's sequencer copied, with its bugs (rv0.be); "authentically annoying" (Gearnews).
  A browser grid editor (AudioPump's TD-3 Pattern Tool) was made for it.
- Cyclone TT-303 Bass Bot v2: edit while running, patterns from seven "personality" algorithms, mutate and rotate.
- Erica Synths DB-01: live or step entry, lanes for slide, accent and filter modulation, a randomiser.
- In the browser: Endless Acid Banger (generated, no editing), errozero's Acid Machine (16 steps, random, MIDI
  file out).

## What players say

- Use the sequencer to make proper patterns, then turn the knobs (antto, KVR).
- The split between pitch and time is hard to get on other sequencers, and part of the acid sound (Elektronauts;
  Reverb credits acid's odd lines to the 303's quirks).
- Time Mode shows nothing of what is written (Elektronauts).
- Patterns lost when the batteries came out.
- Valued now: a grid in place of pitch and time, random and alter, MIDI drag out, copying patterns.
- Presets are disputed: a 303 is its knobs and its patterns.

## The 303's sequencer, as the manual and measurements give it

- 16 steps (15 in triplets), 64 patterns in four groups of A and B sections of eight, 7 tracks of 64 measures.
- Per step: pitch, octave down or up, accent, slide; in time note, tie or rest. Three octaves a pattern.
- A slide on a pattern's last note slides into the next pattern (manual, page 63).
- Tempo 40 to 300.
- The gate (Julian Schmidt's logic analyser on the CPU, sonic-potions.com): six clocks a step, on for three;
  slide keeps it high; accent and slide latched at the step's start; the gate 0.8 to 3.3 ms after the clock.
- Exchange formats: antto's .303 text, the TD-3's SysEx (303patterns.com), ReBirth's .rbs (OpenRB-338 reads it).

## Effects after a 303

- Distortion is the classic choice (MusicRadar); Hardfloor's "Acperience" is the known example. Delay next.
- Built in elsewhere: the TB-03 (one based on Boss's OD-2), Roland Cloud's drive, Phoscyon's 19 models, the DB-01's
  analogue drive; ABL3 brought its back in 3.3.0. Several reviewers say to leave a built-in one off.
- Pedals with public circuit analyses: ProCo RAT, Boss DS-1, MXR Distortion+, Ibanez Tube Screamer
  (electrosmash.com, not reachable on 2026-11-23); LTspice write-ups of the RAT and TS808 (cushychicken.github.io).

## The Devil Fish

Robin Whittle's manual (2019): overdrive into the filter up to 66 times, slide 60 to 360 ms, soft attack, Decay
moved to the volume, separate normal and accent decays (30 ms to 3 s), accent sweep normal, fast or slow, high
resonance to self-oscillation, filter tracking, filter FM, the muffler (soft clipping), the cutoff an octave
higher, Env Mod to zero, a manual accent, more bass (C20 and C21 larger), inputs for gate, CV, slide, accent and
filter.
- Its manual's page 8 gives the knob positions that make it a stock 303: the trimmers' stock setting.
- The TD-3-MO exposes most of them (and adds a sub-oscillator); Phoscyon 2 some; ABL3 none.

## Naming and the two apps together

- Roland's US mark 6044644, the stylised "TB-303", covers plug-in software; its 2019 EU filing 018016159 covers
  the 303's look. Roland had ReBirth for iPad withdrawn in 2017.
- Others name their own product and say the 303 is Roland's, with no endorsement (AudioRealism, D16, Whittle).
- Two apps together: MIDI clock between them; Windows 11's MIDI Services give loopback ports; JUCE can create a
  virtual port on Windows only with Windows MIDI Services (merged June 2026).
  - Ableton Link is GPL-2.0-or-later: an MIT app can use it, but its binaries then ship under the GPL (Sonic Pi
    does that).
- A library: products ship their own or artists' patterns, none named transcriptions of records; a short ostinato
  was held unprotected in Gray v. Hudson (US, 2022), but a distinctive line may be protected.
