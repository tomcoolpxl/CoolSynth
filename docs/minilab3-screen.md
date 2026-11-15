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
| Pad pop-up | as the knob pop-up with `1F 05 02` | pad graphic; firmware 1.2.0 shows it only while something moves (D191) |
| Parameter pop-up | `04 02 60 1F 08 02 <knob CC> 00 01 <name> 00 02 <value text> 00` | the device draws from its own knob value |
| Scroll screen | `04 02 60 1F 06 00 <position> 00 <length> 00 00 01 <line 1> 00 02 <line 2> 00` | stays; line 1 small, **line 2 big** between `<` and `>` arrows, a small marker (about 1.5 characters wide) at position/length |

- **The top line is the big one**, 16 characters; the bottom line is smaller, 18 characters
  (measured with digits and capitals). Longer text is cut, not scrolled.
- **Icons**, one at the start of each line, then a space, then the text: 0 none, 1 heart,
  2 play triangle, 3 record dot, 4 note, 5 check mark (each checked on the device). The third
  icon byte shows nothing: one icon per line at most.
- **In the scroll screen the sizes flip**: line 2 is the big one. So it is in plain text with
  no layout bytes (`04 02 60 01 <line 1> 00 02 <line 2> 00`) and in the two-lines mode
  (`1F 02 01 00`): line 1 small, line 2 big, no arrows, no marker. Only the full screen
  (`1F 07`) has line 1 big and the icons.
- **Pop-ups are transient**, about a second, and **draw their graphic only while a control is
  physically moving**. A pop-up sent while nothing moves shows at most its text, briefly. So the
  app sends a pop-up in answer to each knob or fader message, never on its own.
- **A pad press is not "moving"** for firmware 1.2.0: the pad pop-up showed only while keys were
  played (the user, 2026-11-15). Arturia's 1.2.0 notes say automated controls show only while
  adjusted, and a user reports a pad-held pop-up that worked on 1.1.1 and fails on 1.2.0
  (https://forum.arturia.com/t/minilab-3-firmware-v1-2-0/6064). So a pad flashes its name on the
  **full screen** (`1F 07`, always shown) for a second, then the app sends the scroll screen again
  (D191; confirmed on the device).
- **Knobs 1-8** send absolute CCs on channel 1: 86, 87, 89, 90, 110, 111, 116, 117 (knob 1
  checked). In the DAW program the device draws nothing by itself. The knobs turn endlessly, but
  each sends a count, 0-127, that stops at its ends; turning on past an end sends the end again
  (127, 127...). The device ignores a count sent back to it. CoolSynth reads the knobs as scroll
  wheels: a change of the count is that many clicks, the end sent again one more (D200). About 40
  counts make a quarter turn.
- **Main knob:** turning sends CC 28 on channel 1: one click right is 65 (faster 66), one click
  left is **62** (faster 61); 63 never comes, so below 64 a step is value - 63. Pressing sends
  CC 118, 127 on press and 0 on release.
- **Shift** sends CC 27 on channel 1, 127 pressed and 0 released. **Oct -, Oct + and Hold send
  nothing** on any of the four ports (guided test with every port open, 2026-11-14).
- **Firmware 1.2.0** (identity reply `F0 7E 7F 06 02 00 20 6B 02 00 04 04 45 00 02 01 F7`, version
  bytes lowest first). Arturia users report that this firmware now and then sends a key's note-on
  with velocity 1 (https://forum.arturia.com/t/random-velocity-1-bug/5669); the driver gives such a
  note the previous note's velocity. The flight recorder (D150) logs every note with its velocity,
  and each fixed note, in `%LOCALAPPDATA%\CoolSynth\logs\session-*.log`.

## The screen in CoolSynth (navigation E; D143-D146)

**Lists** (categories, a category's sounds, the type list) use the **scroll screen**, the user's
choice after trying it live (D147; it replaced the icon menu of D146):

- Line 2 (big): the selected item between the device's `<` `>` arrows, with ` *` after it when it
  is the sound playing (or the slot's current type).
- Line 1 (small): where you are, `Outrun`, and nothing else (a count like `2/6` was tried and
  dropped by the user as confusing).
- The marker slides with position/length; the app redraws the screen on every turn.
- Inside a category the first row is `< Back`; pressing it returns to the categories.

| Where | Line 1 | Line 2 |
|---|---|---|
| Category list (scroll) | `Categories`, small | `< Outrun >`, big |
| A category (scroll) | `Outrun`, small | `< Gated Pad >`, big (`*` if playing) |
| Its first row (scroll) | `Outrun`, small | `< < Back >`, big |
| The type list (scroll) | `Type P1`, small | `< SVF >`, big |
| A section (scroll; the main knob moves between its slots) | `Shape · Filter`, small (section and page) | `< P1 Ladder >`, big |
| Turning knob 1-4 or 6-8 | parameter name | value with unit, plus the knob graphic |
| Moving a fader | `Part A` | `80%`, plus the bar graphic |

Pads take the colours of the screen: the open section bright, the others dim, Home (pad 8)
teal, the recorder pads dim until the recorder exists.

## Test tools

Throwaway scripts (not in the repo) over winmm: ports, SysEx, input logging, a live menu driven
by the main knob. A real implementation belongs in the app's MIDI layer (JUCE `MidiOutput` and
`MidiInput`).
