"""
CoolSynth's Ableton Live control surface for the Arturia MiniLab 3 (decision D211).

CoolSynth drives the MiniLab itself (its screen, pad lights, knobs and pads), straight from the MiniLab's own MIDI
port. Ableton would also hand the MiniLab's pads to the track as notes (on channel 1, as it gives every note to an
instrument), so a pad would switch CoolSynth's panel and play a note. This control surface listens to the pads' notes
(channel 10, notes 36-51, banks A and B); Ableton keeps messages a control surface uses out of the tracks. It sends
nothing to the MiniLab: the keys, knobs, faders, bend, mod wheel and pedal go on to the track as with any controller.
Like Live's own controller scripts, it arms the selected MIDI track by itself (the pink arm button).

Install: put this folder in Documents\\Ableton\\User Library\\Remote Scripts\\ (the CoolSynth installer does it). Live
loads it by itself (get_capabilities below: Arturia's USB vendor ID, the MiniLab 3's product IDs and model name, as
Live's own MiniLab_3 script claims them). The output port is needed: a script that claims a device starts only once
Live has asked that device for its identity through the output (tested in Live 12.4, D247). Where Live already has a
choice for the MiniLab, choose it by hand: Settings > Link, Tempo & MIDI, "CoolSynth_MiniLab" as a Control Surface
with Input and Output "Minilab3 MIDI".
"""

from _Framework.ButtonElement import ButtonElement
from _Framework.Capabilities import (AUTO_LOAD_KEY, CONTROLLER_ID_KEY, NOTES_CC, PORTS_KEY, SCRIPT, controller_id,
                                     inport, outport)
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import MIDI_NOTE_TYPE

PAD_CHANNEL = 9  # channel 10, counted from 0
PAD_NOTES = range(36, 52)  # banks A and B
ARTURIA_USB_VENDOR = 7285  # 0x1C75
# 0x220B is the one Live's own MiniLab_3 script claims; 0x020B is the MiniLab 3 under Arturia's USB MIDI driver
# (both in that driver's hardware IDs).
MINILAB3_USB_PRODUCTS = [8715, 523]


def get_capabilities():
    # The keys still reach the tracks (NOTES_CC), the pads come to this script. The output is only for Live's
    # identity request; the script sends nothing.
    return {CONTROLLER_ID_KEY: controller_id(vendor_id=ARTURIA_USB_VENDOR, product_ids=MINILAB3_USB_PRODUCTS,
                                             model_name="Minilab3"),
            PORTS_KEY: [inport(props=[NOTES_CC, SCRIPT]), outport(props=[SCRIPT])],
            AUTO_LOAD_KEY: True}


TRACK_WATCHES = ("arm", "input_routing_type", "is_frozen")


class CoolSynthMiniLab(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        with self.component_guard():
            # A control with a listener is forwarded to the script, and so is not the track's.
            self._pads = [ButtonElement(True, MIDI_NOTE_TYPE, PAD_CHANNEL, note) for note in PAD_NOTES]
            for pad in self._pads:
                pad.add_value_listener(self._pad_pressed)
        # Auto-arm (D247), as Live's own controller scripts do it (ableton.v2 AutoArmComponent): the selected MIDI
        # track is armed implicitly (the pink arm button), so the keys play the track you are on.
        self._armable = []
        self._auto_arm_pending = False
        song = self.song()
        song.view.add_selected_track_listener(self._request_auto_arm)
        song.add_exclusive_arm_listener(self._request_auto_arm)
        song.add_tracks_listener(self._on_tracks_changed)
        self._on_tracks_changed()
        self.show_message("CoolSynth is set up for the MiniLab 3")

    def _pad_pressed(self, value):
        pass  # CoolSynth's MiniLab driver has the pad already

    def _watch_tracks(self, watch):
        for track in self._armable:
            for prop in TRACK_WATCHES:
                try:
                    has = getattr(track, prop + "_has_listener")(self._request_auto_arm)
                    if watch and not has:
                        getattr(track, "add_" + prop + "_listener")(self._request_auto_arm)
                    elif not watch and has:
                        getattr(track, "remove_" + prop + "_listener")(self._request_auto_arm)
                except Exception:
                    pass  # a track that was just deleted

    def _on_tracks_changed(self):
        self._watch_tracks(False)
        self._armable = [track for track in self.song().tracks if track.can_be_armed]
        self._watch_tracks(True)
        self._request_auto_arm()

    def _request_auto_arm(self):
        # Live forbids changing the song from inside its notifications: the arming waits for the next tick.
        if not self._auto_arm_pending:
            self._auto_arm_pending = True
            self.schedule_message(1, self._update_auto_arm)

    def _update_auto_arm(self):
        self._auto_arm_pending = False
        song = self.song()
        selected = song.view.selected_track
        midi_tracks = [track for track in self._armable if track.has_midi_input]
        on = getattr(self.application(), "number_of_push_apps_running", 0) == 0  # Push arms tracks itself
        # As Live's own: with Exclusive Arm on, a track armed by hand keeps the keys while another is selected.
        if song.exclusive_arm and selected in midi_tracks and not selected.arm:
            on = on and not any(track.arm for track in midi_tracks)
        for track in midi_tracks:
            want = on and track == selected
            if track.implicit_arm != want:
                track.implicit_arm = want

    def disconnect(self):
        for pad in self._pads:
            pad.remove_value_listener(self._pad_pressed)
        song = self.song()
        song.view.remove_selected_track_listener(self._request_auto_arm)
        song.remove_exclusive_arm_listener(self._request_auto_arm)
        song.remove_tracks_listener(self._on_tracks_changed)
        self._watch_tracks(False)
        for track in self._armable:
            try:
                if track.implicit_arm:
                    track.implicit_arm = False
            except Exception:
                pass
        ControlSurface.disconnect(self)


def create_instance(c_instance):
    return CoolSynthMiniLab(c_instance)
