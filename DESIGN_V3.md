# DESIGN_V3.md

# CoolSynth V3 engine design: the slot chain

Status: 2026-11-12, first version. Follows the decision in `REQUIREMENTS_V3.md` section 7. The
bank that drives it is `SOUND_BANK_V3.md`. The build order is `IMPLEMENTATION_PLAN_V3.md`.

## 1. Purpose

V3 replaces the fixed V2 voice (two oscillators, one ladder, two envelopes, one LFO, fixed
poly-mod routes) with a voice made of typed slots that accept pluggable modules, a modulation
system with local and global sources and a matrix, a note layer with chord memory, two parts,
and a master effect chain of slots. Everything that is DSP and already sounds right in V2 is
kept as a module type; everything that is topology is rewritten.

Design rules carried over: sound quality decides, no backward compatibility, no allocation or
locks on the audio thread, UI work stays parked except what is needed to audition.

## 2. Overview

```
note layer (per part):   MIDI -> chord memory -> arpeggiator -> play mode / glide -> allocator
voice (per part, x N):   [G1] [G2] [G3] -> mixer -> [P1] -> [P2] -> [P3] -> VCA -> pan
                          local mods: ENV1 ENV2 ENV3  LFO1 LFO2  velocity key random AT MPE drift
global (per part):        GLFO1 GLFO2  GENV  STEP  macros wheel bend expression  chaos
matrix (per part):        24 x { source, destination, amount, via, curve }
part mix:                 level pan transpose detune key range velocity range mode
part inserts:             [I1] [I2] [I3]
master:                   part A + part B -> [F1] .. [F6] -> master gain -> clipper
```

Voices render at 2x the host rate and are summed per part on an oversampled bus, decimated once
per part with the V2 half-band, then the inserts and master chain run at host rate. Nonlinear
effect modules that need it oversample internally, as the V2 drive does.

## 3. Modules

### 3.1 Kinds and types

| Kind | Slot | Types in the first cut | Later |
| --- | --- | --- | --- |
| Generator | G1..G3 | Off, VA, Noise, Wavetable, FM | Karplus, Modal, Sample, Grain |
| Processor | P1..P3 | Off, Ladder, SVF, Crusher, Folder, Drive | Comb, Formant |
| Effect | I1..I3, F1..F6 | Off, Drive, Chorus, Phaser, Delay, Plate, Compressor, Limiter | EQ, Flanger, Shimmer, Lo-fi, Pump |

A slot holds one type at a time. Changing the type never changes any other slot. "Off" costs
nothing and passes signal through (processors and effects) or produces silence (generators).

Shipped (phase 3.1, 2026-11-13): `NoiseGenerator` in `src/synth/v3/NoiseGenerator.h`, generator
type 2. Kinds White, Pink (seven shelves designed at the running rate), Band (state-variable
band-pass on the note with level compensation) and Chip (NES 15-bit register, Long or Short,
clocked at 93 times the note, Clock in semitones, steps band-limited with the VA's BLEP, which
moved to `dsp::blepResidual`). Tone, Width, pan, ring and AM; output two samples late like the
VA; every kind about 3 dB under a saw (decisions D25, D26).

Shipped (phase 3.2, 2026-11-13): `FmGenerator` ("FM2") in `src/synth/v3/FmGenerator.h`, generator
type 3. A sine modulator at Ratio (0.25 to 16) with Fine in cents phase-modulates a sine carrier
at Carrier times the note; Index 0 to 10 radians, Feedback through the average of the last two
modulator outputs, Mix from carrier to modulator. The index is limited where the first
sideband that would fold into the audible band falls under 1e-4 (`indexLimit()`); index,
feedback and ratios ramp across each sub-block; phases in double precision restart at a fresh
note. Hands on its signal and the carrier's wrap track; combine FM, ring and AM (decisions D30,
D31).

Shipped (phase 3.3, 2026-11-13): `Fm4Generator` ("FM4") in `src/synth/v3/Fm4Generator.h`,
generator type 4: four sine operators, operator 1 at the note, ratios and levels for 2 to 4,
feedback on 4, six algorithms (stack, two into one, stack plus one, two pairs, one into three,
organ). A Level is an index where the operator modulates and an output level where it sounds;
carriers are normalised by their total level; every modulation uses FM2's alias limit
(decision D33).

