#pragma once

#include <array>
#include <span>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/Minilab3Profile.h"
#include "midi/MidiLearn.h"

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

        void setLearnedBindings(std::span<const LearnedCcBinding> bindings);
        bool clearLearnedBinding(juce::StringRef parameterId);
        const LearnedCcBinding* findLearnedBinding(juce::StringRef parameterId) const noexcept;

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        ParameterTarget resolveParameterTarget(juce::StringRef parameterId) const noexcept;
        bool isFixedBindingShadowedByLearnedTarget(const Minilab3Binding& binding) const noexcept;
        bool isFixedBindingShadowedByLearnedSignature(uint8_t expectedMidiType,
                                                      uint8_t channel,
                                                      uint8_t primaryData) const noexcept;

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

    struct LearnedBindingWithTarget
    {
        LearnedCcBinding binding;
        ParameterTarget target;
        mutable TakeoverState state = TakeoverState::waitingForFirstTouch;
        mutable float initialHardwareValue = 0.0f;
        mutable float initialSoftwareValue = 0.0f;
    };

        std::array<BindingWithTarget, 13> activeBindings {};
        std::vector<LearnedBindingWithTarget> learnedBindings;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingEngine)
    };
}
