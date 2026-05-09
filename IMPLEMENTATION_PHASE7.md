<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 7 Blueprint: Fixed MiniLab Core Controller Mapping

## Phase Selection

Selected phase: `Phase 7 - Fixed MiniLab core controller mapping`

Selection basis:

- `TODO.md` is still pinned to `Phase 5`, and every Phase 5 checkbox is already checked.
- `DONE.md` records `Phase 5` as complete.
- `IMPLEMENTATION_PHASE6.md` already exists and covers the MiniLab message-capture contract for `Phase 6`.
- There is no `IMPLEMENTATION_PHASE7.md` in the repository, so the next missing surgical blueprint is `Phase 7`.
- `IMPLEMENTATION_PLAN.md` explicitly separates message capture from controller mapping so verified hardware facts exist before any parameter-routing code lands.

This blueprint assumes `Phase 6` has already produced one authoritative MiniLab message table in code, or at minimum a verified document that can be promoted into code without re-capturing the hardware.

## Scope Guardrails

In scope for this phase:

- Add a fixed MiniLab mapping path for the already-verified `Phase 7` control set only.
- Route mapped controller changes into the canonical APVTS parameter model.
- Keep parameter mutation off the audio callback.
- Reuse the existing UI attachments so controller changes reflect automatically in the editor.
- Add an explicit command path for panic so hardware-triggered actions do not masquerade as automatable parameters.

Explicitly out of scope for this phase:

- Filter mappings for `Knob 2` and `Knob 3`.
- Delay mappings for `Knob 8`, `Fader 2`, or `Fader 3`.
- Generic controller support.
- MIDI learn.
- Any new synth DSP.
- Any new plugin-host CC remapping path.
- Any controller-specific logic in `SynthEngine`, `SynthVoice`, or parameter-layout files.

Decision gate before implementation:

- `IMPLEMENTATION_PLAN.md` expects a panic command path in this phase.
- `REQUIREMENTS.md` keeps pads and the main encoder deferred unless `Phase 6` confirmed simple default behavior.
- If `Phase 6` did not verify any safe fixed control for panic, do not invent one in code. Land the command infrastructure, then update the plan or requirements before claiming the mapped panic checkbox complete.

## Current Code Anchors

The design for this phase should stay anchored to the seams that already exist:

- `src/standalone/StandaloneMidiInput.cpp` owns the standalone hardware MIDI callback.
- `src/midi/MidiMonitor.cpp` already captures MIDI monitor events in a fixed-size queue.
- `src/plugin/SynthAudioProcessor.cpp` already owns the canonical parameter model and the panic request flag.
- `src/plugin/SynthAudioProcessorEditor.cpp` already wires APVTS attachments and constructs the standalone MIDI panel only in standalone mode.
- `src/ui/StandaloneMidiInputPanel.cpp` currently owns the standalone MIDI controller object.
- `CMakeLists.txt` uses explicit `target_sources(...)`, so every new `.cpp` file must be added manually.

Important current behavior constraints:

- The standalone MIDI callback currently mirrors incoming messages into the MIDI monitor and does not yet route controller traffic anywhere else.
- The shared editor already reflects APVTS changes through existing attachments and a timer-based value refresh. No extra UI synchronization mechanism should be introduced.
- `SynthAudioProcessor::processBlock(...)` currently clears panic requests and renders from cached raw parameter atomics. It must stay free of host-notifying parameter writes.

## Exact `TODO.md` Entries This Blueprint Expands

These are the `Phase 7` checklist items from `IMPLEMENTATION_PLAN.md` that this document turns into execution steps:

- [ ] Add `MidiMappingEngine` for fixed MiniLab 3 controller routing.
- [ ] Route verified MiniLab waveform, ADSR, and master-gain controls to APVTS parameters.
- [ ] Route the panic action through an explicit command path.
- [ ] Keep MiniLab-specific constants isolated from synth-engine and voice code.
- [ ] Verify controller-driven parameter changes update the UI in standalone mode.
- [ ] Verify no host-notifying parameter writes occur in the audio callback.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Keep the MiniLab-specific message contract isolated, keep translation logic in the shared processor domain, and keep actual parameter mutation on a non-audio thread owned by the standalone ingress path.

Required consequences:

