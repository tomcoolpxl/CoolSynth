# REQUIREMENTS_V3.md

# CoolSynth V3 engine requirements, derived from the sound bank

Status: draft, 2026-11-12. This document exists because the V3 factory bank
(`SOUND_BANK_V3.md`) asks for sounds the V2 engine cannot make. It lists what the bank needs,
how often, and proposes the engine shape that covers it: a chain of modules per voice, with local
(per-voice) and global modulators, pluggable module types, and a rack of global effect modules.
Section 7 holds the open decision. No code follows from this document until that decision is
taken and `DESIGN_V3.md` is written.

Working assumptions, as agreed:

- Sound quality decides; the V2 design documents are not binding.
- No backward compatibility: V3 may change the parameter contract, state format and preset
  format freely.
- The engine may be extended, redesigned or rewritten; wavetables, samples, physical models and
  new modulation are all in scope.
- UI work stays parked; this document notes UI consequences only where the engine shape forces
  them.

## 1. What the V2 engine is today

One fixed voice: two PolyBLEP oscillators (pulse, triangle, saw, sine; octave, fine, level, pulse
width, hard sync A-from-B, osc B low-frequency mode), one noise source, a mixer, a transistor
ladder low-pass with resonance, envelope amount and key tracking, two exponential ADSRs, one
global LFO (saw, triangle, square, sine; Hz only) to pitch, pulse width or cutoff with wheel
depth, fixed poly-mod routes (osc B and filter envelope to pitch, pulse width and cutoff), glide,
poly / mono / unison play modes, key priority, bend range, Vintage drift, pan spread, velocity to
amp and filter. Note layer: an arpeggiator with Euclid, chance, ratchet, accent, swing and latch.
Global rack in fixed order: drive, phaser, chorus, delay, plate reverb, compressor, master gain
with a soft clipper. Voices render at 2x and are decimated with a polyphase half-band.

What is worth keeping regardless of direction: the ladder filter, the envelope model (to be
extended), the PolyBLEP oscillator core (becomes the VA engine), the plate reverb, chorus, delay
and half-band code, the arpeggiator, the MIDI, controller-profile, state and preset
infrastructure, and the test scaffolding.

## 2. What the bank demands

Counts are the number of presets in `SOUND_BANK_V3.md` that list the tag in their Needs column.
"Rooms" is how many rooms are blocked or weakened without it. Priority is by audible reach: how
many presets it unblocks, and whether a whole room signature depends on it.

| Priority | Tag | Presets | Rooms | Why it ranks here |
| --- | --- | --- | --- | --- |
| 1 | Modulation system: MATRIX, LFO-SYNC, LFO-V, S&H, STEP, GENV, ENV3 | 13 | 7 | Nearly every placeholder needs a tempo-synced, per-voice or any-to-any modulation route. Without it no "modern" motion exists. |
| 1 | SWARM / UNISON-N (unison with count, detune, spread) | 6 | 4 | Main Stage, Outrun and Engine Room signatures depend on width. |
| 1 | FM2 / FM4 | 5 | 5 | Bells, DX keys, FM bass, growl, Init FM. The single most-cited new generator. |
| 1 | WT (wavetable with morph) | 5 | 4 | Observatory, Arcade, Main Stage. The defining modern generator. |
| 1 | SVF (HP / BP / notch) | 4 | 4 | High-passed supersaws, breath and wind noises, bass-free pads. |
| 1 | LOFI block (wow, flutter, hiss, crackle, bandwidth, crush) | 4 | 2 | The whole Bedroom signature. |
| 1 | DELAY-SYNC (note values, ping-pong) | 4 | 4 | Any tempo-locked room. Cheap on top of the existing delay. |
| 2 | CHORDMEM | 3 | 3 | Chip chords, future chords, chord trigger. Lives in the note layer, not the voice. |
| 2 | CRUSH | 3 | 3 | Arcade, Basement, Main Stage. Trivial DSP, large effect. |
| 2 | KS (Karplus-Strong) | 2 | 1 | Greenhouse strings. |
| 2 | MODAL | 2 | 2 | Greenhouse mallets, Bedroom mallet. |
| 2 | FORMANT | 2 | 2 | Vowel pads and growls. |
| 2 | FOLD / VDRIVE (per-voice nonlinearity) | 2 | 2 | Basement and Main Stage bass. Global drive smears chords; per-voice drive does not. |
| 2 | LAYER (two parts) | 1 explicit, product requirement | all | Wanted as a feature in its own right ("real patches on patches"); the bank uses it for the Supersaw sub layer and will use it more once it exists. |
| 2 | PUMP, SHIMMER, PRE-DELAY | 1 each | 2 | Main Stage pumping, Observatory haze and depth. |
| 2 | SUB (dedicated sub) | 2 | 2 | Solved for free by a third generator slot. |
| 3 | GRAIN + SAMPLE | 1 | 1 | Observatory cloud. Biggest implementation cost per preset; last. |
| 3 | RING, COMB, CHIP (LFSR noise) | 1 each | 1 each | Cheap individually; schedule when their rooms are authored. |
| 3 | AT / MPE, MACRO, SEQ, EQ, FLANGER | 0 explicit | - | Not demanded by a preset; MACRO and AT come almost free with a mod matrix, SEQ and EQ are later. |

