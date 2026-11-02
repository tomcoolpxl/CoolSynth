# CoolSynth UI/UX Review

Date: 2026-11-02. Reviewed at commit `6463892` (working tree), Debug standalone build run on a 3840x2160 display at 175% scaling. Plugin mode was reviewed from code only; it shares the editor, so every finding applies to both targets unless noted.

**Status (2026-11-02, later the same day):** C1, C2 and C3 are fixed in the working tree. Deck 2 is now 240 px with named arpeggiator rows and a `static_assert` that the rows fit; the Rate group is a 2 x 3 grid, Octave a horizontal 1 x 3 strip, and Gate, Swing, Latch and the summary strip have real bounds. The Advanced overlay derives its section heights from the controls' minimum sizes and places the Euclidean knobs beside the Rhythm selector. The eight missing bottom-row readouts were added. A regression test, `editor_gives_every_visible_control_a_usable_size`, walks every visible slider, button and combo box in the main panel and in the overlay (Euclidean mode) and fails on anything under 16 x 16 px. Verified by the test suite, a capture of the rebuilt standalone, and offscreen renders of the overlay in plugin and standalone layouts. The standalone window is now 1550 x 766, which makes C4 (resizability) more pressing, not less. Everything else in this document is still open.

**Status (2026-11-02, second pass):** Priority recommendations 2 to 5 are implemented. C4: the editor is user-resizable between 50% and 200% with a fixed aspect ratio; every control lives in a content panel at the fixed design size that the editor scales with an `AffineTransform`, so no layout code changed. Zoom is also available from a right-click menu on any non-control area, from Ctrl+plus / Ctrl+minus / Ctrl+0, and the standalone remembers the last scale and never opens larger than the primary display. Keyboard and screen reader: knobs and faders take keyboard focus (arrow keys change the value), every custom paint path draws a focus ring, sliders, toggles, segment cells, sections and the icon-only buttons carry accessible titles and help text, and the Advanced overlay contains Tab traversal. Labels: widget titles are 13 px, `Resonance`, `Pulse W`, `MW->Dep`, `PB Range`, `Pan Spread` and `Rat Ch` are shortened, icon cells narrower than 32 px or shorter than 44 px draw the icon alone (the word stays in the tooltip and value label), the LFO wave group is 2 x 2, and the oscillator rows carry `A` / `B` tags in the section inset. Undo and patch identity: every UI gesture and every bulk edit (Init, preset, Load) is one undo step held by the processor, with Undo / Redo buttons in the title row and Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z; the title row shows the current patch name with a `*` when edited; the preset selector is grouped by category with prev / next steppers and clears whenever the patch is edited; processor state format is now 8 and carries the patch name and dirty flag. Still open: O5 target sizes, the non-text contrast failures, themed popups and dialogs, output meter and compressor gain-reduction, knob precision, visual hierarchy, MIDI-learn discoverability, build info in the status bar and the README screenshot.

Method: design critique (first impression, usability, hierarchy, consistency) plus a WCAG 2.2 AA accessibility audit. Evidence comes from the source in `src/plugin`, `src/ui`, `src/standalone`, from live screenshots of the main panel, tooltip, right-click menu, Tab focus, KEYS view, Advanced overlay and Settings dialog, and from contrast ratios computed from `src/ui/UiPalette.h`. All pixel sizes below are logical pixels at the fixed 1550x666 window.

## Overall impression

The panel reads as an instrument. The green LED language from `NEW_UI_DESIGN_AND_IMPLEMENTATION_PLAN.md` is implemented consistently, the seven-pane visualizer gives the top row life, every knob has a live readout, and the tooltip copy is unusually good. The biggest problem is not aesthetic: the layout engine silently collapses controls to zero size wherever a row is too short, and nothing in the test suite notices. Today the arpeggiator section and the entire Advanced overlay are decorative. Fixing that, giving the bottom-row FX knobs real readouts, and making the window resizable would move the UI from "looks finished" to "is finished".

## Critical defects (blocking)

