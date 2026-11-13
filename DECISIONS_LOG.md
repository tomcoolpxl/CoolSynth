# Decisions log

Decisions taken without asking while the user was away. Each entry lists every option that
was on the table, with its consequence and side effects, the option taken and why. Review and
overrule anything here; each entry says what changing it would take.

Entries are newest last.

## D1. Juno Pad moves from phase 2.1 to phase 2.2 (2026-11-13, phase 2.1)

The plan ships Juno Pad with the LFO. Its signature is pulse-width modulation from an LFO, and
an LFO reaches a destination only through the matrix, which is phase 2.2.

- **Move Juno Pad to 2.2 (taken, recommended).** It ships with the matrix, one chunk later,
  with the route the bank file will use for every later preset. No throwaway code.
- Add a hardwired "LFO1 to G1 pulse width" parameter now. Juno Pad ships an hour earlier, but
  the parameter is a dead end once the matrix exists and stays in the patch format.
- Ship Juno Pad without PWM. It would not sound like a Juno pad, which is the whole preset.

Changing it: nothing to undo; Juno Pad lands in 2.2 either way.

## D2. Tempo for synced LFOs: the host's, else the arp's internal tempo (2026-11-13, phase 2.1)

A synced LFO needs a tempo when the host gives none (the standalone never does).

- **Host tempo when the host reports one, otherwise the existing arp internal tempo parameter
  (taken, recommended).** One tempo for the arp and every synced modulator, so an arp and a
  synced wobble never drift apart. No new parameter.
- A separate part tempo parameter. More flexible, but two tempos that can disagree, and a
  second knob to set in every synced preset.
- Fixed 120 BPM without a host. Simple, but no way to set the standalone's tempo.

Changing it: give `PartSettings` its own tempo and read it in `Part::renderChunk`.

## D3. What the "16-step" LFO shape is (2026-11-13, phase 2.1)

The plan lists a 16-step shape without saying what it holds; the STEP modulator with editable
values is a separate source in phase 2.3.

- **A rising staircase of 16 equal levels per cycle (taken, recommended).** Predictable and
  useful on its own: through a pitch route it plays a stepped run, on a filter a stepped sweep.
  Bank name `steps`.
- A looping random 16-step pattern, new per note. Overlaps sample-and-hold and cannot be set.
- The STEP modulator's values. Couples two modulators and duplicates the STEP source.

Changing it: `LfoShape::steps` in `src/synth/v3/Lfo.h`.

## D4. LFO delay as well as fade-in (2026-11-13, phase 2.1)

The plan names a fade-in. Logic's and Halion's LFOs, and the Juno's, also have a delay before
the modulation starts (checked online: Logic Pro "Delay" and "Attack", Halion one-shot modes).

- **Delay plus fade-in (taken, recommended).** Delayed vibrato, the classic use, needs both. One
  more parameter per LFO (nine instead of eight).
- Fade-in only. Delayed vibrato would start rising at once.

Changing it: drop `delay` from the LFO group; presets then must not use it.

## D5. LFO behaviour details (2026-11-13, phase 2.1)

Smaller calls made while writing the class, each with what else was possible:

- **Free-running local LFOs advance only while their voice sounds (taken).** The alternative,
  advancing all 16 voices' LFOs all the time, costs CPU for no audible gain: with retrigger off
  the phase a note meets is arbitrary either way.
- **Delay and fade restart at every note even with retrigger off; retrigger decides only the
  phase (taken).** The alternative, no fade at all when free-running, loses the
  free-running-vibrato-that-fades-in patch.
- **A synced, free-running LFO follows the host's song position while the transport plays
  (taken).** The alternative, ignoring the song position, lets loops drift between passes.
- **Global LFOs default to synced and free-running; local ones to free rate and retrigger
  (taken).** Matches the design text ("tempo-synced, one phase shared by all voices"). The
  alternative was identical defaults everywhere.
- **Fade-in is a straight line (taken).** An exponential or S-curve fade was the alternative;
  the straight line is what the Juno and most plug-ins do and what the test checks.
- **Shape start points: sine and triangle at 0 rising, the saws and steps at their low end
  (saw down at its top), square high (taken).** The alternative, all shapes from their low
  point, makes sine and triangle start with a jump when routed to pitch.

