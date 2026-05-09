#include "MidiMappingEngine.h"
#include "parameters/ParameterIDs.h"
#include <algorithm>

namespace coolsynth::midi
{
    MidiMappingEngine::MidiMappingEngine(juce::AudioProcessorValueTreeState& state)
    {
        bindings = getPhase7Bindings();
        
        int bindingIndex = 0;
        for (const auto& b : bindings)
        {
            if (bindingIndex >= static_cast<int>(activeBindings.size()))
                break;

            activeBindings[bindingIndex].binding = b;
            
            if (b.target == Minilab3LogicalTarget::panic)
            {
                activeBindings[bindingIndex].target = { nullptr, MappingCurve::linearNormalized };
            }
            else
            {
                const char* paramId = nullptr;
                MappingCurve curve = MappingCurve::linearNormalized;

                switch (b.target)
                {
                    case Minilab3LogicalTarget::waveform:   paramId = parameters::ids::waveform; curve = MappingCurve::waveformChoice3Step; break;
                    case Minilab3LogicalTarget::filterCutoff: paramId = parameters::ids::filterCutoffHz; break;
                    case Minilab3LogicalTarget::filterResonance: paramId = parameters::ids::filterResonance; break;
                    case Minilab3LogicalTarget::ampAttack:  paramId = parameters::ids::ampAttackMs; break;
                    case Minilab3LogicalTarget::ampDecay:   paramId = parameters::ids::ampDecayMs; break;
                    case Minilab3LogicalTarget::ampSustain: paramId = parameters::ids::ampSustain; break;
                    case Minilab3LogicalTarget::ampRelease: paramId = parameters::ids::ampReleaseMs; break;
                    case Minilab3LogicalTarget::delayMix: paramId = parameters::ids::delayMix; break;
                    case Minilab3LogicalTarget::delayFeedback: paramId = parameters::ids::delayFeedback; break;
                    case Minilab3LogicalTarget::delayTime: paramId = parameters::ids::delayTimeMs; break;
                    case Minilab3LogicalTarget::masterGain: paramId = parameters::ids::masterGainDb; break;
                    default: break;
                }

                if (paramId != nullptr)
                {
                    activeBindings[bindingIndex].target = { state.getParameter(paramId), curve };
                }
            }
            
            bindingIndex++;
        }
    }

    ParameterTarget MidiMappingEngine::resolveParameterTarget(juce::StringRef parameterId) const noexcept
    {
        for (const auto& active : activeBindings)
        {
            if (active.target.parameter != nullptr && active.target.parameter->getParameterID() == parameterId)
            {
                return active.target;
            }
        }
        return { nullptr, MappingCurve::linearNormalized };
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
        auto it = std::remove_if(learnedBindings.begin(), learnedBindings.end(),
            [&](const auto& b) { return b.binding.parameterId == parameterId; });
        
        if (it != learnedBindings.end())
        {
            learnedBindings.erase(it, learnedBindings.end());
            return true;
        }
        return false;
    }

    const LearnedCcBinding* MidiMappingEngine::findLearnedBinding(juce::StringRef parameterId) const noexcept
    {
        for (const auto& b : learnedBindings)
        {
            if (b.binding.parameterId == parameterId)
                return &b.binding;
        }
        return nullptr;
    }

