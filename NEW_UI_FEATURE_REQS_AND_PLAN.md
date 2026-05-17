# New UI + Effects — Requirements & Plan

Status: draft for approval. Date: 2026-05-17.

## 1. Scope (confirmed)

### New effects
| Effect      | Type                                | Status today                            | This plan              |
|-------------|-------------------------------------|-----------------------------------------|------------------------|
| Timbre      | Performance macro (no new DSP)      | Not present                             | Add as macro           |
| Excite      | Performance macro (no new DSP)      | Not present                             | Add as macro           |
| Distortion  | Rename of existing Drive            | Present as `Drive` (tanh waveshaping)   | Relabel only           |
| Phaser      | New global FX                       | Not present                             | New DSP + UI module    |
| Compressor  | New global FX                       | Not present                             | New DSP + UI module    |

### Dropped from earlier list (no work)
- `blend` — Mixer already exposes Osc A / Osc B / Noise.
- `detune` — `oscBFineCents` + per-voice `vintageAmount` already cover this.
- `body` — out of scope for now.
- `decay` — already exposed via `ampDecayMs`, `filterDecayMs`, `reverbSize`.

### UI rework
- The LED row stops being a "collapsed" form of the keyboard. It becomes an **alternate view** at the same fixed row height as the keyboard view. **LED is the default view.**
- The LED bar's width matches the keyboard's total width (so they swap in the same footprint).
- The current `LED` arrow-up button becomes a **`KEYS` toggle**. It is visible in both views.
- The `OCTAVE` label becomes `OCT`. The `OCT−` and `OCT+` buttons become tiny and stack vertically directly under the `KEYS` toggle. All three control elements are visible in both views.
- The horizontal real estate freed by fixing the keyboard at one footprint (and never expanding) hosts the new compact effect modules (Timbre macro, Excite macro, Phaser, Compressor).

---

## 2. FX rack signal chain

Confirmed order (Drive is the renamed Distortion stage):

```
Voices → Distortion → Phaser → Chorus → Delay → Reverb → Compressor → Master Gain
```

Notes:
- Compressor sits last (mastering-style glue/limit), before master gain.
- Phaser between Distortion and Chorus is the conventional placement.
- `GlobalFxRack::process()` becomes the single owner of the new stages.

---

## 3. Effect specs

### 3.1 Timbre (macro)
Single knob, no enable toggle, default centred. Acts as a snapshot modulator over multiple existing parameters. Suggested mapping (signed, centred = no effect):

| Source value | Routed to                                                  |
|--------------|------------------------------------------------------------|
| −1 → 0       | `filterCutoffHz` down ~−60%, `oscAPulseWidth` toward 0.10   |
| 0 → +1       | `filterCutoffHz` up ~+60%, `oscAPulseWidth` toward 0.90, `driveAmount` (now distortionAmount) up ~+30% |

Implementation: applied inside `makeBlockRenderParameters()` as a post-step that scales the already-resolved values. Macro itself is **one normalized parameter** (`timbre`, range [-1, +1], default 0). It does **not** drive the underlying parameter values via setValueNotifyingHost — that would cause flicker on the original knobs. The macro is read each block and folded into the `BlockRenderParametersV2` snapshot only.

### 3.2 Excite (macro)
Single knob, no toggle, default 0. Treated as a transient HF emphasis macro. Mapping:

| Source value | Routed to                                                      |
|--------------|----------------------------------------------------------------|
| 0 → +1       | Shorten `ampAttackMs` toward 1 ms, boost `filterEnvAmount` by up to +0.5, raise `velocityToFilter` by up to +0.3 |

Same "fold into block snapshot" approach as Timbre. No automation feedback into the source knobs.

> Open: Excite could also gate a one-shot HF noise burst per note-on (a true "exciter" feel). That's real DSP and out of scope for this pass; the macro version ships first.

### 3.3 Distortion (rename of Drive)
- Parameter IDs **stay the same** (`driveEnabled`, `driveAmount`, `driveMix`) so existing presets and the APVTS state load unchanged.
- Only the section label, knob labels, and tooltips change. Section title: `Distortion`.
- Internal C++ types and namespaces (`SynthAudioProcessor::ParameterRefs::drvOn` etc.) stay named after `drv` for now — renaming them is mechanical and can be a follow-up commit. (Avoids one giant noisy PR.)