Shipped (phase 3.4, 2026-11-13): `WavetableGenerator` in `src/synth/v3/WavetableGenerator.h`,
generator type 5, playing `WavetableBank` (`src/synth/v3/Wavetables.h/.cpp`): four factory
tables (Classic, Digital, Vocal, Chip) of 16 frames of 2048 samples, built from their spectra at
11 octave band limits. Position crossfades frames and ramps per sample; each copy reads the
band limit its frequency needs (harmonics under 0.75 fs) with a Hermite read; Unison 1 to 5,
Detune, Spread, Phase Rand; combine FM, ring and AM; no sync input (decisions D34 to D36).

Shipped (phase 4.1, 2026-11-13): `SvfProcessor` in `src/synth/v3/SvfProcessor.h`, processor
type 2: a trapezoidal state-variable filter with LP, HP, BP (peak gain 1) and notch, resonance
Q 0.5 to 40, a soft limit on the band state, drive as an input stage, key tracking, cutoff and
resonance ramped per sample (decision D38).

Shipped (phase 6.1, 2026-11-13): `StringGenerator` ("String") in
`src/synth/v3/StringGenerator.h`, generator type 6: Karplus-Strong after Jaffe and Smith. A
delay line of one period less the loop low-pass's phase delay, Hermite-read; Decay as the 60 dB
time, Damping, Pick as a cutoff in harmonics, Pick Position as an output comb, Release Damp on
key-up, noise or click excitation. Slots gained the optional module hooks `noteOff()` and
`strike()` (decisions D58, D59).

Shipped (phase 4.2, 2026-11-13): `CrusherProcessor` and `FolderProcessor` in
`src/synth/v3/ShaperProcessors.h`, processor types 3 and 4. Crusher: sample-and-hold at Rate,
rounding to 2^bits levels (bits continuous), Mix. Folder: a sine folder with first-order
antiderivative anti-aliasing, Drive, Symmetry with a DC blocker, Mix. The matrix gained a sixth
curve, Quantized (decisions D40 to D42).

Shipped (phase 4.3, 2026-11-13): `DriveProcessor` in `src/synth/v3/ShaperProcessors.h`,
processor type 5: Soft, Hard and Tube curves with first-order antiderivative anti-aliasing, the
ladder's headroom, peak normalisation by the larger peak, Tone, Mix. The ring and FM combine
modes were audited and left as they are (decisions D44, D45).

Shipped (phase 4.4, 2026-11-13): `FormantProcessor` and `CombProcessor` in
`src/synth/v3/ResonatorProcessors.h`, processor types 6 and 7. Formant: three parallel
band-passes on the a e i o u formants, Vowel morph, Size, Resonance with make-up, Mix. Comb: a
key-tracked feedback comb with Tune, damping, a soft limit, polarity, a Hermite read and Mix
(decisions D47, D48).

### 3.2 Module interface

Each type is a plain class with the same four operations; no virtual base is needed because a
slot stores a `std::variant` of the types of its kind and dispatches with `std::visit`.

```
prepare(double sampleRate, int maxSubBlock, SlotMemory& memory)   // off audio thread, may allocate from memory
reset()                                                           // realtime safe
noteOn(const NoteContext&)                                        // generators and processors: per-voice start
process(float* left, float* right, int n, const SlotParams& p)   // in place, realtime safe
```

`SlotParams` is the slot's eight generic parameter values plus the kind-specific fixed ones
(level, pan, pitch offset for generators; none for processors), already summed with the
modulation offsets for this sub-block and clamped to the descriptor ranges. Generators write,
processors and effects read and write in place. Modules keep no pointers to anything outside
themselves and their `SlotMemory`.

### 3.3 Descriptors

Every type has a static descriptor:

```
struct ParameterDescriptor { const char* id; const char* name; float min, max, def; Skew skew; Unit unit; bool modulatable; };
struct ModuleDescriptor    { ModuleType type; const char* name; CostClass cost; std::array<ParameterDescriptor, 8> params; int usedParams; size_t memoryBytes(double sampleRate); };
```

The descriptor tells the parameter layer how to display P1..P8 for that type, the voice how
much `SlotMemory` to reserve, and the allocator how expensive a voice is. Descriptors are the
single source for UI labels, bank file keys and test expectations.

### 3.4 Slot memory and type changes

