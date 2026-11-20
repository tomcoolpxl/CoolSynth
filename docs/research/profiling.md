# Profiling the heaviest sound

Preliminary research for TODO 23, 2026-11-20, for the user to discuss: which profiler, how to build the heaviest
patch, what to measure. Nothing is decided here. Line numbers are those of 2026-11-20.

## What CoolSynth measures already

The worst-case cost test (`tests/V3ModuleCostReport.cpp`, `V3 worst-case cost`, `COOLSYNTH_CPU_REPORT=1`).
- Each module alone, sixteen voices at its hardest settings, struck twice, the best of three runs.
- Per block: the average, the 99th percentile and the slowest, in percent of a 256-sample block at 48 kHz.
- Its results are the table in `src/synth/v3/VoiceCost.h`, which sets the voice cap against a 50 % budget.

The per-preset report (`V3 CPU report` in `tests/V3BankTests.cpp`): Trance Glass 52.5 % at the 99th percentile.

The CPU meter (`cpuLoad()`, `SynthAudioProcessor.h:136`) and the helpers' counts (`AudioWorkers::takeActivity`).
- Busy share per thread, the worst block, helper wake times and late wakes, logged each second as `threadLoad`.

The flight log (`src/diagnostics/FlightRecorder.*`).
- `blockSlow` names the slowest stage of a slow block: part A or B, their inserts, the master chain.
- `memoryUse`: working set, committed memory, page faults, process-wide.
- A recent session: the working set climbed from 386 MB to 4018 MB in two seconds as the sample memory was
  locked (`SampleLibrary.cpp:420`: the whole library or half the free memory, whichever is smaller).

What none of these can tell.
- Where the time goes inside a module: FM4's 0.86 % may be its sines, its feedback or its parameter smoothing.
- Cache misses, branch misses, how well the loops vectorise.
- Where memory is allocated, and how much each voice holds (`SlotMemory` per slot, sized by each module's
  `memoryFloats`); the working set is one number for the whole program.
- A timeline: which voice or effect made one particular block slow.

## The machine

AMD Ryzen 7 3700X (Zen 2), 8 cores and 16 threads, 32 GB; the flight log prints "CPU: 8 cores, 16 threads, 2
cache groups". An AMD processor rules out the hardware counters of Intel's tools.

Already installed, so needing no download.
- `C:\Windows\System32\wpr.exe`, and the whole Windows Performance Toolkit (`wpa.exe`, `xperf.exe`) under
  `C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit`.