Reading the table top-down gives the implementation order in section 6.

## 3. Module catalogue requirements

Every module is a type that can be placed in a slot. A slot holds exactly one module type at a
time plus the parameters that type exposes. Changing the type of a slot must not touch any other
slot (the Roland SH-4d rule: filter, amp and LFO settings survive an oscillator model change).

### 3.1 Generators (per voice)

| Type | Must have | Notes |
| --- | --- | --- |
| VA | pulse / triangle / saw / sine, pulse width, hard sync from the previous slot, unison 1 to 7 with detune and stereo spread, sub mix one octave down | The V2 oscillator plus SuperWave-style unison. Unison inside the generator keeps CPU proportional to what is used. |
| Noise | white, pink, band-passed with pitch tracking, LFSR periodic (chip) with clock divider | Covers NOISE, CHIP and the breath sounds. |
| Wavetable | table bank (factory tables plus user import later), position with morph (crossfade), band-limited playback (mip-mapped or per-table oversampling), optional 4-bit or 32-sample "chip" tables | Position is the primary modulation destination. |
| FM | 2 operators minimum (carrier, modulator with ratio, fine, index, feedback); 4-operator with 4 to 6 fixed algorithms as a second step | Phase modulation, sine operators with optional waveshape. Index must be an envelope destination with its own curve. |
| Karplus-Strong | excitation (noise burst or click), pick position, damping, string decay, release damping | Loop filter must track pitch across the keyboard without going sharp. |
| Modal | 8 to 24 partials with a material morph (wood, metal, glass), mallet hardness, decay, strike position | Plaits modal / Rings-style. |
| Sample | one-shot or loop, start, loop points, pitch tracking on or off, factory samples embedded in the binary; user import later | Needed for Grain. |
| Grain | source from Sample or from another generator, density, size, pitch spray, position scan, freeze | Last on the list. |

Each generator slot also has: level, pan, octave, semitone, fine, key tracking on or off, and a
combine mode with the previous slot: off, sync, ring, FM (phase), AM. Three generator slots per
voice. Two is the V2 count and blocks the three-oscillator bass and the sub-under-supersaw cases;
four adds cost for no preset in the bank.

### 3.2 Processors (per voice)

| Type | Must have |
| --- | --- |
| Ladder LP4 | the V2 ladder: cutoff, resonance, drive into the saturator exposed, envelope amount, key tracking |
| SVF | LP2 / HP / BP / notch, cutoff, resonance, drive |
| Comb | tuned delay with feedback, key tracked, polarity |
| Formant | five-vowel morph (a e i o u), size, resonance |
| Wavefolder | fold amount, symmetry, pre-gain; envelope-controllable |
| Crusher | bit depth 1 to 16 continuous, sample-rate reduction to 1 kHz, dry/wet |
| Drive | tube or tanh with tone; per voice |

Three processor slots in series. A parallel split (two filters in parallel then mixed, Pigments
style) is not required by any preset and is left out of the first design.

### 3.3 Modulators

Local (per voice, retriggered per note):

- Three envelopes: ADSR with per-stage curve (exponential to linear), velocity sensitivity, loop
  mode (attack-decay cycling, MiniFreak cycling envelope), one-shot. Envelope 1 is hardwired to
  the amp, envelope 2 defaults to the first processor cutoff, envelope 3 is free.
- Two LFOs: sine, triangle, saw up, saw down, square, sample-and-hold, smoothed random, 16-step
  user shape; rate in Hz or tempo division; phase; key retrigger on or off; fade-in; one-shot.
- Sources without parameters: velocity, key position, note random (new value per note),
  aftertouch, MPE pressure and slide, release velocity.

Global (one instance, shared across voices):

