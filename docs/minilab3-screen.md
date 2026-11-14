# MiniLab 3: screen, pads and controls, verified on the device

Tested on 2026-11-14 on the user's MiniLab 3, from Windows over winmm, port **Minilab3 MIDI**
(the device also shows DIN THRU, MCU/HUI and ALV ports; all replies came on Minilab3 MIDI).
Sources for the formats: Ableton Live 12's `MiniLab_3` remote script
(https://github.com/gluon/AbletonLive12_MIDIRemoteScripts/tree/main/MiniLab_3), a gist from
Arturia's decompiled Bitwig extension
(https://gist.github.com/Janiczek/04a87c2534b9d1435a1d8159c742d260) and the Arturia forum post
(https://forum.arturia.com/t/sysex-protocol-documentation/5746). Decisions: D146.

Every message: `F0 00 20 6B 7F 42 <body> F7`.

## Verified on the device

| What | Body | Result |
|---|---|---|
| Ask the program | `01 00 40 01` | replies `02 00 40 01 01` |
| Connect (DAW program) | `02 00 40 6A 21` | replies `02 00 40 63 00`; screen and pads take messages after it |
| Pad colour, bank A pad n (0..7) | `02 02 16 <34+n> r g b`, 0..127 | pad lights and stays |
| Full screen | `04 02 60 1F 07 01 <icon top> <icon bottom> 00 00 01 <top line> 00 02 <bottom line> 00` | stays until replaced |
| Knob pop-up | `04 02 60 1F 03 02 <value 0..127> 00 00 01 <name> 00 02 <value text> 00` | name, value and a knob graphic |
| Fader pop-up | as the knob pop-up with `1F 04 02` | bar graphic |
| Pad pop-up | as the knob pop-up with `1F 05 02` | pad graphic |
| Parameter pop-up | `04 02 60 1F 08 02 <knob CC> 00 01 <name> 00 02 <value text> 00` | the device draws from its own knob value |

- **The top line is the big one**, 16 characters; the bottom line is smaller, 18 characters
  (measured with digits and capitals). Longer text is cut, not scrolled.
- **Icons**, one at the start of each line, then a space, then the text: 0 none, 1 heart,
  2 play triangle, 3 record dot, 4 note, 5 check mark (each checked on the device).
- **Pop-ups are transient**, about a second, and **draw their graphic only while a control is
  physically moving**. A pop-up sent while nothing moves shows at most its text, briefly. So the
  app sends a pop-up in answer to each knob or fader message, never on its own.
- **Knobs 1-8** send absolute CCs on channel 1: 86, 87, 89, 90, 110, 111, 116, 117 (knob 1
  checked). In the DAW program the device draws nothing by itself.
- **Main knob:** turning sends CC 28 on channel 1 as an offset from 64 (65 = +1, 62 = -2;
  fast turns send larger steps). Pressing sends CC 118, 127 on press and 0 on release.

## The screen in CoolSynth (navigation E; D143-D146)

**Lists** (categories, a category's sounds, the type list) use one layout, tried on the device
and approved by the user:

- Top line (big): the selected item, with the play triangle (icon 2) as the cursor in front.
- Bottom line (small): the next item, as a preview.
- Turning the main knob scrolls the list under the fixed cursor; pressing opens or loads.
- The check mark (icon 5) marks the sound that is playing, on whichever line it is; when the
  cursor is on it, the check mark takes the cursor's place.
- Inside a category the first row is `< Back`; pressing it returns to the categories.

| Where | Top line, big (16) | Bottom line, small (18) |
|---|---|---|
| Category list | ▶ `Outrun` | `Observatory` |
| A category | ▶ `Gated Pad` (✓ if playing) | `Night Drive` |
| Its first row | ▶ `< Back` | `Neon Brass` |
| The type list | ▶ `SVF` | `Crusher` |
| A section | `SHAPE P1 Ladder` | `Filter 1/2` |
| Turning knob 1-4 or 6-8 | parameter name | value with unit, plus the knob graphic |
| Moving a fader | `Part A` | `80%`, plus the bar graphic |

Pads take the colours of the screen: the open section bright, the others dim, Home (pad 8)
teal, the recorder pads dim until the recorder exists.

## Test tools

Throwaway scripts (not in the repo) over winmm: ports, SysEx, input logging, a live menu driven
by the main knob. A real implementation belongs in the app's MIDI layer (JUCE `MidiOutput` and
`MidiInput`).
