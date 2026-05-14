# Requirements Document: CoolSynth V2

## 1. Purpose

This document defines the high-level requirements for `CoolSynth` V2.

V2 is not a general feature expansion. It is a deliberate repositioning of the instrument from the current learning-oriented "single-oscillator polysynth with envelope/filter/delay" into a more convincing analog-style subtractive synthesizer with:

- Prophet-5-inspired synthesis architecture as the primary design reference.
- Stranger Things / S U R V I V E-inspired musical results as the primary sound-design target.
- Shared standalone and VST3 behavior preserved.

The intent is to produce a synth that is still understandable and maintainable, but no longer sonically generic.

The following product decisions are now fixed for V2:

- Polyphony shall modernize beyond strict Prophet-5 limits.
- The control set shall be a tasteful modern superset of the Prophet-5 model, not a strict historical replica.
- V2 may break V1 preset, patch, and state compatibility.
- V2 shall include a significantly expanded onboard effects section.
- Implementation priority shall favor a strong dry synth engine first, then layer in the larger effects palette.

## 2. Current Implementation Snapshot

As of 2026-05-14, the current implementation already provides:

- Polyphonic voice allocation with 8 preallocated voices and release-first voice stealing.
- One oscillator per voice with a global waveform choice: sine, square, saw.
- One per-voice ADSR amplitude envelope.
- One per-voice low-pass filter with cutoff and resonance.
- One global delay and final master gain stage.
- Standalone and VST3 targets, parameter automation, controller profiles, and MIDI learn.

Important current limitations relative to the V2 target:

- No second oscillator per voice.
- No mixer section for balancing multiple sound sources.
- No dedicated filter envelope.
- No LFO-based performance modulation section.
- No Poly Mod-style cross-modulation.
- No oscillator sync.
- No pulse-width modulation architecture.
- No glide or unison mode.
- No explicit analog drift or per-voice variance model.
- No meaningful Prophet-style performance section.
- No pitch-bend implementation in the current voice layer.

V2 shall build on the existing app/plugin shell, MIDI plumbing, parameter infrastructure, and persistence work. It shall not reset the project back to a prototype.

## 3. External Reference Anchors

These online references were checked on 2026-05-14 and inform the V2 direction:

- Sequential Prophet-5/10 product page:
  https://sequential.com/classics-reissued/prophet-5-10/
- Sequential Prophet-5 User's Guide v1.3:
  https://sequential.com/wp-content/uploads/2021/02/Prophet-5-Users-Guide-1.3.pdf
- Sound On Sound coverage of the Stranger Things score:
  https://www.soundonsound.com/techniques/scoring-stranger-things
- Classic FM overview of the Stranger Things theme instrumentation:
  https://www.classicfm.com/discover-music/periods-genres/film-tv/stranger-things-soundtrack-theme-song/
- Mixmag summary of Kyle Dixon and Michael Stein discussing the soundtrack tools:
  https://mixmag.net/read/stranger-things-composers-discuss-the-soundtrack-for-song-exploder-podcast-news
- Reverb overview of the soundtrack's retro synth character:
  https://reverb.com/uk/news/the-synth-sounds-of-stranger-things

The main takeaways from those references are:

- Prophet-5 identity is strongly tied to dual VCO voices, a resonant low-pass filter, separate filter and amp envelopes, Poly Mod, unison, glide, and voice-to-voice organic variance.
- The Stranger Things / S U R V I V E sound is not just "80s synthwave." It depends on restraint, darkness, simple motifs, heavy use of analog tone sources, pulsing basses, eerie pads, arpeggiated figures, and selective use of modulation and filtering.
- The Stranger Things score was created with multiple instruments and processing chains, so V2 should target that shared tonal center rather than promise one-synth reproduction of every cue.
- V2 should prioritize strong raw tone and musical sweet spots over a large modulation matrix or a long feature list.

## 4. V2 Product Definition

