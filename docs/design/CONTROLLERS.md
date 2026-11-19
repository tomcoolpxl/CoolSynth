# Controllers in CoolSynth

The design for how MIDI controllers reach CoolSynth, as a plugin in a DAW and as the standalone app. Decided with the user on 2026-11-19 (D216), after the FL Studio test of 0.3.0 found doubled notes, pads playing notes and a MiniLab stuck in its DAW program. It replaces the case-by-case fixes of D211.

## The rule

Every physical control has one owner and one route. The same knob or pad never reaches CoolSynth by two routes, and the sound only gets what is meant for the sound: notes and the playing controls (pitch bend, mod wheel, pressure, sustain, expression).

Everything else follows from that rule:

- In a DAW, the DAW owns generic controllers.
- CoolSynth's driver owns a controller it knows deeply, such as the MiniLab 3.
- In the standalone app, CoolSynth owns everything.

## Why

Research on 2026-11-19 (sources in D216):

- VST3 never hands a plugin raw controller messages. The host translates them through `IMidiMapping`, and hosts differ: Ableton, Cubase, Bitwig and Studio One do it, Reaper's support is doubtful, and FL Studio passes knobs only through a numbered MIDI port, which then delivers every note twice.
- Ableton gives a non-MPE instrument every note on channel 1, so a pad on channel 10 looks like a key.
- The normal, compatible way is the DAW's own mapping onto the plugin's parameters: Ableton's MIDI Map and banks of 8, FL's Link to controller and Multilink, Cubase's Quick Controls, Bitwig's Remote Controls, Reaper's learn, Studio One's focus mapping.
- Page-following knobs are what CLAP's remote control pages are for: the plugin publishes pages of 8 and tells the host which page is open. Bitwig, Reaper, FL Studio and Studio One read them.
- Hardware makers with screens and lights (Akai VIP, Native Instruments) talk to the device directly and leave only the notes to the DAW. The MiniLab 3 has no second port for its controls, so Arturia ships a script per DAW.
- The MiniLab's screen, pad colours and DAW connection are Arturia system-exclusive messages. A DAW does not reliably carry those from a plugin to a device, so the driver sends them itself.

## In a DAW

Generic controllers, the main route:

- The DAW maps controller knobs onto CoolSynth's parameters.
- CoolSynth's parameter order is deliberate: the eight most useful controls first, because Cubase's Quick Controls, Ableton's banks and other hosts' defaults read the list in order.
- Every edit from the panel is a proper gesture (begin, change, end), so the DAWs' "last touched" learn works.
- Parameter IDs stay stable.

CoolSynth's own Learn stays as an extra, the user's choice, as Serum and Pigments do:

- It needs the host to pass controller messages, so it inherits the host's limits.
- In FL Studio it needs the numbered port. CoolSynth drops an exact duplicate note-on (same note, same channel, while that note is already held from the other copy), so FL's doubled delivery plays once.
- The README recommends the DAW's own mapping first, and Learn second.

Page-following knobs:

- CoolSynth also ships as CLAP, with remote control pages: one page per panel page, and `suggest_page` when the panel changes page, so a controller's knobs follow the panel in Bitwig, Reaper, FL Studio and Studio One.
- Built with clap-juce-extensions; the installer installs the CLAP beside the VST3.
- VST3 hosts without pages (Ableton, Cubase) get the fixed first eight parameters.

The sound's playing controls:

- Notes, bend, mod wheel, pressure, sustain and expression come from the DAW as now.

## The MiniLab 3

The driver owns the MiniLab, in the app and in a DAW:

- It opens "Minilab3 MIDI" directly (shared between programs through Windows MIDI Services), connects the DAW program, reads the knobs, faders, encoder, Shift and pads, and draws the screen and pad colours.
- One CoolSynth drives it at a time: in a program the window opened last, between programs the first to take it (a named lock).
- It finds the MiniLab through JUCE's device-change notifications, not by listing the devices every few seconds.
- If the port cannot be opened (Windows without MIDI Services, an exclusive driver), CoolSynth falls back to the MiniLab as a plain controller and says so.

Keeping the pads off the DAW track, in three layers:

- In the plugin, every message the driver owns is dropped from the DAW's input: the DAW program's knob, fader, encoder and Shift controllers, and pad notes 36-51 on channel 10. Exactly, by message, not by timing.
- Where the DAW merges channels (Ableton), CoolSynth tells the player once to set the track's MIDI input to channel 1: the keys play on channel 1 and the pads on channel 10.
- The FL Studio and Ableton scripts stay as an optional extra. The FL script must link to the MiniLab by itself; on this computer FL had fallen back to its generic controller, which has to be fixed.

With the driver switched Off:

- No message switches the MiniLab's program; only Shift + pad 3 does. So CoolSynth has ready setups for both the Arturia program and the DAW program, and the MiniLab works as a plain controller in whichever program it is in.
- The disconnect message is sent in both forms the scripts use (`02 00 40 6A 20` and `02 02 40 6A 20`), after clearing the screen.

## In the standalone app

- The app owns its MIDI devices, so CoolSynth sees every controller's messages as sent, with their channels.
- Generic controllers: the ready setups for known controllers, the guided setup and right-click Learn, kept for the whole computer.
- The MiniLab: its driver, as in a DAW.

## Open questions to test

- Which hosts call `IMidiMapping` today (Reaper especially), with a logging build.
- Whether the duplicate-note filter is enough for FL's doubling in every case.
- The track-input channel setting in DAWs other than Ableton.
- The two disconnect forms on the device.
