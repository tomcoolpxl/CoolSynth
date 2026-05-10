# CC / MIDI Controller Refactor Plan

Status: planning only

This document is the implementation plan for refactoring CoolSynth's controller-mapping and MIDI-learn architecture.

It does not implement the refactor. It defines the target design, migration order, file-level work, risks, and validation gates.

## 1. Goal

Deliver a controller architecture that:

- keeps Arturia MiniLab 3 standalone support working out of the box
- removes controller-specific behavior from generic mapping code
- makes factory mappings data-driven instead of hardcoded in translation logic
- keeps one defensible parameter model for sound and automation
- preserves normal DAW workflow in VST3 mode by default
- adds a clean path for plugin-side MIDI learn where the host and format support it
- scales beyond one controller without redesign

## 2. External Findings

These findings shape the design choice.

### 2.1 VST3 reality

- Steinberg's VST3 model treats MIDI CC as parameter mapping, not as a special raw-control layer. Their `IMidiMapping` docs explicitly say controller functionality should be exported as regular parameters and transformed by the host into parameter changes.
- Steinberg's `IMidiLearn` is optional and host-dependent. It exists specifically so a plugin can change its CC-to-parameter mapping in response to live MIDI input and notify the host with `kMidiCCAssignmentChanged`.
- In VST3, "best practice" is therefore:
  - plugin parameters remain the canonical automation targets
  - CC mapping is a separate mapping layer
  - dynamic learn, when supported, updates that mapping layer rather than inventing a second synth state model

Sources:

- Steinberg `IMidiMapping`: <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.0.1/IMidiMapping.html>
- Steinberg `IMidiLearn`: <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.6.12/IMidiLearn.html>
- Steinberg "About MIDI in VST 3": <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/About%2BMIDI/Index.html>

### 2.2 JUCE reality

- JUCE already exposes `VST3ClientExtensions` for additional VST3 edit-controller or processor interfaces.
- JUCE's VST3 wrapper already implements `IMidiMapping` internally and, when MIDI input is enabled, creates synthetic MIDI-controller parameters that the host can map to and that JUCE translates back into `MidiMessage::controllerEvent(...)` entries in the plugin `MidiBuffer`.
- JUCE also exposes `AudioProcessorEditor::getControlParameterIndex()`, which enables VST3/AAX parameter-under-mouse control, including Steinberg "AI Knob" behavior.
- JUCE standalone provides direct MIDI device identifiers and names via `MidiInput` and `AudioDeviceManager`, which makes device recognition appropriate in standalone mode.

Sources:

- JUCE `VST3ClientExtensions`: <https://docs.juce.com/master/structjuce_1_1VST3ClientExtensions.html>
- JUCE `AudioProcessorEditor`: <https://docs.juce.com/master/classjuce_1_1AudioProcessorEditor.html>
- JUCE `AudioDeviceManager`: <https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html>
- JUCE `MidiInput`: <https://docs.juce.com/develop/classjuce_1_1MidiInput.html>

### 2.3 Mature synth behavior

- Mature synths do not avoid shipped default mappings; they separate them from user-learned mappings and store them as explicit mapping data.
- Surge XT is a useful current reference:
  - plugin-side context-menu MIDI learn
  - clear learned mapping
  - soft takeover
  - save mapping profiles
  - set a mapping as default
  - host-provided VST3 context actions may coexist with plugin actions

Source:

- Surge XT manual: <https://surge-synthesizer.github.io/manual-xt/index.html>

### 2.4 Arturia/MiniLab 3 reality

- MiniLab 3 exposes multiple ports and distinct operating modes. Arturia's own docs separate:
  - `MiniLab 3 MIDI`
  - `MiniLab 3 MCU/HUI`
  - `MiniLab 3 ALV`
- Arturia mode and DAW mode are controller-side modes, not something a generic plugin can reliably query through a normal MIDI input API.
- MiniLab 3 templates and user programs exist in Arturia's MIDI Control Center and can be exported as `.minilab3` files, but that is controller-software workflow data, not a standard runtime API for third-party plugins to query from the device.

