#pragma once

#include <array>
#include <span>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/Minilab3Profile.h"

namespace coolsynth::midi
{
    enum class ControllerMidiEventType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        other,
    };

    struct ControllerMidiEvent
    {
        ControllerMidiEventType type = ControllerMidiEventType::other;
        uint8_t channel = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
    };

    enum class MappingCurve : uint8_t
    {
        linearNormalized,
        waveformChoice3Step,
    };

    enum class MappedCommand : uint8_t
    {
        none,
        panic,
    };

    struct ParameterTarget
    {
        juce::RangedAudioParameter* parameter = nullptr;
        MappingCurve curve = MappingCurve::linearNormalized;
    };

    struct MappedParameterChange
    {
        juce::RangedAudioParameter* parameter = nullptr;
        float normalizedValue = 0.0f;
    };

    struct MappedAction
    {
        enum class Kind : uint8_t
        {
            none,
            parameterChange,
            command,
        };

        Kind kind = Kind::none;
        MappedParameterChange parameterChange {};
        MappedCommand command = MappedCommand::none;
    };

    class MidiMappingEngine
    {
    public:
        explicit MidiMappingEngine(juce::AudioProcessorValueTreeState& state);

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        static float mapControllerValue(uint8_t midiValue, const ParameterTarget& target) noexcept;
        static float mapWaveformChoice(uint8_t midiValue) noexcept;

        std::span<const Minilab3Binding> bindings;
        
    enum class TakeoverState : uint8_t
    {
        waitingForFirstTouch,
        scaling,
        latched
    };

    struct BindingWithTarget
    {
        Minilab3Binding binding;
        ParameterTarget target;
        mutable TakeoverState state = TakeoverState::waitingForFirstTouch;
        mutable float initialHardwareValue = 0.0f;
        mutable float initialSoftwareValue = 0.0f;
    };

        std::array<BindingWithTarget, 13> activeBindings {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingEngine)
    };
}
