<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 6 Blueprint: Verified MiniLab 3 Message Table

## Phase Selection

Selected phase: `Phase 6 - Verified MiniLab 3 message table`

Selection basis:

- `TODO.md` is still pinned to `Phase 5`, and every Phase 5 checkbox is already checked.
- `DONE.md` records `Phase 5` as complete.
- `IMPLEMENTATION_PLAN.md` makes `Phase 6` the next isolated milestone and explicitly separates it from `Phase 7` so message uncertainty does not contaminate mapping behavior.
- The current codebase already has the required capture surface: standalone MIDI device selection, a bounded MIDI monitor, and a shared synth path that must remain free of MiniLab-specific behavior until the message contract is verified.

This blueprint assumes the next review cycle begins by refreshing `TODO.md` to the Phase 6 checklist from `IMPLEMENTATION_PLAN.md`, but this document itself is planning-only and does not perform that refresh.

## Scope Guardrails

In scope for this phase:

- Capture the actual default messages emitted by the developer's MiniLab 3 for keyboard, knobs, faders, pads, and main encoder.
- Preserve those verified observations in one isolated code module and one developer-facing document.
- Decide, from evidence, whether pads and the main encoder remain deferred for the first functional release.
- Document any divergence between the preferred mapping assumptions in `REQUIREMENTS.md` and the actual observed hardware behavior.

Out of scope for this phase:

- Any controller-to-parameter routing.
- Any APVTS writes from MiniLab controls other than the existing note input path.
- Any new synth DSP, UI section, or standalone persistence feature.
- Any changes to waveform, ADSR, filter, delay, or master-gain behavior.
- Any host-side controller support.

Hard boundary for review:

- `SynthAudioProcessor`, `SynthEngine`, and `SynthVoice` must remain behaviorally unchanged in this phase except for compile-only include wiring if a future profile header is referenced. No controller mapping logic may be introduced here.

## Current Code Anchors

The implementation should stay anchored to these existing seams:

- `src/standalone/StandaloneMidiInput.cpp` owns hardware MIDI input callbacks in standalone mode.
- `src/midi/MidiMonitor.cpp` is the only current callback-to-UI capture bridge.
- `src/ui/MidiMonitorPanel.cpp` is the current evidence display surface.
- `src/plugin/SynthAudioProcessorEditor.cpp` already hosts the standalone MIDI panel and monitor only in standalone mode.
- `CMakeLists.txt` uses an explicit `target_sources(...)` list, so any new `.cpp` file must be added intentionally.

Important constraint exposed by the current code:

- `MidiMonitorBuffer::pushMessage(...)` currently records only `noteOn`, `noteOff`, and `controlChange` messages, and silently drops all other message kinds.

That means the phase cannot assume the current monitor is automatically sufficient for pad and encoder verification. If the MiniLab emits anything outside note or CC traffic for those controls, the capture surface must be extended to preserve evidence without routing that traffic into the synth.

## 1. Architectural Design

### 1.1 Controlling Design Decision

Treat Phase 6 as an evidence-capture and contract-definition phase.

The architectural outcome is not a mapping engine. The outcome is a verified, isolated message table that later phases can trust.

Required consequences:

- The standalone MIDI callback remains the single capture ingress.
- The MIDI monitor becomes an evidence tool, not just a note/CC viewer.
- The verified MiniLab table lives outside the synth engine and outside the processor.
- Any future mapping code must depend on the verified profile module produced here rather than hard-coded controller numbers copied into unrelated files.

### 1.2 Runtime Boundaries After Phase 6

```text
Standalone wrapper
  -> SynthAudioProcessorEditor
     -> StandaloneMidiInputPanel
        -> StandaloneMidiInputController
           -> handleIncomingMidiMessage(...)
              -> MidiMonitorBuffer::pushMessage(...)
                 -> bounded event queue
     -> MidiMonitorPanel
        -> drains and displays captured evidence

Shared core
  -> SynthAudioProcessor
  -> SynthEngine
  -> SynthVoice

Documentation + isolated profile module
  -> Minilab3Profile.{h,cpp}
  -> docs/minilab3-default-messages.md
```