Implication:

- The software can and should ship known-good factory profiles.
- The software should not depend on runtime introspection of the MiniLab's current program or on parsing MCC files as the primary control path.

Sources:

- Arturia MiniLab 3 DAW Integration FAQ, updated April 23, 2026: <https://support.arturia.com/hc/en-us/articles/6283884020508-MiniLab-3-DAW-Integration>
- Arturia MiniLab 3 general FAQ, updated April 3, 2026: <https://support.arturia.com/hc/en-us/articles/6189475866396-MiniLab-3-General-Questions>
- MiniLab 3 MIDI Control Center manual: <https://downloads.arturia.net/products/minilab-3/manual/minilab-3-mcc_Manual_1_14_0_EN.pdf>

## 3. Current Repo Assessment

The current code has the right broad intent but the wrong long-term shape.

### 3.1 What is good

- There is already a fixed-profile layer plus learned overrides.
- Learned bindings are separated from synth parameters and persisted.
- Soft takeover already exists.
- Standalone device selection and persistence are already isolated from the processor.

### 3.2 What is wrong

- MiniLab defaults are hardcoded as code-time arrays in `src/midi/Minilab3Profile.cpp`.
- MIDI learn is created only in standalone UI flow, not as a shared controller feature.
- The waveform encoder path contains controller-specific behavior in generic translation logic.
- The plugin currently has no clean product-level controller strategy; it only receives host MIDI notes and ignores plugin-side controller mapping as a first-class concern.
- The current architecture still assumes "standalone controller mapping" rather than "shared controller domain with mode-specific transport."

### 3.3 Concrete local smells to remove

- Controller definitions and bindings are embedded in code:
  - `src/midi/Minilab3Profile.cpp`
- Learn UI is standalone-gated:
  - `src/plugin/SynthAudioProcessorEditor.cpp`
- Standalone-only controller application path:
  - `src/plugin/SynthAudioProcessor.cpp`
- Controller-specific waveform branch in generic mapper:
  - `src/midi/MidiMappingEngine.cpp`

## 4. Recommended End State

### 4.1 Decision summary

Recommended architecture:

- keep a bundled factory profile for MiniLab 3 Arturia mode
- move factory mappings from C++ arrays into data files loaded by a profile registry
- keep standalone auto-detection and auto-activation of the MiniLab profile
- keep learned overrides as a separate layer on top of the active profile
- make the controller domain shared across standalone and plugin
- keep plugin default behavior host-centric, not device-centric
- add plugin-side learn, but only through a design that preserves parameter truth and host automation semantics

### 4.2 Explicit recommendation for standalone

Standalone should:

- auto-detect MiniLab 3 by MIDI input identifier/name
- auto-activate a bundled `MiniLab 3 / Arturia Mode` factory profile
- allow the user to keep or replace that active profile
- allow learned overrides on top of that profile
- persist:
  - selected hardware device
  - selected factory profile
  - learned overrides
  - CC-badge visibility

This preserves the current "works out of the box" requirement.

### 4.3 Explicit recommendation for VST3

VST3 should not auto-detect "MiniLab 3" and silently apply a controller-owned default profile.

Default plugin behavior should be:

- host/DAW owns controller workflow
- plugin exposes normal parameters
- plugin allows host automation
- plugin does not fight DAW scripts or DAW mode mappings

Optional plugin behavior should be:

- user may enable plugin-side mapping
- user may MIDI-learn plugin parameters
- user may choose a factory profile manually if they want direct controller-to-plugin mapping independent of DAW scripts

This matches standard DAW/plugin expectations more closely than forcing device-specific defaults inside the plugin.

### 4.4 Why this is the right split

Standalone is the place where the application owns the hardware device.

Plugin mode is the place where the host owns routing and control-surface policy.

Trying to make both targets behave identically at the hardware-device level is the wrong abstraction boundary.

