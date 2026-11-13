# IMPLEMENTATION_PLAN_V3.md

# CoolSynth V3 implementation plan

Status: 2026-11-12. Builds `DESIGN_V3.md` in the order `REQUIREMENTS_V3.md` section 6 gives, so
rooms of `SOUND_BANK_V3.md` ship as early as their modules exist. Each chunk is one review
cycle: one TODO item, one commit on main, Debug build and `ctest` green, docs updated, and the
rooms it names auditioned in the standalone before the next chunk starts.

Conventions: `V2 parity` means the existing measurement tests pass unchanged in meaning (saw
alias floor, decimator image rejection, steal-fade step, level with one and many voices, reverb
wet levels, latency). Rooms listed under "Ships" are authored in that chunk and normalised with
the loudness tool.

## Phase 0: scaffolding (no product change)

0.1 Module interface and descriptors. Add `src/synth/v3/Module.h` with `SlotParams`,
`NoteContext`, `SlotMemory`, `ParameterDescriptor`, `ModuleDescriptor`, `CostClass`, and the
`Slot<Variant>` template with `std::visit` dispatch and the pending-type crossfade. Tests: a
dummy generator and processor type, type change produces no step larger than the steady-state
step, arena sizing from `memoryBytes`.

0.2 Sub-block modulation context. `ModContext` with the source vector, per-slot offset arrays,
smoothing of base values toward targets per sub-block. Tests: smoothing continuity, clamping.

0.3 Parameter descriptor to APVTS bridge. The generic `pX.sN.pM` parameters with descriptor-
driven text, the part and slot path builder, the flat atomic-pointer table. Tests: every ID
unique, text round trip for each descriptor, count matches the design.

## Phase 1: core voice and parity

1.1 VA generator. Port the PolyBLEP oscillator, pulse width, sync via combine, plus unison 1 to
7 with detune and spread inside the generator, and a sub mix. Tests: tuning within 1 cent, alias
floor at A4 and A7 as V2, unison level compensation.

1.2 Ladder processor and envelopes. `LadderProcessor` from `VoiceLadderFilter` with the drive
exposed; `AnalogEnvelope` extended with curve, velocity sensitivity, loop and one-shot. Tests:
V2 filter tests ported, envelope reaches full level within the programmed attack, loop period.

1.3 Voice. Slots G1..G3, mixer with combine modes, P1..P3, VCA, pan, sub-block rendering, local
sources (velocity, key, random, drift). Tests: combine modes (sync, ring, FM, AM) produce the
expected spectra, voice headroom constant with polyphony.

1.4 Part and engine. `Part` with voices, a new allocator with per-patch and per-part caps (the voice already fades a stolen note itself), oversampled bus and
decimator, `EngineV3` with one part. The processor switches to `EngineV3`; the V2 engine is
deleted together with poly-mod and Vintage parameters. Tests: all V2 engine tests ported, V2
parity figures logged.

1.5 State and presets v3. `patchFormatVersion = 8`, rejection of 7, the YAML to table generator
rewritten for slots using the descriptor export tool, the old bank and its YAML deleted, the
normaliser pointed at the new table. Ships: Workshop (Init Saw, Init Pulse, Init Sine, Init
Noise, Init Arp), Studio 1982 (all six), Engine Room (Pure Sub, 808 Boom, Donk), Main Stage
(Reese).

1.6 Audition UI. Type menus and eight generic knobs per slot relabelled from the descriptor, the
envelope panel, the existing effect panels mapped to F1..F6 with the V2 order as default types.
Enough to hear every preset; no redesign.

## Phase 2: modulation

2.1 LFO class. Shapes (sine, triangle, saw up, saw down, square, sample-and-hold, smoothed
random, 16-step), Hz and tempo division from transport or internal tempo, phase, retrigger,
fade-in, one-shot. Used for LFO1..2 (local) and GLFO1..2 (global). Tests: period at each
division against the transport, retrigger phase, fade-in curve.

2.2 Matrix. 24 slots, sources, destinations enumerated from descriptors, via, curve, per-sub-
block evaluation into offsets. Tests: arithmetic, every destination reachable without NaN,
amount scaling by via.

2.3 Global envelope, step modulator, macros, chaos. GENV with bar-length stages and first-note
or every-note trigger; STEP with 16 values, slides, division and length; four macros; chaos
source. Tests: GENV retrigger rules, STEP timing. Ships: Engine Room Wobble, Main Stage
Riser.

2.4 Delay sync and ping-pong. Note-value times from transport, ping-pong mode, tape mode name
for the existing dark feedback path. Tests: tap spacing at 1/8 and dotted 1/8. Ships: Clock
Tower (Euclid Pulse, Ratchet Run, Step Filter, Random Walk, Polyrhythm Pad), Outrun (Neon Brass,
Night Drive, Laser Lead, Sunset Strings, Gated Pad through LFO to VCA), Main Stage Pluck Drop.

