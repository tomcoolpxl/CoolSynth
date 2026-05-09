<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 13 Blueprint: MIDI Learn Workflow

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 13 - MIDI learn workflow`

Selection basis:

- `TODO.md` is currently blank, so it is not carrying an active implementation slice.
- `IMPLEMENTATION_PHASE12.md` already exists, and the codebase and `README.md` both show the standalone persistence milestone as the current completed phase.
- `IMPLEMENTATION_PLAN.md` defines `Phase 13` as the next deferred milestone after standalone persistence: a per-parameter MIDI learn workflow with clear mapping and persistence support.
- The current codebase already has the correct prerequisites in place: a fixed mapping engine, a standalone-only settings store, and a message-thread controller-event path that can host learn capture without moving host-notifying parameter writes into `processBlock()`.

## Working Hypothesis

The core architectural problem for this phase is not "how to map MIDI to parameters". That part already exists.

The real Phase 13 problem is:

- add a small user-override layer on top of the current fixed MiniLab profile,
- capture learned bindings from the existing standalone controller event stream,
- keep learned mappings out of APVTS/plugin state and future patch files,
- and expose the workflow through the existing editor without turning the UI into a controller-management screen.

That leads to one controlling design decision for this phase:

- treat MIDI learn as a standalone-persisted user override table layered over the fixed profile in `MidiMappingEngine`, not as a synth-engine feature and not as a new parameter/state model.

Cheap disconfirming checks already satisfied by the current code:

1. `src/standalone/StandaloneMidiInput.cpp` already receives MIDI events on the message thread and forwards lightweight `ControllerMidiEvent` values.
2. `src/plugin/SynthAudioProcessor.cpp` already centralizes parameter writes through `setValueNotifyingHost()` outside the audio callback.
3. `src/standalone/SettingsStore.cpp` already isolates standalone-only persistence from APVTS state.
4. `src/plugin/SynthAudioProcessorEditor.cpp` already owns the standalone-only controller callback lambda, which is the cheapest interception point for learn mode.

## Scope Guardrails

In scope for this phase:

- Learn mode for continuous parameters only.
- A per-parameter context action on eligible controls in the shared editor, enabled only in standalone mode.
- CC-only capture for learned mappings.
- Clear-mapping support from the same context-action surface.
- Persistence of learned mappings in standalone settings, separate from synth parameter state.
- Deterministic precedence between fixed profile mappings and learned mappings.
- Minimal visual/status feedback so the user can see which parameter is armed and whether a learned mapping exists.

Explicitly out of scope for this phase:

- Learning waveform or any other discrete/choice parameter.
- Learning note-on, note-off, pitch bend, aftertouch, or program change.
- Multi-controller routing or per-device learned mapping profiles.
- Relative-encoder translation work beyond the currently working absolute CC path.
- Preset integration, patch-file integration, or plugin-state integration.
- A generalized controller-management UI.
- Any redesign of synth voices, DSP, APVTS parameter IDs, or VST3 behavior.

Non-negotiable constraints:

- Learned mappings must not be serialized through `SynthAudioProcessor::getStateInformation()` or `setStateInformation()`.
- Learned mappings must not be saved inside future patch files by accident.
- `processBlock()` must remain free of XML parsing, settings access, learn-session logic, and host-notifying parameter writes.
- The existing fixed MiniLab profile remains the baseline mapping source. Learned mappings override it; they do not replace the profile or move controller constants into synth code.
- Clearing a learned mapping must restore the effective fixed-profile behavior for that parameter if a fixed binding exists.

## Current Code Anchors

These files already own the behavior that Phase 13 must extend.

- `src/midi/MidiMappingEngine.h` and `.cpp`
  - Own the fixed profile translation path.
  - Already resolve `Minilab3Binding` entries to parameter targets.
  - Already implement soft takeover state per binding.
  - Must become "fixed profile + learned overrides", not a different mapping system.

- `src/standalone/SettingsStore.h` and `.cpp`
  - Already isolate standalone persistence from APVTS/plugin state.
  - Are the correct place to add learned-mapping load/save methods.

- `src/plugin/SynthAudioProcessor.h` and `.cpp`
  - Already centralize parameter change gestures and mapped command handling.
  - Should expose only thin wrapper methods for replacing/clearing learned bindings in the mapping engine.
  - Should not start owning standalone settings or learn-session UI state.

- `src/plugin/SynthAudioProcessorEditor.h` and `.cpp`
  - Already build the standalone-only controller callback lambda.
  - Already know which control surfaces correspond to each parameter.
  - Are the cheapest place to add per-parameter context menus, armed-state feedback, and learned-binding status refresh.

- `src/ui/HardwareKnob.h` and `.cpp`
- `src/ui/HardwareFader.h` and `.cpp`
  - Are minimal wrappers today.
  - Need small visual hooks only: armed/highlighted state and learned-binding badge text.
  - They should not become persistence-aware.

- `src/standalone/StandaloneMidiInput.h` and `.cpp`
  - Already deliver message-thread `ControllerMidiEvent` values.
  - Should remain unchanged unless a concrete local defect forces a one-line integration adjustment.

- `CMakeLists.txt`
  - Uses an explicit `target_sources(...)` list, so any new source file in this phase must be added there.

## Exact `TODO.md` Entries This Blueprint Expands

These are the exact `Phase 13` checklist items from `IMPLEMENTATION_PLAN.md` that this blueprint expands into an implementation plan when `TODO.md` is refreshed:

- [ ] Add per-parameter MIDI learn mode for continuous parameters.
- [ ] Capture only CC messages for learned mappings.
- [ ] Reject note events as continuous-parameter mappings.
- [ ] Add a clear-mapping action.
- [ ] Persist learned mappings separately from synth parameter state.
- [ ] Verify learned mappings survive an app restart.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Phase 13 should implement this runtime model:

```text
Fixed MiniLab profile
  + standalone-persisted learned CC overrides
  -> effective mapping table
  -> mapped parameter changes / mapped commands
