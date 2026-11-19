#include <cmath>
#include <memory>

#include <juce_core/juce_core.h>

#include "interface/ControllerInput.h"
#include "interface/InterfaceModel.h"
#include "midi/ControllerMap.h"
#include "midi/ControllerSetup.h"
#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"

/*
    Any controller on the panel (D211): the map, the turn detection, value scaling, the ready profiles, and a
    controller's messages through the processor to the panel and away from the sound. Heap processors in their own
    class: the test runner's stack frame is small in Debug.
*/
class ControllerTests final : public juce::UnitTest
{
public:
    ControllerTests() : juce::UnitTest("Controller", "CoolSynth") {}

    void runTest() override
    {
        using namespace coolsynth::midi;
        auto cc = [](int number, int channel = 1) { return ControlId { ControlId::Kind::cc, channel, number }; };

        beginTest("a_control_stands_for_one_thing_and_a_place_has_one_control");
        {
            ControllerMap map;
            map.link(Place::knob1, cc(21), Encoding::absolute);
            map.link(Place::knob2, cc(21), Encoding::absolute);
            expect(map.linkOf(Place::knob1) == nullptr, "CC 21 moved to knob 2");
            expect(map.linkOf(Place::knob2) != nullptr);
            map.link(Place::knob2, cc(22), Encoding::binaryOffset);
            expect(map.placeFor(cc(21)) == nullptr, "knob 2 let CC 21 go");
            map.link("brightness", cc(22), Encoding::absolute);
            expect(map.linkOf(Place::knob2) == nullptr, "a setting took CC 22 from the place");
            map.link(Place::shift, cc(64), Encoding::absolute);
            map.link(Place::back, cc(1), Encoding::absolute);
            expect(map.linkOf(Place::shift) == nullptr && map.linkOf(Place::back) == nullptr, "sustain and mod wheel stay the sound's");

            map.link(Place::pad1, { ControlId::Kind::note, 10, 36 }, Encoding::absolute);
            map.link(Place::mainTurn, cc(28), Encoding::twosComplement);
            const auto copy = ControllerMap::fromVar(juce::JSON::parse(juce::JSON::toString(map.toVar())));
            expectEquals(static_cast<int>(copy.places.size()), 2);
            expectEquals(static_cast<int>(copy.settings.size()), 1);
            expect(copy.linkOf(Place::mainTurn) != nullptr && copy.linkOf(Place::mainTurn)->encoding == Encoding::twosComplement);
            expect(copy.placeFor({ ControlId::Kind::note, 10, 36 }) != nullptr);
            expect(copy.placeFor({ ControlId::Kind::note, 1, 36 }) == nullptr, "another channel is another control");

            ControllerMap any;
            any.link(Place::knob1, cc(70, 0), Encoding::absolute);
            expect(any.placeFor(cc(70, 5)) != nullptr, "a ready profile's open channel takes any channel");
        }

        beginTest("turns_are_told_from_positions");
        {
            expect(guessEncoding({ 60, 61, 62, 63, 62, 61, 60 }) == Encoding::absolute);
            expect(guessEncoding({ 125, 126, 127, 127, 127, 126, 125 }) == Encoding::absolute, "an endless knob at its end");
            expect(guessEncoding({ 65, 65, 66, 65, 63, 63, 62 }) == Encoding::binaryOffset);
            expect(guessEncoding({ 1, 1, 2, 1, 127, 127, 126 }) == Encoding::twosComplement);
            expect(guessEncoding({ 1, 1, 2, 1, 65, 65, 66 }) == Encoding::signBit);
            expect(guessEncoding({ 1, 1, 1 }) == Encoding::twosComplement, "right only: the commoner");
            expectEquals(relativeSteps(Encoding::binaryOffset, 66), 2);
            expectEquals(relativeSteps(Encoding::binaryOffset, 0), 0);
            expectEquals(relativeSteps(Encoding::twosComplement, 126), -2);
            expectEquals(relativeSteps(Encoding::signBit, 67), -3);
            expectEquals(relativeSteps(Encoding::signBit, 3), 3);
        }

        beginTest("ready_profiles_are_found_by_their_ports");
        {
            const auto* mpk = readyProfileForPorts({ "Microsoft GS Wavetable Synth", "MPK mini 3" });
            expect(mpk != nullptr && mpk->id == "akai.mpkmini3");
            const auto* lk = readyProfileForPorts({ "Launchkey Mini MK3 MIDI" });
            expect(lk != nullptr && lk->id == "novation.launchkeymini3");
            expect(readyProfileForPorts({ "loopMIDI Port" }) == nullptr);
            for (const auto& p : readyProfiles())
            {
                expect(! p.map.empty(), p.name);
                for (const auto& link : p.map.places)
                    expect(! isReserved(link.control), p.name + ": " + link.control.describe());
            }
        }

        beginTest("learning_tells_a_knob_from_a_button");
        {
            auto& setup = ControllerSetup::instance();
            setup.useForTesting({});
            const int owner = 0; // stands for a CoolSynth's controller input
            std::optional<ControllerSetup::Heard> heard;
            auto keep = [&heard](const ControllerSetup::Heard& h) { heard = h; };
            setup.learn(&owner, ControllerSetup::Expect::turn, keep);
            for (int v : { 65, 65, 66, 63, 63 })
                setup.hear({ cc(30, 2), v, false }, &owner);
            setup.finishLearningNow();
            expect(heard.has_value() && heard->control == cc(30, 2) && ! heard->button && heard->encoding == Encoding::binaryOffset);
            expect(! setup.isLearning());

            heard.reset();
            setup.learn(&owner, ControllerSetup::Expect::turn, keep);
            setup.hear({ cc(40), 127, false }, &owner);
            setup.hear({ cc(40), 0, false }, &owner);
            setup.finishLearningNow();
            expect(heard.has_value() && heard->button, "127 then 0 on the main knob's turn: a button (left or right)");

            heard.reset();
            setup.learn(&owner, ControllerSetup::Expect::press, keep);
            setup.hear({ { ControlId::Kind::note, 10, 37 }, 90, true }, &owner);
            expect(heard.has_value() && heard->button && heard->control.kind == ControlId::Kind::note, "a pad, at once");
            setup.stopAllLearning();
        }

        beginTest("a_button_is_wanted_and_a_knob_touched_meanwhile_is_not_taken");
        {
            auto& setup = ControllerSetup::instance();
            setup.useForTesting({});
            const int owner = 0;
            std::optional<ControllerSetup::Heard> heard;
            juce::String said;
            setup.learn(&owner, ControllerSetup::Expect::press,
                        [&heard](const ControllerSetup::Heard& h) { heard = h; },
                        [&said](const juce::String& text) { said = text; });
            for (int v : { 10, 20, 30, 40 })
                setup.hear({ cc(74), v, false }, &owner);
            setup.finishLearningNow();
            expect(! heard.has_value(), "a knob is no button");
            expect(setup.isLearning(), "still listening for the button");
            expect(said.containsIgnoreCase("knob"), "and it says so: " + said);

            setup.hear({ cc(80), 127, false }, &owner);
            setup.hear({ cc(80), 0, false }, &owner);
            setup.finishLearningNow();
            expect(heard.has_value() && heard->button && heard->control == cc(80), "then the button");
            expect(! setup.isLearning());
        }

        beginTest("learning_belongs_to_the_coolsynth_that_asked");
        {
            // Two CoolSynths in one host, fed the same keyboard (review of D211).
            auto& setup = ControllerSetup::instance();
            setup.useForTesting({});
            const int first = 0, second = 0;
            int firstHeard = 0, secondHeard = 0;
            const int firstToken = setup.learn(&first, ControllerSetup::Expect::press,
                                               [&firstHeard](const ControllerSetup::Heard&) { ++firstHeard; });
            setup.hear({ { ControlId::Kind::note, 1, 60 }, 100, true }, &second);
            expectEquals(firstHeard, 0, "the other CoolSynth's copy of a move does not count");
            expect(setup.learningWith(firstToken));

            const int secondToken = setup.learn(&second, ControllerSetup::Expect::press,
                                                [&secondHeard](const ControllerSetup::Heard&) { ++secondHeard; });
            expect(! setup.learningWith(firstToken) && setup.learningWith(secondToken), "a later learning takes over");
            setup.stopLearning(firstToken);
            expect(setup.learningWith(secondToken), "the first one's window closing leaves the second listening");
            setup.hear({ { ControlId::Kind::note, 1, 61 }, 100, true }, &first);
            expectEquals(secondHeard, 0);
            setup.hear({ { ControlId::Kind::note, 1, 61 }, 100, true }, &second);
            expectEquals(secondHeard, 1);
            expectEquals(firstHeard, 0);
            expect(! setup.isLearning());

            const int third = setup.learn(&first, ControllerSetup::Expect::turn, [](const ControllerSetup::Heard&) {});
            setup.stopLearning(third);
            expect(! setup.isLearning(), "its own token stops it");
        }

        beginTest("the_setup_is_kept_in_its_file_with_the_driver_switch");
        {
            auto& setup = ControllerSetup::instance();
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("coolsynth-controller-test", ".json");
            setup.useFileForTesting(file);
            expect(setup.origin() == ControllerSetup::Origin::none && setup.miniLabDriverOn());

            ControllerMap map;
            map.link(Place::knob1, cc(21), Encoding::binaryOffset);
            map.link("brightness", cc(22, 3), Encoding::absolute);
            setup.setMap(map);
            setup.setMiniLabDriverOn(false);
            expect(file.existsAsFile(), "saved");
            const auto saved = juce::JSON::parse(file);
            expect(saved.getDynamicObject() != nullptr && ! static_cast<bool>(saved.getProperty("miniLabDriver", true)));

            // Another program reads it: as a CoolSynth starting.
            setup.useForTesting({});
            expect(setup.map().empty() && setup.miniLabDriverOn());
            setup.useFileForTesting(file);
            expect(setup.origin() == ControllerSetup::Origin::saved);
            expect(! setup.miniLabDriverOn(), "the switch came back off");
            const auto* knob = setup.map().linkOf(Place::knob1);
            expect(knob != nullptr && knob->control == cc(21) && knob->encoding == Encoding::binaryOffset);
            const auto* setting = setup.map().settingFor(cc(22, 3));
            expect(setting != nullptr && setting->parameterId == "brightness");

            // Removed elsewhere (Forget in another program): back to nothing, the switch on.
            file.deleteFile();
            setup.useFileForTesting(file);
            expect(setup.map().empty() && setup.origin() == ControllerSetup::Origin::none && setup.miniLabDriverOn());

            setup.useForTesting({});
            file.deleteFile();
        }

        beginTest("value_scaling_moves_the_way_the_knob_does_and_meets_at_the_ends");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            auto& model = processor->getInterfaceModel();
            auto* brightness = processor->getValueTreeState().getParameter(coolsynth::parameters::ids::brightness);
            expect(brightness != nullptr);
            brightness->setValueNotifyingHost(0.2f);
            model.moveKnobFromController(7, 100, 101); // the knob far above the value
            const float after = brightness->getValue();
            expect(after > 0.2f && after < 0.25f, "a small move, no jump: " + juce::String(after));
            model.moveKnobFromController(7, 101, 127);
            expectWithinAbsoluteError(brightness->getValue(), 1.0f, 1.0e-4f);
            model.moveKnobFromController(7, 127, 0);
            expectWithinAbsoluteError(brightness->getValue(), 0.0f, 1.0e-4f);
            model.moveKnobFromController(7, 0, 64);
            expectWithinAbsoluteError(brightness->getValue(), 64.0f / 127.0f, 1.0e-3f);
        }