- Two LFOs with the same shapes, tempo-synced, phase shared by all voices (the V2 global LFO
  generalised).
- One global envelope: triggered by the first note of a phrase (no voices sounding) or by every
  note, with stage times up to 16 bars in tempo units. This is the riser and the "pad that swells
  once per chord" tool.
- One 16-step modulation sequencer at a tempo division, with slide per step.
- Four macros, mod wheel, pitch bend, expression pedal, sustain.
- Chaos / drift: a slow random walk per voice (replaces Vintage) and a global one.

Modulation matrix: 24 slots, each source to destination with a bipolar amount, an optional "via"
source that scales the amount, and a curve. Any parameter of any slot is a destination, including
generator level (for gating), pan, and other modulators' rates and amounts. Hardwired defaults
(envelope 2 to cutoff, wheel to LFO depth, velocity to amp) stay as plain parameters so a simple
patch needs no matrix entries.

### 3.4 Note layer (before voice allocation)

- Chord memory: one key plays a stored chord (12 chord types plus a learned chord), with strum
  time; feeds the arpeggiator.
- Arpeggiator: the V2 arpeggiator unchanged.
- Play modes: poly, mono, legato, unison with count 2 to 8 and detune (whole-voice unison stays
  for the whole-patch stack sound; generator unison covers supersaws).
- Glide: time, legato-only, constant-rate option.
- Step sequencer with parameter locks: later; the step modulator covers the first bank.

### 3.5 Parts (layering)

Two parts, A and B. Each part is a complete voice architecture (generators, processors,
modulators, note layer) with its own patch. Part mix: level, pan, transpose, fine detune, key
range, velocity range, and a mode: layer, split, or part B only. Voices come from one shared pool
of 16 with a per-part cap. Effects: three insert slots per part and the shared master chain.
Reference behaviour: Prophet-10 stack and split, Jupiter-8 Whole / Dual / Split with balance.

### 3.6 Global effects

A master chain of six slots in user order (per-part inserts use the same types):

| Type | Must have |
| --- | --- |
| Drive | the V2 drive (tube, oversampled) |
| EQ | tilt plus low and high shelf |
| Chorus | the V2 chorus with modes I, II and I+II |
| Flanger | rate, depth, feedback, polarity |
| Phaser | the V2 phaser |
| Delay | the V2 delay plus note-value times, ping-pong, tape mode (the existing dark feedback path) |
| Plate | the V2 plate plus pre-delay to 100 ms, input high-pass, modulation depth |
| Shimmer | plate with +12 (and +7) pitch-shifted feedback, mix of shifted and plain |
| Lo-fi | wow, flutter, hiss, crackle, bandwidth low-pass, bit depth, sample-rate; one block |
| Pump | tempo-synced ducker with attack, release, depth, division |
| Compressor | the V2 compressor |
| Limiter | the V2 soft clipper as a selectable block |

## 4. Engine shape: a chain of modules

What was asked for: modules that are noise or sound generating or sound changing, chained; global
and local envelopes. Three shapes were considered.

### 4.1 Slot chain (proposed)

Per voice, a fixed order of typed slots:

```
[G1] [G2] [G3]  ->  mixer (with combine modes between neighbours)
     -> [P1] -> [P2] -> [P3]  ->  VCA (env 1) -> pan  -> voice sum (2x) -> decimator
local modulators:  ENV1 ENV2 ENV3  LFO1 LFO2  velocity key random aftertouch MPE
global modulators: GLFO1 GLFO2  GENV  STEP  macros wheel bend  chaos
mod matrix:        24 x (source, destination, amount, via, curve)
note layer:        chord memory -> arpeggiator -> play mode / glide -> allocator
parts:             A and B, each the above, mixed, then
master chain:      [F1] .. [F6] -> master gain -> clipper
```

Every slot accepts any module of its kind (generator, processor, effect). Signal order is fixed;
only the types and their parameters change. Feedback exists only inside modules (FM feedback,
comb, delay), never between slots. This is the MiniFreak, SH-4d, Plaits and OP-1 layout.

Why it fits:

- It covers every preset in the bank, including all 29 placeholders.
- CPU is predictable: cost is the sum of the chosen module types; each type declares a cost
  class, and the voice count can be capped per patch from that.
- The UI stays one panel per slot with a type menu at the top; the matrix is one page; parts are
  two tabs. No cables.
- Presets stay authorable in a flat YAML: slot type plus its parameters.
- The V2 code maps onto it: the oscillator becomes the VA generator, the ladder the first
  processor type, the rack blocks become effect types.