At `prepare`, each slot reserves one arena sized to the largest `memoryBytes` of any type of
its kind at that sample rate (for processors that is the comb or Karplus delay line of
`sampleRate / 20` samples; for generators it is a grain buffer later). Modules take their
buffers from the arena in `prepare`, so a type change needs no allocation. Type changes are
requested from the message thread as an atomic "pending type" per slot and applied by the audio
thread at a sub-block boundary: the slot output crossfades over 5 ms from the old module to the
newly constructed one, which prevents clicks when the user browses types with a note held.
Wavetable and sample banks are immutable objects owned by the processor and handed to modules as
a shared pointer swapped atomically; a module holds its copy for the duration of a block.

Shipped (phase 0.1, 2026-11-12): `Slot::prepare` is the only place that allocates; a module's
own `prepare` runs on the audio thread after a type change and may only take spans from its
arena. Each slot owns two instances with their own arenas, so the fading module keeps its
buffers while the new one is prepared. A type request that arrives during a fade is held until
the fade ends. The slot clamps every module's parameters to that module's own descriptor
ranges, so a fading module never sees values meant for the incoming type. Measured: a type
change between two constant generators steps by exactly one fade increment (1/240 at 48 kHz).

### 3.5 Cost classes and the voice cap

Each type declares a cost class (0 Off, 1 VA, 2 Noise, 3 Wavetable, 4 FM, 6 Karplus and
Modal, 8 Grain; processors 0 to 3). The sum over a patch is its voice cost. The engine owns 16
voices; a patch may cap its polyphony lower (parameter), and the allocator never starts more
voices than the cap. The budget target is one full patch of cost 12 with 16 voices under 25 % of
one core at 48 kHz in Release on the development machine; the measurement is a test that prints
the figure, not a hard assertion.

## 4. The voice

### 4.1 Signal

Three generator slots write into the voice's stereo sub-block buffer through the mixer: each
generator has level, pan, octave, semitone, fine, key tracking on or off, and a combine mode with
the previous slot (Off, Sync, Ring, FM, AM). Combine reads the previous generator's output of
the same sub-block, so G1 is a plain source, G2 may be driven by G1, G3 by G2. Sync and FM need
the previous generator's phase or output at sample rate, so generators expose their last
sub-block output and, for VA and Wavetable, a phase-wrap flag per sample. The mixer sums at
fixed voice headroom (the V2 constant 0.5) so levels do not pump with polyphony.

Shipped (phase 1.1, rebuilt in 1.3, 2026-11-13): `VaGenerator`, written new rather than
ported. Parameters: shape (continuous morph sine, triangle, saw, pulse, all four aligned to
start at zero rising so a morph never cancels the fundamental), pulse width, unison 1..7,
detune 0..200 cents (squared skew), spread, mix (centre versus sides), phase rand, sub.
Band-limiting is event based: every crossing of a break point (saw jump, pulse edges, triangle
corners) is located on the rendered phase trajectory and corrected with the 4-point BLEP or
BLAMP from the integrated cubic B-spline; the output runs two samples late so the correction can
reach back. The crossing test uses the same arithmetic as the phase update, so a point hit
exactly is never counted twice. Sync resets every copy at the exact time from the previous
generator's wrap track and corrects the step and slope change on both sides; FM finds crossings
on the modulated trajectory. Unison detune follows a JP-8000-like curve (positions u^1.5, upper
side 2 % narrower), the centre copy or pair against the sides by the mix, power-normalised;
start phases are random per note by Phase Rand. The voice applies level; the generator renders
at unit level and hands on its mono signal and the wrap track of an undetuned reference phase.
Measured after 2x rendering and decimation (inharmonic energy 20 Hz to 20 kHz, whole-period
frames): saw -88.8 dB at 439 Hz, -79.4 dB at 3.5 kHz, -72.9 dB at 7 kHz, each 26 to 27 dB
below the 2-point polyBLEP; triangle -114 dB and pulse -87.5 dB at 3.5 kHz; hard-synced saw
-87.4 dB; tuning within 1 cent; a 7-copy stack +0.3 dB against one copy.

Three processor slots run in series on the stereo pair. Processors are stereo in and out; the
ladder and SVF process left and right with shared coefficients. Then the VCA applies ENV1 times
velocity sensitivity times the voice gain modulation destination, and the pan stage applies pan
spread plus modulation.

