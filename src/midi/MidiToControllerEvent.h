#pragma once

#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "MidiMappingEngine.h"

namespace coolsynth::midi
{
    [[nodiscard]] inline std::optional<ControllerMidiEvent> toControllerMidiEvent(
        const juce::MidiMessage& msg) noexcept
    {
        ControllerMidiEvent e;

        if (msg.isNoteOn())
        {
            if (msg.getChannel() != 10) return std::nullopt;
            e.type  = ControllerMidiEventType::noteOn;
            e.data1 = static_cast<uint8_t>(msg.getNoteNumber());
            e.data2 = static_cast<uint8_t>(msg.getVelocity());
        }
        else if (msg.isNoteOff())
        {
            if (msg.getChannel() != 10) return std::nullopt;
            e.type  = ControllerMidiEventType::noteOff;
            e.data1 = static_cast<uint8_t>(msg.getNoteNumber());
            e.data2 = static_cast<uint8_t>(msg.getVelocity());
        }
        else if (msg.isController())
        {
            e.type  = ControllerMidiEventType::controlChange;
            e.data1 = static_cast<uint8_t>(msg.getControllerNumber());
            e.data2 = static_cast<uint8_t>(msg.getControllerValue());
        }
        else
        {
            return std::nullopt;
        }

        e.channel = static_cast<uint8_t>(msg.getChannel());
        return e;
    }
}
