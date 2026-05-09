<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 10 Blueprint: Hardware-Style UI Refinement

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 10 - Hardware-style UI refinement`

Selection basis:

- `TODO.md` now promotes `Phase 10` as the active checklist immediately after the verified `Phase 9` delay slice.
- `IMPLEMENTATION_PLAN.md` was revised so `Phase 10` is no longer generic layout polish; it now explicitly consolidates standalone-only runtime utilities into one settings workflow and replaces the large standalone status panel with a compact bottom status bar.
- `REQUIREMENTS.md` now requires the standalone editor to keep synth controls in the main panel, move audio and MIDI utility controls into a dedicated settings surface, and expose a bottom status bar with audio state, MIDI-device state, and a live last-MIDI-event summary.
- The current code still shows the exact pre-refinement surfaces this phase is meant to fix: the standalone editor inlines `StandaloneAudioStatusPanel`, `StandaloneMidiInputPanel`, and `MidiMonitorPanel`; the output section still contains the panic button; and `StandaloneAudioStatusPanel` still exposes a redundant `Audio Settings...` button.
- `IMPLEMENTATION_PHASE10.md` does not exist yet, so this file is the next required surgical blueprint in the documented sequence.

## Scope Guardrails

In scope for this phase:

- Keep the main editor focused on synth controls and global actions.
- Separate panic from the output section so output remains about master level and action remains about commands.
- Add one dedicated standalone settings surface that owns audio-device configuration, MIDI input selection, and MIDI monitor presentation.
- Add a compact bottom status bar for standalone mode only.
- Surface a live last-MIDI-event summary in the status bar using lightweight event snapshots and message-thread formatting only.
- Remove app-owned redundant standalone entry points that duplicate audio or MIDI controls already present in the settings surface.
- Preserve the plugin editor as a synth-only surface with no standalone runtime UI.
- Improve readability of labels and value text without introducing a skinning project.

Explicitly out of scope for this phase:

- Any new audio, MIDI, controller-mapping, persistence, preset, or VST3 behavior.
- Any new synth parameters or DSP changes.
- MIDI learn, preset browser work, or standalone persistence beyond what already exists.
- Rewriting the standalone wrapper or forking JUCE standalone chrome.
- Hiding or restyling the JUCE wrapper's built-in options control if that requires invasive wrapper changes.
- Full visual reskinning, photorealistic controls, or custom rendering systems.
- Refactoring unrelated synth or controller code that already works.

Decision gates before implementation:

- The fix must address the root ownership problem: the MIDI controller and monitor buffer cannot remain owned by a view if that view becomes transient.
- The settings surface must reuse the shared standalone `AudioDeviceManager` and existing standalone MIDI controller path. It must not create a second runtime state source.
- The status bar must not format strings in the MIDI callback or audio callback.
- Standalone-only UI must stay behind `juce::JUCEApplicationBase::isStandaloneApp()` and any required standalone compile guards.
- If the JUCE wrapper's built-in options UI remains visible, the app-owned UI must still avoid presenting a second conflicting audio-settings button or inline device selectors in the main synth panel.

## Current Code Anchors

The blueprint should stay anchored to the files that already own the relevant behavior:

- `src/plugin/SynthAudioProcessorEditor.h` and `src/plugin/SynthAudioProcessorEditor.cpp` currently own the synth sections, the panic button, and all standalone-only UI composition. Standalone mode currently creates three large inline components: `StandaloneAudioStatusPanel`, `StandaloneMidiInputPanel`, and `MidiMonitorPanel`.
- `src/ui/StandaloneAudioStatusPanel.h` and `src/ui/StandaloneAudioStatusPanel.cpp` currently display backend, device, sample rate, buffer size, and status, and they still expose a redundant `Audio Settings...` button that calls `showStandaloneAudioSettingsDialog()`.
- `src/ui/StandaloneMidiInputPanel.h` and `src/ui/StandaloneMidiInputPanel.cpp` currently own both the `MidiMonitorBuffer` and the `StandaloneMidiInputController`, which is the main design problem once the panel is moved out of the always-visible editor.
- `src/ui/MidiMonitorPanel.h` and `src/ui/MidiMonitorPanel.cpp` already provide the bounded monitor table view and are otherwise structurally reusable.
- `src/standalone/StandaloneMidiInput.h` and `src/standalone/StandaloneMidiInput.cpp` already own the actual standalone MIDI device logic, device persistence, disconnect handling, monitor capture, and non-audio-thread controller-event handoff.
- `src/standalone/StandaloneAudioSupport.h` and `src/standalone/StandaloneAudioSupport.cpp` already expose audio snapshot helpers and currently contain the standalone-only audio dialog launcher.
- `src/midi/MidiMonitor.h` and `src/midi/MidiMonitor.cpp` already provide the bounded real-time-safe monitor event buffer. They currently do not expose a dedicated last-event snapshot API.
- `CMakeLists.txt` uses an explicit `target_sources(...)` list, so every new `.cpp` file must be registered there.

## Exact `TODO.md` Entries This Blueprint Expands

These are the active `Phase 10` checklist items that the execution plan below expands into concrete work:

- [ ] Refine the editor into grouped oscillator, filter, envelope, delay, output, and global action sections.
- [ ] Move standalone audio and MIDI utility controls into one dedicated settings surface.
- [ ] Replace the large standalone status panel with a compact bottom status bar.
- [ ] Add live last-MIDI-event status text to the standalone status bar.
- [ ] Remove redundant standalone audio or MIDI settings entry points.
- [ ] Ensure the plugin editor omits standalone-only device, settings, status, and monitor UI.
- [ ] Improve control labels and value readability.
- [ ] Verify the refined UI remains usable during playback.

## 1. Architectural Design

### 1.1 Controlling Design Decision

The main synth editor remains the canonical sound-control surface. Standalone-only runtime utilities move into a dedicated settings surface, while always-visible runtime feedback is compressed into a bottom status bar.

Required consequences:

- The main editor no longer owns large inline device panels or a full-height MIDI monitor.
- The settings surface becomes the only app-owned place where audio-device configuration, MIDI input selection, and the MIDI monitor are presented together.
- The bottom status bar becomes the only always-visible standalone runtime summary.
- The plugin editor continues to show only synth controls and the panic action.

### 1.2 Ownership and Lifetime Model

The current ownership model is not sufficient for the revised scope because `StandaloneMidiInputPanel` owns the `StandaloneMidiInputController` and `MidiMonitorBuffer`. Once that panel becomes transient, the controller would disappear whenever the settings surface is closed.

Required ownership model after this phase:

```text
SynthAudioProcessorEditor (standalone mode only)
  -> owns StandaloneMidiInputController for full editor lifetime
  -> owns StandaloneStatusBar for full editor lifetime
  -> launches StandaloneSettingsDialog on demand
     -> borrows StandaloneMidiInputController
     -> borrows AudioDeviceManager
     -> contains StandaloneMidiInputPanel view
     -> contains MidiMonitorPanel view