| # | Finding | Evidence | Fix |
|---|---------|----------|-----|
| C1 | The arpeggiator section has no working Rate, Octave, Gate, Swing or Latch control. Rate and Octave show only a title and a value, Swing shows a title and `0 %`, Latch shows a title and `Off`, Gate is absent, and the advanced-summary strip is absent. | Deck 2 is 140 px, so the section content is 92 px. The arp block splits it into a 46 px top row and a 36 px middle row. `SegmentedChoiceGroup::resized()` reserves 20 px title + 18 px value + 8 px padding, leaving 0 px for buttons at 46 px. `HardwareKnob::resized()` reserves 40 px for labels, leaving nothing at 36 px. `arpGateKnob` is laid out with weight 0.0. The status row is 92 - 46 - 6 - 36 - 4 = 0 px. Screenshot confirms. | Give deck 2 enough height for two real rows (about 190 px) or move the arp to its own strip; make `HardwareKnob`, `LedToggleButton` and `SegmentedChoiceGroup` degrade (hide the value label below a minimum height instead of eating the control); give Gate a real weight; add the layout regression test described at the end. |
| C2 | The Advanced overlay is non-functional. The Rhythm selector (Straight/Euclidean) has no buttons, so Euclidean mode cannot be entered from the UI. Chance, Ratchet Chance, Accent and Tempo show no knob. Ratchet and Accent Every render as 10 px tall strips. | `rhythmChoice` gets a 40 px row (0 px for buttons). The Modifiers section is 104 px, content 56 px, so knobs get 56 - 40 = 16 px, reduced by 8 px padding to 0. Tempo content is 30 px. Screenshot confirms only titles and values are drawn. The existing tests only assert `isVisible()`, which is true for a 0 px component. | Size the overlay sections from the controls' real minimum heights (knob about 96 px, segmented group about 76 px). The overlay panel has roughly 150 px of unused height at the bottom already. |
| C3 | The bottom-row knobs (Timbre, Excite, Phaser Rate/Depth, Compressor Amount/Mix) show `-` as their value, forever. | `refreshValueDisplays()` stops at `outGainKnob`; the eight controls added in the FX phase were never added. Screenshot shows `-` under all six knobs. | Add the six `setValueText` calls (and the two header toggles for completeness). Better: derive the refresh list from the same registration table used for MIDI learn so a new control cannot be forgotten. |
| C4 | The window is a fixed 1550x666 and cannot be resized or scaled. On a 1920x1080 display at Windows' default 125% scaling the usable width is 1536 px, so the panel does not fit; 1366x768 laptops are hopeless. There is no zoom, so WCAG 1.4.4 (resize text to 200%) fails as well. | `setSize()` only; no `setResizable` or `setResizeLimits` anywhere in `src/`. JUCE documents `setResizable(bool allowHostToResize, bool useBottomRightCornerResizer)` and `setResizeLimits` for exactly this. The weighted-row layout was explicitly built "once the window becomes user-resizable". | Call `setResizable(true, true)` and `setResizeLimits` with a fixed aspect ratio via `getConstrainer()->setFixedAspectRatio`, scale the editor with `AffineTransform::scale` or `Desktop::setGlobalScaleFactor` (standalone), and add a scale menu entry (75/100/125/150%). |

## Usability

