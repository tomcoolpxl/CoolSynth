#include <cmath>
#include <memory>

#include <juce_core/juce_core.h>

#include "interface/ControllerInput.h"
#include "interface/InterfaceModel.h"
#include "midi/ControllerMap.h"
#include "midi/ControllerSetup.h"
#include "midi/DuplicateNoteFilter.h"
#include "parameters/ParameterIDs.h"
#include "plugin/RemoteControlPages.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

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
            setup.setKeyboardKeys(49);
            expect(file.existsAsFile(), "saved");
            const auto saved = juce::JSON::parse(file);
            expect(saved.getDynamicObject() != nullptr && ! static_cast<bool>(saved.getProperty("miniLabDriver", true)));

            // Another program reads it: as a CoolSynth starting.
            setup.useForTesting({});
            expect(setup.map().empty() && setup.miniLabDriverOn());
            setup.useFileForTesting(file);
            expect(setup.origin() == ControllerSetup::Origin::saved);
            expect(! setup.miniLabDriverOn(), "the switch came back off");
            expectEquals(setup.keyboardKeys(), 49, "the keyboard's size came back");
            const auto* knob = setup.map().linkOf(Place::knob1);
            expect(knob != nullptr && knob->control == cc(21) && knob->encoding == Encoding::binaryOffset);
            const auto* setting = setup.map().settingFor(cc(22, 3));
            expect(setting != nullptr && setting->parameterId == "brightness");

            // Removed elsewhere (Forget in another program): back to nothing, the switch on.
            file.deleteFile();
            setup.useFileForTesting(file);
            expect(setup.map().empty() && setup.origin() == ControllerSetup::Origin::none && setup.miniLabDriverOn());
            expectEquals(setup.keyboardKeys(), 0);

            // Only a keyboard size of the player's own: the file is kept for it.
            setup.setKeyboardKeys(61);
            expect(file.existsAsFile(), "kept for the keyboard's size alone");
            setup.useFileForTesting(file);
            expectEquals(setup.keyboardKeys(), 61);
            setup.setKeyboardKeys(0);
            expect(! file.existsAsFile(), "nothing of the player's own left");

            setup.useForTesting({});
            file.deleteFile();
        }

        beginTest("an_old_unchanged_copy_of_a_ready_setup_becomes_todays_and_a_changed_one_stays");
        {
            // 0.3.0 saved the MiniLab's setup with its Arturia program only (the user's test pass, D225): with the
            // driver off and the MiniLab in its DAW program, the knobs did nothing.
            auto& setup = ControllerSetup::instance();
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("coolsynth-controller-old", ".json");
            const auto* ready = readyProfile("arturia.minilab3");
            expect(ready != nullptr);
            ControllerMap old;
            old.name = "Arturia MiniLab 3 (Arturia program)";
            old.profileId = "arturia.minilab3";
            int knob = 1;
            const auto any = [](int number) { return ControlId { ControlId::Kind::cc, 0, number }; }; // any channel, as 0.3.0 saved it
            for (const int n : { 74, 71, 76, 77, 93, 18, 19, 16 })
                old.link(knobPlace(knob++), any(n), Encoding::absolute);
            file.replaceWithText(juce::JSON::toString(old.toVar()));
            setup.useFileForTesting(file);
            const auto* knob7 = setup.map().placeFor(any(116));
            expect(knob7 != nullptr && knob7->place == Place::knob7, "the DAW program's knob 7 (CC 116) works now");
            expect(setup.map().placeFor(any(19)) != nullptr, "and the Arturia program's still");

            // The player's own change (knob 1 on CC 21, channel 1) is the player's: kept as it is.
            ControllerMap changed = old;
            changed.link(Place::knob1, cc(21), Encoding::absolute);
            file.replaceWithText(juce::JSON::toString(changed.toVar()));
            setup.useFileForTesting(file);
            expect(setup.map().placeFor(cc(21)) != nullptr, "the player's own link stays");
            expect(setup.map().placeFor(any(116)) == nullptr, "and nothing is added to a setup the player changed");
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

/*
    D216: one note by two routes plays once (FL Studio's doubled delivery), and the MiniLab 3's ready setup in both its
    programs. Its own class: the test runner's stack frame is small in Debug.
*/
class ControllerRouteTests final : public juce::UnitTest
{
public:
    ControllerRouteTests() : juce::UnitTest("Controller routes", "CoolSynth") {}

    void runTest() override
    {
        using namespace coolsynth::midi;
        auto cc = [](int number, int channel = 1) { return ControlId { ControlId::Kind::cc, channel, number }; };

        beginTest("a second copy of a note within 3 ms is dropped, with one of its releases");
        {
            DuplicateNoteFilter filter;
            filter.prepare(48000.0); // 3 ms = 144 samples
            const auto on = juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100));
            const auto off = juce::MidiMessage::noteOff(1, 60);
            expect(! filter.isDuplicate(on, 1000), "the first copy plays");
            expect(filter.isDuplicate(juce::MidiMessage::noteOn(10, 60, static_cast<juce::uint8>(100)), 1000),
                   "the second, at the same moment and on another channel, does not");
            expect(filter.isDuplicate(off, 1200), "one release is the copy's");
            expect(! filter.isDuplicate(off, 1200), "the other is the key's own");
            expect(! filter.isDuplicate(on, 1000 + 144 + 1), "a new strike after the window plays");
            expect(! filter.isDuplicate(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(90)), 1000),
                   "another note at the same moment is a chord");
            filter.noteTaken(36, 5000);
            expect(filter.isDuplicate(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 5010),
                   "a pad the panel took does not sound by the other route");
        }

        beginTest("a pad's copy by FL Studio's second route plays no note");
        {
            // FL with a MIDI port set delivers the pad twice: on its own channel (10, linked: the panel takes it) and
            // again through its routing to the selected channel, here on channel 1.
            ControllerMap map;
            map.link(Place::pad1, { ControlId::Kind::note, 10, 36 }, Encoding::absolute);
            ControllerSetup::instance().useForTesting(map);
            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 256);
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == "Basic Saw")
                    processor->loadSoundIntoLayer(i, 0);
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer both;
            both.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 10);
            both.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 10);
            processor->processBlock(buffer, both);
            processor->flushMappedControllerEventsSync();
            expectEquals(processor->engineForTesting().activeVoiceCount(), 0, "the pad plays no note by either route");
            expect(processor->getInterfaceModel().view() == coolsynth::ui::InterfaceModel::View::home, "and pad 1 opened Home");

            // A key of that pitch a moment later is a key.
            juce::MidiBuffer key;
            key.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 200);
            processor->processBlock(buffer, key);
            expectEquals(processor->engineForTesting().activeVoiceCount(), 1, "the key plays");
            ControllerSetup::instance().useForTesting({});
        }

        beginTest("the first eight parameters a DAW sees are the panel's knobs on Home");
        {
            // DAWs that give a controller a plugin's first eight controls (Cubase's Quick Controls, Ableton's banks,
            // generic scripts) start here (D216): layer A's four macros, Glide, Brightness, Space, the master volume.
            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto& all = processor->getParameters();
            const juce::StringArray wanted { "pA.macro1", "pA.macro2", "pA.macro3", "pA.macro4", "pA.voice.glide",
                                             coolsynth::parameters::ids::brightness, coolsynth::parameters::ids::space,
                                             coolsynth::parameters::ids::volumeMaster };
            expect(all.size() > wanted.size());
            for (int i = 0; i < wanted.size() && i < all.size(); ++i)
            {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(all[i]);
                expect(ranged != nullptr && ranged->getParameterID() == wanted[i],
                       "parameter " + juce::String(i + 1) + ": " + (ranged != nullptr ? ranged->getParameterID() : juce::String("?")));
            }
            expectEquals(all[0]->getName(32), juce::String("A Macro 1"));
        }

        beginTest("the MiniLab 3's ready setup answers in its Arturia and its DAW program");
        {
            const auto* ml = readyProfile("arturia.minilab3");
            expect(ml != nullptr);
            if (ml != nullptr)
            {
                const auto& map = ml->map;
                for (int number : { 19, 116 })
                {
                    const auto* link = map.placeFor(cc(number));
                    expect(link != nullptr && link->place == Place::knob7, "knob 7 as CC " + juce::String(number));
                }
                for (int number : { 17, 31 })
                {
                    const auto* link = map.placeFor(cc(number));
                    expect(link != nullptr && link->place == Place::fader4, "fader 4 as CC " + juce::String(number));
                }
                const auto* turn = map.placeFor(cc(28));
                expect(turn != nullptr && turn->place == Place::mainTurn && turn->encoding == Encoding::offsetGap);
                expect(map.placeFor(cc(118)) != nullptr && map.placeFor(cc(118))->place == Place::mainPress);
                expect(map.placeFor(cc(27)) != nullptr && map.placeFor(cc(27))->place == Place::shift);
                expect(map.linkOf(Place::knob7) != nullptr && map.linkOf(Place::knob7)->control == cc(19, 0),
                       "the panel names the Arturia program's control first");

                // Kept whole when saved and read back (a place with two controls).
                const auto copy = ControllerMap::fromVar(juce::JSON::parse(juce::JSON::toString(map.toVar())));
                expectEquals(static_cast<int>(copy.places.size()), static_cast<int>(map.places.size()));
                expect(copy.placeFor(cc(116)) != nullptr && copy.placeFor(cc(19)) != nullptr);
            }
            // The DAW program's main knob: 65 one click right, 62 one click left (63 never comes).
            expectEquals(relativeSteps(Encoding::offsetGap, 65), 1);
            expectEquals(relativeSteps(Encoding::offsetGap, 66), 2);
            expectEquals(relativeSteps(Encoding::offsetGap, 62), -1);
            expectEquals(relativeSteps(Encoding::offsetGap, 61), -2);

            // Learning a place still gives it one control.
            ControllerMap learned = ml != nullptr ? ml->map : ControllerMap {};
            learned.link(Place::knob7, cc(50), Encoding::absolute);
            expect(learned.placeFor(cc(19)) == nullptr && learned.placeFor(cc(116)) == nullptr, "both old controls let go");
        }
    }
};

