# The TR-909 bass drum as a circuit

Research for a circuit model of the 909 kick, after the 808 became its circuit (D235, D236).
Today's 909 in `KickGenerator.h` is behavioural: a sine with a pitch envelope, a tanh shape, a click pulse and noise.
Researched online on 2026-11-20, online implementations first, before anything is designed; every source is linked where it is used and listed at the end.

How to read the claims:
- "Read" means the page was fetched and read through the fetch tool; "search summary" means only a search engine's summary was seen.
- No PDF was opened. PDF-only sources are listed with their URLs and marked unopened.
- One image was seen: Robin Whittle's PNG of the service notes' bass drum schematic. The fetch tool cached it locally on its own (in the session's tool-results folder, not the project); I read the cached copy and fetched no other image.

## What the 909 kick is

### The schematic

The bass drum sits on the voicing board, drawn on page 11 of the service notes.
- Whittle's crop of that page, which I read: [TR-909-schematic-voicing-board-BD-only.png](https://www.firstpr.com.au/rwi/tr-909/TR-909-schematic-voicing-board-BD-only.png), linked from his [TR-909 page](https://www.firstpr.com.au/rwi/tr-909/).
- The service notes are on ManualsLib ([1000500](https://www.manualslib.com/manual/1000500/Roland-Tr-909.html), [1026406](https://www.manualslib.com/manual/1026406/Roland-Tr-909.html)), 16 pages; the link in the brief (947469) is the owner's manual.
  - ManualsLib's pages are scans: fetching `?page=7` and `?page=11` returned only the site's frame, no text. Nothing was read there.
- Archive.org has an OCR text of the service notes ([djvu.txt](https://archive.org/stream/synth_Roland_TR-909_service_notes/Roland_TR-909_service_notes_djvu.txt)), read; badly garbled.
  - The circuit descriptions cover the snare, cymbals, CPU and tape, not the bass drum; they say the snare, bass and toms "operate basically in the same manner" and share one noise generator.
  - The change list: C9 from 0.22 µF to 0.33 µF "for expanding the TUNE range".

What follows is from the schematic image, checked against two independent readings of it: the mw909 research notes ([01-voices-analog.md](https://github.com/mattWoolly/mw909/blob/main/research/01-voices-analog.md)) and the constants in the operator project's code ([bd/mod.rs](https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/bd/mod.rs)), both read.
Where the three agree, a value is given plainly; where the image is unclear to me, it says so.

### Trigger

- The sequencer's trigger is a 5 V pulse, 2 ms long; each voice turns it into a 15 V pulse on its own transistors ([operator circuit.rs](https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/circuit.rs)).
- TRIG enters through R1 22 k and R2 10 k to Q1; Q1 drives Q2 (PNP, from +15 V, R3 100 k, R4 47 k, D4), whose collector is the unaccented +15 V pulse.
- C6 0.0022 µF and R11 4.7 k at the input; R19 2.2 k at Q1.
- C1 0.068 µF and R12 100 k to Q7 (R13 47 k), which drives Q11's base through R47 330 k: the VCO's reset.

### Pitch envelope (ENV3)

- The +15 V pulse charges C9 0.33 µF through D7 and R21 2.2 k.
- C9 drains through R57 22 k plus TUNE (VR2, 100 k audio taper) to ground, and through R26 330 k into IC12a's virtual ground.
- Time constant, τ = C9 ((R57 + VR2) ∥ R26): 6.8 ms at TUNE minimum to 29.4 ms at maximum (mw909; my arithmetic agrees).
- So TUNE is the sweep's decay, not the pitch. Whittle: the Tune control "does not control the overall frequency" (as quoted by mw909); Fraser calls it "really a decay control" of the falling sweep.
- Early units, below serial 381500, have C9 0.22 µF: τ 4.5 to 19.6 ms (mw909).
- Accent does not reach the pitch: C9 charges from the unaccented pulse. mw909 first read the block diagram the other way and corrected itself from the schematic and recordings.

### Control voltage

- IC12a sums the envelope and an offset: R28 150 k feedback, R27 1.5 M from +15 V, R25 100 k on the other input.
- V_cv = (R28/R27) 15 V + (R28/R26) V_C9 = 1.50 V + 0.4545 V_C9 (operator's code; mw909).
- C9 charges to about 14 V in the 2 ms pulse (my estimate: 15 V less a diode, R21 against the drain), so the peak V_cv is about 7.9 V, 5.3 times the resting 1.5 V.

### VCO

A triangle oscillator of an integrator and a comparator, with a switched inverter:
- IC12b with R29, R30, R31, R23 100 k and the switch Q10 (base through R24 22 k from the comparator) passes plus or minus V_cv: a ±1 stage whose sign the comparator sets.
- R59 47 k into the integrator IC13a with C14 0.033 µF: the triangle.
- IC13b is the comparator with hysteresis, R49 100 k and R50 47 k: the thresholds are its output swing divided by these, about ±4.3 V with a 13.5 V swing (both readings).
- Q11 shorts C14 through R51 2.2 k while the trigger lasts, so every hit starts from the same phase. network-909 (search summary only, the site refused the fetch): "a positive pulse at the base of Q11, discharging C14 and resetting VCO at every TRIG period".
- Frequency: f = V_cv / (4 V_th R59 C14). With 1.5 V and 4.32 V: 56.0 Hz (mw909 and operator; my arithmetic agrees). At the peak, about 295 Hz.
- The frequency is linear in V_cv, so the sweep is an exponential in hertz, f(t) = f_b + Δf e^(−t/τ), not an exponential in pitch as most synthesised kicks use.
- Operator's notes call the comparator an "inverting Schmitt with PNP Q10"; mw909 calls Q10 the ±1 stage's switch. The image shows Q10 at IC12b, so I read it as the switch.

### Waveshaper

- Triangle through R52 10 k into D10 and D11, anti-parallel to ground; then R53 2.2 k and R43 47 Ω to the VCA transistor's emitter.
- mw909 gives D10 and D11 as 1SS133; Whittle (quoted by mw909): the diodes "turn on around 0.5 to 0.6 volts above and below ground".
- This is a soft clip that rounds the triangle's corners; it is not a sine converter in the textbook sense.
- Fraser's and Whittle's "drive" mod replaces R52 with a pot, pushing the wave toward a square (mw909). The 9090 builder fitted a switch for 1N4148s, red LEDs or no diodes ([subatomicglue 9090 log](https://www.subatomicglue.com/9090l0g/), read).

### VCA

- Q12, a single transistor, takes the shaped wave at its emitter (a few tens of millivolts across R43); its base is fed from the decay envelope's C8 through R54 470 k; its output goes to IC11a's summing node.
- Q12 is marked (S) on the schematic, a selected part; its type is not on the page.
- Its gain follows the base current above a threshold of about 0.4 to 0.6 V; below it the sound stops abruptly (mw909, after Whittle). Whittle, as quoted: C8 "is discharged through a resistor into the base of the transistor, with this being the only discharge path".
- Electric Druid describes Roland's single-transistor "swing type" VCA, which "turns up in many of their instruments, being pressed into service in the TR-909" ([DR-110 article](https://electricdruid.net/?p=71361), read); there it is drawn the other way round, the audio at the base.

### Decay envelope (ENV1) and hold

- Accent buffer: IC11b, gain 1 + R37/R38 = 1 + 10 k/4.7 k = 3.13.
- Q4 passes the accent voltage, through R35 1.8 k and D9, into C8 0.33 µF. A divider (R22 10 k, R36 47 k) and D8 clamp C8 at about 2.6 V, so accent saturates above about 80 % (mw909).
- The hold: the +15 V pulse charges C7 0.033 µF through D6 and R20 6.8 k; C7 drains through R18 470 k into Q3 (R17 470 k), which keeps Q4 on through R32 47 k (R33 1 M).
- DECAY: VR5 1 M audio taper plus R58 47 k from C8 to ground, in parallel with R54 into Q12's base.

### Attack (click and noise)

- Pulse: Q5 passes the scaled accent (R34 100 k, R39 1 M, D5) through R6 22 k to Q8 (R5 100 k, C5 0.0068 µF, R14 22 k).
- C2 0.0033 µF differentiates it into a Sallen-Key low-pass around Q9: R7 100 k, R16 39 k, C3 0.0047 µF, C4 100 pF, R15 100 k; about 3.7 kHz with Q about 3.1 (mw909): a ringing click that grows with accent. C11 0.47 µF, R8 10 k, R44 22 k follow.
- Noise: the shared noise generator through R10 12 k and R9 4.7 k, C15 0.47 µF (high-pass near 26 Hz), R46 4.7 k with C13 0.1 µF (low-pass near 500 Hz), R45 4.7 k, to a node with R56 330 Ω to ground.
- ATTACK: VR3, a 500 Ω rheostat, with R55 47 Ω to Q6's emitter; Q6 is the click's own VCA, its base fed by ENV2: C12 0.033 µF charged through R41 22 k, R42 470 k to the base, τ about 0.7 ms.
- R495 150 k is fitted on units up to serial 371499 only, as the schematic notes.
- The noise generator is a CD4006 shift register, shared with the snare, toms and clap ([electro-music thread](https://electro-music.org/forum/post-357936.html), read; a builder's statement).

### Output

- IC11a sums Q12's and Q6's currents; LEVEL (VR4, 100 k linear) is its feedback resistor, so level is linear in rotation.
- Rails ±15 V; the op-amp types are not printed on the page. mw909 assumes 4558-class parts with ±13.5 V swing (unverified); operator uses 13.5 V.

### Parts types

- The 9090 builder replaced BC549 and BC559 with 2SC2603F and 2SA1115-F, "both original 909 parts" ([subatomicglue](https://www.subatomicglue.com/9090l0g/), read).
- Operator's code uses the 2SC945's saturation current and the 2SD1469 muting transistor for some VCAs (in the snare, by mw909's account).
- I could not confirm the bass drum's transistor types from the parts list: the ManualsLib scans have no text and the OCR does not tag parts by voice.

### What the real machine measures

From mw909's measurements of recordings of one unit ([05-emulation-techniques.md](https://github.com/mattWoolly/mw909/blob/main/research/05-emulation-techniques.md), read), with a second unit for pitch:
- Settled pitch 49.2 to 49.7 Hz, not the derived 56 Hz: 12 % low. A second unit settles within 0.3 % of the first.
- The sweep: f(0) 279 to 296 Hz, Δf 229 to 247 Hz, τ 7.6 ms (TUNE 0) to 29.1 ms (TUNE 100). The τ matches the RC exactly; the base does not.
- Harmonics of the settled tone: H2 −28, H3 −26, H4 −45, H5 −41, H6 −47, H7 −41 dB. A pure triangle has H3 −19 dB and no even harmonics; the even ones and a +/− asymmetry are unexplained by a matched diode pair (VCA or diode mismatch, unverified).
- The shaping holds as the level falls 20 dB, so the shaper is before the VCA.
- A hold, flat within 0.5 dB, of about 55 to 65 ms at every DECAY and accent; derived only 47 to 50 ms.
- Time to −20 dB at DECAY 0, 25, 50, 75, 100: 75, 115, 135, 175, 235 ms in the first pass; a re-measurement gives about 96 and 298 ms at the ends. The derived model runs 17 to 22 % short.
- Hold plus a thresholded exponential fits the decay twice as well as a plain exponential.
- ATTACK spans about 7 dB on the first 2 ms; the click peaks at 1.5 ms; TOTAL ACCENT spans about 6.2 dB.

The derived numbers miss the measured ones in exactly the transistor-level places: the comparator's swing, Q3's gain in the hold, Q12's threshold. A circuit model that gets these right without fitting would be the strongest check of it.

## Implementations found

### Operator (Shoreline), Rust, AGPL-3.0

[Pull request 169](https://github.com/HelveticaScenario/operator/pull/169), [repository](https://github.com/HelveticaScenario/operator), read.
- Circuit-level: "TR-909 bass drum, simulated from the 909 service notes' voicing board", the designators as the service notes'.
- Structure: the pitch envelope as an exact RC update on C9; the VCO as an integrator reflecting between the comparator's thresholds; the shaper and Q12 solved together by Newton; the decay and attack networks as small modified-nodal-analysis systems with precomputed switch states; transistors by Ebers-Moll.
- The VCO ([vco.rs](https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/vco.rs)): each sample moves the triangle by slope × dt; on reaching a threshold it snaps there, flips direction and spends the rest of the sample's travel the other way. No BLEP or oversampling.
- The shaper's Newton ([circuit.rs](https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/circuit.rs)): two unknowns, the diode node and Q12's collector, a 2 × 2 Jacobian, at most 20 iterations, steps clamped to 0.1 V and 0.01 V, warm-started.
- Checked by 15 behavioural tests; its ngspice cross-check covers only the clap.
- Lessons: the 909 splits cleanly at its op-amp outputs, and a hybrid of exact RC updates, an event-driven VCO and small Newton roots works. Licence: facts only; its code cannot go into MIT CoolSynth.

### mw909, C++/JUCE plugin, AGPL-3.0-or-later

[Repository](https://github.com/mattWoolly/mw909), read; the research notes cited above.
- "The analog voices are simulated in volts, part by part"; the kick is a triangle whose integrator is shorted for the 2 ms trigger, swept from 302 to 49.5 Hz, shaped by the diode pair.
- The research notes are the best circuit description found: every stage, every value, the equations, measurements against recordings, open conflicts listed.
- Its emulation notes recommend a behavioural discretisation: a phase accumulator linear in V_cv, exact one-pole envelopes, a static shaper, a thresholded VCA with a hold timer.
- On aliasing: a naive triangle at 48 kHz aliases at −88 dB at 49 Hz to −65 dB at 291 Hz; with polyBLAMP on the corners and polyBLEP on the reset, "1× is enough for every 909 analog VCO".
- Lessons: the measurements and the open conflicts; the check list for any model. The notes are AGPL-licensed text; facts can be used, text and code not copied.

### model909, C, MIT

[Repository](https://github.com/DMDaudio/model909), read; [909-circuits.md](https://raw.githubusercontent.com/DMDaudio/model909/main/research/909-circuits.md), read.
- For the Elektron Model:Cycles: "integer models of the 909's circuits", constants fitted to recordings of a real 909 (`src/t9_cal.h`, generated by scripts in `proto/`).
- Its circuit notes condense mw909's and add the 9W9 project's measurements, with conflicts marked.
- Lessons: an MIT summary of the same facts; a fitting pipeline. Behavioural at heart, fixed-point.

### Replicant909, C++, MIT

[Repository](https://github.com/DanimanX/Replicant909), read.
- Claims "component-level circuit topology"; the README describes a triangle VCO swept by "a dual-slope exponential pitch drop (280 Hz down to 42–75 Hz)", a "diode-transistor soft waveshaper", decay 0.15 to 1.1 s, accent deepening the chirp.
- No values, no solver named. The accent-on-pitch claim contradicts the schematic. Little to learn.

### Mz909, C++/JUCE, AGPL-3.0

[Repository](https://github.com/simozzer/Mz909), read.
- Behavioural: the README says its numbers are "reasoned from the 909's structure and sound, not measured from a real TR-909".
- A 1 ms trigger, accent raising the sweep's start: both contradict the schematic. Not a reference.

### 9W9 (Schwung, Ableton Move), GPL-3.0

[Repository](https://github.com/synthfr/schwung-9W9), read; [Synth Anatomy](https://synthanatomy.com/2026/08/athousanddetails-9w9-ableton-move-schwung-now-has-a-tr-909-drum-machine.html), search summary.
- "Circuit-modelled", checked against recordings; "where the two disagree, the schematic wins".
- Kick: a fixed base near 49 Hz, the additive-exponential sweep, a "16 ms pitch hold", diode rounding.
- The 16 ms pitch hold conflicts with the 2 ms reset in the schematic; unexplained.

### Lemus ro-drums-909, AGPL-3.0

[lemusdev.org](https://lemusdev.org), [repository](https://github.com/lemus-dev/lemus), read; [dawplugins.net](https://dawplugins.net/vst-plugins/instruments/drum-software/ro-drums-808-909/), read.
- Claims the voicing board "modeled component by component from the service notes", compared with ngspice, calibrated to AudioRealism's recordings.
- I could not find the 909's files or any netlist in the repository; unconfirmed.

### Mutable Instruments Plaits, MIT

[Manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/), read; [synthetic_bass_drum.h](https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/drums/synthetic_bass_drum.h), read.
- The manual's drum model AUX: an FM'd triangle "turned into a sine with a pair of diodes", a dirty VCA; "behavioral simulation".
- The code: a triangle soft-clipped by 2x/(1 + |x|), crossfaded to a sine by "dirtiness"; the phase held for about 1.3 ms at the trigger; the "TransistorVCA" offsets by 0.6 and soft-clips, so the VCA distorts more when loud.
- Lessons: cheap shapes for the diode pair and the threshold VCA; behavioural.

### VCV Drums (Vult), closed source

[VCV Drums](https://vcvrack.com/Drums) and [Vult's making-of](https://www.vult-dsp.com/post/making-of-the-vcv-drums), read.
- The Kick has 808-like and 909-like models; "Virtual analog models of vintage drum machines".
- Vult built a simulation from the schematic, measured commercial 909 PCBs with test points, then optimised the code.
- On the shaper: "If the triangle waves have a small DC offset the clipping will be different", a likely source of the measured even harmonics.

### Owen Vallis, Reaktor

- CDM credits him with "intensive research into the TR-909" in 2016, as a Reaktor Core tutorial ([CDM](https://cdm.link/make-a-909-kick-on-the-make-noise-0-coast-and-more-drum-modeling-fun/), read); the video's link is not in the text.
- The Reaktor User Library's "909 Kick Synth" ([entry](https://www.native-instruments.com/reaktor-community/reaktor-user-library/entry/show/9877/)) returned 404 to the fetch. Search summaries say it was built from the schematics, the author simulated each part in LTspice and found the oscillator rich in harmonics even after the diodes. Unconfirmed.

### Other things searched

- No Faust, SuperCollider, Pure Data or Csound model of the 909 kick's circuit was found. The Axoloti patch ([thread](https://sebiik.github.io/community.axoloti.com.backup/t/emulation-of-909-kick-drum/468), read) was tuned by ear against ReBirth.
- Synth Secrets calls the oscillator a sawtooth, without a schematic ([SOS](https://www.soundonsound.com/techniques/practical-bass-drum-synthesis)); the schematic shows a triangle.
- No academic paper on any 909 voice was found: no DAFx, ICMC or AES title, no thesis. The nearest are Werner's 808 papers ([DAFx 2014](https://www.dafx.de/paper-archive/details/hwkd1Lfe8vmtOxCEjsNY-g), read; the [808 cymbal](https://ccrma.stanford.edu/papers/keyword/398), search summary), whose cymbal paper models six Schmitt-trigger oscillators; I did not open it, so how it treats them is unconfirmed.

## Commercial claims

- Roland ACB (TR-8, TR-8S, Roland Cloud TR-909): "part-by-part analyses and faithful recreation" ([TR-8](https://www.roland.com/au/products/tr-8/), read); the software 909 by "carefully analyzing original hardware units, circuit diagrams, and other historical data" ([TR-909 software](https://www.roland.com/global/products/rc_tr-909/), read). Roland's engineers say the 909 kick is made of "multiple circuits such as Enveloper, VCO, VCA" (search summary of the TR-8 pages). Nothing on method.
- MusicRadar's TR-8 review: a 32-bit, 96 kHz ACB engine ([review](https://musicradar.com/reviews/tech/roland-aira-tr-8-594315), search summary).
- Synsonic BD-909: "We have analyzed the original circuit and created an accurately digital model" ([product](https://synsonic-instruments.com/synsonic-bd-909/), read); extras Pitch, Tune Depth, Hold, Noise Decay, Distortion. Its manual is a PDF, unopened.
- D16 Drumazon 2: "matching each individual instrument with meticulous accuracy" ([product](https://d16.pl/drumazon2), read); nothing on the kick's method.
- AudioRealism ADM: generators "carefully modeled after the classics' analog circuits" (manual, search summary; the [Scribd copy](https://www.scribd.com/document/370622048/Audiorealism-ADM-Manual-pdf) unopened).
- Behringer RD-9: analog, with an authentic mode and an enhanced mode that adds bass drum Pitch and Pitch Depth ([gearnews](https://www.gearnews.com/behringer-rd-9-rhythm-designer-full-details-and-price), search summary). No schematic found.
- Tiptop BD909: "faithfully recreates the TR-909's all-analog bass drum circuit", adding tune with CV, tuning attack, extended tuning decay and a waveshaper ([Tiptop](https://tiptopaudio.com/909-2/), read).
- Hexinverter Mutant BD9 (now built by Erica Synths): 909-based, switchable sine or square, a sub-oscillator, drive ([Erica Synths](https://www.ericasynths.lv/shop/eurorack-modules/by-series/hexinverter/hexinverter-mutant-bd9), search summary). No schematic published.

None publishes a method. The open projects above say more than all the commercial ones.

## DIY clones

- The 9090 project, Trevor's clone of the 909's synth section ([9090project.co.uk](http://www.9090project.co.uk/), not reached); the 9090 build log ([subatomicglue](https://www.subatomicglue.com/9090l0g/), read).
- Jörg Schmitz's board ([analog-synth.de](http://www.analog-synth.de/synths/tr909/tr909.htm), read): the 909 kick plus a compression pot and adjustable noise; its schematic is a JPG (unopened), its PCB a PDF (unopened).
- Builders found errors in it: a continuously running oscillator, a 330 Ω that should be 330 k, unmarked polarised capacitors; furio's debugged version is a PDF, unopened ([thread](https://electro-music.org/forum/post-357936.html), [Eagle thread](https://electro-music.org/forum/post-439055.html), both read).
- Hexinverter's "Nein oh Nein" kick is a clone with the CD4006 noise and a trigger conditioner; its schematic is a PNG on electro-music, unopened.
- HexInverter's point: 5 to 20 % tolerances make every original sound different.
- Colin Fraser's mods ([page](http://www.colinfraser.com/tr909/909mods/909mods.htm), the fetch failed on TLS) and Robin Whittle's sound mods ([TR-909-Sound-Mods.pdf](https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf), unopened) add drive, tune and depth controls; known here only through mw909's quotations.
- Yusynth: no 909 bass drum found.

## References for checking

SPICE:
- No published SPICE or LTspice netlist of the 909 bass drum was found; the operator project's ngspice check covers only the clap; Lemus claims one but I found none.
- So, as for the 808 (`tools/kick808/bassdrum.cir`), a netlist would be written from the schematic. It needs a 4558 macromodel (the comparator depends on the swing), 1SS133 diodes, and models for the transistors (2SC2603, 2SA1115 if those are right; 2SC945 and 2SA733 as stand-ins).

Recordings:
- Rob Roy Recordings, 1995 ([Aminet](https://wup.aminet.net/package/mods/smpl/tr-909), read): one 909, 44.1 kHz mono 16-bit, 24 bass drums named by knob: TUNE 0, 3, 7, 10, ATTACK 0 and 10, DECAY 0, 3, 7, 10 (`bt3a0d7.wav` and so on).
  - Terms: "This sample set is FREE", copy freely, do not change it, "You may not distribute these samples for profit!!" Fit for analysis, never for shipping. mw909 notes they are normalised, so only pitch and timing compare.
- AudioRealism, 2025, by mw909's account: one original unit, 96 kHz 24-bit, individual outputs, part level 50 %, TUNE, ATTACK and DECAY each at 0, 25, 50, 75, 100 %; "free for personal & commercial use" but "may not be distributed". I could not find the pack's own page, so its existence and terms are unconfirmed by me.
- Whittle's demo of his modded 909 (a 107 MB FLAC on his page) is a modified machine, not a reference.
- No CC0 recording of an original 909 with per-knob coverage exists, as mw909 also concluded.

## What is hard for a circuit model

The comparator:
- IC13b works saturated, with positive feedback; it is not an op-amp at its virtual ground, so it cannot be a nullor in `CircuitWdf.h` as the 808's op-amps are.
- It switches the circuit between two linear states (Q10 on or off). In a WDF that means two scattering matrices and a jump at the switching instant, which almost never falls on a sample.
- Snapping the switch to the sample grid jitters the period by up to a sample: at 96 kHz and 295 Hz about 0.3 % of a period, and a click of aliasing at every corner.
- How others handle it: operator integrates the triangle analytically and reflects at the exact crossing inside the sample; mw909 adds polyBLAMP at the corners ([Esqueda, Välimäki, Bilbao, "Rounding Corners with BLAMP"](https://dafx.de/paper-archive/details/xRM0GiuF-JmeN0_aaP9VNg), read). Nobody solves the comparator inside a WDF.
- The integrator itself is linear and exact: between flips the triangle is a straight line with slope V_cv / (R59 C14), so an event-driven VCO loses nothing.

The reset:
- Q11 shorts C14 through 2.2 k for 2 ms (τ 73 µs, seven samples at 96 kHz): a value jump at the trigger's edges. mw909 puts a polyBLEP on it.
- The trapezoidal rule rings when a switch closes on a capacitor; a backward-Euler step at the switching instant is the usual cure (my reasoning, not sourced). The alpha transform `CircuitWdf.h` already offers is that.

The VCA and the hold:
- Q12 conducts with its emitter swinging both ways by tens of millivolts, so Ebers-Moll's forward and reverse modes both matter, and its threshold sets the decay's abrupt end.
- Q3, Q4 and Q5 are PNPs; `CircuitWdf.h` has NPNs only. A PNP is the same equations with the signs turned.
- The measured hold (62 ms) and decay are longer than the RC derivation; they hang on Q3's and Q12's gains, which are unknown parts.

The 56 Hz against 49.5 Hz:
- Every reading of the schematic gives 56 Hz; two machines measure 49.5. The comparator's real swing, its threshold arrangement, or Q10's and IC12b's error must account for 12 %.
- A circuit model with stock values will be 12 % sharp until this is understood; a SPICE run with a real op-amp model is the first test.

What is easy:
- Every block is driven by an op-amp output or a transistor, so the drum splits into small trees as Werner split the 808: the pitch envelope, the control voltage, the VCO, the shaper with Q12, ENV1 with the hold, the attack network with Q6.
- The diode pair is a classic WDF one-port pair; chowdsp_wdf solves it in closed form by the Wright omega function ([chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf)); our own root solves it by Newton.
- Nothing is very stiff: the shortest time constants are the reset (73 µs) and the click's filter (about 3.7 kHz), both comfortable at 96 kHz.
- The content is low: the sweep peaks near 300 Hz; the shaper and the click are the only bright parts, and the click is a band-limited filter's output.

## Decisions for the user

The user chose on 2026-11-20 (D237): replace today's 909; the hybrid oscillator; the whole drum; stock values, then the unknowns fitted; C14 scaled with the key; the Rob Roy set, an RC4558 model, the service notes and mod PDFs, and the AudioRealism set once its terms are confirmed; facts only from the AGPL and GPL projects; each knob on its part. The options as they were put:

Where it goes:
- Replace today's behavioural 909 with the circuit, as the 808 was (D235), recommended: one 909, the true one; sounds that use the 909 change and cost more.
- A further Model, "909 Circuit", beside today's: both stay, nothing saved changes; the Model list grows and shifts stored values, as SVF's Mode did (D233).

How the VCO is solved:
- A hybrid, recommended: the analog blocks in `CircuitWdf.h` roots, the integrator and comparator as an exact event-driven triangle with the crossing found inside the sample and polyBLAMP at the corners, as operator and mw909 do; the flip and the reset are exact, and the comparator's swing becomes a named constant to calibrate.
- Everything in WDF, with two precomputed scattering matrices switched at the sample nearest the crossing: one method throughout, but period jitter and aliasing at the corners, so likely 2x to 4x oversampling of the VCO and more cost.
- A behavioural VCO (a phase accumulator linear in V_cv) inside an otherwise circuit model: cheapest, but the reset, the slope and any offset no longer come from parts.

How far to model:
- The whole drum, recommended: the pitch envelope, the VCO, the shaper with Q12, ENV1 with Q3, Q4 and the clamp, the attack network with Q8, Q9 and Q6, the accent buffer; matching the 808's depth and the measured hold and decay.
- The body only (envelope, VCO, shaper, VCA, ENV1) with today's click and noise: less work; the attack loses the accent-dependent ringing click.
- The VCO and shaper as circuit, the envelopes behavioural, fitted to the measurements: cheapest, closest to mw909's own recommendation, but no longer a circuit model in the places where the derivation and the machine disagree.

Calibration:
- Stock values first, then the few unknowns (the comparator's swing, Q3's and Q12's gains, the diodes' saturation current) fitted to the recordings, recommended: the model stays its circuit and lands on 49.5 Hz and the 62 ms hold.
- Stock values only: pure, but 12 % sharp and with a short hold, against every recording.
- Fit freely to the recordings: closest to one machine, least a circuit.

Key tracking:
- Scale C14 with the note, recommended: the whole sweep scales, so the 5.3 to 1 ratio and the TUNE knob keep their meaning, as the 808's C41 and C42 do.
- Scale the offset (R27): only the base moves; at high notes the sweep shrinks to a few semitones, at low ones it grows.
- None: the true 909, fixed near 49.5 Hz, no bass lines.

Reference material:
- An ngspice netlist from the schematic plus the Rob Roy set, analysis only, recommended: the 808's method again; the Rob Roy terms forbid only profit, and nothing ships.
- That plus the AudioRealism set: 96 kHz, finer knob steps and real levels; its terms need confirming first and it would be a further download.
- The mw909 measurements alone: no download, but numbers from someone else's analysis, not our own.

Code from other projects:
- Facts only from the AGPL and GPL projects (operator, mw909, Mz909, Lemus, 9W9), MIT code (model909, Plaits) where useful, recommended: CoolSynth is MIT, and copying AGPL code would change that.
- Our own code throughout: the same in practice for the circuit; slightly more work for the shaper's closed forms.

Downloads needing permission:
- The Rob Roy set (a small archive from Aminet, outside git).
- The AudioRealism set, if chosen (size unknown).
- The firstpr and Fraser mod documents (PDFs) and the service notes' PDF, to read the parts list for the transistor types.
- ngspice is already in `tools/` from the 808.

## Sources

- Roland TR-909 service notes, ManualsLib: https://www.manualslib.com/manual/1000500/Roland-Tr-909.html and https://www.manualslib.com/manual/1026406/Roland-Tr-909.html (scans, no text read)
- Roland TR-909 owner's manual, ManualsLib: https://www.manualslib.com/manual/947469/Roland-Tr-909.html
- Service notes OCR, archive.org: https://archive.org/stream/synth_Roland_TR-909_service_notes/Roland_TR-909_service_notes_djvu.txt
- Robin Whittle, TR-909 page and schematic crop: https://www.firstpr.com.au/rwi/tr-909/ and https://www.firstpr.com.au/rwi/tr-909/TR-909-schematic-voicing-board-BD-only.png
- Robin Whittle, sound mods (PDF, unopened): https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf
- Colin Fraser, 909 mods (fetch failed): http://www.colinfraser.com/tr909/909mods/909mods.htm
- network-909, bass drum (fetch failed, search summary only): http://www.network-909.de/bassdrum.htm
- Operator (Shoreline): https://github.com/HelveticaScenario/operator and https://github.com/HelveticaScenario/operator/pull/169
- Operator source: https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/bd/mod.rs and https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/vco.rs and https://raw.githubusercontent.com/HelveticaScenario/operator/4273dfa/crates/modular_core/src/dsp/drums/tr909/circuit.rs
- mw909: https://github.com/mattWoolly/mw909 and https://github.com/mattWoolly/mw909/blob/main/research/01-voices-analog.md and https://github.com/mattWoolly/mw909/blob/main/research/05-emulation-techniques.md
- model909: https://github.com/DMDaudio/model909 and https://raw.githubusercontent.com/DMDaudio/model909/main/research/909-circuits.md
- Replicant909: https://github.com/DanimanX/Replicant909
- Mz909: https://github.com/simozzer/Mz909
- 9W9: https://github.com/synthfr/schwung-9W9 and https://synthanatomy.com/2026/08/athousanddetails-9w9-ableton-move-schwung-now-has-a-tr-909-drum-machine.html
- Lemus: https://lemusdev.org and https://github.com/lemus-dev/lemus and https://dawplugins.net/vst-plugins/instruments/drum-software/ro-drums-808-909/
- Plaits: https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/ and https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/drums/synthetic_bass_drum.h
- VCV Drums and Vult: https://vcvrack.com/Drums and https://www.vult-dsp.com/post/making-of-the-vcv-drums
- CDM on 909 kicks and Owen Vallis: https://cdm.link/make-a-909-kick-on-the-make-noise-0-coast-and-more-drum-modeling-fun/
- Reaktor User Library 909 Kick Synth (404): https://www.native-instruments.com/reaktor-community/reaktor-user-library/entry/show/9877/
- Elektronauts thread: https://www.elektronauts.com/t/synthesizing-a-909-kick/132188
- Axoloti thread: https://sebiik.github.io/community.axoloti.com.backup/t/emulation-of-909-kick-drum/468
- Gordon Reid, Synth Secrets: https://www.soundonsound.com/techniques/practical-bass-drum-synthesis
- Electric Druid, DR-110 and the swing VCA: https://electricdruid.net/?p=71361
- Werner, Abel, Smith, DAFx 2014: https://www.dafx.de/paper-archive/details/hwkd1Lfe8vmtOxCEjsNY-g
- Werner and others, CCRMA papers including the 808 cymbal: https://ccrma.stanford.edu/papers/keyword/398 (cymbal PDF unopened: https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf)
- Esqueda, Välimäki, Bilbao, Rounding Corners with BLAMP: https://dafx.de/paper-archive/details/xRM0GiuF-JmeN0_aaP9VNg
- chowdsp_wdf: https://github.com/Chowdhury-DSP/chowdsp_wdf
- Roland TR-8: https://www.roland.com/au/products/tr-8/
- Roland TR-909 software: https://www.roland.com/global/products/rc_tr-909/
- MusicRadar TR-8 review: https://musicradar.com/reviews/tech/roland-aira-tr-8-594315
- Synsonic BD-909: https://synsonic-instruments.com/synsonic-bd-909/ (manual PDF unopened: https://synsonic-instruments.com/wp-content/uploads/2022/01/BD-909-Doku.pdf)
- D16 Drumazon 2: https://d16.pl/drumazon2
- AudioRealism ADM manual (unopened): https://www.scribd.com/document/370622048/Audiorealism-ADM-Manual-pdf
- Behringer RD-9: https://www.gearnews.com/behringer-rd-9-rhythm-designer-full-details-and-price
- Tiptop BD909: https://tiptopaudio.com/909-2/
- Hexinverter Mutant BD9: https://www.ericasynths.lv/shop/eurorack-modules/by-series/hexinverter/hexinverter-mutant-bd9
- Jörg Schmitz's 909 bass drum: http://www.analog-synth.de/synths/tr909/tr909.htm
- 9090 project: http://www.9090project.co.uk/ and https://www.subatomicglue.com/9090l0g/
- electro-music threads: https://electro-music.org/forum/post-357936.html and https://electro-music.org/forum/post-439055.html
- Erica Synths FM Drum manual (PDF, unopened; search summary on the 909's integrator and Schmitt trigger): https://www.ericasynths.lv/media/FM_DRUM_MANUAL.pdf
- Rob Roy Recordings 909 set: https://wup.aminet.net/package/mods/smpl/tr-909
- The earlier research: `docs/research/kick-generator.md` and `docs/research/kick-808-wdf.md`