```

That matches the existing design note in `DESIGN.md`:

```text
Default profile
  + user overrides
  + persisted mapping file
```

For this repository, "persisted mapping file" should be interpreted as:

- a standalone settings payload stored through `StandaloneSettingsStore`,
- not APVTS state,
- not plugin state,
- and not patch files.

Why this is the correct minimum design:

- the fixed MiniLab mapping is already implemented and working,
- the editor already has an interception point for standalone controller events,
- the settings store already exists,
- and Phase 13 only needs user overrides plus learn UX, not a new controller architecture.

### 1.2 Learn-Eligible Parameter Set

Only continuous parameters are eligible for Phase 13 learn mode.

Required learn-eligible parameter set:

| Parameter ID | UI Surface | Eligible | Reason |
| --- | --- | --- | --- |
| `filterCutoffHz` | Cutoff knob | Yes | Continuous float |
| `filterResonance` | Resonance knob | Yes | Continuous float |
| `ampAttackMs` | Attack knob | Yes | Continuous float |
| `ampDecayMs` | Decay knob | Yes | Continuous float |
| `ampSustain` | Sustain knob | Yes | Continuous float |
| `ampReleaseMs` | Release knob | Yes | Continuous float |
| `delayTimeMs` | Time knob | Yes | Continuous float |
| `delayFeedback` | Feedback knob | Yes | Continuous float |
| `delayMix` | Mix knob | Yes | Continuous float |
| `masterGainDb` | Master fader | Yes | Continuous float |
| `waveform` | Waveform combo box | No | Discrete choice parameter |
| Panic action | `All Notes Off` button | No | Command, not a parameter |

Design rule:

- learn affordances should be attached only to the ten eligible knob/fader surfaces.
- do not add learn affordances to the waveform combo box or the panic button.

### 1.3 Required Data Structures and State Definitions

#### 1.3.1 Learned Mapping Key

Store learned mappings as explicit CC bindings, not generic MIDI blobs.

```cpp
namespace coolsynth::midi
{
    struct MidiCcKey
    {
        uint8_t channel = 1;
        uint8_t controllerNumber = 0;

        bool isValid() const noexcept;
        bool operator==(const MidiCcKey& other) const noexcept = default;
    };
}
```

Policy:

- persist the exact channel observed during learn.
- do not introduce omni-channel learned bindings in Phase 13.
- do not persist device identifiers with mappings because the standalone app already enforces one active MIDI input at a time.

#### 1.3.2 Learned Binding Record

```cpp
namespace coolsynth::midi
{
    struct LearnedCcBinding
    {
        juce::String parameterId;
        MidiCcKey cc;

        bool isValid() const noexcept;
    };
}
```

Rules:

- exactly one learned binding per parameter.
- exactly one learned binding per `channel + CC number` pair.
- on conflicts, the newly learned binding wins and the displaced binding is removed.

This keeps the user model simple and prevents silent fan-out where one hardware gesture controls multiple parameters.

#### 1.3.3 Learn Session State

```cpp
namespace coolsynth::midi
{
    enum class MidiLearnMode : uint8_t
    {
        idle,
        armed,
    };

    struct MidiLearnSession
    {
        MidiLearnMode mode = MidiLearnMode::idle;
        juce::String parameterId;
        juce::String parameterName;
        juce::String statusText;

        bool isArmed() const noexcept;
    };
}
```

Intent:

- keep armed-state UI and status messaging out of the processor.
- store only transient learn-session information here.
- keep persisted bindings separate from the transient armed-state record.

#### 1.3.4 Learn Capture Result

```cpp
namespace coolsynth::midi
{
    enum class MidiLearnCaptureResult : uint8_t
    {
        ignored,
        rejectedNonCc,
        captured,
        duplicateNoChange,
    };

