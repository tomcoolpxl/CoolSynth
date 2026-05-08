# Phase 1 Blueprint: Reproducible JUCE Build Skeleton

## Phase Selection

Selected phase: `Phase 1 - Reproducible JUCE build skeleton`

Selection basis:

- `TODO.md` is empty, so no later phase has been staged for execution.
- `DONE.md` is empty, so no earlier phase is verified complete.
- The repository currently contains planning documents only: there is no `src/` tree, no `CMakeLists.txt`, no `CMakePresets.json`, and no JUCE dependency checkout.
- `IMPLEMENTATION_PLAN.md` defines `Phase 1` as the root dependency for all later code phases.

This blueprint therefore assumes the next implementation review cycle is the initial bootstrap of the shared standalone plus VST3 codebase.

## Scope Guardrails

In scope for this phase:

- Reproducible JUCE dependency acquisition.
- Top-level CMake project with one shared JUCE plugin target and `Standalone` plus `VST3` outputs.
- Placeholder `SynthAudioProcessor` and `SynthAudioProcessorEditor` classes that compile and open a window.
- Stable first-release APVTS parameter layout with the full required parameter ID set.
- Windows build documentation for a clean checkout using `Visual Studio 17 2022`.

Explicitly out of scope for this phase:

- Any audible synthesis behavior.
- Any standalone audio-device or MIDI-device UI.
- MIDI monitor, controller mapping, MiniLab logic, synth engine, filter, delay, or panic implementation.
- CI, preset work, standalone persistence, or host validation.
- Creating `src/synth`, `src/midi`, `src/standalone`, or `src/ui` directories before they are needed by later phases.

## 1. Architectural Design

### 1.1 Build Topology

Use a single JUCE plugin target as the ownership boundary for all shared runtime code.

Target graph:

```text
CoolSynth                    # shared-code target created by juce_add_plugin
|- CoolSynth_Standalone      # auto-generated JUCE wrapper target
`- CoolSynth_VST3            # auto-generated JUCE wrapper target
```

Rules:

- Do not create a separate standalone executable target by hand.
- Do not create a separate VST3 target by hand.
- Do not create phase-local utility libraries yet; the source surface is still too small to justify them.
- Keep plugin metadata fixed now so later VST3 validation is not invalidated by preventable identity drift.

Required CMake identity:

- Target name: `CoolSynth`
- Product name: `CoolSynth`
- Plugin name: `CoolSynth`
- Manufacturer display name: `tomcoolpxl`
- Manufacturer code: `Tcpx`
- Plugin code: `Csyn`
- Formats: `Standalone VST3`
- Instrument flags: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT FALSE`, `IS_MIDI_EFFECT FALSE`
- VST3 categories: `Instrument Synth`
- Copy-after-build: disabled in this phase to avoid permission friction on Windows

Recommended JUCE pin candidate:

- Submodule path: `external/JUCE`
- Pin candidate: JUCE `8.0.12`
- Commit candidate: `29396c22c93392d6738e021b83196283d6e4d850`

Decision rule:

- Use the pin above unless configure or compile uncovers a concrete compatibility problem on the development machine.
- If a different JUCE commit is required, freeze the new exact commit in the same review cycle and update the README bootstrap instructions immediately.

### 1.2 Minimal Phase-1 Source Tree

Create only the directories required to compile the shared processor skeleton:

```text
external/
`- JUCE/                     # git submodule

src/
|- parameters/
|  |- ParameterIDs.h
|  |- ParameterLayout.h
|  `- ParameterLayout.cpp
`- plugin/
   |- SynthAudioProcessor.h
   |- SynthAudioProcessor.cpp
   |- SynthAudioProcessorEditor.h
   `- SynthAudioProcessorEditor.cpp
```

Do not add deeper directory segmentation yet. Phase 1 should establish stable ownership, not speculative structure.

### 1.3 Runtime Object Model

The minimal runtime object graph for this phase is:

