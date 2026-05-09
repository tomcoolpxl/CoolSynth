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
        src/midi/MidiMappingEngine.cpp
        src/midi/MidiMonitor.cpp
        src/midi/Minilab3Profile.cpp
        src/parameters/ParameterLayout.cpp
        src/plugin/SynthAudioProcessor.cpp
        src/plugin/SynthAudioProcessorEditor.cpp
        src/standalone/CoolSynthStandaloneApp.cpp
        src/standalone/StandaloneAudioSupport.cpp
        src/standalone/StandaloneMidiInput.cpp
        src/synth/SynthEngine.cpp
        src/synth/SynthVoice.cpp
        src/ui/HardwareFader.cpp
        src/ui/HardwareKnob.cpp
        src/ui/MidiMonitorPanel.cpp
        src/ui/StandaloneAudioStatusPanel.cpp
        src/ui/StandaloneMidiInputPanel.cpp
        src/ui/SynthSection.cpp)

target_include_directories(CoolSynth
    PRIVATE
        src)

target_compile_features(CoolSynth PUBLIC cxx_std_20)

target_compile_definitions(CoolSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(CoolSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
