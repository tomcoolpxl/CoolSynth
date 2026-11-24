# TR-808 device models: what the fuller ones change (2026-11-24)

The ngspice references for the 808 voices (`external/coolsynth-engine/tools/tr808/*.cir` and
`external/coolsynth-engine/tools/kick808/bassdrum.cir`) use the same simplified devices as the engine, so the SPICE check proves the solver, not the devices. This note
measures, in ngspice only, how much each simplification changes the sound: each is swapped for a fuller model, the
same hits are rendered, and the result is compared with today's references. The engine's C++ is unchanged.

## Method

- `tools/research/808-device-models/measure.py` builds each variant from the netlists, renders it with ngspice 47
  (`tools/ngspice`, D235) at 96 kHz and the netlists' own solver settings, and measures it against today's netlist
  rendered alike. Run as `python tools/research/808-device-models/measure.py --work <folder>`, then `--summary`.
- Triggers: unaccented 4.7 V and full accent 11.5 V (D258's 4.7 + 6.8 V). Knobs: the netlists' defaults and their
  extremes (bass drum Decay 0 and 1, Tone 0; toms' Tuning 0 and 1, Level 1; the rim shot at Level 1; snare Tone
  and Snappy 0 and 1; cymbal Tone and Decay 0 and 1; open hat Decay 0 and 1; each metal level at 1, accented). 42 cases.
- The noise bus as two test sines of 130 mV rms together (snare 1.7 and 5.3 kHz, clap 1.1 and 2.3 kHz), so that each
  pair of renders differs only by the model; the noise model itself is not changed here.
- Measures, as the tests take them: the rms difference per stage over the reference's rms (each about its resting
  value, stages that stay within 1 mV of rest left out); the peak and rms level; third-octave bands from 25 Hz to
  20 kHz (the largest change among bands within 30 dB of the loudest); the decay to -20 dB of a 5 ms rms envelope (and
  to -40 dB); pitch at each resonator's own node by its zero crossings (at the output a VCA's changing envelope reads as
  a pitch change). Then the measures each fitted part was fitted to.
- When ngspice stopped a run ("timestep too small", mostly the metal board with the vendor op-amps or the finite
  Schmitt edges), the variant and its reference were both rendered again with looser settings (reltol 1e-4, vntol
  1 uV, abstol 0.1 nA), and compared with each other.

