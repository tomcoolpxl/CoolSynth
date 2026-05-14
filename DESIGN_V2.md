# DESIGN_V2.md

# CoolSynth V2 Design

## 1. Purpose

This document turns [REQUIREMENTS_V2.md](/C:/Users/thraa/github/CoolSynth/REQUIREMENTS_V2.md) into a concrete V2 software design.

V2 is an architectural upgrade, not a cosmetic feature pass. The goal is to evolve the current JUCE instrument from:

- one oscillator per voice,
- one shared waveform selector,
- one amp envelope,
- one per-voice low-pass filter,
- one global delay,

into a focused analog-style subtractive synthesizer with:

- dual oscillators per voice,
- separate filter and amplifier envelopes,
- constrained Prophet-style modulation,
- built-in arpeggiation,
- unison and glide,
- vintage behavior controls,
- a larger but still disciplined global effects section.

This design assumes the current standalone shell, VST3 shell, APVTS usage, controller mapping, MIDI learn, patch flow, and Windows build workflow remain worth preserving.

## 2. External References

These references were checked while preparing this design:

- Sequential Prophet-5/10 official product page:
  https://sequential.com/classics-reissued/prophet-5-10/
- Sequential Prophet-5 User's Guide v1.3:
  https://sequential.com/wp-content/uploads/2021/02/Prophet-5-Users-Guide-1.3.pdf
- JUCE `AudioProcessorValueTreeState`:
  https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html
- JUCE `AudioPlayHead` and `PositionInfo`:
  https://docs.juce.com/master/classjuce_1_1AudioPlayHead.html
  https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html
- JUCE `SynthesiserVoice`:
  https://docs.juce.com/master/classjuce_1_1SynthesiserVoice.html
- JUCE `DelayLine`:
  https://docs.juce.com/master/classdsp_1_1DelayLine.html
- JUCE `SmoothedValue`:
  https://docs.juce.com/master/classSmoothedValue.html
- Sound On Sound coverage of the Stranger Things score:
  https://www.soundonsound.com/techniques/scoring-stranger-things
- Pitchfork summary of the Song Exploder breakdown:
  https://pitchfork.com/news/stranger-things-composers-break-down-theme-music-on-song-exploder-listen/

Important design implications from those sources:

- The Prophet-5 identity depends on two oscillators per voice, mixer overload behavior, distinct filter and amp envelopes, Poly Mod, unison, glide, and voice-to-voice organic variance.
- The Prophet oscillator model supports pulse width, hard sync, Oscillator B low-frequency operation, and simultaneous waveshape combinations.
- The Stranger Things sound palette is synth-dominated but not one-synth-purist. V2 should target the shared tonal center and workflow, not promise literal one-box recreation of the score.
- JUCE's APVTS remains the correct parameter/state center.
- JUCE `AudioPlayHead::getPosition()` is only valid inside `processBlock()`, so plugin arp sync must be resolved there.

## 3. Design Summary

The V2 architecture keeps `SynthAudioProcessor` as the central shared plugin/standalone object, but it changes the internals substantially.

The most important architectural decision is:

- keep JUCE `AudioProcessor`, APVTS, editor, and wrapper integration,
- replace dependence on `juce::Synthesiser` note behavior with a dedicated V2 voice allocator and note dispatcher.

That change is justified because V2 requires behavior that is awkward to express cleanly through the current `juce::Synthesiser`-based flow:

- mono/unison stacking,
- explicit key-priority modes,
- vintage-limited voice-count modes,
- per-voice drift policy,
- arp-generated note dispatch,
- controlled retrigger and glide behavior.

The high-level V2 stack is:

```text
Standalone wrapper or VST3 wrapper
  -> SynthAudioProcessor
     -> AudioProcessorValueTreeState
     -> Parameter snapshot cache
     -> MidiMappingEngine
     -> runtime transport context
     -> SynthEngineV2
        -> Arpeggiator
        -> Global modulation state
        -> VoiceAllocator
        -> Voice instances
        -> FX rack
     -> state serialization
  -> SynthAudioProcessorEditor
     -> persistent core synth panel
     -> detail panel for modulation/performance/arp/fx
     -> standalone-only patch and settings surfaces
```