V2 shall be a polyphonic analog-style subtractive synthesizer with immediate panel-style control.
V2 shall remain a single-part, monotimbral instrument.

V2 shall feel closer to:

- a programmable vintage polysynth with a focused architecture,
- a vintage-inspired synth modernized for software expectations,
- a performance instrument with clear sweet spots,
- a score-friendly and synthwave-capable sound source,

than to:

- a modern all-purpose sound-design workstation,
- a modulation-matrix playground,
- a wavetable or hybrid digital synth.

## 5. Primary Sound Target

V2 shall be able to produce, with minimal patching effort:

- dark pulsing basses,
- warm detuned synth brass,
- unstable pads,
- ominous drones,
- simple repeating arpeggios,
- narrow, eerie leads,
- percussive analog stabs,
- tension-building filter sweeps.

The target character is:

- warm but not soft,
- present but not glossy,
- dark and tense rather than euphoric,
- simple and direct rather than hyper-produced,
- organic and slightly unstable rather than clinically static.

V2 shall avoid defaulting toward:

- EDM supersaw aesthetics,
- bright trance-style sheen,
- excessive stereo spectacle,
- dense effect-heavy presets that mask weak source tone.

Even with a larger V2 effects section, the instrument shall still succeed from an init patch and dry signal path.

## 6. Core Synthesis Requirements

### 6.1 Voice Architecture

Each voice in V2 shall follow a Prophet-inspired subtractive path:

- Oscillator mixer
- Per-voice filter
- Per-voice amplifier

The minimum V2 per-voice sound sources shall be:

- Oscillator A
- Oscillator B
- Noise source

The minimum V2 per-voice modulation contour generators shall be:

- Filter ADSR envelope
- Amplifier ADSR envelope

The minimum V2 global modulation sources shall be:

- One LFO
- Performance controls such as mod wheel and pitch bend

### 6.2 Oscillator Section

V2 shall replace the current single global waveform choice with a true dual-oscillator section.

Required oscillator capabilities:

- Independent Oscillator A and Oscillator B controls.
- Per-oscillator tuning controls sufficient for interval and detune work.
- Pulse-capable waveform support.
- Saw-capable waveform support.
- Triangle-capable waveform support.
- Dedicated pulse-width control wherever pulse wave generation is supported.
- A way to produce classic PWM and sync-style tones.
- Oscillator B low-frequency mode or an equivalent architecture needed to support Prophet-style cross-mod behavior.
- Oscillator sync.

Strongly preferred:

- Prophet-like waveform behavior rather than arbitrary digital shape browsing.
- A limited set of musically useful wave options instead of a large waveform list.
- A constrained wave model that can cover Prophet-adjacent results without requiring a large modern wavetable-style browser.
- Oscillator wave selection that either:
  - supports simultaneous multi-wave combinations in a Prophet-adjacent way, or
  - explicitly documents a simplified alternative and preserves the required tonal categories despite that simplification.
- Per-voice oscillator drift/slop interaction with the vintage behavior model.

V2 shall not keep the V1 model where one waveform parameter changes every voice's only oscillator shape globally.

### 6.3 Mixer Section

V2 shall introduce an explicit voice mixer section.

Required source controls:

- Oscillator A level
- Oscillator B level
- Noise level

The mixer shall be able to create:

- single-oscillator sounds,
- detuned dual-oscillator sounds,
- oscillator-plus-noise textures,
- filter-driven timbral movement from richer source material than V1.

### 6.4 Filter Section

V2 shall use the filter as a defining musical component, not as a secondary tone shaper.

Required filter capabilities:

- Per-voice resonant low-pass filter.
- Dedicated cutoff control.
- Dedicated resonance control.
- Dedicated envelope amount control for the filter envelope.
- Keyboard tracking control or a simplified equivalent.

Strongly preferred:

