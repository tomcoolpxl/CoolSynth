#pragma once

#include <map>

#include "midi/ControllerSetup.h"

namespace coolsynth::ui
{
    class InterfaceModel;

    /*
        One CoolSynth's controller (D211): what the computer's setup links, played on this CoolSynth's panel as the
        MiniLab plays it. A position (a knob with end stops) moves its knob by value scaling, a turn (an endless knob)
        by its steps, a button presses; while learning goes on, everything goes to the setup instead. Message thread.
    */
    class ControllerInput
    {
    public:
        explicit ControllerInput(InterfaceModel& model);

        void handle(const coolsynth::midi::ControllerEvent& event);

    private:
        struct Last
        {
            int value = -1;
            /** A control the setup calls a position that repeats a value away from its ends turns instead: a ready
                profile may not know how the keyboard was set (D211). */
            std::optional<coolsynth::midi::Encoding> turns;
        };
        [[nodiscard]] static int keyOf(const coolsynth::midi::ControlId& c) noexcept;
        void play(const coolsynth::midi::PlaceLink& link, const coolsynth::midi::ControllerEvent& event,
                  coolsynth::midi::Encoding encoding, int previous);

        InterfaceModel& model;
        std::map<int, Last> lastValues;
        int mainCounts = 0; // a main knob with end stops: counts not yet a step
    };
}
