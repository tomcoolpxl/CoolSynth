# The Kick's 808 against two real TR-808s (2026-11-20, again 2026-11-21)

## After the fixes (D251, 2026-11-21)

Each gap of the first comparison (below) traced to its cause in SPICE and the model, fixed as the user chose, then
measured again through the plugin itself (CoolSynthRender, the voice included) on all 25 settings of each unit, matched
as before. The scripts are `fit808.py`, `tonefit.py`, `glidemap.py` and the earlier pipeline, run on the bare circuit
through the test program's `COOLSYNTH_808_JOBS` mode and on the plugin through CoolSynthRender.

Why the attack was dull, three causes on our side, in order of size:
- The voice's amp envelope opened over at least 0.5 ms (a click guard), fading in the 808's click and its fast first
  swing: 3.7 dB of click at TONE full in SPICE. The patch's attack knob bottoms out at that 0.5 ms too.
  - The circuits start each note at rest and cannot click, so a voice whose generators all start from rest now opens
    at once from silence (the knob's bottom means at once); struck again while it sounds, the guard stays.
- The trigger. The thesis drives the pulse shaper from an ideal source with edges ramped over 91 us, to ease its
  solver. The service notes (pp. 4, 8, 9) give the real path: IC6's 1 ms pulse lets Q32, an emitter follower, put
  about 4 V on the TRIG line (4 to 14 V accented), which R113 holds down when Q32 lets go; the bass drum takes it
  through Q40, a PNP switch that can only push current, while Q39 holds its instrument data.
  - When the pulse ends, the trigger node is not pulled to zero but drains through Q41's base divider and C40 into
    D53, so the shaped pulse stays clamped negative some 0.3 ms longer and pulls the first lobe back sooner.
  - Microsecond edges alone add only 1 dB of click; the stage as drawn adds 1.8 dB and ends the first lobe 0.17 ms
    sooner.
- The diodes. The thesis takes the 1N4148's saturation current (2.52 nA) without its emission coefficient (1.752), so
  D52 and D53 turn on at about 0.3 V instead of 0.6. D53 clamps the trigger's falling edge: about 1 dB of click and
  another 0.12 ms off the first lobe.
- The model's own solver aids went with the ramp: the thesis's 6.8 nF C0 at Q41's base (the envelope a sample late)
  and C40's alpha transform (the spike 7 % low). The trigger's step now falls between two samples, and the ngspice
  netlist (`tools/kick808/bassdrum.cir`) has the stage as drawn, the standard 1N4148, single-pole 3 MHz op-amps (an
  ideal one cannot pass the edge) and junction capacitances for its solver.

DECAY 0 was only the knob: its table stopped at VR6 = 25 kOhm. VR6 at 0, as the real pot reaches, gives the units'
short hit. Under 25 kOhm the last 20 dB are a slow low component's (C43's), so the time to -60 dB stays near 0.18 s
while the body shortens; the table places VR6 at 0 by its body's time to -40 dB scaled to 60 dB, 0.117 s.

The sigh was partly the matching: with the dull attack the decays were matched slightly long, and the sigh in this
circuit lengthens with the decay. What remained, C39 fitted within its tolerance as the user chose: at 1.2 times, the
top of it, the sigh settles slowest and, without being aimed at, the body's 2nd and 3rd harmonics land within 0.1 dB
of both units (they were 1.6 to 3.3 dB low). No single part within tolerance (C39, Q43's saturation current or
reverse gain, R159) keeps the pitch up at 80 to 130 ms as both units do; no published model of the 2SC945 gives its
reverse gain.

The TONE taper fitted again on both units after the fixes: about 7 % of VR5 left at mid-travel (b = 190 in the
curve 1 - (b^(1-t) - 1) / (b - 1); the old fit, made while the attack was dull, left 26 %). A textbook C taper leaves
about 10 %.

Measured again, model against unit (Fischer's, then Audiorealism's), medians over the five marks of the other knob:
- The click: -1.2 to +2.4 dB and -0.9 to +0.7 dB by TONE mark (before 3.2 to 6.2 dB weak at every mark). The units
  differ from each other by up to 3.1 dB.
