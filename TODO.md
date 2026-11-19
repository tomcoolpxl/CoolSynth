# TODO

Open work only. Chosen by the user on 2026-11-19 (D215): items 1-7 and 9-17 except 11 and 12; MPE dropped for good. What is done, and why, is in `DECISIONS_LOG.md` and the git history; decisions taken while the
user was away are there too, each with every option: review them.

## Before or right after the first release (0.3.0)

1. The FL Studio knob test failed on 2026-11-19 (D216): the MiniLab stayed in its DAW program with the driver Off,
   and FL's MIDI port doubled every note. Replaced by the controller redesign below.
2. The released installer ran here on 2026-11-19: the VST3 and the app match the release byte for byte. Left: check
   that Ableton finds the installed CoolSynth (its custom VST3 folder is empty since D213, so **Use VST3 Plug-In
   System Folders** must be on).

## Controllers (D216, `docs/design/CONTROLLERS.md`)

C1. README: the DAW's own mapping first (FL: Link to controller or Multilink, no port), CoolSynth's Learn second.
C2. Parameter order and gestures: the eight most useful parameters first, stable IDs, every panel edit a proper
    begin/change/end gesture.
C3. An exact duplicate-note filter for FL's doubled delivery when a port is set.
C4. The MiniLab: drop every driver-owned message from the DAW's input exactly (replaces the `isPadEcho` timing guess);
    the channel-1 hint where the DAW merges channels; the FL script linking by itself again; a ready setup for the
    DAW program; the disconnect in both forms after clearing the screen; device-change notifications instead of
    polling; a fallback when the port cannot be opened.
C5. A CLAP build with remote control pages (clap-juce-extensions), one page per panel page and `suggest_page`; the
    installer installs it.
C6. Test per DAW (Ableton, FL Studio, Reaper, and Bitwig or Studio One if available): which hosts call `IMidiMapping`
    (a logging build), the duplicate filter, the channel-1 setting, the guided setup step by step (never tested by
    hand yet), and the MiniLab in both programs.

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
17. V2's old panel components in `src/ui` (SynthSection, HardwareKnob, HardwareFader, LedToggleButton,
    PianoBarComponent, SegmentedChoiceGroup and their helpers) are unused since V3's panel: remove them (D213).

## Standing notes

- Adding a bank file used to make the link fail (LNK1236 on the embedded bank library) because of stale
  incremental-link databases; incremental linking is now off in Debug. If it ever returns, delete the `.ilk`
  files under `build` and build again.
- The engine's specification is in `docs/design/` (DESIGN_V3, REQUIREMENTS_V3, IMPLEMENTATION_PLAN_V3,
  SOUND_BANK_V3); the architecture as built is `docs/architecture.md`.
