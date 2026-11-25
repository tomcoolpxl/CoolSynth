# The Arturia MiniLab 3 in a DAW

The MiniLab sends its pads on MIDI channel 10 and its keys on channel 1. CoolSynth's installer adds a MiniLab script to
Ableton Live, Bitwig Studio and FL Studio, so the pads drive CoolSynth's panel and don't also play notes in your DAW.

## Ableton Live

Live loads the CoolSynth_MiniLab control surface by itself and arms the MIDI track you select, so the keys play it.

If **Settings > Link, Tempo & MIDI** shows another control surface on the MiniLab (one set up before CoolSynth was
installed), choose CoolSynth_MiniLab there instead, with input and output Minilab3 MIDI. Or set the CoolSynth track's
**MIDI From** channel to **Ch. 1**.

## Bitwig Studio

Bitwig's own MiniLab 3 controller launches clips with the pads. In **Settings > Controllers**, remove the Arturia
MiniLab 3 controller if Bitwig set it up, and add CoolSynth MiniLab 3 under the detected MiniLab.

## FL Studio

FL picks the CoolSynth MiniLab 3 script by itself. If you used the MiniLab in FL before, FL keeps your earlier choice:
in **Options > MIDI settings**, set the MiniLab's controller type to CoolSynth MiniLab 3 once.

## As an ordinary MIDI controller

Turn off **Prefs > Controller setup > MiniLab 3 driver** in CoolSynth. The MiniLab then works in its Arturia program
and in its DAW program as usual.