```text
JUCE wrapper
  -> SynthAudioProcessor
     -> juce::AudioProcessorValueTreeState parameters
  -> SynthAudioProcessorEditor
```

Objects intentionally absent in Phase 1:

- `SynthEngine`
- `MidiMappingEngine`
- `MidiMonitor`
- standalone device managers or persistence models

Reason:

- The phase goal is build reproducibility and a stable parameter API, not early feature stubs that create unused-state warnings and blur ownership boundaries.

### 1.4 Stable Parameter API

All first-release synth controls must exist now as stable plugin parameters, even though later phases will consume them gradually.

Parameter conventions:

- Use `juce::ParameterID { id, 1 }` for every parameter.
- Treat version hint `1` and every string ID below as frozen API.
- Use a flat parameter layout in Phase 1. Do not introduce parameter groups unless there is a concrete host-organization need.
- Keep all non-parameter runtime state out of APVTS.

Required parameter table:

| ID | JUCE Type | Range or Values | Default | Display Rule | Notes |
| --- | --- | --- | --- | --- | --- |
| `waveform` | `AudioParameterChoice` | `sine`, `square`, `saw` | `saw` | choice text | Default index must match `saw` |
| `ampAttackMs` | `AudioParameterFloat` | `1.0f` to `5000.0f` | `10.0f` | milliseconds | logarithmic feel |
| `ampDecayMs` | `AudioParameterFloat` | `5.0f` to `5000.0f` | `200.0f` | milliseconds | logarithmic feel |
| `ampSustain` | `AudioParameterFloat` | `0.0f` to `1.0f` | `0.8f` | percent or 0.00-1.00 | linear |
| `ampReleaseMs` | `AudioParameterFloat` | `5.0f` to `5000.0f` | `300.0f` | milliseconds | logarithmic feel |
| `filterCutoffHz` | `AudioParameterFloat` | `20.0f` to `20000.0f` | `10000.0f` | Hz or kHz | logarithmic feel |
| `filterResonance` | `AudioParameterFloat` | `0.0f` to `1.0f` | `0.1f` | normalized value | later DSP mapping stays internal |
| `delayTimeMs` | `AudioParameterFloat` | `1.0f` to `1000.0f` | `250.0f` | milliseconds | logarithmic feel |
| `delayFeedback` | `AudioParameterFloat` | `0.0f` to `0.85f` | `0.25f` | percent | hard upper limit remains `0.85f` |
| `delayMix` | `AudioParameterFloat` | `0.0f` to `1.0f` | `0.0f` | percent | `0.0f` is practical bypass |
| `masterGainDb` | `AudioParameterFloat` | `-60.0f` to `0.0f` | `-12.0f` | dB | final output gain later |

Formatting rules:

- Millisecond parameters must render as `N ms` below `1000 ms` and `N.N s` at or above `1000 ms`.
- Cutoff must render as `N Hz` below `1000 Hz` and `N.N kHz` at or above `1000 Hz`.
- `masterGainDb` must render with `dB` suffix.
- `ampSustain`, `delayFeedback`, and `delayMix` may render as percent values for readability.

### 1.5 Required Data Structures and Type Definitions

`ParameterIDs.h` should be the canonical stable-ID header.

```cpp
namespace coolsynth::parameters::ids
{
    inline constexpr int versionHint = 1;

    inline constexpr char waveform[] = "waveform";
    inline constexpr char ampAttackMs[] = "ampAttackMs";
    inline constexpr char ampDecayMs[] = "ampDecayMs";
    inline constexpr char ampSustain[] = "ampSustain";
    inline constexpr char ampReleaseMs[] = "ampReleaseMs";
    inline constexpr char filterCutoffHz[] = "filterCutoffHz";
    inline constexpr char filterResonance[] = "filterResonance";
    inline constexpr char delayTimeMs[] = "delayTimeMs";
    inline constexpr char delayFeedback[] = "delayFeedback";
    inline constexpr char delayMix[] = "delayMix";
    inline constexpr char masterGainDb[] = "masterGainDb";
}

namespace coolsynth::parameters
{
    enum class WaveformChoice : int
    {
        sine = 0,
        square = 1,
        saw = 2,
    };
}
```

