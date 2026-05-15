# New UI Design And Implementation Plan

## Purpose

This document defines a focused UI redesign for the current V2 editor in `CoolSynth`.

The immediate goal is not a generic visual refresh. The goal is to stop treating discrete states like rotary controls and replace them with flat, panel-like button controls that read quickly on a dense synth surface.

This plan is anchored to the current codebase:

- The editor is a one-page JUCE panel in `src/plugin/SynthAudioProcessorEditor.h` and `src/plugin/SynthAudioProcessorEditor.cpp`.
- Discrete parameters are currently rendered through `HardwareKnob`, attached through `SliderAttachment`, and laid out as if they were continuous controls.
- Section shells are currently simple dark rounded cards via `src/ui/SynthSection.cpp`.

## Core Diagnosis

The current UI is structurally correct for V2 parameter coverage, but it is visually wrong for the parameter types.

The main failures are:

1. Boolean and few-state controls are rendered as knobs, so state reads too slowly.
2. Critical mode choices like waveform, play mode, key priority, and arp pattern do not look selectable at a glance.
3. The panel uses one control language for everything, which flattens hierarchy and makes the synth feel like a parameter dump.
4. The current dark theme has no strong discrete-state language. It does not visually communicate "inactive vs active" or "one of many."

## Fixed Visual Rule For Discrete Buttons

This rule is now explicit and should be treated as the design contract.

### Single Toggle Buttons

For controls like `Sync`, `Low Freq`, `Arp On`, `Latch`, `Drive On`, `Chorus On`, `Delay On`, and `Reverb On`:

- Shape: hard rectangle, no rounded corners.
- `OFF` state:
  - dark background
  - green rectangle stroke
  - green text
- `ON` state:
  - solid green rectangle fill
  - dark text
- Text inside the button must show the state directly: `OFF` or `ON`.
- The control function name stays outside the button as a static label, so the button body can stay state-driven.

### Grouped Choice Buttons

For controls like waveform select, play mode, key priority, LFO wave, filter key track, arp rate, arp pattern, and arp octave range:

- Each option is a hard rectangle.
- Buttons in the same group touch each other with zero gap.
- Grouped buttons may use a shared outer frame with internal divider lines.
- `OFF` option cells:
  - dark background
  - green stroke
  - green icon and/or green text
- `ON` option cell:
  - solid green fill
  - dark icon and/or dark text
- Selection should be obvious without reading value text below the control.

This is the primary discrete-control language for the redesign.

## Design Goals

1. Continuous controls stay rotary or fader-based.
2. Discrete controls become flat buttons or segmented groups.
3. The panel keeps the one-page instrument feel already required by `DESIGN_V2.md`.
4. The new green state language becomes the main visual accent and replaces the current weak blue-highlight pattern for discrete controls.
5. Off states remain readable. Green must communicate state, not hide it.
6. Icon-only controls are not acceptable unless the icon is unmistakable. In practice, waveform buttons should use icon plus small text.

## Control Taxonomy

### Keep As Knobs Or Fader

These are continuous or large-range parameters and should stay as they are conceptually:

- oscillator octave if kept as 5 discrete values but not promoted to a switch strip
- fine tune
- oscillator levels
- pulse width
- noise
- cutoff
- resonance
- envelope amount
- envelope ADSR
- LFO rate
- modulation depths
- glide
- pitch bend range
- vintage
- pan spread
- velocity amounts
- arp tempo
- arp gate
- drive amount and mix
- chorus rate, depth, mix
- delay time, feedback, mix
- reverb size, damping, mix
- output gain

### Convert To Single Toggle LED Buttons

- `oscASyncEnabled`
- `oscBLowFrequencyMode`
- `arpEnabled`
- `arpLatch`
- `driveEnabled`
- `chorusEnabled`
- `delayEnabled`
- `reverbEnabled`

### Convert To Segmented Or Grouped Choice Buttons

- `oscAWave`
- `oscBWave`
- `lfoWave`
- `playMode`
- `keyPriority`
- `filterKeyTracking`
- `arpRateDivision`
- `arpPattern`
- `arpOctaveRange`