Shipped (phase 1.2, rebuilt in 1.3, 2026-11-13): `LadderProcessor`, written new. Every one of
the four stages integrates tanh(previous) - tanh(own) and the input pair sees
tanh(input - k output), as in the Moog ladder; the loop is solved without a unit delay by
linearising each tanh around an operating point and solving the linear ladder exactly, twice per
sample (around the last sample, then around the first answer), which keeps it well behaved near
Nyquist. Parameters: cutoff (capped at 24 kHz), resonance, drive (peak-normalised tanh stage
before the ladder, nearly clean at 0 dB), key tracking, mode (LP24, LP12, BP, HP12 taps, BP and
HP scaled with the feedback so every mode sings at about the same level) and bass (input gain
1 + bass k on the low-pass taps). Resonance maps to k = 4 sqrt(r / 0.9) up to the threshold at
0.9 and rises to 5 at the top; a 4.1 % per unit cutoff correction above the threshold keeps the
self-oscillation in tune, and a -110 dB noise floor lets it start from silence. Measured at
48 kHz: LP24 -27.8 and -49.1 dB one and two octaves above cutoff, LP12 -13.8 and -24.5 dB, HP12
0.1 dB an octave above and -38 dB below, BP 0.2 dB at the centre; self-oscillation starts in
28 ms, within 3.3 cents of the cutoff from 250 Hz to 4 kHz, rms 0.15; bass 1 restores 50 Hz to
0.2 dB at resonance 0.8; full-scale noise at full resonance, drive and bass peaks at 3.3 at most.

### 4.2 Sub-block rendering

A voice renders a block in sub-blocks of 32 samples at the 2x rate (16 host samples). Per
sub-block: evaluate local modulators once, collect the matrix into the per-slot offset arrays,
build `SlotParams` for each slot with linear smoothing of the base values toward their targets,
then run G1..G3, mixer, P1..P3, VCA, pan. Audio-rate modulation exists only where it is part of
a module (FM inside the FM generator, combine modes between generators). This is the V2
`renderNextBlock` structure with the per-sample parameter evaluation moved up to sub-block rate.

Shipped (phase 0.2, 2026-11-13): `Modulation.h` holds `ModSources` (the source vector indexed
by `ModSource`), `SlotOffsets` (per-slot accumulators: generic offsets in normalised units, pan
in -1..1, pitch in semitones), `SlotBase` (host-set values smoothed linearly toward their
targets by at most one step per sub-block, 10 ms by default, in normalised space so linear and
logarithmic parameters ramp alike; pan and pitch scale their step by their span of 2 and 96
semitones) and `buildSlotParams`, which sums base and offsets, clamps, maps generic values to
descriptor units with the descriptor's skew, and ignores offsets on parameters marked not
modulatable. Smoothing is per sub-block, not per sample; modules interpolate internally where
their coefficients need it.

### 4.3 Local modulators

- ENV1, ENV2, ENV3: a delay-attack-hold-decay-sustain-release envelope with a bipolar curve per stage, velocity sensitivity, loop (attack-decay cycling while the gate is held), one-shot.
  ENV1 is hardwired to the VCA, ENV2 has a hardwired amount to P1 cutoff (parameter, not matrix),
  ENV3 is free.
- LFO1, LFO2: sine, triangle, saw up, saw down, square, sample-and-hold, smoothed random, 16-step
  shape; rate in Hz or a tempo division; start phase; key retrigger; fade-in time; one-shot.
  Shipped (phase 2.1, 2026-11-13): `Lfo` in `src/synth/v3/Lfo.h`, the same class for LFO1..2
  and GLFO1..2. Bipolar output times a depth that a delay and a straight-line fade build up from
  the last restart; the delay and fade restart at every note, retrigger (or one-shot) also
  restarts the phase at the start phase and holds it through the delay. 16 tempo divisions from
  8 bars to 1/32 with dotted and triplet values; a synced LFO that does not restart follows the
  host's song position while the transport plays. "Steps" is a rising 16-level staircase. A
  voice restarts its LFOs on a new or retriggered note (legato keeps them) and advances them
  only while it sounds; the mod wheel scales LFO1 by `voice.wheelToLfo1`. Global LFOs run in
  the part all the time, one value per voice sub-block, and restart at the first note after
  silence. Tempo is the host's, else the arp's internal tempo. Decisions D1 to D5 in
  `DECISIONS_LOG.md`.
