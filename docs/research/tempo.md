# Tempo

Preliminary research for TODO 25, 2026-11-20, for the user to discuss. Nothing is chosen or built yet.
Line numbers are those of 2026-11-20.

## How CoolSynth handles tempo today

One parameter holds the tempo when no host gives one: `arpInternalTempoBpm`, shown to hosts as "Internal BPM".
- 40 to 240 BPM, default 120, in the Tempo group (`src/parameters/ParameterLayout.cpp` 230-241).
- One for the whole sound, not per layer (D194); a sound carries it, and loading a sound whose arp is on takes
  its tempo (`SynthAudioProcessor.cpp` 1031; `tempo` in the bank files, `BankFile.cpp` 33).

In a DAW, `processBlock` reads the host's play head: tempo, position in beats (ppq) and whether it plays
(`SynthAudioProcessor.cpp` 685-704).
- Whenever the host reports a tempo, the arps, synced LFOs, the step sequencer, Delay, Pump and Stutter run at
  it, playing or stopped (`Arpeggiator.cpp` 227-253, `v3/Part.cpp` 249-253, `v3/SoundUnit.cpp` 390-398).
- While the host plays, the arp's steps and the LFOs lock to its beat position; its start reseeds the random
  numbers (D222); its stop stops the arp until a key starts it again.

In the app, the play head is not read at all (`isStandaloneApp()` skips it, line 685).
- Everything runs at Internal BPM, free-running from the first key.
- JUCE's standalone holder gives the processor no play head on desktop (only iOS IAA does,
  `juce_StandaloneFilterWindow.h` 268-274).

Where the BPM can be set today:
- From a DAW's automation or generic parameter view, or a controller mapped to it.
- From a preset, as above.
- Not on the panel: the info row (`PanelEditor.cpp` 3100) only shows `tempoBpm()`. The throwaway
  AuditionEditor's generic sliders had it, but the panel replaced that editor.
- So in the app the user cannot really change it, except by loading another sound or through the MCP server.

MIDI clock today: none, in or out.
- Clock (0xF8), Start, Stop, Continue and Song Position arrive with the other MIDI in the app (the device
  manager's inputs and the MiniLab driver's `pushLiveMidi`), and `makeEngineMidiEvent` drops them
  (`SynthAudioProcessor.cpp` 72-140). Nothing reads them.
- The plugin declares no MIDI output (`producesMidi()` false, `NEEDS_MIDI_OUTPUT FALSE` in `CMakeLists.txt`),
  so nothing can be sent.

A small mismatch found on the way: the info row shows the host's tempo only while the host plays
(`SynthAudioProcessor.cpp` 712), but the sound uses the host's tempo whenever the host reports one. In a stopped
DAW at 90 BPM the row says 120 while the arp runs at 90.

## What others do

### Plugins with a standalone app