The correct commonality is:

- one controller profile format
- one binding model
- one learn model
- one target-parameter registry
- one persistence model per target type

The transport and activation policy differ by target.

## 5. Architecture Choice

Three viable designs exist.

### Option A: keep fixed mappings hardcoded and extend the current standalone learn model

Reject.

Why:

- scales poorly
- keeps controller-specific branches in mapper logic
- does not solve plugin-mode architecture
- remains difficult to test and document

### Option B: remove all factory mappings and rely only on learn

Reject.

Why:

- breaks the out-of-box MiniLab requirement
- does not match mature instrument behavior
- forces needless setup for the most important supported controller

### Option C: data-driven factory profiles plus learned overrides plus target-specific activation policy

Recommended.

Why:

- preserves out-of-box MiniLab support
- generalizes to other controllers
- removes controller-specific behavior from generic code
- keeps standalone and plugin behavior principled instead of merely similar

## 6. Core Design

## 6.1 New controller domain model

Introduce a generic controller-domain layer with these concepts.

### `ControllerMessageSignature`

Represents what arrives from hardware or host.

Fields:

- message kind
  - `controlChange`
  - `noteOn`
  - `noteOff`
  - future: `channelPressure`, `pitchBend`
- channel mode
  - `specific`
  - `omni`
- primary data
  - CC number or note number

### `ControllerValueMode`

Represents how incoming values should be interpreted.

Initial supported modes:

- `absolute7`
- `threeStepAbsolute`
- `relativeBinaryOffset`
- `relativeTwoComplement`
- `noteGate`

This is the mechanism that replaces the current hardcoded `CC 114` branch in `MidiMappingEngine`.

### `ControllerTarget`

Represents what a binding controls.

Initial supported target kinds:

- `parameter`
- `command`

Initial commands:

- `panic`

### `ControllerBinding`

Represents one factory or learned mapping.

Fields:

- binding ID
- display label
- signature
- value mode
- target kind
- parameter ID or command ID
- enable flag
- source tag
  - `factory`
  - `user`

### `ControllerProfile`

Represents a reusable mapping set.

Fields:

- profile ID
- display name
- vendor/device hints
- intended controller mode
  - `arturia`
  - `daw`
  - `user1` ... future
- bindings
- version

### `ControllerProfileMatchRule`

Standalone-only device/profile activation rule.

Fields:

- MIDI input identifier regex or exact match
- device name regex or exact match
- optional product-family hint

### `LearnedOverrideSet`

Represents user-learned mappings layered on top of a profile.

Rules:

- one learned binding per parameter
- one signature may map to only one target
- learned binding shadows the matching factory binding

## 6.2 Runtime layering

Active mapping must be assembled as:

`factory profile` + `learned overrides` -> `resolved active bindings`

The runtime should never mutate bundled factory profile data.

## 6.3 Parameter resolution

Bindings should resolve by stable parameter ID, not by controller-specific enum targets.

That means the current `Minilab3LogicalTarget` enum should stop being the central runtime contract.

Recommended replacement:

- a `ParameterTargetRegistry` that resolves known parameter IDs to:
  - `juce::RangedAudioParameter*`
  - display name
  - curve/value interpretation metadata if needed

## 6.4 Persistence split

Persist different things in different scopes.

Standalone settings:

- selected MIDI input device
- selected controller factory profile
- learned overrides
- UI preference for CC labels

Plugin instance state:

- plugin MIDI-learn overrides for that instance
- plugin-selected controller profile mode if plugin mapping is enabled

Plugin-global user settings, if added later:

- optional default mapping preset
- optional default plugin learn channel policy

Do not mix standalone controller settings into plugin state.

## 7. Product Behavior After Refactor

## 7.1 Standalone

Expected user flow:

1. User selects `MiniLab 3 MIDI`.
2. App recognizes MiniLab 3.
3. App auto-activates `MiniLab 3 / Arturia Mode`.
4. Controls work immediately.
5. User may learn overrides.
6. Overrides persist across app restarts.