- Sources without parameters: velocity, release velocity, key position (bipolar around C3),
  note random (new per note), aftertouch (channel or poly), MPE pressure and slide, per-voice
  drift (the V2 Vintage random walk, now a source with no fixed destination).

Shipped (phase 1.2, rebuilt in 1.3, 2026-11-13): `Envelope`, written new: delay, attack,
hold, decay, sustain, release, each segment exactly its programmed time with the curve
(e^(c t) - 1) / (e^c - 1), curve -1..1 to c -8..8 (negative is the analogue RC feel). Velocity
sets the peak, so a retrigger at a new velocity moves without a jump (largest step 0.0005 in the
test); a retrigger shortens the attack by the distance already covered; key time tracking halves
the times per octave above C4; loop, one-shot, and 5 ms sustain smoothing.

### 4.4 Note context

`noteOn` receives note number, velocity, glide start note, unison index and count, and the
chord/arp origin flag so generators can randomise phase per note where the type wants it.

### 4.5 Shipped voice

Shipped (phase 1.3, 2026-11-13): `Voice` in `src/synth/v3/Voice.h/.cpp`. Generators render
into their own buffers at unit level; a generator with a combine mode gets the previous one's
link, so a silent modulator still drives it. The mixer ramps each level per sample. Processors
run in series; P1's first parameter takes the hardwired ENV2 and velocity amounts as normalised
offsets. ENV1 is per sample with a 0.5 ms attack and 2 ms release floor; ENV2 and ENV3 run per
sub-block. Pitch stays in semitones (note, constant-time glide, bend, drift) until each
generator turns it into a frequency; key tracking off sits at C4. The voice owns its handover: a
start on a sounding voice fades it over 1.5 ms and starts the waiting note from a clean slate, a
start on a silent voice snaps every smoothed value and applies a waiting type at once. The
measured handover step equals the steady-state step of the louder note.

Fixed 2026-11-13: every note now begins inside render(), after the block's patch has been read.
Starting it in start() ran the envelopes with the previous patch's times, so after a preset
change the first note on each of the 16 voices sounded like the old preset (a slow attack
made short notes nearly silent). Every generator's output runs `generatorLatencySamples` (2)
late, and at a fresh note the envelopes wait as long, so a zero-attack onset steps 0.074
instead of 0.202.

## 5. Global modulators and the matrix

Per part: GLFO1 and GLFO2 (same shapes as the local LFOs, tempo-synced, one phase shared by all
voices, the V2 global LFO generalised), GENV (an envelope with stage times in seconds or bars,
triggered by the first note after silence or by every note, parameter), STEP (16 steps, value
per step, slide per step, division, length), four macros, mod wheel, pitch bend, expression,
sustain state, and a global chaos source (slow random walk).

Matrix: 24 slots per part. Each slot: source (enumeration of local and global sources),
destination (a stable destination id from the descriptor table: any generic or fixed parameter
of any slot, VCA gain, pan, pitch, plus LFO rates, envelope times and other matrix amounts),
amount in -1..1, via (a second source that scales the amount, Off by default), curve (linear,
exponential, S-curve). Evaluation per sub-block: compute the source vector once (global sources
once per block per part, local ones per voice), then for each active slot add amount times
source times via into the destination's offset. Offsets are in the destination's normalised
range and the slot clamps after summing. Pitch destinations are in semitones.

Shipped (phase 2.2, 2026-11-13): `src/synth/v3/ModMatrix.h`. 24 slots of source, destination,
amount, via and curve (linear, unipolar, exponential, logarithmic, S-curve). Destinations: pitch,
VCA, pan, LFO1 and LFO2 rate, each generator's pitch, level, pan, combine amount and P1..P8,
each processor's P1..P8 (66 in all; new ones only at the end). An amount of 1 is the whole
normalised range, 48 semitones for pitch, four octaves for an LFO rate; the VCA gain is 1 plus
the offset held in 0..1 and ramps across the sub-block. Each voice evaluates the packed list of
active slots once per sub-block after its sources; LFO rate offsets apply from the next
sub-block. Bank files name destinations by key for the slot's type in that preset
(`mod = src=lfo1 dst=g1.pw amount=0.3 via=wheel curve=unipolar`). Envelope-time and
slot-amount destinations wait (decision D9).

