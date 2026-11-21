# 808 Drums: the toms, congas, rim shot and claves against SPICE, two real units and page 14 (2026-11-21)

The 808 Drums generator (D252, D253) holds the bass drum (the Kick's 808 circuit, D251) and four more circuits read
from the service notes (pp. 6, 9): the low, mid and high tom or conga, one circuit a pair with its switch, and the
rim shot or claves. Each is checked in three steps, as the bass drum was.

- Stage by stage against ngspice: `tools/tr808/tom.cir` and `rimclave.cir`, rendered by `tools/tr808/render.py`;
  the references are in `tests/data/tr808`, the test is `tests/V3Tr808DrumsTests.cpp`.
- Against two real units' recordings: Fischer's (`tools/references/fischer-808`, each tom at five TUNING marks) and
  Audiorealism's (`tools/references/audiorealism-808`, five TUNE steps, sixteen rim shot and claves hits).
- Against page 14's table: each voice's frequency at three tuning positions, its decay to a tenth, its amplitude at
  full level, normal and accented.

## The circuits as drawn

The toms and congas take the bass drum's trigger: Q32 onto the TRIG line, each pair's PNP switch (Q51, Q54, Q57)
through its instrument line's transistor. The pulse reaches a multi-feedback bridged-T through C73 and R226, D56
clamping its falling edge; the germanium pair D80, D81 across R231 and the TUNING pot shortens the T's ground leg
while the drum is loud, so its pitch falls as it rings. The conga's switch takes the tom's capacitor out (and the mid
pair's C89), adds R224 at the plus input and leaves the noise out. All three toms take the pink noise bus through a
gate (Q52, Q55, Q58) that the trigger's own envelope (R219, D55, C72) opens.

The rim shot rings two bridged-Ts, IC21 near 455 Hz and IC20 near 1.7 kHz, sums them onto the swing VCA Q62, whose
collector the envelope C24 feeds, then Q63's high-pass. The claves switch takes IC20 to 2.5 kHz with R313 and play it
alone.

The pink noise bus: Q35's avalanche noise, 130 mV rms at TP4 as page 14 sets it, through IC24b's unity-gain
Sallen-Key low-pass (R133, R132 22 kOhm; C32 39 nF, C33 18 nF: 273 Hz, Q 0.74). The later boards (from serial 010600)
put 22 pF across R130, a pole at 7.2 kHz; the 130 mV over that band is the density the model uses. The earlier
boards' band was the op-amp's own, wider, so their pink noise would be some 6 dB quieter.

## Four fits, as the user chose (D252)

With the parts as printed, nothing fitted, the toms' pitch already lies within 1 to 4 % of both units, but they and
the congas ring 10 to 30 % longer, and the claves ring on.

- The feedback resistors R230, R259, R286 (printed 820 kOhm): fitted per pair to both units' tom and conga decays at
  once, low 865k, mid 865k, high 880k.
- R313 (printed 390 kOhm): 500k, the claves' decay to a tenth 24.1 ms against the units' 23.8 and 24.2.
- R324, which the service notes leave unprinted: 130k, the rim shot 3.3 dB over the claves (the units 2.1 and 4.9).
- The 1S188FM germanium pair, which has no published model: a typical germanium small-signal diode, Is 2 uA,
  emission 1.3, 10 Ohm in series. Swept later against page 14 (below): weaker diodes give the units' pitch drop less
  well and make the accent's excess larger, so it stays.

## Against SPICE

Two things the first model left out mattered.

- The op-amps' bandwidth. With ideal op-amps the high conga was 14 % off its reference and the rim shot 10 %; the
  references use a single-pole 3 MHz op-amp (the 4558's gain-bandwidth). With SPICE's op-amps made ideal too, the
  model matched, so the bandwidth was the cause. The resonators now carry it (`addOpAmp` in `CircuitWdf.h`: a
  transconductance into a node held by 100 MOhm and the pole's capacitor, then a follower).
- The op-amps' swing. At full accent the rim shot's IC21 would swing to -18.5 V and the claves' IC20 to +-17.7 V,
  past the +-15 V rails. The uPC4558 swings some +-14 V typical into 10 kOhm (+-12 V at least,
  [TI's RC4558](https://www.ti.com/lit/gpn/RC4558), [Renesas's uPC4558](https://www.renesas.com/document/dst/upc258-4558-datasheet)):
  the references and the model clamp each resonator op-amp's inner node there.

With both, the toms and congas match their references within 2 % at every stage, the accented low tom too: pitch
within 0.1 Hz, decay within 0.5 ms, peak within 1.5 %. The rim shot is within 4 %, accented within 9 %: its swing
VCA switches inside one sample in the first 3 ms, where the reference resolves the edge in sub-microsecond steps
(after them, within 7 %). The claves ring some 5 Hz under their reference's 2498 Hz (3 cents), the bilinear
transform's warping at 2.5 kHz, with their decay and peak as the reference's.

## Against the two units and page 14

The settled pitch at the TUNING's bottom, middle and top (model; Fischer, Audiorealism; page 14):

- Low tom: 81.9, 90.3, 102.0 Hz; 81.2, 88.5, 99.3 and 81.7, 88.6, 100.4; page 80, 90, 100.
- Low conga: 181.4, 200.0, 225.6 Hz; 184.6, 200.3, 224.5 and 187.4, 202.5, 230.5; page 165, 185, 220 (the
  page's row repeats the high tom's, as noted in D252).
- Mid tom: 127.1, 140.1, 158.1 Hz; 123.4, 134.5, 154.4 and 126.7, 136.5, 155.1; page 120, 135, 160.
- Mid conga: 261.3, 287.9, 324.9 Hz; 259.2, 281.0, 319.3 and 264.3, 283.1, 325.4; page 250, 280, 310.
- High tom: 172.4, 190.0, 214.5 Hz; 172.7, 186.6, 213.4 and 179.7, 194.4, 213.6; page 165, 185, 220.
- High conga: 381.8, 420.6, 474.3 Hz; 375.1, 412.4, 466.3 and 394.4, 422.1, 475.4; page 370, 400, 455.
- Rim shot, its first 4 ms by zero crossings: 1758 Hz against 1716 and 1721; page 1667.
- Claves, by zero crossings, steady through the ring: 2492 Hz against 2422 and 2410; page 2500.

The pitch drop from the first cycles to the settled pitch at the TUNING's top, unaccented: the low tom 4.5 % (the
units 4.4 % and 4.3 %), the mid 4.5 % (3.8, 4.5), the high 5.8 % (4.5, 6.8); the congas 0.8 % (0.9 to 2.3).

The decay to a tenth, the TUNING in the middle: low tom 202 ms (196, 194; page 200), low conga 169 (180, 166; 180),
mid tom 128 (132, 127; 130), mid conga 92 (92, 90; 100), high tom 98 (92, 93; 100), high conga 78 (81, 76; 80), rim
shot 10.0 (10.6, 11.9; 10), claves 24.3 (23.8, 24.2; 25).

The amplitude at full level, unaccented (a 4 V trigger): low tom 3.8 Vpp (page 3.5), low conga 3.5 (3.5), mid tom
3.8 (3.0), mid conga 2.4 (3.0), high tom 4.3 (3.5), high conga 3.7 (3.5), rim shot 3.4 (3.0), claves 2.9 (2.5); the
bass drum's reference 3.5 (3.5).

## Left open

The accented amplitude. Page 14 has every voice about 3.3 times louder at full accent than unaccented, the ratio of
the trigger itself (about 4.3 V to 14.3 V, which the service notes' text gives as 4 V and 4 to 14 V,
[p. 6](https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=6)), and the text says each voice's output
is in proportion to it. The circuits as drawn are not: at a 14 V trigger the bass drum's reference is 1.3 dB over page
14, the toms and congas 1.1 to 4.4 dB (low tom 18.8 Vpp against 12, mid conga 11.4 against 10), the rim shot 4.5 dB,
the claves (held by the rails) 1.9 dB.

- In the toms the germanium pair adds most of it, more the louder the drum (without it the conga is in proportion);
  weaker diodes, which would keep the units' unaccented pitch drop less well, add more, not less.
- At full accent the low tom's first cycles are 20 % over its settled pitch. Audiorealism's hits are all accented
  (its readme), at an accent level it does not give, and drop no more than Fischer's.
- No recording of an accented tom or rim shot at a known accent level is at hand to say which is right: the page's
  figures are "typical and variable", and the accent's top is the AC LEVEL knob each player sets.