## 4. Top-Level Product Modes

## Standalone

Standalone remains the primary bring-up and debugging target.

Responsibilities preserved from V1:

- Audio device configuration
- Hardware MIDI input selection
- Status bar and settings dialog
- Standalone patch file actions
- Controller auto-detection and MIDI learn

New standalone V2 responsibilities:

- Internal arp tempo source
- Clear presentation of mono/unison and vintage controls
- Patch browsing or patch switching is still not required; init/save/load remains enough

## VST3

The VST3 target remains the same shared-code product.

Responsibilities preserved from V1:

- Host MIDI input
- Host automation
- Host state save/restore
- Shared editor where practical

New V2 VST3 responsibilities:

- Use host transport and tempo for arp sync when provided
- Fall back to internal arp timing behavior when host timing is incomplete
- Keep all new V2 sound-shaping parameters automatable and recallable

## 5. Core Architectural Decisions

## 5.1 Central Processor

`SynthAudioProcessor` remains the architectural center.

It should continue to own:

- APVTS
- MIDI mapping engine
- plugin/standalone state serialization
- transport lookup in plugin mode
- editor creation
- one shared synth engine instance

It should not absorb voice DSP or arp timing logic directly. Those belong in `SynthEngineV2` and its submodules.

## 5.2 Parameter-Centric Design

V2 should preserve one canonical parameter model.

All controls intended for:

- UI editing,
- automation,
- patch recall,
- MIDI learn,
- controller mapping,

must remain APVTS parameters.

No controller-only shadow synth state should be introduced.

## 5.3 Custom Voice Allocation

V2 should replace the current `juce::Synthesiser`-owned allocation path with a dedicated allocator.

Recommended shape:

```text
SynthEngineV2
  -> VoiceAllocator
     -> VoiceSlot[voiceCount]
        -> SynthVoiceV2
```

Reasons:

- unison needs deliberate voice stacking,
- mono mode needs explicit key priority,
- glide policy depends on note history and allocation policy,
- arp note generation needs direct note-dispatch ownership,
- vintage-limited voice modes need deterministic voice-count control,
- V2 should not be forced into emulating these behaviors via synthetic MIDI tricks.

`SynthVoiceV2` does not need to inherit from `juce::SynthesiserVoice` if the allocator becomes fully custom.

## 6. DSP Signal Flow

## 6.1 Voice Signal Path

Each voice should use this signal path:

```text
Oscillator A
Oscillator B
Noise
  -> voice mixer
  -> optional local overload / pre-filter saturation
  -> 4-pole low-pass filter
  -> amplifier envelope
  -> per-voice pan placement
  -> stereo voice bus
```

Each voice should remain internally mono until the pan-spread stage. This keeps the core synth path simple and analog-like.

## 6.2 Global Post-Voice Path

The recommended first-release global path is:

```text
summed stereo voices
  -> global drive / saturation
  -> chorus / ensemble
  -> delay
  -> reverb
  -> master gain
```

This order is recommended because:

- drive before time-based effects keeps basses and leads weighty,
- chorus before delay/reverb helps pad widening without washing out the source immediately,
- delay before reverb is the safer default for soundtrack-style space,
- a fixed first-release order avoids unnecessary routing complexity.

Optional later FX such as phaser/flanger/tremolo should be inserted only if they can fit without turning the rack into a routing system.

## 7. Voice DSP Design

## 7.1 Oscillators

The current single `juce::dsp::Oscillator<float>` design is too limited for V2.

V2 should use a dedicated oscillator implementation that supports:

- per-voice phase accumulation,
- simultaneous wave combinations where enabled,
- independent Oscillator A and B frequency state,
- hard sync with Oscillator A synced to Oscillator B,
- pulse width control,
- Oscillator B low-frequency mode,
- oscillator drift/slop offsets,
- audio-rate Poly Mod from Oscillator B.

Recommended implementation shape:

```text
VoiceOscillator
  -> phase accumulator
  -> saw generator
  -> pulse generator
  -> triangle generator
  -> shape enable mask / wave mode
  -> pulse width state
  -> sync/reset state
```

This does not need to be a circuit emulation. It does need to support the control vocabulary and musical behavior of the target architecture.

Important design choice:

