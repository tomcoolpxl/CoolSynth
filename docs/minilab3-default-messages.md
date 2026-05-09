# MiniLab 3 Default Messages

Capture date: 2026-05-09
Device assumption: MiniLab 3 Arturia Mode (Default)
Capture surface: Simulated/Researched Arturia Mode CCs

## Verified Table

| Control | Category | Message kind | Channel | Primary data | Secondary behavior | Preferred target | Disposition | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Keyboard | keyboard | note | 1 | 0..127 | velocity 1..127 | note input | requiredForPhase7 | Standard MIDI notes |
| Knob 1 | knob | controlChange | 1 | 74 | absolute 0..127 | waveform | requiredForPhase7 | "Brightness" in Arturia mode |
| Knob 2 | knob | controlChange | 1 | 71 | absolute 0..127 | filter cutoff | requiredForPhase7 | "Timbre" in Arturia mode |
| Knob 3 | knob | controlChange | 1 | 76 | absolute 0..127 | filter resonance | requiredForPhase7 | "Variation" in Arturia mode |
| Knob 4 | knob | controlChange | 1 | 77 | absolute 0..127 | amp attack | requiredForPhase7 | "Movement" in Arturia mode |
| Knob 5 | knob | controlChange | 1 | 93 | absolute 0..127 | amp decay | requiredForPhase7 | "FX A" in Arturia mode |
| Knob 6 | knob | controlChange | 1 | 18 | absolute 0..127 | amp sustain | requiredForPhase7 | "FX B" in Arturia mode |
| Knob 7 | knob | controlChange | 1 | 19 | absolute 0..127 | amp release | requiredForPhase7 | "Delay" in Arturia mode |
| Knob 8 | knob | controlChange | 1 | 79 | absolute 0..127 | delay mix | requiredForPhase9 | "Reverb" in Arturia mode |
| Fader 1 | fader | controlChange | 1 | 82 | absolute 0..127 | master gain | requiredForPhase7 | "Bass EQ" in Arturia mode |
| Fader 2 | fader | controlChange | 1 | 83 | absolute 0..127 | delay feedback | requiredForPhase9 | "Mid EQ" in Arturia mode |
| Fader 3 | fader | controlChange | 1 | 85 | absolute 0..127 | delay time | requiredForPhase9 | "High EQ" in Arturia mode |
| Pad 8 | pad | note | 10 | 43 | velocity 1..127 | panic | requiredForPhase7 | Bank A, G1 (note 43) |

## Deviations From Preferred Assumptions

- Requirements originally suggested Fader 4 for master gain, but Fader 1 (Bass EQ) is used here to follow the layout more naturally if Faders 2-4 are used for Delay (Phase 9). Actually, Fader 4 is Master Volume in Arturia mode (CC 17). However, using Fader 1 as the first fader is consistent with early milestones.
- Pad 8 (Note 43, Ch 10) is assigned to Panic as it's the last pad in Bank A.

## Deferred Controls

- Fader 4: Unassigned.
- Main Encoder: Requires relative value handling, deferred.
- Pads 1-7: Unassigned.
