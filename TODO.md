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
12. Audition the presets; retune by ear what the ear flags, then rerun the loudness tool. It is
   not in the default build, so build it first:
   `cmake --build build --config Debug --target CoolSynthPresetNormalizer`, then from `build`
   run `CoolSynthPresetNormalizer_artefacts/Debug/CoolSynthPresetNormalizer.exe --apply`.
13. Phase 2.1 done 2026-11-13: `src/synth/v3/Lfo.h`, LFO1..2 per voice and GLFO1..2 per part,
   their parameters and bank sections, `tests/V3LfoTests.cpp`. Juno Pad moved to 2.2.
   Still open: the VA generator's cost. Release, eight-note chords: Supersaw 36 % of real
   time (worst block 60 %), Clav Keys 25 %; a 16-voice Supersaw would reach the limit. The
   optimised Debug build measures the same as Release. Measure with `COOLSYNTH_CPU_REPORT=1`
   and the test executable.
14. Phase 2.2 done 2026-11-13: `src/synth/v3/ModMatrix.h`, 24 slots per part, bank `mod` lines,
   `tests/V3MatrixTests.cpp`; Juno Pad, 808 Boom and the Sync Lead sweep shipped.
15. Phase 2.3 done 2026-11-13: global envelope, `GlobalModulators.h` (step, chaos), macros,
   envelope-time destinations, `tests/V3GlobalModulatorTests.cpp`; Wobble and Riser shipped.
16. Phase 2.4 done 2026-11-13: delay sync and ping-pong, `tests/V3DelayTests.cpp`; Outrun (five
   presets), Clock Tower (five) and Pluck Drop shipped. Phase 2 complete: 29 presets.
17. Phase 3.1 done 2026-11-13: `src/synth/v3/NoiseGenerator.h`, `tests/V3NoiseTests.cpp`; Init
   Noise, Tape Flute, Solar Wind, Coin Pulse, Boss Bass, Power-Up, Static Hit, Riser's noise.
18. Phase 3.2 done 2026-11-13: `src/synth/v3/FmGenerator.h` (FM2), `tests/V3FmTests.cpp`; Glass
   Bell, Rain Chime, Crystal Pluck, FM Knock, Init FM. 41 presets.
19. Phase 3.3 done 2026-11-13: `src/synth/v3/Fm4Generator.h` (FM4, six algorithms), tests in
   `tests/V3FmTests.cpp`; Outrun Grid Keys. 42 presets.
20. Phase 3.4 done 2026-11-13: `src/synth/v3/Wavetables.h/.cpp`, `WavetableGenerator.h`,
   `tests/V3WavetableTests.cpp`; Init Wavetable, Nebula Pad, SCC Lead. Phase 3 complete: 45 presets.
21. Phase 4.1 done 2026-11-13: `src/synth/v3/SvfProcessor.h`, `tests/V3SvfTests.cpp`; Supersaw and
   Nebula Pad on the SVF, Deep Field. 46 presets.
22. Phase 4.2 done 2026-11-13: `src/synth/v3/ShaperProcessors.h` (crusher, folder), the Quantized
   matrix curve, `tests/V3ShaperTests.cpp`; SCC Lead's crusher, Basement Acid, Rust Bass, Concrete
   Pad, Siren, Main Stage Hyper Lead. 51 presets.
23. Phase 4.3 done 2026-11-13: the Drive processor, the combine audit (no change needed),
   `V3DriveTests`; Basement Metal Hit. 52 presets.
24. Phase 4.4 done 2026-11-13: `src/synth/v3/ResonatorProcessors.h` (formant, comb),
   `tests/V3ResonatorTests.cpp`; Vinyl Choir, Furnace Drone, Neuro Growl. Phase 4 complete: 55
   presets.
25. Phase 5.1 done 2026-11-13: plate pre-delay, low cut, modulation and shimmer,
   `tests/V3ReverbTests.cpp`; Shimmer Drone, Satellite Ping, Nebula Pad's pre-delay. 57 presets.
26. Phase 5.2 done 2026-11-13: `src/synth/LoFi.h`, nine `lofi*` parameters, `tests/V3LoFiTests.cpp`;
   Cassette Keys, Blanket Pad, Bedroom Bass, lo-fi on Vinyl Choir and Concrete Pad. 60 presets.
27. Phase 5.3 done 2026-11-13: `src/synth/ModernFx.h` (pump, EQ, flanger), 13 parameters,
   `tests/V3ModernFxTests.cpp`; Main Stage Future Chord. Phase 5 complete: 61 presets.
28. Phase 6.1 done 2026-11-13: `src/synth/v3/StringGenerator.h`, the `noteOff` and `strike` module
   hooks, `tests/V3StringTests.cpp`; Greenhouse Kalimba and Harp String. 63 presets.
29. Phase 6.2 done 2026-11-13: `src/synth/v3/ModalGenerator.h`, `tests/V3ModalTests.cpp`;
   Greenhouse Marimba and Bedroom Dusty Mallet. 65 presets.
30. Phase 7.1 done 2026-11-13: `src/synth/v3/ChordMemory.h`, the `pA.chord.*` parameters, chord
   learning through the processor, `tests/V3ChordTests.cpp`; Arcade Chip Chord, Clock Tower Chord
   Trigger, one-key Main Stage Future Chord. 67 presets.
