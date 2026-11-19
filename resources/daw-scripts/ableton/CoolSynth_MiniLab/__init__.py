"""
CoolSynth's Ableton Live control surface for the Arturia MiniLab 3 (decision D211).

CoolSynth drives the MiniLab itself (its screen, pad lights, knobs and pads), straight from the MiniLab's own MIDI
port. Ableton would also hand the MiniLab's pads to the track as notes (on channel 1, as it gives every note to an
instrument), so a pad would switch CoolSynth's panel and play a note. This control surface listens to the pads' notes
(channel 10, notes 36-51, banks A and B); Ableton keeps messages a control surface uses out of the tracks. It sends
nothing to the MiniLab: the keys, knobs, faders, bend, mod wheel and pedal go on to the track as with any controller.

Install: put this folder in Documents\\Ableton\\User Library\\Remote Scripts\\ (the CoolSynth installer does it), then
in Live, Settings > Link, Tempo & MIDI, choose "CoolSynth_MiniLab" as a Control Surface with Input "Minilab3 MIDI"
and Output "None".
"""

from _Framework.ButtonElement import ButtonElement
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import MIDI_NOTE_TYPE

PAD_CHANNEL = 9  # channel 10, counted from 0
PAD_NOTES = range(36, 52)  # banks A and B


class CoolSynthMiniLab(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        with self.component_guard():
            # A control with a listener is forwarded to the script, and so is not the track's.
            self._pads = [ButtonElement(True, MIDI_NOTE_TYPE, PAD_CHANNEL, note) for note in PAD_NOTES]
            for pad in self._pads:
                pad.add_value_listener(self._pad_pressed)
        self.show_message("CoolSynth is set up for the MiniLab 3")

    def _pad_pressed(self, value):
        pass  # CoolSynth's MiniLab driver has the pad already

    def disconnect(self):
        for pad in self._pads:
            pad.remove_value_listener(self._pad_pressed)
        ControlSurface.disconnect(self)


def create_instance(c_instance):
    return CoolSynthMiniLab(c_instance)
