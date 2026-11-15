# Brief: drafting the four macros of each factory preset

CoolSynth (a JUCE synth, `E:\github\CoolSynth`) has four macro knobs per part (`pA.macro1`..`pA.macro4`,
range 0..1, default 0) on its Home screen's Macros page. **No factory preset routes them yet**: the
bank files under `resources/presets/*.csbank` have no line with `src=macro`. Your job: for every preset in
the bank files you are given, draft what each of the four macros does (as bank lines) and a name for it.
The user reviews the names and listens to the result; quality of sound matters more than speed.

## The four roles (fixed, from docs/interface-v3.md section 3)

1. **Tone**: brighter or darker, the timbre's colour (a filter cutoff, a wavetable position, a sample's
   brightness-like parameter, FM index for bright metal...).
2. **Character**: texture or grit (drive, resonance, wave shape or pulse width, FM amount, detune or
   unison spread, noise level, fold, crush).
3. **Time**: how the sound moves in time (envelope times: attack, decay, release, together or one
   envelope; glide is not reachable).
4. **Movement**: life and motion (vibrato, tremolo or filter wobble depth through `via=macro4` on a new
   line, or an LFO's rate).

Example of the naming style (the only one so far, from the spec): *Neon Brass: Blare, Thickness, Swell,
Shimmer*. One word each, at most 10 characters, plain English a player understands, specific to the
sound (not "Tone", "Character", "Time", "Movement" themselves unless nothing better fits).

## Hard rules

- **Only add new lines.** Never change or remove an existing line of a preset. With every macro at 0 the
  preset must sound exactly as now (bit for bit), so every new line must contribute nothing at macro 0:
  `src=macroN ...` lines (macro 0 adds nothing) or `src=<lfo/env> ... via=macroN` lines (scaled by the
  macro, 0 at 0).
- A part has at most **24** modulation slots: count the preset's existing `mod =` lines for that part and
  stay within 24 in total.
- At macro 1 (full) the sound must stay musical and must not get much louder: no VCA boost, at most
  about +3 dB from any route (a cutoff opening is fine; a drive that doubles the level is not). Nothing
  that can run away (self-oscillation at full resonance on a bass is fine only if the preset already
  lives there).
- One macro may have one to three lines (e.g. Tone opening both P1 and P2 cutoffs). Unipolar or
  negative amounts are both fine (a "Darken" macro may use a negative amount).
- Presets with a part B (`parts = layer`, `split` or `b`, and lines starting `b.`): also add the same
  four macros for part B with `b.mod = ...` lines, same names, so both parts answer the same way.
- If a role has no sensible destination in a preset (a recorded instrument without a filter has no
  cutoff), pick the most musical alternative from what exists (for example Tone on a sample's own
  parameter, or a gentle `vca` dip is NOT allowed — level is not tone), or leave that macro without
  lines and write `(none)` as its name and say why.

## The syntax (read the code to be exact; every line you write must parse)

- A routing: `mod = src=macro1 dst=p1.cutoff amount=0.25` (optionally `curve=...`, `via=...`).
  Part B: `b.mod = src=macro1 dst=p1.cutoff amount=0.25`.
- Sources: `macro1`..`macro4`, `lfo1`, `lfo2`, `glfo1`, `glfo2`, `env1`.. , `wheel`, `vel`, ... See
  `sourceText` in `src/presets/BankFile.cpp` and `modSourceNames` in `src/parameters/v3/PartParameters.cpp`.
- Destinations: see `destinationText` in `src/presets/BankFile.cpp` (lines ~154-230): `pitch`, `vca`,
  `pan`, `lfo1.rate`, `lfo2.rate`, `env1.time`.. (scales all of that envelope's times), `g1.pitch`,
  `g1.level`, `g1.pan`, `g1.amount`, and a slot parameter by **the key of the slot's type in this
  preset** (`p1.cutoff`, `g1.pw`, `g2.pos`...) or by number (`p1.p3`). The keys are the first field of
  each `ParameterDescriptor` in the type's header: `src/synth/v3/VaGenerator.h`, `FmGenerator.h`,
  `Fm4Generator.h`, `WavetableGenerator.h`, `NoiseGenerator.h`, `StringGenerator.h`,
  `ModalGenerator.h`, `SampleGenerator.h`, `GrainGenerator.h`, `LadderProcessor.h`, `SvfProcessor.h`,
  `ShaperProcessors.h`, `ResonatorProcessors.h`. A destination must be `modulatable` (the last bool
  of the descriptor); the matrix only reaches voice parameters, never the effects (`f1`..`f6`,
  `i1`..`i3`).
- Scaling (read `src/synth/v3/ModMatrix.h` and `src/synth/v3/Modulation.h`): slot parameters are
  moved in their **normalised** range (amount 0.15 on a 20 Hz-20 kHz log cutoff is about 1.5 octaves);
  envelope times and LFO rates move by `amount * 4` octaves (`matrixRateOctaves`); find the pitch
  scaling yourself (existing presets use tiny amounts like 0.007 for vibrato). Look at the existing
  `mod =` lines in the bank files for amounts that are known to sound right.

## What to write

One markdown file, the path given below, with one section per preset, in bank order:

```
### <Preset name> (<bank file>)
names: <Name1>, <Name2>, <Name3>, <Name4>
mod = src=macro1 dst=... amount=...
mod = src=macro2 dst=... amount=...
mod = src=macro3 dst=... amount=...
mod = src=lfo1 dst=pitch amount=... via=macro4
(b.mod lines for part B when the preset has one)
- Name1: what the player hears when turning it up (one short line)
- Name2: ...
- Name3: ...
- Name4: ...
```

Do not edit any file other than your own draft file. Do not build or run anything. Before you finish,
re-check every line against the parser rules above (valid source, destination valid for the slot's
type in that preset, modulatable, slot count within 24 per part). In your final message, list any
preset where a role was left `(none)` and why, and anything you were unsure of.
