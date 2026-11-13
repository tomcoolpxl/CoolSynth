cmake_minimum_required(VERSION 3.22)

project(CoolSynth VERSION 0.1.0 LANGUAGES CXX)

add_subdirectory(external/JUCE)

# Debug is the everyday build, and the plugin it installs must play in real time: compile it
# optimised, with debug info and JUCE's assertions still on. CMake's /RTC1 cannot be combined
# with optimisation, and JUCE's recommended config flags force /Od in Debug, so CoolSynth targets
# use this flags target instead of the juce_recommended_config_flags target.
add_library(coolsynth_config_flags INTERFACE)
if(MSVC)
    foreach(flagVariable CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
        string(REPLACE "/RTC1" "" ${flagVariable} "${${flagVariable}}")
        string(REPLACE "/Ob0" "" ${flagVariable} "${${flagVariable}}")
        string(REPLACE "/Od" "" ${flagVariable} "${${flagVariable}}")
        # Debug info comes from coolsynth_config_flags on the targets that have code; the data-only
        # libraries (the embedded bank, the controller profiles) need none.
        string(REPLACE "/Zi" "" ${flagVariable} "${${flagVariable}}")
    endforeach()
    # No incremental linking: when a new bank file added a member to the embedded bank library,
    # the stale incremental-link databases made the linker reject the library (LNK1236), and an
    # optimised Debug build gains little from linking incrementally.
    foreach(linkerVariable CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_DEBUG CMAKE_MODULE_LINKER_FLAGS_DEBUG)
        string(REPLACE "/INCREMENTAL:NO" "" ${linkerVariable} "${${linkerVariable}}")
        string(REPLACE "/INCREMENTAL" "" ${linkerVariable} "${${linkerVariable}}")
        string(APPEND ${linkerVariable} " /INCREMENTAL:NO")
    endforeach()
    target_compile_options(coolsynth_config_flags INTERFACE
        $<IF:$<CONFIG:Debug>,/O2 /Ob2 /Zi,/Ox> /MP /EHsc)
else()
    target_compile_options(coolsynth_config_flags INTERFACE
        $<IF:$<CONFIG:Debug>,-O2 -g,-O3>)
endif()

file(GLOB COOLSYNTH_BANK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/presets/*.csbank)
juce_add_binary_data(CoolSynthFactoryBank
    HEADER_NAME FactoryBankData.h
    NAMESPACE FactoryBankData
    SOURCES ${COOLSYNTH_BANK_FILES})

juce_add_binary_data(CoolSynthControllerProfiles
    SOURCES
        resources/controller_profiles/minilab3_arturia_mode.json
        resources/ui/coolsynth-logo-2.png)

option(COOLSYNTH_ENABLE_VST3_USER_INSTALL
    "Copy CoolSynth.vst3 to the per-user Windows VST3 folder after build"
    OFF)

if (WIN32)
    file(TO_CMAKE_PATH
        "$ENV{LOCALAPPDATA}/Programs/Common/VST3"
        COOLSYNTH_VST3_USER_DIR)
endif()

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
    COPY_PLUGIN_AFTER_BUILD ${COOLSYNTH_ENABLE_VST3_USER_INSTALL}
    VST3_COPY_DIR "${COOLSYNTH_VST3_USER_DIR}")

add_custom_target(UpdateBuildInfo ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildInfo.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/GenerateBuildInfo.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildInfo.h
)

# Engine, MIDI, preset, UI and standalone sources shared by the plugin, the test
# runner and the preset normalizer. Kept in one list so the three targets cannot
# drift apart.
set(COOLSYNTH_SHARED_SOURCES
    src/BuildInfo.cpp
    src/presets/BankFile.cpp
    src/presets/FactoryPresets.cpp
    src/presets/PatchState.cpp
    src/midi/ControllerProfile.cpp
    src/midi/MidiLearn.cpp
    src/midi/MidiMappingEngine.cpp
    src/midi/MidiMonitor.cpp
    src/parameters/ParameterLayout.cpp
    src/parameters/v3/SlotParameters.cpp
    src/parameters/v3/PartParameters.cpp
    src/synth/v3/Voice.cpp
    src/synth/v3/Part.cpp
    src/synth/v3/Wavetables.cpp
    src/synth/v3/EngineV3.cpp
    src/plugin/AuditionEditor.cpp
    src/plugin/SynthAudioProcessor.cpp
    src/standalone/CoolSynthStandaloneApp.cpp
    src/standalone/SettingsStore.cpp
    src/standalone/StandaloneAudioSupport.cpp
    src/standalone/StandaloneMidiInput.cpp
    src/synth/Arpeggiator.cpp
    src/synth/GlobalDelay.cpp
    src/synth/GlobalFxRack.cpp
    src/synth/Phaser.cpp
    src/synth/Compressor.cpp
    src/ui/HardwareFader.cpp
    src/ui/HardwareKnob.cpp
    src/ui/LedToggleButton.cpp
    src/ui/MidiMonitorPanel.cpp
    src/ui/PianoBarComponent.cpp
    src/ui/SegmentedChoiceGroup.cpp
    src/ui/StandaloneMidiInputPanel.cpp
    src/ui/StandaloneStatusBar.cpp
    src/ui/SynthSection.cpp)

target_sources(CoolSynth PRIVATE ${COOLSYNTH_SHARED_SOURCES})

target_include_directories(CoolSynth
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(CoolSynth PUBLIC cxx_std_20)

target_compile_definitions(CoolSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # No VST2 build has ever shipped, so the VST3 does not need to masquerade as
        # one. This is JUCE's recommended setting for a VST3-only plugin and makes the
        # VST2/VST3 parameter-ID conflict check pass without suppressing it.
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

add_dependencies(CoolSynth UpdateBuildInfo)

target_link_libraries(CoolSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        CoolSynthControllerProfiles
        CoolSynthFactoryBank
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(CoolSynthMidiLearnTests)

    target_sources(CoolSynthMidiLearnTests
        PRIVATE
            tests/MidiLearnTests.cpp
            tests/PatchStateTests.cpp
            tests/StabilityAndDisconnectTests.cpp
            tests/V3ModuleTests.cpp
            tests/V3ModulationTests.cpp
            tests/V3ParameterTests.cpp
            tests/V3GeneratorTests.cpp
            tests/V3ProcessorTests.cpp
            tests/V3LfoTests.cpp
            tests/V3MatrixTests.cpp
            tests/V3GlobalModulatorTests.cpp
            tests/V3DelayTests.cpp
            tests/V3NoiseTests.cpp
            tests/V3FmTests.cpp
            tests/V3WavetableTests.cpp
            tests/V3SvfTests.cpp
            tests/V3ShaperTests.cpp
            tests/V3ResonatorTests.cpp
            tests/V3ReverbTests.cpp
            tests/V3LoFiTests.cpp
            tests/V3ModernFxTests.cpp
            tests/V3VoiceTests.cpp
            tests/V3PartTests.cpp
            tests/V3BankTests.cpp
            ${COOLSYNTH_SHARED_SOURCES})

    target_include_directories(CoolSynthMidiLearnTests
        PRIVATE
            src
            ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_compile_features(CoolSynthMidiLearnTests PUBLIC cxx_std_20)

    target_compile_definitions(CoolSynthMidiLearnTests
        PRIVATE
            COOLSYNTH_BUILD_VERSION="${PROJECT_VERSION}")

    add_dependencies(CoolSynthMidiLearnTests UpdateBuildInfo)

    target_link_libraries(CoolSynthMidiLearnTests
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
            juce::juce_dsp
            CoolSynthControllerProfiles
            CoolSynthFactoryBank
            coolsynth_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME CoolSynthMidiLearnTests COMMAND CoolSynthMidiLearnTests)
endif()

# Offline preset-loudness tool. Not part of ALL: build it on demand with
#   cmake --build <build-dir> --config <Config> --target CoolSynthPresetNormalizer
juce_add_console_app(CoolSynthPresetNormalizer)
set_target_properties(CoolSynthPresetNormalizer PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_sources(CoolSynthPresetNormalizer
    PRIVATE
        tools/NormalizePresets.cpp
        ${COOLSYNTH_SHARED_SOURCES})

target_include_directories(CoolSynthPresetNormalizer
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(CoolSynthPresetNormalizer PUBLIC cxx_std_20)

target_compile_definitions(CoolSynthPresetNormalizer
    PRIVATE
        COOLSYNTH_BUILD_VERSION="${PROJECT_VERSION}")

add_dependencies(CoolSynthPresetNormalizer UpdateBuildInfo)

target_link_libraries(CoolSynthPresetNormalizer
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_dsp
        CoolSynthControllerProfiles
        CoolSynthFactoryBank
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)