    struct LearnCaptureOutcome
    {
        MidiLearnCaptureResult result = MidiLearnCaptureResult::ignored;
        bool consumeOriginalEvent = false;
        bool bindingsChanged = false;
        std::optional<LearnedCcBinding> capturedBinding;
        juce::String statusText;
    };
}
```

The `consumeOriginalEvent` field is important.

Required behavior:

- the CC event that completes a learn capture must not also trigger the old fixed mapping.
- after capture succeeds, the same event should be intentionally re-applied once through the updated effective mapping so the user gets immediate audible/UI feedback on the newly mapped parameter.

#### 1.3.5 Mapping Engine Runtime Structures

Do not replace the existing fixed-binding structure. Extend it.

```cpp
namespace coolsynth::midi
{
    struct LearnedBindingWithTarget
    {
        LearnedCcBinding binding;
        ParameterTarget target;
        mutable TakeoverState state = TakeoverState::waitingForFirstTouch;
        mutable float initialHardwareValue = 0.0f;
        mutable float initialSoftwareValue = 0.0f;
    };
}
```

Design intent:

- fixed bindings keep using the current `BindingWithTarget` array.
- learned bindings use a separate dynamic collection because the count is variable.
- learned bindings reuse the same takeover semantics so absolute controls do not jump unpredictably after app restart.

### 1.4 Precedence and Conflict Rules

These rules must be explicit in the code and in review.

1. Learned bindings override fixed bindings for the same parameter.
2. Learned bindings also shadow any fixed binding that uses the same `channel + CC number` signature.
3. Clearing a learned binding restores the fixed binding automatically if one exists.
4. The first valid CC captured during learn mode ends the learn session.
5. Note-on and note-off messages never create a learned binding.
6. Rejected note events must not delete or mutate an existing learned binding.
7. Re-learning the same parameter replaces that parameter's previous learned binding.
8. Learning a CC that is already learned by another parameter transfers ownership to the newly armed parameter.
9. If a captured binding is byte-for-byte identical to the current learned binding for that parameter, treat it as `duplicateNoChange` and do not rewrite persistence unnecessarily.

### 1.5 Threading and Ownership Model

| Concern | Owner | Thread | Phase 13 Rule |
| --- | --- | --- | --- |
| Fixed profile definitions | `Minilab3Profile` | N/A | Unchanged in this phase |
| Learned-binding registry | `MidiLearnManager` | Message thread only | No audio-thread access |
| Effective event translation | `MidiMappingEngine` | Message thread in standalone path | No settings I/O inside translation |
| Parameter writes | `SynthAudioProcessor` | Message thread only for learn/fixed CC path | Continue using gesture + `setValueNotifyingHost()` |
| Learned-binding persistence | `StandaloneSettingsStore` | Message thread only | XML/string I/O only here |
| Armed-state UI | `SynthAudioProcessorEditor` | Message thread only | No processor ownership |

Critical boundary:

- `MidiLearnManager` and `StandaloneSettingsStore` are standalone concerns.
- `MidiMappingEngine` remains shared logic, but it should only know about learned bindings as plain data injected into it.
- `SynthAudioProcessor` should not start reading or writing `juce::PropertySet` directly.

### 1.6 Runtime Flow

#### 1.6.1 Standalone Startup

Required startup order:

1. `SynthAudioProcessorEditor` detects standalone mode.
2. The editor obtains `StandaloneSettingsStore*` through the existing standalone accessor.
3. The editor constructs `MidiLearnManager`.
4. The editor loads persisted learned bindings from `StandaloneSettingsStore`.
5. The editor normalizes the loaded set through `MidiLearnManager::replaceBindings(...)`.
6. The editor pushes the normalized learned bindings into `SynthAudioProcessor`, which forwards them into `MidiMappingEngine`.
7. The editor registers learnable controls and paints their learned/idle state.

If the settings store is unavailable:

- do not enable learn mode in standalone.
- continue to allow fixed mappings.
- surface a short standalone-only status message that learn persistence is unavailable.

#### 1.6.2 User Arms Learn Mode

Required flow:

1. User opens the per-parameter context menu on a learnable control.
2. Editor offers `Learn MIDI CC` and, when applicable, `Clear MIDI CC Mapping`.
3. Selecting `Learn MIDI CC` arms that parameter in `MidiLearnManager`.
4. The editor updates the control's visual state to `armed` and shows a short status message such as `Learning Cutoff - move a MIDI CC control`.

Re-arm policy:

- if another parameter is already armed, arming a new parameter simply switches the target.
- do not require a separate cancel step first.

#### 1.6.3 Incoming Controller Event While Armed

Required event flow in the standalone controller callback lambda:

1. Receive `ControllerMidiEvent` from `StandaloneMidiInputController`.
2. Ask `MidiLearnManager` to inspect the event first.
3. If the result is `captured`:
   - update the learned-binding registry,
   - save the new registry through `StandaloneSettingsStore`,
   - push the new registry into `SynthAudioProcessor`/`MidiMappingEngine`,
   - refresh control visuals,
   - then forward the same event once through `processor.handleStandaloneControllerEvent(event)` so the newly learned parameter updates immediately.
4. If the result is `rejectedNonCc`:
   - keep learn mode armed,
   - show a short rejection status,
   - and still forward the event normally so note playback and existing note-driven behavior continue.
5. If the result is `ignored`:
   - forward the event normally.

#### 1.6.4 Clear Mapping

Required clear flow:

1. User opens the context menu on a control with a learned binding.
2. User selects `Clear MIDI CC Mapping`.
3. `MidiLearnManager` removes that parameter's learned binding.
4. The editor persists the new learned-binding set.
5. The editor pushes the updated set into the processor/mapping engine.
6. Control visuals refresh.
7. The effective fixed-profile binding becomes active again if one exists.

### 1.7 Required Function Signatures

The following signatures give the phase a concrete implementation surface.

#### 1.7.1 New Learn Manager Module

Add `src/midi/MidiLearn.h` and `src/midi/MidiLearn.cpp`.

```cpp
namespace coolsynth::midi
{
    class MidiLearnManager final
    {
    public:
        MidiLearnManager();

