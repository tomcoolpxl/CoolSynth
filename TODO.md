# TODO

Open work only. Chosen by the user on 2026-11-19 (D215): items 1-7 and 9-17 except 11 and 12; MPE dropped for good.
Order since then: the controller redesign (C1-C6, D216), JUCE 9 (J1, D217), then the next release (J3). What is done,
and why, is in `DECISIONS_LOG.md` and the git history; decisions taken while the user was away are there too, each
with every option: review them.

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
J2. macOS, for the user's Mac (the user comes back to it): AU, VST3 and CLAP on macOS; an installer package with
    signing and notarisation; the MiniLab driver on CoreMIDI; the FL Studio and Ableton script folders on macOS; the
    sample library's place; a macOS runner in the CI. In Logic an AU gets the controller's messages as sent.
    From the JUCE 9 review: `CMAKE_OSX_DEPLOYMENT_TARGET` 12.0 or later (Xcode 27 drops older); CoreMIDI ports are
    shared, so the MiniLab is never busy but its notes can arrive twice (the duplicate filter); the MiniLab's port
    names on macOS must be checked against `ml::isDawPort`; the standalone app gains JUCE 9's lower CoreAudio latency.
J3. The next release, after C6: the version in `CMakeLists.txt` (the user picks the number), the release notes in
    `docs/releases/`, the tag. It carries the controller redesign, JUCE 9 and the CLAP build.

## The user's ear and hands

3. A listening pass over the whole bank: levels, the glow and click-to-load, sounds ringing on after a change,
   the engine pills, Preferences, the velocity curves, Brightness and Space, every preset's macros (D177), on the
   MiniLab too. Retune what the ear flags, then rerun the loudness tool, which is not in the default build:
   `cmake --build build --config Debug --target CoolSynthPresetNormalizer`, then from `build` run
   `CoolSynthPresetNormalizer_artefacts/Debug/CoolSynthPresetNormalizer.exe --apply`.
4. The user's verdicts on the sampled instruments (the audition page, `audition/index.html`; start the
   `audition` preview server) and on the Night Bus presets (`tools/audition/make_nightbus.py` renders them).

## Sounds and effects

5. Next from the research on the user's reference artists (`docs/research/sound-references.md`): the stutter
   effect; then the Pitch effect's Tune mode and formant control.
6. The Museum: the hoover and Juno-106 patches still have no published source.
7. The wine glasses as a source for the Grain generator.

## Engine

9. Processing cost. The VA generator: Supersaw with eight-note chords takes 36 % of real time (worst block 60 %),
   so a 16-voice Supersaw would reach the limit. Per module (`COOLSYNTH_CPU_REPORT=1`,
   `tests/V3ModuleCostReport.cpp`, eight notes, optimised Debug): the ladder about 8 %, FM4 about 7 % (a faster
   sine gains 6 %, D140), Drive about 6 %, Comb under 4 %, the VA saw under 2 %.
10. A sampled voice whose one-shot recording has ended holds its voice until its release is over (up to 20 s on
    bells and mallets): nothing is lost, older voices go first, but it costs processing.
11. Streaming from disk, for libraries bigger than the memory. Not chosen on 2026-11-19 (D215).
12. Not chosen on 2026-11-19 (D215). The recorder: the Play and Record pads (Stop ends every sound since D212). Its memory rules:
    `docs/sample-memory-and-tracks.md` (D162).
13. Ladder by ear: drive headroom, the bass default, the mode level matching, and whether the self-oscillation
    level (rms 0.15 at full resonance) wants an output lift.
14. If the saw's -79 dB alias floor at A7 ever shows in a mix: a 6-point (quintic B-spline) BLEP, about another
    25 dB for two more samples of latency.
15. Measurement-style regressions still missing: THD of a sine through the ladder at low resonance below a stated
    bound; an attack reaching full level within the programmed time.

## Interface and code

16. The "Your keyboard" line over the screen's keys (D203) shows only while the MiniLab is connected; it should
    take any keyboard's own key count.

## Standing notes

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because of stale
  incremental-link databases; incremental linking is now off in Debug. If it ever returns, delete the `.ilk`
  files under `build` and build again.
- The engine's specification is in `docs/design/` (DESIGN_V3, REQUIREMENTS_V3, IMPLEMENTATION_PLAN_V3,
  SOUND_BANK_V3); the architecture as built is `docs/architecture.md`.
