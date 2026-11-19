#include "ControllerInput.h"

#include "InterfaceModel.h"

namespace coolsynth::ui
{
    namespace cm = coolsynth::midi;

    ControllerInput::ControllerInput(InterfaceModel& m) : model(m) {}

    int ControllerInput::keyOf(const cm::ControlId& c) noexcept
    {
        return ((c.kind == cm::ControlId::Kind::note ? 1 : 0) * 17 + c.channel) * 128 + c.number;
    }

    void ControllerInput::handle(const cm::ControllerEvent& event)
    {
        auto& setup = cm::ControllerSetup::instance();
        if (setup.isLearning())
        {
            setup.hear(event, this);
            return;
        }
        // A changed setup starts every control afresh: what was guessed of it belonged to the old one.
        if (setup.revision() != seenRevision)
        {
            seenRevision = setup.revision();
            lastValues.clear();
        }
        const auto& map = setup.map();
        const auto* place = map.placeFor(event.control);
        const auto* setting = place == nullptr ? map.settingFor(event.control) : nullptr;
        if (place == nullptr && setting == nullptr)
            return;

        auto& last = lastValues[keyOf(event.control)];
        const int previous = last.value;
        last.value = event.value;
        auto encoding = place != nullptr ? place->encoding : setting->encoding;
        if (encoding == cm::Encoding::absolute && ! event.note)
        {
            // Three times the same value an endless knob sends for one step (63-65, 1-3, 125-126): this control turns.
            // Any other repeat is not evidence (a 14-bit fader's coarse half, a resend), so a knob is never taken for
            // an endless one by chance (review of D211).
            const bool turnLike = (event.value >= 61 && event.value <= 67 && event.value != 64)
                               || (event.value >= 1 && event.value <= 3) || (event.value >= 125 && event.value <= 126);
            last.repeats = previous == event.value && turnLike ? last.repeats + 1 : 0;
            if (! last.turns && last.repeats >= 2)
                last.turns = cm::guessEncoding({ event.value, event.value, event.value });
            if (last.turns)
                encoding = *last.turns;
        }

        if (place != nullptr)
        {
            play(*place, event, encoding, previous);
            return;
        }
        if (encoding == cm::Encoding::absolute)
            model.moveSettingFromController(setting->parameterId, previous, event.value);
        else
            model.turnSettingFromController(setting->parameterId, cm::relativeSteps(encoding, event.value), model.hardwareShiftHeld());
    }

    void ControllerInput::play(const cm::PlaceLink& link, const cm::ControllerEvent& event, cm::Encoding encoding, int previous)
    {
        using cm::Place;
        const bool down = event.value >= 64 || (event.note && event.value > 0);
        const bool shift = model.hardwareShiftHeld();
        const auto place = link.place;

        if (cm::isButtonPlace(place))
        {
            if (place == Place::shift)
            {
                model.setHardwareShift(down);
                return;
            }
            // pressed: a note-on, or a controller in its upper half (some buttons send 127 for each press and nothing
            // when let go; none sends while held)
            if (! down)
                return;
            switch (place)
            {
                case Place::mainLeft:  model.turnMain(-1); break;
                case Place::mainRight: model.turnMain(1); break;
                case Place::mainPress: model.pressMain(); break;
                case Place::back:      model.back(); break;
                default:               model.pressPad(cm::padOf(place), shift); break;
            }
            return;
        }

        const bool turns = encoding != cm::Encoding::absolute;
        const int steps = turns ? cm::relativeSteps(encoding, event.value) : 0;
        if (place == Place::mainTurn)
        {
            if (turns)
            {
                model.turnMain(steps);
                return;
            }
            // A knob with end stops browses by its movement: a step for every two counts (D211).
            if (previous < 0)
                return;
            int counts = event.value - previous;
            if (counts == 0 && (event.value == 0 || event.value == 127))
                counts = event.value == 127 ? 2 : -2; // an endless knob at its end goes on (D200)
            if ((mainCounts > 0) != (counts > 0))
                mainCounts = 0;
            mainCounts += counts;
            const int moved = mainCounts / 2;
            mainCounts -= moved * 2;
            if (moved != 0)
                model.turnMain(moved);
            return;
        }
        if (place >= Place::knob1 && place <= Place::knob8)
        {
            const int knob = cm::knobOf(place);
            if (turns)
                model.turnKnobFromDevice(knob, steps, shift);
            else if (previous >= 0)
                model.moveKnobFromController(knob, previous, event.value);
            return;
        }
        if (place >= Place::fader1 && place <= Place::fader4)
        {
            // The faders are volumes the player keeps (D184): taken as they stand.
            const int fader = cm::faderOf(place);
            if (turns)
                model.moveFader(fader, juce::jlimit(0.0f, 1.0f, model.faderValue(fader) + static_cast<float>(steps) / 100.0f), false);
            else
                model.moveFader(fader, static_cast<float>(event.value) / 127.0f, false);
        }
    }
}
