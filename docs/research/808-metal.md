# 808 Metal: the cowbell, cymbal and hi-hats against SPICE, two real units and page 14 (2026-11-22)

The 808 Metal generator (D254, D255) holds the TR-808's cowbell, cymbal, open and closed hi-hat, read from the
service notes' voicing board (p. 13; the circuit text on p. 6, the table on p. 14). They share one bank of six
square-wave oscillators and IC3's two band-passes. Each is checked in three steps, as the 808 Drums were.

- Stage by stage against ngspice: `tools/tr808/metal.cir`, rendered by `tools/tr808/render.py` with `--hit` and
  `--phases`; the references are in `tests/data/tr808` (cb, cy, oh, ch, each unaccented and at 14 V, and ohch, the
  open hat cut by the closed one), the test is `tests/V3Tr808MetalTests.cpp`.
- Against two real units' recordings: Fischer's (`tools/references/fischer-808`: the cymbal at 25 TONE and DECAY
  settings, the open hat at five, one cowbell, one closed hat) and Audiorealism's (`tools/references/audiorealism-808`:
  eight cowbell and eight closed hat shots, the cymbal's 25, the open hat's five).
- Against page 14's table: decay to a tenth, amplitude at full level, normal and accented.

## The circuits as drawn

The bank is an HD14584's six Schmitt-trigger inverters, each with a resistor from its output to its input and a
capacitor to ground. Werner, Abel and Smith take the datasheet's typical thresholds at 5 V, 2.7 and 2.1 V
([ICMC-SMC 2014](https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf); and for the cowbell, "More Cowbell",
AES 137th Convention, 2014, paper 9207): a duty of 47.98 % and 205.3, 304.4, 369.6 and 522.7 Hz as drawn. Hitachi's
HD14584B datasheet gives them so at 25 °C (VT+ 1.8 to 3.4 V, VT- 1.6 to 3.2 V, typically 2.7 and 2.1), the output 0
or 5 V with edges of some 100 ns; its output resistance, about 450 Ohm, is left out here and in the reference (1 to
2 % at most). The papers' own copies and the datasheet are in `tools/references/tr808-metal` (git-ignored). The model's oscillators follow those formulas, their edges exact in time and band-limited.
All six feed R53 through 120 kOhm each; IC3 band-passes the mix twice, each a bridged-T in an op-amp's feedback, the
high band peaking at 7.09 kHz, the low at 3.43 kHz.

- The trigger: IC2a narrows every metal hit's TRIG to some 7 to 14 V (R19, and R18 clamped by D1 into R20, a gain of
  31): 6.5 V at an unaccented 4 V, 13.9 V at 14 V. Each instrument's PNP switch (Q5 to Q8) passes it while its data
  line holds Q1 to Q4 on.
- The cowbell: Q5, R122 and D2 charge C9; R82 and C34 take part of the charge at once, the abrupt fall after the
  attack the text describes. Two swing VCAs: the cowbell's own oscillators through R62, R63 and C32 (C33) onto Q15
  (Q14), which pull R28's (R29's) end down through D3 (D4) while their oscillator is high; IC2b's band-pass (870 Hz)
  mixes them, then CB LEVEL as a divider into IC6a's follower. The base current matters: Werner, Abel and Smith find
  the base network passes the square 28.6 dB down, not the 24.1 dB its resistors alone give; the model solves the
  transistors, so it has that, as the reference does.
- The cymbal: Q6 into Q19, whose emitter charges three envelopes through D6, D7, D8. The top band: the high band
  through C42 onto Q16, a swing VCA under R94 from C38, IC5a's third-order high-pass. The middle: Q17 under R90 and
  R91 (C40, C39, and Q20 following C41, which drains through R93 and CY DECAY), IC5b's high-pass. The low: the low band
  through Q18 under R104 (C45), Q25's Sallen-Key. CY TONE grounds its wiper between the top band and the others; IC6b
  mixes them with CY LEVEL in its feedback.
- The open hat: Q7 into Q24, which charges C62, and into Q21, which empties C60. C62 drains slowly through R139 until
  C60, charging again through R132 and OH DECAY, turns Q22 on, which drains C62 through R134. Q29 follows C62 into the
  swing VCA Q27 on the high band, then Q26's resonant high-pass (8.2 kHz, Q 2.5) and IC7a with OH LEVEL.