## D6. How far a pitch route reaches (2026-11-13, phase 2.2)

Matrix amounts run from -1 to 1. The design says pitch destinations are in semitones but not
what an amount of 1 means.

- **1 is 48 semitones (taken, recommended).** Four octaves cover the 808 drop (24), the sync
  sweep (24 to 36) and a riser; vibrato of a quarter tone is 0.005, still writable in a bank
  file. Matches the slot pitch span.
- 1 is 24 semitones. Finer for vibrato, but a three-octave sync sweep needs two slots.
- 1 is 96 semitones. Covers everything in one slot, but vibrato amounts get tiny.
- Amounts written in semitones for pitch slots. Most readable, but the amount parameter would
  have to know its slot's destination; a job for the real UI in phase 9.

Changing it: `matrixPitchSemitones` in `src/synth/v3/ModMatrix.h`; presets scale with it.

## D7. What the VCA destination does (2026-11-13, phase 2.2)

- **Gain is 1 plus the offset, held between 0 and 1 (taken, recommended).** Negative amounts
  duck, a unipolar LFO with amount -1 gates (Outrun's Gated Pad), and modulation can never push
  a voice above its envelope, so headroom stays as measured. The gain ramps across each
  sub-block, so a square gate does not click (tested).
- Held between 0 and 2. Positive amounts could boost, but a voice could double in level and
  break the fixed headroom.
- An offset in decibels. Natural for tremolo, but a gate needs a very large negative amount.

Changing it: the VCA lines at the end of `Voice::renderSubBlock`.

## D8. Matrix curves (2026-11-13, phase 2.2)

The design names linear, exponential and S-curve. Checked online: HALion and Waldorf split
sources into unipolar (0..1) and bipolar (-1..1); X-Stream offers logarithmic positive and
negative curves; Ultra and HISE multiply the amount by the aux (via) source.

- **Linear, Unipolar, Exponential, Logarithmic, S-Curve (taken, recommended).** Unipolar turns
  a bipolar LFO into 0..1, which gating and one-sided vibrato need; logarithmic is the inward
  curve next to the outward exponential. Curves keep the sign; the via source scales after.
- The design's three only. A unipolar LFO would need an extra slot with a constant offset,
  which the matrix has no source for.

Changing it: `ModCurve` and `applyCurve` in `ModMatrix.h`; add new curves only at the end.

## D9. Matrix destinations left for later (2026-11-13, phase 2.2)

The design also lists envelope times and other matrix slots' amounts as destinations.

- **Defer both to phase 2.3 or later (taken, recommended).** Envelope times are read once per
  segment, so they need a note-on evaluation path, which comes naturally with the global
  envelope in 2.3. A slot amount as destination does what the via source already does.
  Destination numbers stay stable because new ones go at the end.
- Build them now. More work in 2.2 for no preset that needs them yet.

## D10. Juno Pad uses a global LFO (2026-11-13, phase 2.2)

- **GLFO1, free-running, sweeping every voice's pulse width together (taken, recommended).**
  The Juno-106 has a single LFO for all voices (checked online), and the pad's shimmer comes
  from the voices moving together under the chorus.
- LFO1 per voice. Each note would sweep on its own, an ensemble feel, but not a Juno.

## D11. 808 Boom playing behaviour (2026-11-13, phase 2.2)

- **Legato with 70 ms glide on overlapping keys, both envelopes one-shot (taken,
  recommended).** Overlapping keys slide without restarting the boom (the trap 808 slide),
  detached keys hit again, and a short tap still booms for its full 1.4 s.
- Mono with retrigger. Every key restarts the boom; no slides.
- Poly. Booms stack into mud on fast notes.

The recipe's pitch drop (24 semitones in 60 ms) is the modern 808-bass punch; the original
TR-808 sweeps much less (one study of samples found a median under a semitone), checked online.

## D12. Bank master gains corrected by 0.5 to 2 dB (2026-11-13, phase 2.2)

The loudness tool is not part of the default build, so its executable dated from phase 1.5.
Rebuilt, it raised every preset by 0.5 to 2 dB. A build of the tool at the commit before phase
2.1 asks for the same corrections, so the shift came from the earlier preset-leak fix, not from
the LFOs or the matrix (the one-voice level test is also unchanged).

- **Apply the corrections (taken, recommended).** The bank is back on its loudness targets; a
  second pass changes nothing.
- Keep the old masters. Every preset 1 to 2 dB under target, and the new presets louder than
  the old ones.

Also taken: the TODO now says to build the tool's target before running it. Adding it to the
default build was the alternative; it would make every build slower for a tool run rarely.

## D13. Riser ships before the noise generator (2026-11-13, phase 2.3)

The recipe layers noise over a saw; noise is phase 3.1.

- **Ship Riser now with the saw stack and add the noise layer in 3.1 (taken, recommended).** The
  signature, an eight-bar rise in pitch, filter, width and an accelerating pulse, is all there.
- Wait for 3.1. Nothing to audition in the meantime.

## D14. What opens and closes the global envelope (2026-11-13, phase 2.3)

- **The keys as played, before the arpeggiator (taken, recommended).** It opens with the first
  key of a phrase (none other held) or, with trigger Every Note, with every key, retriggering
  from its current level. It closes when the last key and the pedal are up and no latched arp
  notes remain. A riser under an arp keeps rising while the chord is held.
- The voices: open while any voice sounds. A release tail would hold it open, and every arp
  step would count as a new note.
- The arp's notes. The envelope would restart on every step.

## D15. Synced global-envelope times (2026-11-13, phase 2.3)

- **One set of millisecond times, read as at 120 BPM and scaled by 120 / tempo when sync is on;
  bank files may write `8bars` or `2beats` (taken, recommended).** One parameter per stage; at
  120 BPM a bar is 2000 ms. The generic editor shows the 120 BPM milliseconds until the real UI.
- Separate bar and beat parameters for each stage. Twice the parameters, two ways to set the
  same thing.
- Times always in beats when synced, with their own range. The same parameter would mean
  different units depending on another switch, worse for automation.

## D16. Step modulator details (2026-11-13, phase 2.3)

- **Values -1..1; slide per step with one shared glide (the share of a step a slide takes);
  retrigger on by default (taken, recommended).** With retrigger on it restarts at the first
  note after silence, like the global LFOs, so a pattern starts on the first step in the
  standalone. Off, it follows the host's song position while the transport plays.
- Retrigger off by default. Patterns would sit on the song's grid in a DAW without a setting,
  but in the standalone each phrase would start on an arbitrary step.
- A glide time per step. Sixteen more parameters for a rarely needed difference.

## D17. The chaos source (2026-11-13, phase 2.3)

- **A random walk: a new random target every 0.5 to 1.5 periods of its rate, through two
  smoothing stages; one parameter, the rate (taken, recommended).** Wanders without corners
  (largest step 0.0013 per sub-block at 1 Hz), bounded to -1..1.
- A chaotic attractor (Lorenz, logistic map). More "chaotic" in name, but it orbits a fixed
  shape and needs scaling per parameter set.
- A second "smoothness" parameter. S&H on a global LFO already gives the stepped version.

## D18. Macros are smoothed (2026-11-13, phase 2.3)

- **Each macro moves toward its setting with a 10 ms time constant, per sub-block; the first
  block starts at the setting (taken, recommended).** A macro on a cutoff or level does not
  zipper when automated or turned.
- No smoothing. Host automation would step the destination once per block.

## D19. Envelope-time destinations read once per note (2026-11-13, phase 2.3)

- **Evaluated when a note starts or retriggers, from that note's velocity, key and random value
  and the part's current sources; an amount of 1 is 16 times longer (taken, recommended).** The
  uses are velocity and key to envelope time, which are per-note values anyway. No cost while
  the note plays.
- Continuous modulation, rescaling the running segment every sub-block. An LFO could wobble a
  decay, but it needs a segment rescale with an exponential per change.

## D20. The loudness tool measures risers at their top (2026-11-13, phase 2.3)

The tool measures 4 s of each preset; Riser takes 16 s to rise.

- **When a matrix slot uses the global envelope, the tool first renders through the envelope's
  delay, attack, hold and decay (up to 40 s) and measures the 4 s after (taken, recommended).**
  Riser is judged at its loudest, so its top does not come out 10 dB above the bank.
- Measure the first 4 s. Riser's top would be far too loud.
- Leave risers out of the tool and set their master by hand. One more exception to remember.

Found while testing, before commit: the global envelope read a block's settings after that
block's events, so the first note after a change started it with stale times (the same ordering
trap as the earlier preset leak). The settings are now applied before the events.