## Recommended Per-Control Treatment

### Oscillator Waveform

This is the most important discrete redesign target.

Current contract:

- `Pulse`
- `Triangle`
- `Saw`

Recommended treatment:

- Use a touching segmented waveform selector per oscillator.
- Default layout: `3 x 1` strip with icon plus short text in each cell.
- Icons:
  - `Pulse`: square-wave glyph
  - `Triangle`: triangle-wave glyph
  - `Saw`: saw ramp glyph

Reason:

- The current parameter contract is single-select, not multi-wave.
- A flush three-cell selector is clearer than a stepped knob and does not waste space.
- If the oscillator model later expands to simultaneous waves, this component can evolve into a multi-select grid without redesigning the whole panel language.

### Boolean Utility Controls In Oscillator And FX Sections

`Sync`, `Low Freq`, and the FX enable states should not spend a full knob slot.

Recommended treatment:

- Use a small labeled block:
  - static caption above: `Sync`, `Low Freq`, `Drive`, `Chorus`, `Delay`, `Reverb`
  - LED-state rectangle below: `OFF` or `ON`

Good optional improvement:

- Move effect `ON/OFF` buttons into each section header strip instead of occupying the first control slot.
- This frees room for sound-shaping knobs and makes the enable state feel more like a section power switch.

This suggestion is worth doing if the header treatment stays visually disciplined. It should be skipped if it complicates the header code more than it helps density.

### Performance Section

Recommended changes:

- `Play Mode`: segmented `Poly | Mono | Unison`
- `Key Priority`: segmented `Last | Low | High`

Reason:

- These are foundational synth behavior choices and should read instantly.
- They are exactly the kind of few-state choice that should never be a knob.

### Filter Section

Recommended change:

- `Key Track`: segmented `Off | Half | Full`

Reason:

- It is a three-state mode, not a dial.
- This also matches the Prophet-style control vocabulary more closely.

### LFO Section

Recommended change:

- `LFO Wave`: segmented icon/text selector for `Saw | Triangle | Square`

Reason:

- The waveform is categorical and benefits from immediate recognition.
- This mirrors the oscillator treatment and helps make the modulation section read like an instrument, not a spreadsheet.

### Arpeggiator Section

Recommended changes:

- `Arp On`: LED toggle button
- `Latch`: LED toggle button
- `Rate`: touching segmented grid, likely `2 x 3`
- `Pattern`: touching segmented row or `2 x 2`, depending fit
- `Octave`: touching `1 | 2 | 3` segmented group

Reason:

- The arp is one of the strongest discrete-state surfaces in V2.
- It should read like a performance tool with modes, not as seven small knobs.

## Extra Suggestions Worth Keeping

These are optional, but they are good enough to keep in the plan.

### Suggestion: Unify Discrete Color Language Across The Whole Panel

The current panel uses blue accents on knobs and purple keyboard overlays in places. The redesigned discrete controls should establish one deliberate accent language:

- continuous controls: restrained neutral + subtle accent
- discrete states: green
- MIDI learn / armed state: keep distinct from green, likely yellow as it is today

This is worth doing because the discrete-state redesign will look half-finished if the old accent system stays dominant.

### Suggestion: Stop Using Value Text As The Primary State Readout For Discrete Controls

For the redesigned buttons, the selected cell or ON fill should be the primary read.

The old bottom value label can stay for consistency and MIDI-learn feedback, but it should become secondary.

This is worth doing because the current UI depends too much on reading tiny value strings like `Saw`, `Poly`, or `On` under a knob that already communicates the wrong interaction model.

### Suggestion: Use Section Density To Create Hierarchy

Not every section needs the same visual rhythm.

Recommended hierarchy:

- Oscillators, Filter, Envelopes, Performance: primary
- LFO, Poly Mod, Arp: secondary but still prominent
- FX and Output: tertiary

This can be achieved with spacing, title treatment, and grouping, not a full layout rewrite.

This is worth doing only if it remains cheap. If it turns into a large layout refactor, skip it for the first pass.

