# TODO

## V3: sound bank and modular engine (started 2026-11-12)

The factory bank is being replaced by the "Places" bank in `SOUND_BANK_V3.md`, and the bank is
written against the engine we want. `REQUIREMENTS_V3.md` lists what that engine must have and
proposes a slot-chain shape. The old V2 backlog items that V3 absorbs are listed at the bottom.

1. Engine shape decided 2026-11-12: slot chain (`REQUIREMENTS_V3.md` section 7). Done.
2. `DESIGN_V3.md` written 2026-11-12. Done.
3. `IMPLEMENTATION_PLAN_V3.md` written 2026-11-12. Done.
4. Phase 0.1 done 2026-11-12: `src/synth/v3/Module.h` and `tests/V3ModuleTests.cpp`.
5. Phase 0.2 done 2026-11-13: `src/synth/v3/Modulation.h` and `tests/V3ModulationTests.cpp`.
6. Phase 0.3 done 2026-11-13: `src/parameters/v3/SlotParameters.h/.cpp` and
   `tests/V3ParameterTests.cpp`. Phase 0 complete.
7. Phase 1.1 done 2026-11-13: `src/synth/v3/VaGenerator.h` and `tests/V3GeneratorTests.cpp`.
8. Phase 1.2 done 2026-11-13: `src/synth/v3/LadderProcessor.h`, `src/synth/v3/Envelope.h` and
   `tests/V3ProcessorTests.cpp`.
9. Phase 1.3: `Voice` with G1..G3, mixer and combine wiring, P1..P3, VCA, pan, sub-block
   rendering and local sources; combine and headroom tests.

## Carried over from the V2 backlog

Still valid independently of the engine shape:

- Ladder tuning by ear: the bass makeup factor, whether to expose a small filter drive amount,
  and whether a per-stage-saturated Moog-style mode is wanted next to the input-saturated voicing.
- Measurement-style regressions still missing: THD of a sine through the ladder at low resonance
  below a stated bound; attack reaching full level within the programmed time; reverb RT60 within
  a range for a given size.

Absorbed into V3 (do not do them on V2):

- Audition pass on the old factory presets: dropped, the bank is replaced.
- Unison voice count and detune: V3 generator unison and whole-voice unison count.
- Multi-part layering: V3 parts (`REQUIREMENTS_V3.md` section 3.5).
- Plate pre-delay, input high-pass, modulation trim: V3 effects catalogue.
- Delay ping-pong and tempo sync: V3 phase 2.
- ARP Expansion Phase F (manual audition of the curated arp presets): dropped with the old bank;
  the arpeggiator itself carries into V3 unchanged.

## Standing notes

- Supersaw must exist by the end of the project: see the done criteria at the end of
  `IMPLEMENTATION_PLAN_V3.md`. The VA generator unison from phase 1.1 is the base; the JP-8000
  detune curve and centre-versus-sides mix are still to add (phase 3 or 4).