### 3.4 Phaser (new DSP)
- 4 all-pass stages (classic MXR Phase 90 feel) modulated by a sine LFO.
- Stereo: two LFOs 90° out of phase per channel.
- Parameters (compact, fits "toggle + 2 knobs"):
  - `phaserEnabled` (bool, default off)
  - `phaserRateHz` (0.05 – 8 Hz, log, default 0.5)
  - `phaserDepth` (0 – 1, default 0.6) — combines sweep depth and wet/dry mix into one knob.
- Internal feedback fixed at ~0.35 (no knob).
- Lives in `GlobalFxRack`. Add `PhaserParametersV2` to `SynthParameters.h`.

### 3.5 Compressor (new DSP)
- Feed-forward, peak-detection, with internal soft knee. Auto-makeup.
- Stereo-linked (single envelope for both channels).
- Parameters (compact):
  - `compressorEnabled` (bool, default off)
  - `compressorAmount` (0 – 1, default 0.3) — drives both threshold (−0 dB at 0, −24 dB at 1) and ratio (1:1 at 0, 6:1 at 1) along a fixed curve.
  - `compressorMix` (0 – 1, default 1.0) — parallel-comp dry/wet.
- Attack/release/knee fixed internally (e.g. 5 ms / 100 ms / 6 dB) until evidence we need them as knobs.
- Lives in `GlobalFxRack`, last stage before `applyMasterGain`.

---

## 4. UI layout

### 4.1 New bottom row (replaces current PianoBar row)

Fixed height ~120 px (keyboard size). Always-on layout, no expand/collapse:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ┌────┐                                                  ┌──────┬──────┬─────┐│
│ │KEYS│ ┌────────────────────────────────────────────┐   │TIMBRE│EXCITE│PHA▾ ││
│ ├────┤ │                                            │   │  ◯   │  ◯   │◯ ◯  ││
│ │OCT │ │       LED bar  OR  keyboard (toggled)      │   ├──────┴──────┤  ▾  ││
│ │  - │ │       (same width in both views)           │   │ COMPRESSOR  │     ││
│ │  + │ │                                            │   │  ▾   ◯   ◯  │     ││
│ └────┘ └────────────────────────────────────────────┘   └─────────────┴─────┘│
└──────────────────────────────────────────────────────────────────────────────┘
  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  left column (~28 px)         centre: LED/keys                   right: new FX