What counts as audible here: a level change of about 0.5 to 1 dB; pitch near 0.2 % (some 3.5 cents) around 1 kHz,
coarser at low frequencies ([the frequency difference limen, after Wier, Jesteadt and Green 1977](https://ccrma.stanford.edu/~malcolm/correlograms/text/33%20Frequency%20Difference%20Limen%20Or%20Jnd.html));
a decay change of 5 %, ISO 3382-1's figure for decay times, which listening tests have since put higher, 6 to 39 %
([Foteinou and Murphy](https://eprints.whiterose.ac.uk/75128/1/Foteinou_Murphy_58.pdf)). A change moves a fitted part
when it carries the part's own measure outside the window it was fitted into.

## The models

| Device | Today | Fuller | Source |
|---|---|---|---|
| Op-amps (uPC4558) | 1 mS into 100 MOhm and 53 pF (one pole, 3 MHz), clamp diodes 0.55 V inside +-14 V, an ideal follower | TI's RC4558 macromodel as published (1.7 V/us, +-13 V, 25 mA limit, 125 + 125 ohm output); and the same retuned to the uPC4558: 1.0 V/us, +-14 V, 60 nA bias, 3 MHz kept | [TI SLOJ053](https://www.ti.com/lit/zip/SLOJ053) (a 1989 Boyle macromodel, kept in `tools/references/rc4558`); [Renesas uPC4558](https://www.renesas.com/en/products/upc4558), [data sheet](https://www.renesas.com/en/document/dst/upc258-upc4558-datasheet) |
| Transistors (2SC945(P), 2SA733(P)) | IS 10 fA, BF 300, BR 4, CJE 8 pF, CJC 4 pF | Kaz-electro's measured 2SC1815-GR (hFE 200 to 400, the 2SC945(P)'s range) and -Y cards, Gummel-Poon with VAF 526 V, RB 34 ohm, IKF 0.29 A, ISE/NE, charge; the PNPs the same card, as today | [Kaz-electro, March 2022](https://kaz-electro.jp/lab/2sc1815-spice/), one part a rank; no vendor model was found (Toshiba published none; the widely copied "Toshiba" card has VAF 6, which [kdenki](https://blog.kdenki.com/2020/01/spice.html) notes is wrong) |
| Followers Q44, Q25, Q26, Q31 | today's transistor in the netlists; an ideal buffer in the engine | the engine's ideal buffer; the 2SC1815-GR | as above |
| HD14584 Schmitt | edges exact (100 ns ramps in the netlist) at VT+ 2.7, VT- 2.1 V | 455 ohm output and 100 ns edges (typical), 784 ohm and 200 ns (worst); thresholds at their minimum (1.9, 1.6 V) and maximum (3.4, 3.15 V); hysteresis at its maximum (3.1, 2.1 V) and minimum (2.65, 2.4 V) | Hitachi's HD14584B data sheet (`tools/references/tr808-metal`); [onsemi MC14584B](https://www.onsemi.com/pdf/datasheet/mc14584b-d.pdf) |

The retuned op-amp, checked alone: 1.1 V/us and +-13.9 V (TI's as published: 1.9 V/us, +-13.0 V).

60 of the 394 comparisons needed the looser solver settings, all on the metal board but one (the clap with every
model swapped); the looser settings alone move today's netlist by 0.4 % rms and 0.002 dB at most. The full tables
are in `tools/research/808-device-models/results.md`.

## What each changes

The largest change over each voice's cases, unaccented and accented, default knobs and extremes. Levels and bands in
dB, decays to -20 dB in %, pitch in cents.

| Voice | Op-amps: uPC4558 (TI RC4558) | Transistors: 2SC1815-GR | Followers ideal, as the engine | Schmitt edges (worst) |
|---|---|---|---|---|
| Bass drum | level 0.1; sigh -3.5 c (-8.6); 2nd and 3rd harmonics -0.5 (-1.2) | sigh -7 c, -20 accented, -61 at Decay 1 accented; 2nd, 3rd harmonics +1.2, -0.9, to -3.4, -4.7 at Decay 1; 62 Hz band -0.8, -8.8 at Decay 1; decay +1, +9 accented; level 0.2 | +0.23 (Q44) | - |
| Toms, congas | level 0.1, bands 0.3; congas' decay +1.4 to +1.6; pitch 0.7 c | level 0.2, pitch 0.4 c, decay 0.1 | - | - |
| Rim shot | level 0.04 (bands 1.1 at 8 kHz accented); 1.7 kHz ring +0.8 c | decay +17 (11.8 to 13.9 ms); 630 Hz -4.8, 1.26 kHz +5.7 accented; the rings' pitch unchanged | - | - |
| Claves | -0.2, -0.7 accented; decay -2.7 (-4.2); ring +1.6 c | unchanged | - | - |
| Snare | -0.3 to -0.6; the lower shell's band (157 Hz) -1.2, -2.0 accented; decay to -4.8 at Snappy 0 | decay +3.5; the snappy's band (6.3 kHz) +1.2 | - | - |
| Clap, maracas | 0.01 | clap tail (150 to 800 ms) -3.3, -1.9 accented, 1 kHz band -2.9; maracas -0.6, -0.9 accented | - | - |
| Cowbell | 0.02 | decay +2.8, 1 kHz +0.4 | 0.00 | 0.2 (0.4, decay -3) |
| Cymbal | 0.06 | level +0.2, 12.7 kHz +0.9 | 0.01 | 0.1 (0.2) |
| Open hat | 0.05 | level -0.2; decay -0.7, -2.0, -3.6 at Decay 0, 1/2, 1 | +0.34, 10 kHz +0.6 | 0.1 (0.2) |
| Closed hat | 0.05 | decay +5.4, 4 kHz +0.8 | +0.37 to +0.48, 12.7 kHz +0.7 | 0.1 (0.2) |

Where they come from:

- **Op-amps.** The uPC4558's 1 V/us is the cause where there is any change. The snare's IC14a takes the trigger's edge
  through C57 and slews, so the lower shell gets less of the kick; the claves' IC20 runs at the rails accented and
  rings near oscillation (R313), so its decay follows the op-amp's phase. Each model's bias current moves the resting
  voltages by 30 to 140 mV across the 820k to 1M resistors, with no audible effect. TI's model as published (1.7 V/us,
  +-13 V) changes the same places, the bass drum more and the snare less.
- **Transistors.** Splitting the GR card into parts, nearly all of it is the low-current recombination (ISE, NE: the
  current gain falling at small currents). That alone gives the bass drum's sigh -8.5, -30, -69 c, the rim shot's decay
  +19 %, the clap's tail -4.3 dB, the snare's decay +7 %. VAF, RB and IKF alone (the Early effect, base resistance and
  high-current knee the task named) change no voice by more than 0.35 dB, 1 % or 0.4 c. IS and BF alone: the sigh
  +5.8 c; the charge (capacitances, TF, TR) alone: 1.8 c and 0.5 dB at most. The Y rank (hFE 120 to 240) changes the same places
  by some 1.5 times as much (the sigh -86 c at Decay 1 accented, the rim shot +26 %, the clap's tail -4.0 dB).
- **Followers.** A real follower's gain is a little under one: the engine's ideal ones are 0.2 to 0.5 dB louder than
  the references, which the tests already carry. The GR part as follower against today's: 0.1 dB at most.
- **Schmitt edges.** 455 Ohm and 100 ns change 0.2 dB at most; the data sheet's worst (200 ns, about 800 Ohm) 0.4 dB
  and the cowbell's decay 3 %.

A noise-like decay measured to -20 dB moves in steps: the cymbal at TONE 0 reads +6.4 % in three unrelated variants
(`808-metal.md` found some 15 % from the oscillators' phases alone), so changes of that size on the cymbal are not
read as real.

### The Schmitt's thresholds

The cowbell's two oscillators are trimmed (TM1, TM2) and stay at 554 and 825 Hz; the four fixed ones follow the
thresholds, the six together, one chip:

| Thresholds | VT+, VT- (V) | The four fixed oscillators (Hz) | Duty |
|---|---|---|---|
| Typical (today) | 2.7, 2.1 | 522.7, 304.4, 369.6, 205.3 | 48.0 % |
| Both at their minimum | 1.9, 1.6 | 955.7, 556.6, 675.8, 375.5 | 35.0 % |
| Both at their maximum | 3.4, 3.15 | 1139.8, 663.8, 806.0, 447.8 | 65.5 % |
| Hysteresis 1.0 V (maximum) | 3.1, 2.1 | 310.9, 181.0, 219.8, 122.1 | 52.1 % |
| Hysteresis 0.25 V (onsemi's minimum) | 2.65, 2.4 | 1261.4, 734.6, 892.0, 495.6 | 50.5 % |
| Hysteresis 0.10 V (Hitachi's minimum, not rendered) | 2.45, 2.35 | 3151, 1835, 2228, 1238 | 48.0 % |

Rendered, the corners change the cymbal and hats by 1 to 3 dB in level, up to 16 dB in a band, their decays by -32
to +13 %; at the minimum the cowbell's duty of 35 % drops it 7.7 dB and shortens it 45 %. The period over RC is
ln((5 - VT-) / (5 - VT+)) + ln(VT+ / VT-), set mostly by the hysteresis, so the data sheet's spread is an octave
either way. At the typical thresholds the metal voices already lie with both units (`808-metal.md`: the closed hat's
bands within 1 to 4 dB, the decays near the units'); the four fixed oscillators' own frequencies on the units are
not measured here. The corners describe other chips, not a fuller model of the same one.

## The fitted parts

| Part | Fitted to (window) | Op-amps: uPC4558 (TI) | Transistors: GR (Y) | Followers, Schmitt edges |
|---|---|---|---|---|
| C39 (bass drum) | the sigh, at 1.2 times (the top of its tolerance) the slowest; the 2nd and 3rd harmonics within 0.1 dB of both units | sigh -3.5 c (-8.6); harmonics -0.5 dB (-1.2): **moves it** | sigh -7 c; harmonics +1.2, -0.9 dB (+2.6, -0.6): **moves it** | 0 |
| R230, R259, R286 (toms) | each pair's tom and conga decays to a tenth against both units (low conga 169 ms, the units 166 and 180; high conga 78, the units 76 and 81) | congas +1.4 to +1.6 %, still between the units | 0 | - |
| C60, R134 (open hat) | five decay marks, log error 4.6 % (339 ms at Decay 1/2, the units 339 to 344; 550 at Decay 1, the units 529 to 531) | 0.3 % | -0.7, -2.0, -3.6 % (-5.4 at Decay 1): within the fit's own error; the middle mark falls 2 % under the units, the longest onto them | 0.4 % |
| C139 (clap) | the tail within about 2 dB of both units (rms 1.7 dB) | 0 | -3.3 dB (-4.0): **moves it** | - |
| R312 (rim shot, claves) | the rings at 1723 to 1726 Hz and 2410 to 2422 Hz | +0.8 c, +1.6 c: still within | 0 | - |
| R313 (claves) | the decay to a tenth, 24.1 ms against 23.8 and 24.2 | -2.7 % (-4.2): **moves it** | 0 | - |
| R324 (rim shot) | the rim shot 3.3 dB over the claves, the units 2.1 and 4.9 | +0.16 dB | +0.25 dB (+0.30) | - |

Not a fitted part, but outside the units: with the GR card the rim shot's decay grows 17 %, where at 4.7 V it matched
both units unfitted (D258).

## Audible or not

- Audible: the transistors' low-current gain on the bass drum's sigh (7 to 61 cents) and harmonics (1 to 5 dB), the
  rim shot's decay (17 %), the clap's tail (3.3 dB) and the maracas (0.6 to 0.9 dB); the op-amps' slew on the snare's
  lower shell (1.2 to 2 dB in its band) and the claves (0.7 dB accented, 2.7 % decay); the Schmitt's thresholds, by
  far, if a chip sat away from the typical.
- At the threshold: the transistors on the snare (3.5 % decay, 1.2 dB at 6.3 kHz) and the hats and cowbell (3 to 5 %,
  under 1 dB); the followers on the hats (0.3 to 0.5 dB); the op-amps on the bass drum (0.5 dB harmonics, 3.5 c).
- Not audible: VAF, RB and IKF; the op-amps on the toms, clap and metal board; the followers on the bass drum and the
  cymbal; the Schmitt's finite edges.

## Limits

- One measured 2SC1815 a rank, not the 808's 2SC945(P) and 2SA733(P); no vendor model was found for either. The
  low-current gain that moves these voices varies from part to part and between makers' dies, so its direction is
  plausible but its size on a given 808 is not known. The PNPs are the NPN card mirrored, as today.
- TI's macromodel is a 1989 Boyle model: its swing does not fall with load and it has no input offset; the uPC4558
  retune changes its tail current, emitter and tail resistors, swing sources and input gain to Renesas's typicals.
- 25 degrees only; one oscillator phasing on the metal board; the noise bus as test sines, its own model unchanged.
- ngspice only: nothing here is checked against the two units, and the engine is unchanged.
