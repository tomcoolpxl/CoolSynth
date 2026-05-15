#include "MidiLearn.h"
#include <algorithm>
#include "parameters/ParameterIDs.h"
#include "midi/MidiMappingEngine.h"

namespace coolsynth::midi
{
    MidiLearnManager::MidiLearnManager() = default;

    bool MidiLearnManager::isParameterLearnEligible(juce::StringRef parameterId) noexcept
    {
        for (const auto* knownId : parameters::learnableParameterIds)
            if (parameterId == juce::StringRef(knownId))
                return true;

        return false;
    }

    bool MidiLearnManager::isLearnableParameter(juce::StringRef parameterId) const noexcept
    {
        return isParameterLearnEligible(parameterId);
    }

    bool MidiLearnManager::beginLearning(juce::String parameterId, juce::String parameterName)
    {
        if (!isParameterLearnEligible(parameterId))
            return false;

        session.armed = true;
        session.parameterId = parameterId;
        session.parameterName = parameterName;
        session.statusText = "Learning " + parameterName + " - move a MIDI CC control";
        return true;
    }

    void MidiLearnManager::cancelLearning() noexcept
    {
        session = MidiLearnSession{};
    }

    LearnCaptureOutcome MidiLearnManager::handleIncomingEvent(const ControllerMidiEvent& event)
    {
        if (!session.armed)
            return { MidiLearnCaptureResult::ignored, false, false, std::nullopt, "" };

        if (event.type == ControllerMidiEventType::noteOn ||
            event.type == ControllerMidiEventType::noteOff ||
            event.type == ControllerMidiEventType::other)
        {
            session.statusText = "Move a MIDI CC control - note events cannot be learned here";
            return { MidiLearnCaptureResult::rejectedNonCc, false, false, std::nullopt, session.statusText };
        }

        if (event.type == ControllerMidiEventType::controlChange)
        {
            if (isReservedSynthControllerEvent(event))
            {
                session.statusText = "Mod wheel, sustain, and host safety controllers are reserved";
                return { MidiLearnCaptureResult::ignored, false, false, std::nullopt, session.statusText };
            }

            MidiCcKey newKey{ event.channel, event.data1 };
            
            if (const auto* existing = findBindingForParameter(session.parameterId))
            {
                if (existing->cc == newKey)
                {
                    // Exact same binding already exists
                    session = MidiLearnSession{};
                    return { MidiLearnCaptureResult::duplicateNoChange, true, false, std::nullopt, "" };
                }
            }

            LearnedCcBinding newBinding{ session.parameterId, newKey };

            // Remove existing binding for the same parameter
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                [&](const auto& b) { return b.parameterId == session.parameterId; }),
                bindings.end());

            // Remove any other binding that shares the exact same CC signature
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                [&](const auto& b) { return b.cc == newKey; }),
                bindings.end());

            bindings.push_back(newBinding);
            
            session = MidiLearnSession{};
            return { MidiLearnCaptureResult::captured, true, true, newBinding, "" };
        }

        return { MidiLearnCaptureResult::ignored, false, false, std::nullopt, "" };
    }

    void MidiLearnManager::replaceBindings(std::vector<LearnedCcBinding> newBindings)
    {
        bindings = std::move(newBindings);
        normalizeBindings();
    }

    bool MidiLearnManager::clearBinding(juce::StringRef parameterId)
    {
        auto it = std::remove_if(bindings.begin(), bindings.end(),
            [&](const auto& b) { return b.parameterId == parameterId; });
        
        if (it != bindings.end())
        {
            bindings.erase(it, bindings.end());
            return true;
        }
        return false;
    }

    const LearnedCcBinding* MidiLearnManager::findBindingForParameter(juce::StringRef parameterId) const noexcept
    {
        for (const auto& b : bindings)
        {
            if (b.parameterId == parameterId)
                return &b;
        }
        return nullptr;
    }

    std::span<const LearnedCcBinding> MidiLearnManager::getBindings() const noexcept
    {
        return bindings;
    }

    MidiLearnSession MidiLearnManager::getSession() const
    {
        return session;
    }

    void MidiLearnManager::normalizeBindings()
    {
        std::vector<LearnedCcBinding> normalized;
        normalized.reserve(bindings.size());

        for (auto it = bindings.rbegin(); it != bindings.rend(); ++it)
        {
            if (!it->isValid())
                continue;

            if (!isParameterLearnEligible(it->parameterId))
                continue;

            auto paramExists = std::any_of(normalized.begin(), normalized.end(),
                [&](const auto& n) { return n.parameterId == it->parameterId; });
            
            auto ccExists = std::any_of(normalized.begin(), normalized.end(),
                [&](const auto& n) { return n.cc == it->cc; });

            if (!paramExists && !ccExists)
            {
                normalized.push_back(*it);
            }
        }

        std::reverse(normalized.begin(), normalized.end());
        bindings = std::move(normalized);
    }
}