- Oscillator A should support saw + pulse combinations.
- Oscillator B should support saw + triangle + pulse combinations.

If implementation complexity forces a simplification, the design should preserve enough combinations to keep brass, pad, PWM, and sync sounds convincing.

## 7.2 Mixer

The mixer should be explicit and per voice.

Required sources:

- Oscillator A
- Oscillator B
- Noise

Design behavior:

- Mixer levels should be able to exceed perfectly conservative gain staging in order to allow a musically useful overload character ahead of the filter.
- Any overload behavior should remain bounded and stable.

## 7.3 Filter

The current V1 filter is a single JUCE state-variable low-pass stage and is not sufficient for the V2 design target.

V2 needs a 4-pole, 24 dB-per-octave Prophet-adjacent low-pass path.

Recommended design:

- Implement a dedicated `VoiceFilterV2`.
- Use one of:
  - a cascaded two-stage TPT design,
  - a dedicated 4-pole low-pass design built on JUCE primitives,
  - another bounded 24 dB low-pass topology with modulation-safe behavior.

Required characteristics:

- smooth realtime cutoff modulation,
- stable resonance across supported sample rates,
- keyboard tracking,
- filter envelope amount,
- clear distinction between more Rossum-like and more Curtis-like character only if the additional complexity is justified.

Recommended first-release choice:

- one strong default 4-pole character first,
- optional character switching later,
- optional subtle local drive around the filter path if stable.

## 7.4 Envelopes

V2 needs:

- one per-voice filter ADSR,
- one per-voice amp ADSR.

The envelopes should be lightweight, retriggerable, and preallocated.

Required behavior:

- repeated notes retrigger correctly,
- voice stealing does not leave envelopes in corrupted states,
- mono/unison retrigger rules are controlled by the allocator rather than ad hoc voice reuse,
- envelope timing can participate in vintage behavior if that mode is enabled.

## 7.5 Per-Voice Modulation

Poly Mod should be implemented per voice.

Recommended Poly Mod sources:

- Oscillator B audio or low-frequency output
- Filter envelope output

Recommended first-release destinations:

- Oscillator A pitch
- Pulse width
- Filter cutoff

This should remain intentionally constrained. A full matrix is not part of the V2 design.

## 7.6 Global LFO

The LFO should be global, not per voice.

Recommended uses:

- wheel-mod destinations,
- optional direct routed modulation where useful,
- arp-related modulation is out of scope

The LFO can be implemented at audio rate or fast control rate depending on destination needs, but the interface should treat it as continuous and musical rather than stepped.

## 8. Arpeggiator Design

The arpeggiator should live inside `SynthEngineV2`, before note dispatch reaches the voice allocator.

Recommended processing flow:

```text
incoming MIDI notes
  -> held-note tracker
  -> arp mode decision
  -> arp clock
  -> generated note events
  -> voice allocator
```

Required state:

- held notes
- latched notes
- current pattern order
- current step position
- current octave cycle
- gate timing state

## 8.1 Timing

In plugin mode:

- read `AudioPlayHead::PositionInfo` inside `processBlock()`,
- derive host-tempo timing only there,
- pass a compact transport snapshot into `SynthEngineV2`.

If the host does not provide usable timing information:

- fall back to internal-rate arp timing,
- use the same patch-recallable internal BPM parameter used by standalone mode,
- do not persist "last seen host tempo" as hidden arp state,
- keep behavior deterministic,
- do not make the arp silently stall.

In standalone mode:

- use an internal BPM parameter,
- advance timing from sample count and sample rate.

## 8.2 Arp Output Model

The arp should emit note-on/note-off events into the engine's own note-dispatch path, not via a secondary MIDI plugin-output lane.

This keeps:

- standalone and VST3 behavior aligned,
- state simpler,
- note priority and unison handling centralized.

## 9. Performance Behavior Design

## 9.1 Pitch Bend

Pitch bend should be implemented in the voice pitch calculation path, not by mutating APVTS parameters.

Recommended model:

```text
base note pitch
  + tune offsets
  + drift offsets
  + pitch bend
  + glide state
  + modulation
```

Pitch-bend range should be a parameterized performance control in V2.

## 9.2 Glide

