# name=CoolSynth MiniLab 3
# url=https://github.com/tomcoolpxl/CoolSynth
# supportedDevices=Minilab3 MIDI,MiniLab 3 MIDI
# supportedHardwareIds=00 20 6B 02 00 04 04
"""
CoolSynth's FL Studio script for the Arturia MiniLab 3 (decision D211).

CoolSynth drives the MiniLab itself (its screen, pad lights, knobs and pads), straight from the MiniLab's own MIDI
port. FL Studio would also hand the MiniLab's pads to the instrument as notes, so a pad would switch CoolSynth's panel
and play a note. While a CoolSynth is in the project, this script takes the pads' notes (channel 10, notes 36-51,
banks A and B) before FL passes them on; without a CoolSynth the pads play notes in FL as usual. The keys, knobs,
faders, bend, mod wheel and pedal always go on as with any controller.

Install: put this folder in Documents\\Image-Line\\FL Studio\\Settings\\Hardware\\ (the CoolSynth installer does it).
FL links it to the MiniLab by itself: by the port's name (supportedDevices) and by the MiniLab's hardware ID
(supportedHardwareIds, Arturia's family and model bytes, the firmware bytes left out). The ID matters: when FL hears
the MiniLab answer an identity request (from Arturia's apps or any other program), it moves the MiniLab to a script
that claims that ID, even from a script chosen by hand, and FL's own Arturia MiniLab 3 script claims it. Claiming it
too keeps the MiniLab on this one (tested in FL Studio 2025, 2026-11-21).
"""

import channels
import plugins

PAD_CHANNEL = 9           # channel 10, counted from 0
FIRST_PAD_NOTE = 36
PAD_NOTE_COUNT = 16       # banks A and B


def coolsynth_in_project():
    """True when one of the channel rack's instruments is a CoolSynth (FL's scripting API 8 and later)."""
    try:
        count = channels.channelCount(True)
        for index in range(count):
            if plugins.isValid(index, -1, True) and "coolsynth" in plugins.getPluginName(index, -1, False, True).lower():
                return True
        return False
    except TypeError:
        # An FL without global channel indexes (API before 26): the channels in view.
        for index in range(channels.channelCount()):
            if plugins.isValid(index) and "coolsynth" in plugins.getPluginName(index).lower():
                return True
        return False


def OnMidiIn(event):
    kind = event.status & 0xF0
    channel = event.status & 0x0F
    is_pad = channel == PAD_CHANNEL and kind in (0x80, 0x90) and FIRST_PAD_NOTE <= event.data1 < FIRST_PAD_NOTE + PAD_NOTE_COUNT
    if is_pad and coolsynth_in_project():
        event.handled = True  # CoolSynth's MiniLab driver has the pad already