- A 4-pole Prophet-adjacent SSM/Curtis-style musical response rather than a neutral utility filter.
- A subtle drive or nonlinearity stage somewhere around the filter path.
- Optional filter character modes only if they do not significantly complicate the codebase.

V2 shall not rely on a single shared envelope for both loudness and filter motion.

### 6.5 Envelope Section

V2 shall provide two distinct ADSR envelopes:

- Filter envelope
- Amplifier envelope

Required behavior:

- Both envelopes are per-voice.
- Both envelopes retrigger correctly on stolen and repeated voices.
- Envelope response is fast enough for brass, plucks, and basses.
- Envelope timing contributes materially to the sound character.

Strongly preferred:

- Envelope timing variation can participate in the vintage behavior model.
- The UI layout makes it obvious which envelope affects tone and which affects loudness.

### 6.6 Modulation Section

V2 shall introduce a focused modulation architecture modeled after classic subtractive synth practice, not a full matrix.

Required modulation capabilities:

- One global LFO with a small set of musical waveforms.
- Mod wheel control over LFO depth or equivalent performance modulation.
- Poly Mod-inspired routing where Oscillator B and/or the filter envelope can modulate selected destinations.

Required Poly Mod-style destinations:

- Oscillator pitch modulation target
- Pulse-width modulation target
- Filter cutoff modulation target

Strongly preferred:

- Prophet-style constraint and immediacy over modern routing flexibility.
- A few carefully chosen extra routings beyond the original Prophet model when they materially improve usability.
- Audio-rate modulation behaviors that can produce metallic, unstable, or aggressive timbres without becoming unmanageable.

Explicit non-requirement for V2:

- No general-purpose modulation matrix.

### 6.7 Arpeggiator

Because the musical target leans heavily on repeating pulse and sequence figures, V2 shall include a built-in arpeggiator.

The V2 arpeggiator shall be intentionally constrained:

- It shall be a musical performance tool, not a general-purpose step sequencer.
- It shall be bypassable.
- It shall not block ordinary non-arp keyboard play when disabled.

Minimum required arpeggiator controls:

- On/off
- Rate
- Pattern
- Octave range
- Gate length
- Latch

Minimum required arpeggiator behavior:

- Up pattern
- Down pattern
- Up/down pattern
- As-played or note-order-respecting pattern

Timing requirements:

- In plugin mode, the arpeggiator shall sync to host tempo and transport when the host provides that information.
- In plugin mode, if the host does not provide usable tempo and/or transport information, the arpeggiator shall fall back to a clearly defined internal-rate behavior rather than becoming unusable or ambiguous.
- In standalone mode, the arpeggiator shall provide its own tempo setting.

Explicit non-requirement for V2:

- No full piano-roll, tracker, or step-sequencer workflow.

### 6.8 Performance Section

V2 shall add a real performance layer missing from the current engine.

Required behavior:

- Pitch bend works.
- Mod wheel works.
- Glide/portamento exists.
- Unison mode exists.
- Key priority and retrigger behavior are defined for mono/unison operation.

Strongly preferred:

- A software "Vintage" or "Slop" control that increases voice-to-voice variance in a controlled way.
- A software "Pan Spread" or equivalent stereo-width behavior, as long as the dry tone remains strong in mono.
- Velocity routing at least to amplifier level, with optional routing to filter amount.

Open implementation choice:

- Whether aftertouch is required in V2 depends on controller and host expectations. It is useful, but not mandatory for the first V2 delivery.

### 6.9 Polyphony and Voice Behavior

V2 shall remain polyphonic.

Default requirement:

- V2 shall ship with a modern default voice count rather than a strict 5-voice limit.

Minimum requirement:

- At least 8 simultaneous voices.

Strongly preferred:

- A selectable vintage-limited mode such as 5 voices for users who want more original-style behavior.

Required voice behavior:

- Voice stealing remains musically defensible.
- Mono/unison behavior is deliberate, not accidental reuse of polyphonic logic.
- Drift, glide, and retrigger behavior do not create obvious bugs or stuck states.

