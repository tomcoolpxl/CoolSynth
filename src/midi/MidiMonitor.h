#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace coolsynth::midi
{
    enum class MidiMonitorMessageType : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
    };

    struct MidiMonitorEvent
    {
        uint64_t eventOrder = 0;
        double timestampSeconds = 0.0;
        MidiMonitorMessageType type = MidiMonitorMessageType::noteOn;
        int channel = 0;
        int primaryValue = 0;
        int secondaryValue = 0;
        int noteNumber = -1;
        int controllerNumber = -1;
    };

    class MidiMonitorBuffer
    {
    public:
        static constexpr int queueCapacity = 256;
        static constexpr int visibleHistoryCapacity = 128;

        MidiMonitorBuffer() = default;
        ~MidiMonitorBuffer() = default;

        void pushMessage(const juce::MidiMessage& message, double timestampSeconds) noexcept;
        int drainPending(MidiMonitorEvent* destination, int maxEvents) noexcept;
        void clear() noexcept;

    private:
        std::atomic<uint64_t> nextEventOrder { 1 };
        juce::AbstractFifo pendingQueue { queueCapacity };
        std::array<MidiMonitorEvent, queueCapacity> pendingEvents {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitorBuffer)
    };

    juce::String formatMonitorMessageType(MidiMonitorMessageType type);
    juce::String formatNoteName(int noteNumber);
}
