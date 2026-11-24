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
    scripts share that order is not known. The pads with the output port set: checked by the user's hands. The
    script now arms the selected MIDI track by itself, as Live's own controller scripts do.
    Macro names in hosts (D248): follow the sound in Live (VST3), FL (CLAP) and Bitwig (CLAP); Zrythm reads them
    once. Not seen: a reopened project in Live (needs a Live that saves), FL's VST3, Reaper, Cubase.
    Bitwig (D249): our own controller script takes the pads; with Bitwig's own MiniLab 3 script they launched and
    recorded clips. Not seen: which one Bitwig sets up by itself on a computer that never had its own.
C5. The CLAP build with remote control pages: done on 2026-11-19 on JUCE 9; seen in Bitwig on 2026-11-21 (every
    page listed, the open page following the panel). The Arp page's knobs read "Layer A ..." there: the arp's
    parameters are now named "A Arp Rate" and so on, like "A Glide" (the IDs unchanged; not looked at again in
    Bitwig, the user's choice).
    - clap-juce-extensions in `external/`, pinned at 6bde108 (2026-11-06, with its JUCE 9 support); the CLAP is
      `build/CoolSynth_artefacts/<config>/CLAP/CoolSynth.clap`.
    - The pages (`src/plugin/RemoteControlPages`): one per panel page, its four knobs, then Glide, Brightness, Space
      and the master volume; the host is told the open page, and told again when a slot's type or the layer changes
      the pages. An empty slot has no page.
    - The installer puts it in `C:\Program Files\Common Files\CLAP` (its own tick box); the release has a CLAP zip.
C6. Test per DAW: done on 2026-11-20 and 21 in Ableton Live and FL Studio (`docs/test-pass.md`): the duplicate
    filter, the stopped host's tempo, the layer menu at the panel, the MiniLab replugged, the MiniLab in FL with our
    script, the guided setup (C7), the panel at 175 % in both DAWs and at 125 and 150 % as drawn. Bitwig on
    2026-11-21: the CLAP pages, the macro names, the MiniLab with our script (D249). Left, for want of a host: which
    hosts call `IMidiMapping` (a logging build, Reaper above all).
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
    Done (D272): the first build (app, VST3, CLAP, both test programs), the three folders and plugin.options, the
    whole test suite passing, the audio-thread watch (fishhook), the unsigned .pkg (`installer/macos/package.py`), CI
    on every push (`macos.yml`) and the Mac in the release (`release.yml`). Left, in MACOS.md's order: every room
    against Windows renders; AUv2 and auval; pluginval and clap-validator; Logic, GarageBand, FL Studio, Reaper,
    Bitwig (Ableton's VST3 loads, plays and shows its panel); sse2neon and the helper threads in the audio workgroup; the voice cap's costs on the Mac;
    the MiniLab; the knobs' wheel step with a Mac mouse and trackpad; the installer on a clean account.

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
    filter is an SVF character (D233). All of it passed the user's ears (2026-11-20, the SCC tables on the 21st).
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
    recordings; Succession 808 levelled again. The user's ears passed the 808, the 909, Modern and Succession 808
    (2026-11-21, "amazingly good"). Left from D236: oversampling for the hardest Shape. The 909 is its circuit too
    (D237, built D238): the whole drum from the service notes, within 1 % of ngspice stage by stage, R8 read to -15 V (D239), checked against shoreline and
    mw909 (D240): Q8 a PNP switch and R495 give the real click, the triangle fitted near 4.9 V the real pitch and
    harmonics. The 909 made cheaper, then priced: postponed by the user until after the whole 808 (D241, kept
    in D250); its 33.3 % price stands. The largest saving found: since Q8 is a PNP the click circuit never comes to rest (its C11 test),
    though Q8 and Q9 are back at rest by 50 ms. Open: H2 6 dB over the real units', the noise clock, and a 909 hit
    after a reset differs from a fresh one from its first sample (found 2026-11-21 by `V3KickTests`; no preset uses
    the 909, so the determinism tests do not see it). For those checks, a third 909 the user found (2026-11-22): one
    unit's long takes per voice in a REAPER session from 2020, the kick also as its own accented take, knob settings
    not noted; filed (local, not in git) at `tools/references/tr909-reaper/TR909Sampled.7z`. The 808's
    gaps against two real units closed (D251, `docs/research/kick-808-recordings.md`): the trigger stage as drawn,
    the standard 1N4148, the voice's amp opening at once for the circuits, Decay down to VR6 = 0, Punch Time's glide
    (a bend beside Q43), C39 and the TONE taper fitted, made cheaper, Succession 808 kept and levelled. Left open
    there: both ends of TONE about 1 dB darker than the units, Audiorealism's slower and shallower sigh, Fischer's
    DECAY 0 shorter than the circuit's floor. Whether a model is right is measured, not asked of the user.
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
    Its code made cheaper (the user, 2026-11-21), with the 909's cost pass after the whole 808: priced at 1.36 % a
    voice but never optimised. First bit for bit (the quarter-band filter's 12 states by a ring index, not shifted
    each core sample; work fixed within a block lifted out), then whether a polyphase half-band in place of the
    twelfth-order elliptic is worth its slightly different sound.
33. The whole TR-808 (D241, D250), resumed after 0.5.0: all sixteen sounds from its eleven circuits, aimed at
    house use, each its full circuit as the bass drum is (ngspice stage by stage, then Fischer's recordings);
    researched: `docs/research/tr-808-voices.md`. Three family generators that fill one layer's three slots:
    808 Drums (bass drum, toms and congas, rim shot and claves), 808 Snare & Clap (snare, clap, maracas), 808 Metal
    (cowbell, cymbal and hats on six running oscillators). Each instrument at its General MIDI key, one circuit
    each, struck while it rings; the closed hat cuts the open.
    The bass drum's gaps: done (D251). 808 Drums: built (D252, D253), the Kit play mode, the toms and congas and the
    rim shot and claves as their circuits, within 2 % of ngspice (the rim shot 4 to 7 %) and of both units' pitch
    and decay (`docs/research/808-drums.md`). Left open there: at full accent every voice runs 1.1 to 4.5 dB over
    page 14's accented amplitude, the toms' pitch drop larger than Audiorealism's accented hits; no recording at a
    known accent level to judge it by; kept as drawn (the user, D253). 808 Metal: built (D254, D255), the cowbell,
    cymbal, open and closed hat on one bank of six oscillators, their envelopes within 3 % of ngspice and their
    spectra within about 1 dB (`docs/research/808-metal.md`); the cowbell tuned as both units (554 and 825 Hz).
    Left open there: the open hat rings 6 to 32 % longer than both units, waiting for the accent check below;
    unaccented every voice is some 20 to 35 % under page 14's amplitude. The accent check, the user's idea: done
    (D258, `docs/research/808-accent.md`): both sets struck at 4.5 to 5 V, a real 808's top accent (808 From Mars,
    page 14's ratios) at 11.5 V here, now the Accent knob's top in all four 808 generators; no earlier fit was a level
    effect. The open hat's decay fitted to both units (C60 8 uF, R134 82 kOhm), the clap's tail (C139 39 nF), the
    claves' low thump fixed (their own rest), the unaccented trigger raised to 4.7 V (where the units' read, and
    the rim shot's decay with them), IC20 fitted to both units' rim shot and claves pitch (R312 1.07 kOhm; the 1.7
    kHz ring's first 5 ms stay some 5 dB slow). Found and fixed: the overload rule cut whole Kit voices; it now
    takes a kit's quietest ringing drum, and CoolSynthRender renders offline (D258). 808 Snare & Clap:
    built (D256, D257), the snare as changed (173 and 334 Hz, as both units, no fit), the clap's four claps from
    IC23 and its reverb, the maracas; within 4 % of ngspice (the switching nodes by their envelopes) and of both
    units' timing and spectra (`docs/research/808-snare-clap.md`), with C140, C144, TM3 and IC21 as the user chose.
    Left open there: the clap's late tail 4 to 12 dB under both units after 120 ms (above); the accented snare's
    2.8 dB over page 14 settled by the accent's top. The stock 808 Kit in Studio 1982: done (D260), every knob at
    12 o'clock and every Level full as page 14 measures, its macros Tone, Tune, Decay and Accent starting halfway.
    Found there: the whole kit's worst block 92 % of real time at 48 kHz in the Debug build during a dense fill (35 %
    on average), so live a fill may shed its quietest drum: the 808 kits' strike blocks to be made cheaper. Done
    (D262): the solver's analysis on plain pointers (the same bits, a sixth of the time) and the hats and cymbal on
    their tails 0.25 ms after the trigger, as the user chose; what a strike still adds is its pulse on the full
    circuits. Sixteen drums ringing at once stay over a block, struck or not (the overload rule's case).
    Each voice designed with the user (options) before it is built. The 808 Metal at 96 kHz: done (D259), the voices
    near 96 kHz at any host, so it costs there what it does at 48 kHz (29 % of real time, was 46); its five VCAs kept
    running and its cowbell at 100 nV, as the user chose. Then the 909 made cheaper and priced (30), and the 303's
    code made cheaper (32).
34. Parked (the user, 2026-11-21): in the app, the computer's keyboard plays the piano keys, for people without a
    MIDI keyboard. The notes only, not the panel's navigation. The usual layout: a row of letters for the white
    keys, the row above for the black ones, keys for the octave and the velocity (Ableton's computer MIDI
    keyboard, Mixcraft's and Cherry Audio's Musical Typing Keyboard). Typed keys have no velocity, and many
    computer keyboards take only about three keys at once. It must stay clear of the keys the panel already uses
    (the arrows, Enter, Escape, Backspace, 1 to 8 for the pads), and in a DAW the host's own typing keyboard plays.
    To be designed with the user (options) before it is built.
35. CoolSynth 808 (D263, D264): decided as a standalone app first, on the mockup's panel and Under the Lid, the
    engine moved to its own repository for both apps. Now: the specification `docs/design/COOLSYNTH_808.md`
    is approved; the engine is its own repository, `coolsynth-engine` (D265). The sequencer is built there (D266,
    `src/synth/StepSequencer`, its playback; the app's recording, chain keys, clock and handing it edits come with
    the app). The app is built overnight (D267): `tomcoolpxl/CoolSynth808` at `E:\github\CoolSynth808`, the panel,
    lanes, Under the Lid's board, projects, undo, MIDI clock in and out, recording, MIDI drag and a library of 56
    beats, tested offline. A MIDI controller works on it with CoolSynth's controller layer, ported (D268), on a
    first scheme after the user's sketch. A code review of it all is done and fixed (D269); from it, the engine
    now holds one layer for the 808 (D270: 150 MB instead of 707), and the controller has its own MIDI input
    (D271), still to be tried with a real controller beside a second MIDI source. The controller scheme stays as
    built and the library is a shelf with audition, rename and the Recycle Bin (D273); an audition comes in on the
    next bar line (D274). The whole library is heard by ear and okay, two beats fixed (D276), and 22 starter beats
    for modern genres are added (D277). Waiting on the user: their review of the app, and the controller input with
    hardware. The history below is the mockups' round.
    The 808 engine as a plugin of its own. Chosen: the 1980 sequencer plus a length and
    shuffle per drum, MIDI drag, per-step locks, flams and sub-steps, chance and conditions; a panel whose step row
    opens into a lane per drum, and Under the Lid (the live circuit, with the classic mods as trimmers) as the
    second view; own colours, four themes for the vector panel. Mockups: `docs/design/mockups/coolsynth-808.html`
    holds the vector panel, its lanes and two tries at Under the Lid in the four themes (no sound; its scope traces
    and currents are drawn, not the engine's): the first as boxes per stage, the second, asked for by the user, the
    whole board as one reduced schematic with the current moving on its wires and their voltage as a tint. The
    user chose the second to refine, with each drum complete on a click: the bass drum is drawn from its netlist
    with rails, ngspice's own currents and voltages on every wire (`tools/kick808/mockup_currents.py`), a time bar
    and a check of the drawing against the netlist. Every other drum is drawn the same way now (D278), each its own sheet (from
    `tools/tr808/*.cir`, each needing its own recording); the reduced board plays from the recordings too (D280). The realistic panel is still to draw. A UX and UI review is written (D283, `docs/design/COOLSYNTH_808_REVIEW.md`); three layouts follow as mockup views in the app.
    Before a release: the name in the trademark registries, and the line that says whose mark
    TR-808 is.
    Since then (D307 to D329): themes, the own Options window, the panel and Under the Lid live from the engine,
    Lift the Lid with the CPU board, every sheet live in real time, the CPU sheet, the VST3 (tried in FL Studio),
    learnable drum notes. Parked, the user's asides and what was left, so none is lost (2026-11-24):
    - The VST3 in Ableton Live and Bitwig (FL Studio done, D328).
    - The Release build's run-time cost against the Debug build's, not yet measured (D327; built in 183 s, D339).
    - The library's load key says "pattern 1" where the panel says "01" (seen fixing its width, D339).
    - The WAV export's drum files could come from the engine's per-drum taps in one render instead of one render a
      drum (D343's taps; today each drum's file is its own render with the mask).
    - FL Studio lists a long run of "MIDI CC #0..." parameters after the VST3's 28 (JUCE's VST3 MIDI CC emulation,
      seen trying D343 in FL); hiding them may stop host CCs reaching the controller layer, so it needs a look first.
36. CoolSynth 303 (D275): a third app, `tomcoolpxl/CoolSynth303`, the TB-303 as its circuit in `coolsynth-engine`
    on the 808's solver; the plan is `docs/design/COOLSYNTH_303.md`. The stock 1982 circuit, each stage within a few
    percent of ngspice, then the trimmers fitted to Din Sync's real unit (`tools/references/tb303-dinsync`, local)
    and the published figures; the BA662 as its transistors; the gate as Schmidt measured. It replaces the Open303
    generator (TODO 32) in CoolSynth once it passes. The netlists in the engine's `tools/tb303`: the VCO and the
    VCF are done and calibrated by page 7 (`docs/research/tb-303-circuit.md`, its netlists section; R110 and R111
    corrected to 22k). The envelope, the VCA (the BA662 as its transistors) and the whole voice (`voice.cir`) are
    done; against Din Sync's unit, with TM3 fitted at 0.35, each knob alone matches within about 1 to 3 dB and the
    accent's loudness within 1 dB after its first 25 ms; the square too, with C11 fitted at 0.82 uF; all 25 sets
    mostly within 1 dB but for set B's resonance at a quarter turn (open); the rail is 11.85 V, not 12. How it runs
    (D279): the VCO's circuits with the reset as an exact event and a polyBLEP; the filter's resonance loop in one
    solve; 192 kHz, then aliasing and cost measured and put to the user. The filter runs in the engine
    (`Tb303Filter.h`) within 1.5 to 5 % of ngspice, at about 40 % of one core at 192 kHz with the chord method
    (D281); it runs on a core of its own beside the other tracks. The VCO runs too (`Tb303Oscillator.h`, D290):
    pitch within 0.1 cent of ngspice, the saw's and square's harmonics within 0.3 dB, the square's shaper at 4x
    (aliases 72 dB down at the top note), about 25 % of a core. The envelope and the VCA run too
    (`Tb303Envelope.h`, `Tb303Vca.h`, D293): the VCA's output within 0.6 % of ngspice's. The voice runs at 96 kHz
    (D294, the user's choice), every stage as accurate as at 192 kHz, about 73 % of one core. The whole voice is
    wired (`Tb303Voice.h`, D295): within 0.01 cents and about 0.5 dB of `voice.cir`, and as close to Din Sync's unit
    as `voice.cir` itself. Next: the voice played by the step sequencer in CoolSynth (in place of Open303), then the
    app. Open: the filter's 1.5 to 5 % against `vcf.cir`; set C2's resonance at a quarter turn. The interface:
    three layouts as mockups for the user to choose from (D296, D299, `docs/design/mockups/coolsynth-303-layouts.html`;
    the frame in `docs/research/tb-303-app.md`); the charcoal with the keyboard as buttons or a piano (2.1, 2.2, D300); its keyboard is the piano (D301); 2.3 spans three octaves, the step panel down to its locks (D302). The app's first working version on 2.3, in
    `tomcoolpxl/CoolSynth303` (D304); then MIDI clock in, Mods (the trimmers), Drag MIDI and a VST3 run by the host's
    transport (D305); the Devil Fish, on or off in the Mods panel (D309); Under the Lid, the whole voice live (D310).
    The listening pass against Din Sync's unit (D324): the note's start, RESONANCE's track and its loop fitted.
    Done on 2026-11-24: the MCP server (D332, port 47633); the Release build optimised as a whole (no faster for the
    303, kept to match the 808), AVX2 left out; the output's soft saturation (the user's pick); the Devil Fish swept
    through the server (D336): clean, Filter FM's direction turned over to brighten as Whittle's; a start-up crash
    fixed on the way. The library (D338): 30 original lines and 12 sounds on a shelf, built; the user judges them by
    feel. Tried in FL Studio: it loads and plays; Drag MIDI unproven (the user may try it by hand); playback
    started with a 118 % load spike. The rest's settle taken out of the note's first block (D340); left, each note's
    attack (85 to 90 % of a block, the first after rest some 105 %) on a steady 75 %. The user's pick: both, the
    same sound made faster first (the gate's edge, then the steady hot spots), then an accuracy setting as the
    808's. Done so far (D341): the solver's products the same bits and faster, the follower warm-started, the voice
    76 % to some 62 %. The research is in `docs/research/solver-speed.md`; the user picked all four offered (the
    explicit inverse, low-rank updates at the attack, a 1-ulp SIMD exp, SIMD across voices), each to be measured
    against the 10 pV reference, and warned that CoolSynth already has some parallelism. The user's rule: never
    throw a speed-up away; one that costs accuracy becomes a level of the accuracy setting, for all three apps; SIMD
    across voices (the same bits) goes in whenever it works. Done (D345): the -90.5 dB found against the reference
    was the pitch circuit's 100 nV (a 0.0005-cent drift); every 303 circuit now at 10 nV (-128.5 dB rms), the chord
    method's kept inverse on; the app plays the first line at 57.5 % of a core (74 % that morning). Done (D347): SIMD
    across voices, the same bytes, for the 808's toms (no shipped preset plays a circuit in several voices): only 3 to
    4 % off two or three toms, the Newton solve being most of a sample; the Newton solve in lanes left for later.
    Done (D346): low-rank updates at the attack, in Best (the first block from rest 8.9 ms to 7.85, as accurate);
    the 1-ulp SIMD exp built as a bench and kept, not used (0.7 % of the voice). Next: the accuracy levels (100 nV
    the first lighter one). For the 808 and CoolSynth: the chord method on their circuits (they use
    plain Newton), measured against their own tight references. The Devil Fish further by ear if the user wants (it
    is ours, the name stays). The look (D348): the app found a bit bland; seven mockups on one page
    (`docs/design/mockups/coolsynth-303-redesign.html`): the app now, a redesign, the 808's four skins, and an
    all-silver TB-303 face with the 808's knobs; the layout kept. The user picked 2, the redesign, one look (no skins
    yet). Next: the JUCE app drawn as mockup 2.


## Standing notes

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because of stale
  incremental-link databases; incremental linking is now off in Debug. If it ever returns, delete the `.ilk`
  files under `build` and build again.
- The engine's specification is in `docs/design/` (DESIGN_V3, REQUIREMENTS_V3, IMPLEMENTATION_PLAN_V3,
  SOUND_BANK_V3); the architecture as built is `docs/architecture.md`.