        beginTest("a_linked_controller_plays_the_panel_and_not_the_sound");
        {
            ControllerMap map;
            map.link(Place::knob6, cc(23), Encoding::absolute);
            map.link(Place::knob7, cc(21), Encoding::absolute);
            map.link(Place::knob8, cc(22), Encoding::absolute);
            map.link(Place::pad1, { ControlId::Kind::note, 10, 36 }, Encoding::absolute);
            ControllerSetup::instance().useForTesting(map);

            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 128);
            auto* brightness = processor->getValueTreeState().getParameter(coolsynth::parameters::ids::brightness);
            juce::AudioBuffer<float> buffer(2, 128);
            auto play = [&](const juce::MidiMessage& m)
            {
                juce::MidiBuffer midi;
                midi.addEvent(m, 0);
                buffer.clear();
                processor->processBlock(buffer, midi);
                processor->flushMappedControllerEventsSync();
                return buffer.getMagnitude(0, buffer.getNumSamples());
            };

            play(juce::MidiMessage::controllerEvent(1, 21, 0));
            play(juce::MidiMessage::controllerEvent(1, 21, 127));
            expectWithinAbsoluteError(brightness->getValue(), 1.0f, 1.0e-4f);

            // An endless knob a ready profile took for one with end stops: the same step value three times turns it.
            auto* space = processor->getValueTreeState().getParameter(coolsynth::parameters::ids::space);
            expect(space != nullptr);
            space->setValueNotifyingHost(0.5f);
            play(juce::MidiMessage::controllerEvent(1, 22, 65)); // first: a position as far as anyone knows
            play(juce::MidiMessage::controllerEvent(1, 22, 65)); // again: could be a resend
            expectWithinAbsoluteError(space->getValue(), 0.5f, 1.0e-4f, "not yet taken for a turn");
            play(juce::MidiMessage::controllerEvent(1, 22, 65)); // a third time: a turn, one step right
            expect(space->getValue() > 0.5f && space->getValue() < 0.55f, "turned a step: " + juce::String(space->getValue()));

