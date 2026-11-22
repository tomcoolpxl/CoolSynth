# The CoolSynth engine (D265): the synth, its circuit models and the flight recorder, shared by CoolSynth and
# CoolSynth 808. It is an INTERFACE target that hands its sources to each program linking it: JUCE's modules are
# compiled once per program, and a compiled library linking them as well would build them twice (the JUCE forum's
# advice). Each program builds the engine with its own flags, as before the split.
#
# Expects JUCE's CMake functions to be loaded (add_subdirectory of JUCE) before it is included.

set(COOLSYNTH_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH COOLSYNTH_ENGINE_ROOT)

# The sampled instruments' catalogue, read by synth/v3/InstrumentCatalogue.cpp.
juce_add_binary_data(CoolSynthInstrumentData
    HEADER_NAME InstrumentData.h
    NAMESPACE InstrumentData
    SOURCES "${COOLSYNTH_ENGINE_ROOT}/resources/instruments/catalogue.txt")

add_library(coolsynth_engine INTERFACE)

target_sources(coolsynth_engine
    INTERFACE
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/Voice.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/Part.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/Wavetables.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/ChipWaves.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/Samples.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/InstrumentCatalogue.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/SampleLibrary.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/UserLibrary.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/EngineV3.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/SoundUnit.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/SampleMemory.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/SamplePack.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/v3/AudioWorkers.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/Arpeggiator.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/GlobalDelay.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/GlobalFxRack.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/Phaser.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/synth/Compressor.cpp"
        "${COOLSYNTH_ENGINE_ROOT}/src/diagnostics/FlightRecorder.cpp")

target_include_directories(coolsynth_engine INTERFACE "${COOLSYNTH_ENGINE_ROOT}/src")
target_compile_features(coolsynth_engine INTERFACE cxx_std_20)

target_link_libraries(coolsynth_engine
    INTERFACE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        CoolSynthInstrumentData)

# The engine's own tests: the ones that need nothing but the engine. A program of their own, so a change to the
# engine is checked without the app; the operator new that counts audio-thread allocations comes with it.
option(COOLSYNTH_ENGINE_TESTS "Build the engine's own test program" ON)
if(BUILD_TESTING AND COOLSYNTH_ENGINE_TESTS)
    juce_add_console_app(CoolSynthEngineTests)
    target_sources(CoolSynthEngineTests
        PRIVATE
            "${COOLSYNTH_ENGINE_ROOT}/tests/EngineTestMain.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/AudioThreadAlarm.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ChipTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3FmTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3GeneratorTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3KickTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3LiveGrainTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ModalTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ModulationTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ModuleCostReport.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ModuleTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3NoiseTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3PartTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ProcessorTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ResonatorTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3SampleTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3ShaperTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3StringTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tb303Tests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tr808DrumsTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tr808MetalTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tr808SnareClapTests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tr808Tests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3Tr909Tests.cpp"
            "${COOLSYNTH_ENGINE_ROOT}/tests/V3VoiceTests.cpp")
    target_include_directories(CoolSynthEngineTests PRIVATE "${COOLSYNTH_ENGINE_ROOT}/tests")
    target_link_libraries(CoolSynthEngineTests
        PRIVATE
            coolsynth_engine
            coolsynth_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME CoolSynthEngineTests COMMAND CoolSynthEngineTests)
endif()
