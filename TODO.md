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
9. Phase 1.3 done 2026-11-13: `src/synth/v3/Voice.h/.cpp`, `OffModules.h`, `DspMath.h` and
   `tests/V3VoiceTests.cpp`; the VA generator, ladder and envelope rebuilt from first principles.
10. Phase 1.4 done 2026-11-13: `Part`, `EngineV3`, `PartParameters`, the processor on V3, the
   generic editor, the V2 engine, editor and bank deleted, tests ported (`EngineTestAdapter.h`).
11. Phase 1.5 done 2026-11-13: bank files in `resources/presets`, `presets/BankFile`, the
   loudness tool with `--apply`; 14 presets in Workshop, Studio 1982, Engine Room, Main Stage.
12. Audition the 14 presets; retune by ear what the ear flags, then rerun the loudness tool with
   `--apply` (from the build directory).
13. Phase 2.1: LFO class (local and global, sync, shapes) and Juno Pad.
   Before or with it: the VA generator's cost. Release, eight-note chords: Supersaw 36 % of real
   time (worst block 60 %), Clav Keys 25 %; a 16-voice Supersaw would reach the limit. Debug runs
   the same at four to six times the cost (Supersaw 154 %), too slow to play. Measure with
   `COOLSYNTH_CPU_REPORT=1` and the test executable.
14. Phase 2.2: the matrix, 808 Boom and the Sync Lead sweep.

## Carried over from the V2 backlog

Still valid independently of the engine shape:

- Ladder by ear: drive headroom, the bass default, the mode level matching, and whether the
  self-oscillation level (rms 0.15 at full resonance) wants an output lift.
- If the saw's -79 dB alias floor at A7 ever shows in a mix, go to a 6-point (quintic B-spline)
  BLEP: about another 25 dB for two more samples of latency.
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

- UI comes last (plan phase 9). Until then only throwaway UI: the generic editor. In the
  standalone the MiniLab knobs do nothing until then (their mapping lived in the V2 editor);
  notes work, devices are chosen in JUCE's audio and MIDI settings.

- Supersaw must exist by the end of the project: see the done criteria at the end of
  `IMPLEMENTATION_PLAN_V3.md`. The VA generator unison from phase 1.1 is the base; the JP-8000
  detune curve and centre-versus-sides mix are still to add (phase 3 or 4).
