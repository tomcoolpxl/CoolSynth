#pragma once

#include <optional>
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

namespace coolsynth::midi
{
    struct ControllerMidiEvent;
    enum class ControllerMidiEventType : uint8_t;

    struct MidiCcKey
    {
        uint8_t channel = 1;
        uint8_t controllerNumber = 0;

        bool isValid() const noexcept
        {
            return channel >= 1 && channel <= 16;
        }

        bool operator==(const MidiCcKey& other) const noexcept = default;
    };

    struct LearnedCcBinding
    {
        juce::String parameterId;
        MidiCcKey cc;

        bool isValid() const noexcept
        {
            return parameterId.isNotEmpty() && cc.isValid();
        }
    };

    enum class MidiLearnCaptureResult : uint8_t
    {
        ignored,
        rejectedNonCc,
        captured,
        duplicateNoChange,
    };

    struct LearnCaptureOutcome
    {
        MidiLearnCaptureResult result = MidiLearnCaptureResult::ignored;
        bool consumeOriginalEvent = false;
        bool bindingsChanged = false;
        std::optional<LearnedCcBinding> capturedBinding;
        juce::String statusText;
    };

    struct MidiLearnSession
    {
        bool armed = false;
        juce::String parameterId;
        juce::String parameterName;
        juce::String statusText;
    };

    class MidiLearnManager final
    {
    public:
        MidiLearnManager();

        bool isLearnableParameter(juce::StringRef parameterId) const noexcept;
        bool beginLearning(juce::String parameterId, juce::String parameterName);
        void cancelLearning() noexcept;

        LearnCaptureOutcome handleIncomingEvent(const ControllerMidiEvent& event);

        void replaceBindings(std::vector<LearnedCcBinding> bindings);
        bool clearBinding(juce::StringRef parameterId);

        const LearnedCcBinding* findBindingForParameter(juce::StringRef parameterId) const noexcept;
        std::span<const LearnedCcBinding> getBindings() const noexcept;
        MidiLearnSession getSession() const;

    private:
        static bool isContinuousLearnEligible(juce::StringRef parameterId) noexcept;
        void normalizeBindings();

        MidiLearnSession session;
        std::vector<LearnedCcBinding> bindings;
    };
}