## D21. Synced delay note values (2026-11-13, phase 2.4)

- **Fourteen values from 1/32 to one bar, with dotted and triplet values, default 1/8; the
  longest delay grows from 1 s to 4 s, a bar at 60 BPM (taken, recommended).** Covers every
  common echo (1/8, dotted 1/8, 1/4) at any usable tempo. The free time knob keeps its 1 s range.
- Reuse the LFO divisions. They run to eight bars, useless for an echo, and miss 1/2. and 1/2T.
- Keep the 1 s line. A synced 1/4 note would cut off below 60 BPM, a dotted 1/4 below 90.

Tempo comes from the host when it reports one, else the arp's internal tempo (as in D2).

## D22. How ping-pong works (2026-11-13, phase 2.4)

Checked online: the E-MU Proteus X manual and Steinberg's Multi Delay sum the input to mono into
the left line and cross-couple the two lines; pyo's example warns that a block-based version
runs the right side one block late.

- **Mono sum into the left line, the lines cross-coupled per sample through the same darkening
  feedback path as tape mode (taken, recommended).** Echoes land left at T, right at 2T, left at
  3T (tested), each bounce scaled by the feedback and a little darker, so ping-pong and tape
  sound like the same echo unit. Per-sample coupling has no block lag. With no feedback there
  is a single echo on the left.
