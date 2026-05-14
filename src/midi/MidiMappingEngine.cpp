#include "MidiMappingEngine.h"

#include <algorithm>
#include <cmath>

namespace coolsynth::midi
{
    namespace
    {
        bool signatureMatches(const ControllerMessageSignature& signature,
                              const ControllerMidiEvent& event) noexcept
        {
            ControllerMessageKind eventKind = ControllerMessageKind::other;

            switch (event.type)
            {
                case ControllerMidiEventType::noteOn:        eventKind = ControllerMessageKind::noteOn; break;
                case ControllerMidiEventType::noteOff:       eventKind = ControllerMessageKind::noteOff; break;
                case ControllerMidiEventType::controlChange: eventKind = ControllerMessageKind::controlChange; break;
                case ControllerMidiEventType::other:
                default: break;
            }

            if (signature.kind != eventKind)
                return false;

            if (signature.data1 != event.data1)
                return false;

            return signature.channel == 0 || signature.channel == event.channel;
        }

        template <typename RuntimeBinding>
        float applySoftTakeover(float incoming,
                                RuntimeBinding& binding,
                                const juce::RangedAudioParameter& parameter) noexcept
        {
            float finalValue = incoming;

            if (binding.state == MidiMappingEngine::TakeoverState::waitingForFirstTouch)
            {
                binding.initialHardwareValue = incoming;
                binding.initialSoftwareValue = parameter.getValue();

                if (std::abs(incoming - binding.initialSoftwareValue) < 0.05f)
                    binding.state = MidiMappingEngine::TakeoverState::latched;
                else
                    binding.state = MidiMappingEngine::TakeoverState::scaling;
            }

            if (binding.state == MidiMappingEngine::TakeoverState::scaling)
            {
                if (incoming >= 0.99f || incoming <= 0.01f || std::abs(incoming - binding.initialSoftwareValue) < 0.05f)
                {
                    binding.state = MidiMappingEngine::TakeoverState::latched;
                    finalValue = incoming;
                }
                else if (incoming > binding.initialHardwareValue)
                {
                    const float hardwareTravel = (incoming - binding.initialHardwareValue)
                                               / (1.0f - binding.initialHardwareValue);
                    finalValue = binding.initialSoftwareValue
                               + hardwareTravel * (1.0f - binding.initialSoftwareValue);
                }
                else if (incoming < binding.initialHardwareValue)
                {
                    const float hardwareTravel = (binding.initialHardwareValue - incoming)
                                               / binding.initialHardwareValue;
                    finalValue = binding.initialSoftwareValue
                               - hardwareTravel * binding.initialSoftwareValue;
                }
                else
                {
                    finalValue = binding.initialSoftwareValue;
                }
            }

            return finalValue;
        }

        std::optional<float> mapBindingValue(const ControllerBinding& binding,
                                             const juce::RangedAudioParameter& parameter,
                                             const ControllerMidiEvent& event) noexcept
        {
            switch (binding.valueMode)
            {
                case ControllerValueMode::absolute7:
                    return static_cast<float>(event.data2) / 127.0f;

                case ControllerValueMode::threeStepAbsolute:
                    if (event.data2 <= 42) return 0.0f;
                    if (event.data2 <= 85) return 0.5f;
                    return 1.0f;

                case ControllerValueMode::relativeBinaryOffset:
                {
                    if (event.data2 == 64)
                        return std::nullopt;

                    const int steps = juce::jmax(2, parameter.getNumSteps());
                    const float stepSize = 1.0f / static_cast<float>(steps - 1);
                    const float delta = event.data2 > 64 ? stepSize : -stepSize;
                    const float currentValue = parameter.getValue();
                    const float nextValue = juce::jlimit(0.0f, 1.0f, currentValue + delta);

                    if (std::abs(nextValue - currentValue) < 1.0e-6f)
                        return std::nullopt;

                    return nextValue;
                }

                case ControllerValueMode::noteGate:
                default:
                    return std::nullopt;
            }
        }
    }

    MidiMappingEngine::MidiMappingEngine(juce::AudioProcessorValueTreeState& state)
        : parameterState(state)
    {
    }

    bool MidiMappingEngine::setActiveProfile(juce::StringRef profileId)
    {
        if (profileId.isEmpty())
        {
            activeProfile = nullptr;
            rebuildActiveBindings();
            return true;
        }

        if (const auto* profile = ControllerProfileRegistry::get().findProfileById(profileId))
        {
            activeProfile = profile;
            rebuildActiveBindings();
            return true;
        }

        return false;
    }

    juce::String MidiMappingEngine::getActiveProfileId() const
    {
        return activeProfile != nullptr ? activeProfile->profileId : juce::String();
    }

    juce::String MidiMappingEngine::getActiveProfileDisplayName() const
    {
        return activeProfile != nullptr ? activeProfile->displayName : juce::String();
    }

    ParameterTarget MidiMappingEngine::resolveParameterTarget(juce::StringRef parameterId) const noexcept
    {
        return { parameterState.getParameter(parameterId) };
    }

    void MidiMappingEngine::rebuildActiveBindings()
    {
        activeBindings.clear();

        if (activeProfile == nullptr)
            return;

        activeBindings.reserve(activeProfile->bindings.size());

        for (const auto& binding : activeProfile->bindings)
        {
            if (!binding.enabled || !binding.isValid())
                continue;

            ParameterTarget target;
            if (binding.target.kind == ControllerTargetKind::parameter)
            {
                target = resolveParameterTarget(binding.target.parameterId);
                if (target.parameter == nullptr)
                    continue;
            }

            activeBindings.push_back({ binding, target });
        }
    }

