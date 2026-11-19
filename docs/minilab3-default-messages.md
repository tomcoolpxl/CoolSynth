# MiniLab 3 Default Messages

> **History.** This page records V2's factory profile, which mapped the MiniLab's controls straight to sound
> parameters. It was removed with the rest of V2's MIDI mapping (D212). Today the MiniLab 3 has CoolSynth's own
> driver (D184, D211); in its Arturia program, with the driver off, it uses the ready setup in
> `src/midi/ControllerMap.cpp` (`readyProfiles`), which plays the panel's knobs, pads and faders. The control
> numbers below are still the MiniLab's.

Capture date: 2026-05-10
Device assumption: MiniLab 3 Arturia Mode
Capture surface: bundled factory controller profile `arturia.minilab3.arturia-mode.v1`

## Verified Table

| Control | Category | Message kind | Channel | Primary data | Secondary behavior | Preferred target | Disposition | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Keyboard | keyboard | note | 1 | 0..127 | velocity 1..127 | note input | requiredForPhase7 | Standard MIDI notes |
| Knob 1 | knob | controlChange | omni | 74 | absolute 0..127 | filter cutoff | shipped | "Brightness" in Arturia mode |
| Knob 2 | knob | controlChange | omni | 71 | absolute 0..127 | filter resonance | shipped | "Timbre" in Arturia mode |
| Knob 3 | knob | controlChange | omni | 76 | absolute 0..127 | filter envelope amount | shipped | "Variation" in Arturia mode |
| Knob 4 | knob | controlChange | omni | 77 | absolute 0..127 | oscillator B fine tune | shipped | "Movement" in Arturia mode |
| Knob 5 | knob | controlChange | omni | 93 | absolute 0..127 | amp attack | shipped | "FX A" in Arturia mode |
| Knob 6 | knob | controlChange | omni | 18 | absolute 0..127 | amp decay | shipped | "FX B" in Arturia mode |
| Knob 7 | knob | controlChange | omni | 19 | absolute 0..127 | amp sustain | shipped | "Delay" in Arturia mode |
| Knob 8 | knob | controlChange | omni | 16 | absolute 0..127 | amp release | shipped | "Reverb" in Arturia mode |
| Fader 1 | fader | controlChange | omni | 82 | absolute 0..127 | oscillator A level | shipped | "Bass EQ" in Arturia mode |
| Fader 2 | fader | controlChange | omni | 83 | absolute 0..127 | oscillator B level | shipped | "Mid EQ" in Arturia mode |
| Fader 3 | fader | controlChange | omni | 85 | absolute 0..127 | master gain | shipped | "High EQ" in Arturia mode |
| Encoder | encoder | controlChange | omni | 114 | relative binary offset | waveform | shipped | Main encoder, one step per detent |
| Pad 8 | pad | note | 10 | 43 | velocity 1..127 | panic | shipped | Bank A, G1 (note 43) |

## Notes

- V2 loaded this profile from `resources/controller_profiles/minilab3_arturia_mode.json` (removed in D212).
- V2's standalone mode auto-detected it for matching MiniLab 3 device names, and its MIDI tab allowed forcing or disabling it.
- V2's MIDI learn overrode these bindings per parameter.

## Deferred Controls

- Fader 4: Unassigned.
- Pads 1-7: Unassigned.
