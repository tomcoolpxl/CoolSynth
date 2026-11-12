# TODO

## V3: sound bank and modular engine (started 2026-11-12)

The factory bank is being replaced by the "Places" bank in `SOUND_BANK_V3.md`, and the bank is
written against the engine we want. `REQUIREMENTS_V3.md` lists what that engine must have and
proposes a slot-chain shape. The old V2 backlog items that V3 absorbs are listed at the bottom.

1. Decide the engine shape (`REQUIREMENTS_V3.md` section 7: slot chain, free graph, or extend
   V2) and record it there.
2. Write `DESIGN_V3.md` from the decision: module interface, slot parameter descriptor model and
   stable parameter IDs, modulation context and matrix evaluation, parts, effect chain, preset
   and state format v3, CPU budget per module type, what V2 code is kept where.
3. Write `IMPLEMENTATION_PLAN_V3.md` with the eight phases of `REQUIREMENTS_V3.md` section 6,
   each small enough for one review cycle, each ending with the rooms it ships.
4. Phase 1 core: slot chain with the VA generator, ladder processor, three envelopes, parity
   tests from V2 (alias, steal fade, latency, levels) green.
5. Author Workshop, Studio 1982 and Engine Room from `SOUND_BANK_V3.md` in the v3 preset format,
   delete the old bank, update the preset tests, run the normaliser.
6. Phase 2 modulation (LFO sync, per-voice LFOs, S&H, step modulator, global envelope, matrix,
   delay sync), then Clock Tower.

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