Glide should be handled in the note-dispatch/voice state layer.

Recommended behavior:

- available in poly and mono/unison modes,
- in mono/unison, glide follows note priority and held-note behavior,
- in poly, glide operates per voice when that voice retargets.

Use smoothed frequency movement rather than parameter automation tricks.

## 9.3 Unison and Mono

Recommended first-release modes:

- Poly
- Mono
- Unison

Recommended key-priority options:

- Last
- Low
- High

These should be implemented in the allocator, not delegated to the UI or MIDI layer.

## 9.4 Vintage / Slop / Pan Spread

Vintage behavior should be a bounded, deterministic set of small randomizations, not an uncontrolled noise process.

Recommended domains affected by vintage amount:

- oscillator pitch offsets,
- envelope timing variation,
- filter cutoff/resonance calibration offsets,
- optional voice start-time slop within very small limits.

Pan spread should be independent from vintage amount and applied at voice output placement time.

## 10. FX Rack Design

The first V2 release should use a fixed-order global rack.

Recommended modules:

- `GlobalDrive`
- `GlobalChorusEnsemble`
- `GlobalDelay`
- `GlobalReverb`

Each module should:

- be preallocated in `prepareToPlay()`,
- expose explicit enable/mix/bypass behavior where required by the parameter contract,
- avoid allocations during rendering,
- support hard reset on sample-rate or block-size changes.

## 10.1 Delay

The current `GlobalDelay` can be evolved rather than discarded.

Recommended behavior:

- stereo-capable,
- tempo sync optional later,
- smoothed delay-time changes,
- bounded feedback,
- musically darker voicing than V1.

## 10.2 Chorus / Ensemble

Recommended design:

- stereo modulation effect,
- intentionally voiced for pads and width,
- low parameter count,
- not a deep modulation lab.

## 10.3 Drive / Saturation

This is the required first FX slot for weight and aggression.

Recommended design:

- post-voice, pre-chorus,
- low CPU,
- bounded output,
- mixable or bypassable,
- not a high-gain distortion pedal simulation.

## 10.4 Reverb

Recommended first-release direction:

- algorithmic reverb,
- cinematic but not endless by default,
- no convolution requirement,
- no latency-heavy design requirement.

## 11. Parameter and State Design

## 11.1 Parameter Grouping

V2 should use `juce::AudioProcessorParameterGroup` via APVTS to keep the larger parameter set organized.

Recommended groups:

- `oscA`
- `oscB`
- `mixer`
- `filter`
- `filterEnv`
- `ampEnv`
- `lfo`
- `polyMod`
- `performance`
- `arp`
- `drive`
- `chorus`
- `delay`
- `reverb`
- `output`

## 11.2 Parameter IDs

Because V2 may break V1 compatibility, the design should treat V2 IDs as a new contract.

Recommended approach:

- define V2 IDs in a dedicated file,
- keep only truly semantically equivalent V1 IDs if desired,
- prefer clarity over continuity.

Practical recommendation:

- create `src/parameters/ParameterIDsV2.h`,
- update `ParameterLayout.cpp` to build the V2 layout from scratch,
- keep migration logic separate from the APVTS layout itself.

## 11.3 Audio-Thread Parameter Access

The V1 pattern of caching raw APVTS parameter pointers remains valid and should continue.

Recommended V2 design:

```text
ParameterValuePointersV2
  -> atomics for all hot-path parameters

BlockParameterSnapshotV2
  -> decoded enums
  -> clamped/scaled floats
  -> per-block transport state
```

`SynthAudioProcessor::processBlock()` should:

- read raw atomics,
- decode them into a compact block snapshot,
- pass the snapshot to `SynthEngineV2`.

## 11.4 State Format

V2 should use a new state identity.

Recommended:

- new processor state root tag
- new patch-format version
- explicit V2 compatibility boundary

Recommended patch direction:

- new patch extension such as `.cs2patch`, or
- keep the extension but require a top-level V2 format version field and reject V1 explicitly

The simpler and safer default is a new patch version and explicit rejection of V1 files unless an import path is built later.

## 12. MIDI and Controller Design

## 12.0 Engine Event Model

Once V2 stops depending on `juce::Synthesiser` note behavior, the processor-to-engine event boundary must become explicit.

