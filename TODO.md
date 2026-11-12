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
6. Phase 0.3: descriptor-driven generic parameters and the slot path builder in the parameter
   layout; ID uniqueness and text round-trip tests.
7. Phase 1.1: VA generator with unison, sub mix and combine-ready sync; tuning and alias tests.

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