- A clean, unfiltered feedback path for ping-pong. Brighter, but a second voice for the delay.
- Stereo cross-feedback without the mono sum (Retrologue's "Cross"). Keeps the input's stereo
  image but a centred source no longer bounces.

Changing mode clears the lines, since the two leave very different echoes behind.

## D23. The delay starts at its settings (2026-11-13, phase 2.4)

Found while writing the tap test: after prepare, and after the delay is switched on, its time
glided from the default 250 ms to the setting over about 80 ms and its mix ramped from zero.
The first echoes of a freshly loaded patch bent in pitch, and at full mix the dry signal
leaked through for 20 ms.

- **Start at the settings on the first block after a reset or after switching on; keep the
  tape-style glide for changes while it runs (taken, recommended).**
- Leave it. Every preset load would bend its first echoes.

An older effect test (disabling time-based effects clears their tails) had relied on that
leak; it now renders long enough for the first echo to arrive.

## D24. Outrun and Clock Tower authoring choices (2026-11-13, phase 2.4)

- **"UNISON-N" in Neon Brass and Sunset Strings is the VA generator's own unison (2 and 4 per
  saw), not the part's Unison play mode (taken, recommended).** The play mode is monophonic;
  brass and strings must stay polyphonic.
- **Gated Pad gates through one global square LFO at 1/16, restarted with the first chord and
  starting open (taken, recommended).** Every voice chops together. A per-voice LFO would chop
  each note on its own grid, which smears the gate. Retrigger off would lock to a DAW's grid
  but start at a random point in the standalone.
- **Polyrhythm Pad: pulse width from LFO1 at 1/4 (free-running, so in a DAW it locks to the
  song) and cutoff from GLFO1 at 1/4T, three against two (taken).**
- **Bank files `03-outrun.csbank` and `10-clock-tower.csbank`, numbered by the room order of
  `SOUND_BANK_V3.md` (taken).** Grid Keys waits for four-operator FM (3.3), Chord Trigger for
  chord memory (7.1).
- Random Walk's voice level was raised so it reaches its loudness target under the +6 dB master
  cap. Euclid Pulse and Ratchet Run are held below target by the tool's -3 dBFS peak ceiling,
  as plucks are.

Also met: a stale debug database of the embedded bank library made the linker reject it
(LNK1236) after two bank files were added; deleting `build/CoolSynthFactoryBank.dir/Debug`
fixed it. Nothing to decide, noted in case it returns.

## D25. How the noise generator makes its four kinds (2026-11-13, phase 3.1)

Checked online: the NESdev wiki and emulator sources give the NES noise channel as a 15-bit
shift register with feedback from bit 1 (32767 steps) or bit 6 (93 steps, "short mode"); the
musicdsp pink-noise page notes that fixed pinking coefficients degrade at higher sample rates.

