# ARP Expansion Plan

Status: Approved spec. All 13 open questions resolved 2026-05-17. Ready for
Phase A.
Owner: synth engine + editor.
Date: 2026-05-17.

## 1. Goal

Expand the arpeggiator from "Up / Down / UpDown / AsPlayed + rate + gate + octave + latch"
into a serious arp / pattern generator without adding a step sequencer.
Confirmed in-scope generators:

- Note-order generators: **Random**, **Random Walk (Brownian)**, **Order modes** (Converge / Diverge / Inside / Outside), **Chord**.
- Rhythm generator: **Euclidean** (Pulses / Steps / Rotation).
- Global modifiers: **Swing**, **Probability (chance)**, **Ratchet**, **Accent**.

Out of scope (explicit user decision):

- 16-step / 8-step user-programmable sequencer.
- Per-step lanes (velocity lane, octave lane, gate lane).
- Phrase preset banks, motif libraries.
- MPE / channel-pressure routing into the arp.

## 2. Design overview

The current arp has one axis: a `Pattern` choice that decides both note order
and which step fires when. The expansion splits that into three orthogonal
layers, applied in this order each tick:

```
[ Rhythm engine ]   decides which beat slots actually fire   (Straight | Euclidean)
        |
        v
[ Note generator ]  decides which held note plays this step  (Up | Down | UpDown | AsPlayed
        |                                                     | Random | RandomWalk
        v                                                     | Converge | Diverge
[ Modifiers ]       transforms the step                       | Inside | Outside | Chord)
                    - Swing (timing)                          
                    - Probability (gate)                      
                    - Ratchet (subdivisions)                  
                    - Accent (velocity)                       
```

This keeps the engine composable: any note generator works with any rhythm
engine and any subset of modifiers. The user touches **one** pattern dropdown
to change melodic feel and **one** rhythm dropdown to change the rhythmic feel;
modifiers stay set across changes.

## 3. UI integration: main band + overlay

No new editor page. Two layers:

### 3.1 Main arp band (always visible, in current footprint)

Most-touched controls only:

- `Enable`
- `Pattern` dropdown (expanded enum — see §4.1)
- `Rate` dropdown (unchanged)
- `Octave` (unchanged)
- `Gate` (unchanged)
- `Swing` (new knob, replaces some whitespace; small)
- `Latch` toggle
- `Advanced…` button (new) — opens overlay

BPM stays inside the overlay; it's standalone-only and already low-touch.

### 3.2 Advanced overlay (opens over the main panel)

Modal-ish floating component (`juce::Component` painted over the editor with
a dimmed backdrop, closed by Esc / click-outside / close button). Three groups:

```
+---------------------------- ARP — Advanced ----------------------------+
| Rhythm                                                                 |
|   [Straight | Euclidean]                                               |
|   Pulses [ 5 ]   Steps [ 8 ]   Rotation [ 0 ]   <- only when Euclidean |
|   [visualizer:  X . X . X . X X . . X . . X . .]                       |
|                                                                        |
| Modifiers                                                              |
|   Chance      [ 100% ]   per-step probability the step fires           |
|   Ratchet     [ Off | x2 | x3 | x4 ]   Chance [ 0% ]                   |
|   Accent every[ Off | 2 | 3 | 4 ]      Amount [ 0% ]                   |
|                                                                        |
| Internal tempo (standalone only)                                       |
|   BPM [ 120.0 ]                                                        |
|                                                                        |
|                                                       [ Close ]        |
+------------------------------------------------------------------------+
```

A small **status strip** at the bottom of the main arp band (`Euc 5/8`,
`Swing 18%`, `Acc /4`) gives one-line visibility of overlay state without
opening it. If everything is at defaults, the strip is empty.

## 4. Parameter contract additions

All names follow the existing camelCase convention in `ParameterIDs.h`.
Per the V2 policy (GEMINI.md: "no backward compatibility required"), the patch
version bumps once for the whole block; old `.cspatch` files load with new
parameters at sensible defaults.

### 4.1 Pattern enum expansion (`ArpPatternChoice`)