| Finding | Severity | Recommendation |
|---------|----------|----------------|
| Labels truncate: `Resonan...`, `MW-...`, `PB ...`, `Pan Spr...`, `PUL...`, and both Saw and Square in the LFO wave group read `S...`, so two different options carry the same caption. | 🔴 Critical for the LFO group, 🟡 elsewhere | 15 px titles in 46 px cells cannot fit. Drop titles to 12 to 13 px, shorten (`Reso`, `MW Dep`, `Bend`, `Spread`), and in icon cells narrower than 32 px draw the icon only and keep the word in the tooltip. Give the LFO wave group 2x2 instead of 1x4. |
| The two oscillator rows are never labelled A and B. Both read `Wave / Octave / Fine / Pulse W`; only the Mixer and tooltips mention A and B. | 🟡 Moderate | Add a small `A` / `B` tag at the left of each row, or title the rows `Osc A` / `Osc B` in the section header strip. |
| Destructive actions have no undo and no confirmation: Init Patch, Load Patch and picking a preset all replace the whole patch instantly. There is no edit history anywhere. | 🟡 Moderate | Wire the APVTS `UndoManager` and expose Undo/Redo (Ctrl+Z) in the title row, or at least a Compare toggle; a confirmation on Init Patch is the minimal fix. NN/g heuristics 3 and 5 (user control and freedom, error prevention). |
| The preset selector goes stale. After Init Patch or Load Patch it still shows the last preset name, and no patch name is shown anywhere else. | 🟡 Moderate | Clear the selector (or show `Custom`) whenever a non-preset change lands; show the current patch or preset name as the panel's title text. |
| 54 factory presets are one flat list in a 180 px combo box, with no prev/next. | 🟡 Moderate | Group by category (Arp 18, Lead 10, Bass 8, Pad 8, Keys 4, FX 3, Pluck 3) as submenus, and add `<` `>` buttons beside the box. |
| Knobs cannot be set precisely: no double-click to default, no typed entry (`NoTextBox`), no fine-adjust modifier, no readout while dragging other than the 12 px value label. | 🟡 Moderate | `setDoubleClickReturnValue(true, default)`, Shift for fine drag via `setVelocityBasedMode`/`setVelocityModeParameters`, and double-click on the value label to type a value. |
| No output level or clip indication, and the compressor has no gain-reduction meter. The Output section is a lone knob and the Mixer has an empty slot. | 🟡 Moderate | Add a stereo peak meter with clip hold to the Output section (the scope FIFO already carries post-FX audio) and a small GR bar in the Compressor header. |
| MIDI learn is discoverable only by right-click, and no tooltip mentions it. | 🟡 Moderate | Add "Right-click: Learn MIDI CC" as the last line of every parameter tooltip, and a Learn button in the title row that enters a "click a control" mode. |
| Visual hierarchy is flat: Cutoff and Resonance, the most played controls, are 30 px knobs like Fine and Octave, while every FX knob is 44 px and the macro knobs are 16 px. The design plan asked for primary/secondary/tertiary density and it was not implemented. | 🟡 Moderate | Make Cutoff and Resonance at least 1.5x, give the Filter section more weight, shrink or merge Mixer. |
| The `KEYS` toggle, `OCT +/-`, header ON/OFF toggles and segment cells are small (16 to 24 px). | 🟡 Moderate | See the target-size table below. |
| The Advanced overlay is a modal layer but does not trap keyboard focus; Tab still reaches controls underneath. Escape and Close work. | 🟢 Minor | `setFocusContainerType(FocusContainerType::keyboardFocusContainer)` on the overlay and disable the main panel while it is shown. |
| The panic button is a filled square with no text; it reads as a transport Stop. The tooltip toggle is a lowercase `i`. | 🟢 Minor | Label it `PANIC` or `!`, and use a consistent icon style for both. |
| Octave knobs read `8'` / `16'` (organ footage) under a title that says `Octave`. | 🟢 Minor | Either title them `Range` with footage, or show `0 / -1 / +1`. |
| Jargon in labels: `REALITY`, `SPECTRA`, `VIBE`, `Rat Ch`, `MW->Dep`. Tooltips explain them, but the panel should not need tooltips for section names. | 🟢 Minor | `OUTPUT` and `SPECTRUM` for the panes, `MACROS` for the section, `Ratchet %`. |
| The piano-bar tooltip still describes an expand/collapse behaviour that no longer exists. | 🟢 Minor | Rewrite to describe KEYS and OCT. |
| Build hash and last-edit time are shown to end users in the status bar and plugin footer. | 🟢 Minor | Move behind an About item once the app ships; keep a short version string. |
| `README.md` leads with a screenshot of the old blue V1 panel. | 🟢 Minor | Replace with a current capture. |

## Visual hierarchy

