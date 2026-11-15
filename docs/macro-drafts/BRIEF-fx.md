# Brief: effect routes for the presets' macros (D179)

Read `docs/macro-drafts/BRIEF.md` first: the four macro roles, the naming style and the hard rules all
still hold. Every factory preset (`resources/presets/*.csbank`) now ends with its drafted macros: a
`macros = names=...` line and `mod = src=macroN ...` lines that move the voice.

**New since then (D179):** a macro can also move the **effects**. A part may have up to **8** effect
routes, written like matrix lines:

```
fx = src=macro3 dst=f2.feedback amount=0.2
fx = src=glfo1 dst=i1.mix amount=0.15 via=macro4 curve=unipolar
b.fx = src=macro1 dst=i2.tilt amount=0.2     (part B's inserts, or the master, from part B's macros)
```

- Destinations: a setting of the part's own inserts `i1`..`i3` or of the master slots `f1`..`f6`, by
  the key of the effect type that slot holds **in this preset** (`f1.mix`, `i1.feedback`, `f2.size`)
  or by number (`f2.p3`). The keys are the first field of each parameter in `insertDescriptor` in
  `src/synth/v3/Inserts.h`; the parameter must be `modulatable` (the last bool). Only slots the
  preset already sets (`i1 = Delay ...`, `f2 = Reverb ...`); an empty slot cannot be routed.
- Sources: only part-wide ones: `macro1`..`macro4`, `glfo1`, `glfo2`, `genv`, `step`, `chaos`,
  `wheel`, `bend`, `at`, `expression` (a voice's `lfo1`/`env1`/`vel` is refused). `via=macroN` scales
  a route by a macro, so a global LFO can move an effect only as far as the macro is up.
- Values move the setting in its **normalised** 0..1 range (read the parameter's range and skew in
  `Inserts.h` to know what an amount means: 0.2 on a linear 0..1 mix is +20 %; on a log time it is a
  ratio). The result is held inside 0..1.
- Read `src/synth/v3/EffectRoutes.h` and `effectDestinationText` in `src/presets/BankFile.cpp`.

## Your job

For each preset in your bank files, add effect routes **where an effect makes a macro's role better**
(Time widening a delay's feedback or a reverb's size; Tone opening an EQ's high or tilt; Character
pushing an insert drive or a lofi's bits; Movement deepening a chorus or phaser, or a global LFO on an
effect via macro4). Many presets will need none: add a route only when a player would hear it as part
of that macro's job. Keep each macro's name unless it no longer fits; if it must change, say so.

Hard rules (as before): only **new** lines; nothing may change with every macro at 0 (only
`src=macroN` or `via=macroN` lines); at most about **+3 dB** louder with all four macros full up
together with the voice routes already there (a reverb or delay mix rise adds level: keep wet rises
modest, about +0.15 to +0.25 on a mix); at most 8 `fx` lines per part; nothing that can run away
(delay feedback stays under 0.9 at full, counting what the preset sets).

## What to write

One markdown file, the path given below, one section per preset you change, in bank order:

```
### <Preset name> (<bank file>)
fx = src=macro3 dst=f2.feedback amount=0.2
- Swell: also lengthens the delay's echoes (feedback 0.35 to about 0.55)
names: <only if a name must change: all four names>
```

At the end, a line listing the presets you left without effect routes. Edit no other file, build
nothing. Re-check every line against the parser rules before you finish.
