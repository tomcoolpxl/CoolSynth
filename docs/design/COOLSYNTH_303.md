# CoolSynth 303

A standalone bass line app built on a circuit-level TB-303, the sibling of CoolSynth 808 (D275).
This is the plan: what is decided, how the sound is built from the service notes, and in which order.
Nothing here is built yet. The interface is parked by the user until the engine is done.

## What is decided (D275)

- Its own app repository, `tomcoolpxl/CoolSynth303`, public under MIT, as CoolSynth 808 is.
  - The circuit goes into `coolsynth-engine` beside the 808's circuits, on the same solver (`CircuitWdf.h`).
  - No separate engine: the 303 is one more generator in the shared engine, played by one layer
    (`EngineShape::singleLayer()`, D270).
  - CoolSynth 808 is untouched.
- The name is CoolSynth 303.
  - "TB-303" never appears on the panel. The README says the app is based on the TB-303's circuits, that TB-303 is
    Roland's mark, and that Roland does not endorse it (as AudioRealism and D16 word it).
  - Roland's US mark 6044644 is the stylised "TB-303", its goods including plug-in software; its 2019 EU filing
    018016159 covers the 303's panel look. The panel's design has to keep clear of that look.
  - A trademark search before the first release, as for the 808.
- The stock 1982 circuit first, from the service notes.
  - The Devil Fish's changes come later, as trimmers and switches that start at the stock setting.
- The circuit replaces today's TB-303 generator (the Open303 rewrite, D242) in CoolSynth too, once it passes its
  checks.
  - CoolSynth's Acid preset is levelled again on it.
  - Saved projects that use the TB-303 will sound different: the user is asked before that ships.
- What "right" means: each stage within a few percent of an ngspice render of our own netlist, as the 808's are;
  then only the trimmers (TM3 cutoff, TM4 tune, TM5 width) and the parts known to vary are set to a real unit.
  - The real unit: Din Sync's reference recordings of their TB-303 (2010), 25 sets of 16 low C notes, saw and
    square, plain and accented, at documented knob positions. Kept local in `tools/references/tb303-dinsync`, never
    in git or shipped: no licence is stated. Only numbers measured from them go into the repository.
  - The published figures beside it: the service notes' own targets (a 2 ms ring at TP6, A at 110 Hz, the octave
    2:1), Robin Whittle's timings from the Devil Fish manual, Richie Burnett's filter response of a real unit, and
    Julian Schmidt's logic-analyser timing of the 303's CPU.
- The BA662 VCA as its transistors, from Open Music Labs' reverse-engineered schematic.
- The gate as Schmidt measured the original CPU: on for 3 of 6 clocks, a slide holding it high into the next note,
  accent and slide latched at the step's start, the lag behind the clock a fixed 2 ms (his mean).
- The interface is parked (the user, 2026-11-23). What the user wants when it resumes:
  - the original's layout and controls, drawn in CoolSynth 808's style;
  - programming that is completely new, since the original's is widely disliked;
  - modelled on the software 303s whose usability reviewers rate best.
  - The research for it is in `docs/research/tb-303-app.md`.

## The circuit, stage by stage

From the service notes, First Edition, February 1982, page 5, with the block diagram on page 3.
Part numbers are Roland's.

- Pitch: the CPU's note into the latch IC9 (4174), the 6-bit resistor ladder R74 to R90 (0.1 % matched) from
  5.333 V through IC10's buffers.
  - The slide: the ladder's output through IC12's switches and C35 (0.22 uF) into IC11b. An RC of 22 ms.
- The VCO: an exponential converter on the dual Q26 (2SC1583) with TM4 (tune), TM5 (width) and the posistor R100.
  - Its capacitor C33 (0.01 uF) charged and reset by Q24, Q25 and Q27, buffered by the FET Q28 (2SK30): a saw from
    about 12 V down to 5.5 V.
  - The square: the saw through the one-transistor shaper Q8 with C10, C11, R34, R35, R45 and R36; its width moves
    with pitch.
  - The WAVEFORM switch S1 chooses one.
- The VCF: a diode ladder of transistors wired as diodes (Q13 to Q17, Q22, Q23) with C18 (0.018 uF) at the bottom
  and C19, C24, C26 (0.033 uF) above, driven by the dual Q12, read by the dual Q21.
  - Its resonance: the feedback through Q20, the RESONANCE pot's first gang.
  - Its current: Q10 and Q11 as an exponential converter, biased by Q9 (page 8 explains why), with R63, R72, R73
    and TM3; CUTOFF FREQ and ENV MOD set it.