    void MidiMappingEngine::setLearnedBindings(std::span<const LearnedCcBinding> newBindings)
    {
        learnedBindings.clear();
        learnedBindings.reserve(newBindings.size());

        for (const auto& binding : newBindings)
        {
            if (!binding.isValid())
                continue;

            auto target = resolveParameterTarget(binding.parameterId);
            if (target.parameter == nullptr)
                continue;

            learnedBindings.push_back({ binding, target, TakeoverState::waitingForFirstTouch, 0.0f, 0.0f });
        }
    }

    bool MidiMappingEngine::clearLearnedBinding(juce::StringRef parameterId)
    {
        const auto it = std::remove_if(learnedBindings.begin(),
                                       learnedBindings.end(),
                                       [&](const auto& binding)
                                       {
                                           return binding.binding.parameterId == parameterId;
                                       });

        if (it == learnedBindings.end())
            return false;

        learnedBindings.erase(it, learnedBindings.end());
        return true;
    }

    const LearnedCcBinding* MidiMappingEngine::findLearnedBinding(juce::StringRef parameterId) const noexcept
    {
        for (const auto& binding : learnedBindings)
            if (binding.binding.parameterId == parameterId)
                return &binding.binding;

        return nullptr;
    }

    bool MidiMappingEngine::isFactoryBindingShadowedByLearnedTarget(const ControllerBinding& binding) const noexcept
    {
        if (binding.target.kind != ControllerTargetKind::parameter)
            return false;

        return findLearnedBinding(binding.target.parameterId) != nullptr;
    }

    bool MidiMappingEngine::isFactoryBindingShadowedByLearnedSignature(const ControllerMessageSignature& signature) const noexcept
    {
        if (signature.kind != ControllerMessageKind::controlChange)
            return false;

        for (const auto& binding : learnedBindings)
        {
            if (binding.binding.cc.controllerNumber != signature.data1)
                continue;

            if (signature.channel == 0 || binding.binding.cc.channel == signature.channel)
                return true;
        }

        return false;
    }

    float MidiMappingEngine::mapAbsoluteControllerValue(uint8_t midiValue) noexcept
    {
        return static_cast<float>(midiValue) / 127.0f;
    }

    float MidiMappingEngine::mapThreeStepValue(uint8_t midiValue) noexcept
    {
        if (midiValue <= 42) return 0.0f;
        if (midiValue <= 85) return 0.5f;
        return 1.0f;
    }

    bool MidiMappingEngine::shouldUseSoftTakeover(const juce::RangedAudioParameter& parameter,
                                                  ControllerValueMode valueMode) noexcept
    {
        return valueMode == ControllerValueMode::absolute7 && parameter.getNumSteps() > 3;
    }

    MappedAction MidiMappingEngine::translate(const ControllerMidiEvent& event) const noexcept
    {
        if (event.type == ControllerMidiEventType::controlChange
            && (event.data1 == 1 || event.data1 == 64))
        {
            return {};
        }

        if (event.type == ControllerMidiEventType::controlChange)
        {
            for (const auto& binding : learnedBindings)
            {
                if (binding.binding.cc.channel != event.channel
                    || binding.binding.cc.controllerNumber != event.data1
                    || binding.target.parameter == nullptr)
                {
                    continue;
                }

                auto maybeValue = mapAbsoluteControllerValue(event.data2);
                float finalValue = maybeValue;

                if (shouldUseSoftTakeover(*binding.target.parameter, ControllerValueMode::absolute7))
                    finalValue = applySoftTakeover(maybeValue, binding, *binding.target.parameter);

                MappedAction action;
                action.kind = MappedAction::Kind::parameterChange;
                action.parameterChange.parameter = binding.target.parameter;
                action.parameterChange.normalizedValue = finalValue;
                return action;
            }
        }

        for (const auto& binding : activeBindings)
        {
            if (!signatureMatches(binding.binding.signature, event))
                continue;

            if (isFactoryBindingShadowedByLearnedTarget(binding.binding))
                continue;

            if (isFactoryBindingShadowedByLearnedSignature(binding.binding.signature))
                continue;

            if (binding.binding.target.kind == ControllerTargetKind::command)
            {
                if (binding.binding.target.command == ControllerCommandId::panic
                    && event.type == ControllerMidiEventType::noteOn
                    && event.data2 > 0)
                {
                    MappedAction action;
                    action.kind = MappedAction::Kind::command;
                    action.command = ControllerCommandId::panic;
                    return action;
                }

                continue;
            }

            if (binding.target.parameter == nullptr)
                continue;

            const auto maybeValue = mapBindingValue(binding.binding, *binding.target.parameter, event);
            if (!maybeValue.has_value())
                continue;

            float finalValue = *maybeValue;
            if (shouldUseSoftTakeover(*binding.target.parameter, binding.binding.valueMode))
                finalValue = applySoftTakeover(*maybeValue, binding, *binding.target.parameter);

            MappedAction action;
            action.kind = MappedAction::Kind::parameterChange;
            action.parameterChange.parameter = binding.target.parameter;
            action.parameterChange.normalizedValue = finalValue;
            return action;
        }

        return {};
    }
}
