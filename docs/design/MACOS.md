# CoolSynth on macOS

The plan for the port (TODO J2), written on Windows for the Claude Code session the user will run on their Mac.
Start there by reading this file, `docs/research/macos.md` (the online research, with its sources), `GEMINI.md` and `TODO.md`.
Where the research's plan and the user's choices below differ (the research assumed universal builds and macOS 12), the choices win.

## What is Windows-only today

Found by searching the code for Windows names, headers and intrinsics on 2026-11-20.

The engine:
- `src/synth/v3/AudioWorkers.cpp`: the helper threads that render a second sound unit (D164) raise themselves to the
  "Pro Audio" MMCSS class and spin with Windows calls. On macOS they need real-time priority and, on Apple Silicon,
  to join the host's audio workgroup, or the scheduler puts them on the efficiency cores.
- `src/synth/v3/SampleMemory.cpp`: the sample memory is reserved with `VirtualAlloc` and kept in RAM by growing the
  working set (`SetProcessWorkingSetSize`). On macOS: `mmap` and `mlock`, within the limits the system allows.
- `src/synth/v3/VaGenerator.h` and `LadderProcessor.h`: SSE2 fast paths behind `COOLSYNTH_VA_SSE` and
  `COOLSYNTH_LADDER_SSE`, each with a plain path beside it, so an Apple Silicon build works as it is, only slower.
  - Whether to write NEON versions is a choice after measuring.
  - The VA's four-at-a-time copies sum in another order than the plain path (about -140 dB): the fingerprint test
    (`COOLSYNTH_VA_FINGERPRINT`) would need its own reference on ARM.
- `src/synth/v3/SampleLibrary.cpp`: the library is looked for in the common application data folder
  (`C:\ProgramData\CoolSynth\Library` on Windows); on macOS that is `/Library` or `~/Library/Application Support`.

The plugin and the panel:
- `CMakeLists.txt`: the formats are Standalone and VST3 (plus the CLAP); AU must be added for Logic and GarageBand.
  The MSVC settings (static runtime, the optimised Debug flags) need macOS equivalents; the user VST3 copy folder is
  Windows' only.
- `src/interface/MiniLab3Driver.*`: written around Windows MIDI Services sharing the MiniLab's port (Windows 11 24H2).
  CoreMIDI shares ports between programs, so the "busy" case does not arise, but notes can then arrive twice in a DAW
  (the duplicate filter, `src/midi/DuplicateNoteFilter.h`, already handles it).
  - `ml::isDawPort` (`src/midi/MiniLab3Protocol.h`) matches the port names as Windows shows them; the macOS names must
    be checked with the MiniLab plugged in.
- `src/plugin/PanelEditor.cpp`: a mouse wheel step size that differs on Windows; check how the knobs feel with a Mac
  mouse and trackpad.
- `src/diagnostics/FlightRecorder.cpp`: the flight log reads the program's memory with a Windows call (elsewhere it
  logs none: `task_info` gives it on macOS), and its folder is built as the parent of the application data folder plus
  `Local` (`%LOCALAPPDATA%`), which on macOS would land in `~/Local`; `~/Library/Logs/CoolSynth` is the Mac's place.
- Per-computer settings (`controller.json`, `plugin.options`, `library.path`) live under the user's application data
  folder: JUCE maps it to `~/Library` on macOS; check the exact place.
- `src/FullPath.h`: already handles paths saved on the other system.

The tests and tools:
- `tests/AudioThreadAlarm.cpp`: the watch on heap, locks, sleeps and files in the audio thread patches Windows import
  tables; on macOS it returns false, so its test would fail. Clang's RealtimeSanitizer is the macOS way (see the research).
- `tests/HostedPluginTests.cpp`: hosts the VST3 as a DAW does; on macOS it could host the AU too.
- `tools/uia-check.ps1` (Windows UI Automation), `installer/*.iss` and `Install-MiniLabScripts.ps1` (Inno Setup), the
  release workflow (`windows-2022` runner, Inno Setup, PowerShell steps): each needs a macOS counterpart or none.