If the selected device is not MiniLab 3:

- no device-specific factory profile should be forced
- user may choose a profile manually or use learn

## 7.2 VST3 default behavior

Expected user flow:

1. User inserts CoolSynth in a DAW.
2. Controller remains under host/DAW policy by default.
3. Host MIDI notes and automation work as usual.
4. No MiniLab-specific plugin profile is auto-applied.

This preserves existing DAW workflows and avoids conflicting with DAW scripts or control-surface assignments.

## 7.3 VST3 optional direct controller mode

Expected user flow:

1. User enables plugin mapping.
2. User chooses a factory profile or MIDI learn.
3. Host routes CCs to the plugin.
4. Plugin mapping controls exposed synth parameters.

Important:

- this is opt-in
- it should never be the default mode

## 8. Plugin-Side Strategy

This is the most important design choice in the plan.

## 8.1 What not to do

Do not solve plugin learn by adding a second permanent synth-value plane that bypasses plugin parameters.

Why:

- weakens automation/state semantics
- complicates UI synchronization
- breaks the design principle that plugin-visible sound controls are stable parameters
- creates a hard-to-debug split between "what the synth sounds like" and "what the host thinks the parameter is"

## 8.2 Recommended VST3 strategy

Use a two-level VST3 plan.

### Level 1: standard plugin behavior

- continue to expose normal parameters
- implement `AudioProcessorEditor::getControlParameterIndex()`
- use host-provided parameter context where available

This is immediate polish and low risk.

### Level 2: plugin-owned CC mapping

Implement a plugin mapping subsystem that is VST3-aware through JUCE `VST3ClientExtensions`.

Recommended design:

- a `CoolSynthVST3ControllerExtensions` object owned by `SynthAudioProcessor`
- it exposes custom `IMidiMapping` and optionally `IMidiLearn`
- it reads the active plugin controller profile and learned overrides
- when mappings change, it calls `restartComponent(kMidiCCAssignmentChanged)` through the stored VST3 component handler

Why this is recommended:

- it is the VST3-native way to express CC-to-parameter mapping
- it keeps the APVTS parameter model canonical
- it avoids direct host-notifying parameter writes from your own `processBlock`

## 8.3 Important host-support caveat

`IMidiLearn` is optional in the VST3 spec. Therefore plugin-side learn cannot be assumed to work identically in every host.

Plan consequence:

- plugin-side learn should be implemented
- host compatibility must be validated host-by-host
- unsupported-host behavior must degrade cleanly

Recommended degraded behavior:

- if host live-CC learn callback is not available or not observed, plugin mapping UI remains visible but the user is told to use host routing or host mapping instead
- plugin mapping remains opt-in

This is better than faking universal support with a weak internal design.

## 8.4 Whether plugin MIDI learn should exist

Recommendation: yes, but as a plugin feature, not as automatic MiniLab device recognition.

Plugin learn should be:

- generic
- parameter-scoped
- clearable
- stored per plugin instance
- additive to host automation, not a replacement for it

Plugin learn should not:

- assume the physical device is MiniLab 3
- assume the controller is in DAW mode or Arturia mode
- auto-claim DAW control surface behavior

## 9. Factory Profile Strategy

## 9.1 Move factory mappings to data

Replace hardcoded C++ arrays with bundled profile data files.

Recommended path:

- `resources/controller_profiles/minilab3_arturia_mode.json`
- optional later:
  - `resources/controller_profiles/minilab3_user_default.json`
  - other controller profiles

The exact directory name can vary, but the data must live as data, not as translation code.

## 9.2 Profile file format

Recommended JSON shape:

