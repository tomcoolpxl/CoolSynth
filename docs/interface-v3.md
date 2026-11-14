# CoolSynth V3: the interface

The interface designed with the user on 2026-11-14 (decisions D140 to D147), from two rounds of
clickable mock-ups (`docs/ui-v3-mockups.html`, `docs/ui-v3-mockups-2.html`, published at
https://claude.ai/artifact/2bUoUQmdY8dg4VCxwC6mwe) and tests on the user's MiniLab 3
(`docs/minilab3-screen.md`). This file is the specification; where it and the mock-up disagree,
this file wins and the mock-up is fixed.

## 1. The idea

The panel copies the Arturia MiniLab 3 and drives the real one. A small screen and four soft
knobs whose job changes per page, never every parameter at once. A simple, modern surface on the
deep engine; an advanced editor comes much later. Two knobs navigate: the main knob (turn and
press) and knob 5.

## 2. The panel

| Control | Job |
|---|---|
| Shift | Held: knobs turn ten times finer; Shift + pad 8 switches part A/B. Lit while held. Light grey border. |
| Hold, Oct -, Oct + | Work inside the keyboard only; the app does not rely on them. |
| Two touch strips (Pitch, Mod) | Pitch bend and mod wheel, as the keyboard sends them. |
| Screen | The software screen (section 4). Larger in software, in the same place. |
| Main knob | Turn: moves through the list in view. Press: opens, loads or chooses a type. Its current job is printed beside it ("turn Sound", "press Load"). |
| Knobs 1-4 | The page in view. Ringed in the colour of the open section (teal in Home, lime on Perform pages). |
| Knob 5 | Pages: of the focused slot in a section, of the live pages in Home. Blue navigation ring. |
| Knobs 6-8 | Glide, Brightness, Space, the same everywhere. |
| Faders 1-4 | Part A, Part B, Effects, Master; pickup in the software (the hardware has none). |
| Pads 1-4 | Sound, Shape, Motion, Effects: open the section; the lit pad pressed again moves to the next slot. |
| Pads 5-7 | ■ ▶ ●: kept for the recorder (later), dim. |
| Pad 8 | **Home**, teal: the sound browser from anywhere. Shift + pad 8: part A/B. |
| Keys | A short row of keys. White keys white, black keys black in both themes. |

All knobs are flat. Pads print their job (Sound, Shape, Motion, Effects, ■ ▶ ●, Home), centred.
Colours: Sound #f2a33a, Shape #35b8e0, Motion #9b7bff, Effects #ec5fa6, Perform #a4d23f,
Home #23c4b5, Stop #c8d0d8, Play #3ccf73, Record #f0524f.

There is **no Back button** (D144). Going back is a `‹ Back` row at the top of each list; the
pads jump between places, so sections need no way back.

## 3. Navigation: one map per section (E, D143)

Two levels: **Home** (the sound browser) and **a section**.

### Home (pad 8)

- The top is the list of **categories** (the rooms). The main knob picks one, a press opens it.
- Inside a category the first row is `‹ Back to categories`, then the sounds. Turning looks,
  pressing loads; the sound playing is marked. Pressing the playing sound does nothing.
- Home is also the **live page** (D145): knob 5 turns the live pages, knobs 1-4 play the open one.
  - Macros: the playing sound's four, in fixed roles (1 tone, 2 character, 3 time, 4 movement),
    named per preset (Neon Brass: Blare, Thickness, Swell, Shimmer); role names when a preset has
    none yet.
  - Perform: Arp (on, rate, pattern, octaves), Groove (gate, swing, chance, latch), Chord (chord,
    strum, learn), Voice (mode, polyphony, glide, bend range).

### A section (pads 1-4)

The screen draws only that section, wired as in the engine:

| Section | Slots and wiring |
|---|---|
| Sound | G1 + G2 + G3, mixed, → Shape |
| Shape | P1 → P2 → P3, in a row, → Effects |
| Motion | envelopes E1 E2 E3; LFOs L1 L2 |
| Effects | part inserts I1 → I2 → I3; master F1 → F2 → F3, F4 → F5 → F6 |

- The main knob moves between the slots; the lit pad pressed again does the same.
- Knob 5 turns the focused slot's pages; each slot remembers its page.
- Knobs 1-4 edit the page at once; a turned knob shows a pop-up with its name and value.
- A press opens the **type list** for the slot (Generators: Off, VA, Noise, FM2, FM4, Wavetable,
  String, Modal, Sample, Grain; Shapers: Off, Ladder, SVF, Crusher, Folder, Drive, Formant, Comb;
  effects: the twelve effect types). Its first row is `‹ Back` (keeps the type). Motion slots have
  one type; pressing there does nothing.
- Empty slots show dashed; their knobs show "—".

A trail under the screen title says where you are, with marks for what each knob moves:
`Neon Brass › Shape › ◎ P1 Ladder › 5 Filter`.

## 4. The software screen

Header: a chip in the section's colour (or SOUNDS/CATEGORIES/the room in teal) and Part A/B or
the position. Under it the trail. Then the list or the section map, a hint line, and four cards
for knobs 1-4 (role or "knob n", name, value, a bar) in the page's colour. In Home the live-page
tabs sit between the list and the cards.

## 5. The MiniLab's own screen and pads (D146, D147)

Bytes and tests: `docs/minilab3-screen.md`. The standalone app owns the MiniLab's port
("Minilab3 MIDI"), connects to its DAW program, and then:

- **Lists and sections use the scroll screen**: line 1 small says where you are (`Outrun`,
  `Categories`, `Type P1`, `Sound · Wave`); line 2 big is what the main knob is on, between the
  device's `<` `>`, with ` *` for the sound playing or the slot's current type; the device's marker
  shows the position. No counts.
- **Turning a knob** answers with the knob pop-up (name, value with unit, graphic); moving a fader
  with the fader pop-up. The device draws the graphic only while the control moves.
- **Pads** take the screen's colours: the open section bright, others dim, Home teal (bright in
  Home), recorder pads dim.
- **Input**: knobs 1-8 are CCs 86, 87, 89, 90, 110, 111, 116, 117 (absolute; the app treats changes
  as relative steps and resets the stored value on page changes so nothing jumps); main knob CC 28
  (offset from 64), press CC 118; pads notes 36-43 on channel 10; faders CCs 14, 15, 30, 31.
- Inside a DAW, the DAW's own MiniLab script would fight the plugin for the screen; the driver
  runs in the standalone app only.

## 6. Not yet

The recorder (pads 5-7, tracks, its windows), a wizard for other controllers, the advanced
editor, per-preset macro names for all 120 presets (none are written yet), Brightness and Space
engine hooks (D140).