### 6.10 Minimum V2 Parameter Surface

The V2 requirements are not satisfied unless the first V2 release defines an explicit parameter surface large enough to support automation, presets, MIDI learn, and controller mapping.

All sound-shaping controls intended for:

- UI editing,
- host automation,
- preset recall,
- patch serialization,
- MIDI learn,

shall exist as stable V2 parameters from the start.

The minimum required V2 parameter surface shall include:

- Oscillator A: waveform or wave mode, pitch range or octave, fine tune where applicable, level, pulse width where applicable, sync enable.
- Oscillator B: waveform or wave mode, pitch range or octave, fine tune, level, pulse width where applicable, low-frequency mode.
- Mixer: noise level.
- Filter: cutoff, resonance, envelope amount, keyboard tracking.
- Filter envelope: attack, decay, sustain, release.
- Amplifier envelope: attack, decay, sustain, release.
- LFO: rate, waveform, and the routed depth controls required by the chosen constrained modulation design.
- Poly Mod: the source amounts, routing selectors, or routing switches required by the chosen constrained modulation design.
- Performance: glide amount or time, unison enable, pitch-bend range, vintage or slop amount, pan spread, velocity amount.
- Arpeggiator: on/off, rate, pattern, octave range, gate length, latch.
- Effects: delay controls, chorus or ensemble controls, saturation or drive controls, reverb controls, plus whatever mix, bypass, enable, or routing controls are required to make the mandatory first-release effects chain intentionally controllable rather than permanently on.
- Output: master gain.

The exact control names and ranges are a design task, but omitting any of those categories from the first V2 parameter contract would be a requirements miss.

## 7. Effects and Finishing Stage Requirements

V2 shall include a materially larger global effects and finishing stage than V1, but the synth core must still matter more than the FX chain.

For the first V2 release, the effects architecture shall remain global post-voice processing. Per-voice effects are out of scope for the first V2 release.

Required:

- Keep a delay stage or an equivalent echo effect.
- Preserve a final output gain stage.
- Add multiple additional effects beyond delay.
- The first V2 release shall include at least:
  - delay,
  - chorus or ensemble,
  - saturation or drive,
  - reverb.

Strongly preferred:

- Darker and more musical delay voicing than a neutral utility delay.
- Chorus and/or ensemble treatment for pads and widening.
- Saturation, drive, or soft clipping for weight and aggression.
- Reverb appropriate for cinematic tails and space.
- Modulation-capable effects such as phaser, flanger, or tremolo when they support the target palette.
- An effects order and bypass strategy that keeps patch design understandable.

Effects scope rules:

- The mandatory first-release effects shall be global only.
- Additional effects such as phaser, flanger, or tremolo are optional later-expansion candidates, not mandatory blockers for the first V2 release.
- Lookahead, convolution, and other obviously latency-heavy effect designs are not required for the first V2 release.
- Time-based effects may support host sync where practical, but host sync is not a release blocker except for the arpeggiator.

V2 shall avoid turning into an "FX-first" synth where presets only sound impressive because of large ambience.

The V2 effects architecture should support both:

- dry, direct analog-style patches,
- heavily produced soundtrack patches.

## 8. UI and Workflow Requirements

V2 shall present the synth as a coherent instrument panel.

Required panel groupings:

- Oscillators
- Mixer
- Filter
- Filter envelope
- Amplifier envelope
- Modulation
- Performance
- Effects
- Output

Required workflow qualities:

- Fast to program from init patch.
- Easy to make bass, brass, pad, and arpeggio patches without menu diving.
- Patch-init behavior that exposes the raw engine clearly.
- MIDI learn and controller mapping preserved for the new parameter set.

The UI shall feel closer to a real instrument front panel than to a generic parameter inspector.

Because V2 includes more effects than V1, the UI shall also prevent effect sprawl from overwhelming the core synth controls. The oscillator, mixer, filter, envelope, and performance sections shall remain primary.