- **Pink from seven first-order shelves designed at the running sample rate, poles every two
  octaves from 10 Hz, zeros an octave above (taken, recommended).** Measured within 0.67 dB per
  octave from 62 Hz to 16 kHz at the 96 kHz voice rate. The alternatives: Paul Kellet's fixed
  coefficients (made for 44.1 kHz, they drift at 96 kHz), or Voss-McCartney (cheap, but rippled).
- **Band noise: a state-variable band-pass on the note, its gain set from the resonance and the
  note so the level stays even (taken).** Measured within 0.4 dB from 200 Hz wide to 3 kHz narrow.
  Without the compensation a high, narrow band would be 20 dB quieter than a low, wide one.
- **Chip: the register clocked at 93 times the note, so Short mode plays the note; Clock moves it
  in semitones like the NES period divider; every register step band-limited with the VA's
  4-point BLEP (taken, recommended).** A Short register at 375 Hz repeats every 256 samples to
  within 0.001. A naive register aliases badly once the clock passes the sample rate. Keeping
  the aliasing as "chip character" was the alternative; the crusher (4.2) can add it on purpose.
- **Every kind sits about 3 dB under a saw at the same slot level (taken).** Noise sounds louder
  than a saw at equal RMS, and the presets set their own levels anyway.

## D26. Noise runs on across notes (2026-11-13, phase 3.1)

- **Each generator instance gets its own noise sequence at prepare and never reseeds (taken,
  recommended).** Every note is a different burst, as with analogue noise; two voices never
  sum the same noise coherently.
- Restart the noise at each note. Every hit identical, like a sample; fine for a drum machine,
  wrong for breath and wind.

## D27. Phase 3.1 preset choices (2026-11-13, phase 3.1)

- **Tape Flute's breath is band noise on the note, with a 160 ms chiff from ENV3 (taken).** The
  recipe said LP4 noise and "SVF band-pass for the breath"; the band type in the noise
  generator is that band-pass.
- **Solar Wind: two key-independent noise bands drifting on separate slow global LFOs (0.07 and
  0.045 Hz) over two octaves (taken).** One band was the recipe; the second, off-phase, keeps the
  sweep from sounding like one filter going back and forth.
- **Static Hit uses Short mode (taken).** The recipe says "pitched periodic noise"; Long mode
  would be a pitch-coloured hiss instead.
- **Riser's noise layer is wide pink noise whose tone and level the global envelope opens
  (taken).**
- **New bank files `01-bedroom`, `02-arcade` and `04-observatory`, numbered by room order
  (taken).**

## D28. The loudness tool waits for slow swells too (2026-11-13, phase 3.1)

- **When the amplifier's delay, attack and hold exceed half a second, the tool renders through
  them (up to 10 s) before its 4 s measurement (taken, recommended).** Solar Wind (3 s attack)
  is judged at full swell; measured during the swell it came out 3 dB too loud.
- Measure as before. Every slow pad would be pushed above the bank.

Random Walk's measured loudness moves by about 1 dB between runs because its arp pattern is
random, so the tool may nudge its master by 0.5 dB on a rerun; nothing to fix.

## D29. Link errors on the embedded bank library (2026-11-13, phases 3.1 and 3.2)

The embedded bank library failed to link four times (LNK1236, once LNK1106). Every time, CMake
had reconfigured itself in the middle of the build, which a new bank file or a CMakeLists change
triggers. That build can write the library truncated, and later builds update the broken archive
in place, so they keep failing. Deleting `build/Debug/CoolSynthFactoryBank.lib` and building
again always fixed it. (Corrected on the second occurrence: at first I blamed a stale .pdb.)

- **Configure explicitly after adding a bank file (`cmake --preset vs2022-debug`), and delete the
  library if the error appears; both written in TODO's standing notes (taken, recommended).**
- Delete the library before every build with a pre-build step. Never fails, but every build
  would relink the plugin, the standalone and the tests, even with nothing changed.
- Embed the bank in the plugin's own sources instead of a JUCE binary-data library. Removes the
  library, but replaces JUCE's generator with one of our own.

The data-only libraries now also build without debug info, which they never needed.

