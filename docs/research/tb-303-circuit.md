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
  - Not yet compared: resonance, env mod, decay and accent (sets A2 to E5), the square.

## What this means for CoolSynth

- Usable as code, credited: Open303's engine (MIT); TriggerFish's 303 code from release 2.3.1 or earlier (BSD-3); Faust's diode ladder (STK-4.3); Soundpipe's (MIT); the x0xb0x firmware (MIT).
- Facts only: the GPL projects (JC303's wrapper, Corrode, sst-filters, TriggerFish from 2.4.0); a1k0n (no licence); Whittle's pages (copyright).
- The likely route, as for the 808 and 909: the circuit from the service notes in `CircuitWdf.h` (the ladder's four stages and their coupling, the accent and slide networks, the Q8 shaper, the VCA), checked against ngspice and against TriggerFish's published numbers; the layer's step sequencer gives the accent and the slide.
- To put to the user: the reference (which recordings or which owner), the solver (the whole ladder in one circuit, or per stage), whether TriggerFish's BSD code is taken or only its numbers, and how the 303's knobs map onto CoolSynth's.