Ownership rules:

- `StandaloneMidiInputController` continues to own device-selection behavior and the callback entry point.
- `MidiMonitorBuffer` continues to own the bounded lock-free queue.
- `MidiMonitorPanel` continues to own only UI presentation of drained events.
- `Minilab3Profile` owns only read-only verified control metadata.
- No profile code is allowed to mutate parameters, call `requestPanic()`, or depend on the audio thread.

### 1.3 Required Data Structures

The smallest coherent data model for this phase has two layers:

- A generic captured-event layer for raw evidence.
- A MiniLab-specific verified-profile layer for curated conclusions.

#### 1.3.1 Extend the Generic Monitor Event Model

Recommended additions in `src/midi/MidiMonitor.h`:

```cpp
namespace coolsynth::midi
{
    enum class MidiMonitorMessageType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        other,
    };

    struct MidiMonitorRawBytes
    {
        static constexpr int maxBytes = 8;

        std::array<uint8_t, maxBytes> bytes {};
        uint8_t size = 0;
        bool truncated = false;
    };

    struct MidiMonitorEvent
    {
        uint64_t eventOrder = 0;
        double timestampSeconds = 0.0;
        MidiMonitorMessageType type = MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
        MidiMonitorRawBytes rawBytes {};
    };
}
```

Design intent:

- `other` lets the monitor keep unsupported-but-observed hardware evidence without pretending the synth supports those messages.
- `rawBytes` preserves the minimum useful forensic payload for relative encoders, unusual pad traffic, or unexpected device defaults.
- The event remains trivially copyable and fixed-size so the current queue model still fits the real-time constraints.

Semantics for `pushMessage(...)`:

- Always copy the first up to `maxBytes` raw MIDI bytes.
- Classify note-on, note-off, and CC exactly as today.
- For anything else, set `type = other`, preserve channel if available, and keep raw bytes for UI inspection.
- Never allocate, format strings, or lock in the callback path.

#### 1.3.2 Define a Read-Only Verified MiniLab Profile Model

Recommended new types in `src/midi/Minilab3Profile.h`:

```cpp
namespace coolsynth::midi
{
    enum class Minilab3ControlCategory : uint8_t
    {
        keyboard,
        knob,
        fader,
        pad,
        encoder,
    };

    enum class Minilab3Disposition : uint8_t
    {
        requiredForPhase7,
        deferred,
        unsupported,
    };

    enum class Minilab3ValueMode : uint8_t
    {
        none,
        absolute,
        relative,
        velocity,
    };

    enum class VerifiedMidiMessageKind : uint8_t
    {
        note,
        controlChange,
        other,
    };

    struct MidiValueRange
    {
        int min = -1;
        int max = -1;
    };

    struct VerifiedMidiSignature
    {
        VerifiedMidiMessageKind kind = VerifiedMidiMessageKind::controlChange;
        uint8_t channel = 1;
        MidiValueRange primaryData {};
        MidiValueRange secondaryData {};
        Minilab3ValueMode valueMode = Minilab3ValueMode::none;
        bool emitsReleaseEvent = false;
        bool requiresRawByteInspection = false;
    };

    struct Minilab3ControlDefinition
    {
        std::string_view controlId;
        std::string_view displayName;
        Minilab3ControlCategory category = Minilab3ControlCategory::knob;
        std::string_view preferredTarget;
        Minilab3Disposition disposition = Minilab3Disposition::deferred;
        VerifiedMidiSignature signature {};
        std::string_view notes;
    };

    std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept;
    const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept;
    bool validateMinilab3Profile() noexcept;
}
```

Why this shape fits the next two phases:

- It is expressive enough for keyboard note ranges, absolute CC knobs/faders, relative encoders, and “other” traffic.
- It encodes decision state directly (`requiredForPhase7`, `deferred`, `unsupported`) so later phases do not need to reverse-engineer the intent from prose.
- It keeps the table read-only and detached from APVTS, which preserves the shared-core boundary.

#### 1.3.3 Profile Module Invariants

`validateMinilab3Profile()` should check only structural facts, not runtime hardware state.

Recommended invariants:

- Every `controlId` is unique.
- Every `channel` is in `[1, 16]`.
- Every note number and controller number range is within `[0, 127]` when populated.
- Every Phase 7 core control candidate appears exactly once.
- Every deferred pad or encoder row has a non-empty explanatory note.

Implementation guidance:

- Prefer `static_assert` for fixed compile-time facts.
- Use `jassert` or a simple non-allocating validation loop for runtime-only checks in debug builds.
- Do not add a test framework solely for this phase.

### 1.4 Function Signatures and Module Responsibilities

Recommended function surface:

```cpp
// MidiMonitor.cpp
void MidiMonitorBuffer::pushMessage(const juce::MidiMessage& message,
                                    double timestampSeconds) noexcept;
juce::String formatMonitorMessageType(MidiMonitorMessageType type);
juce::String formatNoteName(int noteNumber);
juce::String formatRawMidiBytes(const MidiMonitorRawBytes& rawBytes);

// Minilab3Profile.cpp
std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept;
const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept;
bool validateMinilab3Profile() noexcept;
```

Recommended `MidiMonitorPanel` UI helpers:

```cpp
juce::String getColumnText(const coolsynth::midi::MidiMonitorEvent& event, int columnId) const;
```

Notably absent by design:

- No `applyMinilabMapping(...)`
- No `matchEventToParameter(...)`
- No processor callback that writes APVTS values

Those belong to Phase 7, after the message table exists.

### 1.5 Architecture Decisions to Lock Before Coding

These decisions should be made explicitly during review of this blueprint:

1. `Minilab3Profile` is a code module, not a JSON or XML asset.
2. The verified message table is duplicated deliberately in code and in developer-facing markdown.
3. The monitor may gain `other` message visibility, but the synth still ignores unsupported MIDI behavior.
4. Pads and encoder remain deferred unless capture proves they emit stable, reviewable default traffic.

## 2. File-Level Strategy

### 2.1 Required Files for the Phase 6 Implementation

| File | Responsibility in this phase |
| --- | --- |
| `CMakeLists.txt` | Register `src/midi/Minilab3Profile.cpp` if the profile is implemented as a `.cpp` translation unit. |
| `src/midi/MidiMonitor.h` | Extend the capture model to preserve raw bytes and classify `other` messages without breaking the fixed-size queue. |
| `src/midi/MidiMonitor.cpp` | Implement the expanded evidence capture path while preserving real-time safety. |
| `src/ui/MidiMonitorPanel.h` | Add any new monitor columns or helper methods required to render raw or `other` events clearly. |
| `src/ui/MidiMonitorPanel.cpp` | Surface enough evidence in the UI to distinguish note/CC traffic from deferred or unsupported traffic. |
| `src/midi/Minilab3Profile.h` | Declare the verified MiniLab 3 control schema and lookup API. |
| `src/midi/Minilab3Profile.cpp` | Define the verified control table and structural validation helpers. |
| `docs/minilab3-default-messages.md` | Record the human-readable capture table, deviations, pad decision, and encoder decision. |
| `TODO.md` | Replace the completed Phase 5 list with the Phase 6 checklist before implementation begins. |
| `DONE.md` | Record the verified outcome only after build and manual validation are complete. |

### 2.2 Conditional Files

| File | Touch only if this condition is true |
| --- | --- |
| `REQUIREMENTS.md` | The captured hardware behavior contradicts preferred mapping assumptions in a way that changes accepted first-release behavior. |
| `DESIGN.md` | The capture results require a sharper rule for future mapping isolation, value mode handling, or deferred controls. |
| `src/ui/StandaloneMidiInputPanel.{h,cpp}` | Only if the current UI does not make the active capture device unambiguous enough during manual verification. |