- The closed hat: Q8 into Q28, which charges C63; C63 feeds Q30's VCA (then Q31's high-pass at 12.2 kHz) and, through
  R173, Q23, which drains the open hat's C62: the closed hat cuts the open one.

The bank, IC3 and the five swing VCAs on IC3's bands run while any instrument sounds (the user, D255): each VCA's
coupling capacitor follows the bands as on the machine. A start from silence draws the oscillators' places from the
voice's seeded random numbers, runs IC3 for 2 ms and starts each coupling capacitor where the running bank holds it
(measured on the model: -0.390 V for C32 and C33, -0.526 V on the high band, -0.514 V on the low; ngspice -0.524 and
-0.515). Each envelope runs with its trigger's parts only around a hit (the pulse, the hold and 0.25 ms after,
D262), then without them, the state passed across.

## Against SPICE

Three things in the first model were wrong, all found by the references.

- Devices in series with nothing else at the node between them: Q5's collector into R122 and D2, Q15 and Q14 into
  D3 and D4, Q18 into D12, Q19's emitter feeding only D6 to D8. Their currents are then one, the root's matrix is
  singular and its solve meaningless (the cowbell's envelope charged twenty times too slowly; the cymbal diverged).
  The transistors' own junction capacitances, as the references have them (4 pF collector, 8 pF emitter), untie
  them; `CircuitWdf::isWellPosed` now says so and the tests check every circuit.
- The netlist's first version named the main board's trigger transistor's base `nb31`, the same as the closed hat's
  follower Q31's: the two were shorted in SPICE.
- The sample rate. At 96 kHz (a 48 kHz host) the swing VCAs switch when the band crosses their base's knee, a
  fraction of a sample apart from where SPICE has it, and the high-passes behind them reach 8 to 14 kHz, where the
  bilinear transform moves a peak 2 to 6 % down. Each resonant filter's capacitors are prewarped at its peak.

Over a hit's first 15 ms, the model's output against SPICE's in third-octave bands from 1 to 20 kHz (rms of the
differences): at 96 kHz the cowbell 0.4 dB, the cymbal 0.8 to 0.9 dB (some 0.8 dB quiet in every band), the open hat
0.8 dB, the closed hat 1.1 dB; the same circuits at 192 kHz 0.1, 0.2, 0.2 and 0.3 dB. The user kept the voice rate
(D255): twice it would roughly double the price.