        bool isLearnableParameter(juce::StringRef parameterId) const noexcept;

        bool beginLearning(juce::String parameterId, juce::String parameterName);
        void cancelLearning() noexcept;

        LearnCaptureOutcome handleIncomingEvent(const ControllerMidiEvent& event);

        void replaceBindings(std::vector<LearnedCcBinding> bindings);
        bool clearBinding(juce::StringRef parameterId);

        const LearnedCcBinding* findBindingForParameter(juce::StringRef parameterId) const noexcept;
        std::span<const LearnedCcBinding> getBindings() const noexcept;
        MidiLearnSession getSession() const;

    private:
        static bool isContinuousLearnEligible(juce::StringRef parameterId) noexcept;
        void normalizeBindings();

        MidiLearnSession session;
        std::vector<LearnedCcBinding> bindings;
    };
}
```

#### 1.7.2 Mapping Engine Extensions

Extend `src/midi/MidiMappingEngine.h` and `.cpp`.

```cpp
namespace coolsynth::midi
{
    class MidiMappingEngine
    {
    public:
        explicit MidiMappingEngine(juce::AudioProcessorValueTreeState& state);

        void setLearnedBindings(std::span<const LearnedCcBinding> bindings);
        bool clearLearnedBinding(juce::StringRef parameterId);
        const LearnedCcBinding* findLearnedBinding(juce::StringRef parameterId) const noexcept;

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        ParameterTarget resolveParameterTarget(juce::StringRef parameterId) const noexcept;
        bool isFixedBindingShadowedByLearnedTarget(const Minilab3Binding& binding) const noexcept;
        bool isFixedBindingShadowedByLearnedSignature(uint8_t expectedMidiType,
                                                      uint8_t channel,
                                                      uint8_t primaryData) const noexcept;

        std::span<const Minilab3Binding> bindings;
        std::array<BindingWithTarget, 13> activeBindings {};
        std::vector<LearnedBindingWithTarget> learnedBindings;
    };
}
```

#### 1.7.3 Settings Store Extensions

Extend `src/standalone/SettingsStore.h` and `.cpp`.

```cpp
namespace coolsynth::standalone
{
    class StandaloneSettingsStore final
    {
    public:
        std::vector<coolsynth::midi::LearnedCcBinding> loadLearnedMidiMappings() const;
        void saveLearnedMidiMappings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
        void clearLearnedMidiMappings();
    };
}
```

Storage format requirement:

```xml
<MIDI_LEARN_MAPPINGS version="1">
  <MAPPING parameterId="filterCutoffHz" channel="1" controller="74" />
  <MAPPING parameterId="delayMix" channel="1" controller="83" />
</MIDI_LEARN_MAPPINGS>
```

Store that XML under a new standalone-only key:

```text
midiLearnMappings
```

#### 1.7.4 Processor Wrapper Methods

Extend `src/plugin/SynthAudioProcessor.h` and `.cpp` with thin wrappers only.

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    void setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
    void clearLearnedMidiBinding(juce::StringRef parameterId);
};
```

Rule:

- the processor wrapper updates the mapping engine only.
- it must not own learn-session state or persistence logic.

#### 1.7.5 Editor Registration Surface