### 2.3 Files That Should Not Change in Phase 6

Do not plan edits in these files unless a compile-only include change is unavoidable:

- `src/plugin/SynthAudioProcessor.{h,cpp}`
- `src/plugin/SynthAudioProcessorEditor.{h,cpp}`
- `src/synth/SynthEngine.cpp`
- `src/synth/SynthVoice.cpp`
- `src/parameters/*`
- `src/ui/HardwareKnob.*`
- `src/ui/HardwareFader.*`
- `src/ui/SynthSection.*`

Reason: this phase is not allowed to smuggle in controller behavior, synth behavior, or UI expansion unrelated to evidence capture.

## 3. Atomic Execution Steps

These are the Phase 6 checkbox expansions that should replace the completed Phase 5 list in `TODO.md` when implementation begins.

### 3.1 Checkbox: Capture the MiniLab 3 default note and CC messages for the controls planned for the fixed mapping milestone

#### Plan

- Confirm the device is connected in its intended default template state.
- Refresh `TODO.md` to the Phase 6 checklist so the work is tracked against the correct milestone.
- Decide the exact capture matrix before touching code:
  - keyboard
  - knobs 1 through 8
  - faders 1 through 4
  - pads
  - main encoder
- Verify whether the current monitor is sufficient. If pads or encoder produce anything other than note/CC traffic, the monitor must first gain `other` event visibility.

#### Act

- Implement the minimum monitor extension needed to preserve evidence for unsupported message kinds.
- Build the standalone target.
- Launch the standalone app.
- Select the MiniLab 3 as the active MIDI input.
- Capture one control group at a time with isolated gestures:
  - keyboard: several notes across low, middle, and high ranges
  - knobs: full sweep both directions
  - faders: bottom, middle, top, and repeated passes
  - pads: press and release each pad individually
  - encoder: clockwise and counterclockwise turns, plus press if the hardware emits one
- Record the observed channel, message type, primary data, and secondary-value behavior in a temporary capture sheet before curating the final table.

#### Validate

- Re-run at least two repeated captures for each control group.
- Reject any observation that is not repeatable while the device remains in the same template.
- Confirm that capture did not introduce parameter changes or synth-side behavior beyond the existing note path.
- Confirm the monitor remains bounded and responsive during repeated input sweeps.

### 3.2 Checkbox: Record the verified message table in isolated profile code and developer-facing documentation

#### Plan

- Freeze the verified row schema before entering data so code and docs cannot drift structurally.
- Choose stable `controlId` names now because Phase 7 will depend on them.
- Decide whether to group pads individually or as a deferred bank based on observed uniqueness.

#### Act

- Add `src/midi/Minilab3Profile.h` and `src/midi/Minilab3Profile.cpp`.
- Encode one row per verified hardware control or control family using the read-only profile schema.
- Add `docs/minilab3-default-messages.md` with the same table and richer prose notes.
- Include date, device assumption, and any template-specific caveat in the document header.

#### Validate

- The code table and markdown table match row-for-row.
- The project builds after the new module is added to `CMakeLists.txt`.
- `validateMinilab3Profile()` passes in debug.
- A reviewer can inspect the profile without reading monitor source code or replaying the entire capture session.

### 3.3 Checkbox: Document any deviations from the preferred mapping assumptions

#### Plan

- Compare the captured table against the preferred mapping table in `REQUIREMENTS.md` Section 8.4.
- Separate "message-level deviation" from "target assignment deviation".

#### Act

- In `docs/minilab3-default-messages.md`, add a dedicated deviation section.
- If the preferred first-release assumptions are no longer correct, update `REQUIREMENTS.md` with the observed behavior and rationale.
- If future mapping architecture needs clarification because of relative controls or unsupported traffic, update `DESIGN.md`.

#### Validate