## D30. The two-operator FM generator (2026-11-13, phase 3.2)

- **A type of its own, "FM2", with the four-operator generator a separate type in 3.3 (taken,
  recommended).** Two operators fit the eight slot parameters with room to spare (ratio, fine,
  index, feedback, carrier ratio, mix); four operators need paging. One FM4 type for both was the
  alternative, but a two-operator patch would carry two unused operators and the paging.
- **Ratio is continuous (0.25 to 16, logarithmic), not stepped to DX ratios (taken).** Integer and
  half ratios are typed exactly in bank files, and a sweep of the ratio through the matrix
  makes the inharmonic glides that stepped ratios cannot.
- **Feedback through the average of the modulator's last two outputs (taken).** Reported online
  (Native Instruments forum, on emulating Yamaha chips; the thread itself calls it unproven) as
  what keeps high feedback from buzzing at half the sample rate. Measured: full feedback leaves
  -72 dB above 40 kHz.
- **Phases restart at a fresh note and run in double precision (taken).** Every attack is the
  same, as on a DX; free-running phases would make each note's attack differ.

## D31. Keeping FM from aliasing (2026-11-13, phase 3.2)

Checked online: CCRMA's FM notes suggest limiting the index to about (fs/2 - carrier) /
modulator; forum advice is to oversample.

- **Limit the index where the first sideband that would fold into the audible band, after the
  voice's decimation, has a Bessel amplitude under 1e-4 (taken, recommended).** Measured: the
  largest alias below 20 kHz -75 dB against the strongest sideband at a 2 kHz carrier with index
  10 asked for (held at 3.0). Low and middle notes are never limited.
- The Carson-rule cap from CCRMA. Tried first: -57.6 dB, since the Bessel tail just past
  Carson's bandwidth still folds back.
- Oversample the FM generator again internally. Clean at any index, but twice the cost and a
  second decimator per voice.
- No limit. DX-style grit at high notes, but uncontrolled.

## D32. Phase 3.2 presets (2026-11-13, phase 3.2)

- **Glass Bell has a second, quieter FM pair (ratio 5 on a carrier at 2, 7 cents sharp) for a
  shimmering high partial (taken).** The recipe's single pair sounded plain against the plate.
- **FM Knock plays mono, so every note knocks; its level and sustain were raised so it reaches
  the loudness target under the +6 dB master cap (taken).**
- **New bank file `05-greenhouse.csbank` with Glass Bell, Rain Chime and Crystal Pluck (taken).**

## D33. The four-operator FM generator (2026-11-13, phase 3.3)

The plan asks for six algorithms with "per-operator level and ratio across P1..P8 with paging".

- **Eight parameters, no paging: algorithm, the ratios of operators 2 to 4, their levels and
  feedback on operator 4; operator 1 is the carrier at the note (taken, recommended).** Paging
  needs UI support the throwaway editor does not have; the slot's own octave, semitone and fine
  move operator 1. Per-operator fine detune is the price; a ratio like 14.02 still detunes one.
- Paging over two pages of eight. More control, but the generic editor would show sixteen
  generic parameters for one slot and the bank syntax would need pages.
- **Six algorithms: stack, two into one, stack plus one, two pairs, one into three, organ
  (taken).** They cover the classic four-operator families (the DX7's 32 six-operator algorithms
  reduce to these shapes on four operators).
- **A Level is the index (1 is 8 radians) where the operator modulates and its output level where
  it sounds; carriers are summed and normalised by their total level (taken).** One knob per
  operator whatever the algorithm; the output never exceeds 1 (tested on all six at full).
- **Every modulation is limited with the two-operator generator's alias rule for the operator it
  drives (taken).**

Grid Keys (Outrun) ships on the Two Pairs algorithm: a 1:1 body with velocity bark and a 1:14
tine that rings for 250 ms.

## D34. The factory wavetables (2026-11-13, phase 3.4)

The plan asks for an embedded factory set: classic, digital, vocal, chip 4-bit.