static ControllerRouteTests controllerRouteTests;

/*
    D216, C5: the panel's pages as CLAP remote control pages, which a CLAP host puts on a controller's eight knobs.
*/
class RemoteControlPageTests final : public juce::UnitTest
{
public:
    RemoteControlPageTests() : juce::UnitTest("CLAP remote control pages", "CoolSynth") {}

    void runTest() override
    {
        using coolsynth::ui::InterfaceModel;
        auto processor = std::make_unique<SynthAudioProcessor>();
        auto& model = processor->getInterfaceModel();

        beginTest("every panel page, eight knobs each, the last four the same everywhere");
        {
            const auto pages = coolsynth::plugin::remotePages(model);
            expect(pages.size() > 6, "Home's six pages and the sections' pages: " + juce::String(static_cast<int>(pages.size())));
            const auto live = model.livePageNames();
            for (int i = 0; i < live.size() && i < static_cast<int>(pages.size()); ++i)
                expect(pages[static_cast<std::size_t>(i)].section == "Home" && pages[static_cast<std::size_t>(i)].name == live[i]);
            expectEquals(pages[0].parameterIds[0], juce::String("pA.macro1"), "Home's Macros page: layer A's macro 1 first");
            juce::Array<std::uint32_t> ids;
            for (const auto& page : pages)
            {
                expectEquals(page.parameterIds[4], juce::String("pA.voice.glide"));
                expectEquals(page.parameterIds[5], juce::String(coolsynth::parameters::ids::brightness));
                expectEquals(page.parameterIds[6], juce::String(coolsynth::parameters::ids::space));
                expectEquals(page.parameterIds[7], juce::String(coolsynth::parameters::ids::volumeMaster));
                for (const auto& id : page.parameterIds)
                    expect(id.isEmpty() || processor->getValueTreeState().getParameter(id) != nullptr,
                           page.section + " / " + page.name + ": " + id);
                expect(! ids.contains(page.id), "a page id once: " + page.name);
                ids.add(page.id);
            }
        }

        beginTest("the page the panel shows is the one the host is told");
        {
            auto pages = coolsynth::plugin::remotePages(model);
            // A new panel is on Home with layer A (pressing Home there would switch to layer B, which is empty).
            expect(model.view() == InterfaceModel::View::home && model.part() == 0);
            expectEquals(coolsynth::plugin::openRemotePage(model, pages), model.livePage(), "Home's live page");
            model.pressPad(InterfaceModel::padOfSection(0), false); // Sound
            const int open = coolsynth::plugin::openRemotePage(model, pages);
            expect(open >= 0, "a Sound page: " + InterfaceModel::sectionName(model.section()) + " / " + model.currentSlot().label + " / " + model.currentPage().name
                                  + ", view " + juce::String(static_cast<int>(model.view())));
            if (open >= 0)
            {
                const auto page = model.currentPage();
                for (std::size_t knob = 0; knob < 4; ++knob)
                    expectEquals(pages[static_cast<std::size_t>(open)].parameterIds[knob], page.ids[knob]);
            }
        }

        beginTest("the host's fill gives the parameters themselves");
        {
            juce::String section, name;
            std::uint32_t id = 0;
            std::array<juce::AudioProcessorParameter*, CLAP_REMOTE_CONTROLS_COUNT> knobs {};
            expect(processor->remoteControlsPageCount() > 6);
            expect(processor->remoteControlsPageFill(0, section, id, name, knobs));
            expect(section == "Home" && name == "Macros");
            auto* macro1 = dynamic_cast<juce::RangedAudioParameter*>(knobs[0]);
            expect(macro1 != nullptr && macro1->getParameterID() == "pA.macro1",
                   (knobs[0] == nullptr ? juce::String("no parameter") : knobs[0]->getName(100)) + ", layer " + juce::String(model.part()));
            expect(! processor->remoteControlsPageFill(100000, section, id, name, knobs), "no such page");
            expect(! processor->madeByHostOrApp(), "a test is a test, not a CLAP plugin");
        }
    }
};

static RemoteControlPageTests remoteControlPageTests;