- The level above 500 Hz in the first 20 ms: -0.7 to +2.1 dB and -1.2 to +0.3 dB.
- The first 20 ms's centroid: within 2 to 3 Hz from TONE 2.5 to 7.5; at TONE full 8 and 5 Hz low (before 18 to
  25 Hz), at TONE 0 7 and 3 Hz low. At both ends of the pot the model is about 1 dB darker than the units: left open.
- The first lobe: within 0.17 and 0.05 ms (before 0.2 to 0.4 ms long). The first peak against the body: within 0.2 dB.
- DECAY 0: -10 dB at 24.1 ms (units 24.5 and 26.2), -40 dB at 94 ms (units 77 and 98; before 131 to 135). The other
  marks within 1 to 6 % at -10 and -40 dB.
- The sigh's time constant: within 0.3 to 1.6 ms of Fischer's unit at every mark (before 11 to 30 % fast);
  Audiorealism's settles 3 to 11 ms slower than the model, the most at DECAY 10. Its depth: 0.2 semitones under
  Fischer's unit, 0.6 over Audiorealism's (the units differ by 0.8). The pitch at 100 ms within 0.03 to 0.11 semitones.
- The body's 2nd and 3rd harmonics within 0 to 1.7 dB from DECAY 2.5 up; at DECAY 0 1.5 and 3 to 4 dB low.

Punch Time's glide (the user's choice: past 6 ms, a glide, not the held beep) is a bend beside Q43: a second
transistor across R165 whose base capacitor the 1 ms trigger charges through a diode and drains through its own base
and 1 MOhm, its capacitor up and its charging resistor down with its size. Q43, the envelope and the first millisecond
stay stock at every size. In SPICE a first try on Q43's own base pulled Q42's collector from 11 to 5 V for the whole
envelope (halving the retrigger) and reshaped the first lobe even at the smallest capacitor; a charge-packet try still
reshaped it. The model's glide comes halfway down (in semitones) in 7.8 ms times Punch Time over 6 ms, up to some
0.65 s; no recording of an 808 so bent exists.

## The first comparison (2026-11-20)

An independent comparison of the circuit model (D236) with two real units, at matched settings and equal loudness.
The "spread" is the difference between the two units; no published part tolerances were found, so it rests on two
machines.

- Michael Fischer's 1994 set ([tidalcycles/sounds-tr808-fischer](https://github.com/tidalcycles/sounds-tr808-fischer),
  CC0 1.0): `bd8/BD<TONE><DECAY>.WAV`, unaccented.
- Audiorealism's TR-808 pack ([audiorealism.se](https://www.audiorealism.se/TR-808-SamplePack.html), free for
  personal use, not redistributed): a real 808 at 96 kHz from its own output, LEVEL 50 %, all accented.
- Skipped: TriSamples (unknown unit, processed, some Fischer copies), MusicRadar packs (effects), Echo Sound Works
  (hybrid), Splice and Sample Magic (sign-up), Loopmasters and Wave Alchemy (paid).

## Where the model differs, largest first

- The attack is too dull and the click too weak, at every TONE mark on both units.
  - The click (the peak above 500 Hz) is 3.2 to 6.2 dB down; the level above 500 Hz in the first 20 ms 1.4 to 4.5 dB.
  - The first 20 ms's centroid is 9 to 25 Hz low (TONE 10: 96 Hz against 121 and 118).
  - TONE moves the model's centroid a third as far (+6 to +7.5 Hz against +20 and +24); the click's rise over TONE is
    right (+7 dB against +8.7 and +7.5), only its level is off.
  - The first lobe is 0.2 to 0.4 ms longer. The units agree within 0.3 to 3 dB, 2 to 7 Hz and 0.05 ms.