```

Left column (always visible, ~28 px wide, fixed):
- `KEYS` toggle (top, ~20 px tall). Latched-style; when latched, keyboard is shown.
- `OCT` static label (~12 px tall).
- `−` button (tiny, ~14 px square).
- `+` button (tiny, ~14 px square).

Centre: the LED bar OR `juce::MidiKeyboardComponent`, occupying the keyboard's natural total width. The currently-hidden one is `setVisible(false)`. No resize cascade triggered when toggling.

Right: a sub-row hosting the new effect modules, compact (toggle + 2 knobs per FX). Order: **Timbre, Excite, Phaser, Compressor**. Macros (Timbre, Excite) render as a single knob with no toggle. Phaser and Compressor render as a compact section with the existing `LedToggleButton` in the header and two `HardwareKnob`s underneath.

### 4.2 What changes in `PianoBarComponent`
- Drop the `collapsed` boolean as the source of visibility. Replace with `viewMode { ledRow, keys }`.
- `getDesiredHeight()` returns a single fixed value (`120`) regardless of mode — no more 24/120 switch.
- Buttons and label move out of the conditional `if (!collapsed)` block in `resized()` and are sized in the always-visible left column.
- The LED-bar `paint()` path is updated to clip its drawing to the keyboard's width (not the component's full width) so the LED dots stay aligned with what the keyboard would show.
- Rename `ledModeButton` → `keysToggleButton`. Update `applyGreenActionButtonStyle` componentID (`"ledModeButton"` → `"keysToggleButton"`) and update the look-and-feel custom icon path in `ActionButtonLookAndFeel.h` so the `KEYS` glyph is drawn when in LED-row mode and a "back to LED" glyph is drawn when in keys mode (or just render the text `KEYS` — simpler).

### 4.3 What changes in `SynthAudioProcessorEditor::resized()`
- The PianoBar still occupies one bottom strip, but the strip is also home to the new FX cluster to its right.
- Easiest layout: `pianoBar.setBounds(area.removeFromLeft(pianoBarWidth))`, then place the FX modules in the remaining strip. `pianoBarWidth` = left-column-width + keyboard-total-width + small gap.
- Add four new `SynthSection`s (or reuse the pattern): `timbreSection`, `exciteSection` (each just hosts one knob and a tiny label), `phaserSection`, `compressorSection`.
- New `HardwareKnob`s and `LedToggleButton`s for the new params; new `SliderAttachment`/`ButtonAttachment`s.

---

## 5. Parameter additions

Add to `ParameterIDs.h`:
```cpp
inline constexpr char timbre[]            = "timbre";
inline constexpr char excite[]            = "excite";
inline constexpr char phaserEnabled[]     = "phaserEnabled";
inline constexpr char phaserRateHz[]      = "phaserRateHz";
inline constexpr char phaserDepth[]       = "phaserDepth";
inline constexpr char compressorEnabled[] = "compressorEnabled";
inline constexpr char compressorAmount[]  = "compressorAmount";
inline constexpr char compressorMix[]     = "compressorMix";
```

Append to `allParameterIds` (8 new entries). Increment `parameterVersionHint` to 3 and add migration handling so v2 presets still load (the new params take defaults). Update `bindParameterPointers` bindings in `SynthParameters.h` table.

`Drive` parameter IDs remain `driveEnabled` / `driveAmount` / `driveMix` for back-compat.

---

## 6. Implementation phases (PR-sized)

Each phase is independently shippable and testable.

1. **Phase A — Drive→Distortion relabel** (smallest PR; flushes UI churn out of the way)
   - Section title, tooltips, header label, preset display name.
   - No parameter ID changes.
   - No tests should break.

2. **Phase B — UI rework: PianoBar fixed-height with KEYS toggle**
   - Refactor `PianoBarComponent` to `viewMode`.
   - Rename `ledModeButton` → `keysToggleButton`, update LookAndFeel icon.
   - Adjust `SynthAudioProcessorEditor::resized()` so the bottom row no longer resizes.
   - Add a placeholder rectangle on the right side where Phase C/D will land. No new params yet.
   - Manual test: toggle view, octave shift, both views show buttons.

3. **Phase C — Macros: Timbre + Excite**
   - Add params, attachments, UI.
   - Fold into `makeBlockRenderParameters()` snapshot.
   - Tests: snapshot values vs. macro endpoints; non-default macro never overshoots parameter ranges.

4. **Phase D — Phaser DSP + UI**
   - `Phaser.h/.cpp` (4-stage all-pass + LFO).
   - Integrate into `GlobalFxRack::process()` between Distortion and Chorus.
   - Add params, attachments, UI; update `BlockRenderParametersV2` and `estimateTailLengthSeconds` (phaser tail = 0).
   - Tests: bypass-when-disabled equality with input; null-mix sanity; CPU smoke test.

5. **Phase E — Compressor DSP + UI**
   - `Compressor.h/.cpp` (feed-forward peak, stereo-linked).
   - Last stage before master gain.
   - Add params, attachments, UI.
   - Tests: bypass equality; gain reduction monotonicity vs. input level.

---

## 7. Open questions / risks

1. **Macro feel vs. real DSP**. The macro versions of Timbre and Excite will not feel as "physical" as an MicroFreak engine. If, after Phase C, the feel is too tame, the Hybrid path (real DSP behind same param IDs) stays open without breaking presets.
2. **Compressor knee/attack defaults**. Fixed internals are a guess. If they don't suit factory presets, we expose them in a later phase as advanced controls.
3. **Phaser CPU cost** at low buffer sizes is negligible (4 single-pole APFs per channel) — no threading needed.
4. **Parameter version bump** — confirm the existing version-check logic in `setStateInformation` will accept v3 state files written by this build and degrade-gracefully for older v2 files (today it returns early on a mismatch — that needs softening to "accept v2, apply defaults for v3 additions").
5. **Bottom row width budget**. If the keyboard is wider than expected on smaller window sizes, the right-hand FX cluster shrinks. Worst case we wrap the compressor under the phaser, or fall back to a single column. Will know once Phase B is in.

---

## 8. Out of scope (explicit)

- `blend`, `detune`, `body`, `decay` (covered or deferred).
- Real physical-modeling oscillator engine.
- Per-effect routing changes (no parallel buses, no sidechain).
- Renaming `drv*` C++ identifiers — separate mechanical commit later.
- New factory presets that use the new params — can be added after Phase E.