- Every deviation is traceable to a specific observed message pattern.
- No deviation is left only in prose comments inside C++.
- There is a clear difference between "preferred but not observed" and "observed and accepted as source of truth".

### 3.4 Checkbox: Decide whether pads remain deferred based on observed default behavior

#### Plan

- Define the acceptance bar before evaluating pads:
  - stable repeated message pattern
  - no hidden mode switching during normal use
  - no unsupported or opaque traffic that would force controller-specific hacks into the shared core

#### Act

- Capture every pad individually.
- Classify the bank as one of:
  - `requiredForPhase7`
  - `deferred`
  - `unsupported`
- Record whether each pad emits note, CC, or other traffic.

#### Validate

- If pads are marked usable, the evidence must show predictable press and release semantics.
- If pads are deferred, the documentation must state the exact reason, not a generic placeholder.
- The decision must not block Phase 7 core mappings for waveform, ADSR, and master gain.

### 3.5 Checkbox: Decide whether the main encoder remains deferred based on observed default behavior

#### Plan

- Define the acceptance bar before evaluating the encoder:
  - message pattern is stable across repeated turns
  - direction is inferable
  - value mode is understandable enough to map later without hostile heuristics

#### Act

- Capture clockwise and counterclockwise turns separately.
- Capture slow and fast turns.
- Capture press behavior only if the hardware emits a distinct message for it.
- Record whether the encoder is absolute CC, relative CC, note-like, or other.

#### Validate

- If the encoder is marked anything other than deferred, the captured evidence must show a deterministic value mode.
- If it remains deferred, the documentation must state whether the blocker is unsupported traffic, unstable relative encoding, or simple lack of first-release need.

### 3.6 Checkbox: Verify the recorded table matches actual monitor output

#### Plan

- Freeze the code table and markdown table.
- Define a replay checklist that exercises every committed row.

#### Act

- Build the project in Debug.
- Re-run the standalone capture session against the committed table.
- Compare each observed control to:
  - `message kind`
  - `channel`
  - `primary data`
  - `secondary value range or mode`
  - deferred/required decision

#### Validate

- Zero unexplained mismatches are allowed.
- If a mismatch is found, update the verified profile first, then the docs, then the deviation notes.
- Do not move the phase to `DONE.md` until this replay pass succeeds.

## 4. Edge Case and Boundary Audit

| Failure mode or trap | Why it matters in this phase | Required handling |
| --- | --- | --- |
| MiniLab is not in the expected default template | All captured evidence becomes suspect | Stop capture, document the mismatch, and do not commit a verified table until the device state is confirmed. |
| Wrong MIDI device is selected | The evidence table can silently describe another controller | Require a visible device-status confirmation before every capture pass. |
| Pads or encoder emit non-note/non-CC messages | Current monitor would otherwise drop the evidence | Extend the monitor to retain `other` events with raw bytes. |
| Note-on with velocity zero appears | Could be misread as missing note-off behavior | Treat it as a note-off-style observation in the evidence notes. |
| Different control groups use different MIDI channels | A later mapping phase could route controls incorrectly | Record channel per row rather than assuming one global channel. |
| A knob or encoder emits relative values | Later APVTS mapping logic is materially different | Record `valueMode = relative` and keep it isolated from Phase 7 until reviewed. |
| One gesture emits multiple messages | Later mapping might over-trigger | Record every distinct message family per control or defer the control. |
| Pads share note numbers with keyboard range | Future routing may need extra discrimination | Document the overlap explicitly; do not solve it in Phase 6. |
| Device disconnects during capture | Evidence set becomes incomplete and ordering may be misleading | Abort the current pass, reconnect, and repeat the affected control group. |
| Persisted stale device selection resumes a missing device | Manual capture can begin against an unavailable state | Confirm `Connected to ...` before recording any evidence. |
| Monitor queue overruns during aggressive sweeps | Missing rows can make the verified table incomplete | Capture one control at a time and keep sweeps deliberate, not simultaneous. |
| Docs and code profile drift apart | Phase 7 consumes one source while reviewers read another | Update code and docs in the same change and verify row parity before completion. |
| `Minilab3Profile.cpp` is not added to `CMakeLists.txt` | Build fails despite correct source code | Add the file to `target_sources(...)` in the same patch. |
| MiniLab constants leak into processor or synth files | Breaks the architectural separation required by the project | Keep all control IDs and message numbers inside `src/midi/Minilab3Profile.*` only. |
| Phase 6 accidentally writes parameters | Violates the milestone boundary and hides behavioral risk | Reject any code that mutates APVTS or synth state from captured hardware controls. |
| Raw-byte capture allocates or formats strings in the callback | Violates real-time constraints | Copy fixed-size bytes only; format strings in the UI thread. |