- **Four tables of 16 frames, computed from their spectra at first use, not stored as audio
  files (taken, recommended).** No third-party tables to license, nothing in the repository but
  code, and every frame is exact. Classic blends sine, triangle, saw and square spectrum by
  spectrum with the fundamental in phase (no thin spot mid-morph); Digital is a hard-synced saw
  whose slave ratio climbs from 1 to 8; Vocal is a saw through the formants of a, e, i, o, u on
  a 110 Hz harmonic grid (the formants move with pitch, as in every wavetable synth); Chip is
  32-step 4-bit waves (pulse widths 1/8 to 1/2, the NES triangle, a stepped saw, four SCC-style
  waves).
- Embedded WAV tables. Richer at once, but they need a source and a licence.
- User table import. Later, with the real UI.

## D35. Band-limited playback (2026-11-13, phase 3.4)

Checked online (KVR, Electric Druid): one band-limited table per octave by FFT, clear, inverse
FFT, 2048 samples, is the common practice; hard switching between octave tables can crackle.

- **11 octave levels per frame; the level is chosen so harmonics stay under 0.75 fs, so what
  folds back lands above fs / 4 and the voice's decimator removes it; a 4-point Hermite read
  (taken, recommended).** At the 96 kHz voice rate a level switch only ever removes harmonics
  above 36 kHz, so switching is inaudible and needs no crossfade. Measured: the largest alias at
  A7 is -118.6 dB.
- Two or four tables per octave, or crossfading levels. More memory or more reads for nothing
  audible here.

## D36. The wavetable generator's parameters (2026-11-13, phase 3.4)

- **Table, Position (crossfading neighbour frames, ramped across each sub-block), Unison 1 to 5,
  Detune, Spread, Phase Rand (taken).**
- **No hard sync input (taken).** A reset without band-limited correction would alias; the VA
  generator does sync properly. Combine FM moves the phase of every copy; ring and AM as usual.
- **The table bank is built in `Part::prepare`, off the audio thread (taken).** Building it the
  first time a slot switches to Wavetable would cost the audio thread about 20 ms once.

## D37. Phase 3.4 presets and test fixes (2026-11-13, phase 3.4)

- **SCC Lead steps its pulse width with the step modulator, eight values that land exactly on
  the chip table's eight pulse-width frames at 1/16 (taken).** An LFO with the Steps shape would
  fall between frames and crossfade. The 4-bit crusher of the recipe waits for 4.2.
- **Nebula Pad layers a vocal stack and a digital stack, each drifting on its own per-voice LFO
  (20 and 27 s a cycle), through the ladder in LP12 until the state-variable filter (4.1) and the
  plate's pre-delay (5.1) arrive (taken).**
- Tests corrected, not the engine: an old bank-error test used "Wavetable" as its example of an
  unknown type; the every-preset-sounds test gives presets whose arp skips steps at random four
  seconds instead of one (Rain Chime once skipped every step of a second); the fuzz test waits
  out each preset's own release instead of a fixed two seconds (Glass Bell releases for 2.5 s).

## D38. The state-variable filter (2026-11-13, phase 4.1)

- **Andy Simper's trapezoidal SVF, 12 dB per octave, LP, HP, band-pass at a peak gain of 1 and
  notch; resonance from Q 0.5 to Q 40, growing exponentially (taken, recommended).** The SVF is
  the clean, precise filter next to the ladder's character: high-passing supersaws, breath and
  wind. At Q 40 it rings long but always decays (tested); self-oscillation stays the ladder's job.
- A self-oscillating SVF (Q to infinity). Duplicates the ladder and needs a level control.
- **A soft limit on the band state, 4 tanh(v / 4), and drive as an input stage like the ladder's
  (taken).** Keeps a fully resonant, fully driven filter bounded (largest output 12 at full-scale
  noise with the cutoff jumping every sub-block) and does nothing at normal levels. A fully
  nonlinear SVF (Oberheim SEM style) was the alternative: more colour, but the clean filter is
  the point of this type.
- 24 dB cascades. The ladder covers four poles.

## D39. Phase 4.1 presets (2026-11-13, phase 4.1)

- **Supersaw's high-pass moves to the SVF at 120 Hz, resonance 0.1, as the recipe says (taken).**
  It also costs less: Supersaw went from 28 % to 20 % of real time.
- **Nebula Pad's soft low-pass moves to the SVF (taken).**
- **Deep Field ships on the ladder, 400 Hz, breathing on a 20-second global LFO (taken).** The
  recipe asks for LP4, which is the ladder.