- Hardware MIDI ingress remains standalone-only.
- Controller-to-parameter translation remains shared logic so later milestones can reuse it.
- APVTS stays the only sound-shaping state model.
- `processBlock(...)` remains read-only with respect to parameters.
- The existing UI attachments remain the only UI synchronization path.

### 1.2 Runtime Flow

The intended `Phase 7` control path is:

```text
Standalone MIDI input callback thread
  -> StandaloneMidiInputController::handleIncomingMidiMessage(...)
     -> MidiMonitorBuffer::pushMessage(...)
     -> copy small controller event into fixed queue
     -> triggerAsyncUpdate()

Message thread
  -> StandaloneMidiInputController::handleAsyncUpdate()
     -> drain queued controller events
     -> forward each event to SynthAudioProcessor::handleStandaloneControllerEvent(...)

Shared processor
  -> MidiMappingEngine::translate(...)
     -> parameter change request OR explicit command
  -> SynthAudioProcessor::applyMappedAction(...)
     -> beginChangeGesture() / setValueNotifyingHost() / endChangeGesture()
     -> or requestPanic()

Audio callback
  -> processBlock(...)
     -> read raw parameter atomics only
     -> render synth
```

This flow deliberately avoids two failure modes:

- calling host-notifying parameter APIs from `processBlock(...)`
- mutating synth voices directly from controller messages

### 1.3 Required Data Structures

#### 1.3.1 Fixed-Size Controller Event

The standalone callback needs a trivially copyable event type that preserves only the data required for `Phase 7` routing.

Recommended type in `src/midi/MidiMappingEngine.h`:

```cpp
namespace coolsynth::midi
{
    enum class ControllerMidiEventType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        other,
    };

    struct ControllerMidiEvent
    {
        ControllerMidiEventType type = ControllerMidiEventType::other;
        uint8_t channel = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
    };
}
```

Semantics:

- `data1` is note number for note events and CC number for control-change events.
- `data2` is velocity for note events and controller value for CC events.
- `other` exists so the queue can ignore unsupported traffic safely without needing to allocate or retain full `juce::MidiMessage` objects.

#### 1.3.2 Mapping Output Model

The translation layer must resolve incoming MiniLab events into exactly one of three outcomes: no action, parameter change, or explicit command.

Recommended types:

```cpp
namespace coolsynth::midi
{
    enum class MappingCurve : uint8_t
    {
        linearNormalized,
        waveformChoice3Step,
    };

    enum class MappedCommand : uint8_t
    {
        none,
        panic,
    };

    struct ParameterTarget
    {
        juce::RangedAudioParameter* parameter = nullptr;
        MappingCurve curve = MappingCurve::linearNormalized;
        uint8_t discreteStepCount = 0;
    };

    struct MappedParameterChange
    {
        juce::RangedAudioParameter* parameter = nullptr;
        float normalizedValue = 0.0f;
    };

    struct MappedAction
    {
        enum class Kind : uint8_t
        {
            none,
            parameterChange,
            command,
        };

        Kind kind = Kind::none;
        MappedParameterChange parameterChange {};
        MappedCommand command = MappedCommand::none;
    };
}
```

Design intent:

- `ParameterTarget` caches the APVTS-owned parameter pointer outside the audio thread.
- `normalizedValue` is the exact value passed to `setValueNotifyingHost(...)`.
- `MappedCommand` keeps panic separate from parameter automation and serialized state.

#### 1.3.3 MiniLab Profile Binding Table

`Phase 6` should already have introduced `Minilab3Profile`. `Phase 7` should extend or consume it with one binding table that covers only the verified control set required now.

Recommended additions in `src/midi/Minilab3Profile.h`:

```cpp
namespace coolsynth::midi
{
    enum class Minilab3LogicalTarget : uint8_t
    {
        waveform,
        ampAttack,
        ampDecay,
        ampSustain,
        ampRelease,
        masterGain,
        panic,
    };

    struct Minilab3Binding
    {
        std::string_view controlId;
        ControllerMidiEventType expectedEventType = ControllerMidiEventType::controlChange;
        uint8_t primaryData = 0;
        Minilab3LogicalTarget target = Minilab3LogicalTarget::waveform;
        MappingCurve curve = MappingCurve::linearNormalized;
        bool enabled = true;
    };

    std::span<const Minilab3Binding> getPhase7Bindings() noexcept;
}
```