## 9. State, Compatibility, and Migration

V2 introduces a fundamentally different sound engine, and V1 compatibility is not required.

Required decisions and behaviors:

- Any parameter whose meaning, range, taper, unit, polarity, default, modulation role, or routing behavior changes materially from V1 shall receive a new V2 parameter ID.
- Reusing a V1 parameter ID is permitted only for a semantically equivalent control and shall not be done merely for continuity.
- New V2 parameters shall get stable IDs from the start.
- V2 state, preset, and patch formats may intentionally diverge from V1.
- The project shall document the compatibility break explicitly rather than attempting silent best-effort migration.
- V1 states or patches shall either be rejected clearly or handled by an explicit import path. They shall not be loaded silently into mismatched V2 semantics.

## 10. Real-Time and Engineering Requirements

V2 shall preserve the current real-time-safety standards.

Hard requirements:

- No heap allocation in normal audio rendering.
- No locking in the audio path.
- No UI-thread coupling into live DSP objects.
- No broad architectural regression in standalone/plugin sharing.

V2 shall also preserve:

- Standalone and VST3 targets.
- Existing MIDI learn concepts.
- Existing patch save/load concepts, even if the patch format version must change.

V2 should remain understandable to a motivated reader. This project is allowed to become more sophisticated, but not obscure.

## 11. Explicit Non-Goals for V2

The following are not required for V2 unless a later decision changes scope:

- Wavetable synthesis
- Granular synthesis
- Sample playback
- Full modulation matrix
- MPE
- Microtonal workflow
- Multi-timbrality
- Built-in sequencer
- Circuit-perfect Prophet-5 clone behavior

V2 is inspired by the Prophet-5 and by the Stranger Things / S U R V I V E palette. It is not required to be a legal, visual, or circuit-level clone of any hardware instrument.

## 12. Success Criteria

V2 should be considered successful only if all of the following are true:

- The raw oscillator/filter/envelope path is dramatically more characterful than V1.
- Dual-oscillator subtractive patches feel like the default mode of operation, not a bolt-on.
- Filter movement and cross-modulation can produce recognizably vintage and cinematic results.
- The synth can cover core Prophet-adjacent categories: bass, brass, pad, sync lead, PWM motion, dark arp.
- The synth can also reach Stranger Things / S U R V I V E-adjacent categories: ominous pulse, eerie pad, analog drone, score texture.
- The built-in arpeggiator can generate musically useful repeating patterns without requiring an external sequencer or DAW MIDI effect.
- The expanded effects section meaningfully broadens the soundtrack palette without obscuring the core voice character.
- The instrument remains playable and stable in standalone and plugin form.

## 13. Remaining Design Questions

These questions remain open at the design level even after the core product direction is fixed:

1. What exact default voice count should V2 ship with: 8, 12, or 16?
2. How far should the analog-imperfection model go relative to code simplicity and educational readability?
3. Should the expanded V2 panel stay single-page, or should effects move to tabs/pages while the synth core stays always visible?
4. Which optional later-expansion effects, if any, should join the mandatory first-release set after delay, chorus or ensemble, drive, and reverb?

## 14. Recommended Direction

If no further clarification is provided, the recommended V2 interpretation is:

- Prophet-5-inspired architecture, not a literal clone.
- Strong dry dual-oscillator subtractive engine first.
- Separate filter and amp envelopes.
- Focused Poly Mod and LFO performance modulation, with a tasteful modern superset where useful.
- Built-in arpeggiator included as a constrained musical tool rather than a sequencer.
- Unison, glide, drift, noise, and modernized voice-count behavior included.
- Delay retained and joined by a broader set of effects suitable for soundtrack work.
- Core implementation order remains source tone first, effects second.
- Higher-than-5 default polyphony with a vintage-mode story.

That direction is the most likely to satisfy both the architecture reference and the musical target without turning V2 into a generic modern synth.