    bool MidiMappingEngine::isFixedBindingShadowedByLearnedTarget(const Minilab3Binding& binding) const noexcept
    {
        const char* paramId = nullptr;
        switch (binding.target)
        {
            case Minilab3LogicalTarget::filterCutoff: paramId = parameters::ids::filterCutoffHz; break;
            case Minilab3LogicalTarget::filterResonance: paramId = parameters::ids::filterResonance; break;
            case Minilab3LogicalTarget::ampAttack:  paramId = parameters::ids::ampAttackMs; break;
            case Minilab3LogicalTarget::ampDecay:   paramId = parameters::ids::ampDecayMs; break;
            case Minilab3LogicalTarget::ampSustain: paramId = parameters::ids::ampSustain; break;
            case Minilab3LogicalTarget::ampRelease: paramId = parameters::ids::ampReleaseMs; break;
            case Minilab3LogicalTarget::delayMix: paramId = parameters::ids::delayMix; break;
            case Minilab3LogicalTarget::delayFeedback: paramId = parameters::ids::delayFeedback; break;
            case Minilab3LogicalTarget::delayTime: paramId = parameters::ids::delayTimeMs; break;
            case Minilab3LogicalTarget::masterGain: paramId = parameters::ids::masterGainDb; break;
            default: return false;
        }

        if (paramId == nullptr)
            return false;

        return findLearnedBinding(paramId) != nullptr;
    }

    bool MidiMappingEngine::isFixedBindingShadowedByLearnedSignature(uint8_t expectedMidiType,
                                                                     uint8_t channel,
                                                                     uint8_t primaryData) const noexcept
    {
        // Learned bindings only support CC (type 1)
        if (expectedMidiType != 1)
            return false;

        for (const auto& b : learnedBindings)
        {
            // Channel 0 in fixed binding means Omni. If so, does it shadow?
            // "Learned bindings also shadow any fixed binding that uses the same channel + CC number signature."
            // If fixed is omni, we still shadow it for the specific channel.
            if ((channel == 0 || b.binding.cc.channel == channel) &&
                b.binding.cc.controllerNumber == primaryData)
            {
                return true;
            }
        }
        return false;
    }