```json
{
  "profileId": "arturia.minilab3.arturia-mode.v1",
  "displayName": "MiniLab 3 / Arturia Mode",
  "deviceHints": {
    "nameContains": ["MiniLab 3", "Minilab3"],
    "identifierContains": []
  },
  "bindings": [
    {
      "bindingId": "knob1-cutoff",
      "displayName": "Knob 1",
      "signature": { "kind": "controlChange", "channel": "omni", "data1": 74 },
      "valueMode": "absolute7",
      "target": { "kind": "parameter", "parameterId": "filterCutoffHz" }
    }
  ]
}
```

## 9.3 Do not parse `.minilab3` as runtime truth

Shipping a companion `.minilab3` file for users is reasonable.

Using `.minilab3` files as the runtime source of truth for plugin/app mapping is not recommended in this refactor.

Why:

- controller-side MCC files are a separate ecosystem
- no standard runtime contract exists between the plugin and those files
- runtime loading from MCC format adds integration fragility for little product value

Practical recommendation:

- optionally ship a companion MCC template later
- keep CoolSynth's own controller profile format independent

## 10. Standalone Device Detection

## 10.1 Keep device recognition in standalone only

Use JUCE device info to select factory profiles in standalone mode.

Match against:

- MIDI device identifier
- MIDI device name

The current standalone device controller already persists identifiers and names. Reuse that.

## 10.2 Matching policy

Recommended matching order:

1. exact identifier rule
2. exact name rule
3. substring name match
4. no automatic profile

Do not guess beyond that.

If multiple profiles match:

- prefer exact identifier
- then exact name
- then most-specific profile
- otherwise require manual choice

## 10.3 UI behavior on match

When a device match occurs:

- activate the matched factory profile
- show the active profile name in standalone settings
- do not interrupt the user if the same profile is already active

## 11. UI Plan

## 11.1 Standalone UI

Move from "standalone MIDI learn controls" to "controller mapping controls available in standalone transport."

Recommended standalone settings additions:

- active controller profile
- profile source: auto-detected or manual
- reset learned overrides
- export/import learned override set later, optional

The current per-control context menu flow is good and should remain.

## 11.2 Plugin UI

Add plugin-side controller controls carefully.

Recommended initial plugin UI additions:

- per-control context menu:
  - `MIDI Learn`
  - `Abort MIDI Learn`
  - `Clear Learned MIDI`
  - optional `Assign to MIDI CC...` later
- a small status line only when plugin mapping is enabled
- no standalone device selector
- no assumption about MiniLab identity

If a host context menu for the parameter exists, append it after plugin items instead of replacing it.

## 11.3 Parameter-to-control mapping for hosts

Implement `getControlParameterIndex()` for knobs, faders, and waveform selector.

This improves:

- VST3 parameter-under-mouse workflows
- Steinberg AI-knob style control
- general host integration polish

## 12. Runtime and Threading Plan

## 12.1 Standalone path

Keep the standalone apply path off the audio thread.

Recommended flow:

`MidiInputCallback -> lightweight event -> AsyncUpdater/message thread -> resolve binding -> beginChangeGesture/setValueNotifyingHost/endChangeGesture`

This keeps current real-time assumptions intact.

## 12.2 VST3 path

Do not add project-owned host-notifying parameter mutation inside `processBlock`.

Preferred VST3 flow:

`host MIDI CC -> host/plugin mapping layer -> parameter change -> JUCE wrapper -> APVTS parameter change -> DSP reads atomics`

This is the cleanest way to keep the parameter model canonical in VST3.

## 12.3 Soft takeover

Soft takeover should remain in the controller binding layer, but it must be generalized by value mode and target parameter rather than by MiniLab-specific branches.

Required behaviors:

- continuous absolute controls support pickup or scaling
- discrete controls can bypass soft takeover
- relative encoders use encoder-specific interpretation instead of pickup logic

## 13. File-Level Refactor Plan

The exact names may shift, but the refactor should roughly move in this direction.

## 13.1 New files

Recommended new domain files:

- `src/midi/ControllerProfile.h`
- `src/midi/ControllerProfile.cpp`
- `src/midi/ControllerProfileRegistry.h`
- `src/midi/ControllerProfileRegistry.cpp`
- `src/midi/ControllerProfileLoader.h`
- `src/midi/ControllerProfileLoader.cpp`
- `src/midi/ControllerMappingState.h`
- `src/midi/ControllerMappingState.cpp`
- `src/midi/ControllerTargetRegistry.h`
- `src/midi/ControllerTargetRegistry.cpp`

Recommended plugin extension files:

- `src/plugin/CoolSynthVST3ControllerExtensions.h`
- `src/plugin/CoolSynthVST3ControllerExtensions.cpp`

Recommended resource files:

- `resources/controller_profiles/minilab3_arturia_mode.json`

## 13.2 Files to replace or shrink

- `src/midi/Minilab3Profile.cpp`
  - remove runtime binding truth from this file
  - keep only compatibility helpers temporarily, then delete or convert to profile registration metadata

- `src/midi/MidiMappingEngine.h/.cpp`
  - rename or evolve into a generic controller mapping engine
  - stop depending on `Minilab3LogicalTarget`
  - remove direct MiniLab-specific waveform branch

## 13.3 Files to modify

- `src/plugin/SynthAudioProcessor.h/.cpp`
  - own controller target registry
  - own plugin mapping state
  - expose VST3 client extensions

- `src/plugin/SynthAudioProcessorEditor.h/.cpp`
  - support plugin-side context-menu learn flow
  - implement `getControlParameterIndex()`
  - append host parameter menu actions where available

- `src/standalone/StandaloneMidiInput.h/.cpp`
  - integrate profile auto-selection
  - keep device ownership local to standalone

- `src/standalone/SettingsStore.h/.cpp`
  - persist selected controller profile separately from learned overrides
  - support migration from old learned-binding-only schema

## 14. Persistence and Migration

## 14.1 Standalone migration

Current persisted learned bindings should migrate into:

- active factory profile: `none` or detected MiniLab profile
- learned overrides: existing bindings

Migration rule:

- if previous learned bindings exist and no explicit profile is stored, keep them as overrides on top of the auto-detected profile or `none`

## 14.2 Plugin state

Plugin controller mappings should live in plugin state, not in standalone settings.

Recommended first plugin-state schema:

- plugin mapping enabled flag
- selected plugin factory profile ID
- learned override bindings

Store it inside the processor state tree under a separate child node so it travels with the DAW session.

## 14.3 Future optional feature

Later, if desired, add:

- "Set current plugin mapping as default"
- "Save mapping preset as..."

This matches mature synth behavior better than only global controller settings.

## 15. Implementation Phases

## Phase 0: document and freeze contracts

Before code changes:

- decide final JSON schema
- decide final persistence split
- decide whether plugin mapping is opt-in via UI toggle or implicit when any plugin learned binding exists

Exit criteria:

- no unresolved contract ambiguity on profile format or persistence ownership

## Phase 1: generic controller-domain refactor

Work:

- add generic controller profile types
- add target registry by parameter ID
- convert current fixed MiniLab bindings into profile data
- remove `Minilab3LogicalTarget` from runtime binding truth
- remove MiniLab-specific waveform special-case logic

Exit criteria:

- standalone fixed profile still works
- all previous fixed mappings can be expressed by data

## Phase 2: standalone integration

Work:

- add profile registry and loader
- auto-select MiniLab profile based on device info
- persist active profile selection
- layer learned overrides on top

Exit criteria:

- standalone behaves like today out of the box on MiniLab 3
- learned overrides still shadow factory bindings

## Phase 3: editor integration

Work:

- make learnable-control registration target-agnostic
- add active-profile status to standalone UI
- add plugin-side context menu plumbing
- implement `getControlParameterIndex()`

Exit criteria:

- standalone learn UI still works
- plugin editor knows which components map to which parameters

## Phase 4: plugin mapping support

Work:

- add processor-owned VST3 client-extension object
- expose dynamic mapping state to `IMidiMapping`
- add mapping-change restart notifications
- add plugin mapping enable/profile selection