The tests over 30 ms: IC2a within 0.3 %; IC3's mix, high and low band within 2.2, 4.1 and 0.9 %; the cowbell within 2
% at its envelope, band and output, 4 to 6 % at its gates; every envelope within 3 % (Q22's timer 4.5 % at 14 V);
the cymbal's low band 0.7 %, its top and middle gates 3 to 4 % (12 to 26 % at 14 V), its output 14 to 25 % sample by
sample but within 0.8 dB in level and 0.9 dB in its bands; the hats' gates 15 to 32 %, their outputs within 0.5 dB in
level and 0.9 dB in their bands. The closed hat cuts the open one by 30 dB from 100 ms after it.

## Against the two units and page 14

The cowbell's two tones: Audiorealism's eight shots at 550 and 825 Hz, Fischer's at 558 and 824 Hz, the service
notes' trimmer setting 540 and 800 Hz. The user chose the units' tuning (D255): TM1 and TM2 at 554 and 825 Hz, the
notes' within reach on CB Pitch. Sound on Sound's often-quoted 587 and 845 Hz were found by ear on a CR-8000
([Synth Secrets](https://www.soundonsound.com/techniques/synthesizing-cowbells-claves)), whose own service notes set
its cowbell's trimmers to 1.25 and 1.8 ms a cycle, 800 and 556 Hz (CR-5000/8000 service notes, p. 12): that unit ran
some 5 % sharp, as both 808s here run 2 to 3 % sharp. The same page asks the technician to tailor the cymbal
oscillators' R and C per unit, their neighbours 1.1 to 1.4 apart, to avoid a beating cymbal. The harmonics' levels
agree within 3 dB but for the lower tone's second harmonic, some 5 dB weaker in the model.

Decays to a tenth, unaccented (model; Fischer, Audiorealism; page 14):

- Cowbell: 106 to 126 ms over four oscillator phasings (147 accented); 83 and 106 to 129; page 50.
- Closed hat: 39 to 43 ms (49 accented); 45 and 42 to 47; page 50.
- Open hat at OH DECAY 0, 25, 50, 75, 100 %: as drawn 89, 253, 403, 533, 551 ms; 77, 189, 339, 482, 529 and 70, 216,
  344, 471, 531; page 90, 450, 600 at short, mid and long. Steady across phasings (405 to 407 ms at 50 %), so 6 to 32 %
  long. Fitted (D258): C60 8 uF (printed 10) and R134 82 kOhm (printed 100k), 76, 209, 339, 457, 550 ms (the open hat
  alone at render.py's phases: as drawn 93, 251, 406, 541, 568); C60 alone at 8 uF left the shortest 14 % long.
- Cymbal at TONE and DECAY 50 %: 493 to 656 ms over five phasings (1070 accented); 527 and 546; page 800. Across the
  grid the model lies with the units, longer at the dark end (TONE 0: 339, 656, 981, 1188, 1400 ms against 373 to
  395, 584 to 764, 848 to 881, 1067 to 1397, 1193 to 1628), a little shorter at the bright end (TONE 100: 168 to 210
  against 179 to 246). A noise-like decay measured to a tenth moves some 15 % with the oscillators' places alone.

The spectra over a hit's first 100 to 300 ms, against both units, third-octave bands from 500 Hz to 16 kHz:

- The closed hat within 1 to 4 dB, the model 3 to 4 dB darker at 6.3 kHz.
- The open hat within 1 to 5 dB, the model 3 to 5 dB lower from 3 to 5 kHz.
- The cymbal within 1 to 4 dB above 5 kHz at every TONE, the model 3 to 4 dB stronger from 2.5 to 5 kHz (6 dB at 1
  kHz against Fischer's); accented, it moves some 2 dB toward the units there.
- The cowbell within 1 to 5 dB of Audiorealism's in most bands, the 1 kHz band (the lower tone's second harmonic) 5
  dB weaker; at 1.25 kHz the units themselves differ by 28 dB.

The amplitude at full level (model unaccented and accented; page 14): cowbell 2.4 to 2.9 and 6.0 Vpp (3.5, 12),
cymbal 2.2 and 5.4 (3.5, 7), open hat 2.7 and 7.0 (3.5, 7), closed hat 1.9 to 2.2 and 3.7 (3, 6).

## Left open

- The open hat's decay, 6 to 32 % longer than both units across OH DECAY: kept as drawn until each recording set's
  accent was known (the user, D255). Deduced in D258 (`808-accent.md`): both sets were struck at 4.5 to 5 V, as good
  as unaccented, so the accent did not explain it; fitted there (C60, R134, above), within 1 to 5 % of the units'
  range at every mark.
- Unaccented, every voice is 20 to 35 % under page 14's amplitude; accented, the cowbell is at half of the page's 12
  Vpp, which IC2b's swing could not give at the drawing's divider.
- The price: the whole kit 70 % of a block at its 99th percentile with every instrument struck at once (52 % on
  average); outside the Kit mode a voice holds one instrument but runs the bank and all five VCAs, 49 % for the
  dearest, the cowbell. At a 96 kHz host it costs the same since D259 (the voices near 96 kHz at any host; before,
  192 kHz there): an open hat then a cymbal 29 % of real time. Profiled at 96 kHz, in us a sample: the bank 0.12, the
  five VCAs at rest 1.95, the open hat's envelope 0.41, the cowbell 1.62 while it rings.
- At full accent the open hat is chaotic in the model: its high band 1 pV higher at one sample moves its waveform by
  up to 3.5 to 4.5 dB under its peak within 50 ms, its level within 0.4 dB in every 10 ms (D262). At 4.7 V, and in
  the cymbal and closed hat, the same nudge stays near -230 dB. Whether the machine does the same is not known.
