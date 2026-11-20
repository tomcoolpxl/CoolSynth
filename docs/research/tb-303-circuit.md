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

## What this means for CoolSynth

- Usable as code, credited: Open303's engine (MIT); TriggerFish's 303 code from release 2.3.1 or earlier (BSD-3); Faust's diode ladder (STK-4.3); Soundpipe's (MIT); the x0xb0x firmware (MIT).
- Facts only: the GPL projects (JC303's wrapper, Corrode, sst-filters, TriggerFish from 2.4.0); a1k0n (no licence); Whittle's pages (copyright).
- The likely route, as for the 808 and 909: the circuit from the service notes in `CircuitWdf.h` (the ladder's four stages and their coupling, the accent and slide networks, the Q8 shaper, the VCA), checked against ngspice and against TriggerFish's published numbers; the layer's step sequencer gives the accent and the slide.
- To put to the user: the reference (which recordings or which owner), the solver (the whole ladder in one circuit, or per stage), whether TriggerFish's BSD code is taken or only its numbers, and how the 303's knobs map onto CoolSynth's.