```

Strict lifetime rules:

- `StandaloneMidiInputController` must outlive every standalone UI view that depends on MIDI runtime state.
- `MidiMonitorBuffer` must be owned by the controller, not by a panel.
- `StandaloneStatusBar` may poll or observe the controller, but it must not own or recreate runtime state.
- The settings dialog must borrow shared runtime state and must not create a second `StandaloneMidiInputController`.

### 1.3 Component Graph After the Change

The intended standalone composition is:

```text
Standalone editor
  -> title row
  -> synth row
     -> oscillator section
     -> filter section
     -> envelope section
     -> delay section
     -> output section
     -> action section (panic)
  -> footer row
     -> Settings... button
  -> bottom status bar
     -> audio summary
     -> MIDI-device summary
     -> last MIDI event summary

Standalone settings dialog
  -> tabbed container
     -> Audio tab
        -> AudioDeviceSelectorComponent
     -> MIDI tab
        -> StandaloneMidiInputPanel
        -> MidiMonitorPanel
```

The intended plugin composition is:

```text
Plugin editor
  -> title row
  -> synth row
     -> oscillator section
     -> filter section
     -> envelope section
     -> delay section
     -> output section
     -> action section (panic)
```

### 1.4 Required Data Structures and State Definitions

#### 1.4.1 Public Snapshot for Last MIDI Event

Add a lightweight copyable snapshot type in `src/standalone/StandaloneMidiInput.h`.

```cpp
namespace coolsynth::standalone
{
    struct LastMidiEventSnapshot
    {
        bool hasEvent = false;
        uint64_t eventOrder = 0;
        coolsynth::midi::MidiMonitorMessageType type =
            coolsynth::midi::MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
    };
}
```

Why this structure is required:

- The status bar needs the most recent MIDI event without depending on the monitor table being visible.
- The structure matches the existing monitor-event vocabulary, so formatting logic can stay consistent.
- The structure is POD-like enough to copy safely on the message thread from atomics or cached fields.

#### 1.4.2 Internal Atomic Last-Event State

Inside `StandaloneMidiInputController`, store a lock-free internal state separate from the public snapshot.

```cpp
struct AtomicLastMidiEventState
{
    std::atomic<uint64_t> eventOrder { 0 };
    std::atomic<uint8_t> type { 0 };
    std::atomic<uint8_t> channel { 0 };
    std::atomic<int> primaryValue { 0 };
    std::atomic<int> secondaryValue { 0 };
    std::atomic<int> noteNumber { -1 };
    std::atomic<int> controllerNumber { -1 };
};
```

State rules:

- The MIDI callback may only write primitive fields or enqueue events. It must not allocate or format strings.
- The status bar copies the atomic fields into `LastMidiEventSnapshot` and formats readable text on the message thread.
- If no MIDI event has been received yet, `eventOrder == 0` means `hasEvent == false`.

#### 1.4.3 Standalone Editor Members

Add standalone-only state to `SynthAudioProcessorEditor`.

```cpp
std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> standaloneMidiController;
std::unique_ptr<juce::Component> standaloneStatusBar;
juce::TextButton standaloneSettingsButton { "Settings..." };
```

Do not keep these members in plugin mode.

#### 1.4.4 Status Bar State

The status bar should cache snapshots, not own runtime state.

```cpp
class StandaloneStatusBar final : public juce::Component,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
private:
    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::StandaloneMidiInputController& midiController;
    coolsynth::standalone::AudioDeviceSnapshot audioSnapshot;
    coolsynth::standalone::MidiInputSnapshot midiSnapshot;
    coolsynth::standalone::LastMidiEventSnapshot lastMidiSnapshot;

    juce::Label audioStatusLabel;
    juce::Label midiStatusLabel;
    juce::Label lastMidiStatusLabel;
};
```

#### 1.4.5 Settings Dialog State

The settings surface should own view objects only.

```cpp
class StandaloneSettingsDialog final : public juce::Component
{
private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::Component> audioTab;
    std::unique_ptr<juce::Component> midiTab;
};
```

The MIDI tab should contain a `StandaloneMidiInputPanel` and a `MidiMonitorPanel` that both borrow the shared controller or buffer.

### 1.5 Required Function Signatures

#### 1.5.1 `StandaloneMidiInputController`

Change the constructor so the controller owns the monitor buffer instead of receiving one from a panel.

```cpp
StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                              juce::PropertySet* settings,
                              ControllerEventHandler onControllerEvent,
                              DisconnectCallback onSelectedDeviceDisconnected = {});