## 5. Verification Protocol

### 5.1 Manual UX Verification Checklist

1. Confirm `TODO.md` has been refreshed to the Phase 6 checklist before coding starts.
2. Build the project in `Debug`.
3. Launch the standalone app, not the VST3 target.
4. Confirm the MIDI input status explicitly names the MiniLab 3 as connected.
5. Confirm the MIDI monitor still updates in real time and remains scrollable.
6. Clear mental capture state and exercise only one control group at a time.
7. Verify keyboard messages show stable note behavior across at least low, middle, and high ranges.
8. Verify each knob produces a stable, repeated message pattern through multiple full sweeps.
9. Verify each fader produces a stable, repeated message pattern through bottom, middle, and top positions.
10. Verify each pad's press and release behavior is captured or explicitly classified as unsupported/deferred.
11. Verify the main encoder's left and right turns can be distinguished or explicitly justify deferral.
12. Compare the committed code profile to the live monitor output for every row in the table.
13. Verify that moving knobs, faders, pads, or encoder does not introduce new synth-parameter behavior in this phase.
14. Verify the standalone app remains stable through disconnect/reconnect during a non-recording pass.

### 5.2 Automated Checks That Must Pass

1. `cmake --build build --config Debug`
2. `cmake --build build --config Debug --target CoolSynth` if a narrower target build is preferred locally.
3. Debug-only or compile-time profile invariants must pass:
   - unique `controlId` values
   - channel bounds `[1, 16]`
   - note and CC bounds `[0, 127]`
   - required core control rows present exactly once
4. No new compile warnings related to the new monitor or profile code.
5. `get_errors` or equivalent editor diagnostics show no new issues in touched files.

### 5.3 Exit Criteria for Review Sign-Off

- A reviewer can inspect the committed code and docs and see one exact verified MiniLab 3 message table.
- Pads and encoder are explicitly classified with evidence-based rationale.
- Any requirement drift is documented where it belongs.
- No controller-to-parameter behavior exists yet.
- The build passes and the standalone replay check matches the committed table.

## 6. Code Scaffolding

The scaffolding below is intentionally structural. It is not a prompt to implement extra behavior.

### 6.1 `src/midi/Minilab3Profile.h`

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace coolsynth::midi
{
    enum class Minilab3ControlCategory : uint8_t
    {
        keyboard,
        knob,
        fader,
        pad,
        encoder,
    };

    enum class Minilab3Disposition : uint8_t
    {
        requiredForPhase7,
        deferred,
        unsupported,
    };

    enum class Minilab3ValueMode : uint8_t
    {
        none,
        absolute,
        relative,
        velocity,
    };

    enum class VerifiedMidiMessageKind : uint8_t
    {
        note,
        controlChange,
        other,
    };

    struct MidiValueRange
    {
        int min = -1;
        int max = -1;
    };

    struct VerifiedMidiSignature
    {
        VerifiedMidiMessageKind kind = VerifiedMidiMessageKind::controlChange;
        uint8_t channel = 1;
        MidiValueRange primaryData {};
        MidiValueRange secondaryData {};
        Minilab3ValueMode valueMode = Minilab3ValueMode::none;
        bool emitsReleaseEvent = false;
        bool requiresRawByteInspection = false;
    };

    struct Minilab3ControlDefinition
    {
        std::string_view controlId;
        std::string_view displayName;
        Minilab3ControlCategory category = Minilab3ControlCategory::knob;
        std::string_view preferredTarget;
        Minilab3Disposition disposition = Minilab3Disposition::deferred;
        VerifiedMidiSignature signature {};
        std::string_view notes;
    };

    std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept;
    const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept;
    bool validateMinilab3Profile() noexcept;
}
```

### 6.2 `src/midi/Minilab3Profile.cpp`

```cpp
#include "Minilab3Profile.h"

