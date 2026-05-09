#include "MidiMappingEngine.h"
#include "parameters/ParameterIDs.h"

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

    MappedAction MidiMappingEngine::translate(const ControllerMidiEvent& event) const noexcept
    {
        for (const auto& b : activeBindings)
        {
            if (!b.binding.enabled)
                continue;

            // Match MIDI type
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

            // Channel 0 means Omni
            if (b.binding.channel != 0 && b.binding.channel != event.channel)
                continue;

            // Found a match
            if (b.binding.target == Minilab3LogicalTarget::panic)
            {
                // Panic triggers on Rising Edge (noteOn with velocity > 0)
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
                float incomingNormalizedValue = 0.0f;
                if (b.target.curve == MappingCurve::waveformChoice3Step)
                    incomingNormalizedValue = mapWaveformChoice(event.data2);
                else
                    incomingNormalizedValue = mapControllerValue(event.data2, b.target);

                if (!b.isLatched)
                {
                    const float softwareValue = b.target.parameter->getValue();
                    const float diff = std::abs(incomingNormalizedValue - softwareValue);
                    
                    // 5% threshold for soft takeover
                    if (diff <= 0.05f)
                    {
                        b.isLatched = true;
                    }
                    else
                    {
                        // Ignore input until it catches up
                        return {};
                    }
                }

                MappedAction action;
                action.kind = MappedAction::Kind::parameterChange;
                action.parameterChange.parameter = b.target.parameter;
                action.parameterChange.normalizedValue = incomingNormalizedValue;
                
                return action;
            }
        }

        return {};
    }

    float MidiMappingEngine::mapControllerValue(uint8_t midiValue, const ParameterTarget& /*target*/) noexcept
    {
        // Simple linear map from 0..127 to 0.0..1.0
        return static_cast<float>(midiValue) / 127.0f;
    }

    float MidiMappingEngine::mapWaveformChoice(uint8_t midiValue) noexcept
    {
        // Buckets: 0-42 (sine), 43-85 (square), 86-127 (saw)
        // Normalized values for 3-step: 0.0, 0.5, 1.0
        
        if (midiValue <= 42) return 0.0f;
        if (midiValue <= 85) return 0.5f;
        return 1.0f;
    }
}