Binding rules:

- Each active binding row must correspond to a `Phase 6` verified control.
- Deferred controls must remain present in the broader profile metadata but absent or disabled in `getPhase7Bindings()`.
- If no panic-capable control was verified, keep the panic row absent rather than encoding a guess.

#### 1.3.4 Standalone Controller Queue State

`StandaloneMidiInputController` needs one small FIFO for controller events so the MIDI callback does no host-notifying work.

Recommended private state additions:

```cpp
std::array<coolsynth::midi::ControllerMidiEvent, 128> pendingControllerEvents {};
juce::AbstractFifo pendingControllerEventQueue { 128 };
std::atomic<uint32_t> droppedControllerEventCount { 0 };
std::atomic<bool> deviceRefreshPending { false };
```

Queue policy:

- Drop newest on overflow.
- Do not block.
- Do not allocate.
- Do not log from the MIDI callback.
- Preserve device-refresh behavior and controller-event forwarding in the same `AsyncUpdater` without mixing their state.

#### 1.3.5 Processor State Additions

`SynthAudioProcessor` needs one shared translation object and one narrow application surface.

Recommended state additions:

```cpp
coolsynth::midi::MidiMappingEngine midiMappingEngine;
```

Optional but useful state if duplicate suppression proves necessary:

```cpp
std::array<float, 6> lastStandaloneNormalizedValues;
```

Only add duplicate suppression if actual knob/fader traffic shows redundant repeated values. Do not speculate unless validation demonstrates gesture or UI churn.

### 1.4 Function Signatures

Recommended function surface for the phase:

```cpp
namespace coolsynth::midi
{
    class MidiMappingEngine
    {
    public:
        explicit MidiMappingEngine(juce::AudioProcessorValueTreeState& state);

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        static float mapControllerValue(uint8_t midiValue,
                                        const ParameterTarget& target) noexcept;

        static float mapWaveformChoice(uint8_t midiValue,
                                       const ParameterTarget& target) noexcept;

        std::span<const Minilab3Binding> bindings;
        std::array<ParameterTarget, 6> parameterTargets;
    };
}
```

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);