Arturia's apps (Analog Lab, V Collection, Pigments) have one global tempo, set in Audio MIDI Settings, standalone
only.
- Users report Analog Lab does not follow MIDI clock or start/stop
  ([Arturia forum](https://forum.arturia.com/t/setting-bpm-in-al-standalone/3620)).
- Arturia lists the tempo among the standalone's stored preferences
  ([Arturia support](https://support.arturia.com/hc/en-us/articles/16326068259868-How-to-manually-clean-Instruments-FXs-preferences)).
- Pigments' sequencer syncs to the host's tempo in a DAW ([Sweetwater](https://www.sweetwater.com/sweetcare/articles/arturia-pigments-3-quickstart-guide/)).

Vital's standalone runs at a fixed 120 BPM with no control, and users ask for one
([Vital forum](https://forum.vital.audio/t/bpm-in-standalone-mode/2996),
[feature request](https://forum.vital.audio/t/the-ability-to-change-the-internal-clock-tempo/6851)).

Serum 2 and u-he's synths (Diva and the rest) are plugins only, with no standalone app, so they take the host's
tempo ([Serum 2 manual](https://xferrecords.com/manual/serum-2), [Thomann on Diva](https://www.thomann.co.uk/u_he_diva.htm)).

So among plugin makers, a settable tempo in the app is the norm at best; MIDI clock in an app is rare.

### Hardware

The MiniLab 3's arp has its own tempo and Tap Tempo; since firmware 1.1 its Auto sync uses the internal clock
until MIDI clock arrives, then follows it, and keeps that tempo after the clock stops
([Arturia forum](https://forum.arturia.com/t/how-to-sync-an-arpeggiator-with-a-daw-ableton-solve/3039),
[manual](https://manualsnet.com/arturia/minilab-3)).

Korg's minilogue xd has a Tempo knob on the panel and a Clock Source setting: Auto (USB), Auto (MIDI) or
Internal, plus a switch for receiving start, stop and continue ([minilogue xd manual](https://cdn.korg.com/us/support/download/files/a5bba576f00973bf63c78cdeff83a7f7.pdf)).
- The modwave (wavestate family) has Internal, External and Auto; in Auto it falls back to its own tempo when
  clocks stop for more than 500 ms; the tempo sits on a Tap Tempo button
  ([modwave manual, p. 96](https://www.manualslib.com/manual/2157468/Korg-Modvawe.html?page=96)).

Elektron machines set clock and transport send and receive separately, over MIDI and USB, in MIDI Config, Sync;
clock send is off by default ([Elektronauts](https://www.elektronauts.com/t/digitakt-no-clock-out/79192),
[Elektronauts](https://www.elektronauts.com/t/midi-confusion/225503)).

Novation's Circuit Tracks has Clock Rx and Tx switches, both on by default; its own tempo is 40-240 BPM in whole
numbers; it follows 30-300 BPM from outside, and stops if the clock goes away
([Novation](https://userguides.novationmusic.com/hc/en-gb/articles/26207162799506),
[appendix](https://userguides.novationmusic.com/hc/en-gb/articles/25494477003154-Circuit-Tracks-appendix)).

The common pattern: a tempo control, tap, and an Auto mode that follows clock when present and its own tempo when
not; transport (start, stop) as a separate switch.

### MIDI clock in JUCE

JUCE has the messages: `isMidiClock`, `isMidiStart`, `isMidiStop`, `isMidiContinue`, `isSongPositionPointer`
and the makers for each ([MidiMessage](https://docs.juce.com/master/classMidiMessage.html)).

Receiving: 24 pulses per quarter note, each with jitter (a VCV user measured 200-300 samples at 44.1 kHz).
Receivers smooth it with a phase- or delay-locked loop
([JUCE forum](https://forum.juce.com/t/high-accuracy-scheduling-for-midi-only-so-no-audio-interface-or-process-block-callback/63659),
[VCV forum](https://community.vcvrack.com/t/dedicated-midi-clock-sync-module-programmer-needed/6856)).
- Bespoke Synth, open source, uses a delay-locked loop, waits 48 pulses before trusting it, averages a short
  history, rounds the tempo, and obeys Start, Stop, Continue and Song Position as options
  ([MidiClockIn.cpp](https://sources.debian.org/src/bespokesynth/1.3.0%2Bdfsg-3/Source/MidiClockIn.cpp)).
- JUCE timestamps incoming MIDI from its millisecond counter
  ([AudioProcessorPlayer](https://docs.juce.com/master/classAudioProcessorPlayer.html)); CoolSynth already places
  the MiniLab's messages in the block from those times (D159).

Sending: the app's player passes the processor's MIDI out to the default MIDI output
(`juce_StandaloneFilterWindow.h` 583). With the output's background thread running it schedules by timestamp;
otherwise it sends each block's messages at once, which puts clock jitter at block size
(`juce_AudioProcessorPlayer.cpp` 322-333; [JUCE forum](https://forum.juce.com/t/sending-midi-clock-from-standalone-app/8025)).
- A 5-pin MIDI byte takes about 1 ms, so wire clock is never exact; receivers are expected to smooth.

### Tap tempo

Common designs average the last 3 to 8 intervals, start after the third tap, and reset after 2 to 4 seconds of
no taps or after 1.5 times the last interval; averaging 16 taps feels sluggish
([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=3671875),
[Max cookbook](https://music.arts.uci.edu/dobrian/maxcookbook/tap-tempo),
[ArduinoTapTempo](https://github.com/eppfel/ArduinoTapTempo)).

### Ableton Link

Link syncs tempo, beat and phase between apps on the same network, start and stop optionally
([Ableton Link](https://ableton.github.io/link)).
- Licence: GPL version 2 or later; for proprietary software, "contact link-devs@ableton.com"
  ([LICENSE.md](https://github.com/Ableton/link/blob/master/LICENSE.md)).
- CoolSynth's code is MIT, but it is built with JUCE under the Starter licence, a proprietary licence. As far as
  I can tell, shipping Link would mean either releasing the app under the GPL (and JUCE under its AGPLv3 option)
  or asking Ableton for its licence. Not legal advice; worth asking before any work.
- JUCE examples exist ([juce_meets_link](https://github.com/COx2/juce_meets_link),
  [JUCE forum tutorial](https://forum.juce.com/t/ableton-link-tutorial-how-to-build-and-some-tips/31242)).
- Only the app would use it; in a DAW the host already does (Live: Link and MIDI clock out, not clock in, at once,
  [Live manual](https://www.ableton.com/en/live-manual/12/synchronizing-with-link-tempo-follower-and-midi)).

## Options

### A. Tempo on the panel, tap, and the info row fixed (recommended)

A way to set Internal BPM in the app, and tap it in.
- Tap from a panel click and from a MiniLab pad or button; the MiniLab 3 has a Tap button of its own, which the
  driver could take.
- The info row shows the tempo actually used (the mismatch above).
- Perhaps decimals (Circuit's 40-240 whole numbers is the narrow end; an Auto mode later needs decimals).
- Cost: small. The tap logic, the row fix and the MiniLab mapping need no real interface and can be tested now.
  The control itself is UI: in the throwaway UI, a scroll or drag on the BPM in the info row; the real place
  with the real interface.
- Side effects: none on the sound; presets keep their tempo as now.

### B. A plus MIDI clock in, for the app

The app follows a drum machine, a hardware sequencer or another app, as the Korg Auto mode does.
- Auto: own tempo until clock arrives, follow it smoothed (a delay-locked loop, as Bespoke), back to own tempo
  some 500 ms after it stops. Start, Stop, Continue and Song Position drive the arp's grid as a host's transport
  does now, so the same code path (`EngineTransportInfo`) serves both.
- A Prefs switch: Auto or Internal, and whether transport is obeyed.
- Cost: medium. All engine and processor work, testable without the real interface with recorded clock streams
  (steady, jittery, stopping, tempo ramps). The switch can sit in Prefs now.
- Side effects: smoothing lags a tempo change by a beat or so; a jittery source wobbles synced delays unless the
  delay time is smoothed too. Plugins unaffected (the host's tempo wins).

### C. B plus MIDI clock out, and a metronome, for the app

The app leads: an external arp or drum machine follows CoolSynth.
- Clock out only matters with something to start and stop: today the arp runs from the keys, not from a
  transport. So this wants a Play and Stop of the app's own, which is the recorder's and sequencer's (TODO 12).
- A metronome click (and a count-in) is the same story: useful for playing to a click or recording, much less
  for a free arp.
- Cost: medium to large. The MIDI output must be switched on in the build (`NEEDS_MIDI_OUTPUT`), which also
  gives the VST3 and CLAP plugins a MIDI out port, unless limited to the app; clock sent from the audio block with
  the output's background thread running, for timestamped sending. The output choice is Prefs, the Play button is
  UI.
- Side effects: a new port in DAWs (harmless, but visible); clock timing tied to the audio buffer size.

### D. Ableton Link in the app

Tempo and phase shared with Live, phone apps and others on the network, no cables.
- Cost: large, mostly the licence question; the code is a known pattern. Only meaningful once there is a
  transport (TODO 12), and only in the app.
- Side effects: a network library in the app (firewall prompt on first run on Windows), and the licence choice.

## How it ties to the recorder and a sequencer (TODO 12)

The recorder's Play and Record pads would need a tempo for bars, a count-in and quantised loop lengths, and a
transport of its own in the app.
- Option A is needed whatever TODO 12 becomes, and does not depend on it.
- Option B's transport handling would also start and stop a recorder or sequencer.
- Options C and D only pay off once the app has a transport, so they are best decided with TODO 12.

In a DAW none of this changes: the host's tempo and transport keep winning, and the recorder there would follow
the host.
