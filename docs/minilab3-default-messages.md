# MiniLab 3 Default Messages

Capture date: 2026-05-10
Device assumption: MiniLab 3 Arturia Mode
Capture surface: bundled factory controller profile `arturia.minilab3.arturia-mode.v1`

## Verified Table

| Control | Category | Message kind | Channel | Primary data | Secondary behavior | Preferred target | Disposition | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Keyboard | keyboard | note | 1 | 0..127 | velocity 1..127 | note input | requiredForPhase7 | Standard MIDI notes |
| Knob 1 | knob | controlChange | omni | 74 | absolute 0..127 | filter cutoff | shipped | "Brightness" in Arturia mode |
| Knob 2 | knob | controlChange | omni | 71 | absolute 0..127 | filter resonance | shipped | "Timbre" in Arturia mode |
| Knob 3 | knob | controlChange | omni | 76 | absolute 0..127 | delay time | shipped | "Variation" in Arturia mode |
| Knob 4 | knob | controlChange | omni | 77 | absolute 0..127 | delay feedback | shipped | "Movement" in Arturia mode |
| Knob 5 | knob | controlChange | omni | 93 | absolute 0..127 | amp attack | shipped | "FX A" in Arturia mode |
| Knob 6 | knob | controlChange | omni | 18 | absolute 0..127 | amp decay | shipped | "FX B" in Arturia mode |
| Knob 7 | knob | controlChange | omni | 19 | absolute 0..127 | amp sustain | shipped | "Delay" in Arturia mode |
| Knob 8 | knob | controlChange | omni | 16 | absolute 0..127 | amp release | shipped | "Reverb" in Arturia mode |
| Fader 1 | fader | controlChange | omni | 82 | absolute 0..127 | master gain | shipped | "Bass EQ" in Arturia mode |
| Fader 2 | fader | controlChange | omni | 83 | absolute 0..127 | delay mix | shipped | "Mid EQ" in Arturia mode |
| Fader 3 | fader | controlChange | omni | 85 | absolute 0..127 | unassigned | deferred | "High EQ" in Arturia mode |
| Encoder | encoder | controlChange | omni | 114 | relative binary offset | waveform | shipped | Main encoder, one step per detent |
| Pad 8 | pad | note | 10 | 43 | velocity 1..127 | panic | shipped | Bank A, G1 (note 43) |

## Notes

- The shipped profile is data-driven and loaded from `resources/controller_profiles/minilab3_arturia_mode.json`.
- Standalone mode auto-detects this profile for matching MiniLab 3 device names, and the MIDI tab also allows forcing or disabling the factory profile.
- Standalone MIDI learn overrides these shipped bindings on a per-parameter basis.

## Deferred Controls

- Fader 4: Unassigned.
- Pads 1-7: Unassigned.