Exit criteria:

- plugin can advertise current controller profile to supporting VST3 hosts
- changing plugin profile invalidates and rebuilds host CC mapping cleanly

## Phase 5: plugin learn support

Work:

- add plugin learn session state
- expose learn arming from plugin UI
- implement `IMidiLearn` path in VST3 extension object
- update mapping and restart host on successful learn

Exit criteria:

- plugin-side learn works in at least one validated VST3 host
- unsupported hosts degrade cleanly

## Phase 6: persistence and cleanup

Work:

- migrate old settings format
- persist plugin mapping in plugin state
- remove dead MiniLab-specific runtime code
- update docs

Exit criteria:

- old standalone settings still load safely
- no duplicated or orphaned mapping logic remains

## 16. Validation Matrix

This refactor is high enough risk that it should have explicit manual gates.

## 16.1 Standalone gates

- MiniLab 3 selected, Arturia mode active:
  - default profile auto-loads
  - every current default control works
- learned CC overrides:
  - override the factory mapping
  - persist across restart
  - can be cleared independently
- unplug/replug:
  - device selection behavior remains stable
  - active profile status is understandable

## 16.2 Plugin gates

Minimum:

- VST3 still loads and plays notes in host
- automation still works
- no regression to state recall

Preferred host validation list:

- Steinberg host, because VST3 mapping behavior is native there
- Ableton Live, because that is a likely user workflow and conflict-risk environment
- one lightweight VST3 test host if available

Plugin mapping gates:

- plugin mapping disabled:
  - host workflow unchanged
- plugin mapping enabled:
  - mapped CC controls parameter as expected
- plugin learn armed:
  - successful learn updates mapping in supporting host

## 16.3 Regression tests

Even though a full unit-test suite is not the product priority, this refactor should add focused tests for:

- profile parsing
- profile resolution and override shadowing
- duplicate-binding normalization
- value-mode decoding
- takeover-state transitions
- standalone settings migration

## 17. Risks

## 17.1 Main risk: host support variability for VST3 learn

Risk:

- `IMidiLearn` is optional
- host behavior will vary

Mitigation:

- implement plugin learn behind explicit host validation
- degrade cleanly when host support is missing
- keep plugin mapping opt-in

## 17.2 Main risk: over-design

Risk:

- creating a large generic controller subsystem for a small synth

Mitigation:

- keep scope tight
- only support the message/value modes actually needed now
- do not build a marketplace of controller profiles

## 17.3 Main risk: persistence confusion

Risk:

- users do not understand why standalone and plugin mappings persist differently

Mitigation:

- name them explicitly in UI:
  - standalone controller profile
  - plugin mapping for this instance

## 18. Non-Goals for This Refactor

This refactor should not include:

- multi-controller simultaneous standalone support
- aftertouch or pitch-bend learn
- pad/note learn beyond current command use
- OSC or MIDI 2.0 support
- deep DAW control-surface integration
- parsing Arturia MCC project internals as runtime truth

## 19. Docs to Update When Implementation Starts

These repo docs will need updates once the refactor is underway:

- `REQUIREMENTS.md`
  - current standalone-only learn language
  - current controller-strategy wording
  - plugin learn deferral wording
- `DESIGN.md`
  - shared versus mode-specific code
  - standalone MIDI learn section
  - plugin UI differences
  - current open technical decisions
- `README.md`
  - user-facing controller behavior
  - standalone versus plugin mapping expectations

## 20. Final Recommendation

Implement the refactor around a shared, data-driven controller profile system.

The product behavior should be:

- standalone:
  - MiniLab 3 works out of the box via auto-selected factory profile
  - learned overrides remain available

- VST3:
  - host/DAW control remains the default
  - plugin mapping is available as an explicit feature
  - plugin MIDI learn exists, but as a format-aware VST3 feature, not as hardware auto-detection

This is the cleanest way to preserve today's good behavior while removing the hacky parts.

