# The Kick's 808 against two real TR-808s (2026-11-20)

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