## Phase 3: generators

3.1 Noise generator. White, pink, band-passed with pitch tracking, LFSR periodic with clock
divider. Tests: spectra, LFSR period. Ships: Bedroom Tape Flute, Observatory Solar Wind
(band-pass inside the generator until SVF lands), Arcade (Coin Pulse, Boss Bass, Power-Up,
Static Hit).

3.2 FM generator, two operators. Carrier and modulator with ratio, fine, index, feedback, index
envelope through ENV3 or the matrix. Tests: ratio tuning, index to bandwidth, feedback stability.
Ships: Greenhouse (Glass Bell, Rain Chime, Crystal Pluck), Engine Room FM Knock, Workshop Init
FM.

3.3 FM generator, four operators. Six algorithms, per-operator level and ratio across P1..P8
with paging in the descriptor. Ships: Outrun Grid Keys.

3.4 Wavetable generator. Factory table set embedded (classic, digital, vocal, chip 4-bit), mip-
mapped band-limited playback, position with crossfade morph, phase randomisation. Tests: alias
floor at A7, position interpolation continuity. Ships: Observatory Nebula Pad (SVF pending),
Arcade SCC Lead (crusher pending), Workshop Init Wavetable.

## Phase 4: processors

4.1 SVF. LP2, HP, BP, notch with drive. Tests: response shapes, stability at resonance 1.
Ships: Main Stage Supersaw (without the sub part), Observatory (Nebula Pad, Deep Field).

4.2 Crusher and folder. Bit depth continuous, sample-rate reduction, dry/wet; wavefolder with
symmetry and pre-gain. Tests: bit steps, fold harmonics. Ships: Arcade SCC Lead, Basement (Acid,
Rust Bass, Concrete Pad, Siren), Main Stage Hyper Lead.

4.3 Per-voice drive and ring combine polish. Drive processor type; ring and FM combine audited
for level and aliasing. Ships: Basement Metal Hit, Main Stage Neuro Growl (formant pending).

4.4 Formant and comb. Five-vowel morph with size and resonance; tuned comb with feedback and
polarity. Ships: Bedroom Vinyl Choir, Basement Furnace Drone, Main Stage Neuro Growl.

## Phase 5: effects

5.1 Plate pre-delay, input high-pass, modulation trim; shimmer type. Ships: Observatory Shimmer
Drone and Satellite Ping.

5.2 Lo-fi type. Wow, flutter, hiss, crackle, bandwidth, bit depth, sample rate. Ships: Bedroom
Cassette Keys, Blanket Pad, Bedroom Bass (lo-fi added).

5.3 Pump, EQ, flanger. Ships: Main Stage Future Chord as a played chord (the one-key version
lands with chord memory in 7.1).

## Phase 6: physical generators

6.1 Karplus-Strong. Excitation, pick position, damping, string decay, release damping, pitch-
correct loop filter. Tests: tuning across the range, decay time. Ships: Greenhouse Kalimba, Harp
String.

6.2 Modal. Partials with material morph, mallet hardness, strike position. Ships: Greenhouse
Marimba, Bedroom Dusty Mallet.

## Phase 7: note layer and parts

7.1 Chord memory. Chord types, learned chord, strum, feeding the arpeggiator. Tests: chord
through arp ordering. Ships: Arcade Chip Chord, Clock Tower Chord Trigger, Main Stage Future
Chord.

7.2 Parts. Second part, part mix, modes, per-part caps, per-part inserts, part selector in the
audition UI, state for both parts. Tests: layer and split allocation, caps, state round trip.
Ships: Main Stage Supersaw with the sub part; a layered variant of Juno Pad and Deep Field.

## Phase 8: sample and grain

8.1 Sample generator with an embedded factory set. 8.2 Grain generator. Ships: Observatory
Granular Sky. All 68 presets exist; the bank is complete.

## Per-phase close

After every chunk: `TODO.md` item moved to `DONE.md` with what was verified, `DESIGN_V3.md`
"shipped" notes where the implementation deviated, the normaliser run when presets changed, and
the Release standalone rebuilt for audition.

## Done criteria for the whole project

V3 is not finished until all of these hold:

- Every room of `SOUND_BANK_V3.md` ships with its signature audible, all 68 presets exist, and
  the old bank is gone.
- A supersaw exists as a sound you can reach without building it: the Main Stage Supersaw preset
  plus a VA generator unison that makes a JP-8000-style stack (seven saws, non-linear detune
  curve, centre-versus-sides mix, high-passed low end) from a few knobs.
- Two parts can play at once as a layer or a split from one patch.
- The processor runs the V3 engine only, the state format is 8, and the suite is green.
