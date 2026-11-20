# CoolSynth Project Rules

This is the shared project memory for every AI tool (`CLAUDE.md` and `AGENTS.md` point here). If your memory file
needs adapting, adapt this file. History is in git and in `DECISIONS_LOG.md`; this file holds only the rules and
the current state.

## What CoolSynth is now

- A bi-timbral synthesizer for Windows: a VST3 plugin, a CLAP plugin and a standalone app, built with JUCE 9.0.5
  (C++20, CMake). The CLAP comes from clap-juce-extensions (`external/clap-juce-extensions`, D216, D217) and
  publishes remote control pages that follow the panel (`src/plugin/RemoteControlPages`). macOS is planned (J2).
- Version 0.3.0 was released on 2026-11-19 (D208, D209, D213): installer, VST3 and app zips on GitHub Releases;
  the recorded instruments as zips on the Hugging Face dataset `tomccargo/coolsynth-instruments`. Version 0.4.0
  followed on 2026-11-20 (J3, the CLAP, the stutter, D221 to D228); the instruments are still the 0.3.0 zips.
- The V3 engine (`src/synth/v3/`): two layers, each with three generator slots, three processor slots, its own
  arpeggiator and six effect slots (I1-I6); six master effect slots (F1-F6) at the end, any of 14 effect types. 139 presets in 14 rooms
  (`resources/presets/*.csbank`), including the recorded instruments of the Conservatory and the Museum's rebuilt
  classic synths.
- The panel (`src/plugin/PanelEditor`, `src/interface/InterfaceModel`) copies and drives the Arturia MiniLab 3
  (`src/interface/MiniLab3Driver`, in the app and in the VST3 through Windows MIDI Services).
- Any MIDI controller plays the panel through the computer's controller setup (`src/midi/ControllerSetup`,
  `%APPDATA%\CoolSynth\controller.json`, D211). DAW scripts for the MiniLab's pads are in `resources/daw-scripts/`.
- In a DAW (D216, `docs/design/CONTROLLERS.md`): the host's own mapping comes first. The first eight parameters are
  the panel's Home knobs (layer A's macros, glide, Brightness, Space, master volume); the CLAP's remote control pages
  follow the panel's page; a note reaching CoolSynth by two routes within 3 ms plays once (`src/midi/DuplicateNoteFilter.h`).
- A new CoolSynth starts on Main Stage's Reese (D212). Stop ends every sound (All Sound Off, after a 3 ms fade).
- The app has an MCP server (D225, `src/mcp`): switched on in Prefs, on `http://127.0.0.1:47613/mcp`, registered
  for this project with `claude mcp add --transport http coolsynth http://127.0.0.1:47613/mcp`. Use it to read and set
  the running app while guiding a test, instead of asking the user to turn knobs. `read_screen` gives the panel as
  text and `click` works it by name (D227); check anything visible that way, and ask the user only for ears and hands.
- Output is deterministic (D224): every random source restarts from its seed at a reset and when the host's
  transport starts, a bounce waits for sounds to load, and a voice cap by measured cost (`src/synth/v3/VoiceCost.h`)
  keeps a sound within half a block; `tests/DeterminismTests.cpp` holds every preset to it bit for bit.
- A state must name every parameter (`buildSanitizedParameterStateTree`): a new parameter always raises
  `processorStateFormatVersion` and `patchFormatVersion`, or older states are refused (D225). The Master Boost
  (`masterBoost`, 0 to +12 dB) sits above the Master fader's top; presets stay at -20 LUFS (D153). A controller's
  fader 4 runs along the Master's whole travel, 0 dB at four fifths (D228).
- Plugin checks (D228): `tests/HostedPluginTests.cpp` hosts the built VST3; pluginval and clap-validator live in
  `tools/validators` (not committed; run them with `COOLSYNTH_TEST_HOST=1` and `COOLSYNTH_LIBRARY` at an empty
  folder, so the plugin leaves the MiniLab, the flight log and the 3.8 GB library alone). A new float parameter
  is a `coolsynth::parameters::FloatParameter`, so typed values read back.

## Where things are

- `README.md`: the player's guide. `docs/architecture.md`: the architecture as built, with the engine map.
- `docs/design/`: the V3 engine's specification (`DESIGN_V3.md`, `REQUIREMENTS_V3.md`, `IMPLEMENTATION_PLAN_V3.md`,
  `SOUND_BANK_V3.md`). `docs/interface-v3.md`: the interface specification. `docs/minilab3-screen.md`: the MiniLab
  3 protocol as tested on the device.
- `TODO.md`: open work only. `DECISIONS_LOG.md`: every decision, with the options and the reason.
- `docs/releases/<tag>.md`: the release notes of each version.
- `installer/`: the Inno Setup installer. `scripts/ci/` and `.github/workflows/`: validation and release builds.
- `tools/`: the preset normalizer, the renderer, the library packer, the audition and engine-map scripts.

## Compatibility stance

- No backward compatibility is required anywhere unless the user explicitly asks for it.
- Patch and processor-state formats are not kept backward compatible by default: when the parameter contract
  changes, bump the format. Where a migration already exists (older states carried over), keep it working.
- 0.4.0 is out now: before a change that would stop a saved project from loading, ask the user.

## Project rules

- Keep one `TODO.md` item small enough for one review cycle. Update `TODO.md` before starting a chunk and after it;
  remove an item once it is verified, and record what was decided in `DECISIONS_LOG.md`.
- Every choice that is the user's goes to the user as a pick list, with context and consequences per option and
  the recommended one first. When the user is away, take the recommended option and log all options in
  `DECISIONS_LOG.md`.
- Research online how others solve a problem before proposing or coding a fix, and name the sources.
- Text a player sees (the panel, the installer, the README, release notes) uses plain words, no internal jargon.
- Update `docs/design/` when the engine's scope changes, and `docs/architecture.md` when the engine changes: update
  the facts in `tools/site/build_engine_map.py` and the page, run `python tools/site/build_engine_map.py --png`,
  and commit both.
- Ask before a large refactor, a change of the directory structure, or removing tests.
- Required checks: the Debug build and the tests the change touches (`ctest --test-dir build -C Debug
  --output-on-failure` for the whole suite; `COOLSYNTH_TEST_ONLY=<part of a test's name>` runs a subset). The Debug
  configuration is compiled optimised (`coolsynth_config_flags`), so it plays in real time; keep JUCE's recommended
  config flags off CoolSynth targets.
- `CoolSynthRender` and `CoolSynthPresetNormalizer` are not in the `build-debug` preset: rebuild them by target
  after any change to a bank file or the engine.
- Commit on `main` and push to `origin` after each verified chunk; no branches, no pull requests.
- A release: set the version in `CMakeLists.txt` (`project(CoolSynth VERSION ...)`; Preferences shows it), write
  `docs/releases/v<version>.md` (one line per paragraph and per bullet: the release page shows every line break),
  then push the tag `v<version>`. `.github/workflows/windows-release.yml` builds, tests, packages and publishes.
- Keep terminal commands profile-free (PowerShell `-NoProfile`) to avoid profile noise.
- Never use heredocs (`<<'EOF'`, `<<EOF`, PowerShell here-strings) in shell commands. Write scripts, test snippets
  and commit messages to a file first and pass the file by path (`python script.py`, `git commit -F message.txt`).

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.
