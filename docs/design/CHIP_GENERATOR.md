# The Chip generator

A generator type for chiptune (TODO 20, D229, D230): the sound chips of the NES, the Game Boy, the Commodore 64,
the AY-3-8910 and YM2149, and Konami's SCC, in one slot, with the frame tables chip musicians drive them with.
It is a synthesizer's take on the chips, not an emulator: every wave can be played on every chip model, and the
model brings that chip's quirks.

## What the research found

- The chips (nesdev.org, gbdev, Wikipedia's MOS 6581 and AY-3-8910 pages, the oxyron SID reference, the Furnace
  docs):
  - NES 2A03: pulse at 12.5, 25 and 50 % (75 % sounds as 25 %), 16 volume levels, a 32-step 4-bit triangle with no
    volume, a 15-bit noise register with a long mode and a 93-step mode on a table of 16 periods, a non-linear
    mixer, and output filters at 90 Hz (high-pass) and 14 kHz (low-pass).
  - Game Boy: pulse 12.5 to 75 %, a wave channel of 32 4-bit samples at 0, 25, 50 or 100 % volume, noise in 15-bit
    and 7-bit modes.
  - C64 SID (6581, 8580): triangle, saw, pulse with a 12-bit width, a 23-bit noise register, combined waveforms,
    ring modulation (the triangle turned by the previous voice) and hard sync, a stepped envelope whose "ADSR bug"
    trackers handle with a hard restart, and a 12 dB multimode filter (the 6581's distorting, the 8580's clean).
  - AY-3-8910 and YM2149: squares, a 17-bit noise, and one envelope of 16 (AY) or 32 (YM) steps, which run fast
    becomes the "buzzer" saw and triangle bass.
  - SCC: 32-step waves of 8 bits.
- How they are played: arpeggios a step a frame (1/60 s on NTSC, 1/50 on PAL), duty sweeps, pitch slides, vibrato
  with a delay, and drums made by switching wave and pitch every frame (GoatTracker's wave, pulse and filter tables;
  Furnace's volume, arpeggio, duty, wave and pitch macros with loop and release points; Magical 8bit Plug's 1/60 s
  volume, pitch and duty sequences).
- What others offer: Plogue's chipsynth line (32 SID variants, three oscillators with sync, PWM and ring, a
  modelled filter, 4- and 8-bit sample playback), Magical 8bit Plug 2, Tweakbench Peach (30 NES waves) and Toad
  (NES drums), inSIDious.

## The user's choices (D230)

- All four families: NES, Game Boy, SID (6581 and 8580), AY/YM and SCC.
- Frame tables as named tables with knobs, which work on today's panel and the MiniLab.
- Band-limited, with a Raw switch for deliberate aliasing.
- The SID filter as a new character of the SVF processor, for any sound (built after the generator).

## The twelve knobs

- Chip: NES, Game Boy, SID 6581, SID 8580, AY, YM, SCC. What it brings:
  - the duty steps (NES and Game Boy: 12.5, 25, 50, 75 %; SID: free, 12 bits; AY and YM: 50 % only);
  - the triangle's resolution (NES: 32 steps of 16 levels; SID: smooth) and the noise register (NES 15-bit,
    Game Boy 15 and 7-bit, AY and YM 17-bit, SID 23-bit, each with its own way of reading out);
  - the volume steps of its envelope (16 levels; YM 32);
  - its output (Output below): the NES's non-linear mixer and filters, the Game Boy's high-pass, the SID's DC
    offset and its own output filter;
  - its frame rate (Frame below): NES and Game Boy 60 Hz, C64, AY and YM 50 Hz (as the European machines ran).