`ParameterLayout.h` should expose exactly one creation function in Phase 1.

```cpp
namespace coolsynth::parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
```

`SynthAudioProcessor` should remain minimal.

```cpp
class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SynthAudioProcessor();
    ~SynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    APVTS& getValueTreeState() noexcept;
    const APVTS& getValueTreeState() const noexcept;

private:
    APVTS parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthAudioProcessor)
};
```

`SynthAudioProcessorEditor` should stay presentation-only.

```cpp
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SynthAudioProcessorEditor (SynthAudioProcessor&);
    ~SynthAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SynthAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthAudioProcessorEditor)
};
```

State definitions for this phase:

- `parameters` is the only processor-owned state that must persist.
- `prepareToPlay()` may cache sample rate and block size later, but it does not need additional member state in Phase 1.
- `processBlock()` must render silence and avoid all logging, allocation, locking, parameter-notification writes, and future-engine placeholders.
- `getStateInformation()` and `setStateInformation()` must use `parameters.copyState()` and `parameters.replaceState()` only outside the audio callback.

### 1.6 Bus, Editor, and Serialization Boundaries

Processor boundaries:

- Input bus count: none.
- Output bus: stereo main output.
- `isBusesLayoutSupported()` should accept stereo output and may accept mono output if the implementation can do so without extra complexity.
- No standalone device UI or MIDI selector logic may live in the processor.
- No device-selection, monitor, or debug UI state may be serialized into APVTS.

Editor boundaries:

- The editor is a fixed-size placeholder only.
- It may show title text, build-skeleton text, and a short note that audio and MIDI controls arrive in later phases.
- It must not reach into future synth-engine internals.

Serialization boundary:

- Save only APVTS parameter state.
- Use XML via `ValueTree::createXml()` and `copyXmlToBinary()` for `getStateInformation()`.
- Validate the XML tag against `parameters.state.getType()` in `setStateInformation()` before calling `replaceState()`.

## 2. File-Level Strategy

Exact file set for Phase 1 implementation:

| Path | Change Type | Responsibility |
| --- | --- | --- |
| `.gitmodules` | new | Records JUCE submodule URL and path. |
| `external/JUCE` | new git submodule | Pinned JUCE dependency checkout. |
| `CMakeLists.txt` | new | Root build graph, JUCE inclusion, plugin target definition, target sources, compiler standard, warnings. |
| `CMakePresets.json` | new | Canonical VS Code and CLI configure/build presets using `Visual Studio 17 2022`. |
| `src/parameters/ParameterIDs.h` | new | Frozen parameter IDs and `WaveformChoice` enum. |
| `src/parameters/ParameterLayout.h` | new | Declaration of the canonical APVTS layout factory. |
| `src/parameters/ParameterLayout.cpp` | new | Full parameter object construction, ranges, display lambdas. |
| `src/plugin/SynthAudioProcessor.h` | new | Processor class declaration and APVTS ownership. |
| `src/plugin/SynthAudioProcessor.cpp` | new | JUCE processor overrides, silence-only `processBlock`, APVTS state save/restore, editor creation. |
| `src/plugin/SynthAudioProcessorEditor.h` | new | Placeholder editor declaration. |
| `src/plugin/SynthAudioProcessorEditor.cpp` | new | Placeholder layout and paint logic for a launchable window. |
| `README.md` | update | Bootstrap steps, submodule init, configure/build commands, expected artifacts, current phase limitations. |
| `TODO.md` | update | Refresh with the six Phase 1 checkboxes from `IMPLEMENTATION_PLAN.md` before coding starts. |
| `DONE.md` | update after verification | Record only verified Phase 1 items after all checks pass. |

Files intentionally not touched in Phase 1:

- `.gitignore` because it already ignores `build/` and common binaries.
- Any `src/synth`, `src/midi`, `src/standalone`, or `src/ui` file.
- Any CI config.
- Any preset or persistence format file.

## 3. Atomic Execution Steps

Every Phase 1 `TODO.md` checkbox should be executed as a self-contained `Plan -> Act -> Validate` cycle.

### 3.1 Checkbox: Add the JUCE git submodule under `external/JUCE` and pin it to a known commit

Plan:

- Use a git submodule, not a zip drop and not an unpinned branch checkout.
- Freeze the dependency to one exact commit so a clean checkout reproduces the same build.
- Prefer the current stable JUCE release tag `8.0.12` unless the local toolchain proves incompatible.

Act:

1. Add the submodule at `external/JUCE`.
2. Check out the pinned commit inside the submodule.
3. Stage both `.gitmodules` and the submodule gitlink.
4. Do not edit JUCE source files in this phase.

Validate:

1. Run `git submodule status --recursive`.
2. Confirm the reported SHA matches the pinned commit and does not show `-dirty`.
3. Confirm `external/JUCE/CMakeLists.txt` exists.
4. Confirm the README bootstrap commands reference `git submodule update --init --recursive`.

### 3.2 Checkbox: Create the top-level CMake project that builds standalone and VST3 outputs from one shared plugin target

Plan:

- Build one `juce_add_plugin(CoolSynth ...)` target and let JUCE generate wrapper targets.
- Keep CMake flat and explicit: top-level project, JUCE inclusion, target sources, target link libraries, recommended warning/config flags.
- Default the documented Windows flow to the `Visual Studio 17 2022` generator with an x64 build directory.

Act:

1. Create `CMakeLists.txt` with `cmake_minimum_required(VERSION 3.22)` and `project(CoolSynth LANGUAGES CXX VERSION 0.1.0)`.
2. Add `add_subdirectory(external/JUCE)`.
3. Call `juce_add_plugin(CoolSynth ...)` with the fixed metadata and `FORMATS Standalone VST3`.
4. Add `target_sources(CoolSynth PRIVATE ...)` for the six new source files.
5. Link `juce::juce_audio_utils`, `juce::juce_recommended_config_flags`, and `juce::juce_recommended_warning_flags`.
6. Set `CXX_STANDARD 20`.
7. Add `target_compile_definitions(CoolSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)` unless the initial configure shows they are unnecessary and omitting them keeps the build simpler.
8. Create `CMakePresets.json` with one debug and one release preset using the Visual Studio generator.

Validate:

1. Run `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`.
2. Confirm configure completes without missing-target or missing-module errors.
3. Inspect the generated target list if needed and confirm JUCE created `CoolSynth_Standalone` and `CoolSynth_VST3` wrappers.
4. Confirm no extra manual executable or plugin targets were added.

### 3.3 Checkbox: Add `SynthAudioProcessor` and `SynthAudioProcessorEditor` placeholder classes

Plan:

- Start from the minimal JUCE plugin skeleton, then remove anything not required.
- Keep the processor silent and the editor visually obvious as a placeholder.
- Expose APVTS access through one accessor for near-future UI attachment work; do not add future engine members yet.

Act:

1. Implement `SynthAudioProcessor` with APVTS construction in the member initializer list.
2. Set up standard JUCE overrides for a synth with MIDI input and no audio input.
3. Implement `processBlock()` to clear the output buffer and ignore MIDI for now.
4. Implement `createEditor()` and `hasEditor()`.
5. Implement `SynthAudioProcessorEditor` with a fixed size, a title label, and a status label such as `Phase 1 build skeleton - audio and MIDI features arrive in later phases`.

Validate:

1. Build Debug.
2. Launch the standalone artifact.
3. Confirm a window opens without crashing.
4. Confirm the editor paints text and closes cleanly.
5. Confirm there is no audible output and no placeholder DSP path was accidentally introduced.