## Visual Specification

### Palette

Introduce explicit UI color tokens instead of hardcoding ad hoc colors.

Recommended starting tokens:

- `panelBlack`
- `panelRaised`
- `panelStroke`
- `ledGreen`
- `ledGreenDim`
- `ledTextOff`
- `ledTextOn`
- `learnYellow`
- `badgeBlue`

Recommended behavior:

- `ledGreen` is saturated and hot enough to feel like a lit synth panel state.
- `ledTextOn` is dark charcoal, not pure black, so filled buttons remain readable without looking dead.
- `ledTextOff` stays the same green as the stroke or slightly brighter.

### Geometry

- Discrete buttons use square-corner rectangles.
- Grouped choice controls use zero horizontal or vertical spacing inside the group.
- Outer section cards may remain slightly rounded, but the internal discrete buttons should stay rectangular.
- Internal dividers should be crisp and minimal.

### Typography

- Button text should be short, uppercase where it improves scan speed.
- State text for toggles should be `ON` / `OFF`.
- Choice labels should be short enough to fit without truncation.

### Waveform Icon Treatment

- Use simple line glyphs, not illustrated icons.
- The icon stroke should invert with the text color between off and on states.
- Add a short label under or beside the icon inside the cell if space allows.
- Do not rely on icon alone.

## Implementation Strategy

### New UI Components

Add dedicated components instead of overloading `HardwareKnob`.

Recommended new files:

- `src/ui/UiPalette.h`
- `src/ui/LedToggleButton.h`
- `src/ui/LedToggleButton.cpp`
- `src/ui/SegmentedChoiceGroup.h`
- `src/ui/SegmentedChoiceGroup.cpp`
- `src/ui/WaveformChoiceGroup.h`
- `src/ui/WaveformChoiceGroup.cpp`

The exact file split can be reduced if needed, but the component responsibilities should stay separate.

### Attachment Strategy

The current editor uses `SliderAttachment` almost everywhere. That should change for discrete controls.

Recommended mapping:

- Bool parameters:
  - use `juce::AudioProcessorValueTreeState::ButtonAttachment`
- Choice parameters:
  - use a small custom attachment that maps parameter index <-> selected segment

Reason:

- Bool parameters already have a native JUCE attachment path.
- Choice groups need direct control over button selection state and should not pretend to be sliders.

### Touching Group Implementation

Use JUCE button edge connectivity for grouped cells.

Relevant JUCE support:

- `juce::Button::setConnectedEdges(...)`
- `juce::Button::ConnectedOnLeft`
- `juce::Button::ConnectedOnRight`
- `juce::Button::ConnectedOnTop`
- `juce::Button::ConnectedOnBottom`

That should be used to keep borders coherent when cells touch.

### MIDI Learn And Context Menu Integration

Discrete controls still need to participate in the existing learn path.

Recommended rule:

- Every button cell that represents a parameter choice must resolve back to the same parameter ID as its parent control group.
- Learn highlighting can be applied at the group container level rather than on each cell individually for choice groups.
- Toggle buttons can keep per-button learn highlighting.

This matters because the current editor already depends on parameter-surface registration and right-click learn actions.

## Editor Refactor Plan

### Phase 1: Infrastructure

1. Add palette tokens.
2. Add LED toggle component.
3. Add segmented choice-group component.
4. Add waveform icon rendering.
5. Add bool and choice attachment support.

Verification:

- Components render correctly in isolation.
- Button state inversion matches the agreed `OFF` and `ON` contract exactly.
- Grouped cells touch with no layout gap.

### Phase 2: High-Value Control Replacement

Replace the most obviously wrong discrete knobs first:

1. oscillator waveforms
2. `Sync`
3. `Low Freq`
4. `Play Mode`
5. `Key Priority`
6. `LFO Wave`
7. `Key Track`

Verification:

- The editor compiles.
- Parameter automation and recall still work.
- Value display refresh stays correct.

### Phase 3: Arp And FX Discrete Pass

Replace:

1. `Arp On`
2. `Latch`
3. `Arp Rate`
4. `Arp Pattern`
5. `Arp Octaves`
6. FX enable controls

Verification:

- Dense lower-deck sections still fit without overlap.
- The arp section becomes faster to read than the current seven-knob row.

### Phase 4: Layout Cleanup

After the new controls exist, adjust section layout where needed.

Likely layout changes:

- Oscillators may need more width because waveform groups are wider than a knob.
- Performance may become shallower and clearer after swapping in segmented choices.
- FX sections may reclaim space if `On/Off` moves into section headers.

Verification:

- No control collisions at the current editor size.
- The one-page panel remains intact.
- The main synth programming path feels faster from init patch.

## Concrete File Touch List

Primary files:

- `src/plugin/SynthAudioProcessorEditor.h`
- `src/plugin/SynthAudioProcessorEditor.cpp`
- `src/ui/SynthSection.cpp`
- new discrete-control component files under `src/ui/`

Possible minor touch points:

- `src/ui/HardwareKnob.h`
- `src/ui/HardwareKnob.cpp`
- `src/ui/HardwareFader.h`
- `src/ui/HardwareFader.cpp`

These should only be touched if palette harmonization or learn-badge consistency requires it.

## Risks

### Risk: Too Many New Custom Components

Mitigation:

- Keep the button family small.
- Build one toggle component and one segmented-group component, then specialize only where needed.

### Risk: Layout Gets Worse Before It Gets Better

Mitigation:

- Replace high-value discrete controls first.
- Re-layout only after seeing real component widths.

### Risk: Waveform Groups Become Too Wide

Mitigation:

- Use a compact icon+text cell design.
- Keep the first implementation to a single-row three-cell selector.

### Risk: Learn-State Visuals Clash With Green Selection Visuals

Mitigation:

- Keep MIDI learn as yellow.
- Apply learn emphasis to the group frame, not the selected-cell fill.

## Acceptance Criteria

The redesign is successful only if all of the following are true:

1. No boolean parameter in the main synth panel is still presented as a rotary knob.
2. All three-state and few-state mode selectors read as buttons or grouped selectors.
3. `OFF` discrete buttons remain readable on a dark background.
4. `ON` discrete buttons are obviously active through green fill plus dark text.
5. Button groups touch with no gaps.
6. Waveform selection is faster to understand than the current value-under-knob approach.
7. The panel still fits the one-page V2 editor without introducing tabs or paging.

## Open Questions To Confirm Before Coding

These do not block the plan, but answering them will tighten the implementation.

1. Should oscillator wave selectors remain `3 x 1`, or do you want a more obviously grid-like `2 x 2` treatment even though the current parameter contract only exposes three single-select wave states?
2. Do you want FX `ON/OFF` toggles embedded in section headers, or kept as first-row controls inside each section?
3. Should the green discrete-state language also replace the current purple keyboard overlays and blue knob accents, or should that be deferred to a second polish pass?

## External References Checked

These sources informed the direction and are consistent with the proposed implementation.

- Sequential Prophet-5/10 official product page: https://sequential.com/classics-reissued/prophet-5-10/
  - Relevant because the panel emphasizes immediate mode selection, oscillator shape controls, low-frequency oscillator-B behavior, key tracking modes, and performance-oriented top-panel controls.
- JUCE `AudioProcessorValueTreeState::ButtonAttachment`: https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState_1_1ButtonAttachment.html
  - Relevant because bool parameters should stop using slider attachments.
- JUCE `Button`: https://docs.juce.com/master/classjuce_1_1Button.html
  - Relevant because touching segmented groups can use button edge connectivity.
- Apple Human Interface Guidelines, Toggles: https://developer.apple.com/design/human-interface-guidelines/toggles
  - Relevant because selected and unselected states need obvious visual distinction and adequate contrast.

## Recommended Next Step

If this plan is accepted, the next work item should be a narrow implementation slice:

1. build the discrete button components,
2. replace oscillator waveform plus performance mode controls first,
3. review the visual result before touching every remaining discrete control.