Shipped (phase 2.3, 2026-11-13): the global envelope is an `Envelope` in the part, its gate the
played keys before the arpeggiator (first key of a phrase or every key; closed when keys, pedal
and latched arp notes are gone), synced times written as at 120 BPM. `StepModulator` and
`ChaosSource` in `src/synth/v3/GlobalModulators.h`: 16 values with per-step slide and a shared
glide, division, length, retrigger or song-position lock; a smoothed random walk at a rate. Four
macros, smoothed over 10 ms. All of them, with the global LFOs, reach the voices as one frame
per sub-block. The matrix gained ENV1..3 time destinations, read once per note (decisions D14
to D19).

Hardwired routes that stay as plain parameters so simple patches need no matrix entries:
ENV1 to VCA, ENV2 to P1 cutoff amount, velocity to VCA and to P1 cutoff, wheel to LFO1 depth,
bend to pitch with range, key tracking per generator and per processor.

## 6. Note layer

In order, per part: chord memory (Off or one of 12 chord types plus one learned chord; strum
time; applied to each incoming note), then the V2 arpeggiator unchanged, then play mode (poly,
mono, legato, unison with count 2 to 8 and detune cents and stereo spread), key priority, glide
(time, legato only, constant rate), then the allocator. The allocator is new: release-first stealing, with the stolen voice fading itself out over
1.5 ms and starting the waiting note (section 4.5), extended with a per-patch
polyphony cap and a per-part cap so two parts share the pool of 16 fairly (each part is
guaranteed its cap, default 8 and 8; a part may be set to 16 when the other is Off).

Shipped (phase 1.4a, 2026-11-13): `Part` holds 16 voices at twice the host rate with a fixed
headroom of 0.5 and one decimator. Poly allocation: a sounding key reuses its voice, then the
least recently used silent voice, the quietest released, the quietest pedal-held, the oldest
held; voices above the polyphony limit finish and rest. Mono and legato play from a note stack
with last, low or high priority and return to held keys; unison stacks 2 to 8 voices spread in
pitch and pan; glide may be limited to overlapping keys. All Sound Off silences at once,
decimator memory and effect tails included. The arpeggiator is reused with its merge rules.
Per-part caps and the shared pool arrive with parts in phase 7.2.

## 7. Parts

A part is one complete patch: three generator slots, three processor slots, local and global
modulators, matrix, note layer, three insert slots, and a part mix (level, pan, transpose in
semitones, fine detune, low and high key, low and high velocity). Part mode on the patch: A
only, Layer (both on every note), Split (each by key range), B only. Parts share the master
chain and the master gain. Each part has its own oversampled bus and decimator; the two
host-rate outputs are summed before F1.

## 8. Effects

Insert slots I1..I3 per part and master slots F1..F6 take the same effect types. The chain
order is the slot order; the user reorders by changing types. First-cut types are the V2 blocks
(drive, chorus, phaser, delay with added note-value times and ping-pong, plate with added
pre-delay and input high-pass, compressor, the soft clipper as Limiter). Later types: EQ (tilt
plus shelves), flanger, shimmer (plate with pitch-shifted feedback), lo-fi (wow, flutter,
hiss, crackle, bandwidth, bit depth, sample rate), pump (tempo-synced ducker). Effects receive
the transport (tempo, position, playing) for synced times, with the internal tempo as fallback
exactly as the arpeggiator does today.

Shipped (phase 2.4, 2026-11-13), on the V2 rack until the effect slots arrive: the delay has
Sync with 14 note values (1/32 to one bar, dotted and triplet) and Mode Tape or Ping-Pong. The
engine sets the synced time from the host tempo, else the arp's internal tempo; the line holds
4 s. Ping-pong sums the input to mono into the left line and cross-couples the lines per sample
through the tape feedback path. The delay starts at its settings after a reset or when switched
on (decisions D21 to D23).

Shipped (phase 5.1, 2026-11-13), also on the V2 rack: the plate has Pre-Delay (0 to 250 ms), Low
Cut (an input high-pass), Modulation (a trim on the tank's excursion) and Shimmer (the tank's
output, band-limited, an octave up through a two-head delay-line shifter, fed back into the tank;
0 is the plain plate) (decision D50).

