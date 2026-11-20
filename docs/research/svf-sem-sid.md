# The SVF: the SEM morph and the SID characters

Research for TODO 20 and 21 (D229, D230), 2026-11-20. Nothing is built yet.

## The SVF today

`src/synth/v3/SvfProcessor.h`: Andy Simper's trapezoidal state-variable filter, 12 dB per octave (D38).

- Cutoff (p1), Resonance (p2), Drive (p3), Key Track (p4), Mode (p5: LP, HP, BP, Notch).
- Q 0.5 to 40, always decaying; self-oscillation is the ladder's job (D38).
- A soft limit on the band state, 4 tanh(v / 4); Drive is a tanh stage before the filter.
- No Character value yet. The ladder has none either: its colour is built in.
- Cost class 0.17, the ladder 0.56 (`VoiceCost.h`). It runs at the voice rate, twice the host rate.
- `tests/V3SvfTests.cpp` checks the four shapes, the peak of Q, boundedness at full resonance and drive with a jumping cutoff, a decaying ring, key tracking.

Thirteen bank slots use it (Observatory 5, Basement 1, Main Stage 3, Night Bus 4), in LP, BP and HP.

## The Oberheim SEM filter

A 12 dB per octave state-variable filter giving low-pass, band-pass and high-pass at once ([Cherry Audio SEM](https://docs.cherryaudio.com/cherry-audio/instruments/synthesizer-expander-module/vcf), [Studio Electronics Modstar SEM](https://www.andertons.co.uk/eurorack-filter-modules/studio-electronics-modstar-sem-oberheim-eurorack-12db-filter)).

Its integrators use OTAs in place of resistors ([Georgia Tech ECE4450](https://lanterman.ece.gatech.edu/?p=149), [Syntiac](https://syntiac.com/syntiac_obvcf.html)).

One knob sweeps low-pass, through notch in the middle, to high-pass.

- Between the ends and the middle it gives notches leaning to low-pass or high-pass ([Sound On Sound, Tom Oberheim SEM, 2010](https://www.soundonsound.com/node/4906533)).
- On the original, band-pass was a position of the same selector; the 2010 re-issue gave it its own switch (same review).
- The notch is low-pass plus high-pass, the inverse of the band-pass (Modstar listing).

It does not self-oscillate.

- Sound On Sound names this as the difference from the Minimoog, 2600 and Odyssey.
- Doepfer's A-106-5 clone keeps it, with a mod to add oscillation ([Thomann](https://www.thomann.co.uk/doepfer_a1065_sem_filter.htm)).
- Mystran's explanation: the resonance pot only divides the band-pass feedback, and diodes there always conduct a little, so damping never reaches zero ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=7660325)).
- In the same thread Urs Heckmann and Andy Simper solve a saturating SVF with a per-sample tanh(x)/x slope, or a table.

How others show the morph:

- Arturia SEM V: a seamless LP, notch, HP sweep, and a separate band-pass mode ([Arturia](https://www.arturia.com/products/software-instruments/sem-v/overview)).
- Cherry Audio: an LP>HP/Notch knob; its BP switch greys the knob out.
- Surge's OB-Xf: Mode blends LP, notch, HP; a Band-Pass button swaps the notch for band-pass, so the knob then blends LP, BP, HP ([OB-Xf manual](https://surge-synth-team.org/ob-xf/manual/filter/)).
- Brainmodular's SEM steps instead: LP, BP, HP, notch at fixed points ([Usine](https://www.brainmodular.com/manuals/hh7/en/modules/audio/filters/audio-filter-oberheim-sem)).
- Behringer's 2-XM rebuilds the Two Voice with SEM-style filters; Oberheim's TEO-5 carries the SEM filter ([Gearnews](https://www.gearnews.com/celebrating-the-sem-filter-synth/)).
- AAS: an LP/HP mix that is a notch when centred ([AAS Multiphonics CV-1](https://www.applied-acoustics.com/multiphonics-cv-1/manual/state-variable-filter/)).

The maths:

- Simper mixes the input and two states with m0, m1, m2 for every response and notes that interpolating them morphs between responses, stable even under audio-rate modulation ([Cytomic, SvfLinearTrapOptimised2](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)).
- Zavalishin: notch is 1 - 2R times band-pass, which equals LP plus HP; the 2R tap is the unit-gain band-pass; a nonlinear SVF saturates the integrators or boosts the band-pass feedback ([The Art of VA Filter Design, 5.3 and 5.4](https://noisehack.com/research/VAFilterDesign_1.0.3.pdf); [2.1.0](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)).

A plain crossfade (1 - m) LP + m HP has its zeros at the cutoff times sqrt((1 - m) / m).

- The notch slides down from the top as m rises, sits on the cutoff at 0.5 and leaves at the bottom: the leaning shapes Sound On Sound hears.
- At m 0.5 both sides sit at -6 dB. That is the arithmetic of a pot between two outputs; no source gives the SEM's own level there.

## The SID filter

One 12 dB per octave two-integrator filter for the three voices; LP, BP and HP are switched in by three bits and summed unweighted ([reSID filter.h](https://sources.debian.org/src/sidplay-libs/2.1.1-16/resid/filter.h)).

- LP+HP is the notch; LP+BP a brighter low-pass ([SID-Article](https://github.com/libsidplayfp/SID-Article)).
- Band-pass falls 6 dB per octave ([Wikipedia, MOS Technology 6581](https://en.wikipedia.org/wiki/MOS_Technology_6581)).
- Cutoff is an 11-bit register; resonance 4 bits, 16 steps.

Cutoff range:

- The datasheet's 30 Hz to 12 kHz, linear: the 8580 follows it closely; reSID uses 0 to 12.5 kHz ([reSID filter.cc](https://sources.debian.org/src/sidplay-libs/2.1.1-16/resid/filter.cc)).
- reSID measured one 6581 with 470 pF capacitors: 220 Hz at register 0, 6 kHz at 1023, a drop to 4.6 kHz at 1024, 18 kHz at the top.
- SID-Article: the 6581 floors near 200 Hz, so basses sound muffled, but reaches the top of hearing; the 8580 goes near 0 Hz and stops near 13 kHz.

Resonance:

- 6581: 1/Q = (15 - step) / 8 (reSID 1.0 and reSIDfp) ([reSID filter.cc](https://raw.githubusercontent.com/libsidplayfp/resid/master/filter.cc), [libresidfp](https://github.com/libsidplayfp/libresidfp)).
  - Q 0.53 at 0, 2.7 at 12, 4 at 13, 8 at 14, no damping at 15: little change until the top steps, as the SID-Article says.
- 8580: two small resistor DACs give 1/Q = 2^((4 - step) / 8), Q 0.71 to 2.6 in even steps (libresidfp `Filter8580.h`).
- Older reSID used Q 0.707 to 1.7 for both.
- Self-oscillation: no clear source. A forum user heard none on a 6581 ([MIDIbox](https://midibox.org/forums/topic/7129-feedback-loop-on-sid/page/2)); a VICE bug has the emulated 8580 ringing on after the gate closes ([VICE 1698](https://sourceforge.net/p/vice-emu/bugs/1698)).

The 6581's colour (libresidfp `Filter6581.h` and `Integrator6581.h`, after Dag Lem, Michael Huth's die photos and Tommi Lempinen's vectors):

- The op-amps are self-biased NMOS inverters; the cutoff resistors are NMOS transistors set by the 11-bit DAC.
- The DAC lacks its bit-0 termination and is loaded by the signal: the curve kinks.
- The transistor's current is K (Vgst^2 - Vgdt^2), so its conductance moves with the signal on it.
- So the distortion follows the level at HP and BP, mostly on one half-wave, and the cutoff drifts up as the level rises ([Wikipedia, reSID](https://en.wikipedia.org/wiki/ReSID)).
- The curve differs widely from chip to chip and with warmth; the 8580 was redesigned to be linear and temperature-stable.
- A little sound passes even at volume 0, and the volume shifts the DC level (the digi trick), far less on the 8580 (SID-Article).

Emulations:

- reSID is Dag Lem's; Antti Lankila's 6581 distortion work (2007 to 2010) became reSIDfp, kept by Leandro Nini in libsidplayfp.
- reSIDfp solves each integrator in one fixed-point step on tabulated transistor and op-amp curves.
- Its users get a filter curve, 0 dark to 1 bright, default 0.5 ([sidplayfp.ini](https://manpages.ubuntu.com/manpages/questing/man5/sidplayfp.ini.5.html)); the 8580 has one too.
- Plogue chipsynth C64 models 32 chips, every 6581 and 8580 revision ([Plogue](https://plogue.com/products/chipsynth-c64.html)).
- inSIDious, its own engine, picks 6581 or 8580 and offers six 6581 curves ([Synthtopia](https://www.synthtopia.com/content/2020/07/15/new-plugin-insidious-brings-classic-commodore-64-sound-to-your-daw/)).

What musicians hear:

- The 6581: dirty, moody, vintage, weak resonance, muffled bass, often distorting.
- The 8580: clean, exact, fatter, with stronger resonance and almost no distortion.
- Sources: [MIDIbox](https://forum.midibox.org/t/differences-between-6581-and-8580-chips/2411), [MIDIbox](https://midibox.org/forums/topic/2365-differences-between-6581-and-8580-chips), Wikipedia.

## The SEM morph: options

### Option A, recommended: a Morph mode and a Morph value

- Mode gains a choice at the end: LP>HP.
- A new value, Morph, 0 to 1, default 0 (low-pass), modulatable, ramped per sample. It acts only in LP>HP and is greyed out otherwise.
- Output: (1 - m) LP + m HP, divided by max(m, 1 - m), so both pass-bands stay at 0 dB and the middle is today's notch to the bit.
- Without the division the middle dips by 6 dB, as a pot would: the one choice inside this option.
- BP stays its own mode, like the re-issue's switch.
- Cost: three multiplies and a division a sample.
- Tests: m 0 is LP and m 1 is HP; m 0.5 notches below -40 dB at the cutoff with 0 dB beside it; at m 0.25 the notch sits at sqrt 3 times the cutoff; a fast LFO on Morph stays bounded without clicks.

### Option B: Mode itself turns continuous

- Mode becomes a modulatable knob, 0 to 3; between 0 and 1 it is the LP to notch to HP crossfade.
- No new value or choice, so no migration.
- But HP to BP and BP to notch are not SEM shapes; the knob shows numbers, not names; the interface and MiniLab code that treat Mode as a list change.

### Option C: an SEM character

- A Character value (the SID needs one anyway) gets SEM: option A's morph plus the SEM's colour.
- The colour: saturating OTA integrators and damping that grows with level, as the diodes do (Zavalishin 5.4).
- More sound, at about the 6581's cost, but it bundles a colour the user did not ask for. It can come later on top of A.

## The SID: options

All three keep Cutoff in Hz, so Key Track, Brightness and octave modulation work as now.

### Option A: one SID character with a Chip switch

- Character gets SID; another choice picks 6581 or 8580.
- Two values for one choice, and a switch that means nothing in Clean.

### Option B, recommended: two characters

- A new Character value: Clean, SID 6581, SID 8580, new names only at the end.
- A new Curve value, 0 dark to 1 bright, default 0.5, as reSIDfp's; greyed out in Clean.
- The names match the Chip generator's.

### Option C: a SID processor type

- Its own values: register, three mode switches for all eight sums, chip, curve.
- Closest to the chip, but it repeats the SVF's code, and the user chose a character of the SVF (D230).

### What the characters model

SID 8580:

- Ceiling 12.5 kHz, floor 30 Hz.
- Resonance in 16 steps, 1/Q = 2^((4 - step) / 8), each change ramped so it does not click.
- Clean integrators, and a soft limit that bites only when high resonance lifts the level.
- Curve scales the cutoff a little, as reSIDfp's 8580 curve does.

SID 6581:

- The knob is read as the register an 8580 would need, then through a 6581 curve: one shared 2048-point table, fitted to reSID's points (220 Hz floor, the drop at 1024, 18 kHz top). Curve moves floor and slope together.
- The other way keeps the knob's Hz true and adds only the 200 Hz floor: honest readings, but sweeps lose the dead bottom and the kink.
- Resonance: 1/Q = (15 - step) / 8, capped at Q 16 at step 15, a long ring that decays, as D38 keeps for the SVF. Letting step 15 sing at the level the saturation allows is the other way.
- Distortion, from the transistor law: each integrator's g is scaled by (1 - b u), u being what it integrates (HP for the first, BP for the second), held between 0.25 and 2.
- That gives the one-sided distortion and the cutoff that climbs with level; b set so a full-scale input moves the cutoff about a third, then tuned by ear.
- The inverter stages: an asymmetric soft clip on the summer and both integrators.
- Leak: a fixed share of the dry signal round the filter, about -50 dB, the Ladder's Leak idea.
- Left out: warm-up drift (the voice's Drift covers it), the DC steps (as in D231), the 11-bit cutoff steps (6 Hz steps would zipper bass sweeps).

How it is computed:

- With two g's Simper's solve stays closed: v1 = (ic1 + g1 (x - ic2)) / (1 + g1 (k + g2)), v2 = ic2 + g2 v1. With g1 = g2 it is today's code.
- The g's and clips come from the last sample's values, the Ladder's linearised zero-delay method; one pass, two near Nyquist.
- Per sample and channel: a division and two or three rational tanh; left and right side by side in SSE as in D138.
- Estimate: Clean and 8580 at 0.17, the 6581 about 0.4. `VoiceCost.h` holds one figure per type, so it needs one per character, or every SVF counts at the 6581's cost and polyphony falls.
- At twice the host rate the 18 kHz top is far from Nyquist and the nonlinearity aliases little.

New modes for every character: the SID's LP+BP and BP+HP could join the Mode list with LP>HP, so one format step covers all three.

Tests:

- Clean renders today's samples to the bit in all four modes.
- 8580: the peak at each step matches 2^((step - 4) / 8); nothing above 12.5 kHz.
- 6581: a 20 Hz knob gives at least 200 Hz; the curve follows its table; Curve 0 is darker than 1.
- 6581: a quiet sine stays clean (harmonics under -50 dB); a full-scale one gains a second harmonic above -30 dB; the band-pass peak rises with level.
- Every character at full resonance and drive with a jumping cutoff stays bounded; a step-15 ring decays.
- Old saved states load with their modes, in Clean.

## Risks to existing sounds

Bank presets are safe: `.csbank` writes Mode by name (mode=lp), and unwritten values keep their defaults, so they load in Clean with Morph 0.

Saved patches and DAW projects need a migration.

- Slot values are stored 0 to 1 over the type's range; Mode's range grows from 3 to 4 (or 6).
- With five choices a saved BP (0.667) reads 2.67 and rounds to Notch; a saved Notch becomes the new mode.
- This is D221's case (Noise gained Brown): a format version step and a rescale of every SVF slot's stored Mode.
- The same step sets an older SVF slot's p6 to p8 to their defaults, in case a host type change left values there, and drops mod routes to them, which did nothing until now.
- DAW automation of an SVF's Mode cannot be migrated and would need redrawing. Mode is not modulatable inside CoolSynth, so only host lanes are touched.

The Clean path must not change: the thirteen bank slots and every user sound rely on it, so the first test is a bit-exact render against today's SVF.

Index plan: Morph p6, Character p7, Curve p8, inside the first eight the matrix numbers (`ModMatrix.h`), four left free. Whichever lands first takes p6.