Parameter model: each slot exposes a type parameter and a fixed set of generic parameters
(P1..P8) whose names, ranges and defaults come from a descriptor table per module type, so the
parameter tree stays static and the host sees stable IDs while the display changes with the type.
Patch state is versioned; V3 rejects V2 state.

Rendering model: a module is an object with `prepare`, `reset` and `process` over a span of
samples plus a modulation context; voices render in sub-blocks of 32 samples at 2x host rate,
modulators are evaluated once per sub-block and interpolated; nonlinear processors already run at
2x on the voice bus. The decimator, latency reporting and the test harness carry over.

### 4.2 Free-form graph

Any module output to any module input, with cables and loops (a modular). More freedom than any
preset in the bank uses. Costs: a patching UI, unit delays and stability rules on feedback paths,
unpredictable CPU, slower preset authoring and a harder time keeping rooms distinct. Not
proposed. The slot chain can grow a "send" or a second processor lane later without becoming a
graph.

### 4.3 Extend V2 in place

Keep the fixed Prophet-style voice and bolt features on: unison count, sub, filter modes, a
second LFO, a few matrix routes, FM and wavetable as extra oscillator waveforms. Fastest for
Studio 1982 and Engine Room, which are already buildable. Each new generator becomes a special
case in `SynthVoice`, the modulation stays a fixed list, and Main Stage, Greenhouse, Arcade and
Bedroom placeholders stay placeholders for a long time. Not proposed.

## 5. Non-goals for the first V3 cut

- User wavetable and sample import (factory content only first).
- Parallel filter routing, more than three generator or processor slots, more than two parts.
- Step sequencer with parameter locks (the step modulator comes first).
- MPE beyond pressure and slide as matrix sources.
- Any UI redesign; the editor grows slot panels only as far as needed to audition.

## 6. Implementation order (for `IMPLEMENTATION_PLAN_V3.md`)

1. Core: slot chain, module interface, parameter descriptor model, VA generator, ladder
   processor, three envelopes with curves, VCA, pan, parts-free single engine, V2 parity tests
   (alias, steal fade, latency, levels) green.
2. Modulation: local and global LFOs with sync, S&H and step shapes, global envelope, step
   modulator, matrix, chaos; delay sync and ping-pong. Ship Workshop, Studio 1982, Engine Room,
   Clock Tower.
3. Generators: noise colours and LFSR, FM2 then FM4, wavetable with a factory table set. Ship
   Arcade, Greenhouse bells, Outrun.
4. Processors: SVF, crusher, wavefolder, per-voice drive, formant, comb, ring combine. Ship
   Basement, Main Stage (minus layering).
5. Effects: lo-fi block, pre-delay and shimmer, pump, EQ, flanger. Ship Bedroom, Observatory.
6. Physical: Karplus-Strong, modal. Ship the rest of Greenhouse.
7. Note layer and parts: chord memory, two parts with inserts. Ship the layered presets.
8. Sample and grain. Ship Granular Sky; remaining placeholders closed.

Each phase ends with the normaliser run, the test suite green, and the rooms it names auditioned.

## 7. Decision

Decided 2026-11-12: V3 is built as the slot chain of section 4.1. The free-form graph and
extending V2 in place were considered and rejected for the reasons in sections 4.2 and 4.3.
`DESIGN_V3.md` and `IMPLEMENTATION_PLAN_V3.md` follow from this.

## 8. Sources consulted for the module catalogue

- Mutable Instruments Plaits manual (engine catalogue): https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/
- Arturia MiniFreak review and specification (pluggable engines, cycling envelope, matrix): https://www.soundonsound.com/reviews/arturia-minifreak and https://www.arturia.com/products/hardware-synths/minifreak/details
- Roland SH-4d oscillator models (fixed voice, swappable model): https://static.roland.com/manuals/sh-4d_v200/en-US/8648833186192395.html
- Teenage Engineering OP-1 / OP-1 field engines: https://www.synthtopia.com/content/2023/02/10/teenage-engineering-op-1-op-1-field-synth-engines-compared/
- Korg NTS-1 mk2 (serial effects, multiple filter types, three LFOs): https://www.korg.com/uk/products/synthesizers/nts_1_mk2/
- Dirtywave M8 (Braids-derived Macrosynth, FM, sampler): https://www.soundonsound.com/reviews/dirtywave-m8-model02
- Serum 2 oscillator types: https://www.xferrecords.com/products/serum-2
- Vital preset categories: https://forum.vital.audio/t/categories-for-arps-brass-etc/6362