private:
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;
};
```

```cpp
namespace coolsynth::standalone
{
    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        using ControllerEventHandler = std::function<void(const coolsynth::midi::ControllerMidiEvent&)>;

        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer,
                                      ControllerEventHandler onControllerEvent,
                                      DisconnectCallback onSelectedDeviceDisconnected = {});

    private:
        void enqueueControllerEvent(const juce::MidiMessage& message) noexcept;
        int drainControllerEvents(coolsynth::midi::ControllerMidiEvent* destination,
                                  int maxEvents) noexcept;
    };
}
```

```cpp
class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    using ControllerEventHandler = std::function<void(const coolsynth::midi::ControllerMidiEvent&)>;

    explicit StandaloneMidiInputPanel(ControllerEventHandler onControllerEvent,
                                      std::function<void()> onSelectedDeviceDisconnected = {});
};
```

Design note:

- `SynthAudioProcessor::handleStandaloneControllerEvent(...)` is intentionally non-`noexcept` because APVTS host-notifying APIs are not callback-thread code paths and should not be force-fit into a callback-style exception contract.

### 1.5 Mapping Rules by Control

The phase should implement only the fixed mapping rows that are both required by `Phase 7` and already verified by `Phase 6`.

| Verified control role | Required message kind | Logical target | Parameter ID or command | Transform |
| --- | --- | --- | --- | --- |
| Knob 1 | `controlChange` | `waveform` | `waveform` | 3-step discrete choice |
| Knob 4 | `controlChange` | `ampAttack` | `ampAttackMs` | normalized 0.0 to 1.0 |
| Knob 5 | `controlChange` | `ampDecay` | `ampDecayMs` | normalized 0.0 to 1.0 |
| Knob 6 | `controlChange` | `ampSustain` | `ampSustain` | normalized 0.0 to 1.0 |
| Knob 7 | `controlChange` | `ampRelease` | `ampReleaseMs` | normalized 0.0 to 1.0 |
| Fader 1 | `controlChange` | `masterGain` | `masterGainDb` | normalized 0.0 to 1.0 |
| Verified panic control from `Phase 6`, if one exists | verified by hardware capture | `panic` | explicit command | rising-edge trigger only |

Explicit non-mappings for this phase:

- `Knob 2` and `Knob 3` remain untouched until `Phase 8`.
- `Knob 8`, `Fader 2`, and `Fader 3` remain untouched until `Phase 9`.
- `Fader 4` remains unassigned.
- All deferred pads and the main encoder remain ignored unless `Phase 6` explicitly promoted one control into the panic path.

### 1.6 Parameter Mutation Rules

Parameter application rules must be strict:

- Use `beginChangeGesture()` and `endChangeGesture()` around each discrete controller-applied parameter mutation in the standalone path.
- Call `setValueNotifyingHost(normalizedValue)` only from the non-audio standalone forwarding path.
- Do not call `setValue(...)` directly unless there is a clear need to avoid host notification, which `Phase 7` does not have.
- Do not call any parameter-notifying API from `processBlock(...)`.
- Do not mutate UI controls directly. Let APVTS attachments reflect the change.

Waveform-specific rule:

- Convert `0..127` into exactly three buckets using integer math, then translate the chosen bucket into the parameter's normalized domain. Do not rely on floating-point rounding that can create unstable boundary behavior near bucket edges.

Panic-specific rule:

- Trigger panic on the verified control's active edge only.
- Ignore note-off, release, or zero-value follow-up messages so one physical action produces one panic request.

## 2. File-Level Strategy

The actual implementation phase should stay confined to the following files.

### Required Files

| File | Responsibility in `Phase 7` |
| --- | --- |
| `CMakeLists.txt` | Register any new `.cpp` files, specifically the new mapping engine and the profile implementation if it is not already in the target. |
| `src/midi/MidiMappingEngine.h` | Declare the shared controller-event model, mapping output model, and the engine interface. |
| `src/midi/MidiMappingEngine.cpp` | Implement binding lookup, controller-value transforms, and translation into parameter-change or command actions. |
| `src/midi/Minilab3Profile.h` | Expose the verified `Phase 7` binding table consumed by the mapping engine. |
| `src/midi/Minilab3Profile.cpp` | Define the actual verified binding rows and keep all MiniLab-specific constants isolated here. |
| `src/plugin/SynthAudioProcessor.h` | Add the standalone controller-event ingress and the mapping-engine member. |
| `src/plugin/SynthAudioProcessor.cpp` | Construct the mapping engine, translate controller events, apply APVTS changes off the audio thread, and route commands to `requestPanic()`. |
| `src/standalone/StandaloneMidiInput.h` | Extend the controller interface with a controller-event handler and queue state for deferred forwarding. |
| `src/standalone/StandaloneMidiInput.cpp` | Mirror incoming hardware messages into the monitor, enqueue small controller events, drain them on `handleAsyncUpdate()`, and preserve current device-selection behavior. |
| `src/ui/StandaloneMidiInputPanel.h` | Accept the new controller-event handler in the panel constructor. |
| `src/ui/StandaloneMidiInputPanel.cpp` | Pass the controller-event handler into `StandaloneMidiInputController` without changing the panel's UI responsibilities. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Wire the standalone MIDI panel to `processor.handleStandaloneControllerEvent(...)`. |
| `README.md` | Document the fixed controls implemented in this phase and any verified deferred controls that still do nothing by design. |

### Conditional Files

| File | Touch only if this condition is true |
| --- | --- |
| `src/plugin/SynthAudioProcessorEditor.h` | Only if the editor needs a dedicated helper method or typed panel member to keep the `.cpp` clean. |
| `REQUIREMENTS.md` | Only if the verified panic control or another `Phase 7` control contract differs from the current requirement assumptions. |
| `IMPLEMENTATION_PLAN.md` | Only if the mapped panic expectation must be narrowed because `Phase 6` evidence kept every candidate panic control deferred. |

### Files That Should Remain Untouched

Do not edit these unless a compile-only include adjustment is unavoidable:

- `src/synth/SynthEngine.{h,cpp}`
- `src/synth/SynthVoice.{h,cpp}`
- `src/parameters/ParameterIDs.h`
- `src/parameters/ParameterLayout.cpp`

Those files already express the shared synth state model. `Phase 7` should consume them, not reopen them.

## 3. Atomic Execution Steps

Each `Phase 7` checkbox should be executed as one tight `Plan -> Act -> Validate` loop.

### 3.1 Add `MidiMappingEngine` for fixed MiniLab 3 controller routing

Plan:

- Create one shared translation unit that converts a small `ControllerMidiEvent` into either a parameter change or a command.
- Depend on `Minilab3Profile` for all MiniLab constants.
- Cache APVTS parameter pointers in the engine constructor so lookup is O(1) and there is no string-based parameter lookup per event.

Act:

- Add `src/midi/MidiMappingEngine.h` and `src/midi/MidiMappingEngine.cpp`.
- Define `ControllerMidiEvent`, `MappedAction`, `MappingCurve`, and `MappedCommand` there.
- Add one constructor that binds required APVTS parameters by stable parameter ID.
- Implement `translate(...)` so it returns `Kind::none` for any non-matching event or deferred control.
- Register the new `.cpp` in `CMakeLists.txt`.

Validate:

- Build succeeds after adding the new files.
- A code read confirms no MiniLab CC or note constants appear in the engine implementation body except through the profile bindings.
- A code read confirms the engine performs no host-notifying parameter mutation itself.

### 3.2 Route verified MiniLab waveform, ADSR, and master-gain controls to APVTS parameters

Plan:

- Extend the standalone MIDI ingress so it forwards compact controller events to the processor on a non-audio thread.
- Let the processor own the translation engine and parameter-application step.
- Keep the parameter writes entirely inside `SynthAudioProcessor` so there is one narrow place to audit host-notifying calls.

Act:

- Extend `StandaloneMidiInputController` with a fixed-size controller-event queue.
- Push monitor events and controller events from the same incoming callback without blocking.
- Drain the controller-event queue in `handleAsyncUpdate()`.
- Add `SynthAudioProcessor::handleStandaloneControllerEvent(...)`.
- Translate Knob 1, Knobs 4 through 7, and Fader 1 into APVTS parameter updates.
- Use the existing parameter attachments to propagate the changes into the editor.

Validate:

- Moving each mapped hardware control changes the correct on-screen control in standalone mode.
- The corresponding sound change is audible while notes are held.
- No direct UI mutation code was added outside existing APVTS attachments.

### 3.3 Route the panic action through an explicit command path

Plan:

- Treat panic as a command, never as a parameter.
- Reuse the existing `SynthAudioProcessor::requestPanic()` endpoint so UI and hardware both converge on the same audio-thread-safe stop path.
- Only bind a hardware panic control if `Phase 6` verified one.

Act:

- Add `MappedCommand::panic` to the mapping model.
- Add `SynthAudioProcessor::applyMappedCommand(...)` that switches on command type and calls `requestPanic()`.
- If `Phase 6` verified a panic-capable control, add a binding row for it in `Minilab3Profile`.
- Gate the binding so a deferred pad or encoder does not accidentally become active.

Validate:

- The hardware panic control, if active, silences currently playing notes through the existing panic pathway.
- No panic-related parameter was added to APVTS.
- Note release or zero-value follow-up messages do not trigger duplicate panic requests.

### 3.4 Keep MiniLab-specific constants isolated from synth-engine and voice code

Plan:

- Treat `Minilab3Profile` as the only home for verified CC or note constants.
- Keep the mapping engine generic enough to consume bindings, not hard-code controller numbers.
- Audit file boundaries before implementation so the controller contract does not leak into DSP code.

Act:

- Put verified CC or note identifiers in `src/midi/Minilab3Profile.cpp` only.
- Keep `SynthAudioProcessor` working in terms of `ControllerMidiEvent`, `MappedAction`, and APVTS parameters.
- Do not include `Minilab3Profile.h` from synth-engine or synth-voice files.

Validate:

- Search the changed diff for MiniLab-specific numeric constants outside `src/midi/Minilab3Profile.cpp`.
- Confirm `src/synth/**` remains untouched.
- Confirm `SynthAudioProcessor` refers to logical mapped actions, not controller numbers.

### 3.5 Verify controller-driven parameter changes update the UI in standalone mode

Plan:

- Reuse the current editor structure instead of introducing a second UI update path.
- Wire the standalone MIDI panel to the processor event handler and let existing attachments do the rest.
- Keep plugin mode free of standalone hardware references.

Act:

- Update `StandaloneMidiInputPanel` so its controller accepts a forwarded event callback.
- Update `SynthAudioProcessorEditor.cpp` so the standalone panel is constructed with a callback to `processor.handleStandaloneControllerEvent(...)`.
- Keep the standalone-only gate exactly where it is now.

Validate:

- Waveform selector, ADSR displays, and master gain display update when the MiniLab controls move.
- VST3 editor construction still omits standalone MIDI and audio panels.
- No new polling or direct component mutation logic was introduced.

### 3.6 Verify no host-notifying parameter writes occur in the audio callback

Plan:

- Make the non-audio-thread parameter write path explicit and centralized.
- Audit the processor and controller code after the first implementation slice before widening scope.
- Use a search-based verification pass because this is a code-structure guarantee, not only a runtime behavior check.

Act:

- Keep all `beginChangeGesture()`, `setValueNotifyingHost()`, and `endChangeGesture()` calls in `SynthAudioProcessor::applyParameterChange(...)` only.
- Do not add any APVTS mutation path to `processBlock(...)`.
- Leave `processBlock(...)` limited to panic-flag consumption, buffer prep, and synth render.

Validate:

- Search the touched source for `setValueNotifyingHost`, `beginChangeGesture`, and `endChangeGesture` and confirm none appear in `processBlock(...)` or callback-thread code.
- Build succeeds with no new warnings introduced by the routing changes.
- Manual playback confirms mapped controls work without any code path that mutates parameters in the audio callback.

## 4. Edge Case & Boundary Audit

These are the specific failure modes to audit during implementation and review.

### 4.1 Threading and Real-Time Boundaries

- MIDI callback accidentally calling `setValueNotifyingHost(...)` directly.
- `processBlock(...)` growing a hidden CC-to-parameter path during later local fixes.
- Queue overflow under fast controller movement causing blocking, allocation, or undefined state.
- Device-refresh async work and controller-event async work clobbering one another in the same `AsyncUpdater` pass.

Required handling:

- Queue and return on overflow.
- Keep all host-notifying writes in the non-audio-thread forwarding path.
- Keep callback work bounded to fixed-size copies and `triggerAsyncUpdate()`.

### 4.2 Mapping Correctness

- Waveform bucket boundaries producing unstable toggling near `42/43` or `85/86`.
- Using raw controller values directly against parameter value space instead of normalized parameter space.
- Routing a verified control to the wrong stable parameter ID because the binding table and parameter cache diverged.
- Accidentally activating future-phase controls such as filter or delay because the binding table is too broad.

Required handling:

- Use exact integer bucket math for waveform choice.
- Map linear controller range into normalized parameter space, not raw milliseconds or dB values.
- Restrict `Phase 7` bindings to the current six parameter targets plus optional panic.

### 4.3 Command Path Boundaries

- Panic implemented as a fake APVTS parameter or boolean state property.
- Panic firing on both note-on and note-off for the same pad.
- Panic firing from a control that `Phase 6` explicitly marked deferred.

Required handling:

- Keep panic in `MappedCommand` only.
- Trigger on the active edge only.
- Gate all command bindings through the verified profile.

### 4.4 Standalone Versus Plugin Separation

- Plugin editor accidentally instantiating standalone hardware panels.
- Plugin path accidentally depending on a hardware device name or standalone settings object.
- Host MIDI CC remapping accidentally appearing in `processBlock(...)` while implementing the standalone path.

Required handling:

- Keep hardware ingress inside `StandaloneMidiInputController` only.
- Keep plugin mode free of standalone controller forwarding.
- Keep shared mapping logic reusable but dormant unless the standalone path calls it.

### 4.5 State and UX Consistency

- UI values moving but sound not changing because APVTS writes hit the wrong parameter or are not normalized correctly.
- Sound changing but UI not moving because the code mutates controls directly or bypasses APVTS.
- Disconnecting the selected MIDI device while pending controller events still exist.

Required handling:

- APVTS is the only parameter mutation path.
- Let existing attachments update the UI.
- On device disconnect, keep app running, allow pending events to drain or clear deterministically, and preserve the existing disconnect-triggered panic behavior.

## 5. Verification Protocol

`Phase 7` should not be marked complete until all mandatory checks below pass.

### 5.1 Mandatory Manual UX Checks

1. Launch the standalone app with the MiniLab disconnected and confirm the app still opens normally.
2. Connect and select the MiniLab as the active MIDI input and confirm the status panel shows the connected device.
3. Hold a note and move `Knob 1`; confirm the waveform selector changes between `sine`, `square`, and `saw`, and the audible waveform changes with it.
4. Hold a note and move `Knob 4`; confirm attack changes audibly and the attack value display updates.
5. Hold a note and move `Knob 5`; confirm decay changes audibly and the decay value display updates.
6. Hold a note and move `Knob 6`; confirm sustain changes audibly and the sustain value display updates.
7. Hold a note and move `Knob 7`; confirm release changes audibly and the release value display updates.
8. Hold a note and move `Fader 1`; confirm output level changes audibly and the master value display updates.
9. Trigger the verified hardware panic control, if one exists for this phase, and confirm active notes stop through the same path used by the UI panic button.
10. Disconnect the selected MIDI device during playback and confirm the app remains running and active notes are cleared through the existing disconnect panic path.
11. Open the VST3 target in a host or at minimum instantiate the plugin editor and confirm standalone-only MIDI and audio panels are absent.

### 5.2 Mandatory Automated Checks

1. `cmake --build build --config Debug`
2. `cmake --build build --config Release`
3. Search the changed source for `setValueNotifyingHost`, `beginChangeGesture`, and `endChangeGesture` and confirm all call sites are outside `processBlock(...)`.
4. Search the changed source for MiniLab-specific numeric constants and confirm they appear only in `src/midi/Minilab3Profile.cpp`.
5. Review the final diff and confirm `src/synth/**` and `src/parameters/**` were not modified unless a compile-only include adjustment was unavoidable.

### 5.3 Strongly Preferred Pure-Logic Tests If a Small Harness Is Added

This project does not require unit tests for the early milestones, but `Phase 7` is one of the first phases where pure-logic tests are cheap and high-value. If a lightweight test target is introduced, these cases should be covered:

- `translate(...)` maps the verified `Knob 1` CC number to `waveform` and quantizes `0`, `64`, and `127` to the expected three choices.
- `translate(...)` maps verified ADSR controls to the correct stable parameter IDs.
- `translate(...)` maps `Fader 1` to `masterGainDb`.
- Unmapped CCs return `Kind::none`.
- Deferred pad or encoder rows return `Kind::none`.
- Panic binding, if enabled, emits one `MappedCommand::panic` for the active event and no command for the release event.

## 6. Code Scaffolding

These scaffolds are intentionally structural. They are not drop-in final implementations.

### 6.1 `src/midi/MidiMappingEngine.h`

```cpp
#pragma once

#include <array>
#include <span>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/Minilab3Profile.h"

namespace coolsynth::midi
{
    enum class ControllerMidiEventType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        other,
    };

    struct ControllerMidiEvent
    {
        ControllerMidiEventType type = ControllerMidiEventType::other;
        uint8_t channel = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
    };

    enum class MappingCurve : uint8_t
    {
        linearNormalized,
        waveformChoice3Step,
    };

    enum class MappedCommand : uint8_t
    {
        none,
        panic,
    };

    struct ParameterTarget
    {
        juce::RangedAudioParameter* parameter = nullptr;
        MappingCurve curve = MappingCurve::linearNormalized;
        uint8_t discreteStepCount = 0;
    };

    struct MappedParameterChange
    {
        juce::RangedAudioParameter* parameter = nullptr;
        float normalizedValue = 0.0f;
    };

    struct MappedAction
    {
        enum class Kind : uint8_t
        {
            none,
            parameterChange,
            command,
        };

        Kind kind = Kind::none;
        MappedParameterChange parameterChange {};
        MappedCommand command = MappedCommand::none;
    };

    class MidiMappingEngine
    {
    public:
        explicit MidiMappingEngine(juce::AudioProcessorValueTreeState& state);

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        static float mapControllerValue(uint8_t midiValue,
                                        const ParameterTarget& target) noexcept;

        std::span<const Minilab3Binding> bindings {};
        std::array<ParameterTarget, 6> parameterTargets {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingEngine)
    };
}
```

### 6.2 `src/plugin/SynthAudioProcessor.h`

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);

    void requestPanic() noexcept;

private:
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;

    APVTS parameters;
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    coolsynth::synth::ParameterValuePointers parameterValues;
    coolsynth::synth::SynthEngine synthEngine;
    std::atomic<bool> panicRequested { false };
};
```

Constructor ordering requirement:

- `parameters` must be initialized before `midiMappingEngine` so the engine can bind parameter pointers safely.

### 6.3 `src/standalone/StandaloneMidiInput.h`

```cpp
namespace coolsynth::standalone
{
    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        using DisconnectCallback = std::function<void()>;
        using ControllerEventHandler = std::function<void(const coolsynth::midi::ControllerMidiEvent&)>;

        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      coolsynth::midi::MidiMonitorBuffer& monitorBuffer,
                                      ControllerEventHandler onControllerEvent,
                                      DisconnectCallback onSelectedDeviceDisconnected = {});

    private:
        void handleIncomingMidiMessage(juce::MidiInput* source,
                                       const juce::MidiMessage& message) override;
        void handleAsyncUpdate() override;

        void enqueueControllerEvent(const juce::MidiMessage& message) noexcept;
        int drainControllerEvents(coolsynth::midi::ControllerMidiEvent* destination,
                                  int maxEvents) noexcept;

        ControllerEventHandler onControllerEvent;
        std::array<coolsynth::midi::ControllerMidiEvent, 128> pendingControllerEvents {};
        juce::AbstractFifo pendingControllerEventQueue { 128 };
    };
}
```

### 6.4 `src/ui/StandaloneMidiInputPanel.cpp`

```cpp
StandaloneMidiInputPanel::StandaloneMidiInputPanel(ControllerEventHandler onControllerEvent,
                                                   std::function<void()> onDisconnected)
{
    if (auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager())
    {
        auto* settings = coolsynth::standalone::getStandaloneSettings();
        controller = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
            *deviceManager,
            settings,
            monitorBuffer,
            std::move(onControllerEvent),
            std::move(onDisconnected));
    }
}
```

### 6.5 `src/plugin/SynthAudioProcessorEditor.cpp`

```cpp
if (juce::JUCEApplicationBase::isStandaloneApp())
{
    auto midiInputPanel = std::make_unique<StandaloneMidiInputPanel>(
        [this](const coolsynth::midi::ControllerMidiEvent& event)
        {
            processor.handleStandaloneControllerEvent(event);
        },
        [this]
        {
            processor.requestPanic();
        });
}
```

### 6.6 `src/midi/Minilab3Profile.cpp`

```cpp
namespace coolsynth::midi
{
    namespace
    {
        constexpr Minilab3Binding phase7Bindings[] {
            { "knob1",  ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::waveform,   MappingCurve::waveformChoice3Step, true },
            { "knob4",  ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::ampAttack,  MappingCurve::linearNormalized,  true },
            { "knob5",  ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::ampDecay,   MappingCurve::linearNormalized,  true },
            { "knob6",  ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::ampSustain, MappingCurve::linearNormalized,  true },
            { "knob7",  ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::ampRelease, MappingCurve::linearNormalized,  true },
            { "fader1", ControllerMidiEventType::controlChange, /* verified CC */ 0, Minilab3LogicalTarget::masterGain, MappingCurve::linearNormalized,  true },
            // Add a panic row only if Phase 6 verified a concrete control and the plan still approves it.
        };
    }
}
```

Implementation rule for this file:

- Replace placeholder numeric comments with the exact verified values from `Phase 6`. Never infer them from the preferred mapping table in `REQUIREMENTS.md`.

## Exit Readiness Summary

`Phase 7` is ready for implementation review only when all of the following are true:

- One shared mapping engine exists and only produces parameter-change requests or explicit commands.
- The standalone MIDI callback performs no host-notifying parameter writes.
- Mapped waveform, ADSR, and master-gain hardware controls change sound through APVTS.
- The existing UI reflects those changes without a second control plane.
- MiniLab constants remain isolated to the profile module.
- Mapped panic is implemented only if `Phase 6` verified a real hardware binding for it.