Resolved (Q3): reorder enum for logical grouping. Old preset indices do not
survive — per V2 policy this is fine. New indices:

```
// Sweeps
up               = 0
down             = 1
upDown           = 2
asPlayed         = 3
// Order modes
converge         = 4   // NEW   low, high, low+1, high-1, ...
diverge          = 5   // NEW   from center, outward
inside           = 6   // NEW   1, n, 2, n-1, ... (touches endpoints first)
outside          = 7   // NEW   n, 1, n-1, 2, ... (mirror of Inside)
// Random family
random           = 8   // NEW
randomWalk       = 9   // NEW   Brownian: 50% next / 25% prev / 25% repeat
// Polyphonic
chord            = 10  // NEW   all held notes simultaneously
```

Note: factory preset YAMLs and `FactoryPresets.cpp` must be re-emitted in
Phase F since stored `arpPattern: 0..3` references still resolve correctly
(Up..AsPlayed are 0..3), but any future presets that target Random/Walk/etc.
need the new indices.

### 4.2 New parameters

| ID | Type | Range / values | Default |
|---|---|---|---|
| `arpSwing` | float | 0 – 0.75 | 0 |
| `arpChance` | float | 0 – 1 | 1.0 |
| `arpRatchetCount` | choice | Off / 2 / 3 / 4 | Off |
| `arpRatchetChance` | float | 0 – 1 | 0 |
| `arpAccentEvery` | choice | Off / 2 / 3 / 4 | Off |
| `arpAccentAmount` | float | 0 – 1 | 0 |
| `arpRhythm` | choice | `straight` / `euclidean` | straight |
| `arpEuclideanPulses` | int | 1 – 16 | 4 |
| `arpEuclideanSteps` | int | 1 – 16 | 8 |
| `arpEuclideanRotation` | int | 0 – 15 | 0 |

`ArpParametersV2` grows by 10 fields; remains a POD copied into the engine
once per block. No allocations.

## 5. Engine architecture changes

`Arpeggiator` (`src/synth/Arpeggiator.{h,cpp}`) keeps its current shape but
gains three internal pieces:

### 5.1 PRNG

A single per-instance `juce::Random` (seeded once at `prepare()` time from
`juce::Time::getHighResolutionTicks()`) drives Random / Random Walk / Chance /
Ratchet roll. Seed is **not** part of the patch — random patterns are not
reproducible across reloads, matching every other hardware/soft arp.

For test determinism, expose a `setSeedForTesting(uint64_t)` accessor.

### 5.2 Note-generator state

`pickNextPatternNote()` already takes the working set (held or latched). It
gains four new cases:

- **Random**: pick `rng.nextInt(workingSetSize)`, then random `octaveShift`
  if `octaveRange > 1`.
- **RandomWalk**: keep `int walkIndex` and `int walkOctave` across calls.
  Rolls: 50% advance, 25% retreat, 25% stay. Bounce off both ends of the
  combined `workingSetSize * octaveRange` range. On panic, reset to 0.
- **Converge / Diverge / Inside / Outside**: deterministic index permutations
  computed from `patternStepCounter` modulo `totalSweepLength`. See §6.3 for
  formulas.
- **Chord**: returns a sentinel; caller iterates the working set and emits N
  simultaneous noteOns (see §5.3).

### 5.3 Chord emission

Chord mode does **not** fit the existing "one note per step" path. In
`generateEventsForBlock`, when pattern is `chord`:

- Emit one `noteOn` per held note (with shared `sampleOffset`, full velocity
  apart from accent — see §5.4).
- Schedule one ringing entry per held note (or emit noteOff in-block if it
  fits).
- Octave range still applies: at step k, the chord is transposed by
  `(k / 1) % octaveRange * 12` semitones — i.e. each step is one chord at a
  different octave, cycling.

Buffer math: `maxArpEventsPerBlock = 64` already; one chord step needs
`2 * heldNoteCount` events (on + off). With 16 held notes that's 32 — fits.
We add a guard: if the chord won't fit in the remaining `maxEvents`, the
chord step is dropped (preserves invariant: never half-emit a chord).