### 3.4 Checkbox: Define the initial APVTS parameter layout with the stable IDs from `REQUIREMENTS.md`

Plan:

- Put all parameter IDs in one header.
- Put all parameter creation in one `.cpp` factory.
- Use the modern APVTS constructor that takes a `ParameterLayout`; do not use the deprecated add-after-construction workflow.
- Include all required first-release parameters now, even if later phases do not yet read them.

Act:

1. Create `ParameterIDs.h` containing the exact IDs and `WaveformChoice` enum.
2. Create `ParameterLayout.h/.cpp` containing `createParameterLayout()`.
3. Implement helper functions in `ParameterLayout.cpp` for log-like ranges and text formatting.
4. Construct each parameter with stable `juce::ParameterID` objects using version hint `1`.
5. Use `juce::AudioParameterChoice` for `waveform` and `juce::AudioParameterFloat` for the remaining controls.
6. Initialize `SynthAudioProcessor::parameters` with `createParameterLayout()` in the constructor initializer list.

Validate:

1. Build Debug.
2. Confirm there are exactly 11 parameters in the layout.
3. Confirm every ID string matches `REQUIREMENTS.md` exactly, including casing.
4. Confirm the state serialization round-trip succeeds without null XML or mismatched root-tag handling.
5. Confirm no parameter is created dynamically after APVTS construction.

### 3.5 Checkbox: Document the Windows configure and build workflow in `README.md` using `Visual Studio 17 2022` as the default generator

Plan:

- Expand the README from the current one-line placeholder into a concise bootstrap guide.
- Document only the supported Windows-first path for now.
- Include exact commands and expected artifact locations.

Act:

1. Add prerequisites: Git, CMake 3.22+, Visual Studio 2022 Build Tools or Visual Studio 2022, Windows 11.
2. Add submodule bootstrap commands.
3. Add configure and build commands using the Visual Studio generator.
4. Add expected output paths for standalone and VST3 artifacts.
5. Add a short current-status note clarifying that Phase 1 is a silent build skeleton with a placeholder window.

Validate:

1. Re-run the README commands from a clean shell.
2. Confirm they match the actual build layout produced by CMake.
3. Confirm the README does not promise audio, MIDI, or controller behavior yet.

### 3.6 Checkbox: Verify a Debug build produces a launchable standalone artifact and a VST3 artifact

Plan:

- Treat this as the final gate for the phase.
- Validate both build artefacts, but keep host-loading tests deferred to Phase 11.

Act:

1. Build Debug from the configured build directory.
2. Locate the standalone executable under the JUCE artifacts directory.
3. Locate the VST3 bundle under the JUCE artifacts directory.
4. Launch the standalone executable.

Validate:

1. `cmake --build build --config Debug` completes successfully.
2. `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe` exists.
3. `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3` exists.
4. The standalone app opens a window and remains stable at idle.
5. Build output introduces no new warnings that are accepted as `temporary`.

## 4. Edge Case and Boundary Audit

- Parameter ID drift: a later rename of `waveform`, `ampAttackMs`, or any other ID will break saved state and invalidate later VST3 smoke assumptions.
- Wrong four-character codes: `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` must stay four characters and preserve the chosen case pattern.
- APVTS construction trap: using the deprecated constructor plus later `createAndAddParameter()` calls weakens lifecycle guarantees and should be rejected.
- State-threading trap: `copyState()` and `replaceState()` are not realtime-safe and must never leak into `processBlock()`.
- Scope creep trap: adding an early `SynthEngine`, audio-device component, or MIDI monitor here will make the phase larger than its exit criteria.
- Wrapper duplication trap: creating custom standalone or VST3 targets in parallel with `juce_add_plugin` will split ownership and cause maintenance churn.
- Permission trap on Windows: enabling copy-after-build can fail on protected plugin directories; keep this off for Phase 1.
- Wrong bus layout trap: exposing audio input or rejecting the host's supported mono/stereo output layouts can cause unnecessary load failures.
- Placeholder DSP trap: adding a test tone or synthetic audio path violates the plan, which explicitly keeps Phase 2 silent until real synth work lands later.
- README drift trap: if the build directory, preset names, or artifact paths differ from the README, the phase is not complete even if local build succeeds.
- JUCE pin drift: leaving the submodule at a floating branch head makes the build non-reproducible.
- Unused abstraction trap: adding future-phase member fields that are not exercised in Phase 1 increases warning risk and obscures the phase boundary.

