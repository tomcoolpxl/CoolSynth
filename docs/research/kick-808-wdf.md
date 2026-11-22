# The TR-808 bass drum as a circuit, in wave digital filters

Research for the Kick generator's next step (D232 left "Werner's full circuit in wave digital filters" for later).
Today's 808 is a struck resonator that does what Werner, Abel and Smith measured; this file plans the circuit itself.
Researched online on 2026-11-20; every source is linked where it is used and listed at the end.

## What Werner modelled

### The DAFx 2014 model

The paper splits the schematic into blocks where a low-impedance output drives a high-impedance input, so each block can be solved alone ([paper](https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf)).
- Trigger logic: Q39 and Q40 pass a 1 ms pulse at the accent voltage, 4 to 14 V.
- Pulse shaper: a low shelf (C40, R162, R163) with diode D53 clipping the falling edge to about a diode drop.
- Bridged-T network: a band-pass in the feedback of half of IC12, centred near 49.5 Hz.
- Feedback buffer: the other half of IC12 with the Decay pot VR6, fed back into the bridged-T through R170.
- Envelope generator: Q41 and Q42, which turn Q43 on for about 5 ms; Q43 shorts R165, which raises the centre frequency more than an octave and changes its Q.
- Retriggering pulse: C39, D52 and R161 put a second pulse into the bridged-T as the envelope ends; leakage through R161 and Q43's nonlinear current make the sigh.
- Tone, level and output: a passive low-pass (VR5), a divider (VR4), a high-pass and the buffer Q44.

How it is computed:
- Every block is a bilinear-transform filter in transposed direct form II, its coefficients recomputed as the effective resistance moves.
- The nonlinearities are memoryless curves: the pulse shaper's diode, the retrigger diode, Q43's current fitted to a SPICE run (a softplus with three fitted constants), and op-amp clipping.
- The loop between the bridged-T and the feedback buffer is broken by a unit delay.

What it leaves out, by its own account:
- Q43 and its interaction with the envelope generator are "oversimplified"; the envelope is an ideal voltage source.
- Loading between blocks is ignored where it is small, the op-amps are ideal apart from clipping, and the bilinear transform is not prewarped.
- It was checked against SPICE (the first 13 ms of a hit, and the pitch over 300 ms) and against a sample set of a real 808; the match is close but not exact.

### The 2016 thesis

Werner's thesis rebuilds the same drum in wave digital filters, one subcircuit a chapter ([record](https://purl.stanford.edu/jy057cz8322), [PDF](https://stacks.stanford.edu/file/druid:jy057cz8322/KurtJamesWernerDissertation-augmented.pdf), [catalogue](https://searchworks.stanford.edu/view/11891203)).
- The output filter, with standard series and parallel adaptors.
- The bridged-T, which is not a series-parallel network: a new R-type adaptor derived by modified nodal analysis, in several versions up to "Bridged-T Resonator With Feedback".
- The pulse shaper, the classic case of one nonlinear one-port (the diode) at the root.
- The full nonlinear bridged-T, with several nonlinearities solved together at the root.

The abstract says the four together make a complete model that matches the circuit almost exactly.
The thesis is copyright, with no open licence.

What I could not read: the PDF is over 10 MB, too large for the fetch tool, and the browser pane would not show it.
So which devices sit in the nonlinear root, the solver, the sample rate and the cost come from the papers below, not the thesis itself; reading it is a decision further down.

### The papers it rests on