- `resources/daw-scripts`: the FL Studio and Ableton scripts themselves are portable; their install folders differ.
- The MCP server (`src/mcp`) has no Windows code.

The research (`docs/research/macos.md`) found three more:
- The helper threads only start on Windows, so on macOS the engine renders every sound unit on one thread.
- Three folders come out wrong: the logs (`~/Local/...`), the controller setup (`~/Library/CoolSynth`, not under
  Application Support) and the sample library (`/Library/CoolSynth/Library`, which a normal user cannot write).
- `isDawPort` looks for "minilab3" without a space; some of Arturia's guides write "MiniLab 3 MIDI". Read the real
  name on the Mac before changing it.

## The user's choices (D234)

Asked as pick lists on 2026-11-20, each with the research's context; the recommended option is named where the user
chose another.
- Signing: unsigned, with the steps explained (recommended was the Apple Developer Program, 99 USD a year).
  - Apple Silicon still needs every binary signed ad hoc; Xcode's linker does that by itself.
  - The README must show the Open Anyway step for the pkg.
- Processors: Apple Silicon only (recommended was universal). Intel Macs and hosts running under Rosetta cannot load it.
- Audio Unit: AUv2, the .component (recommended).
- Oldest macOS: 13 Ventura or newer (recommended was 12). `CMAKE_OSX_DEPLOYMENT_TARGET` 13.0.
- Installer: a .pkg with a choice per format (recommended). Unsigned, users click Open Anyway once; what it installs
  carries no quarantine flag.
- Plugin folders: the user's own, `~/Library/Audio/Plug-Ins/Components`, `VST3` and `CLAP` (recommended was the system
  folders). No administrator password; only that user account sees them. A pkg installing into the home folder needs
  the current-user domain in its distribution file.
- Sample library: `~/Music/CoolSynth` (recommended was `/Users/Shared/CoolSynth`). Readable even by sandboxed hosts
  such as GarageBand; per user. `SampleLibrary.cpp` must use it on macOS instead of the common application data folder.
- Instruments: downloaded by the installer, as on Windows (my recommendation; the research had preferred a download in
  CoolSynth on first run).
- SIMD: the existing SSE code through sse2neon (MIT licence), checked against the plain path (recommended).
- Floating point: Clang's default, which fuses multiplies and adds (recommended was `-ffp-contract=off`).
  - Renders differ from Windows in the last bits; the exact-number tests need Mac references.
  - Tests that compare two code paths bit for bit in one build (the SVF's Clean path against the old code, D233; the
    VA fingerprint) may now differ between the paths: check them first, and give them a tolerance or keep the two
    paths fused alike.
- Audio-thread checks: port today's watch to macOS by rebinding the same calls (fishhook) (recommended was
  RealtimeSanitizer in a separate build).
- CI: GitHub's hosted Apple Silicon runner, on every push and on tags (recommended).

## The order

Each step ends in something that runs; the research's phases have the detail.
- A first build on the Mac: the standalone and the VST3, Apple Silicon, macOS 13, the optimised Debug as on Windows.
  - Fix what Clang rejects, the three folders, the library in `~/Music/CoolSynth`.
  - Run the test program; give the exact-number tests their Mac references.
  - Play every room against Windows renders.
- The formats: AU and CLAP; auval, pluginval, clap-validator; Logic, GarageBand, Ableton, FL Studio, Reaper, Bitwig.
- Speed: sse2neon for the SSE paths, the helper threads on macOS joined to the host's audio workgroup, the voice cap's
  costs measured on the Mac.
- The audio-thread watch ported with fishhook.
- The MiniLab: its port names, the screen, the pads and knobs, several instances, notes arriving twice, the FL,
  Ableton and Bitwig scripts in their Mac folders. Which control surface Live loads by itself there: without Arturia's Windows
  driver the MiniLab is likely product 0x220B, which Live's own MiniLab_3 claims too (D247).
- The installer: an unsigned .pkg into `~/Library`, the instruments downloaded by it; tested on a clean user account
  after a browser download; the README's Mac install with the Open Anyway step.
- CI: a macOS job beside the Windows one, building the pkg on tags.
