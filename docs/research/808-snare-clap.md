# 808 Snare & Clap: the snare, clap and maracas against SPICE, two real units and page 14 (2026-11-22)

The 808 Snare & Clap generator (D256, D257) holds two circuits read from the service notes (pp. 6, 9, Fig. 13): the
snare drum, and the hand clap or maracas, one circuit with its switch SW12. Each is checked in three steps, as the 808
Drums were.

- Stage by stage against ngspice: `tools/tr808/snare.cir` and `clap.cir`, rendered by `tools/tr808/render.py`; the
  references are in `tests/data/tr808`, the test is `tests/V3Tr808SnareClapTests.cpp`.
- Against two real units' recordings: Fischer's (`tools/references/fischer-808`: the snare at five TONE and five SNAPPY
  marks, one clap, one maracas) and Audiorealism's (`tools/references/audiorealism-808`: the same snare grid, sixteen
  claps and sixteen maracas, all accented at a level it does not give).
- Against page 14's table: the snare's two frequencies and decay, each voice's amplitude normal and accented, and the
  clap's TM3 adjustment.

## The circuits as drawn

The snare, in page 9's changed version (C58 0.056 uF, C61 0.015 uF, R191 10 kOhm, R200 a link), takes the bass
drum's kind of trigger through its PNP switch Q46 into two bridged-Ts around IC14: the lower shell near 173 Hz, the
upper near 334 Hz, mixed on the TONE pot VR8. The snappy is white noise through the swing VCA Q48, whose collector the
envelope C51 and C52 feeds through D54 (Q47 charges C51 from the trigger through the SNAPPY pot VR9), then Q49's
high-pass; IC13 mixes both. As drawn before the change the shells ring near 250 and 500 Hz, page 14's 238 and 476; both
units here ring at the changed values, as most 808s do
([Norgatronics, 808 Snare - Mutations](https://norgatronics.blogspot.com/2021/11/808-snare-mutations.html), which
computes 173.3 and 336 Hz).

The clap gates the band-passed white noise (IC21) twice: a long "reverb" through Q70, whose base C138 feeds through
R349 from C139 (charged by the trigger through Q69 and D69); and bursts through IC22, a BA662 transconductance
amplifier whose control current Q72 and Q71 make exponential from the sawtooth on C144. IC23, an AN6912 quad
comparator, makes that sawtooth: A holds C140 low while the trigger stands and B holds a window open while C140
recovers; inside it D fires Q73, which empties C144 while C charges it through R365 and D71, a narrow pulse each time
C144 climbs past pin 1's share; when the window shuts, C141 and D70 fire Q73 once more. So three claps, then a fourth
that dies away slowly as C144 drains through R367, R368 and R371, as Fig. 13 draws it. TM3, the C.P. OFFSET, sets how
far each clap falls before the next. The maracas switch the trigger to D67 and C135: Q67 empties C134, Q66 follows it
down and lets Q65, its base on the white noise, pass its current through D65 into Q68's high-pass.

The parts the notes do not describe were taken from their makers' data or their clones':
- The AN6912 as LM339-like open-collector comparators (0.2 V at 3 mA, Panasonic's
  [AN1339/AN6912N](https://electronix.org.ru/datasheet/Panasonic/an6912%20-%201_3us%20quad%20comparators.pdf)), as
  switches to -15 V with a millivolt's lean toward on, so pins 2 and 13 rest low as page 6 says.
- The BA662, whose Rohm datasheet is not online, as Open Music Labs' reverse-engineered schematic
  ([BA662](https://www.openmusiclabs.com/wiki/BA662)) and Alfa's AS662D clone draw it: a standard transconductance
  amplifier, Iabc tanh(v / 2 VT), its control input a Wilson mirror one junction and some 400 Ohm above -15 V, its
  buffer an NPN and a PNP follower (no level shift to speak of).
- The white noise bus as 808 Drums' (130 mV rms at TP4 over C202's 7.2 kHz band), its density the model's, through
  that pole.

## The user's choices (D256, D257)

- C140, printed 0.0027 uF: 0.027 uF. As printed the window lasts 4 ms, one clap and a smeared second; page 6's text
  gives 30 ms and Fig. 13 draws three claps and a fourth, which with R350 (1 MOhm) needs 0.027 uF. Both units' fourth
  clap lands 30.7 to 31.5 ms after the first (Fischer's, all sixteen of Audiorealism's); the model's at 30.7.
- C144, drawn 0.47 uF: 0.39 uF, inside an electrolytic's 20 %. As drawn the claps come every 13 ms; Audiorealism's
  unit every 10.5 ms, Fischer's 11.6; 0.39 uF gives 11.1, their mean (a hobby model,
  [johnnyhawk02/Tr808](https://github.com/johnnyhawk02/Tr808), fitted 0.37 uF to one recording).
- TM3 at the service notes' adjustment, 0.62 of its travel toward -15 V: each clap falls from 6 Vpp to 0.5 (-21.6 dB)
  before the next, as page 14 sets it; both units' claps fall some 20 dB, from 9 to 33 hit by hit.
- IC21's C127 0.82 nF (drawn 1.8) and C128, C129 5.2 nF (+10 %). As drawn the clap is 3 to 8 dB brighter than both units
  above 2 kHz, in its claps and in its tail alike, and peaks at 1.27 kHz against their 0.93 to 1.19; the snappy and the
  maracas, on the same noise bus and output, match both units within 1 to 2 dB, so the difference is in IC21. Fitted,
  every octave from 500 Hz to 16 kHz lies within about 1 dB of both units, the centre at 1.14 kHz. No free source says
  what is fitted on real boards.
- The snare as changed (D256), which needed no fit.

## Against SPICE

The snare's stages match their references within 1.5 % unaccented (the snappy's high-pass output 6 %, the output 3 %).
IC14a's input is a 4.6 us spike through C57 into R190, shorter than a sample, held to its running integral (4 %); the
shell it rings matches within 0.6 %. At full accent the snappy's swing VCA Q48 switches inside a sample on every swing
of the noise and the high-pass after it turns the edges into spikes: its nodes are held to their envelopes there, 6 to
7 % at the VCA (some 3 % of charge, 0.3 dB), 3 to 4 % at the output.

The clap's stages match within 4 %, the output within 3.7 % (1.7 % accented); IC23's pins 2 and 5, which switch inside
a sample, within 5.4 %. The maracas' within 1.6 % up to Q68's high-pass (the trigger node 3.4 %, all of it in the
first samples of its rise); its output and the mix by their envelopes,
within 2.5 to 4.4 %: Q65 switches the noise inside a sample and the high-pass rings at 10.6 kHz on each edge.

Taken on the way, each the circuit as drawn or the method's own limit:
- A device chain through resistors only (Q72's collector, R374, the BA662's mirror) ties its currents: Q72's junction
  capacitance, as the reference has it, unties it.
- The capacitors a diode or a switch charges within a sample (C139, C143, C135, C140, C141) and the small junction
  capacitances take the backward Euler rule: the bilinear transform overshot there (C139 to 4.9 V against 2.8) and the
  diode held the overshoot.
- At rest Q70's base sits at the C.P. OFFSET's voltage, reached through D68 and D69 near no bias: the clap is silent
  between hits (-123 dB in ngspice) and its reverb dies away slowly toward it. The model keeps both diodes once the
  trigger has passed.
- Q68's high-pass is prewarped to its 10.6 kHz resonance, which the bilinear transform would put 3.7 % low at 96 kHz.

## Against the two units and page 14

The snare, unaccented, Snappy at zero. Frequencies and the time to fall 60 dB (model, the reference; Fischer;
Audiorealism): the lower shell 172.9 Hz and 205 ms; 172.3 Hz and 197 ms; 172.9 Hz and 196 ms. The upper shell 334.0 Hz
and 65 ms; 339.1 Hz and 62; 331.1 Hz and 63 to 67. The upper shell over the lower at the TONE's five marks: -33.4,
-23.2, -14.5, -6.4, +0.4 dB; -31.7, -25.2, -14.6, -5.7, -0.4; -33.0, -23.3, -13.6, -4.8, +1.4.

The snappy over the shells (5 to 25 ms) at the SNAPPY's quarter, middle, three quarters and top, Tone in the middle:
-15.3, -1.5, +3.8, +7.1 dB; -21.5, -1.3, +4.4, +7.4; -19.8, -2.5, +2.8, +7.4. At the quarter the units part (Fischer
-23 to -28 dB over the TONE's marks, Audiorealism -12 to -22) and the model lies between (-15 to -21); from the middle up
all three agree within about 2 dB. Its decay to -20 dB: 55 to 68 ms; the units 55 to 85. Its spectrum, octave by
octave from 125 Hz to 16 kHz, within 1 to 2 dB of both units'.

The clap (six hits each). The claps' onsets: 0.8 to 1.8, 11.8 to 12.8, 22.8 to 24.0, 31.0 to 33.0 ms; Fischer 0.7,
12.0, 23.9, 31.4; Audiorealism 0.5, 9.5 to 10.8, 21.2 to 22.0, 31.2 to 32.8. Down 40 dB from its peak 92 to 123 ms
after it; the units 100 to 149. Its spectrum within about 1 dB of both units' from 500 Hz up. Its envelope matches
theirs within about 2 dB to 100 ms; after 120 ms the units' run 4 to 12 dB above it, where the model at an 8 V trigger
lies within 2 dB to 120 ms and 3 to 6 dB under them later. Fitted (D258, the units being as good as unaccented):
C139 39 nF (printed 18 nF), the tail from 150 ms to a second within about 2 dB of both units (rms 1.7 dB, as drawn
7.0), the claps and the first 100 ms as they were; C138 or R349 alone made it worse, C139 and C138 together 2.4 dB. Audiorealism's hits are accented at a level it does not
give, Fischer's at none he gives either. All three keep a faint tail: the units -59 dB at a second, -67 to -72 at two;
the reference -57 dB at 0.7 s.

The maracas: their peak 9.5 to 17 ms after the hit, down 40 dB 11 to 18 ms later; the units 14 to 19 and 11 to 16.
Their spectrum's centre 9.5 kHz and peak 10.4 kHz; the units 9.2 to 9.3 and 10.5 to 10.7.

The amplitude at full level (page 14 in brackets): the snare 3.0 Vpp (3), accented 13.8 (10); the clap 5.4 (6),
accented 7.4 (the page's 2 can hardly be its accented figure); the maracas 4.5 (3), accented 5.7 (5). These, the
clap's timing and the snare's pitch come through the generator itself (the test's dump mode,
`COOLSYNTH_808_SNARECLAP_DUMP`).

## Left open

- The accented snare runs 2.8 dB over page 14, as the 808 Drums' voices do (D253). Settled in D258: the Accent's top
  is 11.5 V, where the snare's ratio lies 0.6 dB over the page's.
- The clap's late tail, 4 to 12 dB under both units after 120 ms unaccented: fitted in D258 (C139, above), once the
  accent check had both units as good as unaccented.
- The maracas unaccented, 3.5 dB over page 14's 3 Vpp.