const MidiInputSnapshot& getSnapshot() const noexcept;
coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept;
LastMidiEventSnapshot getLastMidiEventSnapshot() const noexcept;
```

Required internal helper:

```cpp
void updateLastMidiEventSnapshot(const juce::MidiMessage& message) noexcept;
```

#### 1.5.2 `StandaloneMidiInputPanel`

Convert the panel from owner to view.

```cpp
explicit StandaloneMidiInputPanel(coolsynth::standalone::StandaloneMidiInputController& controller);
```

Remove this API from the panel because ownership moves to the controller:

```cpp
coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept;
```

#### 1.5.3 `StandaloneStatusBar`

Add a new bottom status component.

```cpp
class StandaloneStatusBar final : public juce::Component,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    explicit StandaloneStatusBar(coolsynth::standalone::StandaloneMidiInputController& midiController);
    ~StandaloneStatusBar() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshAudioSnapshot();
    void refreshMidiSnapshot();
    void refreshLastMidiSnapshot();
    void refreshLabels();
};
```

#### 1.5.4 `StandaloneSettingsDialog`

Add a dedicated settings-surface component and launch helper.

```cpp
class StandaloneSettingsDialog final : public juce::Component
{
public:
    StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                             coolsynth::standalone::StandaloneMidiInputController& midiController);
    void resized() override;
};

bool showStandaloneSettingsDialog(juce::Component* parentComponent,
                                  juce::AudioDeviceManager& deviceManager,
                                  coolsynth::standalone::StandaloneMidiInputController& midiController);
```

The helper should own the dialog content and must not capture a stack-allocated component.

### 1.6 Layout Contract

Required main-editor layout rules:

- The synth sections remain the visual priority.
- `Output` should contain master gain only.
- `Actions` should contain panic only in this phase.
- The `Settings...` button should be secondary to the synth controls and should not read like a primary sound-shaping action.
- The bottom status bar should span the full editor width in standalone mode.

Recommended layout constants:

```cpp
static constexpr int titleHeight = 48;
static constexpr int synthRowHeight = 240;
static constexpr int footerRowHeight = 40;
static constexpr int statusBarHeight = 28;
static constexpr int outerPadding = 24;
static constexpr int sectionGap = 16;
```

### 1.7 Message and Threading Flow

The required flow after the change is:

```text
Standalone MIDI callback
  -> StandaloneMidiInputController::handleIncomingMidiMessage(...)
     -> monitorBuffer.pushMessage(...)
     -> updateLastMidiEventSnapshot(...)
     -> enqueueControllerEvent(...)
     -> triggerAsyncUpdate()

