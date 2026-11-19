# Determinism and a processing budget

Research and code review for TODO 19, 2026-11-19. The user asked that everywhere in the chain, and above all in
big sounds, the audio be predictable and deterministic and fit a known time budget, with a known answer for when
it would not; first how others do it, then a review of CoolSynth, then a plan. The user's choices are in
`DECISIONS_LOG.md` D222. Line numbers are those of 2026-11-19.

## What others do

### Real-time rules

"If you don't know how long it will take, don't do it" ([Bencina, Real-time audio programming 101](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)).
- Never on the audio thread: allocation, locks (priority inversion), any input or output including logging,
  system calls that may block.
- Code is judged by its worst case, not its average; hazards include clearing a large delay line in one go,
  a growing vector, page faults.
- Remedies: bounded algorithms, work spread over blocks, work done ahead on other threads, lock-free queues.

Waiting on a lock risks priority inversion; a try-lock needs a fallback ([Doumler](https://timur.audio/using-locks-in-real-time-audio-processing-safely)).
The same rules in Rowland and Renn-Giles's "Real-time 101", ADC 2019 ([index](https://github.com/drowaudio/presentations)).

JUCE: `isNonRealtime()` is true while a host renders offline, "may be unreliable until prepareToPlay()"
([AudioProcessor](https://docs.juce.com/master/classAudioProcessor.html)); `ScopedNoDenormals` sets the flags
of the thread it runs on ([JUCE forum](https://forum.juce.com/t/whats-the-scope-of-scopednodenormals/33076)).
Flush-to-zero is per thread, so every worker sets its own ([Intel](https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2025-0/set-the-ftz-and-daz-flags.html)).

Clang's RealtimeSanitizer reports allocation, locks and blocking calls in functions marked non-blocking
([clang](https://clang.llvm.org/docs/RealtimeSanitizer.html)); Windows support is not stated.

### Hardware with a fixed budget

Polyphony set by the patch's cost.
- Access Virus TI: the voice count "can vary dramatically" by patch, a meter shows it; unison divides it, the
  analogue filter model costs half the polyphony, a third oscillator about a quarter, and a feature costs even at a
  small setting ([manual](https://www.manualslib.fr/manual/75884/Access-Virus-Ti-S%C3%A9rie.html?page=41),
  [quickstart](https://www.fullcompass.com/common/files/12107-AccessMusicVirusTISnowQuickstartGuide.pdf)).
- Korg opsix: 32 voices, or 24 depending on settings ([Korg](https://www.korg.com/us/products/synthesizers/opsix/specifications.php));
  opsix SE and multi/poly vary with the setting; wavestate and modwave state fixed maximums.
- Nord Lead 2 halves its voices in unison ([Sound On Sound](https://www.soundonsound.com/reviews/clavia-nord));
  Kurzweil counts a layer as a voice and a stereo sample as two; Elektron's Digitone reserves voices per track.
- In all of these the cap is decided by the patch, so the instrument is predictable; none of the sources found
  drops voices because of the moment's load (inferred).

### Software instruments

Native Instruments Kontakt's overload protection "allows Kontakt to kill voices when the CPU load gets critical",
at Relaxed, Medium or Strict; offline interpolation quality is a separate setting, and multiprocessor support in
the plugin is off by default because it "can sometimes cause crackles" ([Kontakt options](https://docs.native-instruments.com/online-guides/kontakt-manual/en/options-dialog)).

u-he Diva has accuracy modes (draft to divine) and a separate offline accuracy for renders
([user guide](https://manualmachine.com/uhe/diva/17903499-user-manual)).

Ableton: one heavy chain on one track can miss the deadline even when the total load looks low; freeze or bounce
it ([multi-core FAQ](https://help.ableton.com/hc/en-us/articles/209067649), [Live 12 manual](https://www.ableton.com/en/live-manual/12/computer-audio-resources-and-strategies/)).

Not confirmed from primary sources: Pigments' steal modes ([forum only](https://forum.arturia.com/t/bug-voice-steal-modes-do-nothing-different/9082)),
Vital's and Serum's rules, any lowering of oversampling under load.

### Determinism

Floating-point addition depends on order, so parallel sums vary unless the order is fixed
([arXiv 1312.3300](https://arxiv.org/pdf/1312.3300)).

The Windows C runtime picks FMA3 code for its maths functions at start-up when the processor has it, so the last
bits can differ between computers ([Microsoft, _set_FMA3_enable](https://learn.microsoft.com/cpp/c-runtime-library/reference/get-fma3-enable-set-fma3-enable)):
bit for bit is a promise on one machine.

Renders that do not null usually come from free-running modulators and random phases; seeded randomness with a
defined start makes them repeat ([Sound On Sound](https://www.soundonsound.com/sound-advice/q-why-arent-my-rendered-and-live-plugin-sounds-nulling),
[KVR](https://www.kvraudio.com/forum/viewtopic.php?p=7458748)).

### Multi-core

Apple's audio workgroups let worker threads share the audio thread's deadline ([WWDC 2020](https://developer.apple.com/videos/play/wwdc2020/10224/)).
CLAP's thread pool runs a plugin's tasks on the host's threads and falls back to one thread
([header](https://sources.debian.org/src/lsp-plugins/1.2.5-1/modules/lsp-3rd-party/include/clap/ext/thread-pool.h),
[cleveraudio](https://cleveraudio.org/1-feature-overview/_thread-pool/)). More cores do not scale linearly
([coreaudio-api](https://lists.apple.com/archives/coreaudio-api/2011/Jul/msg00163.html)). Either static or dynamic
task division stays bit for bit when each task writes its own buffer and the sum runs in a fixed order (inferred).

## CoolSynth on 2026-11-19

### What is already right

- Multi-core output is bit for bit: each sound renders into its own buffer (`EngineV3.cpp:190-204`), summed in task
  order (`EngineV3.cpp:426-440`), tested against one thread (`tests/V3EarlierSoundTests.cpp:176-216`); offline
  rendering uses one thread (`SynthAudioProcessor.cpp:691`).
- Flush-to-zero on the audio thread (`SynthAudioProcessor.cpp:507`) and on every worker (`AudioWorkers.cpp:85`).
- Seeds per sound, part, voice and slot, with no global counter (D162).
- No iterative solvers; the ladder's tanh is one rational per stage. Grains and the Pitch effect's search are
  bounded.
- The flight recorder is wait-free, sample access from the audio thread is atomic loads, sample memory is locked in
  RAM, release events have reserved slots, a broken value is contained per stage, a slot clears only what it used
  (D160).
- The allocation alarm counts `operator new` on the audio thread and its workers, over every preset, preset
  switches, panic, 32-note blocks and odd block sizes (`tests/AudioScenarioTests.cpp:117-313`).

### Gaps, most serious first

1. Nothing limits the sound being played to a block's time. `ModuleDescriptor::cost` (`Module.h:54`) is read
   nowhere; the voice limits count voices only, while DESIGN_V3 section 3.5 specifies a cost-summed cap. The
   worst case adds up to 4 sounds × 16 voices × 6 slots at twice the host rate, unison up to 7, 18 effect slots
   per sound. Supersaw with 8 notes takes 14 % on average and 19 % in its worst block. The only answer to overload
   is fading the oldest earlier sound (`EngineV3.cpp:386-388`).
2. The random arpeggiator is seeded from the clock (`Arpeggiator.cpp:77`).
3. The overload fade reads the wall clock, also when a host bounces offline (`SynthAudioProcessor.cpp:743-755`,
   `EngineV3.cpp:387`).
4. Offline, notes are dropped while a sampled instrument loads, as many as the disk's timing gives
   (`SynthAudioProcessor.cpp:530-538`).
5. Random states run on from preparation, not from a start: voices' drift, LFOs, chaos, grains, the stutter's
   Chance, LoFi, the grain effect. Two bounces in one session differ unless the host prepares again.
6. Average cost is measured, worst-case cost is not: spectral Pitch's frames, clearing used slots, a slot's
   crossfade running two modules, a steal fade, a type switched on the audio thread.
7. Multi-core helps only when sounds overlap; one sound is always on the audio thread; the audio thread waits
   without a bound for a helper's task (`AudioWorkers.cpp:212-217`).
8. The alarm catches `operator new` only, not `malloc`, locks or files (none found on the audio thread).
9. Output depends on the block size (the 32-sample grid restarts each block), as in most instruments (inferred).
10. Bit for bit holds on one machine (the runtime's FMA3 maths).
11. A preset change lands on a block decided by thread timing (`SynthAudioProcessor.cpp:1887-1902`).
12. Broken values are scrubbed from the output only while the flight recorder runs (`SynthAudioProcessor.cpp:793-813`).
13. The VA's random start phases begin from one constant in every voice (`VaGenerator.h:667`).

Not confirmed: whether hosts prepare again before each bounce, the real worst case of four sounds and 64 voices
(estimated from the code, not measured).
