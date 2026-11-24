# The TB-303 as its circuit: what exists (D241)

Research for TODO 32: the TB-303 Bass Line as its circuit, the Chicago acid bass, played by the layer's step sequencer with its slides. A survey of open projects, papers, the service notes and reference recordings, on 2026-11-20.

In short: one open project models every part from the circuit and publishes numbers (TriggerFish's 303 modules for VCV Rack, BSD-3 up to release 2.3.1); the best-liked by ear is a tuned sound-alike (Open303, MIT); the filter's analysis is Tim Stinchcombe's. Nothing open is checked against a real 303 at known knob settings, and no free recordings at known settings were found.

## The circuit's parts

- The VCO: a sawtooth, and a square shaped from it by one transistor (Q8), whose pulse width moves with pitch.
- The filter: a four-stage diode ladder, its stages unbuffered from each other, so each pole sits elsewhere; its resonance by feedback that must reach about 17 to self-oscillate (a Moog-style ladder needs 4).
- The envelopes: the filter's (decay only, its depth on Env Mod, which cannot reach zero) and the VCA's.
- The accent: louder, and a sweep of the cutoff through a diode, 47 kOhm with a 100 kOhm pot (the resonance knob's second gang) and a 1 uF capacitor, so repeated accents climb higher.
- The slide: the pitch control voltage through an RC, a time constant near 22 ms, about 100 ms for a full glide; the gate high for 3.5 of the 6 clock pulses in each sixteenth.
- The VCA: a BA662.

## Source material

- The service notes, First Edition, February 19, 1982, 12 pages: https://archive.org/details/Roland_TB-303_Service_Notes, https://www.manualslib.com/manual/3378574/Roland-Tb-303.html, https://www.synfo.nl/servicemanuals/Roland/ROLAND_TB-303_SERVICE_NOTES.pdf.
- Reported problems: C29 said to be fitted opposite to the drawing on real units (https://www.matrixsynth.com/2010/11/component-c29-is-backwards-on-all-tb.html); the filter's printed "18 dB/oct", which Stinchcombe disputes; an open question on the ladder's transistor types. No published errata list.

## Papers and analyses

- Tim Stinchcombe, the 303's diode ladder: https://www.timstinchcombe.co.uk/index.php?pge=diode and `?pge=diode2`.
  - The core response -1/(s^4 + 6.727 s^3 + 14.142 s^2 + 9.514 s + 1), real poles at -3.24, -2.33, -1.04 and -0.13; the halved bottom capacitor raises the cutoff by 2^0.25.
  - Four poles, 24 dB/oct, though it acts like 18 dB over its main range.
  - The coupling capacitors add six poles and six zeros, and a second resonance near 8 to 10 Hz, seen on a clone's bench.
- Vadim Zavalishin, "The Art of VA Filter Design" 2.1.2, section 5.10: https://www.discodsp.net/VAFilterDesign_2.1.2.pdf. The ideal diode ladder's response, real poles, the resonant peak at 1/sqrt(2) of the cutoff, self-oscillation from k = 17; a zero-delay-feedback solution.
- Federico Fontana and others on diode ladders (the EMS VCS3's, checked against PSpice): DAFx-08, IEEE TASLP 18(4) 2010, DAFx-11 (https://dafx.de/paper-archive/details/y9N6dG2Rw_L1-hVzFviCrw). Not the 303's, but the same kind of network and solvers (fixed-point, Newton).
- Kernmayer, "The Sound of Acid", thesis, FH St. Polten, 2020: https://phaidra.ustp.at/api/object/o:4344/download. A Max/MSP 303 after Zavalishin; a listening test, no hardware comparison.
- Robin Whittle (the Devil Fish's maker): the accent sweep and the Env Mod floor (https://www.firstpr.com.au/rwi/dfish/303-unique.html), the slide and the gate timing (https://www.firstpr.com.au/rwi/dfish/303-slide.html).
- KVR: the Env Mod bias near a third (service notes page 8, https://www.kvraudio.com/forum/viewtopic.php?t=562991); the glide's time constant; the square's pulse width moving with pitch; a debate on modelling the ladder's nonlinearity once or per stage (https://www.kvraudio.com/forum/viewtopic.php?p=4878962).
- No wave digital filter paper on a diode ladder was found.

## Open projects

Circuit-level:
- TriggerFish 303 Oscillator and 303 Voice Core for VCV Rack (https://github.com/JTriggerFish/TriggerFish-VCV), with technical reports; BSD-3-Clause in releases 2.2.0 to 2.3.1, GPL-3.0 from 2.4.0.
  - The diode ladder by Newton with Stinchcombe's full coupling network; the envelopes; the accent sweep (47 kOhm, 33 nF, its memory on C13); the BA662 VCA; the VCO's Q8 shaper transistor by transistor; values from the service notes.
  - Checked against its own reference (the ladder within 6.3 % on the first five odd harmonics), ngspice (the VCA within 0.2 %) and x0xb0x measurements (the square 5.50/3.48 Vpp and 45.7 % duty against 5.6/3.8 Vpp and about 45 %); 4x oversampled, aliasing at -47.7 dBc.
  - Not checked against a real 303.

Sound-alikes and parts:
- Open303, Robin Schmidt (https://github.com/RobinSchmidt/Open303), MIT: a linear four-stage ladder with a high-pass in its feedback, high-passes before and after, an all-pass and a notch; accent and slide in milliseconds; tuned by ear from owners' recordings; widely judged the best-sounding open 303. Forks: JC303 (https://github.com/midilab/jc303, a GPL-3 JUCE wrapper around the MIT engine), nt_303 (MIT), Corrode (GPL-3).
- Faust's `ve.diodeLadder`, Eric Tarr (faustlibraries `vaeffects.lib`), STK-4.3 (MIT-like): a generic diode ladder after Will Pirkle, a cubic clipper at its input.
- Soundpipe and AudioKit (https://github.com/AudioKit/SoundpipeAudioKit), MIT: `diode` (Pirkle's design) and a 303-like `tbvcf`.
- Surge XT's diode ladder (sst-filters, GPL-3): generic.
- a1k0n/303 (https://github.com/a1k0n/303), no licence: scope measurements of an x0xb0x; the ladder close to theory but for the high-pass in its feedback.
- squelchbox (Rust), licence not stated; criticised on KVR for its square, feedback and accent.
- The x0xb0x, the open hardware clone (https://certification.oshwa.org/us002547.html): firmware MIT, documentation CC BY-SA.

## Reference recordings

- None found of a real 303 at documented knob settings.
- Freesound's "laban x0xb0x" loops, named by their settings (cutoff, resonance, env mod, decay, accent), mostly CC-BY: a clone, not a 303.
- Sir Galaxy's loops: an unmodified 303, settings not given. Others are clones or forbid redistribution.
- A check against a real unit needs an owner's recordings at known settings.

## Built (D242)

The user's choices: checked against SPICE and published numbers; built on Open303's engine, rewritten into `src/synth/v3/Tb303Generator.h` with its MIT notice; a TB-303 generator; the accent on or off at velocity 100; the 303's own knobs and the slide's time.

Changed from Open303 for CoolSynth: the band-limited tables built once and shared (Open303 kept two of some 210 kB per instance); the note list left to the layer (Open303's allocated on every note); the core at 2x inside the voice's 96 kHz (192 kHz, near Open303's 176.4 kHz design point; at 4x the harmonics agree within 0.1 to 0.5 dB, so 4x buys nothing); a legato hook from the voice, so a slid note keeps its envelopes and is heard for its accent.

Against the published figures (`tests/V3Tb303Tests.cpp`):
- The filter: Open303's coupled ladder matches Stinchcombe's response, -1 / (s^4 + 6.727 s^3 + 14.142 s^2 + 9.514 s + 1), within 0.1 dB from an eighth of the cutoff to sixteen times it, its frequency read as 1.2 f / fc; 23.3 dB an octave at the top.
- Resonance: the feedback reaches 17.8 at full and the ladder rings but does not self-oscillate, as a stock 303's does not (the Devil Fish adds it).
- The slide: Open303's time constant was 12 ms; the 303's is an RC of 100 kOhm and 220 nF (C35), 22 ms, its corner measured at 7.2 to 7.5 Hz (synth-diy, Gearspace). Corrected: Slide is the time to 95 %, 66 ms on the 303, three time constants.
- The square: 46.9 % duty, the x0xb0x's about 45 %.
- The accent: velocity 98 plays as 64 does; 100 plays 2.3 times as loud at the panel's middle, its filter swept higher.

Not checked: a real 303's recordings at known settings (none free); the accent's sweep against a real one (Open303's 15 ms against Whittle's 47 kOhm and 1 uF).

## For the circuit model of CoolSynth 303 (D275)

Searched again on 2026-11-23, for a model of every stage from the service notes (`docs/design/COOLSYNTH_303.md`).

The real unit:
- Din Sync's reference recordings of their TB-303 (2010, dinsync.info): 25 WAVs, stereo, 16-bit, 44.1 kHz, 49 s
  each, 16 low C notes a set (saw and square, plain and accented), the five knobs at 0, 25, 50, 75 and 100 % in
  documented sets. No licence stated: kept in `tools/references/tb303-dinsync`, not in git.
- No other real 303 at known settings is free. Lemus's recording protocol (lemusdev.org/ro-mono-303/record) is a
  script to hand an owner, CC BY 4.0, with no takes yet.

Measurements:
- Julian Schmidt's logic analyser on the CPU (sonic-potions.com): the gate 3 of 6 clocks, 50.3 % at the slowest
  tempo and 55 to 56 % at the fastest, 0.8 to 3.3 ms after the clock (2.06 ms mean); in triplets 5 of 8; a slide
  never lowers the gate; accent latched for the whole step.
- Whittle's Devil Fish manual, for a stock 303: the note's start 1 to 5 ms after the gate, its end 8 ms full then
  8 ms falling; the filter decay 200 ms to 2 s, 200 ms on an accent; the volume's own 3 to 4 s; 32 Hz 5 dB down;
  the highest resonant peak near 2.5 kHz; Env Mod never zero; the slide 22 ms.
- Richie Burnett's response of a real unit at full resonance (sequencer.de): the peak from about +11 dB at 260 Hz
  to +39 dB high up; below 100 Hz about 5 dB an octave down.
- Units spread: full cutoff from 1.2 to 3 kHz (ro-mono-303's fit to the Din Sync set); the filter's ring 2.0 ms
  drawn, 2.5 ms fitted.
- The square's width about 45 % high and 71 % at the lowest notes (Olney, after Whittle).

Parts and models:
- The BA662: Open Music Labs' reverse-engineered schematic (an OTA as the CA3080 with Wilson mirrors, a buffer),
  its offsets measured; the AS662 clone's datasheet gives its transconductance.
- The 2SK30A: the datasheet's ranks (O 0.6 to 1.4 mA, Y 1.2 to 3 mA). The AN6562: an LM358 in effect.
- No model and no pinout found for the 2SC1583 and 2SC2291 duals.
- Schematics to cross-check our reading: the x0xb0x's Eagle files (MIT, github.com/a1k0n/x0xb0x), its node
  voltages (Open Music Labs' x0xmap), Oakley's TB3030 (CC BY-SA). No complete, licensed netlist of the voice exists;
  vibe303's is a warning (mixed sheets, its conflicts listed).
- Doubts: C29 reversed on real units; two resistors near the ladder some call misprinted (Eddy Bergman, a claim).
- Papers: none free on the 303's ladder in WDF or nodal form; McClellan (DAFx 2026) proves a linear diode ladder
  stable for feedback up to 8.9 as it changes.

## The netlists (D275)

In the engine's `tools/tb303`, read from page 5 at 600 dpi (the scan kept locally in `tools/references/tb303-service`),
checked against the x0xb0x's schematic as Open Music Labs redrew it (openmusiclabs.com/wiki/x0xb0x, MIT).

- `vco.cir`: the slide, the exponential converter (Q26, IC11a), the C33 saw with its thyristor reset (Q24, Q25, Q27;
  Richie Burnett's description, synth-diy 2011), Q28 and the Q8 square.
  - The reset lets go only through real transistors' storage and low-current gain; the 2SC945 and 2SA733 are taken as
    Fairchild's 2N3904 and 2N3906 (their datasheets' models), IS set to both parts' typical VBE of 0.62 V at 1 mA.
  - Calibrated by page 7 (`calibrate.py vco`): A at 110.0 Hz, 2:1 and 4.002:1. Then the pitch tracks within 1.4
    cents from C1 to C5; the slide's time constant is 22.00 ms; the saw runs 5.6 to 11.2 V, the square 5.33 to 8.8 V.
  - The square's duty falls from 59 % at C1 to 37 % at C5; published: about 71 % low, 45 % high. To settle against
    the Din Sync set (C2).
- `vcf.cir`: the ladder, Q12's drive, Q21's read-out, the resonance's feedback, the Q9/Q10/Q11 converter (page 8's
  Fig. 2), CUTOFF, TM3, ENV MOD and the accent's C13 through the resonance pot's second gang.
  - A misprint in the notes: R110 and R111 (Q21's loads) are 22k, not 2.2k (the x0xb0x; Eddy Bergman's build). With
    2.2k the resonance's loop gains about 1.3 and nothing rings.
  - TP6 is Q19's emitter: 5.7 V at rest, inside page 5's sketch of 5.2 to 5.8 V.
  - Calibrated by page 7 (`calibrate.py vcf`): TM3 at 0.17 of its travel gives the 2.000 ms ring.
  - Then, at full resonance: the peak from 246 Hz to 2.13 kHz over the CUTOFF knob (real units 1.2 to 3 kHz at the
    top, Whittle about 2.5 kHz); +8 dB low to +35 dB high over the low end (Burnett: +11 dB at 260 Hz, rising); no
    self-oscillation; the coupling capacitors' second resonance near 7 Hz (Stinchcombe).
- `env.cir`: Q37 inverts the CPU's gate; Q36 passes it as GATE; C54 and R148 make Q38 a one-shot that charges C62
  (1 uF) through D37 and R152; Q41 and Q40 follow C62 over Q39, a 2SK30(Y) as a current sink. C62 falls through
  R138 (68k) to the DECAY line, and through DECAY (1 M A) to the diodes D28, D29 and D26 (R117 from 5.333 V); on an
  accent IC12B shorts DECAY and IC12A passes the envelope to the ACCENT knob.
  - 100 to 10 % in 172 ms at the least DECAY and 2.41 s at the most (page 5: 200 ms and 2.5 s).
  - A held accent decays in 172 ms whatever DECAY says (Whittle). IC13 latches the accent for the whole step, so at
    120 BPM it halves in 50 ms, then finishes at DECAY's rate.
  - The envelope peaks at 10.2 V and rests at 0.02 V.
- `vca.cir`: IC15 as Open Music Labs' reverse-engineered BA662 (a CA3080-like input mirror with its measured 500 ohm,
  Wilson mirrors as an LM13700's, a complementary follower), Q31 and Q32 on C42, the accent through D27, with the
  envelope's circuit so GATE and the trigger come from it.
  - The audio comes from the filter's output (R121) and the RESONANCE wiper (R122), as the x0xb0x draws them: more
    resonance feeds the VCA more.
  - A note opens on the gate, holds while C42 refills through 1.5 M, and dies within about 15 ms of the gate's fall
    (Whittle: 16 ms).
  - Left out: D34 and R137, from GATE's side to a line not yet traced.
- `voice.py` builds `voice.cir`, the whole voice from the stages; 0.3 s of a note renders in about 5 s.
- `compare_dinsync.py` against Din Sync's real unit (SET-A1, low C saw, the other knobs at nothing):
  - With page 7's TM3, at cutoff full: pitch within 4 cents, harmonics 2 to 8 within 2.2 dB, the circuit brighter.
  - TM3 fitted at 0.35: harmonics 2 to 8 within 0.7 dB on average at cutoff 25, 50 and 100 %, 2.1 dB at 75 %
    (the knob positions are approximate, and the audio taper is assumed). The unit is darker than page 7's 2 ms
    ring, as ro-mono-303 found for it (2.5 ms).
  - With TM3 at 0.35, each knob alone at 25 to 100 % (the others at nothing), harmonics 2 to 8 on average:
    RESONANCE (A2) within 0.3 to 1.6 dB, 2.8 dB at 25 %; ENV MOD (A3) within 0.2 to 1.1 dB, 2.3 dB at 75 %; DECAY
    (A4, from 150 ms into the note) within 0.1 to 1.0 dB; ACCENT (A5, accented notes) within 0.3 to 1.1 dB. Pitch
    within 10 cents throughout.
  - The accent's loudness over a plain note at ACCENT full (the recordings keep one gain): +13.2 dB against the real
    unit's +13.3 dB from 30 to 80 ms, +6.9 against +7.8 dB from 80 to 150 ms; the first 25 ms 2.8 dB louder.
  - These first comparisons started the note before the VCO had settled (C11 takes some 0.2 s; a real 303's VCO
    never stops). Settled, the saw at cutoff full matches within 0.3 dB on harmonics 2 to 8; TM3 at 0.35 holds
    (within 0.6 dB at cutoff 25, 50 and 100 %, 2.1 dB darker at 75 %). Sets A2 to A5 are to repeat settled.
  - The square at low C: its even harmonics 3 to 5 dB low with C11 as printed (1 uF); with C11 at 0.82 uF, inside an
    electrolytic's tolerance, harmonics 2 to 10 within 0.4 dB (Open Music Labs too changed C11 to match its 303).
  - All 25 sets, settled, TM3 0.35 and C11 0.82 uF (`compare_sets.py`; plain and accented saw at each position):
    - Sets A, C, D and E: the tone mostly within 1 dB on harmonics 2 to 8, the accent's loudness over the plain note
      mostly within 1 dB, a few positions 2 to 3 dB off.
    - Set A's two 75 % misfits (cutoff 2.2 dB dark, accent 2.6 dB weak) fit at about 85 %: Din Sync's positions are
      approximate.
    - Set B (every knob at 25 %) runs 5 to 7 dB bright whenever RESONANCE is at 25 % or more; with RESONANCE at 0 it
      fits within 0.6 dB. Resonance at 25 % is also set A2's and C2's worst position (3 dB). Open: the resonant
      peak's place among the low C's harmonics at a quarter turn of CUTOFF and RESONANCE (the pots' tapers are
      assumed).
- The 12 V rail is not 12 V: IC16 makes it from its 5.333 V times 1 + R178 / R179 (2.2k / 1.8k), 11.85 V, and TM6
  (the CV trimmer) sets the 5.333 V itself. With 5 % resistors a unit sits anywhere from about 11.4 to 12.3 V. The
  netlists take 11.85 V (`vcc`); the rail barely moves the sound (the accent's loudness over a plain note is the same
  at 11.4 and 12.3 V), as the user suspected from the 808, whose accent proved to be 11.5 V, not the drawn 14 V.
- D34 and R137 feed the GATE OUT jack from the internal gate; unplugged they carry nothing.
- The note's start: IC15's control current rises 4.8 ms after the gate (Whittle: 1 to 5 ms, typically 4 ms), set by
  the trigger's path through D36, R134 and C41 into Q32.

## Running it in real time (D279)

Searched on 2026-11-23, before putting the circuit in the engine.

- TriggerFish's 303 (its v2.3.1 technical reports, BSD): the saw is a phase ramp with a polyBLEP reset (a minBLEP for
  hard sync); only Q8 is a circuit, a 2x2 damped Newton per sample, up to 8 iterations, warm-started. 4x of the
  host by default. Its Q8 duty against ngspice: 10 Hz 71.6 % (model 73.6), 135 Hz 45.7 %, 1 kHz 31.2 %. Cost at 4x
  about 1.0 to 1.2 us per 48 kHz sample (5 to 6 % of a core). Its ladder is a reduced tanh model, 4 unknowns, damped
  Newton, aliasing -36 dBc at 2x and -47.7 dBc at 4x of 48 kHz.
- Olsen, Werner, Germain, DAFx-17: a fixed-step WDF relaxation oscillator locks its period to whole samples, its
  pitch off by several cents even at 32x; their fix predicts the crossing, steps at 16x for three samples around it,
  and places a polyBLEP at the sub-sample time on the output path (0.001 cent error).
- Valimaki, Pekonen, Nam (JASA 2012): a 4-point B-spline polyBLEP saw is perceptually alias-free up to 7.8 kHz at
  44.1 kHz; 2-point up to 2.1 kHz.
- Zavalishin (The Art of VA Filter Design): solve a nested feedback loop as one multidimensional Newton system, and
  oversample the whole loop.
- Werner, Abel, Smith (DAFx-14): split where a low-impedance output drives a high-impedance input, but not inside a
  feedback loop. Holters and Zolzer (DAFx-17): a Newton iteration costs as N squared; decomposing helps only where no
  global feedback ties the parts.

## Making the filter fast enough (D281)

Measured on 2026-11-23 (Ryzen 7 3700X, the optimised Debug build) with a timing program outside the test suite.
- The filter alone took 5.1 us a sample at 192 kHz, about one core. The 150 % first reported included the test's
  own bookkeeping: a map lookup per node per sample costs more than the filter in a Debug build.
- 56 % of that time went to the 22 by 22 elimination, which ran on every sample, because every junction conducts.
- AVX2 gained nothing. Neither did storing the matrices by column so the products run side by side.
- An inline exp, within 1 ulp, came out slower than the Debug library's std::exp, so it was dropped.

Searched the same day:
- Jacobian reuse (the chord method) and SPICE3's device bypass:
  [Holmes and van Walstijn](https://pureadmin.qub.ac.uk/ws/files/17395135/improving_the_robustness.pdf),
  [SPICE3](https://www2.eecs.berkeley.edu/Pubs/TechRpts/1989/ERL-89-42.pdf).
  - Holmes and van Walstijn found it fails to converge in some cases at 44.1 kHz.
- Yeh calls one Newton step a sample the semi-implicit method
  ([thesis](https://ccrma.stanford.edu/~dtyeh/papers/DavidYehThesisdoublesided.pdf)).
- Shrinking the system: forward-active transistors would leave 14 ports instead of 22, but lose the Early effect and
  saturation. Holters and Zolzer's decomposition fails where global feedback ties the parts, as the resonance does.
- Multiple cores: no established way splits one feedback circuit.
  - Diva spreads whole voices over cores ([manual](https://u-he.com/downloads/manuals/plugins/diva/Diva-manual-de-usuario.pdf)).
  - The parallel wave digital method of Giampiccolo and others targets DSP chips and FPGAs
    ([JAES 2022](https://re.public.polimi.it/bitstream/11311/1217627/1/GiampiccoloEtAl_JAES_2022.pdf)).
  - Waveform relaxation is an offline method.
  - CoolSynth's own pool (`AudioWorkers`, D164) already gives each sounding unit its own task in a block, so the 303
    gets a core of its own beside the other tracks and only has to fit in that one core.

The chord method, in `CircuitWdf.h` (`setJacobianReuse`, off by default):
- The factors are kept from sample to sample.
- A fresh factoring happens when a chord step fails to halve the one before, or after three chord steps in a sample.
- A chord step ends the solve below 2 uV.
- The result: 2.0 us a sample, 38 to 48 % of one core. The ngspice match is unchanged.
- It stays within 0.2 to 3 ppm of the exact solve.
- 0.5 to 1.3 % of the samples factor afresh.

## The oscillator in the engine (D290)

Measured on 2026-11-24 against vco.cir rendered at 192 kHz (low C, A, high C, and a slide up from A).
- The draft had 26 parts in a circuit sized for 24: the op-amps' two hidden parts each. `CircuitWdf.h` now refuses
  an overfull circuit (`fitsItsLimits`, silence) instead of writing past its end, and the tests check it.
- Pitch: first 0.57 to 0.71 cents sharp at every note, a constant ratio, so a capacitance. 3.8 pF on C33, the
  junctions ngspice gives Q26's collector, Q28's gate and Q25's emitter, brings it within 0.08 cents.
- The thyristor's dead time now carries into the next step when it runs past one.
- The saw's harmonics 1 to 10 match within 0.03 dB; the square's strong ones within 0.3 dB.

Aliasing, the strongest component below 20 kHz that is not a harmonic, re the fundamental:
- ngspice's own waveform sampled at 192 kHz without a filter: saw -47 dB, square -53 dB at high C.
- The polyBLEP first sat at the reset's middle while the samples jump at its start. Placed at the start, the saw is
  90 dB down at high C and 100 dB at A.
- The square's falling edge is the reset's (0.24 us in ngspice), so the shaper runs on the raw saw and both outputs
  get a polyBLEP of the jump they take there.
- What is left is the rising edge's corners, Q8 starting to conduct and saturating. Each doubling of the shaper's
  rate halves it. At high C, middle C and A:
  - 192 kHz: -59, -68, -75 dB, the oscillator 9 % of a core;
  - 2x: -66, -74, -78 dB, 14 %;
  - 4x: -72, -80, -86 dB, 23 %, the user's choice;
  - 8x: -76, -84, -92 dB, 48 %.
- Tried and dropped: Q8's junction capacitance (it matches the rise time, 26 us against 25.5, but leaves the
  aliasing and puts a one-sample spike on each reset); the jump measured over more steps (Q8 lets go within one).
- Listening tests put the threshold for aliases at 0 to 41 dB under the harmonics at 60 dB SPL
  ([Schimmel, Radioengineering 2012](https://www.radioeng.cz/fulltexts/2012/12_01_0056_0062.pdf);
  [JASA 2012](https://www.pubmed.gov/23039464/)); Nam, Valimaki, Abel and Smith found a third-order B-spline
  BLIT free of perceptible aliasing ([TASLP 2010](https://mac.kaist.ac.kr/pubs/jnam-taslp2010.pdf)).

## The envelope and the VCA in the engine (D293)

Measured on 2026-11-24 against vca.cir at 96 kHz (48 kHz for a 1.5 s decay); the model at 192 kHz.
- The envelope and the VCA's control share nodes (Q32's trigger hangs off the one-shot's collector), so they are one
  circuit, run at a quarter of the voice's rate. The BA662's core is a second circuit, its output follower a third.
- At rest every node matched ngspice's operating point but those floating between reverse-biased diodes, which only
  leakage sets.
- Three groups of nodes only junctions leave made the linear part tie their currents: Q41's emitter, pin 1, and D35's
  chain through R133 and R131 to Q31's emitter (K reached 5e18). 1 GOhm leaks, as SPICE's GMIN, fixed it;
  `isWellPosed` now catches such a group in the tests.
- Q32 held C41 after a trigger and drained C42: QEM's ISE (6.7 fA, NE 1.26) is most of an NPN's base current at
  nanoamps. As a diode across base and emitter it brings C42 from 13 % slow to within 1 mV.
- Results: the VCA's output within 0.5 to 0.6 % of its swing; the envelope 14 to 29 mV rms; C42 7 to 14 mV.

The voice's cost and its rate, measured the same day:
- At 192 kHz: filter 40 %, oscillator 25 %, VCA 33 %, envelope 10 %: about 108 % of one core.
- The filter at 96 kHz matches ngspice as at 192 (4.84 % against 4.81 % at worst), at half the cost.
- The filter's own aliasing, a 1 kHz and a 3 kHz sine of 2.7 V at full resonance: nothing that is not a harmonic
  above -91 dB below 20 kHz, at either rate.
- The oscillator at 96 kHz: the saw's aliases 90 to 103 dB down. The square's depend only on the shaper's own rate:
  96 kHz times 8 gives what 192 kHz times 4 gives (-72, -79, -86 dB).
- The user chose 96 kHz for the voice (D294). There every check passes as at 192 kHz: filter 21 %, oscillator 23 to
  28 %, VCA 15 to 20 %, envelope 10 % of a core, the voice about 73 %.

## The whole voice in the engine (D295)

Measured on 2026-11-24, `Tb303Voice.h` at 96 kHz against voice.cir rendered at 96 kHz (ngspice's step 0.2 us; at
1 us its thyristor resets late enough to shift the pitch).
- voice.py wires two couplings the stage netlists leave out: the RESONANCE wiper also feeds the VCA (C22, R122), and
  the ACCENT wiper feeds the filter's D24. Both are in the voice: the first in the filter's circuit, the second as a
  current handed back to the envelope.
- Pitch: first 0.49 cents sharp at low C (2 V), which sat between the threshold's three measured notes. Nine notes
  by half octaves now set it; each within 0.001 cents of vco.cir, the voice within 0.01 cents of voice.cir.
- As heard, at low C with every knob at half: the loudness in 5 ms blocks 0.32 dB rms, harmonics 2 to 12 within
  0.56 dB. Accented square: 0.63 dB rms, within 1.2 dB. Resonant saw: 0.42 dB rms, within 1.14 dB.
- The waveform into the filter matches within 0.04 dB; the filter's output already carries the differences, the
  VCA adds 0.2 dB. The same at 192 kHz, so it is the filter's own residual.
- Against Din Sync's sets C1 to C5 (`compare_sets.py --model`), the engine's figures and ngspice's voice.cir's side
  by side: within 0.1 to 0.3 dB of each other at every position, the accent's loudness within 0.1 dB, the pitch the
  same 5 to 6 cents under the unit.

## Against Din Sync's unit by ear (D324)

Measured on 2026-11-24 with CoolSynth303's `tools/listening-pass` (every set rendered as Din Sync recorded it, one
level fitted across all 400 notes). The user heard the real notes start a little plucked, ours like brass.
- Din Sync's recordings are inverted against the circuit's output; the waveforms match once one is turned over.
- The steady tone matches: harmonics 1 to 40 within 0.3 dB on the saw. The difference is the first two cycles.
  Measured a whole cycle at a time (each window one period, so the phase cancels), over the 64 notes whose filter
  stands still (sets A1, A2, A4, A5): the real unit's first two cycles equally loud and its brightness falling from
  the first; ours 1.6 dB quieter in the first and brighter in the second, a swell.
- The swell: Q31 starts each note saturated, pin 1 0.07 V under its emitter, and comes out as C42 refills; pin 1
  climbs from 0.635 to 0.659 V over 10 ms, some 8 dB of gain. Lemus fitted the same unit with pin 1 80 mV lower
  against Q31 (ro-mono-303's docs/circuit.md); the BA662's input pair at 22 times its other transistors' area does
  that, and the first cycle then comes within 0.2 dB of the unit's brightness on sets A1, A2 and A4. In the engine
  (`Tb303Envelope::chipInputArea`) and the netlists (`q1area`, 1 as drawn, the references rendered at 22).
- Not the cause: C72 (the gate's delay; smaller opens faster but in two steps), a DC step from the chip's offset
  (none under the notes), the oscillator's start.
- RESONANCE: Din Sync's marks fall on our linear pot at about 0.10 to 0.15 (25 %), 0.42 to 0.50 (50 %), 0.88
  (75 %) and 1.0 on sets A, C, D and E: an S-shaped track, Whittle's "log curve at both ends" of replacement pots.
  Set B (every knob at 25 %) is the cutoff's quarter-turn residual, as Lemus's (the resonant peak some 250 cents
  high there), not the pot's. `Tb303Voice::resonanceTrack` maps the knob as t^2 / (t^2 + (1 - t)^2) (0.10, 0.5,
  0.90, 1): a quarter turn within 1.7 dB of the unit where it was up to 4.5 dB quiet.
- RESONANCE full with CUTOFF and ENV MOD high (set E): ours 3 to 5 dB loud over the note's first 80 ms. Not the pot
  (a cap at 0.88 makes A2 worse and E2 no better): both whistle above 12.8 kHz where the envelope sweeps the cutoff
  to its top, ours 6.4 dB louder and higher. Open.

## What this means for CoolSynth

- Usable as code, credited: Open303's engine (MIT); TriggerFish's 303 code from release 2.3.1 or earlier (BSD-3); Faust's diode ladder (STK-4.3); Soundpipe's (MIT); the x0xb0x firmware (MIT).
- Facts only: the GPL projects (JC303's wrapper, Corrode, sst-filters, TriggerFish from 2.4.0); a1k0n (no licence); Whittle's pages (copyright).
- The likely route, as for the 808 and 909: the circuit from the service notes in `CircuitWdf.h` (the ladder's four stages and their coupling, the accent and slide networks, the Q8 shaper, the VCA), checked against ngspice and against TriggerFish's published numbers; the layer's step sequencer gives the accent and the slide.
- To put to the user: the reference (which recordings or which owner), the solver (the whole ladder in one circuit, or per stage), whether TriggerFish's BSD code is taken or only its numbers, and how the 303's knobs map onto CoolSynth's.