Extend `src/plugin/SynthAudioProcessorEditor.h` and `.cpp`.

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        private juce::MouseListener
{
private:
    struct LearnableControlRegistration
    {
        juce::String parameterId;
        juce::String displayName;
        juce::Component* surface = nullptr;
        std::function<void(bool isArmed, juce::String badgeText)> applyVisualState;
    };

    void registerLearnableControl(juce::Component& surface,
                                  juce::String parameterId,
                                  juce::String displayName,
                                  std::function<void(bool, juce::String)> applyVisualState);
    void mouseUp(const juce::MouseEvent& event) override;
    void showMidiLearnMenu(const LearnableControlRegistration& registration,
                           juce::Point<int> screenPosition);
    void refreshMidiLearnVisuals();
    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);

    std::unique_ptr<coolsynth::midi::MidiLearnManager> midiLearnManager;
    std::vector<LearnableControlRegistration> learnableControls;
    juce::Label midiLearnStatusLabel;
};
```

## 2. File-Level Strategy

### 2.1 Exact Files to Touch

This is the exact file list for a surgical Phase 13 implementation.

| File | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Add `src/midi/MidiLearn.cpp` and the focused test target for this phase. |
| `src/midi/MidiLearn.h` | Define learned-binding types, learn-session state, and the `MidiLearnManager` API. |
| `src/midi/MidiLearn.cpp` | Implement eligibility checks, capture logic, conflict replacement, duplicate normalization, and transient status text. |
| `src/midi/MidiMappingEngine.h` | Add learned-binding API and runtime learned-binding storage. |
| `src/midi/MidiMappingEngine.cpp` | Implement learned-binding translation, fixed-binding shadow rules, and takeover initialization for newly learned bindings. |
| `src/standalone/SettingsStore.h` | Add standalone-only load/save/clear methods for learned mappings. |
| `src/standalone/SettingsStore.cpp` | Implement XML parse/write logic for `midiLearnMappings`, including invalid-entry dropping. |
| `src/plugin/SynthAudioProcessor.h` | Expose thin learned-binding wrapper methods. |
| `src/plugin/SynthAudioProcessor.cpp` | Forward learned-binding updates into `MidiMappingEngine` without touching APVTS state serialization. |
| `src/plugin/SynthAudioProcessorEditor.h` | Add learn manager pointer, control registration metadata, mouse listener hooks, and learn status label members. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Register learnable controls, build context menus, load persisted mappings on startup, intercept controller events before forwarding, and refresh learn visuals. |
| `src/ui/HardwareKnob.h` | Add small visual-state API for armed/learned status. |
| `src/ui/HardwareKnob.cpp` | Paint armed/highlight state and show a compact learned badge string such as `CC74`. |
| `src/ui/HardwareFader.h` | Add the same visual-state API used by knobs. |
| `src/ui/HardwareFader.cpp` | Paint armed/highlight state and learned badge text for the master fader. |
| `tests/MidiLearnTests.cpp` | Add focused automated tests for learn eligibility, capture/rejection behavior, persistence round-trip, and fixed-vs-learned precedence. |
| `README.md` | Document how to enter learn mode, clear a mapping, what persists, and that learn is standalone-only for this phase. |
| `TODO.md` | Refresh with the six Phase 13 checklist items before implementation starts. |
| `DONE.md` | Record the milestone only after all manual and automated checks pass. |

### 2.2 Files to Leave Alone Unless a Local Defect Forces a Change

These files should stay untouched in the normal implementation path.

- `src/parameters/ParameterIDs.h`
  - No parameter-ID changes are justified in this phase.

- `src/midi/Minilab3Profile.h` and `.cpp`
  - The fixed profile remains the baseline source of truth.
  - MIDI learn is an override layer, not a profile rewrite.

- `src/standalone/StandaloneMidiInput.h` and `.cpp`
  - The existing controller-event callback path is already the correct interception point.
  - Do not widen this phase by moving learn logic into the device manager layer.

- `src/synth/**`
  - The synth engine should be unaffected by learn-mode work.

- `src/ui/StandaloneStatusBar.*`
  - The editor-level learn label and control indicators are sufficient for this phase.
  - Only touch the status bar if a local UX gap proves impossible to solve inside the editor.

## 3. Atomic Execution Steps

Each item below expands one high-level Phase 13 checkbox into a concrete `Plan -> Act -> Validate` cycle.

### 3.1 TODO Item 1: Add Per-Parameter MIDI Learn Mode for Continuous Parameters

Plan:

- Introduce one standalone-only learn manager that knows which parameter IDs are eligible.
- Attach one context-menu surface to each eligible knob/fader and nowhere else.
- Keep armed-state ownership in the editor, not the processor.

Act:

1. Add `MidiLearnManager` in `src/midi/MidiLearn.*` with an explicit allowlist for the ten learnable parameter IDs.
2. Extend `SynthAudioProcessorEditor` with a `LearnableControlRegistration` table for:
   - cutoff
   - resonance
   - attack
   - decay
   - sustain
   - release
   - delay time
   - delay feedback
   - delay mix
   - master gain
3. Register a mouse listener recursively on each learnable control so right-clicks on the slider/label children still open the menu.
4. Add a standalone-only popup menu with these entries:
   - `Learn MIDI CC`
   - `Cancel MIDI Learn` when that parameter is already armed
   - `Clear MIDI CC Mapping` when a learned binding exists
5. Add a small editor status label and a per-control visual API so the armed parameter is obvious.

Validate:

- In standalone mode, right-clicking each learnable control opens the MIDI learn menu.
- The waveform selector and the panic button expose no learn action.
- Arming one control then arming another moves the armed state cleanly with no stale highlight.
- The plugin editor does not expose learn actions because no standalone settings/device path exists there.

### 3.2 TODO Item 2: Capture Only CC Messages for Learned Mappings

Plan:

- Reuse the existing standalone controller-event callback path.
- Inspect learn mode before normal fixed mapping so the capture event cannot move the wrong parameter first.
- Store learned bindings as `parameterId + channel + controllerNumber` only.

Act:

1. In the editor's standalone controller callback, route each `ControllerMidiEvent` through `MidiLearnManager` before calling `processor.handleStandaloneControllerEvent(event)`.
2. In `MidiLearnManager::handleIncomingEvent(...)`, accept only `ControllerMidiEventType::controlChange` when learn mode is armed.
3. Build a `LearnedCcBinding` from the captured event.
4. Replace any existing learned binding for the armed parameter and any existing learned binding using the same CC signature.
5. Push the updated learned-binding table into `SynthAudioProcessor`, which forwards it into `MidiMappingEngine`.
6. After capture succeeds, forward the same event once through the processor so the newly learned parameter updates immediately.

Validate:

- When a control is armed and the user moves a CC source, the target parameter changes immediately.
- The CC that completed learning does not also trigger the old fixed mapping.
- If the same CC is learned onto a second parameter, the original learned owner stops responding and the new one takes over.

### 3.3 TODO Item 3: Reject Note Events as Continuous-Parameter Mappings

Plan:

- Treat note events as invalid learn candidates, not as errors that break normal note playback.
- Keep learn mode armed after rejection so the user can immediately move a CC without reopening the menu.

Act:

1. In `MidiLearnManager::handleIncomingEvent(...)`, explicitly reject:
   - `noteOn`
   - `noteOff`
   - `other`
2. Return `MidiLearnCaptureResult::rejectedNonCc` with a status text such as `Move a MIDI CC control - note events cannot be learned here`.
3. Do not mutate the learned-binding registry.
4. Do not persist anything.
5. Still forward the rejected event through the normal processor path so note playback and existing note-based behaviors remain intact.

Validate:

- While a control is armed, pressing a keyboard key or pad does not create a learned binding.
- The armed state remains active after the rejection.
- Existing sound playback still works while the learn session is armed.
- No persisted mapping XML changes after a rejected note event.

### 3.4 TODO Item 4: Add a Clear-Mapping Action

Plan:

- The clear action must live in the same per-parameter context menu so the workflow is symmetrical.
- Clearing a learned binding should remove only the user override and restore the default profile behavior automatically.

Act:

1. Add `Clear MIDI CC Mapping` to the context menu when `MidiLearnManager` reports a learned binding for that parameter.
2. Implement `MidiLearnManager::clearBinding(parameterId)`.
3. Persist the updated learned-binding set immediately after a clear.
4. Push the updated set into the processor/mapping engine.
5. Refresh visuals so the control loses its learned badge and armed state if it was the active learn target.

Validate:

- Clearing a learned binding removes the learned badge from the control.
- If a fixed binding exists for that parameter, the original fixed hardware control works again after clear.
- Clearing an unmapped parameter is a no-op and does not corrupt persistence.

### 3.5 TODO Item 5: Persist Learned Mappings Separately From Synth Parameter State

Plan:

- Extend the existing standalone settings store.
- Keep learned mappings in one dedicated XML payload under one standalone-only key.
- Do not touch APVTS serialization.

Act:

1. Add `loadLearnedMidiMappings()`, `saveLearnedMidiMappings(...)`, and `clearLearnedMidiMappings()` to `StandaloneSettingsStore`.
2. Use a dedicated XML payload under `midiLearnMappings` with `version="1"`.
3. On load, drop any entry that is invalid because:
   - `parameterId` is empty
   - the parameter ID is not in the learn-eligible allowlist
   - `channel` is outside `1..16`
   - `controller` is outside `0..127`
4. Normalize duplicates deterministically so the persisted set cannot produce ambiguous runtime ownership.
5. Keep `SynthAudioProcessor::getStateInformation()` and `setStateInformation()` unchanged.

Validate:

- Learned mappings are written only through `StandaloneSettingsStore`.
- No learn XML or key appears inside processor state serialization.
- Loading malformed or stale learned-binding XML drops bad entries without crashing.
- The settings file may contain audio settings, MIDI device settings, and learned mappings, but the code paths remain separate.

### 3.6 TODO Item 6: Verify Learned Mappings Survive an App Restart

Plan:

- Treat the settings store as the source of truth for learned mappings at startup.
- Hydrate the editor-side learn manager first, then inject the normalized binding set into the processor.

Act:

1. During standalone editor construction, load learned mappings from `StandaloneSettingsStore` before registering visual status.
2. Pass the normalized set to `MidiLearnManager`.
3. Push the current learned-binding set into the processor/mapping engine.
4. Refresh control badges so the UI reflects learned mappings immediately after the editor appears.
5. Ensure the same code path runs on every startup, not only after a successful previous session shutdown.

Validate:

- Learn a mapping, close the app, relaunch the standalone app, and confirm the same hardware CC still drives the same parameter.
- The learned badge is visible on relaunch without re-entering learn mode.
- If the persistence payload is absent, the app falls back to fixed mappings cleanly.

## 4. Edge Case and Boundary Audit

Every item below is a concrete failure mode or boundary condition that Phase 13 must either handle or explicitly reject.

- Right-click on a non-learnable control must not offer learn mode by mistake.
- Plugin mode must not expose standalone-only learn actions or try to touch `StandaloneSettingsStore`.
- A learn session armed on one parameter must be cancelled or replaced cleanly when another parameter is armed.
- Pressing notes while learn mode is armed must not create a binding.
- Rejected note events must not delete a valid existing learned binding.
- The CC that completes learning must not first change the old fixed-mapped parameter.
- The first captured CC should still update the newly learned parameter once so the user gets immediate feedback.
- Re-learning the same parameter must replace only that parameter's old learned binding.
- Learning a CC already owned by another parameter must transfer ownership deterministically.
- Clearing a learned binding must reactivate the fixed binding for that parameter if one exists.
- Parameters without a fixed default mapping after clear must simply become unmapped rather than borrowing another control.
- Persisted mappings with unknown parameter IDs after future refactors must be dropped on load.
- Persisted duplicate mappings must be normalized into one deterministic winner before runtime use.
- Missing settings store access in standalone must disable learn-mode persistence rather than crash the editor.
- Learned mappings must not leak into APVTS state, VST3 recall, or future patch save/load behavior.
- The mapping engine must not allocate, parse XML, or acquire locks inside `processBlock()`.
- The learn implementation must not move controller-specific constants into synth voices or DSP code.
- Absolute takeover state must be initialized sensibly for newly learned bindings so the first control motion after restart does not jump unpredictably.
- If a controller emits relative CC data rather than simple `0..127` absolute values, the phase should reject or document the limitation rather than silently mis-map it.
- Editor destruction while a learn session is armed must not leave dangling callbacks or stale armed state.

## 5. Verification Protocol

This phase should not move to `DONE.md` until every required manual and automated check below is passing.

### 5.1 Automated Checks

Because the repository currently has no test target, this phase should add one small focused test executable for the pure logic introduced here. Do not treat build-only validation as sufficient for learned-binding precedence and persistence behavior.

Required commands:

1. Configure with testing enabled.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
```

1. Build the app and the Phase 13 test target.

```powershell
cmake --build build --config Debug --target CoolSynth CoolSynthMidiLearnTests
```

1. Run the focused test target.

```powershell
ctest --test-dir build -C Debug --output-on-failure -R CoolSynthMidiLearnTests
```

Required automated test cases inside `tests/MidiLearnTests.cpp`:

- `learn_manager_accepts_only_the_ten_continuous_parameter_ids`
- `learn_manager_rejects_note_events_without_mutating_bindings`
- `learn_manager_replaces_existing_binding_by_parameter`
- `learn_manager_replaces_existing_binding_by_cc_signature`
- `settings_store_round_trips_learned_mapping_xml`
- `settings_store_drops_invalid_or_unknown_mapping_entries`
- `mapping_engine_prefers_learned_binding_over_fixed_target_binding`
- `mapping_engine_shadows_fixed_binding_when_cc_signature_is_reused`
- `mapping_engine_restores_fixed_binding_after_clear`

### 5.2 Manual UX Checklist

Run these manual checks in the standalone app after the automated checks pass.

1. Launch the standalone app and confirm learn actions appear only on the ten learnable knob/fader controls.
2. Right-click `Cutoff`, choose `Learn MIDI CC`, and confirm the control becomes visually armed and the editor shows a learn status message.
3. Move a hardware CC control and confirm:
   - learn mode ends,
   - the cutoff parameter changes immediately,
   - the control now shows a learned badge such as `CC74`.
4. Re-arm `Cutoff`, press a keyboard note instead of moving a CC, and confirm:
   - no binding changes,
   - learn mode stays armed,
   - note playback still works,
   - the status message explains that note events are not learnable here.
5. Learn the same CC onto a different parameter and confirm the old learned owner stops responding.
6. Right-click a learned control and clear the mapping. Confirm the learned badge disappears and the original fixed mapping becomes effective again if one exists.
7. Close and relaunch the standalone app. Confirm learned mappings survive restart and appear in the UI immediately.
8. Confirm audio-device settings and MIDI-device selection still persist independently and are unaffected by the learned-binding work.
9. Open the VST3 plugin in the smoke-test host and confirm no standalone learn UI leaks into host-only operation.

### 5.3 Exit Review Questions

- Are learned mappings stored only in standalone settings and nowhere in processor state?
- Can a reviewer identify the exact precedence rule between fixed mappings and learned overrides from code alone?
- Are learn actions limited to continuous parameters only?
- Does clearing a mapping restore default behavior rather than leaving a partial dead state?
- Do note events fail safely during learn mode without breaking normal playback?

## 6. Code Scaffolding

These templates are not final implementations. They are structural guides to keep the phase idiomatic and small.

### 6.1 `src/midi/MidiLearn.h`

```cpp
#pragma once

#include <optional>
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

#include "midi/MidiMappingEngine.h"

namespace coolsynth::midi
{
    struct MidiCcKey
    {
        uint8_t channel = 1;
        uint8_t controllerNumber = 0;

        bool isValid() const noexcept
        {
            return channel >= 1 && channel <= 16;
        }
    };

    struct LearnedCcBinding
    {
        juce::String parameterId;
        MidiCcKey cc;

        bool isValid() const noexcept
        {
            return parameterId.isNotEmpty() && cc.isValid();
        }
    };

    enum class MidiLearnCaptureResult : uint8_t
    {
        ignored,
        rejectedNonCc,
        captured,
        duplicateNoChange,
    };

    struct LearnCaptureOutcome
    {
        MidiLearnCaptureResult result = MidiLearnCaptureResult::ignored;
        bool consumeOriginalEvent = false;
        bool bindingsChanged = false;
        std::optional<LearnedCcBinding> capturedBinding;
        juce::String statusText;
    };

    struct MidiLearnSession
    {
        bool armed = false;
        juce::String parameterId;
        juce::String parameterName;
        juce::String statusText;
    };

    class MidiLearnManager final
    {
    public:
        MidiLearnManager();

        bool isLearnableParameter(juce::StringRef parameterId) const noexcept;
        bool beginLearning(juce::String parameterId, juce::String parameterName);
        void cancelLearning() noexcept;

        LearnCaptureOutcome handleIncomingEvent(const ControllerMidiEvent& event);

        void replaceBindings(std::vector<LearnedCcBinding> bindings);
        bool clearBinding(juce::StringRef parameterId);

        const LearnedCcBinding* findBindingForParameter(juce::StringRef parameterId) const noexcept;
        std::span<const LearnedCcBinding> getBindings() const noexcept;
        MidiLearnSession getSession() const;

    private:
        static bool isContinuousLearnEligible(juce::StringRef parameterId) noexcept;
        void normalizeBindings();

        MidiLearnSession session;
        std::vector<LearnedCcBinding> bindings;
    };
}
```

### 6.2 `src/standalone/SettingsStore.*` Additions

```cpp
std::vector<coolsynth::midi::LearnedCcBinding>
StandaloneSettingsStore::loadLearnedMidiMappings() const;

void StandaloneSettingsStore::saveLearnedMidiMappings(
    std::span<const coolsynth::midi::LearnedCcBinding> bindings);

void StandaloneSettingsStore::clearLearnedMidiMappings();
```

Suggested XML write pattern:

```cpp
auto xml = std::make_unique<juce::XmlElement>("MIDI_LEARN_MAPPINGS");
xml->setAttribute("version", 1);

for (const auto& binding : bindings)
{
    auto* child = xml->createNewChildElement("MAPPING");
    child->setAttribute("parameterId", binding.parameterId);
    child->setAttribute("channel", static_cast<int>(binding.cc.channel));
    child->setAttribute("controller", static_cast<int>(binding.cc.controllerNumber));
}

propertySet.setValue("midiLearnMappings", xml.get());
```

### 6.3 `src/midi/MidiMappingEngine.*` Additions

```cpp
struct LearnedBindingWithTarget
{
    LearnedCcBinding binding;
    ParameterTarget target;
    mutable TakeoverState state = TakeoverState::waitingForFirstTouch;
    mutable float initialHardwareValue = 0.0f;
    mutable float initialSoftwareValue = 0.0f;
};

void MidiMappingEngine::setLearnedBindings(std::span<const LearnedCcBinding> bindings)
{
    learnedBindings.clear();
    learnedBindings.reserve(bindings.size());

    for (const auto& binding : bindings)
    {
        if (!binding.isValid())
            continue;

        auto target = resolveParameterTarget(binding.parameterId);
        if (target.parameter == nullptr)
            continue;

        learnedBindings.push_back({ binding, target });
    }
}
```

Required translation shape:

```cpp
MappedAction MidiMappingEngine::translate(const ControllerMidiEvent& event) const noexcept
{
    if (auto action = translateLearnedBinding(event); action.kind != MappedAction::Kind::none)
        return action;

    return translateFixedBinding(event);
}
```

### 6.4 `src/plugin/SynthAudioProcessorEditor.*` Learn Registration

```cpp
void SynthAudioProcessorEditor::registerLearnableControl(
    juce::Component& surface,
    juce::String parameterId,
    juce::String displayName,
    std::function<void(bool, juce::String)> applyVisualState)
{
    learnableControls.push_back({ parameterId, displayName, &surface, std::move(applyVisualState) });
    surface.addMouseListener(this, true);
}

void SynthAudioProcessorEditor::handleStandaloneControllerEvent(
    const coolsynth::midi::ControllerMidiEvent& event)
{
    if (midiLearnManager != nullptr)
    {
        const auto outcome = midiLearnManager->handleIncomingEvent(event);

        if (outcome.bindingsChanged && standaloneSettingsStore != nullptr)
        {
            standaloneSettingsStore->saveLearnedMidiMappings(midiLearnManager->getBindings());
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
            refreshMidiLearnVisuals();
        }

        if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::captured)
        {
            processor.handleStandaloneControllerEvent(event);
            return;
        }
    }

    processor.handleStandaloneControllerEvent(event);
}
```

### 6.5 `tests/MidiLearnTests.cpp`

```cpp
#include <juce_core/juce_core.h>

class MidiLearnTests final : public juce::UnitTest
{
public:
    MidiLearnTests() : juce::UnitTest("MidiLearn", "CoolSynth") {}

    void runTest() override
    {
        beginTest("learn_manager_rejects_note_events_without_mutating_bindings");
        // Arrange / Act / Expect

        beginTest("settings_store_round_trips_learned_mapping_xml");
        // Arrange / Act / Expect

        beginTest("mapping_engine_prefers_learned_binding_over_fixed_binding");
        // Arrange / Act / Expect
    }
};

static MidiLearnTests midiLearnTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();
    return runner.getNumResults().failures == 0 ? 0 : 1;
}
```

## Phase 13 Done Condition

This phase is ready to move from `TODO.md` to `DONE.md` only when all of the following are true:

- the standalone editor exposes per-parameter learn and clear actions for the ten continuous controls,
- note events cannot be learned as continuous mappings,
- learned bindings override the fixed profile deterministically and clear cleanly,
- learned mappings persist across standalone restart,
- learned mappings remain separate from synth parameter state,
- focused automated tests pass,
- manual standalone UX checks pass,
- and no new audio-thread or plugin-state regressions were introduced.