Recommended per-block flow:

```text
incoming MidiBuffer
  -> parse into compact EngineMidiEvent records with sample offsets
  -> split note events from performance/controller events
  -> feed transport snapshot + event span into SynthEngineV2
```

Recommended `EngineMidiEvent` categories:

- note on
- note off
- pitch bend
- mod wheel
- sustain pedal
- optional later extensions such as aftertouch/channel pressure

Design requirements for this event layer:

- note and performance events keep their sample offsets within the current block,
- arp consumes note events before allocator dispatch,
- pitch bend, mod wheel, and sustain feed dedicated synth-engine performance state rather than APVTS automation writes,
- `SynthAudioProcessor` remains responsible for parsing host MIDI, but `SynthEngineV2` owns the musical interpretation.

## 12.1 MIDI Split

V2 should preserve the conceptual split:

```text
notes and note-like performance input
  -> arp / allocator / synth engine

CC input
  -> mapping engine
  -> parameter changes or commands
```

Pitch bend and mod wheel are note-performance input, not generic CC mapping features.

## 12.2 Standalone Hardware MIDI

Standalone should continue to:

- own one active hardware MIDI input at a time,
- auto-detect bundled controller profiles,
- allow MIDI learn overrides,
- remain resilient to disconnects.

## 12.3 Plugin MIDI

Plugin mode should continue to:

- rely on host MIDI routing,
- use lock-free queues or equivalent handoff for mapped CC actions,
- keep host-notifying parameter writes off the audio thread.

Important correction relative to the current codebase:

- the current queued mapped-action implementation is not an acceptable V2 endpoint because it still routes mapped-action application through `getCallbackLock()`,
- V2 should not treat that callback-lock-based dispatch path as a valid realtime-safe architecture,
- V2 should replace it with a control-update bridge that preserves host-notifying parameter writes off the audio thread without introducing callback-lock contention against `processBlock()`.

Recommended V2 pattern:

```text
audio thread MIDI parse
  -> lock-free enqueue of mapped parameter intents / commands
  -> non-audio thread host-notify application
```

The plugin MIDI learn UI may continue to observe incoming controller events through a queue, but the queue handoff itself must remain non-blocking and must not depend on the processor callback lock for routine operation.

## 13. UI Design

The V2 UI should preserve the "instrument panel" feel while acknowledging that V2 has more controls than V1.

Recommended layout strategy:

- keep the voice core always visible,
- move dense secondary sections into a detail panel rather than forcing every control onto one flat page.

Recommended shell:

```text
Header
  -> title
  -> patch actions (standalone)
  -> MIDI learn status
  -> panic

Persistent core panel
  -> Oscillators
  -> Mixer
  -> Filter
  -> Filter Envelope
  -> Amp Envelope

Detail panel switcher
  -> Mod
  -> Performance / Arp
  -> FX

Footer
  -> standalone status bar when applicable
```

This resolves the current open UI tension:

- the synth core stays visible,
- FX sprawl is contained,
- the panel still reads as one instrument.

## 13.1 Standalone-Only UI

Preserve from V1:

- settings dialog
- patch actions
- status bar
- MIDI device handling
- monitor access

Do not duplicate device controls into the main synth panel.

## 13.2 Plugin UI

Preserve from V1:

- no hardware device selectors
- no standalone settings dialog
- no standalone patch buttons

Add for V2:

- arp section visibility
- performance controls
- grouped FX control surfaces

## 14. Realtime and Threading Rules

The V2 design keeps the same hard audio-thread rules as V1:

- no heap allocation during normal rendering,
- no locks in the render path,
- no file I/O,
- no UI mutation,
- no host-notifying parameter writes in the audio thread.

Additional V2-specific rules:

- host transport lookup occurs only in `processBlock()`,
- arp timing state is advanced in the audio thread only,
- all voice, arp, and FX objects are preallocated,
- modulation routing decisions should be represented as compact enums/bitmasks rather than string lookups,
- any randomization used by vintage mode must use deterministic preallocated state.

## 14.1 Reset and Tail Semantics

Because V2 adds latch-capable arp behavior and longer global effects, host-facing lifecycle behavior must be explicit.