### 5.4 Modifier pipeline

Each generated step passes through:

1. **Swing** — odd steps (0-indexed: steps 1, 3, 5, …) get their `sampleOffset`
   delayed by `swingAmount * (stepLengthSamples / 2)`. Note: even when host-
   synced, swing applies to the rendered offset, never to the underlying
   PPQ math (so it doesn't drift relative to the bar). When `arpRhythm =
   euclidean`, swing applies to alternating *active* pulses, not alternating
   slots, so the groove follows the rhythm.
2. **Chance** — `if (rng.nextFloat() >= arpChance) skip step`. Pattern step
   counter still advances so the melodic pattern doesn't stall.
3. **Ratchet** — if `arpRatchetCount != Off` and `rng.nextFloat() <
   arpRatchetChance`: emit the same note `count` times at sub-step offsets
   `0, L/count, 2L/count, …` (where `L = stepLengthSamples * gateFraction`).
   Each sub-hit gets its own ringing entry. Skipped if buffer would overflow.
4. **Accent** — if `arpAccentEvery != Off` and `(patternStepCounter %
   accentN == 0)`: scale velocity by `1 + arpAccentAmount`, clamped to 1.0.
   Accent counts on emitted steps (post-chance), not raw pattern advances,
   so chance doesn't desync the accent grid.

### 5.5 Euclidean rhythm engine

When `arpRhythm = euclidean`:

- `Pulses`, `Steps`, `Rotation` define a fixed-length boolean cycle of length
  `Steps`. Bjorklund (see §6.1) computes this when any of the three params
  change; result is cached in `std::array<bool, 16> euclideanCycle` on the
  `Arpeggiator`.
- The step length comes from `Rate` exactly as before. The arp ticks once per
  "step slot"; whether a slot is a pulse (fires) or a rest is determined by
  `euclideanCycle[(euclideanPosition) % steps]`.
- On each tick: increment `euclideanPosition`. If the slot is a rest, advance
  but emit nothing (the note-generator's `patternStepCounter` is **not**
  advanced — so the melodic walk only progresses on actual pulses). If the
  slot is a pulse, run the full pipeline (note-gen → modifiers).
- Host-sync alignment: cycle resets to position 0 at bar starts when host-
  synced. Internal clock: cycle resets only on panic / enable transition.

## 6. Algorithm notes

### 6.1 Bjorklund (Euclidean)

Standard iterative Bjorklund. Pseudocode:

```
function euclidean(pulses, steps, rotation):
    if pulses >= steps: return [true] * steps
    if pulses <= 0:     return [false] * steps
    pattern = [[true]] * pulses + [[false]] * (steps - pulses)
    while True:
        countA = number of leading [[true]]-headed groups
        countB = number of trailing [[false]]-headed groups
        if min(countA, countB) <= 1: break
        merge: pair each of the first min groups with one trailing group
    flatten pattern -> length steps
    rotate left by `rotation`
    return
```

Implementation: `std::array<bool, 16>` work-buffer, no heap. Done once per
parameter change on the message thread; `Arpeggiator::setParameters()`
already runs there (it's called per-block but the inputs only change on user
gestures).

### 6.2 Random Walk

```
walkIndex starts at 0 on enable / panic / pattern change.
each step:
  roll = rng.nextFloat()
  if   roll < 0.50:  walkIndex += 1
  elif roll < 0.75:  walkIndex -= 1
  else:              (repeat)
  clamp:    bounce at [0, totalSweepLength - 1] (reflect, not wrap)
  yield (walkIndex / notesPerOctaveSweep) octave shift,
        (walkIndex % notesPerOctaveSweep) into sorted held set
```

Bounce (not wrap) keeps motion natural. Resets on note-set change too, so
adding/removing a held note doesn't strand the walker outside the new range.

### 6.3 Order-mode index formulas

Given sorted held set of size N and octaveRange O, total sweep T = N * O.
Let s = patternStepCounter % T_eff (T_eff depends on mode).

- **Converge** (low, high, low+1, high-1, …): T_eff = T.
  `pair = s / 2; flip = s & 1; idx = flip ? T - 1 - pair : pair`.
- **Diverge** (center outward): T_eff = T.
  `mid = T / 2; pair = s / 2; flip = s & 1;`
  `idx = flip ? mid - 1 - pair : mid + pair` (clamp into [0, T-1]).
- **Inside** (1, N, 2, N-1, … per octave, then octave++): T_eff = T.
  Inside-octave: as Converge but on N rather than T; octaveShift = s / N.
- **Outside**: Inside reversed: index = N - 1 - (Inside index).

Edge cases: N == 1 → all four collapse to repeating that one note; degenerate
but correct.

## 7. Phased implementation plan

Goal of phasing: each phase is shippable on its own, ordered by
biggest-payoff-per-line-of-code.

### Phase A — Note-order generators (highest payoff, lowest risk)

- Extend `ArpPatternChoice` with the seven new values (§4.1).
- Implement Random, RandomWalk, Converge, Diverge, Inside, Outside, Chord in
  `pickNextPatternNote()` and (for Chord) in `generateEventsForBlock()`.
- Add `juce::Random` member and `setSeedForTesting()`.
- Update `ArpParametersV2` only by extending the enum mapping in the layout
  and processor decode.
- Extend Pattern dropdown UI in the editor — string entries only, no new
  layout.
- Patch version bump (V2 → V3 inside the `.cspatch` wrapper, since enum
  indices for old patterns 0..3 are unchanged this is "additive").

Acceptance:

- `V2Arpeggiator` tests cover each new pattern: empty held set behavior,
  single-note behavior, deterministic-under-seed behavior for Random/Walk,
  index formulas for Converge/Diverge/Inside/Outside, multi-note chord
  emission and ringing-set integrity.
- Manual: each pattern audibly distinct on a 4-note held chord, standalone +
  Ableton Live Lite.
- No new allocations on the audio thread (assert in `generateEventsForBlock`
  via existing test allocator hooks if present, otherwise visual review).

### Phase B — Swing + Chance (global timing/gate modifiers)

- Add `arpSwing`, `arpChance` parameters with layout + APVTS attachments.
- Apply in `generateEventsForBlock`: swing as offset-on-odd-steps, chance as
  step-skip post-pattern-pick.
- Add two small knobs to the main arp band (replace whitespace).

Acceptance: `V2ArpeggiatorModifiers` tests for swing offset math under
internal clock and host-sync, and for chance skipping with a seeded RNG
(expect known skip count over 1000 steps at 50%).

### Phase C — Ratchet + Accent

- Add `arpRatchetCount`, `arpRatchetChance`, `arpAccentEvery`,
  `arpAccentAmount` parameters.
- Engine: sub-step emission for ratchet (chord-mode aware: ratchet a single
  step's worth, so chord + ratchet = repeated chord stab); velocity scaling
  for accent.
- Live in the Advanced overlay (Phase E gates main visibility; Phase C is
  backed by knobs in the layout that the editor can place anywhere — we just
  add a temporary placement until E lands).

Acceptance: ratchet emits exactly `count` sub-events at correct offsets;
accent boost matches expected curve; combination tests against chord mode.

### Phase D — Euclidean rhythm

- Add `arpRhythm`, `arpEuclideanPulses`, `arpEuclideanSteps`,
  `arpEuclideanRotation`.
- Implement Bjorklund in a free function in `Arpeggiator.cpp` (anonymous
  namespace), cache result in the `Arpeggiator`.
- Integrate with the step loop: rests advance the Euclidean position but
  not the pattern step counter.

Acceptance:

- Unit tests for Bjorklund on known inputs (e.g. `(3, 8) → 10010010`,
  `(5, 8) → 10110110`, `(7, 12) → 110110101101` after standard rotation).
- Integration test: `pulses = 3, steps = 8, rotation = 0`, pattern = Up,
  N = 4 held notes → first 8 steps fire on slots 0, 3, 6 only, and the
  Up pattern walks one held note per pulse (not per slot).
- Host-sync test: cycle resets to slot 0 at bar boundary.

### Phase E — Advanced overlay UI

- New component `ArpAdvancedOverlay` (`src/plugin/ArpAdvancedOverlay.{h,cpp}`).
- Painted over the editor, dismissible. Owns its own attachments to the
  shared APVTS — no duplicate state.
- Small visualizer pane for the Euclidean cycle (`std::array<bool, 16>`
  rendered as dots).
- "Advanced…" button in main band toggles overlay.
- Status strip in main band shows summary of non-default overlay values.

Acceptance: overlay shows/hides cleanly; resizes with editor; all
attachments round-trip; no rendering glitches when toggling Straight ↔
Euclidean inside the overlay; tooltip strings present.

### Phase F — Patch format + factory presets

- Bump wrapped `.cspatch` version from 2 → 3 (it already grew once; this is
  a clean V2-policy break).
- Update `coolsynth_30_presets_with_scifi_additions.yaml`, the inject /
  splice scripts, and `FactoryPresets.cpp` to supply defaults for the new
  fields.
- Add at least three factory patches showcasing new modes:
  - "Random Walk Pluck" — RandomWalk + 1/16 + short gate + chorus.
  - "Trance Gate" — Chord + 1/16 + chance 100% + accent /4.
  - "Tresillo Bass" — Up + Euclidean 3/8 + 1/16T + drive.

Acceptance: factory presets round-trip; sound check matches preset name.

## 8. Test coverage strategy

New test suite `V2ArpeggiatorExpansion` (lives next to `V2Arpeggiator` in
`tests/`), grouped by phase. Patterns:

- **Determinism**: every random-flavored test seeds the arp via
  `setSeedForTesting()` and asserts an exact sequence over ≥ 100 steps.
- **Boundedness**: every test with a 256-sample block asserts at most
  `maxArpEventsPerBlock` events out, and asserts no ringing leak after
  panic.
- **Combinations**: at least one test per (Note generator × Rhythm engine ×
  Modifier) cell that's musically interesting; e.g. Chord + Euclidean +
  Accent, RandomWalk + Swing, Outside + Ratchet.

Existing `V2Arpeggiator` tests stay green; they exercise the four original
patterns and existing behavior.

## 9. Risks and tradeoffs

- **Patch size growth**: 10 new params × ~12 bytes each ≈ 120 bytes per
  patch. Negligible.
- **CPU**: all generators are O(N) in the held set per step; Euclidean
  cycle is cached. No measurable hit expected even at 1/32 rate.
- **UI complexity**: Pattern dropdown grows from 4 entries to 11. Long but
  one column; we should keep the existing names exactly to avoid breaking
  factory presets that store an index.
- **Random reproducibility**: random patterns don't reload identically.
  This matches every commercial arp; documented in the parameter tooltip.
- **Host-sync × swing**: groove will shift the rendered offsets relative to
  the bar grid. This is intentional and matches Ableton's groove pool, but
  is worth a tooltip note.
- **Chord mode + voice stealing**: pressing 16 keys in chord mode at fast
  rates will hit the V2 allocator's release-first stealing. Already tested,
  but worth a smoke pass.

## 10. Resolved decisions (2026-05-17)

All 13 open questions were resolved before drafting started. Decisions are
recorded here as the design record.

### Pre-A
1. **Chord mode + octaveRange** → **Cycle chord octaves per step.** Step k
   plays the chord transposed by `(k % octaveRange) * 12`. `octaveRange = 2`
   alternates `chord, chord+12, chord, chord+12, …`.
2. **Random/Walk reset on held-set change while latched** → **Walker resets,
   RNG state continues.** Walker index/octave returns to 0 so it can't be
   stranded; the underlying `juce::Random` keeps its sequence.
3. **Pattern enum ordering** → **Reorder for logical grouping.** Sweeps
   (0–3) → Order modes (4–7) → Random family (8–9) → Chord (10). The four
   existing patterns keep indices 0–3 by coincidence; new patterns get
   numerically grouped slots even though old preset references happen to
   stay valid.

### Pre-B
4. **Swing under host-sync** → **Rate-relative.** Every other arp step gets
   delayed regardless of `Rate`. 1/16 → 16th-note swing, 1/8 → 8th-note
   swing.
5. **Chance when pattern is Chord** → **Per-step (whole chord rolls one
   die).** If the roll fails, the entire chord step is skipped.

### Pre-C
6. **Ratchet on Chord** → **Re-trigger the whole chord N times** within the
   step. Classic stutter; cheap on the event buffer (N × `heldNoteCount`
   events, with the same overflow-drop rule as plain chord steps).
7. **Accent on Chord** → **Boost all chord notes uniformly.** Accent step
   scales every emitted note's velocity by `1 + arpAccentAmount`.

### Pre-D
8. **Euclidean step length unit** → **One Rate division.** A Euclidean slot
   = one Rate tick. `Pulses 3 / Steps 16 @ 1/16` = exactly one bar; the same
   shape `@ 1/8` is exactly two bars.
9. **Euclidean rotation on note-set change** → **Hold absolute position.**
   The cycle position counter is independent of the held set; same slot
   fires regardless of which notes are pressed.
10. **Cycle reset under host-sync** → **Reset at bar starts.** Position 0
    lands on every bar 1. Internal-clock cycles reset only on panic / enable
    transition, as in §5.5.

### Pre-E
11. **Overlay dismissal** → **Esc + close button only.** No click-outside —
    protects against accidental dismissal during knob drags that leave the
    overlay rectangle.
12. **Overlay rendering** → **Full-editor dimmed overlay, centered.** Modal
    feel; the rest of the editor is non-interactive while open.

### Pre-F
13. **Factory preset coverage** → **Dedicated "Arp" bank (10+ patches).**
    Full curated bank in Phase F covering every new pattern × major modifier
    combination. Concrete patches TBD in Phase F itself, but the bank should
    include at minimum: RandomWalk Pluck, RandomWalk Pad, Trance Gate
    (Chord), Stuttered Chord (Chord + Ratchet), Tresillo Bass (Euclidean
    3/8), Cinquillo Lead (Euclidean 5/8), Polymeter Stab (Euclidean 5/7),
    Converge Bell, Outside Plucker, Diverge Sweep. Bank curation is part of
    the Phase F deliverable and acceptance gate.

## 11. Implications of the resolved decisions

A few behaviors that fall out of the decisions above and are worth noting
before implementation:

- **Per-step probability uses one die for Chord, but one die per sub-hit for
  Ratchet.** Decision 5 says chord chance is per-step; decision 6 says
  ratchet repeats the whole chord; the design implication is that chance is
  evaluated **once** before ratchet expansion, so a successful chance roll
  followed by ratchet x3 emits three chord stabs (not three independent
  rolls). Document this in `arpChance`'s tooltip.
- **Accent grid counts emitted steps, not raw pattern advances** (already in
  §5.4). Combined with decision 5, a skipped chord step does **not**
  contribute to the accent counter — so `accentEvery = 4` always lands on
  every 4th *audible* step, regardless of chance.
- **Euclidean × Rate together can produce long cycles.** `Steps 16 @ 1/32` =
  half a bar; `Steps 16 @ 1/4` = four bars. The overlay status strip should
  show the cycle length in bars so users aren't surprised.
- **Patch break is hard, not soft.** Decision 3 reorders the pattern enum.
  Although indices 0–3 are coincidentally stable, we should not rely on
  that; Phase F bumps the wrapped patch version to 3 and re-emits factory
  presets explicitly.

## 12. Suggested phase order

Recommended ship order if each phase is gated for review:

`A → B → C → D → E → F`

A and B together give the biggest sonic upgrade for the smallest UI surface.
C lands ratchet/accent which need the overlay (E) to be usable for non-power
users, so C's UI is temporary until E. D is a big-payoff feature but has the
most algorithm work; landing it after the overlay (E) is finished avoids
revisiting overlay code twice.

Alternative: swap D and E if Euclidean is the headline feature — in that
case, ship D with temporary main-band knobs and consolidate into the overlay
in E.

---

Resolved decisions are tracked back into this file as each Open Question is
answered. When a phase lands in `DONE.md`, the corresponding section of this
plan stays as the design record; do not delete it.