Shipped (phase 5.2, 2026-11-13): `LoFi` in `src/synth/LoFi.h`, a global stage after the drive:
wow and flutter on a short delay line, a bandwidth low-pass, bit and rate crushing, hiss and
crackle that follow the signal, Mix with a matched dry delay (decision D52).

Shipped (phase 5.3, 2026-11-13): `Pump`, `MasterEq` and `Flanger` in `src/synth/ModernFx.h`. The
rack's chain is drive, lo-fi, phaser, flanger, chorus, delay, reverb, compressor, EQ, pump; the
newer stages travel in `ExtraFxParameters`. The engine gives the pump its beat phase: the host's
song position while it plays, otherwise its own count restarted at the first note after silence
(decisions D54 to D56).

## 9. Parameters, state, presets

### 9.1 Parameter IDs

All IDs are stable strings built from the slot path, never from the type:

```
pA.g1.type  pA.g1.p1 .. pA.g1.p8  pA.g1.level  pA.g1.pan  pA.g1.oct  pA.g1.semi  pA.g1.fine  pA.g1.key  pA.g1.combine
pA.p1.type  pA.p1.p1 .. pA.p1.p8
pA.env1.a .. pA.env1.loop         pA.lfo1.shape .. pA.lfo1.oneshot
pA.glfo1.*  pA.genv.*  pA.step.s1 .. s16 pA.step.div ...  pA.macro1 .. 4
pA.m1.src  pA.m1.dst  pA.m1.amt  pA.m1.via  pA.m1.curve   (24 slots)
pA.chord.*  pA.arp.*  pA.play.*  pA.glide.*  pA.poly.cap
pA.i1.type  pA.i1.p1 .. p8       pA.mix.level .. pA.mix.velHi
pB.*  (same)
part.mode   f1.type f1.p1 .. p8  .. f6.*   master.gain
```

Generic parameters are `AudioParameterFloat` in 0..1 normalised form; the descriptor supplies
the display name, range, skew and unit through a custom `getText` / `getValueForText`, so the
host automation name follows the current type (hosts that cache names show the generic name;
that is accepted). Roughly 700 parameters in total; the processor keeps the V2 pattern of a
flat table of atomic pointers read once per block into a plain struct.

Shipped (phase 0.3, 2026-11-13): `parameters/v3/SlotParameters` builds one group per slot
from a `SlotTypeTable` (which `Slot<Variant>::typeTable()` supplies): a type choice named after
the descriptors, eight `GenericSlotParameter`s (normalised floats whose `getName`, `getText` and
`getValueForText` read the descriptor of the currently selected type; unused ones show "P n" and
"-"), and for generators the fixed level, pan, octave (-2..2), semitone (-12..12), fine
(-50..50 cents), key tracking and combine (Off, Sync, Ring, FM, AM). The default of a generic
parameter is type 0's default; a type change does not reset values. `bindSlotPointers` resolves
the raw pointers by ID, `readSlotTargets` folds octave, semitone and fine into one pitch offset,
and `applySlotTargets` pushes a slot's targets into its `SlotBase`. Parameter version hint 8.

### 9.2 State and patch format

`patchFormatVersion` goes to 8; V2 state (7 and lower) is rejected, as the compatibility stance
requires. The patch XML stores every parameter by ID plus the chord-memory learned chord and the
step shapes. Presets are authored in YAML with the same IDs, grouped by slot:

Shipped (phase 1.4, 2026-11-13): processor state format 9 and patch format 8; older state is
rejected. The patch and state loader validates against every parameter the processor
registers (each known ID exactly once, unknown IDs ignored), not against a static list, so
part parameters built from slot paths round-trip like the fixed ones.

```
name: Reese
room: Main Stage
part: A
g1: { type: VA, wave: saw, oct: 16, fine: +6, level: 0.8 }
g2: { type: VA, wave: saw, oct: 16, fine: -6, level: 0.8 }
p1: { type: Ladder, cutoff: 800, res: 0.2, env: 0.1 }
env1: { a: 5, d: 200, s: 1.0, r: 120 }
play: { mode: mono, glide: 0 }
i1: { type: Drive, amount: 0.15 }
```

