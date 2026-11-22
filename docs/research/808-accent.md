# The 808's accent: what the recordings were struck at, and what the AC LEVEL's top does (2026-11-22)

TODO 33's accent check, the user's idea: deduce each recording set's accent from what the accent does that no
recording gain can, on voices we trust, then judge the gaps left open in the three families at it.

The model's trigger was 4 V unaccented and 4 + 10 x Accent x velocity volts accented, page 6's 4 to 14 V; since
D258 it is 4 + 7.5 x Accent x velocity, a top of 11.5 V (below).

## The sources

- Fischer's and Audiorealism's sets, as in the families' write-ups. Fischer's says nothing of the accent;
  Audiorealism's readme has every hit accented, the AC LEVEL not given.
- A real 808 at three accent levels: 808 From Mars (paid, not bought here), measured by the gf180-parasynth project,
  whose per-file results are public: the three toms at no accent, accent and "more accent", the congas at no accent
  and accent, each at eleven TUNING marks
  ([PR 110](https://github.com/2AMLogic/gf180-parasynth/pull/110),
  [its results](https://github.com/2AMLogic/gf180-parasynth/blob/0bdf44f/docs/tom-pitch-drop-results.json)).
  Its files are each level-normalised, so they carry the pitch drop but not the amplitude.
- Page 14's table ([the service notes](https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=14)), which
  says how it was read: the AC LEVEL fully counter-clockwise for "normal", then fully clockwise for "accent", the
  voice's LEVEL fully clockwise, its other knobs at 12 o'clock. Each voice's accented over normal amplitude is free of
  any gain.
- Checked and set aside: a "Roland TR-808 Rhythm Composer Samples" set the user found is Fischer's, all 116 files
  identical.

## How

- The toms' and congas' pitch drop is the germanium pair's: it grows with the drum's swing, which the trigger sets,
  and no gain moves it. Measured as gf180-parasynth measures it (its `tom_pitch_probe.py`, read and reimplemented):
  each full period's frequency from same-direction zero crossings behind a Schmitt trigger, unfiltered, over the
  settled frequency; the first period's ratio, the second's, and an exponential fitted back to the onset. Validated
  on a resonator with a known sweep: x1.2 read as x1.19999.
- The model's toms and congas rendered through CoolSynthRender at the eleven TUNING marks and 4, 6, 8, 10, 12 and
  14 V, as recordings (-6 dBFS, a -90 dBFS floor, 24-bit steps), measured alike. Each recorded hit then reads as the
  trigger at which the model, at the hit's own settled pitch, drops as far.
- The bass drum's accent-bound measures (the first half cycles' pitch, the sigh's time, the 2nd and 3rd harmonics, the
  body against the click), at three DECAY marks per unit, against the model at 4 to 14 V.
- Every voice's accented over normal Vpp through its generator, the LEVEL at full and the other knobs at 12 o'clock,
  at 4 to 14 V, against page 14's.

## What the recordings were struck at

The drop's reading, the fitted onset ratio's median (the first and second period's agree within about 1 V but where
noted):

- 808 From Mars, the toms: no accent 4.7 V, accent 8.2 V, more accent 11.7 V (quartiles 11.5 to 12.0). Each level
  reads the same on all three toms and across all eleven TUNING marks (the low tom's more accent 11.0 to 11.6 V from
  82 to 101 Hz), so the model's drop follows the TUNING as the real one does.
- 808 From Mars, the congas: no accent 4 V or under, accent 11.7 V (11.1 to 13.0). Recorded at two levels only, their
  "accent" reads as the toms' "more accent": both the AC LEVEL's top, as the reading has it.
- Fischer's: the toms 4.9 V by the first period, 4.2 V by the second; the congas 4.3 to 4.5 V. Unaccented.
- Audiorealism's: the toms 4.6 and 4.5 V, the congas 4.4 V (but its low conga, 6 to 9 V). Accented with the AC LEVEL
  at or near its bottom: as good as unaccented.
- The bass drum agrees: both units' half cycles, sigh times and harmonics fit 4 to 5 V at every DECAY mark, as D251
  found for Audiorealism's before its fixes.

So the units read 4.5 to 5 V unaccented where the model has 4 V: their toms drop a little more (the low tom's first
period x1.08 at its top against the model's x1.063).

## What the AC LEVEL's top does

Page 14's accented over normal amplitude is met by the model at (the page's ratio in brackets):

- Bass drum 12.2 V (2.86), snare 11.0 (3.33), claves 10.8 (3.20).
- Low conga 10.6, low tom 10.3, mid conga 10.6, high tom 10.7 (all 3.43 or 3.33); mid tom 11.5 (3.67), high conga 11.6
  (3.43).
- Rim shot 9.1 (3.33).
- The metal voices, the maracas and the clap are not read here: their renders were cut short by the engine's overload
  rule (below). The cowbell's accented 6.0 Vpp against the page's 12 stands from D255 (IC2b's swing).

Two measures that share nothing, a real 808's drop at its top accent and page 14's amplitude ratios, put the AC
LEVEL's top at about 11.5 V in these circuits, not 14. At 11.5 V the drums lie within -0.5 to +1.2 dB of page 14's
ratio (the rim shot +2.4); at 14 V they run +0.8 to +3.3 dB over, the gap D253 and D257 left open.

Why the real top lies lower is not settled here: the TRIG line under the load of every voice's input, or the AC LEVEL
pot's own range, would both do it; the voices themselves are not the cause, since the drop and the amplitude agree.

## Chosen (the user, D258)

- The Accent knob's top at 11.5 V in the 808 Drums, the 808 Snare & Clap, the 808 Metal and the Kick's 808,
  `Tr808BassDrum::triggerFor`. The Kick's full Accent over none is 2.42 at its own defaults, the 808 Drums' bass
  drum at 12 o'clock 2.69, page 14 2.86 (both against the 4 V bottom this was read at).
- The bottom first stayed at 4 V; raised to 4.7 V later (below, the rim shot), so the trigger is 4.7 + 6.8 x Accent
  x velocity volts. Succession 808's Accent 0.6 became 0.78, its top at full velocity the 10 V it was passed at, its
  softest hits 0.7 V hotter; the Kick's level calibration stays.
- The earlier fits, checked against the units' 4.5 to 5 V: none was a level effect. Between 4 and 4.7 V the toms'
  and congas' decays move 1.2 % at most (D252's resistors moved them 10 to 30 %), the claves' 1 %, the rim shot over
  the claves 0.6 dB (3.9 against the units' 2.1 and 4.9); the clap's claps not at all from 4 to 8 V (C140, C144).

## The gaps, judged at it

- The open hat's decay, 6 to 32 % longer than both units across OH DECAY (D255): both units are as good as
  unaccented, and the model is already long at 4 V, so the accent does not explain it. Fitted (the user): C60 8 uF
  and R134 82 kOhm, swept in the test runner on the circuits themselves (`COOLSYNTH_808_OH_FIT`), the ngspice
  netlist and its open hat references with them (`808-metal.md`).
- The clap's late tail, 4 to 12 dB under both units after 120 ms (D257): the same; at the units' 4.5 to 5 V the tail
  is still under. Fitted (the user): C139 39 nF, swept through the generator in the test runner
  (`COOLSYNTH_808_CLAP_FIT`), the ngspice netlist and its clap references with it (`808-snare-clap.md`).
- The accented amplitude over page 14 (D253, D257): the AC LEVEL's top, above.

## The rim shot, and the bottom raised to 4.7 V

The rim shot's two rings, by trigger, against both units (5 ms frames, dB under the 1.7 kHz band's first frame; its
decay to a tenth by a 0.5 ms rms envelope):

- The units: decay 9.71 to 9.75 ms; the 455 Hz ring at 10 to 15 ms -30.9 dB (Fischer) and -23 (Audiorealism, three
  shots within 0.4 dB); the 1.7 kHz ring at 5 to 10 ms -19.0 and -18.0.
- The model at 4 V: 8.48 ms, -36.0, -14.9. At 4.71 V: 9.58 ms, -27.2, -13.1. At 5 V: 9.76 ms, -25.3, -12.5.

So at the units' own 4.7 V the rim shot's decay and its 455 Hz ring are theirs with no fit, and most of the 6 dB
"off balance" was the trigger: the swing VCA makes the rim shot the voice where the bottom shows. What remains is the
1.7 kHz ring falling some 5 dB too slowly over its first 10 ms.

The user's choice (options: a 4.7 V bottom; keep 4 V and fit the rim shot's envelope and balance; keep 4 V and leave
it): 4.7 V. The fits made at 4 V move little at it, measured: the open hat's decays 74, 207, 339, 460, 537 ms
against 72, 207, 336, 452, 533 (within 2 %, inside the units' range), the clap's tail 0.6 to 1 dB louder (still
within some 2 dB of the units), the toms' and congas' decays under 1.2 %.

Then the 1.7 kHz ring, at 4.7 V. IC20 rings some 3 % sharp in both SW11 positions (the rim shot 1774 Hz against the
units' 1723 to 1726, the claves 2492 against 2410 to 2422), and its first 5 ms drop 12.0 dB against their 17.0 to
17.7. Swept in the test runner on the circuit (`COOLSYNTH_808_RIM_FIT`): R308 and R312 set IC20's pitch and Q
together (its pitch with their product, its Q with their ratio), and R320 the mix.

- R312 alone at 1.07 kOhm: both pitches on the units (1725 and 2412 Hz), the rings' balance within 0.5 dB (-15.1
  against -14.3 to -14.7), the claves' decay unchanged (22.5 ms against 22.0 and 22.3); the first 5 ms's drop stays
  11.7 dB.
- R308 680 kOhm, R312 1.3 kOhm and R320 16.5 kOhm: the rim shot's pitch, drop and balance all on the units, but a lower
  Q halves the claves' decay (10.9 ms), so D252's R313 would need fitting again.

The user's choice (options: R312 1.07 kOhm; the four parts; keep): R312. `tools/tr808/rimclave.cir` takes it and its
rim shot and claves references were rendered again.

## Found on the way

- The rim shot's two rings: from 5 to 10 ms the 1.7 kHz ring is some 3.5 dB stronger and the 455 Hz ring some 3 dB
  weaker than both units' (Fischer's and Audiorealism's agree within 1 dB), so the balance between them lies some 6 dB
  toward IC20; ngspice draws it as the model does.
- The claves carry a low thump, lines from 51 to 141 Hz some 37 dB under the ring, that Fischer's do not (68 dB
  under). Not the circuit's (alone it is ngspice's, 93 dB under) but the generator's: the rim shot's rest was solved
  in the rim shot's position only, and the claves take the mixer's input at another DC, so every claves hit
  recharged the mixer's coupling. Fixed: each SW11 position has its own rest, which a switch at rest restores; the
  thump is 97 dB under and the claves' peak 2.3 dB lower, the rim shot now 3.65 dB over them (D252's 3.3 on the
  circuit; the units 2.1 and 4.9). The congas' switch was checked alike: their low end is ngspice's.
- The engine's overload rule (D222) cuts Kit-mode voices. CoolSynthRender reads the load as a live host does (it
  never says it renders offline), and a Kit voice counts as released from its strike, so under load the whole kit
  gives way first: with eight renders at once and a build running, claps stopped after 7 to 445 ms by the run. The
  808 Metal is cut at 96 kHz even alone on a quiet machine: the block it is struck in takes 3.4 ms of its 2.67 (the
  Debug build), so an open hat stops after 6 ms where at 48 kHz it sounds its 446. One render crashed (an access
  violation) under load. The tom and conga grid (396 renders), the rim shot's and the Kick's were not cut
  (checked render by render); the fits run on the circuits in the test runner, which the rule does not reach.