## 5. Verification Protocol

### 5.1 Automated Checks

1. Run `git submodule update --init --recursive`.
2. Run `git submodule status --recursive` and confirm the pinned SHA is clean.
3. Run `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`.
4. Run `cmake --build build --config Debug`.
5. Confirm the standalone artifact exists at `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe`.
6. Confirm the VST3 artifact exists at `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3`.
7. Review the build output and clear all new warnings introduced by this phase.

### 5.2 Manual UX Checks

1. Launch `CoolSynth.exe`.
2. Confirm a window titled for `CoolSynth` opens.
3. Confirm the editor paints placeholder text instead of a blank or broken surface.
4. Leave the app idle for at least 15 seconds and confirm no crash occurs.
5. Close and relaunch the app once to confirm repeat startup stability.
6. Confirm the app remains silent; no early synth behavior should exist in this phase.

### 5.3 Review Checklist Before Moving Anything to `DONE.md`

1. Confirm only Phase 1 files and responsibilities were introduced.
2. Confirm the parameter list matches `REQUIREMENTS.md` exactly.
3. Confirm the processor owns APVTS and uses one shared-code architecture for standalone and VST3.
4. Confirm no standalone-only device logic exists in the processor.
5. Confirm the README matches the real bootstrap flow from a clean checkout.
6. Confirm unresolved follow-up work is written in `TODO.md`, not left implicit.

## 6. Code Scaffolding

The snippets below are structural templates, not the final reviewed implementation. They define the intended shape and ownership boundaries for this phase.

### 6.1 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)

project(CoolSynth VERSION 0.1.0 LANGUAGES CXX)

add_subdirectory(external/JUCE)