The generator script maps descriptor names (`cutoff`) to generic IDs (`pA.p1.p1`) using the
descriptor table exported by a small tool, so preset YAML never mentions P1..P8. Factory presets
are still compiled in as a table; the normaliser and the master-gain script carry over.

Shipped (phase 1.5, 2026-11-13), replacing the YAML and generator above: bank files in
`resources/presets/*.csbank` are embedded and parsed at startup. `g1..g3` and `p1..p3` name the
slot type and then its parameters by descriptor key; `env1..env3`, `voice`, `play` take part
parameters by name; `arp`, `drive`, `chorus`, `delay`, `reverb`, `phaser`, `compressor` take on or
off first; `master` sets the output. The parser only maps keys to parameter IDs and reports
unknown keys with file and line; applying a preset resets every parameter, loads a named slot
type's defaults, and has each parameter read its value from text, reporting anything it cannot
read or that lies out of range. Descriptor parameters may carry option names (`Sine|Triangle|
Saw|Pulse`, `LP24|LP12|BP|HP12`), which both the bank files and the generic editor use; a percent
value is a fraction unless written with a `%` sign.

## 10. Realtime rules

- No allocation, locks or blocking calls on the audio thread. Arenas are sized at `prepare`.
- Type changes, bank swaps and chord learning cross threads through atomics and are applied at
  sub-block boundaries.
- Every module is finite for any parameter combination: inputs are clamped by descriptor range,
  feedback paths have explicit limits, denormals are flushed at sub-block ends (the V2
  `snapToZero` pattern).
- Latency is the per-part decimator latency (identical for both parts) plus any lookahead an
  effect declares; the processor reports the sum as in V2.
- Tail: a part reports a tail while any voice sounds or any insert or master effect reports one;
  `reset` on transport stop clears what V2 clears today.

## 11. Tests

The V2 suites stay and are ported to the new parameter struct. New suites:

- Module tests per type: tuning (generators within 1 cent across the range), alias floor for VA
  and Wavetable (below -90 dB at A7 as in V2), FM ratio and index behaviour, Karplus decay,
  processor stability at extreme settings, crusher bit steps.
- Matrix tests: amount and via arithmetic, destination clamping, a route to every destination
  id with no NaN.
- Voice tests: combine modes, sub-block smoothing continuity, type change crossfade without a
  step above the steady-state step, cost accounting and voice cap.
- Note-layer and parts: chord memory through the arp, split and layer allocation, per-part caps.
- State: round trip of every parameter, rejection of version 7, preset coverage (every factory
  preset loads, every slot type appears in at least one preset).
- Performance: the cost-12 patch at 16 voices, printed per run.

## 12. UI consequences (designed last)

The real UI is designed after the engine, presets and arpeggiator are finished (plan phase 9),
as a condensed view of everything the engine can do. Until then the UI is throwaway: since phase
1.4 the processor shows JUCE's generic editor, in which every parameter is a slider whose name
and text come from the descriptor of its slot's current type, so parameter names, units and
groups must stay clean. Nothing UI-specific is built before phase 9 beyond the throwaway
audition editor: a factory preset menu grouped by room above the generic editor. The
processor lists the bank as host programs named "Room: Preset".

## 13. Migration from V2

| V2 | V3 |
| --- | --- |
| `SynthVoice` fixed path | `Voice` with slots, written new; `VaGenerator` written new with event-based 4-point BLEP |
| `VoiceLadderFilter` | `LadderProcessor`, written new with per-stage nonlinearity |
| `AnalogEnvelope` | `Envelope`, written new (DAHDSR, exact-time curves), used by ENV1..3 and GENV |
| global LFO in `SynthEngineV2` | `Lfo` class used by LFO1..2 and GLFO1..2 |
| poly-mod parameters | matrix routes; removed as parameters |
| Vintage amount | drift source; the fixed filter and envelope offsets are removed |
| Unison play mode | play mode with count and detune |
| `GlobalFxRack` fixed order | effect slots; each block becomes an effect type |
| `BlockRenderParametersV2` | `PartParameters` with slot arrays; built the same way per block |
| `patchFormatVersion = 7` | 8, older rejected |
| `FactoryPresets.cpp` bank | bank files in `resources/presets`, parsed at startup; old bank deleted |

Deleted after phase 1: the fixed voice path, poly-mod and Vintage parameters, the V2 preset
YAML and generator snippet path, and the tests that only exercised them.