- **What draws the eye first:** the animated visualizer, then the orange logo. That is acceptable for a synth, but the second fixation should be the Filter, and nothing distinguishes it from Poly Mod.
- **Reading flow:** left to right along deck 1 follows the signal path (oscillators, mixer, filter, envelopes), which is right. Deck 2 breaks the flow: the Arpeggiator (a note source) sits in the FX row, and the macros/phaser/compressor sit on a third row beside the keyboard. Expect users to hunt for the Phaser.
- **Emphasis:** every section title is the same 17 px bold green uppercase, every knob in a deck is the same size, and the macro knobs are the smallest on the panel even though "one knob changes everything" is their point. Section weight is carried only by width.
- **Whitespace:** deck 1 is dense and even; deck 2 and the bottom row have empty area (Mixer's fourth cell, Output, the arp status strip) while the Advanced overlay wastes a third of its panel.

## Consistency

| Element | Issue | Recommendation |
|---------|-------|----------------|
| Discrete controls | Every segmented group and body toggle shows its state twice: the lit cell or `ON` plus a value label underneath (`Saw`, `Full`, `Mono`, `Off`). The design plan said the value text should become secondary. | Hide the value label on discrete controls (keep it for MIDI-learn badges) and reclaim 18 px per control, which also fixes most zero-height rows. |
| System surfaces | The right-click menu, the Settings dialog, the message boxes and the MIDI monitor table use JUCE's default grey LookAndFeel. The Settings dialog title says "Settings" while the button says "Options". | Install one `LookAndFeel` on the editor so popups, dialogs and tables inherit the palette; name the button and dialog the same. |
| Casing | Status bar mixes `MIDI: Unavailable (Minilab3 MIDI)` with `PROFILE: MINILAB 3 / ARTURIA MODE`; action buttons are uppercase except the `i` toggle. | One rule: uppercase for buttons and section titles, sentence case for status text. |
| Accent colours | Orange exists only in the logo and the tooltip border; red exists only in the LED row and key-down overlay; yellow means both "MIDI learn armed" and the LFO/Reality traces. | Decide what orange and yellow mean. Suggest yellow = learn only, orange = modulation sources in the visualizer. |
| Spacing tokens | Knob label fonts (15/12), section title (17), overlay title (20), segment text (11/8.8), badges (8) are ad hoc. | Define a type scale in `UiPalette.h` (for example 20/15/12/10) and stop below 10 px. |
| Panel chrome | Section cards are 12 px rounded while all controls are hard rectangles, as intended; the visualizer and piano bar use 12 px and 5 px radii respectively. | Use one radius for all cards. |

## Accessibility audit (WCAG 2.2 AA)

Standard: WCAG 2.2 AA. Issues found: 14. Critical: 3, Major: 6, Minor: 5.

### Perceivable

| # | Issue | Criterion | Severity | Recommendation |
|---|-------|-----------|----------|----------------|
| P1 | No way to enlarge the UI (fixed size, no scale option). | 1.4.4 Resize Text | 🔴 Critical | Same as C4. |
| P2 | Knob range track `ledGreenDim` on `panelRaised` is 1.90:1; the user cannot see where the sweep starts and ends until the pointer moves. | 1.4.11 Non-text Contrast | 🟡 Major | Brighten the dim track to about (70,120,62) for 3:1, or draw tick marks at 3:1. |
| P3 | Off-state LED dots for black keys are pure black on `panelRaised`, 1.16:1, so the LED row shows 29 of 49 notes. | 1.4.11 | 🟡 Major | Use a mid grey at 3:1 for black-key dots, or outline them. |
| P4 | Key-down overlay on white keys is red at 42% alpha, 2.18:1. | 1.4.11 | 🟡 Major | Use an opaque fill (the `ledGreen` would also tie it to the panel language). |
| P5 | Section card strokes are 1.9 to 2.1:1. Cards are also identified by their titles, so this is allowed, but the groups read weakly on the darker lower gradient. | 1.4.11 (exempt, boundary not the only cue) | 🟢 Minor | Lift `panelStroke` to about (84,94,99). |
| P6 | Text below 10 px: 8 px CC badges, 8.8 px segment captions, 9 px visualizer and OCT labels. Contrast passes but legibility at 100% scaling does not. | Best practice (1.4.4 related) | 🟡 Major | 10 px minimum; move CC badges to the tooltip or make them optional, which `Show CC Labels` already half does. |

### Operable

| # | Issue | Criterion | Severity | Recommendation |
|---|-------|-----------|----------|----------------|
| O1 | Knobs are not keyboard operable: JUCE `Slider` defaults to `setWantsKeyboardFocus(false)` (vendored `juce_Slider.cpp` line 1461) and the editor never enables it. Arrow-key handling exists in `Slider::keyPressed` but is unreachable. | 2.1.1 Keyboard | 🔴 Critical | `knob.setWantsKeyboardFocus(true)` in `HardwareKnob`, `HardwareFader`; set a sensible interval so arrows step the parameter. |
| O2 | No focus indicator anywhere. Buttons and combo boxes are focusable (JUCE `Button` sets `setWantsKeyboardFocus(true)`), but the custom `paintButton` and LookAndFeel overrides never check `hasKeyboardFocus`. Pressing Tab three times produced a screenshot identical to the idle panel. | 2.4.7 Focus Visible | 🔴 Critical | Draw a 2 px `learnYellow` or `textPrimary` ring when `hasKeyboardFocus(true)` in `LedToggleButton::StateButton`, `SegmentButton`, `GreenActionButtonLookAndFeel`, `GreenComboBoxLookAndFeel` and `GreenKnobLookAndFeel`. |
| O3 | Focus order is creation order, which jumps from the title row to oscillators, then back to action buttons. | 2.4.3 Focus Order | 🟢 Minor | `setExplicitFocusOrder` per section or mark each `SynthSection` as a focus container. |
| O4 | Pointer targets under 24x24: `OCT +/-` 32x16, `KEYS` 32x22, Key Trk / Mode / Priority cells about 60x17, LFO wave cells 20x50, arp Octave and Rate cells 0 px, overlay Ratchet/Accent cells 10 px tall. Header ON/OFF toggles are exactly 24x24. | 2.5.8 Target Size (Minimum) | 🟡 Major | 24 px minimum everywhere; 28 px for the header toggles. |
| O5 | The LED row and keyboard octave shift has no keyboard shortcut; the on-screen keyboard cannot be played from the computer keyboard. | 2.1.1 | 🟢 Minor | Z/X for octave, standard QWERTY-to-note mapping via `KeyPress` in `PianoBarComponent`. |

### Understandable

| # | Issue | Criterion | Severity | Recommendation |
|---|-------|-----------|----------|----------------|
| U1 | Two LFO wave options are both captioned `S...`. | 3.3.2 Labels or Instructions | 🟡 Major | See label truncation above. |
| U2 | Oscillator rows have no A/B label; `Wave` appears four times on the panel. | 3.3.2 | 🟡 Major | See usability table. |
| U3 | Patch errors use a native message box with a clear message; good. Init Patch gives no warning. | 3.3.4 Error Prevention (AAA, informational) | 🟢 Minor | Confirmation or undo. |

### Robust

| # | Issue | Criterion | Severity | Recommendation |
|---|-------|-----------|----------|----------------|
| R1 | Screen readers get no names. The `juce::Slider` inside every `HardwareKnob` has an empty name, the title `Label` is not attached to it, every `LedToggleButton` is a `juce::Button` named `"state"`, so Narrator announces "state, button" for Sync, Lo Freq, Arp On, Latch and all FX enables. Section titles are painted text with no accessible equivalent. `SegmentButton` is named from its option label, which is right. | 4.1.2 Name, Role, Value | 🟡 Major | `setTitle`/`setDescription`/`setHelpText` on each slider and button from the same table that feeds tooltips; `titleLabel.attachToComponent(&knob, false)`; give each `SynthSection` a title and `AccessibilityRole::group`. JUCE's `docs/Accessibility.md` describes these three calls as the way to control what clients read out. |

### Colour contrast check

Ratios computed with the WCAG relative-luminance formula from `UiPalette.h`.

| Element | Foreground | Background | Ratio | Required | Pass? |
|---------|-----------|------------|-------|----------|-------|
| Knob title 15 px | textPrimary (244,244,239) | panelRaised (18,23,25) | 16.4:1 | 4.5:1 | ✅ |
| Knob value 12 px | textSecondary (166,173,176) | panelRaised | 7.9:1 | 4.5:1 | ✅ |
| Section title 17 px bold | ledTextOff (139,255,121) | panelRaisedAlt (28,34,37) | 12.8:1 | 3:1 | ✅ |
| Button OFF text 11 px | ledTextOff | panelRaised | 14.4:1 | 4.5:1 | ✅ |
| Button ON text 11 px | ledTextOn (12,18,12) | ledGreen (123,245,96) | 13.6:1 | 4.5:1 | ✅ |
| CC badge 8 px | panelBlack | badgeGreen at 72% over panelRaised | 6.2:1 | 4.5:1 | ✅ (size is the problem) |
| Status bar 12 px | textSecondary | black 20% over gradient | 8.7:1 | 4.5:1 | ✅ |
| Tooltip 13 px | textPrimary | tooltipBackground (38,45,48) | 12.7:1 | 4.5:1 | ✅ |
| MIDI-learn status 14 px bold | learnYellow (255,214,76) | panelBlack | 14.3:1 | 4.5:1 | ✅ |
| Knob track (non-text) | ledGreenDim (45,77,40) | panelRaised | 1.90:1 | 3:1 | ❌ |
| Knob active arc | ledGreen 65% | panelRaised | 6.0:1 | 3:1 | ✅ |
| Button/segment stroke | ledGreen | panelRaised | 13.0:1 | 3:1 | ✅ |
| Section card stroke | panelStroke (63,71,75) | panelBlack / panelRaised | 2.1:1 / 1.9:1 | 3:1 | ❌ (exempt, see P5) |
| LED off, white key | white 42% | panelRaised | 4.1:1 | 3:1 | ✅ |
| LED off, black key | black | panelRaised | 1.16:1 | 3:1 | ❌ |
| Key-down overlay | red 42% | white key | 2.18:1 | 3:1 | ❌ |
| Learn-armed ring | learnYellow | panelRaised | 12.9:1 | 3:1 | ✅ |

Text contrast is excellent throughout; every text failure in this UI is a size failure, not a colour failure.

### Keyboard navigation

| Element | Tab | Enter/Space | Escape | Arrows |
|---------|-----|-------------|--------|--------|
| Knobs (all `HardwareKnob`) | Not reachable | n/a | n/a | Would work if focusable |
| LED toggles, segment cells, action buttons | Reachable, invisible | Toggles / activates | n/a | n/a |
| Preset and Pattern combo boxes | Reachable, invisible | Opens menu | Closes menu | Changes selection |
| Advanced overlay | Opens by click only | Close button works | Closes overlay | n/a |
| Right-click learn menu | Mouse only (no Menu key / Shift+F10 path) | n/a | Closes | Moves in menu |
| Piano keyboard | Not playable | n/a | n/a | n/a |

### Screen reader

| Element | Announced as | Issue |
|---------|-------------|-------|
| Any knob | "slider" with a value | No name; the visible title is not associated |
| Sync, Lo Freq, Arp On, Latch, FX enables | "state, toggle button" | Button name is the literal string `state` |
| Wave / Mode / Priority cells | "Saw, button, pressed" | Good; the group has no name though |
| Section titles | Nothing | Painted text |
| Visualizer | Nothing | Decorative; acceptable, but should be marked ignored |
| LED row | Nothing | Note activity has no text equivalent |

## What works well

- The green LED state language is applied exactly as the design contract specified: hard rectangles, lit cell for the active choice, dark text on green, zero-gap groups with connected edges.
- Tooltips are complete, titled, wrapped and written for musicians ("Lower values sound darker"). Section tooltips explain the signal path. The toggle to turn them off is thoughtful.
- Every continuous control shows its value in real units (Hz, ms, %, dB, st, octave footage), refreshed only when a parameter actually moves.
- The visualizer gives genuine feedback: LFO shape, both envelope curves, source and filtered waveform, a live scope and a spectrum, all from one glance.
- MIDI learn is end to end: right-click to arm, yellow ring and status text while armed, CC badges after, persisted in standalone settings or plugin state, with a Show CC Labels preference.
- The Advanced overlay dims the panel, centres, closes on Escape and on its button, and its Euclidean visualizer uses the same cycle helper as the engine, so what you see is what plays.
- Patch format errors are explicit and user-readable instead of silently partial loads.
- The layout uses a weighted distributor and named metrics, so the resizing recommendation above is cheap to implement.
- Vector rendering is crisp at 175% scaling; nothing is bitmap-blurry.

## Priority recommendations

1. **Repair the collapsed controls and stop it recurring.** Raise deck 2 to about 190 px, size the overlay sections from control minimums, give Gate a weight, and make the three control classes hide their value label instead of the control when short. Add the regression test below so a 0 px control fails CI. This restores Rate, Octave, Gate, Swing, Latch, the summary strip, Rhythm, Chance, Ratchet Chance, Accent and Tempo.
2. **Finish the readouts and the resizable window.** Add the six missing `setValueText` calls (or drive refresh from a table), then `setResizable(true, true)` with a fixed aspect ratio and a scale menu. This fixes C3, C4 and P1 and lets the 1080p/125% audience use the app.
3. **Make it keyboard and screen-reader usable.** Focusable sliders, a visible focus ring in the five custom paint paths, names on sliders and toggles, attached labels, focus containment in the overlay. This is mostly mechanical and clears O1, O2, R1.
4. **Fix the label system.** Smaller title font, shorter words, icon-only cells under 32 px, A/B tags on the oscillator rows, and drop the redundant value labels under discrete controls. This clears the `S...` ambiguity and gives rows the height they need.
5. **Add undo and a visible patch identity.** APVTS `UndoManager`, Ctrl+Z, a current patch name in the title row, a preset selector that clears on edit and groups by category.
6. **Then polish:** output meter and compressor GR, 24 px minimum targets, themed popups and dialogs, consistent casing, replace the README screenshot.

## Suggested regression test

Add to `tests/StabilityAndDisconnectTests.cpp`: construct `SynthAudioProcessorEditor` in standalone and plugin mode at the default size, open the Advanced overlay with the test hook, then walk every visible descendant that is a `juce::Slider`, `juce::Button` or `juce::ComboBox` and assert `getWidth() >= 24 && getHeight() >= 24`, printing the component's parent title on failure. That single test would have caught C1, C2 and the Gate knob, and it guards every future layout change.

## Sources

- WCAG 2.2 Understanding SC 1.4.3 Contrast (Minimum): https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
- WCAG 2.2 Understanding SC 1.4.11 Non-text Contrast: https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html
- WCAG 2.2 Understanding SC 2.4.7 Focus Visible: https://www.w3.org/WAI/WCAG22/Understanding/focus-visible.html
- WCAG 2.2 Understanding SC 2.5.8 Target Size (Minimum), 24x24 CSS px at level AA: https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html
- JUCE `AudioProcessorEditor` reference (`setResizable`, `setResizeLimits`, `getConstrainer`, `setScaleFactor`): https://docs.juce.com/master/classAudioProcessorEditor.html
- JUCE `docs/Accessibility.md` (`setTitle`, `setDescription`, `setHelpText`, focus order, custom handlers): https://sources.debian.org/src/juce/7.0.5%2Bds-1/docs/Accessibility.md
- Vendored JUCE source: `juce_Slider.cpp` line 1461 (`setWantsKeyboardFocus(false)`), `juce_Slider.cpp` lines 1029-1060 (arrow-key handling), `juce_Button.cpp` line 89 (`setWantsKeyboardFocus(true)`).
- Nielsen Norman Group, 10 Usability Heuristics for User Interface Design: https://www.nngroup.com/articles/ten-usability-heuristics/