            // A knob with end stops that sends the same value again (a resend, a fader's coarse half): no turn.
            auto& model = processor->getInterfaceModel();
            auto* sixth = model.knobParameter(6);
            expect(sixth != nullptr);
            if (sixth != nullptr)
            {
                sixth->setValueNotifyingHost(0.5f);
                for (int i = 0; i < 3; ++i)
                    play(juce::MidiMessage::controllerEvent(1, 23, 40));
                play(juce::MidiMessage::controllerEvent(1, 23, 41));
                const float moved = sixth->getValue();
                expect(moved > 0.5f && moved < 0.52f, "a count up moves it a little, as a knob: " + juce::String(moved));
            }
            const float pad = play(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)));
            expectWithinAbsoluteError(pad, 0.0f, 1.0e-6f, "the pad plays no note");
            expect(model.view() == coolsynth::ui::InterfaceModel::View::home, "pad 1 opened Home");
            play(juce::MidiMessage::noteOff(10, 36));

            const float key = play(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
            expect(key > 1.0e-4f, "a key still plays");

            expect(! ControllerSetup::takesFromSound(juce::MidiMessage::controllerEvent(1, 1, 90)), "the mod wheel stays the sound's");
            ControllerSetup::instance().useForTesting({});
        }

        beginTest("a_block_full_of_controller_moves_reaches_the_panel_and_an_overflow_is_counted");
        {
            ControllerMap map;
            map.link(Place::knob7, cc(21), Encoding::absolute);
            ControllerSetup::instance().useForTesting(map);

            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 512);
            auto* brightness = processor->getValueTreeState().getParameter(coolsynth::parameters::ids::brightness);
            brightness->setValueNotifyingHost(0.0f);
            juce::AudioBuffer<float> buffer(2, 512);

            // 200 moves in one block, up to the end stop: every one is there in order, so the knob ends at the top.
            juce::MidiBuffer sweep;
            for (int i = 0; i < 200; ++i)
                sweep.addEvent(juce::MidiMessage::controllerEvent(1, 21, juce::jmin(127, i)), i * 2);
            const auto droppedBefore = processor->getDroppedControllerEventCount();
            buffer.clear();
            processor->processBlock(buffer, sweep);
            processor->flushMappedControllerEventsSync();
            expectWithinAbsoluteError(brightness->getValue(), 1.0f, 1.0e-4f);
            expect(processor->getDroppedControllerEventCount() == droppedBefore, "nothing lost");

            // More than the queue holds (255) before the panel looks: the rest are counted, nothing breaks.
            juce::MidiBuffer flood;
            for (int i = 0; i < 300; ++i)
                flood.addEvent(juce::MidiMessage::controllerEvent(1, 21, i % 2 == 0 ? 100 : 101), i);
            buffer.clear();
            processor->processBlock(buffer, flood);
            expect(processor->getDroppedControllerEventCount() - droppedBefore == 45,
                   "dropped: " + juce::String(static_cast<juce::int64>(processor->getDroppedControllerEventCount() - droppedBefore)));
            processor->flushMappedControllerEventsSync();
            juce::MidiBuffer next;
            next.addEvent(juce::MidiMessage::controllerEvent(1, 21, 0), 0);
            buffer.clear();
            processor->processBlock(buffer, next);
            processor->flushMappedControllerEventsSync();
            expectWithinAbsoluteError(brightness->getValue(), 0.0f, 1.0e-4f, "the queue works on after an overflow");
            ControllerSetup::instance().useForTesting({});
        }
    }
};

static ControllerTests controllerTests;
