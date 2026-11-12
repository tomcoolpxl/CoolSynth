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
single source for UI labels, preset YAML keys and test expectations.

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

Three processor slots run in series on the stereo pair. Processors are stereo in and out; the
ladder and SVF process left and right with shared coefficients. Then the VCA applies ENV1 times
velocity sensitivity times the voice gain modulation destination, and the pan stage applies pan
spread plus modulation.

### 4.2 Sub-block rendering

A voice renders a block in sub-blocks of 32 samples at the 2x rate (16 host samples). Per
sub-block: evaluate local modulators once, collect the matrix into the per-slot offset arrays,
build `SlotParams` for each slot with linear smoothing of the base values toward their targets,
then run G1..G3, mixer, P1..P3, VCA, pan. Audio-rate modulation exists only where it is part of
a module (FM inside the FM generator, combine modes between generators). This is the V2
`renderNextBlock` structure with the per-sample parameter evaluation moved up to sub-block rate.

### 4.3 Local modulators

- ENV1, ENV2, ENV3: the V2 `AnalogEnvelope` extended with a per-stage curve (0 exponential to 1
  linear), velocity sensitivity, loop (attack-decay cycling while the gate is held), one-shot.
  ENV1 is hardwired to the VCA, ENV2 has a hardwired amount to P1 cutoff (parameter, not matrix),
  ENV3 is free.
- LFO1, LFO2: sine, triangle, saw up, saw down, square, sample-and-hold, smoothed random, 16-step
  shape; rate in Hz or a tempo division; start phase; key retrigger; fade-in time; one-shot.
- Sources without parameters: velocity, release velocity, key position (bipolar around C3),
  note random (new per note), aftertouch (channel or poly), MPE pressure and slide, per-voice
  drift (the V2 Vintage random walk, now a source with no fixed destination).

### 4.4 Note context

`noteOn` receives note number, velocity, glide start note, unison index and count, and the
chord/arp origin flag so generators can randomise phase per note where the type wants it.

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

Hardwired routes that stay as plain parameters so simple patches need no matrix entries:
ENV1 to VCA, ENV2 to P1 cutoff amount, velocity to VCA and to P1 cutoff, wheel to LFO1 depth,
bend to pitch with range, key tracking per generator and per processor.

## 6. Note layer

In order, per part: chord memory (Off or one of 12 chord types plus one learned chord; strum
time; applied to each incoming note), then the V2 arpeggiator unchanged, then play mode (poly,
mono, legato, unison with count 2 to 8 and detune cents and stereo spread), key priority, glide
(time, legato only, constant rate), then the allocator. The allocator is the V2 one (release-
first stealing with the 1.5 ms steal fade and pending note start), extended with a per-patch
polyphony cap and a per-part cap so two parts share the pool of 16 fairly (each part is
guaranteed its cap, default 8 and 8; a part may be set to 16 when the other is Off).

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

### 9.2 State and patch format

`patchFormatVersion` goes to 8; V2 state (7 and lower) is rejected, as the compatibility stance
requires. The patch XML stores every parameter by ID plus the chord-memory learned chord and the
step shapes. Presets are authored in YAML with the same IDs, grouped by slot:

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

## 12. UI consequences (parked)

The editor needs, at minimum to audition: a type menu per slot, eight generic knobs per slot
relabelled from the descriptor, an envelope and LFO panel, a matrix page as a table, a part
selector, and the effect chain as six type menus. The existing `SynthSection`, knob and fader
components are reused. The full redesign stays parked.

## 13. Migration from V2

| V2 | V3 |
| --- | --- |
| `SynthVoice` fixed path | `Voice` with slots; oscillator code becomes `VaGenerator` |
| `VoiceLadderFilter` | `LadderProcessor` |
| `AnalogEnvelope` | extended, used by ENV1..3 and GENV |
| global LFO in `SynthEngineV2` | `Lfo` class used by LFO1..2 and GLFO1..2 |
| poly-mod parameters | matrix routes; removed as parameters |
| Vintage amount | drift source; the fixed filter and envelope offsets are removed |
| Unison play mode | play mode with count and detune |
| `GlobalFxRack` fixed order | effect slots; each block becomes an effect type |
| `BlockRenderParametersV2` | `PartParameters` with slot arrays; built the same way per block |
| `patchFormatVersion = 7` | 8, older rejected |
| `FactoryPresets.cpp` bank | regenerated from the V3 YAML, old bank deleted |

Deleted after phase 1: the fixed voice path, poly-mod and Vintage parameters, the V2 preset
YAML and generator snippet path, and the tests that only exercised them.