    MappedAction MidiMappingEngine::translate(const ControllerMidiEvent& event) const noexcept
    {
        // 1. Check learned bindings first
        if (event.type == ControllerMidiEventType::controlChange)
        {
            for (const auto& b : learnedBindings)
            {
                if (b.binding.cc.channel == event.channel && b.binding.cc.controllerNumber == event.data1)
                {
                    if (b.target.parameter != nullptr)
                    {
                        float incoming = mapControllerValue(event.data2, b.target);
                        float finalValue = incoming;

                        if (b.state == TakeoverState::waitingForFirstTouch)
                        {
                            b.initialHardwareValue = incoming;
                            b.initialSoftwareValue = b.target.parameter->getValue();
                            
                            if (std::abs(incoming - b.initialSoftwareValue) < 0.05f)
                                b.state = TakeoverState::latched;
                            else
                                b.state = TakeoverState::scaling;
                        }

                        if (b.state == TakeoverState::scaling)
                        {
                            if (incoming >= 0.99f || incoming <= 0.01f || std::abs(incoming - b.initialSoftwareValue) < 0.05f)
                            {
                                b.state = TakeoverState::latched;
                                finalValue = incoming;
                            }
                            else
                            {
                                if (incoming > b.initialHardwareValue)
                                {
                                    float hardwareTravel = (incoming - b.initialHardwareValue) / (1.0f - b.initialHardwareValue);
                                    finalValue = b.initialSoftwareValue + hardwareTravel * (1.0f - b.initialSoftwareValue);
                                }
                                else if (incoming < b.initialHardwareValue)
                                {
                                    float hardwareTravel = (b.initialHardwareValue - incoming) / b.initialHardwareValue;
                                    finalValue = b.initialSoftwareValue - hardwareTravel * b.initialSoftwareValue;
                                }
                                else
                                {
                                    finalValue = b.initialSoftwareValue;
                                }
                            }
                        }

                        MappedAction action;
                        action.kind = MappedAction::Kind::parameterChange;
                        action.parameterChange.parameter = b.target.parameter;
                        action.parameterChange.normalizedValue = finalValue;
                        return action;
                    }
                }
            }
        }

        // 2. Fallback to fixed bindings
        for (const auto& b : activeBindings)
        {
            if (!b.binding.enabled)
                continue;

            uint8_t eventTypeInt = 0;
            switch (event.type)
            {
                case ControllerMidiEventType::controlChange: eventTypeInt = 1; break;
                case ControllerMidiEventType::noteOn:        eventTypeInt = 2; break;
                default: break;
            }

            if (b.binding.expectedMidiType != eventTypeInt)
                continue;

            if (b.binding.primaryData != event.data1)
                continue;

            if (b.binding.channel != 0 && b.binding.channel != event.channel)
                continue;

            if (isFixedBindingShadowedByLearnedTarget(b.binding))
                continue;

            if (isFixedBindingShadowedByLearnedSignature(b.binding.expectedMidiType, b.binding.channel, b.binding.primaryData))
                continue;

            if (b.binding.target == Minilab3LogicalTarget::panic)
            {
                if (event.type == ControllerMidiEventType::noteOn && event.data2 > 0)
                {
                    MappedAction action;
                    action.kind = MappedAction::Kind::command;
                    action.command = MappedCommand::panic;
                    return action;
                }
                continue;
            }

            if (b.target.parameter != nullptr)
            {
                float incoming = 0.0f;
                if (b.target.curve == MappingCurve::waveformChoice3Step)
                {
                    if (b.binding.primaryData == 114 && event.data2 >= 60 && event.data2 <= 68)
                    {
                        float current = b.target.parameter->getValue();
                        if (event.data2 < 64)
                            incoming = current > 0.6f ? 0.5f : 0.0f;
                        else if (event.data2 > 64)
                            incoming = current < 0.4f ? 0.5f : 1.0f;
                        else
                            incoming = current;
                    }
                    else
                    {
                        incoming = mapWaveformChoice(event.data2);
                    }
                }
                else
                {
                    incoming = mapControllerValue(event.data2, b.target);
                }

                float finalValue = incoming;

                if (b.target.curve != MappingCurve::waveformChoice3Step)
                {
                    if (b.state == TakeoverState::waitingForFirstTouch)
                    {
                        b.initialHardwareValue = incoming;
                        b.initialSoftwareValue = b.target.parameter->getValue();
                        
                        if (std::abs(incoming - b.initialSoftwareValue) < 0.05f)
                            b.state = TakeoverState::latched;
                        else
                            b.state = TakeoverState::scaling;
                    }

                    if (b.state == TakeoverState::scaling)
                    {
                        if (incoming >= 0.99f || incoming <= 0.01f || std::abs(incoming - b.initialSoftwareValue) < 0.05f)
                        {
                            b.state = TakeoverState::latched;
                            finalValue = incoming;
                        }
                        else
                        {
                            if (incoming > b.initialHardwareValue)
                            {
                                float hardwareTravel = (incoming - b.initialHardwareValue) / (1.0f - b.initialHardwareValue);
                                finalValue = b.initialSoftwareValue + hardwareTravel * (1.0f - b.initialSoftwareValue);
                            }
                            else if (incoming < b.initialHardwareValue)
                            {
                                float hardwareTravel = (b.initialHardwareValue - incoming) / b.initialHardwareValue;
                                finalValue = b.initialSoftwareValue - hardwareTravel * b.initialSoftwareValue;
                            }
                            else
                            {
                                finalValue = b.initialSoftwareValue;
                            }
                        }
                    }
                }

                MappedAction action;
                action.kind = MappedAction::Kind::parameterChange;
                action.parameterChange.parameter = b.target.parameter;
                action.parameterChange.normalizedValue = finalValue;
                return action;
            }
        }

        return {};
    }

    float MidiMappingEngine::mapControllerValue(uint8_t midiValue, const ParameterTarget& /*target*/) noexcept
    {
        return static_cast<float>(midiValue) / 127.0f;
    }

    float MidiMappingEngine::mapWaveformChoice(uint8_t midiValue) noexcept
    {
        if (midiValue <= 42) return 0.0f;
        if (midiValue <= 85) return 0.5f;
        return 1.0f;
    }
}