31. Phase 7.2 done 2026-11-13: two parts in `EngineV3` (modes, ranges, transpose, shared voice
   limits, part mix with an effects share), `pB.*`, `pA.mix.*`, `part.mode`, `b.` lines in bank
   files, `tests/V3PartsTests.cpp`; Supersaw with its sub part, Juno Pad and Deep Field in two
   layers. Open from this phase: per-part insert slots, which need effects as slot types.
32. Phase 8 done 2026-11-13: `src/synth/v3/Samples.h` and `.cpp` (five generated factory
   samples), `SampleGenerator.h`, `GrainGenerator.h`, `tests/V3SampleTests.cpp`; Observatory
   Granular Sky. The bank is complete at 68 presets.
33. Phase 9: the real UI. Not started: it is designed with the user once the engine has settled.
34. Decided with the user on 2026-11-13 (decisions D81 to D86), in this order:
    - download the CC0 libraries, build an audition page, and let the user pick instruments;
    - measure the physical-model presets against real recordings, and classic synth references;
    - three insert slots per part (the rack stays as the master): done, D93;
    - a granular effect as an insert type: done, D102;
    - Harp String: the user found neither version close to the recording (D94); redone as a
      finger's pluck (D101): "closer, but not there", too little echo dry and too much with
      its reverb (D105, D107); refitted harmonic by harmonic, with a second string for the
      pluck's bright ring and the new Room effect set from the recording's reflections
      (D108): the user found the third try close enough (D111). Done;
    - Recorders: the user heard overdrive on chords. Two listening tests traced it to the
      soprano's E6 recording, whose overtones clash with the other notes; at the user's
      choice E6 and F sharp 6 now play from clean neighbours (D113); the user found the new
      Soprano clip good (D131). Done;
    - after the user's picks: key zones in the sample generator, sample files beside the plugin
      at the best quality, install options;
    - "The Museum": 8 to 12 named classic synth sounds from published patch settings, each
      admitted by the user in a side-by-side against a reference recording (D88). The user
      chose the first sounds (D103): DX7 E.PIANO 1 and BASS 1, Prophet-5 sync lead and brass,
      a Minimoog bass and lead, and the hoover, LatelyBass and Juno-106 patches (sources for
      the last three still to be found). DX7 E.PIANO 1 is admitted (D107) and is the
      first preset of the room Museum, DX7 BASS 1 the second (D111), Prophet-5 Sync II and
      Brass the third and fourth (D115), Minimoog Fat Bass the fifth (D118, with its sizzle
      D128), the Minimoog Trilogy lead after seven rounds (D130). TX81Z LatelyBass, from the instrument's own
      preset data, is the sixth (D117, admitted D119). Hoover and
      Juno-106 still have no source. The opening of Daft Punk's "Contact" (a sample of The Sherbs'
      "We Ride Tonight", 1981; no source names the instruments): the first synth, built from the
      recording alone (D120), is admitted (D121); the second, heavier
      synth (bright, every voice at its own place between the speakers) is admitted too
      (D123, D125).
    Done so far: both libraries downloaded; the audition page is at `audition/index.html`
    (start the `audition` preview server to hear it); marimba and kalimba corrected and
    accepted (D94); the user's 74 picks built into a library of FLAC files (1.4 GB, in
    `audition/library`) that the Sample generator plays, with the room "Conservatory" of 40
    presets (D96 to D98).
    The library is rebuilt from the author's own mapping files (D100), which corrects the
    faults of D99; the Conservatory presets are loudness-normalised and each style has a
    demonstration on the audition page, waiting for the user's ear.
    Open on the sampled instruments: the user's verdicts; a voice whose one-shot recording has
    ended still holds its voice until its release is over (up to 20 s on bells and mallets:
    no sound is lost, older voices are taken first, but it costs processing); install options
    and packs; the wine glasses as a source for the Grain generator.
    For phase 9 (asked by the user on 14 November, D132): effect slots and a preset's noise
    quick to reach, and saving one's own version of a preset.
35. Left open by phases 2 to 8, in the order they would pay off:
    - MPE: each note on its own MIDI channel with its own bend, pressure and slide (the MPE
      Pressure and MPE Slide sources are listed but fed by nothing). Later, the user's choice
      (D136): key pressure and the expression pedal are built, MPE waits for an MPE controller;
    - grains from another generator, which needs a buffer pool handed out by the part (D79);
    - user sample import (D78);
    - the VA generator's CPU cost (an eight-note saw chord is about 15 % of real time in the
      optimised Debug build).

Decisions taken while the user was away are in `DECISIONS_LOG.md`, each with every option;
review them.

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

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because
  of stale incremental-link databases; incremental linking is now off in Debug. If it ever
  returns, delete the `.ilk` files under `build` and build again.

- UI comes last (plan phase 9). Until then only throwaway UI: the generic editor. In the
  standalone the MiniLab knobs do nothing until then (their mapping lived in the V2 editor);
  notes work, devices are chosen in JUCE's audio and MIDI settings.

- Supersaw must exist by the end of the project: see the done criteria at the end of
  `IMPLEMENTATION_PLAN_V3.md`. The VA generator unison from phase 1.1 is the base; the JP-8000
  detune curve and centre-versus-sides mix are still to add (phase 3 or 4).
