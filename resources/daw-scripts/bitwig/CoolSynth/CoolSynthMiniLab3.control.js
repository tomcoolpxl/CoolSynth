// CoolSynth's Bitwig Studio controller script for the Arturia MiniLab 3 (D249).
//
// CoolSynth drives the MiniLab itself (its screen, pad lights, knobs and pads), straight from the MiniLab's own MIDI
// port. Bitwig's own MiniLab 3 script would also take the pads: it launches clips with them, so a pad that switches
// CoolSynth's panel also starts the transport and records into an empty slot of an armed track. This script takes the
// MiniLab instead. Its note input passes the keys, knobs, faders, bend, mod wheel and pedal on to the tracks and keeps
// the pads' notes (channel 10) away from them. It sends nothing to the MiniLab.
//
// Install: put this file in Documents\Bitwig Studio\Controller Scripts\CoolSynth\ (the CoolSynth installer does it).
// Bitwig finds the MiniLab by its port name. Where Bitwig already set up its own Arturia MiniLab 3 controller, remove
// that one in Settings > Controllers and add "CoolSynth MiniLab 3" (vendor CoolSynth).

loadAPI(17);

host.defineController("CoolSynth", "CoolSynth MiniLab 3", "1.0", "e9cc1e6c-ebc2-4e5e-97e7-55cda1883087", "tomcoolpxl");
host.defineMidiPorts(1, 0);

// The MiniLab's DAW port, as Bitwig's own MiniLab 3 script names it on each system.
if (host.platformIsLinux())
    host.addDeviceNameBasedDiscoveryPair(["Minilab3 Minilab3 MIDI"], []);
else
    host.addDeviceNameBasedDiscoveryPair(["Minilab3 MIDI"], []);

function init()
{
    // Every message of the fifteen other channels, and channel 10's controllers, bend and pressure: only channel 10's
    // notes stay out (no 89 or 99 mask), which on the MiniLab are the pads.
    var port = host.getMidiInPort(0);
    port.createNoteInput("MiniLab 3",
        "?0????", "?1????", "?2????", "?3????", "?4????", "?5????", "?6????", "?7????", "?8????",
        "A9????", "B9????", "C9????", "D9????", "E9????",
        "?A????", "?B????", "?C????", "?D????", "?E????", "?F????");
    // The pads come here and stop: CoolSynth's MiniLab driver has them already.
    port.setMidiCallback(function (status, data1, data2) {});
    host.showPopupNotification("CoolSynth is set up for the MiniLab 3");
}

function exit()
{
}