juce_add_plugin(CoolSynth
    COMPANY_NAME "tomcoolpxl"
    PRODUCT_NAME "CoolSynth"
    PLUGIN_NAME "CoolSynth"
    PLUGIN_MANUFACTURER_CODE Tcpx
    PLUGIN_CODE Csyn
    FORMATS Standalone VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(CoolSynth
    PRIVATE
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/SynthAudioProcessorEditor.cpp)

target_include_directories(CoolSynth
    PRIVATE
        src)

target_compile_features(CoolSynth PUBLIC cxx_std_20)

target_compile_definitions(CoolSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(CoolSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
```

### 6.2 `CMakePresets.json`

```json
{
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 22,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "vs2022-debug",
      "displayName": "Visual Studio 2022 Debug",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build",
      "architecture": {
        "value": "x64",
        "strategy": "external"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "vs2022-release",
      "displayName": "Visual Studio 2022 Release",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build",
      "architecture": {
        "value": "x64",
        "strategy": "external"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "build-debug",
      "configurePreset": "vs2022-debug",
      "configuration": "Debug"
    },
    {
      "name": "build-release",
      "configurePreset": "vs2022-release",
      "configuration": "Release"
    }
  ]
}
```

### 6.3 `src/parameters/ParameterIDs.h`

```cpp
#pragma once

namespace coolsynth::parameters::ids
{
    inline constexpr int versionHint = 1;

    inline constexpr char waveform[] = "waveform";
    inline constexpr char ampAttackMs[] = "ampAttackMs";
    inline constexpr char ampDecayMs[] = "ampDecayMs";
    inline constexpr char ampSustain[] = "ampSustain";
    inline constexpr char ampReleaseMs[] = "ampReleaseMs";
    inline constexpr char filterCutoffHz[] = "filterCutoffHz";
    inline constexpr char filterResonance[] = "filterResonance";
    inline constexpr char delayTimeMs[] = "delayTimeMs";
    inline constexpr char delayFeedback[] = "delayFeedback";
    inline constexpr char delayMix[] = "delayMix";
    inline constexpr char masterGainDb[] = "masterGainDb";
}

namespace coolsynth::parameters
{
    enum class WaveformChoice : int
    {
        sine = 0,
        square = 1,
        saw = 2,
    };
}
```

### 6.4 `src/parameters/ParameterLayout.h`

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace coolsynth::parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
```

### 6.5 `src/parameters/ParameterLayout.cpp`

```cpp
#include "ParameterLayout.h"

#include "ParameterIDs.h"

namespace
{
    juce::NormalisableRange<float> makeLogRange(float minValue, float maxValue)
    {
        return {
            minValue,
            maxValue,
            [] (float start, float end, float proportion)
            {
                return start * std::pow(end / start, proportion);
            },
            [] (float start, float end, float value)
            {
                return std::log(value / start) / std::log(end / start);
            },
            [] (float value)
            {
                return value;
            }
        };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout coolsynth::parameters::createParameterLayout()
{
    using namespace coolsynth::parameters;
    using namespace coolsynth::parameters::ids;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { waveform, versionHint },
        "Waveform",
        juce::StringArray { "sine", "square", "saw" },
        static_cast<int>(WaveformChoice::saw)));

    // Add the remaining 10 AudioParameterFloat objects here using the exact IDs,
    // ranges, defaults, and display lambdas defined in Section 1.4.

    return layout;
}
```

### 6.6 `src/plugin/SynthAudioProcessor.h`

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SynthAudioProcessor();
    ~SynthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CoolSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    APVTS& getValueTreeState() noexcept { return parameters; }
    const APVTS& getValueTreeState() const noexcept { return parameters; }

private:
    APVTS parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
```

### 6.7 `src/plugin/SynthAudioProcessor.cpp`

```cpp
#include "SynthAudioProcessor.h"

#include "SynthAudioProcessorEditor.h"
#include "parameters/ParameterLayout.h"

SynthAudioProcessor::SynthAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "CoolSynthState", coolsynth::parameters::createParameterLayout())
{
}

void SynthAudioProcessor::prepareToPlay(double, int)
{
}

void SynthAudioProcessor::releaseResources()
{
}

bool SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.inputBuses.isEmpty())
        return false;

    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    return new SynthAudioProcessorEditor(*this);
}

void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
```

### 6.8 `src/plugin/SynthAudioProcessorEditor.h`

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class SynthAudioProcessor;

class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SynthAudioProcessorEditor(SynthAudioProcessor& processor);
    ~SynthAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SynthAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessorEditor)
};
```

### 6.9 `src/plugin/SynthAudioProcessorEditor.cpp`

```cpp
#include "SynthAudioProcessorEditor.h"

#include "SynthAudioProcessor.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
{
    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Phase 1 build skeleton", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    setSize(900, 600);
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.drawRect(getLocalBounds(), 1);
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(48));
    statusLabel.setBounds(area.removeFromTop(32));
}
```

### 6.10 README Bootstrap Section

````md
## Build

### Prerequisites

- Windows 11
- Git
- CMake 3.22 or newer
- Visual Studio 2022 Build Tools or Visual Studio 2022

### Bootstrap

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

### Expected Artifacts

- `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe`
- `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3`

### Current Status

Phase 1 provides a reproducible JUCE build skeleton only. The standalone app should open a placeholder window, but audio-device, MIDI-device, and synth behavior arrive in later phases.
````

## 7. Exit Condition

This phase is ready for implementation review once the implementation work can be executed exactly as described above without adding extra scope. Do not begin coding beyond this file until the blueprint is reviewed and `TODO.md` has been refreshed with the six Phase 1 checklist items.