- The envelope generator: Q36, Q38 to Q41 and the FET Q39, its capacitor C62, the DECAY pot VR6 (1 MOhm).
  - IC12 switches the decay to its shortest on an accent.
- The accent: through D24 and R46 (47 kOhm) with the RESONANCE pot's second gang into C13 (1 uF), so repeated
  accents climb; ACCENT (VR7) sets how much.
- The VCA: IC15 (BA662A), its envelope from Q31 and Q32 with C42, the accent's share through D27, R120 and C36.
- The output: C38, then the mixer Q33 and Q34 to OUTPUT. The headphone amplifier (IC14) is left out.
- What is not modelled: the CPU (the engine's sequencer plays its role), the switch board, the power supply (its
  rails taken as ideal 12 V, 5.333 V and 6 V).

How it is solved, as the 808 is:
- Split where a low-impedance output drives a high-impedance input, each piece one `wdf::Circuit` with its devices
  at the root, solved by Newton's method.
- The ladder, its drive, its read-out and its feedback stay together in one circuit: its stages are unbuffered from
  each other, which is what makes the 303's filter.
- Device models as for the 808 where the parts are the same (2SC945, 2SA733, the 1S2473 as a 1N4148).
  - New: the 2SC1583 and 2SC2291 duals (no published model; their pinout is to be found first), the 2SK30A (the
    datasheet's ranks O and Y), the AN6562 (an LM358-like op-amp), the BA662 (transistor-level).
- Known doubts in the notes, each to settle against ngspice and the recordings: C29 fitted opposite to the drawing
  on real units; two resistors some say are misprinted near the ladder's supply.

## How it is checked

- An ngspice netlist per stage and one of the whole voice in the engine's `tools/tb303`, with a render script as
  `tools/tr808/render.py`.
- Each stage of the solver within a few percent of its render, in `V3Tb303Tests`.
- Against the service notes' targets: the 2 ms ring at TP6 with cutoff centred and resonance full, A at 110 Hz,
  one volt an octave.
- Against Burnett's measured response, Whittle's timings (the note's start 1 to 5 ms after the gate, its end over
  16 ms, the filter decay 200 ms to 2 s, 200 ms on an accent, the volume's own 3 to 4 s) and Schmidt's gate.
- Then the trimmers fitted to the Din Sync unit: cutoff, resonance, env mod, decay and accent across their sets.
- Aliasing and cost measured before the oversampling is chosen; the numbers go to the user as options.
  - The saw's reset is the one hard edge: today's 303 runs its core at 192 kHz.
  - Chosen (D290, D294): the voice at 96 kHz, the square's shaper at 768 kHz, the envelope near 48 kHz; about 73 % of
    one core.
- The determinism and click tests over every preset, as for every engine change.

## The order of the build

- The netlists: every stage read from the service notes into `tools/tb303/*.cir`, rendered with ngspice.
- The device models, with what the datasheets and Open Music Labs give.
- The circuit in the engine, stage by stage, each against its render: the VCO and shaper, the ladder, the
  envelope and accent, the VCA, the output.
- The aliasing and cost measured; the oversampling chosen with the user.
- The trimmers fitted to the recordings and the published figures.
- The circuit replaces the Open303 generator in CoolSynth; Acid levelled again; the user asked before a release.
- The 303's sequencer in the engine: pitch, octave, accent, slide, note, tie and rest per step, played with the
  measured gate. How it is programmed is designed with the user when the interface resumes.
- The app's shell in `tomcoolpxl/CoolSynth303`: a window, the audio device, the engine with one layer, MIDI in,
  MIDI clock in and out, as CoolSynth 808's.
- The interface, when the user resumes it.
- The Devil Fish's changes, as trimmers.

## Open, for later

- How the 303 and 808 apps play together: MIDI clock between them, as hardware does, works now.
  - Windows 11's MIDI Services give every app loopback ports; Ableton Link is GPL, so shipping it would put the
    app's binaries under the GPL.
- A library of acid lines: products ship their own or "in the style of" patterns, not named transcriptions.
- Which Devil Fish changes, and how they are shown.