- Visual Studio Community 2022 and 18, both with `VSDiagnostics.exe` (the profiler's command line).
- The optimised Debug build (`/O2 /Ob2 /Zi`, `CMakeLists.txt:39`) writes PDBs, which every tool below needs for
  function names; the test program is `build\CoolSynthMidiLearnTests_artefacts\Debug\CoolSynthMidiLearnTests.exe`.

## The tools

### Windows Performance Recorder and Analyzer (ETW)

WPR records Event Tracing for Windows; WPA shows it ([WPR](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-recorder)).
- Free; WPR ships with Windows, WPA with the ADK and is already here.
- `wpr -start CPU -filemode`, run the program, `wpr -stop heavy.etl`, from an administrator prompt
  ([options](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)).
- Samples every thread, the kernel too, with call stacks and context switches: hot functions per thread, and
  why a helper woke late.
- The default interval is 1 ms; `wpr -setprofint` goes down to about 0.12 ms, a system setting the user would
  change and reset (`wpr -resetprofint`). `wpr -pmcsources` lists the processor counters Windows offers here.
- Heap tracing with allocation stacks (`wpr -heaptracingconfig`, then the Heap profile).
- Own markers with no download: TraceLogging (`TraceLoggingProvider.h`, in the Windows SDK) writes ETW events
  that WPA can show as regions ([quick start](https://learn.microsoft.com/en-us/windows/win32/tracelogging/tracelogging-native-quick-start),
  [regions of interest](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/regions-of-interest)).
- The cost: WPA is dense and takes learning; markers need a little code.

### Visual Studio's profilers

CPU Usage (sampling, call tree, flame graph, hot path) and Memory Usage (native heap snapshots and their
differences) both support C++ ([which tool](https://learn.microsoft.com/en-us/visualstudio/profiling/choose-performance-tool?view=vs-2022)).
- Microsoft lists no edition limit for them; Community has them installed here.
- A CMake project must pick the executable as the target.
- Memory Usage shows allocation call stacks only while debugging ([memory usage](https://learn.microsoft.com/en-us/visualstudio/profiling/memory-usage-without-debugging2)).
- An Instrumentation tool gives exact call counts at a cost in overhead.
- Easier to read than WPA, less detail on threads and the kernel.

### Tracy

A frame and sampling profiler, BSD 3-clause ([repository](https://github.com/wolfpld/tracy), [licence](https://raw.githubusercontent.com/wolfpld/tracy/master/LICENSE)).
- A download: the client source into the build, and the viewer (`Tracy-<version>.7z`).
- Zones (`ZoneScoped`), about 2.25 ns each by its manual's benchmark; nothing when `TRACY_ENABLE` is off
  ([manual](https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf)).
- Also sampling, context switches, and allocations through `TracyAlloc`/`TracyFree` in `operator new`.
- Sampling and context switches need administrator rights on Windows ([IREE's notes](https://iree.dev/developers/performance/profiling-with-tracy)).
- No JUCE module found; it would be wired in by hand.
- The best fit for "which part of this one slow block": a zone per stage, per voice, per effect, live.

### melatonin_perfetto

A JUCE module around Google's Perfetto ([repository](https://github.com/sudara/melatonin_perfetto)).
- A download (FetchContent or a submodule); `-D PERFETTO=ON`; `TRACE_DSP()` in functions to trace.
- Off by default, the macros "evaporate on compile".
- Writes the trace only on a clean exit, to the Desktop on Windows; viewed at ui.perfetto.dev, in a browser.
- Instrumentation only: a timeline of what was marked, no hot functions, no memory.
- No licence is declared on the repository (GitHub reports none); the author says Windows "required some fancy
  build settings" ([JUCE forum](https://forum.juce.com/t/new-module-profile-your-juce-ui-dsp-performance-with-perfetto/54589)).

### Superluminal

A sampling profiler at 8 kHz on Windows, no code changes ([Windows](https://superluminal.eu/applications/windows/)).
- Paid: 59 euros for a personal non-commercial licence, 289 euros a year commercial, a 14-day trial
  ([pricing](https://superluminal.eu/pricing/)).
- Sampling eight times finer than WPR's default resolves short functions in a 5.3 ms block well.

### Intel VTune

Free ([Wikipedia](https://en.wikipedia.org/wiki/VTune)); integrates with Visual Studio.
- On AMD only the software analyses (Hotspots by user-mode sampling, Threading, Memory Consumption); hardware
  event sampling needs an Intel processor ([Intel forum](https://community.intel.com/t5/Analyzers/VTune-on-AMD-cpus/td-p/1166631),
  [FAQ](https://www.intel.com/content/www/us/en/docs/vtune-profiler/installation-guide/2026-0/faq.html)).
- On this machine it gives what WPR and Visual Studio already give.

### AMD uProf

AMD's own profiler, free, Windows supported, Zen 2 since version 3.0 ([AMD](https://www.amd.com/en/developer/uprof.html),
[Notebookcheck](https://www.notebookcheck.net/AMD-announces-mProf-3-0-a-free-tool-to-optimise-apps-for-AMD-processors.427132.0.html)).
- The one tool here that reads Zen's counters: cache misses, IBS instruction sampling.
- A download behind an EULA, and an install with its own driver; AMD's pages timed out during this research,
  so the current Windows feature list for Zen 2 is still to check.

### Optick

Instrumentation plus ETW sampling and context switches on Windows ([repository](https://github.com/bombomby/optick)).
- A download; made for games; no releases listed, so less alive than Tracy.

## A recommendation and its alternatives

First: WPR and WPA, with the test program's own timers.
- No download and no install; driven from a command line, so a session can record and the user can look.
- Hot functions per thread with PDB names, the helpers' wakes, heap allocation stacks.
- TraceLogging markers can come later, for single blocks.

Visual Studio's CPU Usage and Memory Usage, instead or beside it.
- Also installed; the friendliest view of hot functions and the native heap.
- Weaker on threads and on finding one slow block.

Tracy, when a slow block needs explaining.
- A download for the user to approve, and code behind a CMake option.
- The clearest timeline per block, plus allocations.

AMD uProf, when the hot loops are known and the question is cache or memory traffic.
- A download and a driver install; the only way to the Zen 2 counters.

Not recommended for this machine: VTune (no more than WPR on AMD), Superluminal (paid; the trial would do for a
second opinion), melatonin_perfetto and Optick (timelines only, and Tracy covers that).

## The heaviest patch

From `VoiceCost.h`, per voice in percent of a block at 48 kHz.
- Generators, three slots: FM4 0.86, Grain 0.85 (densest, longest), Wavetable 0.12 a copy, 0.60 at unison 5,
  VA 0.43 at unison 7 with the sub. Three FM4 cost the most (2.58); FM4, Grain and FM4 (2.57) reach more code.
- Processors, three slots: Ladder 0.56, Drive 0.48, Comb 0.28. Three Ladders cost the most (1.68); Ladder,
  Drive and Comb (1.32) reach more code.
- With the voice itself (0.35): about 4.6 per voice, 74 % for sixteen, 148 % for both layers.

Effects, six per layer and six on the master, eighteen slots.
- The costliest: Grain at its densest (about 7.2), Pitch spectral (5.54), Room (0.86), Drive (0.80), Reverb
  (0.77), LoFi (0.65).
- One of each in every chain: about 16 a chain, 47 % in all.

Together nearly two blocks of work on one core, so the voice cap would cut it to fit 50 %.
- To see sixteen voices a layer, the test calls `Part` and `InsertSlot` directly, as `worstCaseOf` does, which
  skips the cap (`SoundUnit.cpp:338`); the helper threads then add a second view through the engine.
- All three envelopes held, every LFO running and the matrix full, since the matrix costs per voice too.
- Played for 30 to 60 seconds: at a 1 ms sampling interval one 5.3 ms block gives about five samples, so the
  run needs thousands of blocks.
- A second variant with the largest sampled instrument in one slot, for memory.
- At 96 kHz the costs double (the voices run at twice the host's rate); worth one run.

## What to measure

CPU.
- Per module: the share of samples in each module's `process`, against the cost table.
- The worst block: per-stage timers (part A, part B, their inserts, the master), extending the test's own.
- Inside the hot modules: lines and loops, and whether MSVC vectorised them.

Memory.
- Per voice: the sum of each slot's `memoryFloats`, printable by a test with no profiler at all.
- The heap: allocations during play (there should be none) and at a preset change.
- The sample library: reserved, locked and used, against the 4 GB seen in the log.

The rule from TODO 23 holds throughout: faster or smaller without changing the sound, checked as TODO 9 did
with the bit-for-bit fingerprints (`COOLSYNTH_VA_FINGERPRINT`, `COOLSYNTH_VA_RECORD` and `_COMPARE`).

## Likely hot code

From the cost table, in order.
- Spectral Pitch (5.54, its slowest block 15): its FFT frames come all at once in some blocks; spreading them
  out would help the worst block more than the average.
- The Grain effect at its densest (7.1) and the Grain generator (0.85): cost grows with grains at once.
- FM4 (0.86): four oscillators with feedback, per sample, at 96 kHz.
- Ladder (0.56) and Drive (0.48): nonlinear stages per sample, likely `tanh`-style functions.
- The voice itself (0.35, sixteen times per layer): envelopes, matrix, the decimator.