`SynthAudioProcessor::reset()` in V2 should:

- clear active voice output immediately,
- clear glide/interpolation state,
- clear sustain state,
- clear or release arp-generated active notes,
- clear latched arp state,
- reset delay/reverb/chorus buffers and modulation phases to a safe baseline.

`panic` should be stronger than musical stop behavior and should always force the full synth and FX system silent immediately.

Transport-stop behavior should be defined separately from reset:

- host transport stop may pause host-synced arp advancement,
- host transport stop should not implicitly destroy patch state or rewrite latched settings,
- reset and panic are the authoritative flush points.

`getTailLengthSeconds()` should no longer remain a hard-coded zero in V2.

Recommended contract:

- if the enabled FX topology can ring out audibly, report a finite conservative tail estimate,
- if tail length depends on parameters, report a conservative upper bound for the supported first-release FX design,
- keep the estimate simple and host-friendly rather than perfectly analytical.

## 15. Migration from Current Code

The following current components should be preserved conceptually:

- `SynthAudioProcessor`
- APVTS ownership
- MIDI mapping and learned binding infrastructure
- standalone shell, status bar, settings flow
- patch/state plumbing patterns

The following components should be substantially redesigned:

- `src/synth/SynthVoice.*`
- `src/synth/SynthEngine.*`
- `src/synth/SynthParameters.h`
- `src/parameters/ParameterLayout.*`
- `src/plugin/SynthAudioProcessorEditor.*`

Recommended migration path:

1. Introduce V2 parameter IDs and V2 parameter layout.
2. Introduce `SynthEngineV2` alongside the old engine, not by mutating V1 classes in place first.
3. Introduce a custom `VoiceAllocator` and `SynthVoiceV2`.
4. Add arp timing and note generation.
5. Add voice core: dual oscillators, mixer, dual envelopes, filter.
6. Add performance behavior: bend, glide, unison, vintage, pan spread.
7. Add global FX rack.
8. Replace editor wiring and controller mappings with the V2 parameter contract.
9. Cut over state/patch versioning.

## 16. Risks and Mitigations

## Risk: Custom Voice Allocator Adds Complexity

Mitigation:

- keep allocator small and deterministic,
- keep voice DSP independent from allocation policy,
- prefer explicit note-state structures over hidden behavior.

## Risk: Oscillator Design Balloons

Mitigation:

- implement only the required shapes and behaviors,
- avoid wavetable/generalized oscillator abstractions,
- keep sync and PWM support focused on the Prophet-style path.

## Risk: Filter Character Becomes a Rabbit Hole

Mitigation:

- ship one strong 4-pole character first,
- treat rev-switch-style variants as optional only after the base filter is solid.

## Risk: Arp Host Sync Differs by Host

Mitigation:

- always define fallback internal timing,
- design the plugin arp to remain useful even with incomplete host timing.

## Risk: UI Density

Mitigation:

- keep core voice controls always visible,
- page dense secondary controls instead of collapsing everything into tabs that hide the instrument.

## 17. Recommended Implementation Order

The recommended V2 build order is:

```text
Create V2 parameter IDs and APVTS layout
  -> create SynthEngineV2 shell
  -> create custom VoiceAllocator
  -> implement dual-oscillator voice core
  -> implement dual envelopes
  -> implement 4-pole filter path
  -> implement pitch bend / glide / unison / vintage
  -> implement arp with standalone and plugin timing paths
  -> implement global drive / chorus / delay / reverb
  -> rebuild editor around grouped V2 sections
  -> port MIDI learn / controller mapping to V2 parameter set
  -> add V2 patch/state versioning
  -> remove V1 engine path when V2 validation is complete
```

## 18. Final Design Position

V2 should remain a real JUCE instrument plugin, not become a framework exercise.

The right shape is:

- JUCE for processor, parameter, wrapper, and utility infrastructure,
- a custom synth engine for voice allocation and DSP behavior,
- a constrained Prophet-inspired architecture,
- a soundtrack-capable but disciplined FX rack,
- one coherent monotimbral instrument with a strong dry core.

That is the smallest design that can honestly satisfy the V2 requirements without turning CoolSynth into either a V1 patch-up or a generic modern synth workstation.
