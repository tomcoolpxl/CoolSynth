cmake_minimum_required(VERSION 3.22)

project(CoolSynth VERSION 0.1.0 LANGUAGES CXX)

add_subdirectory(external/JUCE)

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

target_sources(CoolSynth
    PRIVATE
        src/BuildInfo.cpp
        src/presets/FactoryPresets.cpp
        src/presets/PatchState.cpp
        src/midi/ControllerProfile.cpp
        src/midi/MidiLearn.cpp
        src/midi/MidiMappingEngine.cpp
        src/midi/MidiMonitor.cpp
        src/midi/Minilab3Profile.cpp
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/EditorTooltips.cpp
        src/plugin/SynthAudioProcessorEditor.cpp
        src/standalone/CoolSynthStandaloneApp.cpp
        src/standalone/SettingsStore.cpp
        src/standalone/StandaloneAudioSupport.cpp
        src/standalone/StandaloneMidiInput.cpp
        src/synth/Arpeggiator.cpp
        src/synth/SynthEngineV2.cpp
        src/synth/GlobalDelay.cpp
        src/synth/GlobalFxRack.cpp
        src/synth/SynthVoice.cpp

        src/ui/HardwareFader.cpp
        src/ui/HardwareKnob.cpp
        src/ui/LedToggleButton.cpp
        src/ui/MidiMonitorPanel.cpp
        src/ui/PianoBarComponent.cpp
        src/ui/SegmentedChoiceGroup.cpp
        src/ui/StandaloneMidiInputPanel.cpp
        src/ui/StandaloneSettingsDialog.cpp
        src/ui/StandaloneStatusBar.cpp
        src/ui/SynthSection.cpp
        src/ui/SignalChainVisualizer.cpp)

target_include_directories(CoolSynth
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(CoolSynth PUBLIC cxx_std_20)

target_compile_definitions(CoolSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

add_dependencies(CoolSynth UpdateBuildInfo)

target_link_libraries(CoolSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        CoolSynthControllerProfiles
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(CoolSynthMidiLearnTests)
    
    target_sources(CoolSynthMidiLearnTests
        PRIVATE
            tests/MidiLearnTests.cpp
            tests/PatchStateTests.cpp
            tests/StabilityAndDisconnectTests.cpp
            src/BuildInfo.cpp
            src/presets/FactoryPresets.cpp
            src/presets/PatchState.cpp
            src/midi/ControllerProfile.cpp
            src/plugin/SynthAudioProcessor.cpp
            src/plugin/EditorTooltips.cpp
            src/plugin/SynthAudioProcessorEditor.cpp
            src/midi/MidiLearn.cpp
            src/midi/MidiMappingEngine.cpp
            src/midi/MidiMonitor.cpp
            src/midi/Minilab3Profile.cpp
            src/standalone/SettingsStore.cpp
            src/standalone/StandaloneAudioSupport.cpp
            src/standalone/StandaloneMidiInput.cpp
            src/parameters/ParameterLayout.cpp
            src/synth/Arpeggiator.cpp
            src/synth/SynthEngineV2.cpp
            src/synth/GlobalDelay.cpp
            src/synth/GlobalFxRack.cpp
            src/synth/SynthVoice.cpp
            src/ui/HardwareFader.cpp
            src/ui/HardwareKnob.cpp
            src/ui/LedToggleButton.cpp
            src/ui/MidiMonitorPanel.cpp
            src/ui/PianoBarComponent.cpp
            src/ui/SegmentedChoiceGroup.cpp
            src/ui/StandaloneMidiInputPanel.cpp
            src/ui/StandaloneSettingsDialog.cpp
            src/ui/StandaloneStatusBar.cpp
            src/ui/SynthSection.cpp
            src/ui/SignalChainVisualizer.cpp
    )

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
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME CoolSynthMidiLearnTests COMMAND CoolSynthMidiLearnTests)
endif()

juce_add_console_app(CoolSynthPresetNormalizer)

target_sources(CoolSynthPresetNormalizer
    PRIVATE
        tools/NormalizePresets.cpp
        src/BuildInfo.cpp
        src/presets/FactoryPresets.cpp
        src/presets/PatchState.cpp
        src/midi/ControllerProfile.cpp
        src/midi/MidiLearn.cpp
        src/midi/MidiMappingEngine.cpp
        src/midi/MidiMonitor.cpp
        src/midi/Minilab3Profile.cpp
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/EditorTooltips.cpp
        src/plugin/SynthAudioProcessorEditor.cpp
        src/standalone/SettingsStore.cpp
        src/standalone/StandaloneAudioSupport.cpp
        src/standalone/StandaloneMidiInput.cpp
        src/synth/Arpeggiator.cpp
        src/synth/SynthEngineV2.cpp
        src/synth/GlobalDelay.cpp
        src/synth/GlobalFxRack.cpp
        src/synth/SynthVoice.cpp
        src/ui/HardwareFader.cpp
        src/ui/HardwareKnob.cpp
        src/ui/LedToggleButton.cpp
        src/ui/MidiMonitorPanel.cpp
        src/ui/PianoBarComponent.cpp
        src/ui/SegmentedChoiceGroup.cpp
        src/ui/StandaloneMidiInputPanel.cpp
        src/ui/StandaloneSettingsDialog.cpp
        src/ui/StandaloneStatusBar.cpp
        src/ui/SynthSection.cpp
        src/ui/SignalChainVisualizer.cpp)

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
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
