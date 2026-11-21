# TODO

Open work only. Chosen by the user on 2026-11-19 (D215): items 1-7 and 9-17 except 11 and 12; MPE dropped for good.
Order since then: the controller redesign (C1-C6, D216), JUCE 9 (J1, D217), then the next release (J3). What is done,
and why, is in `DECISIONS_LOG.md` and the git history; decisions taken while the user was away are there too, each
with every option: review them.
After 0.4.0 (the user's choice, 2026-11-20; options: the full bank by ear, the real interface, the parked sound
items, sounds of one's own and tempo): the parked sound items first. Researched and chosen (D229), in this order:
a dedicated Chip generator (20), a kick generator (30), the SEM morph on the SVF and a Museum set of ARP and
Oberheim presets (21), then the presets: the Succession kick, Arcade revoiced, six deep presets in the rooms (22).
Each is designed with the user (options) before it is built. Built on the night of 2026-11-20: the Chip generator,
the Kick generator, the SVF's SEM morph and SID characters, Arcade revoiced, Succession 808 (D231 to D233). The
user's answers to the waiting questions (D235): items 21 to 25, 27, 29 and 30 say what was chosen. Still waiting:
the full bank by ear (3), the interface (24, 26, 27, 29, 31), the Mac (J2).

## Before or right after the first release (0.3.0)

1. The FL Studio knob test failed on 2026-11-19 (D216): the MiniLab stayed in its DAW program with the driver Off,
   and FL's MIDI port doubled every note. Replaced by the controller redesign below.
2. The released installer ran here on 2026-11-19: the VST3 and the app match the release byte for byte. Left: check
   that Ableton finds the installed CoolSynth (its custom VST3 folder is empty since D213, so **Use VST3 Plug-In
   System Folders** must be on).

## Controllers (D216, `docs/design/CONTROLLERS.md`)

Done on 2026-11-19, tested by the suite, not yet in a DAW: C1 (the README), C2 (the first eight parameters as the
panel's Home, the user's choice; gestures were already right), C3 (the duplicate-note filter), and of C4 the exact
drop, the channel-1 hint, the ready setup for both programs, the disconnect, the device-change notifications and the
busy-port fallback.

C4. The FL script linking by itself (D246). Found in FL on 2026-11-21: the first time FL learns the MiniLab's
    hardware ID (its answer to an identity request), it moves the MiniLab to a script that claims that ID, even from
    a script chosen by hand, and Arturia's own script claims it. Ours now claims the same ID besides the port name.
    Seen: ours stays linked across restarts and repeated ID answers; a choice stored in FL (generic, Arturia's, ours)
    is never changed at startup. Not seen: the first ID with both scripts claiming it, as FL keeps the learned ID
    where it could not be found (not in its registry keys or files) and the MiniLab's other ports do not answer. To
    check on a fresh FL (a new Windows account or a clean computer). Also to try: FL's own "Forward CCs to current
    plugin Port 10" script, which hands knobs to a plugin on port 10 without putting the controller on a port.
    Ableton (D247): Live now loads our control surface by itself, seen on 2026-11-21 with the slot emptied. Not seen:
    a MiniLab on product 0x220B (without Arturia's driver, likely on the Mac), where Live's own MiniLab_3 claims it
    too; of two user scripts Live took the first by name, and ours sorts before MiniLab_3, but whether Live's own
    scripts share that order is not known. Also not pressed again by hand: the pads with the output port now set.
C5. The CLAP build with remote control pages: done on 2026-11-19 on JUCE 9, tested by the suite, not yet in a DAW.
    - clap-juce-extensions in `external/`, pinned at 6bde108 (2026-11-06, with its JUCE 9 support); the CLAP is
      `build/CoolSynth_artefacts/<config>/CLAP/CoolSynth.clap`.
    - The pages (`src/plugin/RemoteControlPages`): one per panel page, its four knobs, then Glide, Brightness, Space
      and the master volume; the host is told the open page, and told again when a slot's type or the layer changes
      the pages. An empty slot has no page.
    - The installer puts it in `C:\Program Files\Common Files\CLAP` (its own tick box); the release has a CLAP zip.
C6. Test per DAW: done on 2026-11-20 and 21 in Ableton Live and FL Studio (`docs/test-pass.md`): the duplicate
    filter, the stopped host's tempo, the layer menu at the panel, the MiniLab replugged, the MiniLab in FL with our
    script, the guided setup (C7), the panel at 175 % in both DAWs and at 125 and 150 % as drawn. Left, for want of
    hosts: the CLAP pages (Bitwig or Reaper), which hosts call `IMidiMapping` (a logging build, Reaper above all).
C7. The guided controller setup, redesigned with the real interface (the user, 2026-11-21: it works, the flow is
    wrong). Back and Next on every step. Ask first what the controller has and what the player wants: a row of four
    knobs, a page knob, faders, pads (explain what the pads do here; they may not want them). Say what each control
    is for: knob 5 turns the pages, the most important for getting around the panel. Show a small drawing of the
    panel with the control being asked for marked (a red ring), and say where to look on the controller ("a row of
    four knobs"). The user chose on 2026-11-19 to do this later, after
    the coding left (D217). To try a build in a DAW without a release, make a
    test installer from the Debug build (from `installer`):
    `iscc /DAppVersion=0.3.0 /DBuildDir=..\build\CoolSynth_artefacts\Debug /O<folder> CoolSynth.iss`.

## JUCE, platforms and the next release

J1. JUCE 9.0.5: done on 2026-11-19 (D217). Everything builds, the CLAP too, and the full suite passes (562 s).
J1b. The JUCE 9 review round (D217): done. Fixed: the Save dialog's replace question (off the deprecated
    `showOkCancelBox`), a saved sound's path from a Mac project, CoolSynth letting go of a MiniLab as soon as its
    port is gone, the layer menu tied to the panel, the editor made the JUCE 9 way. Windows MIDI Services stays
    Windows' own, the user's choice. Left: the hardware and DAW checks, under C6.
J2. macOS, done by the user in a Claude Code session on their Mac. The plan, the Windows-only code and the user's
    choices (D234): `docs/design/MACOS.md`; the research: `docs/research/macos.md`. Apple Silicon only, macOS 13 or
    newer, AUv2, VST3 and CLAP, an unsigned .pkg into the user's `~/Library`, the instruments in `~/Music/CoolSynth`
    downloaded by the installer, sse2neon, Clang's default floating point, the audio-thread watch ported with
    fishhook, GitHub's Mac runner. CoreMIDI shares ports, so the MiniLab is never busy but its notes can arrive twice
    (the duplicate filter); its port names must be checked against `ml::isDawPort`.

## The user's ear and hands

3. A listening pass over the whole bank, a card per preset on the page in `tools/listening-pass`: round 1 done on
   2026-11-21, every card answered (`docs/test-pass.md`); round 2, the fixes against how they were, all passed. Mine, through the MCP server and in DAWs: levels, the glow and click-to-load, sounds ringing on after
   a change, the engine pills, Preferences. On the hands list: the velocity curves, Brightness and Space on the
   MiniLab. Retune what the ear flags, then rerun the loudness tool, which is not in the default build:
   `cmake --build build --config Debug --target CoolSynthPresetNormalizer`, then from `build` run
   `CoolSynthPresetNormalizer_artefacts/Debug/CoolSynthPresetNormalizer.exe --apply`.
4. The sampled instruments and the Night Bus presets: all kept on the listening page (2026-11-21), but for High
   Voice and Four-Track House, removed; Vibraphone's click on repeated keys fixed and passed (round 2).

## Sounds and effects

5. Next from the research on the user's reference artists (`docs/research/sound-references.md`): the Pitch effect's
   Tune mode and formant control. The stutter effect is built (D220); its three draft presets in Night Bus (Cut-Up
   Chords, Wheel Stutter, Glitch Pad) passed the user's ear on 2026-11-20.
6. The Museum: the hoover's source is found (D223): the Alpha Juno's factory tone "What the", decoded from Roland's
   bulk dump with the manual's table (`resources/museum-candidates/sources/alpha-juno-what-the.txt`). Next: the
   candidate, converted from these numbers (D104), then the user's side-by-side. The Juno-106 patches still have no
   free factory data (Roland sells its 128 for its own plugin; the dump format is documented, ST-Log, June 1989).
7. The wine glasses as a source for the Grain generator.
18. The added noise (D221, `docs/research/noise-modelling.md`): built on 2026-11-19 as the user chose (a steady level
    with a mute in silence, tape-shaped hiss before Bandwidth, the crackle model, a seed per slot, modulation noise,
    Brown). All passed the user's ear (2026-11-20 and 21); Washed Poly's turned down by a quarter and passed again.
20. Chip sounds and chiptune. The Chip generator is built (D230, D231), Arcade revoiced with it, and the SID's
    filter is an SVF character (D233). Left: the user's ears on all of it (each chip's waves, the drums and
    arpeggios, the named waves, which are my own recipes, the 6581 and 8580 filters, how hard the 6581 distorts).
21. The ARP and Oberheim sounds (D229): the SVF's SEM morph is built (D233). Left: the Museum set (OB-Xa Jump, OB-X
    stabs, an SEM notch pad, the Odyssey's Chameleon bass, an Odyssey sync lead), made as every Museum sound is,
    fitted against reference recordings the user approves; how is in `docs/overnight-2026-11-20.md`.
22. Six deep presets in the rooms (D229), made with the user one at a time (D235): I propose a sound with its idea
    and settings, the user hears it, we adjust, then the next.

## Engine

9. Processing cost, measured on 2026-11-19 (optimised Debug, eight-note chords; `COOLSYNTH_CPU_REPORT=1`): Supersaw
   14.2 % of real time on average, 19 % in its worst block (18.4 % and 26 % before D219). Its share per module, over
   the voices' own 2.7 %: the ladder 4.8 %, the seven saws 2.6 %, the SVF 1.6 %, the rest its second layer and
   effects. Elsewhere FM4 5.3 %, Drive 3.9 %. Done for now (D219): AVX2 gained nothing and the build stays SSE2.
19. Determinism and a processing budget (D222, D224, `docs/research/determinism-and-budget.md`): built on
    2026-11-19 as the user chose. Every preset renders the same bits fresh and after a reset, live and bounced;
    bouncing waits for sounds to load and never fades for load; the arps' New Patterns switch; the test build
    watches the heap, locks, waits, sleeps and files in audio blocks; the worst-case cost test, the voice cap shown
    on the screen and the live emergency. Left: the user's hands on the cap and the emergency (a heavy preset, a
    slow block size), and multi-core by layer if a single sound ever needs more than its share.
23. Profile the heaviest sound there can be and optimise (`docs/research/profiling.md`). The user's choices (D235):
    Windows Performance Recorder and Analyzer, CPU first, before the Mac port so the Mac has a Windows baseline.
    A patch that fills everything (both layers, every slot at its costliest, sixteen voices, every effect slot),
    run by the test program calling `Part` and `InsertSlot` directly; faster without changing the sound, checked
    bit for bit.
11. Streaming from disk, for libraries bigger than the memory. Not chosen on 2026-11-19 (D215).
12. Not chosen on 2026-11-19 (D215). The recorder: the Play and Record pads (Stop ends every sound since D212). Its memory rules:
    `docs/sample-memory-and-tracks.md` (D162).
13. Ladder by ear: done on 2026-11-21, all right as they are (drive headroom, Bass 50 % by default, the modes'
    levels, the self-oscillation's level).
14. If the saw's -79 dB alias floor at A7 ever shows in a mix: a 6-point (quintic B-spline) BLEP, about another
    25 dB for two more samples of latency.

## Interface and code

24. Your own sounds (`docs/research/own-sounds.md`). The user's choice (D235): a full browser, filter columns,
    ratings, audio preview, My Sounds in the DAW's program list and CLAP preset discovery, one browser for sounds
    and samples (with 26 and 27). Mostly the real interface; the file work (Save versus Save as, an edited mark,
    Compare, metadata in the file) can come first.
26. From the user's test pass (2026-11-19): the sample selection wants a better interface (choosing an instrument
    or a file of your own by turning one knob through the whole list is slow); and clicking the LED (the panel's
    lit value) should let you set the value too. With the real interface: the one browser of item 24.
27. Your own samples (`docs/research/own-samples.md`). The user's choices (D235): one browser shared with your own
    sounds (folders, instruments by kind, your files, recents, favourites, preview, search), with the real
    interface; your files levelled by default to the bank's loudness by a gain stored in the sound, switchable.
28. Started on 2026-11-19 (D225): the app's MCP server, switched on in Prefs, registered with Claude Code. Left: a
    tool that moves a controller (the mod wheel, for Wheel Stutter's timing), saving a sound, perhaps the plugins.
    The user's request: an MCP server in CoolSynth, so Claude can read and set the running
    app's settings (parameters, the slots' files, presets), read the flight log and play notes while guiding a test,
    instead of closing the app to edit its saved state. Off by default, this computer only (localhost), probably the
    app and test builds only. Check online how others expose a running program to MCP (a local HTTP or stdio server,
    JUCE examples, security), then options to choose from.
25. Tempo (`docs/research/tempo.md`). The user's choices (D235): a tempo set in the app with tap (a click, a pad,
    the MiniLab's Tap button) and the info row showing the tempo used (fixed); MIDI clock in with an Auto mode
    (its own tempo until clock arrives, the last tempo kept when it stops) following the tempo and the beat
    (Start, Stop, Continue, Song Position) as a DAW's transport does, the transport part switchable in Prefs; the
    tempo is the app's, kept through sound changes, a sound marked to bring its own (a format change). No clock
    out or Link for now: they wait for the app's own transport (item 12). In a DAW the host's tempo still wins.
29. Context menus (`docs/research/context-menus.md`). The user's choices (D235): the full set. On knobs and faders:
    Reset to default, Enter value, Learn, the DAW's items. On a slot: choose type, Copy, Copy with modulation,
    Paste, Clear, Swap with, Reset. On list rows: Load into A or B; on your sounds Rename, Delete (with a
    confirmation), Show in folder. Lock a setting through sound changes; copy a layer between CoolSynths. Copies go
    to the system clipboard as text. Right click always opens a menu, Back is an item in every menu. Opened by right
    click, long press and Shift+F10; each change one undo step.
30. The Succession bass drum. The Kick generator is built (D232) and Succession 808 drafted in Engine Room. The
    user changed the night's choice (D235): Werner's full circuit model of the 808 in wave digital filters. Built
    (D236): the full circuit with its envelope generator, our own root for every element (chowdsp_wdf not needed),
    the resonator's capacitors scaled with the key, within 2.5 % of ngspice renders and close to Fischer's
    recordings; Succession 808 levelled again. Left: the user's ears on the 808 circuit, Succession 808 and the 909
    and Modern models; the open questions in D236 (short decays, oversampling for the hardest Shape, the model's
    first 20 ms duller than the recordings'). The 909 is its circuit too (D237, built D238): the whole drum from the
    service notes, within 1 % of ngspice stage by stage, R8 read to -15 V (D239), checked against shoreline and
    mw909 (D240): Q8 a PNP switch and R495 give the real click, the triangle fitted near 4.9 V the real pitch and
    harmonics. The 909 made cheaper, then priced: postponed by the user until after the whole 808 (D241), both
    now paused (D243); its 33.3 % price stands. The largest saving found: since Q8 is a PNP the click circuit never comes to rest (its C11 test),
    though Q8 and Q9 are back at rest by 50 ms. Open: H2 6 dB over the real units', the noise clock. The 808
    against two real units (`docs/research/kick-808-recordings.md`, 2026-11-20): the attack dull and its click 3 to
    6 dB weak, DECAY 0 out of reach, the sigh settling too fast. Left: those fixes, and Punch Time's glide past the
    stock 6 ms (the user's choice; it beeps now); whether a model is right is measured, not asked of the user.
31. With the real interface (D227, the user's choice): accessibility for what the screens draw (list rows, slots,
    page tabs, cards, the layer screen's chain blocks), so Windows Narrator and UI Automation read and work the whole
    panel, as the knobs, faders, pads and buttons already are. Check with `tools/uia-check.ps1`.
32. The TB-303 (D241, D242): built as a generator, rewritten from Open303 (the user's choice over our own circuit),
    accent on at velocity 100, slide by legato notes at the 303's 22 ms; the Acid preset on it (its hidden default
    Ladder taken out on 2026-11-21, so the 303 plays alone). Checked against
    published figures: Stinchcombe's filter response within 0.1 dB, the slide's RC, the square's duty, no
    self-oscillation. The user's ear passed Acid, its accent and its slide (2026-11-21). Left: a check against a
    real 303's recordings when an owner's turn up;
    whether the 303's own step sequencer (accent and slide per step) comes into the layer's.
33. The whole TR-808 (D241, after the 303): its voices beyond the bass drum, about five circuit families (the
    bridged-T toms, congas, rim and claves on the bass drum's machinery; the snare; the six-oscillator bank for
    cowbell, cymbal and hats; the clap and maracas), as generators and as a kit. Aimed at house use; researched:
    `docs/research/tr-808-voices.md`. Paused by the user (D243), with the 909's cost work (30); its fidelity and
    order to be put to the user when it resumes. Then the 909 made cheaper and priced (30).
34. Parked (the user, 2026-11-21): in the app, the computer's keyboard plays the piano keys, for people without a
    MIDI keyboard. The notes only, not the panel's navigation. The usual layout: a row of letters for the white
    keys, the row above for the black ones, keys for the octave and the velocity (Ableton's computer MIDI
    keyboard, Mixcraft's and Cherry Audio's Musical Typing Keyboard). Typed keys have no velocity, and many
    computer keyboards take only about three keys at once. It must stay clear of the keys the panel already uses
    (the arrows, Enter, Escape, Backspace, 1 to 8 for the pads), and in a DAW the host's own typing keyboard plays.
    To be designed with the user (options) before it is built.


## Standing notes

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because of stale
  incremental-link databases; incremental linking is now off in Debug. If it ever returns, delete the `.ilk`
  files under `build` and build again.
- The engine's specification is in `docs/design/` (DESIGN_V3, REQUIREMENTS_V3, IMPLEMENTATION_PLAN_V3,
  SOUND_BANK_V3); the architecture as built is `docs/architecture.md`.