- Werner, Smith, Abel, DAFx 2015: R-type adaptors for any topology and for multiport linear elements, from modified nodal analysis; one port can still be made reflection-free ([archive](https://dafx.de/paper-archive/details/dFlSsTeUmyOhfdA7HIi7Aw)).
- Werner, Nangia, Smith, Abel, DAFx 2015: all nonlinearities gathered as one vector at the root, under an R-type adaptor, solved jointly by a variant of the K-method ([archive](https://dafx.de/paper-archive/details/4fsq4pZNdCbUzyzAL8xsQg)).
  - Their case is the Big Muff's clipping stage: an Ebers-Moll transistor (a two-port) and a diode pair, tabulated on a 201 by 201 by 201 grid and read by scattered interpolation, at 44.1 kHz. An offline method, not one for a plugin.
- Olsen, Werner, Smith, DAFx 2016: the same roots solved by Newton's method instead of tables ([archive](https://dafx.de/paper-archive/details/py9HnDl6pMzyENpNpbK7KQ)).
- Werner and others, IEEE TCAS-I 2018: op-amps and other active multiports in R-type adaptors ([CCRMA](https://ccrma.stanford.edu/papers/modeling-circuits-with-arbitrary-topologies-and-active-linear-multiports-using-wave-digital-f)).
- Bogason and Werner, DAFx 2018: capacitors and resistors that change while the circuit runs ([archive](https://dafx.de/paper-archive/details/xg03x9t32QawVRNtU4MwZQ)).

## The circuit's values

From the paper's Figure 1, which redraws Roland's service notes; the service notes themselves are on [ManualsLib](https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=5).
- Rails: plus and minus 15 V; both op-amps are a dual µPC4558.
- Pulse shaper: C40 0.015 µF, R162 4.7 k, R163 100 k, D53.
- Bridged-T: C41 and C42 0.015 µF, R165 47 k, R166 6.8 k, R167 1 M, R161 1 M, R170 470 k.
- Q43's base through R159 100 k; retrigger C39 0.033 µF and D52.
- Envelope generator: Q41, Q42, R156 1 M, R157 8.2 k, R158 2.7 k, R160 22 k, C38.
- Feedback buffer: R164 47 k, R168 22 k, R169 47 k, VR6 500 k, C43 33 µF.
- Output: R171 220, R172 10 k, VR5 10 k, C45 0.1 µF, C47 and C49 0.47 µF, VR4, R176 100 k, Q44, R173 6.8 k.

Corrected since, against two real units (D251, `kick-808-recordings.md`): the diodes are the standard 1N4148 with
its emission coefficient (1.752), the trigger reaches the drum through Q32 and the PNP switch Q40 rather than an ideal
source, and C39 sits 20 % up, the top of its tolerance.

A check: the paper's centre frequency is 1 / (2π √(R_eff R167 C41 C42)), with R_eff = R161 ∥ (R165 + R166) ∥ R170 = 46 k; that gives 49.4 Hz.
With R165 shorted, R_eff falls to 6.7 k and the centre to about 130 Hz: the attack shift.
The paper adds that the parts' tolerances are wide (20 % capacitors, 5 % resistors), which is why no two 808s sound alike; the voice's seed (D222) could give each voice its own spread.
The transistor and diode types must be read from the service notes' parts list; SPICE models of the Japanese parts may not exist, and ChowKick uses a 1N4148 for D53.

## Libraries and open code

chowdsp_wdf ([GitHub](https://github.com/Chowdhury-DSP/chowdsp_wdf), [paper](https://arxiv.org/abs/2210.12554v1), [docs](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf/namespacechowdsp_1_1wdft.html)):
- BSD 3-clause, header-only, C++14 or newer; xsimd optional.
- A compile-time API (templates, no virtual calls, no heap) and a run-time one; the paper measures the compile-time one fastest on three of its four test circuits, and about ten times faster than RT-WDF on the R-type one.
- R-type adaptors, root or inner, with a user functor that recomputes the scattering matrix when a component changes, and deferred updates so several pot moves cost one recompute.
- Diode and diode pair by the Wright omega function; no transistor model and no multiport nonlinear root or Newton solver. That part would be ours.
- Used in Chowdhury's BYOD, ChowCentaur and ChowKick.

ChowKick ([GitHub](https://github.com/Chowdhury-DSP/ChowKick)), BSD 3-clause:
- Inspired by Werner's thesis, not a circuit model of the whole drum.
- Its pulse shaper is the 808's, in chowdsp_wdf: R162 4.7 k, R163 100 k, C40 and D53 ([PulseShaper.h](https://raw.githubusercontent.com/Chowdhury-DSP/ChowKick/main/src/dsp/PulseShaper.h)).
- Its resonant filter is a biquad with "tight" and "bounce" nonlinear modes, not a bridged-T ([ResonantFilter.h](https://raw.githubusercontent.com/Chowdhury-DSP/ChowKick/main/src/dsp/ResonantFilter.h)).

RT-WDF ([GitHub](https://github.com/RT-WDF/rt-wdf_lib), [paper](https://www.dafx.de/paper-archive/2016/dafxpapers/40-DAFx-16_paper_35-PN.pdf)):
- Werner's group's library: R-type roots and a multidimensional Newton solver for multiport nonlinearities.
- GPL, needs Armadillo, uses heap and virtual calls; the paper also warns that nonlinear parts alias and Newton's convergence is not guaranteed. A reference to read, not code to ship.

Faust wdmodels ([docs](https://faustlibraries.grame.fr/libs/wdmodels)): built on Werner's thesis, diodes by Lambert and omega functions, one fixed six-port R-type; commercial use asks the author first.

I found no open WDF or SPICE implementation of the whole 808 bass drum.
- Wolfram's System Modeler model of the drum (160 equations, 7 semiconductors, 3.5 s to simulate 4 s) has downloadable files but no stated licence ([blog](https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/)).
- EveryCircuit has a basic bridged-T ([circuit](https://everycircuit.com/circuit/5216749304676352/bridged-t-network-808-basic-drum-circuit)).

## What to check it against

SPICE:
- Werner checked against SPICE; RT-WDF used LTspice.
- ngspice is BSD 3-clause in its core, runs in batch from a script and on Windows and macOS ([Wikipedia](https://en.wikipedia.org/wiki/Ngspice)).
- LTspice is free but closed, with separate terms for its models ([Analog Devices](https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html), [model licence](https://www.analog.com/en/license/spice-models.html)).
- No 808 bass drum netlist is published; it would be written from the schematic.

The paper's figures: the transient of the first 13 ms, the instantaneous pitch over 300 ms, the pulse shaper at five accent levels, the feedback buffer's response over Decay, and a retrigger series without the machine gun effect.

Recordings:
- The paper's companion page with audio (ccrma.stanford.edu/~kwerner/papers/dafx14.html) now returns 404.
- Michael Fischer's 1994 sample set, the one the paper used: 25 bass drums named by their knob settings, from one 808, "absolutely free" but with no licence terms ([TR808.TXT](https://discmaster.textfiles.com/view/3307/Aminet%2037%20(2000)(Schatztruhe)%5B!%5D%5BJun%202000%5D.iso/Aminet/mods/smpl/tr-808.lha/TR808.TXT)). Fit for analysis, not for shipping.
- MusicRadar's SampleRadar packs are free and royalty-free but mostly processed ([378 free 808 samples](https://www.musicradar.com/news/sampleradar-378-free-808-drum-samples)).

The bends the paper's model offers: tuning, attack tuning, extended decay, input pulse width. It also names removing the sigh, bypassing the tone stage and changing the envelope's timing (C38, R156); Wolfram notes removing C43 lengthens the decay.

## Pitfalls

Aliasing:
- At 96 kHz (twice a 48 kHz host) the drum's own content is far below Nyquist; the risks are the trigger's edges and the clipping.
- The pulse's edges must be band-limited (raised-cosine edges or a band-limited step), as today's Click is.
- The diodes and Q43 act on slow, filtered signals; op-amp clipping at the rails on a hard accent is the main source. Antiderivative anti-aliasing exists for one-port WDF nonlinearities ([Albertini, Bernardini, Sarti](https://dafx.de/paper-archive/details/1VN4BTM85kVIGKymyKzBXg)) but not cheaply for a multiport root, so measure first and oversample the clip only if needed.

Denormals:
- Wave variables decay exponentially and pass through the denormal range in long tails ([de Soras](https://musicdsp.readthedocs.io/en/latest/_downloads/d81fc9af8a9fa63332b248772fbb4a54/denormal.pdf)).
- The audio threads already flush to zero (`SynthAudioProcessor.cpp`, `AudioWorkers.cpp`); JUCE's ScopedNoDenormals covers arm64 too. A rest state, as the Modal generator's, ends the work once silent.

Moving components:
- A pot is a resistor; moving it changes port resistances, and every R-type above it needs its scattering matrix recomputed (a small matrix inverse). Do it at most once a sub-block, deferred.
- Moving capacitors (key tracking, glide) changes stored charge; small smooth moves are safe, large jumps may click or ring (Bogason and Werner).
- Newton's method can fail to converge on the exponential junctions; it needs a damped step, a limit on iterations and a warm start from the last sample.

Delay-free loops:
- The paper's unit delay between bridged-T and feedback buffer shifts the decay slightly; a single R-type tree holding both removes it, which is what the thesis's "with feedback" version does.
- The rail clip, if it sits outside the tree, brings a delay back.

## What it would mean for CoolSynth

A fourth Model, "808 Circuit", beside 808, 909 and Modern, keeps today's 808 (cheap, already in Succession 808) and the shared knobs:
- Decay: VR6, read through a table so the knob still means seconds to -60 dB; past the stock range the extended-decay bend.
- Tone: VR5 itself.
- Accent: how far velocity moves the trigger between 4 and 11.5 V (D258; the service notes' text gives 14 V); the
  circuit then changes attack and sigh on its own.
- Punch: the attack shift's depth, as R166 (the resistor left when Q43 shorts R165).
- Punch Time: C38, the envelope's length.
- Sigh: the leakage, as R161 (and C39).
- Click: the input pulse width, 0.1 to 10 ms (a bend).
- Shape: the op-amp rails, 15 V down to about 5 V, so hard hits clip.
- Noise: kept as today, an addition, not the circuit.
- Drive and Trigger: as today. Level (VR4) is linear and the voice has gain, so it stays fixed.

Key tracking: the circuit is fixed at 49.5 Hz.
- Scaling C41 and C42 together by s moves the pitch by 1/s and keeps the bridged-T's Q, as an 808 tuning mod does; the attack and click stay their length, as Kick 3 and Punchbox track only the body.
- The settled pitch also moves a little with Decay (long decays go flat), so a table measured offline over VR6 sets s so the tail lands on the note.
- Glide and bend move the capacitors every sub-block.

## How to build it

Structure, all in double precision, one instance a voice, no heap:
- The trigger: an ideal voltage source with band-limited edges.
- The pulse shaper: a small tree, D53 at its root by Wright omega (ChowKick's arrangement).
- The resonator: one R-type tree holding the bridged-T, both op-amps as ideal (nullor) elements, the feedback buffer with VR6 and C43, R161 and the retrigger path, with a nonlinear root of Q43 (Ebers-Moll, two ports) and D52.
- The envelope generator: Q41 and Q42 as their own small nonlinear tree, or first as the paper's behavioural envelope.
- The output: a linear tree for tone, level and the high-pass, driven by the resonator's output, an op-amp's, so no loading.

The solver: Newton's method in the Kirchhoff domain at the root, as Olsen, Werner and Smith, two or three dimensions, warm-started; at most eight iterations. Q43 barely conducts on a quiet tail, so most samples settle in one.

Oversampling: none beyond the engine's 96 kHz at first; a test renders the same hit at 96 and 384 kHz and compares the band above 20 kHz.

Tests (`tests/V3KickTests.cpp`, in the separate UnitTest class the stack-frame rule asks for):
- Linear parts against the paper's analytic transfer functions: an exact match is expected.
- The whole hit against ngspice renders of the netlist at several Decay, Tone and accent settings, run offline once; only the numbers (pitch at set times, peaks, decay times) go into the tests, so CI needs no SPICE.
- Against the paper's figures: about 130 Hz in the attack, about 57 Hz settling to 49.5 Hz over 250 ms, no machine gun.
- Key tracking within a few cents from C1 to C3; Newton's iterations bounded; silence after the tail; no allocation.
- Against recordings by ear and by spectrum, the user's ears last.

## Cost

Today's Kick costs 0.33 % of a block a voice, about 130 clock cycles a sample.
- The R-type scattering: about 100 multiply-adds a sample.
- Newton: three exponentials and a 3 by 3 solve an iteration, one to three iterations: about 200 to 600 cycles.
- The pulse shaper, envelope and output trees: about 100 cycles.
- The scattering matrix after a knob or pitch move: one small inverse a sub-block, about 50 flops a sample spread out.

An estimate of 1.5 to 3 % a voice, up to about 5 % in the worst case, twice that with internal 2x oversampling; to be measured by the cost report, priced on its own as the SID 6581 is (`svf6581`).
With a 50 % budget a sound, a mono kick never notices; sixteen voices of it would.

## Phases

- References: the service notes read, the netlist written, ngspice renders; the thesis read.
- The linear resonator with feedback and the output, against transfer functions and SPICE.
- The pulse shaper and trigger, against Figure 3 and SPICE.
- The nonlinear root (Q43, D52) and the envelope, against Figures 10 and 11 and SPICE.
- Rails, accent, retrigger, pots, key tracking, the bends as knobs.
- The Model in the generator, cost, a preset (an 808 Circuit Succession), docs and DECISIONS_LOG.

## Decisions for the user

Library:
- chowdsp_wdf for the elements, adaptors, R-type and diodes, our own nonlinear root, recommended: BSD, header-only, proven on this very pulse shaper; one dependency (a submodule to download).
- All our own code: no dependency, full control, more work and more to test.
- RT-WDF: has the Newton root, but GPL, Armadillo, heap and virtual calls; it would change CoolSynth's licence.

Where it goes:
- A fourth Model, "808 Circuit", recommended: today's 808 stays cheap and presets keep their sound; the Model list grows, which shifts stored values as SVF's Mode did (D233).
- Replace today's 808: one 808, but Succession 808 and saved sounds change, and the cost rises for everyone using it.
- A generator of its own with its own twelve knobs (pulse width, rails, tolerance, tuning mode): the most room, a new slot type and its own panel.

How far to model:
- The full circuit, envelope generator included, recommended: what the thesis claims matches the circuit.
- The thesis's four subcircuits with the paper's behavioural envelope: less work, the attack less exact.
- The resonator alone (bridged-T, feedback, Q43) in WDF, the rest as today: cheapest, half the gain.

Key tracking:
- Scale C41 and C42 with the note, recommended: the hardware tuning mod; attack and click keep their length.
- Scale the whole circuit in time: the same character at every pitch, but attack and pulse shrink at high notes, like a sample played fast.
- None, a fixed 49.5 Hz with a Tune knob: the true 808, but no bass lines.

Reference material:
- ngspice plus the Fischer set for analysis only, recommended: open, scriptable on both platforms; the samples' licence is unclear, so they never ship.
- LTspice: friendlier to look at, closed.
- The paper's figures only: no downloads, the weakest check.

Downloads needing permission:
- chowdsp_wdf (a small header library, as a git submodule beside JUCE in `external/`).
- ngspice (an installer for Windows, and later the Mac).
- The thesis PDF (over 10 MB) to read in full.
- Fischer's sample set (a small archive from the Aminet mirrors).

## Sources

- Werner, Abel, Smith, DAFx 2014: https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
- Werner, thesis, 2016: https://purl.stanford.edu/jy057cz8322 and https://stacks.stanford.edu/file/druid:jy057cz8322/KurtJamesWernerDissertation-augmented.pdf and https://searchworks.stanford.edu/view/11891203
- Werner's publications: https://ccrma.stanford.edu/~kwerner
- Werner, Smith, Abel, DAFx 2015 (R-type adaptors): https://dafx.de/paper-archive/details/dFlSsTeUmyOhfdA7HIi7Aw
- Werner, Nangia, Smith, Abel, DAFx 2015 (multiple nonlinearities): https://dafx.de/paper-archive/details/4fsq4pZNdCbUzyzAL8xsQg and https://dafx.de/paper-archive/2015/DAFx-15_submission_54.pdf
- Olsen, Werner, Smith, DAFx 2016: https://dafx.de/paper-archive/details/py9HnDl6pMzyENpNpbK7KQ
- Werner, Bernardini, Smith, Sarti, TCAS-I 2018: https://ccrma.stanford.edu/papers/modeling-circuits-with-arbitrary-topologies-and-active-linear-multiports-using-wave-digital-f
- Bogason, Werner, DAFx 2018: https://dafx.de/paper-archive/details/xg03x9t32QawVRNtU4MwZQ
- Rest, Dunkel, Werner, Smith, RT-WDF, DAFx 2016: https://www.dafx.de/paper-archive/2016/dafxpapers/40-DAFx-16_paper_35-PN.pdf and https://github.com/RT-WDF/rt-wdf_lib
- Chowdhury, chowdsp_wdf: https://github.com/Chowdhury-DSP/chowdsp_wdf and https://arxiv.org/abs/2210.12554v1 and https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf/namespacechowdsp_1_1wdft.html
- ChowKick: https://github.com/Chowdhury-DSP/ChowKick
- Faust wdmodels: https://faustlibraries.grame.fr/libs/wdmodels
- Albertini, Bernardini, Sarti, DAFx 2020: https://dafx.de/paper-archive/details/1VN4BTM85kVIGKymyKzBXg
- de Soras, denormals: https://musicdsp.readthedocs.io/en/latest/_downloads/d81fc9af8a9fa63332b248772fbb4a54/denormal.pdf
- Roland TR-808 service notes: https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=5
- Wolfram's 808 kick model: https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/
- EveryCircuit bridged-T: https://everycircuit.com/circuit/5216749304676352/bridged-t-network-808-basic-drum-circuit
- ngspice: https://en.wikipedia.org/wiki/Ngspice
- LTspice: https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html and https://www.analog.com/en/license/spice-models.html
- Fischer's sample set: https://discmaster.textfiles.com/view/3307/Aminet%2037%20(2000)(Schatztruhe)%5B!%5D%5BJun%202000%5D.iso/Aminet/mods/smpl/tr-808.lha/TR808.TXT
- SampleRadar 808 samples: https://www.musicradar.com/news/sampleradar-378-free-808-drum-samples
- The earlier research: `docs/research/kick-generator.md`