Message thread
  -> StandaloneMidiInputController::handleAsyncUpdate()
     -> dispatch controller events to SynthAudioProcessor
  -> StandaloneStatusBar timer
     -> copy last MIDI snapshot
     -> format readable summary text
  -> StandaloneSettingsDialog
     -> hosts AudioDeviceSelectorComponent
     -> hosts StandaloneMidiInputPanel and MidiMonitorPanel
```

Hard rules:

- No string formatting in the MIDI callback.
- No host-notifying parameter writes from the audio callback. This phase must not disturb the existing controller-thread handoff.
- The status bar may poll at a modest rate, for example `10 Hz` to `20 Hz`, because it is a UX surface, not a sample-accurate display.

## 2. File-Level Strategy

### Files to Touch During Phase 10 Implementation

| File | Responsibility in `Phase 10` |
| --- | --- |
| `CMakeLists.txt` | Add `StandaloneStatusBar.cpp` and `StandaloneSettingsDialog.cpp`. Remove `StandaloneAudioStatusPanel.cpp` from the source list if that component is retired instead of reused. |
| `src/plugin/SynthAudioProcessorEditor.h` | Add standalone-only members for the persistent MIDI controller, the status bar, and the settings button. Add a distinct `Actions` section if the output section is no longer allowed to contain panic. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | Recompose the main layout, create the persistent standalone MIDI controller, launch the settings dialog, add the status bar, remove inline standalone utility panels, and keep standalone-only surfaces gated out of plugin mode. |
| `src/standalone/StandaloneMidiInput.h` | Move `MidiMonitorBuffer` ownership into the controller, add `LastMidiEventSnapshot`, and add getters for the shared monitor buffer and last-event snapshot. |
| `src/standalone/StandaloneMidiInput.cpp` | Update runtime ownership, record the latest supported MIDI event in lock-free form, preserve device persistence and disconnect handling, and keep controller-event dispatch semantics unchanged. |
| `src/standalone/StandaloneAudioSupport.h` | Keep snapshot helpers and remove or replace the app-owned `showStandaloneAudioSettingsDialog()` API so it no longer advertises a redundant second dialog path. |
| `src/standalone/StandaloneAudioSupport.cpp` | Preserve audio snapshot behavior, and either delete the old audio-only dialog launcher or narrow the file to utility helpers only. |
| `src/ui/StandaloneMidiInputPanel.h` | Convert the panel to a pure view over a shared `StandaloneMidiInputController`. Remove panel-owned runtime state. |
| `src/ui/StandaloneMidiInputPanel.cpp` | Update the constructor, refresh behavior, and selection handling to use the shared controller instance passed in by the settings dialog. |
| `src/ui/StandaloneSettingsDialog.h` | Declare the new tabbed standalone settings surface and its launch helper. |
| `src/ui/StandaloneSettingsDialog.cpp` | Implement the settings dialog with an Audio tab backed by `AudioDeviceSelectorComponent` and a MIDI tab backed by `StandaloneMidiInputPanel` plus `MidiMonitorPanel`. |
| `src/ui/StandaloneStatusBar.h` | Declare the compact bottom status bar, its snapshot state, and its message-thread refresh hooks. |
| `src/ui/StandaloneStatusBar.cpp` | Implement audio and MIDI status refresh, last-event summary formatting, compact layout, and fallback text for missing devices or no recent MIDI input. |
| `src/ui/StandaloneAudioStatusPanel.h` | Delete or retire this file if the status panel is replaced rather than rewritten in place. |
| `src/ui/StandaloneAudioStatusPanel.cpp` | Delete or retire this file if the status panel is replaced rather than rewritten in place. |
| `README.md` | Update the user-facing standalone workflow to explain the `Settings...` surface and the bottom status bar. |
| `TODO.md` | Track active Phase 10 checkboxes during implementation. |
| `DONE.md` | Record completion only after validation and review are complete. |

Files that should not change in this phase unless a concrete defect is uncovered:

- `src/plugin/SynthAudioProcessor.cpp`
- `src/synth/*`
- `src/parameters/*`
- `src/midi/MidiMappingEngine.*`

This is a UI-structure and runtime-surface phase, not a synth-engine phase.

## 3. Atomic Execution Steps

Each subsection below expands one active TODO checkbox into a `Plan -> Act -> Validate` cycle.

### 3.1 Checkbox: Refine the Editor Into Grouped Oscillator, Filter, Envelope, Delay, Output, and Global Action Sections

#### Plan

- Keep the existing synth sections as the primary composition anchor.
- Split panic out of `Output` into a distinct `Actions` section so the layout matches the revised design intent.
- Preserve the existing parameter attachments and synth-control ordering to avoid unintended functional regressions.

#### Act

- Add a new `SynthSection actionsSection { "Actions" };` to `SynthAudioProcessorEditor`.
- Keep `masterGainFader` in `Output` only.
- Move `panicButton` into `Actions` and update `resized()` so `Output` and `Actions` are independently bounded.
- Keep all synth sections visible in both standalone and plugin mode.

#### Validate

- `cmake --build build --config Debug`
- Launch standalone and confirm the synth panel reads as sound controls first, with panic visually separate from output level.
- Launch plugin editor or preview path and confirm the same grouped synth sections appear without standalone runtime UI.

### 3.2 Checkbox: Move Standalone Audio and MIDI Utility Controls Into One Dedicated Settings Surface

#### Plan

- Create one settings surface instead of keeping three separate inline standalone panels.
- Reuse the existing standalone device manager and MIDI controller runtime state.
- Put audio and MIDI tools behind tabs so the synth editor stays compact.

#### Act

- Add `StandaloneSettingsDialog.{h,cpp}`.
- Build an `Audio` tab around `juce::AudioDeviceSelectorComponent` using the shared standalone `AudioDeviceManager`.
- Build a `MIDI` tab around a refactored `StandaloneMidiInputPanel` and `MidiMonitorPanel` using the shared `StandaloneMidiInputController` and its `MidiMonitorBuffer`.
- Add a single `Settings...` launcher button in standalone mode.

#### Validate

- Open the settings dialog and confirm both Audio and MIDI tabs render and remain interactive.
- Change audio backend, sample rate, and buffer size from the Audio tab and confirm the app remains stable.
- Select a MIDI device from the MIDI tab and confirm the selection persists after closing and reopening the dialog.

### 3.3 Checkbox: Replace the Large Standalone Status Panel With a Compact Bottom Status Bar

#### Plan

- Replace the current large `StandaloneAudioStatusPanel` with a low-height summary component.
- Show audio summary and MIDI-device summary without consuming the main synth layout area.

#### Act

- Add `StandaloneStatusBar.{h,cpp}`.
- Make it attach to the shared `AudioDeviceManager` as a `ChangeListener`.
- Make it read `MidiInputSnapshot` from the shared `StandaloneMidiInputController`.
- Remove `StandaloneAudioStatusPanel` from editor composition.

#### Validate

- Launch standalone and confirm the bottom bar stays visible at all times.
- Disconnect and reconnect audio or MIDI devices and confirm the bar updates without opening settings.
- Resize the standalone window, if resizable, and confirm the bar stays legible and anchored to the bottom edge.

### 3.4 Checkbox: Add Live Last-MIDI-Event Status Text to the Standalone Status Bar

#### Plan

- Reuse the existing monitor-event vocabulary so status text and monitor rows describe the same events.
- Keep the callback path allocation-free and string-free.

#### Act

- Extend `StandaloneMidiInputController` with `LastMidiEventSnapshot` support.
- Update `handleIncomingMidiMessage(...)` to capture the latest note-on, note-off, and CC facts into atomic fields.
- Add a status-bar formatter that turns the latest snapshot into concise text such as `Last MIDI: Note On C4 ch1 vel 96` or `Last MIDI: CC 74 ch1 value 92`.
- Refresh that text from the message thread only.

#### Validate

- Press MiniLab keys and confirm the last-event text changes immediately.
- Move mapped knobs and faders and confirm CC summaries replace note summaries.
- Launch standalone with no MIDI activity and confirm the bar shows a stable fallback message such as `Last MIDI: -` or `No recent MIDI`.

### 3.5 Checkbox: Remove Redundant Standalone Audio or MIDI Settings Entry Points

#### Plan

- Eliminate duplicate app-owned entry points that lead to overlapping audio or MIDI controls.
- Do not leave the stale `Audio Settings...` button in a component that is no longer part of the main editor.

#### Act

- Remove the inline `StandaloneAudioStatusPanel` from `SynthAudioProcessorEditor`.
- Remove the `Audio Settings...` button path from app-owned UI by deleting or retiring `showStandaloneAudioSettingsDialog()` and the panel that calls it.
- Remove the always-visible inline MIDI selector and monitor from the main editor.

#### Validate

- `rg "Audio Settings\.\.\.|StandaloneAudioStatusPanel|standaloneMidiMonitorPanel|standaloneMidiInputPanel" src/plugin src/ui src/standalone`
- Confirm the only app-owned standalone utility launcher left in the editor is the unified `Settings...` entry point.
- Confirm closing the settings dialog does not disable MIDI input or monitoring.

### 3.6 Checkbox: Ensure the Plugin Editor Omits Standalone-Only Device, Settings, Status, and Monitor UI

#### Plan

- Keep the standalone runtime UI behind standalone-only construction paths.
- Do not leak the settings button, status bar, or monitor into plugin mode.

#### Act

- Gate construction of `StandaloneMidiInputController`, `StandaloneStatusBar`, and `Settings...` button behind `juce::JUCEApplicationBase::isStandaloneApp()`.
- Keep plugin `setSize(...)` and layout calculations free of standalone-only rows.
- Ensure new standalone dialog headers are not pulled into plugin-only code paths unless guarded.

#### Validate

- Build the shared target and open the plugin editor.
- Confirm there is no settings button, no status bar, no audio-device text, no MIDI selector, and no MIDI monitor.
- Confirm panic, master gain, and the synth sections still work and render normally.

### 3.7 Checkbox: Improve Control Labels and Value Readability

#### Plan

- Improve readability by layout and labeling first, not by cosmetic complexity.
- Make section names and action placement self-explanatory.

#### Act

- Keep section titles consistent in casing and spacing.
- Ensure the `Settings...` launcher and the bottom status bar do not compete visually with synth controls.
- Review knob and fader value text alignment for cutoff, envelope times, delay parameters, and master gain.
- Ensure the status bar does not rely only on green or orange color to communicate state; text must remain explicit.

#### Validate

- Inspect the editor at normal scale and high-DPI scale if available.
- Confirm labels are readable without relying on color or raw normalized values.
- Confirm the main editor still reads like a synth panel rather than a device-management form.

### 3.8 Checkbox: Verify the Refined UI Remains Usable During Playback

#### Plan

- Run the phase as a behavior check, not just a static layout pass.
- Ensure the settings dialog and status bar do not disrupt controller input, playback, or UI responsiveness.

#### Act

- Build and launch standalone.
- Play notes while opening and closing the settings dialog.
- Change audio settings, change MIDI selection, and continue playing.
- Observe status-bar changes and control readouts during active playback.

#### Validate

- Confirm no crashes, hung UI, or stuck notes are introduced.
- Confirm the synth remains playable while the settings surface is opened and closed.
- Confirm controller-driven parameter changes still update the main synth UI in standalone mode.

## 4. Edge Case and Boundary Audit

| Risk or Boundary | Trigger | Required Handling |
| --- | --- | --- |
| Transient-panel lifetime bug | Settings dialog closes after creating its own MIDI controller | The controller must be editor-owned and survive dialog open/close cycles. |
| Dangling borrowed references | Standalone editor closes while settings dialog is still open | The dialog must not outlive the editor-owned runtime objects without an intentional lifetime strategy. Keep ownership explicit and tied to standalone app shutdown behavior. |
| Duplicate audio settings entry points | Old `Audio Settings...` button remains while `Settings...` also exists | Remove the app-owned duplicate path. One app-owned settings entry point only. |
| Audio tab shows an empty or useless surface | Dialog embeds the wrong device-selector configuration or a null device manager | Construct the audio selector directly from the live standalone `AudioDeviceManager` and verify outputs are enabled. |
| MIDI input stops working when settings closes | MIDI controller remains panel-owned | Move buffer and controller ownership into the persistent controller object. |
| Last MIDI status formats on callback thread | Status text built inside `handleIncomingMidiMessage(...)` | Store primitive facts only in the callback; format on timer or message thread. |
| No recent MIDI state looks broken | Status bar assumes at least one event exists | Provide a stable fallback text such as `Last MIDI: -`. |
| Disconnect while settings is closed | Device is unplugged after dialog closes | Status bar must still update via shared controller and device-manager listeners. |
| Plugin editor accidentally includes standalone UI | New standalone components are constructed unconditionally | Gate all standalone-only components and rows behind standalone-mode checks. |
| Status bar becomes a second monitor table | Too much detail added to the bottom bar | Keep status-bar text concise. Full event history belongs in the settings-surface monitor only. |
| Layout crowding at smaller heights | Standalone editor keeps old 850 px height assumptions while panels are removed | Recompute standalone size and row heights after removing inline utility panels. |
| Color-only status | Green or orange text without explicit message | Keep readable text like `Ready`, `No output device available`, or `Connected to MiniLab 3`. |
| Monitor and status drift apart | Last-event status uses different event semantics than `MidiMonitorPanel` | Use the same monitor-message taxonomy for both surfaces. |
| Wrapper options duplication remains confusing | JUCE standalone frame still exposes its own options button | Do not add more duplicates in the editor. If wrapper chrome cannot be changed safely, document the remaining platform-level behavior instead of fighting it in Phase 10. |

## 5. Verification Protocol

There is no dedicated unit-test suite in this repository yet, so `Phase 10` verification must combine compile validation, targeted static checks, and manual UX checks.

### 5.1 Build and Static Checks

1. Run `cmake --build build --config Debug`.
2. Run `get_errors` on every touched file and clear any new diagnostics caused by the phase.
3. Run `rg "Audio Settings\.\.\." src` and confirm no app-owned inline audio-settings button remains.
4. Run `rg "StandaloneAudioStatusPanel" src CMakeLists.txt` and confirm the old status panel is either removed from the build or intentionally repurposed, not orphaned.
5. Run `rg "isStandaloneApp\(|JUCEApplicationBase::isStandaloneApp" src/plugin/SynthAudioProcessorEditor.cpp src/ui` and confirm standalone-only surfaces are still gated.

### 5.2 Manual Standalone UX Checks

1. Launch the standalone app.
2. Confirm the main window shows synth sections first, not device-management panels.
3. Confirm `Output` contains master gain only and `Actions` contains panic.
4. Confirm `Settings...` exists in standalone mode.
5. Confirm a bottom status bar is visible immediately on launch.
6. With no MIDI activity, confirm the status bar shows stable audio and MIDI fallback text plus `Last MIDI: -` or equivalent.
7. Open `Settings...` and confirm the Audio tab renders live audio-device controls.
8. In the Audio tab, change output device, sample rate, and buffer size if available, then confirm the status bar updates afterward.
9. In the MIDI tab, confirm device selection and the full monitor are present together.
10. Select the MiniLab 3 and confirm the status text updates to a connected state.
11. Press MiniLab keys and confirm the main synth remains playable.
12. Confirm the status bar's last-event text updates for note events.
13. Move MiniLab knobs and faders and confirm the last-event text updates for CC events.
14. Keep playback running while opening and closing the settings dialog and confirm no controller regression occurs.
15. Unplug the selected MIDI device and confirm the app remains running, held notes clear as they already do, and the status bar reflects the disconnect.
16. Launch the app with no usable audio device if that scenario can be reproduced and confirm the status bar reports the problem without crashing.

### 5.3 Manual Plugin UX Checks

1. Launch the VST3 in a host or preview path.
2. Confirm there is no bottom status bar.
3. Confirm there is no settings button.
4. Confirm there is no audio-device text, MIDI selector, or MIDI monitor.
5. Confirm the synth sections, master gain, and panic action still render correctly.

### 5.4 Exit Criteria for This Phase

This phase is ready to move from `TODO.md` to `DONE.md` only when all of the following are true:

- The main standalone editor reads as a synth panel, not a runtime configuration form.
- Audio and MIDI utility controls live in one settings workflow instead of inline panels.
- The bottom status bar shows audio state, MIDI-device state, and a live last-MIDI-event summary.
- No app-owned redundant audio-settings entry point remains.
- Plugin mode omits standalone-only settings, status, and monitor UI.
- The build passes, touched-file diagnostics are clean, and the manual UX checks pass.

## 6. Code Scaffolding

These templates are intentionally structural. They define the target module seams without pre-implementing the phase.

### 6.1 `StandaloneStatusBar`

```cpp
// src/ui/StandaloneStatusBar.h
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "standalone/StandaloneAudioSupport.h"
#include "standalone/StandaloneMidiInput.h"

class StandaloneStatusBar final : public juce::Component,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    explicit StandaloneStatusBar(coolsynth::standalone::StandaloneMidiInputController& midiController);
    ~StandaloneStatusBar() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshAudioSnapshot();
    void refreshMidiSnapshot();
    void refreshLastMidiSnapshot();
    void refreshLabels();

    juce::String formatAudioSummary(const coolsynth::standalone::AudioDeviceSnapshot& snapshot) const;
    juce::String formatMidiSummary(const coolsynth::standalone::MidiInputSnapshot& snapshot) const;
    juce::String formatLastMidiSummary(const coolsynth::standalone::LastMidiEventSnapshot& snapshot) const;

    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::StandaloneMidiInputController& midiController;
    coolsynth::standalone::AudioDeviceSnapshot audioSnapshot;
    coolsynth::standalone::MidiInputSnapshot midiSnapshot;
    coolsynth::standalone::LastMidiEventSnapshot lastMidiSnapshot;

    juce::Label audioStatusLabel;
    juce::Label midiStatusLabel;
    juce::Label lastMidiStatusLabel;
};
```

### 6.2 `StandaloneSettingsDialog`

```cpp
// src/ui/StandaloneSettingsDialog.h
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "standalone/StandaloneMidiInput.h"

class StandaloneSettingsDialog final : public juce::Component
{
public:
    StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                             coolsynth::standalone::StandaloneMidiInputController& midiController);

    void resized() override;

private:
    class MidiTab final : public juce::Component
    {
    public:
        explicit MidiTab(coolsynth::standalone::StandaloneMidiInputController& midiController);
        void resized() override;

    private:
        StandaloneMidiInputPanel midiInputPanel;
        MidiMonitorPanel midiMonitorPanel;
    };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
};

bool showStandaloneSettingsDialog(juce::Component* parentComponent,
                                  juce::AudioDeviceManager& deviceManager,
                                  coolsynth::standalone::StandaloneMidiInputController& midiController);
```

### 6.3 Refactored `StandaloneMidiInputPanel`

```cpp
// src/ui/StandaloneMidiInputPanel.h
class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    explicit StandaloneMidiInputPanel(coolsynth::standalone::StandaloneMidiInputController& controller);
    ~StandaloneMidiInputPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshFromController();
    void repopulateDeviceSelector();
    void handleDeviceSelectionChanged();

    coolsynth::standalone::StandaloneMidiInputController& controller;
    bool isRefreshingSelector = false;

    juce::Label deviceTitleLabel;
    juce::ComboBox deviceSelector;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;
};
```

### 6.4 Refactored `StandaloneMidiInputController`

```cpp
// src/standalone/StandaloneMidiInput.h
namespace coolsynth::standalone
{
    struct LastMidiEventSnapshot
    {
        bool hasEvent = false;
        uint64_t eventOrder = 0;
        coolsynth::midi::MidiMonitorMessageType type =
            coolsynth::midi::MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
    };

    class StandaloneMidiInputController final : public juce::ChangeBroadcaster,
                                                private juce::MidiInputCallback,
                                                private juce::AsyncUpdater
    {
    public:
        StandaloneMidiInputController(juce::AudioDeviceManager& deviceManager,
                                      juce::PropertySet* settings,
                                      ControllerEventHandler onControllerEvent,
                                      DisconnectCallback onSelectedDeviceDisconnected = {});

        const MidiInputSnapshot& getSnapshot() const noexcept { return snapshot; }
        coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept { return monitorBuffer; }
        LastMidiEventSnapshot getLastMidiEventSnapshot() const noexcept;

    private:
        void updateLastMidiEventSnapshot(const juce::MidiMessage& message) noexcept;

        coolsynth::midi::MidiMonitorBuffer monitorBuffer;
        AtomicLastMidiEventState lastMidiEventState;
    };
}
```

### 6.5 Editor Integration Skeleton

```cpp
// src/plugin/SynthAudioProcessorEditor.h
private:
    coolsynth::ui::SynthSection actionsSection { "Actions" };
    juce::TextButton standaloneSettingsButton { "Settings..." };
    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> standaloneMidiController;
    std::unique_ptr<juce::Component> standaloneStatusBar;
```

```cpp
// src/plugin/SynthAudioProcessorEditor.cpp
if (juce::JUCEApplicationBase::isStandaloneApp())
{
    auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager();
    auto* settings = coolsynth::standalone::getStandaloneSettings();

    jassert(deviceManager != nullptr);

    standaloneMidiController = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
        *deviceManager,
        settings,
        [this](const coolsynth::midi::ControllerMidiEvent& event)
        {
            processor.handleStandaloneControllerEvent(event);
        },
        [this]
        {
            processor.requestPanic();
        });

    standaloneStatusBar = std::make_unique<StandaloneStatusBar>(*standaloneMidiController);
    addAndMakeVisible(*standaloneStatusBar);

    standaloneSettingsButton.onClick = [this, deviceManager]
    {
        if (deviceManager != nullptr && standaloneMidiController != nullptr)
            showStandaloneSettingsDialog(this, *deviceManager, *standaloneMidiController);
    };

    addAndMakeVisible(standaloneSettingsButton);
}
```

## Implementation Notes That Must Stay True

- This phase should not alter the synth parameter model, mapping engine semantics, or DSP code.
- It should solve the runtime-surface problem with minimal new abstraction: one persistent MIDI controller, one settings dialog, one bottom status bar.
- Every changed line in implementation should trace back to one of the eight active `Phase 10` checklist items above.