#include <array>

namespace coolsynth::midi
{
    namespace
    {
        constexpr std::array<Minilab3ControlDefinition, /* fill after capture */ 1> verifiedControls {{
            Minilab3ControlDefinition {
                .controlId = "keyboard",
                .displayName = "Keyboard",
                .category = Minilab3ControlCategory::keyboard,
                .preferredTarget = "note input",
                .disposition = Minilab3Disposition::requiredForPhase7,
                .signature = VerifiedMidiSignature {
                    .kind = VerifiedMidiMessageKind::note,
                    .channel = 1,
                    .primaryData = MidiValueRange { .min = 0, .max = 127 },
                    .secondaryData = MidiValueRange { .min = 1, .max = 127 },
                    .valueMode = Minilab3ValueMode::velocity,
                    .emitsReleaseEvent = true,
                    .requiresRawByteInspection = false,
                },
                .notes = "Replace placeholder values with actual captured defaults.",
            },
        }};
    }

    std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept
    {
        return verifiedControls;
    }

    const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept
    {
        for (const auto& control : verifiedControls)
            if (control.controlId == controlId)
                return &control;

        return nullptr;
    }

    bool validateMinilab3Profile() noexcept
    {
        // Keep this loop allocation-free and side-effect-free.
        return !verifiedControls.empty();
    }
}
```

### 6.3 `src/midi/MidiMonitor.h` Event Extension

```cpp
struct MidiMonitorRawBytes
{
    static constexpr int maxBytes = 8;

    std::array<uint8_t, maxBytes> bytes {};
    uint8_t size = 0;
    bool truncated = false;
};

struct MidiMonitorEvent
{
    uint64_t eventOrder = 0;
    double timestampSeconds = 0.0;
    MidiMonitorMessageType type = MidiMonitorMessageType::noteOn;
    int channel = 0;
    int primaryValue = 0;
    int secondaryValue = 0;
    int noteNumber = -1;
    int controllerNumber = -1;
    MidiMonitorRawBytes rawBytes {};
};
```

### 6.4 `docs/minilab3-default-messages.md` Template

```md
# MiniLab 3 Default Messages

Capture date: YYYY-MM-DD
Device assumption: MiniLab 3 default template on the developer machine
Capture surface: standalone app MIDI monitor

## Verified Table

| Control | Category | Message kind | Channel | Primary data | Secondary behavior | Preferred target | Disposition | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Keyboard | keyboard | note | 1 | note range observed | velocity range observed | note input | requiredForPhase7 | |
| Knob 1 | knob | controlChange | ? | CC ? | absolute 0..127 | waveform | requiredForPhase7 | |

## Deviations From Preferred Assumptions

- None yet.

## Deferred Controls

- Pads: document exact reason.
- Encoder: document exact reason.
```

## Review Notes for This Blueprint

The key review question is not whether the project can capture MIDI events. It already can. The key review question is whether Phase 6 should explicitly harden the monitor into a reliable evidence tool before Phase 7 depends on a verified controller contract.

This blueprint takes the narrowest safe stance:

- add only enough monitor detail to preserve unexpected hardware evidence,
- isolate the verified MiniLab table in one new MIDI module,
- duplicate that table in one markdown document,
- and keep controller-to-parameter behavior out of scope until the evidence is reviewed.
