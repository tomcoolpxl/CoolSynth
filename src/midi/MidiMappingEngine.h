#pragma once

#include <span>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/ControllerProfile.h"
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

    constexpr bool isReservedSynthControllerNumber(uint8_t controllerNumber) noexcept
    {
        switch (controllerNumber)
        {
            case 1:   // mod wheel
            case 64:  // sustain
            case 120: // all sound off
            case 121: // reset all controllers
            case 123: // all notes off
                return true;

            default:
                return false;
        }
    }

    constexpr bool isReservedSynthControllerEvent(const ControllerMidiEvent& event) noexcept
    {
        return event.type == ControllerMidiEventType::controlChange
            && isReservedSynthControllerNumber(event.data1);
    }

    using MappedCommand = ControllerCommandId;

    struct ParameterTarget
    {
        juce::RangedAudioParameter* parameter = nullptr;
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
        enum class TakeoverState : uint8_t
        {
            waitingForFirstTouch,
            scaling,
            latched
        };

        explicit MidiMappingEngine(juce::AudioProcessorValueTreeState& state);

        bool setActiveProfile(juce::StringRef profileId);
        juce::String getActiveProfileId() const;
        juce::String getActiveProfileDisplayName() const;

        void setLearnedBindings(std::span<const LearnedCcBinding> bindings);
        bool clearLearnedBinding(juce::StringRef parameterId);
        const LearnedCcBinding* findLearnedBinding(juce::StringRef parameterId) const noexcept;

        MappedAction translate(const ControllerMidiEvent& event) const noexcept;

    private:
        ParameterTarget resolveParameterTarget(juce::StringRef parameterId) const noexcept;
        void rebuildActiveBindings();
        bool isFactoryBindingShadowedByLearnedTarget(const ControllerBinding& binding) const noexcept;
        bool isFactoryBindingShadowedByLearnedSignature(const ControllerMessageSignature& signature) const noexcept;

        static float mapAbsoluteControllerValue(uint8_t midiValue) noexcept;
        static float mapThreeStepValue(uint8_t midiValue) noexcept;
        static bool shouldUseSoftTakeover(const juce::RangedAudioParameter& parameter,
                                          ControllerValueMode valueMode) noexcept;

        juce::AudioProcessorValueTreeState& parameterState;
        const ControllerProfile* activeProfile = nullptr;

        struct BindingWithTarget
        {
            ControllerBinding binding;
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

        std::vector<BindingWithTarget> activeBindings;
        std::vector<LearnedBindingWithTarget> learnedBindings;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingEngine)
    };
}