- Wave: Pulse, Triangle, Saw, Noise, Noise Short (the 93-step or 7-bit buzz), Tri+Saw, Tri+Pulse, Saw+Pulse,
  Tri+Saw+Pulse (the SID's combined waves), Table (a 32-step wave below), Buzz Saw, Buzz Triangle (the AY's
  envelope run at the note's pitch).
- Duty: the pulse width, 0 to 100 %, stepped as the chip allows; modulatable, so sweeps come from an LFO or the
  step modulator.
- Table: the 32-step waves for Wave Table, 4-bit on the Game Boy, 8-bit on the SCC: researched classics (the
  Game Boy's saw, square and organ-like waves; SCC brass, strings, bell and bass waves).
- Sequence: a named frame table, played from the note's start: Off; Kick, Snare, Hat, Tom (drums as trackers make
  them: a frame of noise, then a falling pulse or triangle); Arp Major, Arp Minor, Arp Octave, Arp Fifth, Arp
  Seventh (a step a frame through the chord, looping); Duty Sweep; Pluck (volume down a step a frame); Swell;
  Blip (a pitch flick); Vibrato (with a delay); Hard Restart (the SID's: silence and a phase reset for a frame or
  two before the note).
- Speed: frames per step of the sequence, 1 to 16.
- Depth: how far the sequence goes: the arpeggio's spread, the drum's pitch drop, the sweep's width.
- Decay: the chip's own volume envelope, a step every few frames from 15 down to 0 (NES, Game Boy, AY), off for a
  steady level (the voice's envelopes then shape it, as on the SID).
- Sweep: the NES sweep unit's pitch glide per frame, up or down.
- Frame: the frame rate of Sequence, Decay and Sweep: the chip's own, 50 Hz or 60 Hz.
- Output: Chip (its mixer curve, output filters and offsets) or Clean.
- Raw: off (every step band-limited) or on (naive steps, aliasing as on a cheap emulator).

What already exists stays where it is:
- the slot's level, pan, octave, semitone and fine;
- Combine with the generator before it: Sync (hard sync, as the SID), Ring, AM, FM, as on every generator (the
  SID's own ring needs the other oscillator's phase, which a generator link does not carry);
- the voice's envelopes, LFOs and matrix for the rest.

## As built (D231)

- `src/synth/v3/ChipGenerator.h` and `src/synth/v3/ChipWaves.h`; tests in `tests/V3ChipTests.cpp`.
- Decay counts the chip's own envelope period: Off, then 1 to 16.
  - The NES: a step every N quarter frames, its envelope's own period.
  - The Game Boy: a step every N/64 s.
  - The SID: its decay stage's sixteen rates, 6 ms to 24 s, with its exponential steps (reSID).
  - The AY, YM and SCC have none per channel: a step every N frames, as a tracker does.
- Noise follows the key, 93 register clocks a note period (the Game Boy's 7-bit mode 127), rounded to the rates the
  chip can make: sixteen on the NES, 31 on the AY, 112 on the Game Boy, any on the SID. The SCC's noise is the
  MSX's AY.
- Noise Short is the NES's 93-step mode, the Game Boy's 7-bit mode on the Game Boy.
- Output Chip: the NES's mixer curve and its 90 Hz and 14 kHz filters, the Game Boy's 28 Hz high-pass (its
  capacitor), reSID's external filter for the C64, the AY's and YM's logarithmic volume (Ayumi's tables).
- The NES triangle has no volume, as on the chip: Decay and Pluck reach it only at 0.
- The SID 6581's combined waves are half the 8580's level.
- The SID's DC offset is left out: constant on the chip, it would only thump under CoolSynth's envelopes.
- The ten named waves are my own harmonic recipes in the spirit of the classics; no game's data is copied.
- Sequences, a step every Speed frames, Depth as follows:
  - Kick and Tom: a frame of noise, then the tone falling Depth × 4 octaves to the note.
  - Snare: noise with a tone frame; Hat: high noise, Depth its length.
  - The arpeggios: Depth 0 to a third one octave, then two, then three.
  - Duty Sweep: Depth the width's swing; Pluck and Swell: how far the volume falls or starts.
  - Blip: the first step up to two octaves; Vibrato: after twelve steps, up to two semitones.
  - Hard Restart: the first step silent, then the phase restarts.

## How it is made

- Fixed shapes (the NES triangle, Game Boy and SCC tables, the AY buzz, the SID's triangle and saw, its combined
  waves) are band-limited one-cycle tables built from their spectra at 11 band limits, as the Wavetable generator's.
  The SID's combined waves are a model: the waveforms' bits ANDed, a known approximation that is brighter than a real
  chip (Plogue samples them); with a pulse in them, a table of 16 widths, read between its frames.
- Moving edges (a pulse whose width sweeps, the noise registers) are drawn with the same 4-point band-limited steps
  as the VA and Noise generators.
- Raw draws the same shapes without band limits.
- The sequences run at the frame rate from the note's start; the generator's own volume (Decay, a sequence's
  volume) is stepped to the chip's 16 or 32 levels.
- Mono, as the chips were; a sound gets its width from the effects or a second slot.
- Its cost goes into `VoiceCost.h` once measured.
- New parameters and the new type go at the end of their lists, so saved sounds load as they were.

## Not in it, and why

- The SID's ADSR bug itself: CoolSynth's envelopes stay smooth; Hard Restart and Decay give the chip's behaviour
  where it is heard.
- The SID's filter: a character of the SVF (D230), so it also serves other sounds.
- Free per-frame editing of the tables, as a tracker's: with the real interface (UI last).
- Sample playback through the volume register (the C64's digis): the Sample generator with the Crusher does it.
