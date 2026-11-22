cmake_minimum_required(VERSION 3.22)

project(CoolSynth VERSION 0.5.0 LANGUAGES C CXX) # C as well: JUCE 9 builds zlib, libpng, libjpeg and libflac as C

include(cmake/CoolSynthFlags.cmake)

add_subdirectory(external/JUCE)
# CLAP beside the VST3 (D216): free-audio/clap-juce-extensions, pinned in external/.
add_subdirectory(external/clap-juce-extensions EXCLUDE_FROM_ALL)

file(GLOB COOLSYNTH_BANK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/presets/*.csbank)
juce_add_binary_data(CoolSynthFactoryBank
    HEADER_NAME FactoryBankData.h
    NAMESPACE FactoryBankData
    SOURCES ${COOLSYNTH_BANK_FILES})

juce_add_binary_data(CoolSynthImages
    SOURCES
        resources/icon/coolsynth-icon-512.png) # the icon beside the version in Preferences

include(CTest)
# The engine (D265): its sources, its data and its own test program.
include(cmake/CoolSynthEngine.cmake)

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
    # Two knobs over a little keyboard (D209): the app's icon, and the VST3 bundle's in Explorer.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/coolsynth-icon-512.png"
    COPY_PLUGIN_AFTER_BUILD ${COOLSYNTH_ENABLE_VST3_USER_INSTALL}
    VST3_COPY_DIR "${COOLSYNTH_VST3_USER_DIR}")

# The CLAP version (D216): its remote control pages let a controller's knobs follow the panel's page in Bitwig,
# Reaper, FL Studio and Studio One. Stepped parameters keep their steps (DISCRETE).
clap_juce_extensions_plugin(TARGET CoolSynth
    CLAP_ID "com.tomcoolpxl.coolsynth"
    CLAP_FEATURES instrument synthesizer stereo
    CLAP_MANUAL_URL "https://github.com/tomcoolpxl/CoolSynth#readme"
    CLAP_SUPPORT_URL "https://github.com/tomcoolpxl/CoolSynth/issues"
    CLAP_USE_JUCE_PARAMETER_RANGES DISCRETE)

add_custom_target(UpdateBuildInfo ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildInfo.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/GenerateBuildInfo.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/GeneratedBuildInfo.h
)

# MIDI, preset, UI and standalone sources shared by the plugin, the test runner and the tools; the engine's come
# with coolsynth_engine. Kept in one list so the targets cannot drift apart.
set(COOLSYNTH_SHARED_SOURCES
    src/BuildInfo.cpp
    src/presets/BankFile.cpp
    src/presets/FactoryPresets.cpp
    src/presets/PatchState.cpp
    src/presets/MySounds.cpp
    src/presets/StateMigration.cpp
    src/midi/ControllerMap.cpp
    src/midi/ControllerSetup.cpp
    src/parameters/ParameterLayout.cpp
    src/parameters/v3/SlotParameters.cpp
    src/parameters/v3/PartParameters.cpp
    src/interface/InterfaceModel.cpp
    src/interface/ControllerInput.cpp
    src/plugin/AuditionEditor.cpp
    src/plugin/PanelEditor.cpp
    src/plugin/RemoteControlPages.cpp
    src/plugin/SynthAudioProcessor.cpp
    src/standalone/CoolSynthStandaloneApp.cpp
    src/standalone/SettingsStore.cpp
    src/standalone/StandaloneAudioSupport.cpp
    src/interface/MiniLab3Driver.cpp
    src/mcp/McpServer.cpp
    src/mcp/CoolSynthTools.cpp)

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
        CoolSynthImages
        clap_juce_extensions
        CoolSynthFactoryBank
        coolsynth_engine
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
    juce_add_console_app(CoolSynthMidiLearnTests)

    target_sources(CoolSynthMidiLearnTests
        PRIVATE
            tests/MidiLearnTests.cpp
            tests/ControllerTests.cpp
            tests/AudioThreadAlarm.cpp
            tests/AudioScenarioTests.cpp
            tests/ClickDetectorTests.cpp
            tests/V3EarlierSoundTests.cpp
            tests/InterfaceModelTests.cpp
            tests/PatchStateTests.cpp
            tests/StabilityAndDisconnectTests.cpp
            tests/V3ParameterTests.cpp
            tests/V3LfoTests.cpp
            tests/V3MatrixTests.cpp
            tests/V3GlobalModulatorTests.cpp
            tests/V3DelayTests.cpp
            tests/V3WavetableTests.cpp
            tests/V3SvfTests.cpp
            tests/V3ReverbTests.cpp
            tests/V3LoFiTests.cpp
            tests/V3ModernFxTests.cpp
            tests/V3ChordTests.cpp
            tests/V3PartsTests.cpp
            tests/V3InsertTests.cpp
            tests/V3RoomTests.cpp
            tests/V3PitchTests.cpp
            tests/V3StutterTests.cpp
            tests/DeterminismTests.cpp
            tests/V3VoiceCapTests.cpp
            tests/McpServerTests.cpp
            tests/V3InstrumentTests.cpp
            tests/V3PressureTests.cpp
            tests/V3UserSoundTests.cpp
            tests/V3SliceTests.cpp
            tests/V3BankTests.cpp
            tests/HostedPluginTests.cpp
            tests/ParameterRoundTripTests.cpp
            ${COOLSYNTH_SHARED_SOURCES})

    target_include_directories(CoolSynthMidiLearnTests
        PRIVATE
            src
            ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_compile_features(CoolSynthMidiLearnTests PUBLIC cxx_std_20)

    target_compile_definitions(CoolSynthMidiLearnTests
        PRIVATE
            COOLSYNTH_BUILD_VERSION="${PROJECT_VERSION}"
            COOLSYNTH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            # The test program hosts the built VST3 as a DAW does (D228, tests/HostedPluginTests.cpp).
            JUCE_PLUGINHOST_VST3=1
            COOLSYNTH_VST3_BUNDLE="${CMAKE_BINARY_DIR}/CoolSynth_artefacts/$<CONFIG>/VST3/CoolSynth.vst3")

    add_dependencies(CoolSynthMidiLearnTests UpdateBuildInfo)
    if(TARGET CoolSynth_VST3)
        add_dependencies(CoolSynthMidiLearnTests CoolSynth_VST3) # the plugin it hosts is today's
    endif()

    target_link_libraries(CoolSynthMidiLearnTests
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
            juce::juce_dsp
            CoolSynthImages
            clap_juce_extensions
            CoolSynthFactoryBank
            coolsynth_engine
            coolsynth_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME CoolSynthMidiLearnTests COMMAND CoolSynthMidiLearnTests)
endif()

# Offline preset renderer (a preset playing given notes, to a WAV file). Not part of ALL:
#   cmake --build <build-dir> --config <Config> --target CoolSynthRender
juce_add_console_app(CoolSynthRender)
set_target_properties(CoolSynthRender PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_sources(CoolSynthRender
    PRIVATE
        tools/RenderPreset.cpp
        ${COOLSYNTH_SHARED_SOURCES})

target_include_directories(CoolSynthRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(CoolSynthRender PUBLIC cxx_std_20)

target_compile_definitions(CoolSynthRender
    PRIVATE
        COOLSYNTH_BUILD_VERSION="${PROJECT_VERSION}")

add_dependencies(CoolSynthRender UpdateBuildInfo)

target_link_libraries(CoolSynthRender
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_dsp
        CoolSynthImages
        clap_juce_extensions
        CoolSynthFactoryBank
        coolsynth_engine
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

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
        CoolSynthImages
        clap_juce_extensions
        CoolSynthFactoryBank
        coolsynth_engine
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)

# Packs the sampled-instrument library once (decision D163). Not part of ALL: build it on demand with
#   cmake --build <build-dir> --config <Config> --target CoolSynthPackLibrary
juce_add_console_app(CoolSynthPackLibrary)
set_target_properties(CoolSynthPackLibrary PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(CoolSynthPackLibrary PRIVATE tools/PackLibrary.cpp ${COOLSYNTH_SHARED_SOURCES})
target_include_directories(CoolSynthPackLibrary PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(CoolSynthPackLibrary PUBLIC cxx_std_20)
target_compile_definitions(CoolSynthPackLibrary PRIVATE COOLSYNTH_BUILD_VERSION="${PROJECT_VERSION}")
add_dependencies(CoolSynthPackLibrary UpdateBuildInfo)
target_link_libraries(CoolSynthPackLibrary
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_dsp
        CoolSynthImages
        clap_juce_extensions
        CoolSynthFactoryBank
        coolsynth_engine
        coolsynth_config_flags
        juce::juce_recommended_warning_flags)