- DECAY 0 is out of reach.
  - The model's shortest hit falls to -40 dB in 131 to 135 ms, against 77 (Fischer) and 98 (Audiorealism); to -10 dB
    in 40 ms against 24 to 26. The knob stops at VR6 = 25 kOhm.
  - The real short hit also falls 1.2 to 1.6 semitones as it dies (the model 0.4 to 0.6) and has stronger
    harmonics (the 2nd 7 to 8 dB, the 3rd 9 to 10 dB above the model's).
- The sigh settles 11 to 30 % too fast at every DECAY mark (DECAY 10: 57.8 ms against 65.2 and 57.8); the pitch at
  100 ms is 0.16 to 0.25 semitones low. Its depth is right: 2.3 to 2.6 semitones, between 2.6 to 3.0 and 2.0 to 2.1.
- The body's harmonics are low from DECAY 5 up (2nd 2.3 to 4.1 dB, 3rd 1.9 to 7.6 dB), on both units but within their
  raw spread (4.5 to 7 dB). DECAY 2.5 matches.
- The accent is unresolved: at 9 to 14 V the model's sigh is 1.6 to 3 times slower and up to 1.4 semitones deeper,
  its 2nd harmonic 10 to 14 dB up. The accented Audiorealism hits match the model at 4 V instead: either that unit's
  ACCENT knob was low, or the accent is too strong (ngspice says the schematic slows the sigh when accented).

## Where it matches

- The decay's shape: the times to -10, -20 and -60 dB within 1 to 4 % once -40 dB is matched, from DECAY 2.5 up.
- The settled tail's pitch, constant over DECAY and TONE within 0.03 Hz.
- The attack's jump: one half cycle at 2.35 to 2.5 times the tail for about 4 ms, then 1.25 to 1.3 times, between the
  two units.
- The body's peak at 17 to 19 ms, its 10 to 90 % rise in 12.5 to 13 ms, the polarity, TONE leaving decay and pitch
  alone, and the same numbers at 44.1, 48, 96 and 192 kHz.

## Punch Time's plateau (the beep)

- The model at Punch 24, Punch Time 20 ms at C2 holds 322 to 328 Hz for 14.4 ms, then 143 and 83 Hz; at 10 ms it
  holds 6.7 ms, at 40 ms 28 ms. Stock is a single half cycle of 3.3 ms.
- None of the 50 hits from both units, nor the 82 TriSamples hits that are not Fischer copies, shows a plateau: one
  half cycle at the peak pitch (112 to 130 Hz, 3.9 to 4.5 ms), the next already at 60 to 66 Hz. No recording of an
  808 with a larger C38 exists to check the bend.
- The user's choice (2026-11-20): past the stock 6 ms, Punch Time becomes a glide from the raised pitch onto the note.

## How they were matched

- Accent 0 (a 4 V trigger) for both; trigger = 4 + 10 x Accent x velocity volts.
- Tuning: +23.0 cents for Fischer's (tail 49.63 Hz), -32.0 for Audiorealism's (48.07 Hz).
- Decay set to each mark's median time to -40 dB. The units' real times to -60 dB: Fischer's 0.154, 0.342, 1.20,
  1.99 and over 3 s; Audiorealism's 0.19, 0.53, 0.82, 1.16 and 1.68 s. Their DECAY ranges differ about twofold.
- Against Werner's DAFx-14 Fig. 11 (about 56 falling to 49.5 Hz): the model is 56.0 Hz at 25 ms on a 49.6 Hz tail,
  Fischer's 57.2, Audiorealism's 53.0 on 48.1.

## Against the first comparison (D236)

It agrees that the first 20 ms are duller (here 11 to 21 %, on both units) and that the sigh is within about 1 Hz on
Fischer's. It differs: the dullness grows with TONE and the click is 3 to 6 dB weak; Fischer's settled tail is
49.63 Hz at every mark (the 50.3 Hz before was measured inside the slower real sigh); Fischer's DECAY 0 reaches
-60 dB in 0.15 s, under the model's floor of 0.20 s; "rings 25 to 35 % longer" holds only for a linear knob mapping
on one unit.

Not measured: an accented and unaccented pair from one unit, Fischer's DECAY 10 to -60 dB (the files end at 3 s,
-53 dB), hit-to-hit variation, a modified C38, the recording chains, VR6 under 25 kOhm.
