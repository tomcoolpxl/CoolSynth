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

C4. Left of the MiniLab work: the FL script linking by itself. Arturia's own FL script claims the MiniLab by its
    hardware ID (`supportedHardwareIds=00 20 6B 02 00 04 04`), ours by port name, and FL had fallen back to its
    generic controller; FL documents no rule for which wins. Also to try: FL's own "Forward CCs to current plugin
    Port 10" script, which hands knobs to a plugin on port 10 without putting the controller on a port.
C5. The CLAP build with remote control pages: done on 2026-11-19 on JUCE 9, tested by the suite, not yet in a DAW.
    - clap-juce-extensions in `external/`, pinned at 6bde108 (2026-11-06, with its JUCE 9 support); the CLAP is
      `build/CoolSynth_artefacts/<config>/CLAP/CoolSynth.clap`.
    - The pages (`src/plugin/RemoteControlPages`): one per panel page, its four knobs, then Glide, Brightness, Space
      and the master volume; the host is told the open page, and told again when a slot's type or the layer changes
      the pages. An empty slot has no page.
    - The installer puts it in `C:\Program Files\Common Files\CLAP` (its own tick box); the release has a CLAP zip.
C6. Test per DAW (Ableton, FL Studio, Reaper, and Bitwig or Studio One if available): which hosts call `IMidiMapping`
    (a logging build), the duplicate filter, the channel-1 setting, the guided setup step by step (never tested by
    hand yet), the MiniLab in both programs, and the CLAP pages. From J1b: unplug and replug the MiniLab (its screen
    should let go at once), the layer menu (a right click on a layer panel) opening at the panel in two DAWs, and the
    panel at Windows scaling 125 % and 150 % in the app and in a DAW (the CLAP's editor too: clap-juce-extensions
    has an open report of a clipped editor at such scaling). The user chose on 2026-11-19 to do this later, after
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

3. A listening pass over the whole bank: levels, the glow and click-to-load, sounds ringing on after a change,
   the engine pills, Preferences, the velocity curves, Brightness and Space, every preset's macros (D177), on the
   MiniLab too. Retune what the ear flags, then rerun the loudness tool, which is not in the default build:
   `cmake --build build --config Debug --target CoolSynthPresetNormalizer`, then from `build` run
   `CoolSynthPresetNormalizer_artefacts/Debug/CoolSynthPresetNormalizer.exe --apply`.
4. The user's verdicts on the sampled instruments (the audition page, `audition/index.html`; start the
   `audition` preview server) and on the Night Bus presets (`tools/audition/make_nightbus.py` renders them).

## Sounds and effects

5. Next from the research on the user's reference artists (`docs/research/sound-references.md`): the Pitch effect's
   Tune mode and formant control. The stutter effect is built (D220); its three draft presets in Night Bus (Cut-Up
   Chords, Wheel Stutter, Glitch Pad) wait for the user's ear, with the listening pass (item 3).
6. The Museum: the hoover's source is found (D223): the Alpha Juno's factory tone "What the", decoded from Roland's
   bulk dump with the manual's table (`resources/museum-candidates/sources/alpha-juno-what-the.txt`). Next: the
   candidate, converted from these numbers (D104), then the user's side-by-side. The Juno-106 patches still have no
   free factory data (Roland sells its 128 for its own plugin; the dump format is documented, ST-Log, June 1989).
7. The wine glasses as a source for the Grain generator.
18. The added noise (D221, `docs/research/noise-modelling.md`): built on 2026-11-19 as the user chose (a steady level
    with a mute in silence, tape-shaped hiss before Bandwidth, the crackle model, a seed per slot, modulation noise,
    Brown). Left: the user's ear on the presets with hiss or crackle (Bedroom, Basement, Night Bus), with the
    listening pass (item 3).
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
13. Ladder by ear: drive headroom, the bass default, the mode level matching, and whether the self-oscillation
    level (rms 0.15 at full resonance) wants an output lift.
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
    harmonics. Next: the 909 made cheaper, then priced. Open: H2 6 dB over the real units', the noise clock. Left:
    the user's ears on both circuits, Succession 808 and Modern.
31. With the real interface (D227, the user's choice): accessibility for what the screens draw (list rows, slots,
    page tabs, cards, the layer screen's chain blocks), so Windows Narrator and UI Automation read and work the whole
    panel, as the knobs, faders, pads and buttons already are. Check with `tools/uia-check.ps1`.


## Standing notes

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because of stale
  incremental-link databases; incremental linking is now off in Debug. If it ever returns, delete the `.ilk`
  files under `build` and build again.
- The engine's specification is in `docs/design/` (DESIGN_V3, REQUIREMENTS_V3, IMPLEMENTATION_PLAN_V3,
  SOUND_BANK_V3); the architecture as built is `docs/architecture.md`.
