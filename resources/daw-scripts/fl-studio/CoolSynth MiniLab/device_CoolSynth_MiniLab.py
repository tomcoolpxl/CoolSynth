# name=CoolSynth MiniLab 3
# url=https://github.com/tomcoolpxl/CoolSynth
# supportedDevices=Minilab3 MIDI,MiniLab 3 MIDI
"""
CoolSynth's FL Studio script for the Arturia MiniLab 3 (decision D211).

CoolSynth drives the MiniLab itself (its screen, pad lights, knobs and pads), straight from the MiniLab's own MIDI
port. FL Studio would also hand the MiniLab's pads to the instrument as notes, so a pad would switch CoolSynth's panel
and play a note. This script takes the pads' notes (channel 10, notes 36-51, banks A and B) before FL passes them on;
the keys, knobs, faders, bend, mod wheel and pedal go on as with any controller.

Install: put this folder in Documents\\Image-Line\\FL Studio\\Settings\\Hardware\\ (the CoolSynth installer does it),
then in FL Studio, Options > MIDI settings, select the "Minilab3 MIDI" input and choose "CoolSynth MiniLab 3" as its
Controller type.
"""

PAD_CHANNEL = 9           # channel 10, counted from 0
FIRST_PAD_NOTE = 36
PAD_NOTE_COUNT = 16       # banks A and B


def OnMidiIn(event):
    kind = event.status & 0xF0
    channel = event.status & 0x0F
    if channel == PAD_CHANNEL and kind in (0x80, 0x90) and FIRST_PAD_NOTE <= event.data1 < FIRST_PAD_NOTE + PAD_NOTE_COUNT:
        event.handled = True  # CoolSynth's MiniLab driver has the pad already
