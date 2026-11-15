#include <map>
#include <memory>

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "interface/InterfaceModel.h"
#include "midi/MiniLab3Protocol.h"
#include "plugin/PanelEditor.h"
#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "presets/MySounds.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/UserLibrary.h"

/*
    The V3 interface's logic (docs/interface-v3.md): navigation E, Home as browser and live page,
    the type list with its ‹ Back row, knobs, faders with pickup, the MiniLab's screen. The processor
    lives on the heap (MSVC Debug stack frames, see ProcessorUxTests).
*/
class InterfaceModelTests final : public juce::UnitTest
{
public:
    InterfaceModelTests() : juce::UnitTest("InterfaceModel", "CoolSynth") {}

    void runTest() override
    {
        using coolsynth::ui::InterfaceModel;

        beginTest("Starts in Home on the sound playing; the Sound section opens on G1, with knobs on its first page");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            expect(model->view() == InterfaceModel::View::home, "CoolSynth opens in Home (D186)");
            model->pressPad(InterfaceModel::padOfSection(0), false);
            expect(model->view() == InterfaceModel::View::section);
            expectEquals(model->section(), 0);
            expectEquals(model->currentSlot().code, juce::String("G1"));
            expect(! model->currentSlot().empty);
            expect(model->knob(1).active);
            expectEquals(model->mainTurnRole(), juce::String("Slot"));
            expectEquals(model->mainPressRole(), juce::String("Type"));
        }

        beginTest("The main knob and the lit pad move between slots; other pads open their section");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(0), false); // the Sound section (CoolSynth opens in Home, D186)
            model->turnMain(1);
            expectEquals(model->currentSlot().code, juce::String("G2"));
            model->turnMain(10);
            expectEquals(model->currentSlot().code, juce::String("G3"));
            model->pressPad(coolsynth::ui::InterfaceModel::padOfSection(0), false);
            expectEquals(model->currentSlot().code, juce::String("G1"));
            model->pressPad(coolsynth::ui::InterfaceModel::padOfSection(3), false);
            expectEquals(model->section(), 3);
            expectEquals(model->currentSlot().code, juce::String("I1"));
            expectEquals(static_cast<int>(model->slotsOf(3).size()), 12); // six layer slots (D194) and F1-F6
            expectEquals(static_cast<int>(model->slotsOf(2).size()), 5);
        }

        beginTest("Knob 5 turns pages and each slot remembers its page");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            const auto first = model->currentPage().name;
            model->turnPageKnob(1);
            const auto second = model->currentPage().name;
            expect(first != second);
            model->turnMain(1);
            model->turnMain(-1);
            expectEquals(model->currentPage().name, second);
        }

        beginTest("Knobs 1-4 change the page's parameters and raise a pop-up");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false); // Shape: P1 is the ladder
            const auto id = model->currentPage().ids[0];
            auto* p = synth->getValueTreeState().getParameter(id);
            expect(p != nullptr);
            const float before = p->getValue();
            const int serial = model->popup().serial;
            model->turnKnob(1, 5, false);
            expectWithinAbsoluteError(p->getValue(), juce::jmin(1.0f, before + 0.05f), 1.0e-4f);
            expect(model->popup().serial > serial);
            expectEquals(model->popup().name, model->knob(1).name);
        }

        beginTest("A shift in semitones steps by whole semitones, a tenth with Shift (D205)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(3), false); // Effects: I1
            auto* type = synth->getValueTreeState().getParameter("pA.i1.type");
            type->setValueNotifyingHost(type->getValueForText("Pitch"));
            const auto id = model->currentPage().ids[0];
            expect(id.endsWith("i1.p1"));
            auto* shift = synth->getValueTreeState().getParameter(id);
            const auto semitones = [&] { return shift->getValue() * 48.0f - 24.0f; };
            shift->setValueNotifyingHost(0.75f); // an octave up
            expectWithinAbsoluteError(semitones(), 12.0f, 1.0e-4f);
            model->turnKnob(1, 1, false);
            expectWithinAbsoluteError(semitones(), 13.0f, 1.0e-4f);
            model->turnKnob(1, -6, false);
            expectWithinAbsoluteError(semitones(), 7.0f, 1.0e-4f);
            model->turnKnob(1, 1, true);
            expectWithinAbsoluteError(semitones(), 7.1f, 1.0e-4f);
            model->turnKnobFromDevice(1, 5, false); // a step every six counts, as a list
            expectWithinAbsoluteError(semitones(), 7.1f, 1.0e-4f);
            model->turnKnobFromDevice(1, 1, false);
            expectWithinAbsoluteError(semitones(), 8.0f, 1.0e-4f); // back on a whole semitone
            model->turnKnob(1, 40, false);
            expectWithinAbsoluteError(semitones(), 24.0f, 1.0e-4f);
        }

        beginTest("Past the instruments the Sample knob lists your files and kits; a press loads one (D207)");
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("CoolSynthBrowseTests_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
            auto& library = coolsynth::synth::v3::UserLibrary::instance();
            library.setFolder(folder);
            {
                auto synth = std::make_unique<SynthAudioProcessor>();
                auto model = std::make_unique<InterfaceModel>(*synth);
                model->pressPad(InterfaceModel::padOfSection(0), false); // Sound: G1
                auto* type = synth->getValueTreeState().getParameter("pA.g1.type");
                type->setValueNotifyingHost(type->getValueForText("Sample"));
                model->turnKnob(1, 500, false); // to the end of the list
                expect(model->knob(1).value.startsWith("No files yet: put WAV files in "), model->knob(1).value);
                expect(folder.isDirectory(), "the User folder is made, so there is a place for files");
                model->pressKnob(1);
                expect(synth->getUserFile(0, 1).isEmpty(), "the hint line loads nothing");

                folder.getChildFile("Breaks").createDirectory();
                folder.getChildFile("Breaks/amen.wav").replaceWithText("not audio, but a name");
                folder.getChildFile("vox.wav").replaceWithText("not audio, but a name");
                model->turnKnob(1, 500, false);
                expectEquals(model->knob(1).value, juce::String("vox.wav"), "the last of the user's entries");
                model->turnKnob(1, -1, false);
                expectEquals(model->knob(1).value, juce::String("Breaks/amen.wav"));
                model->turnKnob(1, -1, false);
                expectEquals(model->knob(1).value, juce::String("Breaks/ (kit)"), "a folder plays as a kit");
                model->pressKnob(1);
                expectEquals(synth->getUserFile(0, 1), juce::String("Breaks"));
                expectEquals(model->knob(1).value, juce::String("Breaks/ (kit)"), "the knob names what plays");

                model->turnKnob(1, -1, false); // back onto the last instrument
                expect(! model->knob(1).value.contains("/"), model->knob(1).value);
                model->pressKnob(1);
                expect(synth->getUserFile(0, 1).isEmpty(), "an instrument again lets the file go");
            }
            library.setFolder(juce::File());
            folder.deleteRecursively();
        }

        beginTest("A sampled slot's instrument knob only shows the choice; a press loads it (D156)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(0), false); // the Sound section (CoolSynth opens in Home, D186)
            auto* type = synth->getValueTreeState().getParameter("pA.g1.type");
            expect(type != nullptr);
            type->setValueNotifyingHost(type->getValueForText("Sample"));
            const auto id = model->currentPage().ids[0];
            expect(id.endsWith(".p1"));
            auto* instrument = synth->getValueTreeState().getParameter(id);
            const float before = instrument->getValue();
            model->turnKnob(1, 3, false);
            expectWithinAbsoluteError(instrument->getValue(), before, 1.0e-6f); // nothing read in while browsing
            expect(model->knob(1).pending);
            expect(model->choicePending());
            expectEquals(model->deviceScreen().line1, juce::String("Press to load"));
            expectEquals(model->mainPressRole(), juce::String("Load"));
            model->pressMain();
            expect(! model->choicePending());
            expect(std::abs(instrument->getValue() - before) > 1.0e-4f);
            expect(! model->typeListOpen()); // the press loaded; it did not open the type list

            model->turnKnob(1, 2, false);
            model->pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false); // leaving drops the choice
            expect(! model->choicePending());
            model->pressPad(coolsynth::ui::InterfaceModel::padOfSection(0), false);
            model->turnKnob(1, 1, false);
            model->pressKnob(1); // a click on the knob loads too
            expect(! model->choicePending());
        }

        beginTest("The type list opens on the current type; its ‹ Back row keeps the type");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(0), false); // the Sound section (CoolSynth opens in Home, D186)
            model->pressMain();
            expect(model->typeListOpen());
            auto list = model->list();
            expect(list.firstRowIsBack);
            expectEquals(list.cursor, list.used);
            expectEquals(list.rows[list.cursor], juce::String("VA"));
            model->turnMain(-20);
            expectEquals(model->mainPressRole(), juce::String("Back"));
            model->pressMain();
            expect(! model->typeListOpen());
            expectEquals(model->currentSlot().typeName, juce::String("VA"));
        }

        beginTest("Choosing a type sets it and gives the slot that type's defaults");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(0), false); // the Sound section (CoolSynth opens in Home, D186)
            model->turnMain(2); // G3, empty by default
            expect(model->currentSlot().empty);
            expect(! model->knob(1).active);
            model->pressMain();
            const auto rows = model->list().rows;
            model->turnMain(rows.indexOf("FM2") - model->list().cursor);
            model->pressMain();
            expectEquals(model->currentSlot().typeName, juce::String("FM2"));
            expect(! model->currentSlot().empty);
            expect(model->knob(1).active);
        }

        beginTest("Home opens on the playing sound; ‹ Back leads to the categories; a press loads");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->setCurrentProgram(0);
            auto model = std::make_unique<InterfaceModel>(*synth); // it opens in Home (D186); Home again would switch the layer
            expect(model->view() == InterfaceModel::View::home);
            expect(model->inCategory());
            auto list = model->list();
            expectEquals(list.cursor, list.used);
            expectEquals(model->mainPressRole(), juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")));
            expect(model->deviceScreen().line2.endsWith(" *"));

            model->turnMain(1);
            expectEquals(model->mainPressRole(), juce::String("Load"));
            model->pressMain();
            expectEquals(synth->getCurrentProgram(), 1);

            model->turnMain(-10);
            expectEquals(model->deviceScreen().line2, juce::String("< Back"));
            model->pressMain();
            expect(! model->inCategory());
            expectEquals(model->deviceScreen().line1, juce::String("Categories"));
            model->turnMain(3);
            model->pressMain();
            expect(model->inCategory());
            expectEquals(model->list().title, model->categories()[3].name.toUpperCase());
        }

        beginTest("Home's knob 5 turns the live pages: macros, then the arpeggiator");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            // CoolSynth opens in Home (D186); pressing Home there would switch to layer B, empty (D185, D190)
            expectEquals(model->knob(1).role, juce::String("Tone"));
            expect(model->pageTint() == coolsynth::ui::Tint::home);
            model->turnPageKnob(1);
            expectEquals(model->currentPage().ids[0], juce::String(coolsynth::parameters::ids::arpEnabled));
            expect(model->pageTint() == coolsynth::ui::Tint::perform);
        }

        beginTest("Home pressed again switches the part the sections edit, and lights in its colour (D185)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            expect(model->currentPage().ids[0].startsWith("pA."));
            model->pressPad(InterfaceModel::padOfSection(0), false); // a section first: CoolSynth opens in Home (D186)
            model->pressPad(InterfaceModel::homePad, false); // opens Home
            expectEquals(model->part(), 0);
            expect(model->padLight(InterfaceModel::homePad, false).tint == coolsynth::ui::Tint::home);
            model->pressPad(InterfaceModel::homePad, false); // again: part B
            expectEquals(model->part(), 1);
            expect(model->padLight(InterfaceModel::homePad, false).tint == coolsynth::ui::Tint::partB, "yellow for B");
            model->pressPad(InterfaceModel::padOfSection(1), false);
            expectEquals(model->currentPage().name, juce::String("empty"), "layer B starts empty: Shape has no slot in use (D190)");
            model->pressPad(InterfaceModel::padOfSection(2), false); // Motion: an empty layer still has its envelopes
            expect(model->currentPage().ids[0].startsWith("pB."), model->currentPage().ids[0]);
            model->pressPad(InterfaceModel::homePad, true); // from a section, Home opens Home: Shift does nothing on the pads
            expectEquals(model->part(), 1);
        }

        beginTest("A new CoolSynth starts on Contact; the Workshop's plain sounds are Basic (D193)");
        {
            const int start = SynthAudioProcessor::startSoundIndex();
            expect(start >= 0, "the start sound is in the bank");
            if (start >= 0)
                expectEquals(coolsynth::presets::getFactoryPreset(start).name, juce::String("Contact"));
            bool init = false, basic = false;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            {
                init = init || coolsynth::presets::getFactoryPreset(i).name.startsWith("Init ");
                basic = basic || coolsynth::presets::getFactoryPreset(i).name == "Basic Saw";
            }
            expect(! init && basic);
            // An untouched "Init" state an older version saved gives way to the start sound (D198); an edited one stays.
            expect(SynthAudioProcessor::isUntouchedDefault("Init", false));
            expect(! SynthAudioProcessor::isUntouchedDefault("Init", true));
            expect(! SynthAudioProcessor::isUntouchedDefault("Contact", false));

            // Reset the sound (D199): the start sound on A, B empty, faders at the top, the Layers page centred.
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->loadSoundIntoLayer(start >= 0 ? (start == 0 ? 1 : 0) : 0, 0);
            synth->loadSoundIntoLayer(start >= 0 ? (start == 0 ? 1 : 0) : 0, 1);
            auto set = [&](const char* id, float normalised) { synth->getValueTreeState().getParameter(id)->setValueNotifyingHost(normalised); };
            set(coolsynth::parameters::ids::volumeA, 0.3f);
            set("part.balance", 0.9f);
            synth->setSoloLayer(1);
            synth->resetSound();
            auto raw = [&](const char* id) { return synth->getValueTreeState().getRawParameterValue(id)->load(); };
            expect(synth->layerSound(0).name == "Contact");
            expectEquals(raw("pB.g1.type"), 0.0f, "layer B empty");
            expectEquals(synth->getValueTreeState().getParameter(coolsynth::parameters::ids::volumeA)->getValue(), 1.0f, "faders at the top");
            expectEquals(raw("part.balance"), 0.0f);
            expectEquals(synth->soloLayer(), -1);
        }

        beginTest("A saved state brings back the sound it holds, under its new name if renamed (D193)");
        {
            auto indexOf = [](const juce::String& name)
            {
                for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                    if (coolsynth::presets::getFactoryPreset(i).name == name)
                        return i;
                return -1;
            };
            auto source = std::make_unique<SynthAudioProcessor>();
            const int juno = indexOf("Juno Pad");
            source->setCurrentProgram(juno);
            juce::MemoryBlock state;
            source->getStateInformation(state);

            auto target = std::make_unique<SynthAudioProcessor>();
            target->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            expectEquals(target->playingFactoryProgram(), juno, "the screen names what plays, not program 0");
            expect(target->macroNames(0) == source->macroNames(0), "with its macro names");

            auto withName = [&](const juce::String& name)
            {
                auto root = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
                root->setAttribute("patchName", name);
                for (const char* key : { "layerA", "layerAFile", "layerAMacros", "layerB", "layerBFile", "layerBMacros" })
                    root->removeAttribute(key); // a state from before D194 names only the whole sound
                juce::MemoryBlock changed;
                juce::AudioProcessor::copyXmlToBinary(*root, changed);
                target->setStateInformation(changed.getData(), static_cast<int>(changed.getSize()));
            };
            withName("Init Saw");
            expectEquals(target->playingFactoryProgram(), indexOf("Basic Saw"));
            expectEquals(target->getPatchName(), juce::String("Basic Saw"));
            withName("Contact Opening II Grit");
            expectEquals(target->playingFactoryProgram(), indexOf("Contact"));
            withName("A sound the bank lacks");
            expectEquals(target->playingFactoryProgram(), -1, "no factory sound, rather than the wrong one");
            expect(target->macroNames(0)[0].isEmpty());
        }

        beginTest("Each layer holds a sound: a pick fills the layer Home is on; a combination loads whole (D194)");
        {
            auto indexOf = [](const juce::String& name)
            {
                for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                    if (coolsynth::presets::getFactoryPreset(i).name == name)
                        return i;
                return -1;
            };
            auto value = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };
            auto synth = std::make_unique<SynthAudioProcessor>();
            const int keys = indexOf("Cassette Keys"), bass = indexOf("Moog Bass"), juno = indexOf("Juno Pad");
            expect(keys >= 0 && bass >= 0 && juno >= 0);
            expect(SynthAudioProcessor::isCombination(juno) && ! SynthAudioProcessor::isCombination(keys));

            synth->loadSoundIntoLayer(keys, 0);
            const float keysLevel = value(*synth, "pA.mix.level");
            const float keysType = value(*synth, "pA.g1.type");
            synth->loadSoundIntoLayer(bass, 1);
            expectEquals(value(*synth, "pA.mix.level"), keysLevel, "layer A keeps its sound");
            expectEquals(value(*synth, "pA.g1.type"), keysType);
            expect(value(*synth, "pB.g1.type") != 0.0f, "layer B plays the bass");
            expect(synth->layerSound(0).name == "Cassette Keys" && synth->layerSound(1).name == "Moog Bass");
            expect(synth->macroNames(1) == coolsynth::presets::getFactoryPreset(bass).macroNames[0], "B shows the bass's macros");
            expectEquals(synth->getPatchName(), juce::String("Cassette Keys + Moog Bass"));

            // The state keeps both layers' sounds.
            juce::MemoryBlock state;
            synth->getStateInformation(state);
            auto restored = std::make_unique<SynthAudioProcessor>();
            restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            expect(restored->layerSound(0).program == keys && restored->layerSound(1).program == bass);

            // A combination loads whole, from either layer; a single pick then takes it apart.
            synth->loadSoundIntoLayer(juno, 1);
            expect(synth->layerSound(0).program == juno && synth->layerSound(1).program == juno);
            synth->loadSoundIntoLayer(bass, 0);
            expectEquals(value(*synth, "pB.g1.type"), 0.0f, "the combination's other layer empties");
            expect(synth->layerSound(1).name.isEmpty());

            // Home: the first row of the categories empties the layer it is on.
            synth->loadSoundIntoLayer(keys, 1);
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->togglePart(); // layer B
            model->back();       // up to the categories
            expectEquals(model->list().rows[0], juce::String("Empty layer B"));
            model->turnMain(-1000);
            model->pressMain();
            expectEquals(value(*synth, "pB.g1.type"), 0.0f, "layer B emptied");
            expect(value(*synth, "pA.g1.type") != 0.0f, "layer A plays on");
        }

        beginTest("The Layers page: balance, detune, transpose, spread; copy, swap, double, octave, solo, empty (D196)");
        {
            auto indexOf = [](const juce::String& name)
            {
                for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                    if (coolsynth::presets::getFactoryPreset(i).name == name)
                        return i;
                return -1;
            };
            auto value = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->loadSoundIntoLayer(indexOf("Cassette Keys"), 0);
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->turnPageKnob(InterfaceModel::layersPage); // knob 5 to the last live page
            expectEquals(model->currentPage().name, juce::String("Layers"));
            expectEquals(model->currentPage().ids[0], juce::String("part.balance"));
            expectEquals(model->currentPage().ids[2], juce::String("pB.mix.transpose"), "the other layer's transposition");
            expectEquals(model->list().title, juce::String("LAYERS"));
            expectEquals(model->list().rows[0], juce::String("Copy A to B"));

            auto doAction = [&](int row)
            {
                model->turnMain(-100);
                model->turnMain(row);
                model->pressMain();
            };
            doAction(0); // copy A to B
            expectEquals(value(*synth, "pB.g1.type"), value(*synth, "pA.g1.type"));
            expect(synth->layerSound(1).name == "Cassette Keys");

            const float levelA = value(*synth, "pA.mix.level");
            doAction(3); // double A onto B
            expectWithinAbsoluteError(value(*synth, "pA.mix.level"), levelA - 3.0f, 0.01f, "each copy 3 dB down");
            expectWithinAbsoluteError(value(*synth, "part.detune"), 14.0f, 0.01f);
            expectWithinAbsoluteError(value(*synth, "part.spread"), 0.5f, 0.01f);

            doAction(4); // an octave lower on B
            expectEquals(value(*synth, "pB.mix.transpose"), value(*synth, "pA.mix.transpose") - 12.0f);

            synth->loadSoundIntoLayer(indexOf("Moog Bass"), 1);
            doAction(2); // swap
            expect(synth->layerSound(0).name == "Moog Bass" && synth->layerSound(1).name == "Cassette Keys");

            doAction(6); // solo A
            expectEquals(synth->soloLayer(), 0);
            expectEquals(model->list().rows[6], juce::String("Solo A: on"));
            doAction(6);
            expectEquals(synth->soloLayer(), -1);

            doAction(7); // empty B
            expectEquals(value(*synth, "pB.g1.type"), 0.0f);
        }

        beginTest("A MiniLab knob moves a switch or a list a step per few counts, a number per count (D200)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->turnPageKnob(1); // Arp: knob 1 switches the layer's arp on and off
            auto* arp = synth->getValueTreeState().getParameter(model->currentPage().ids[0]);
            expectEquals(arp->getValue(), 0.0f);
            model->turnKnobFromDevice(1, InterfaceModel::countsPerSwitchStep - 1, false);
            expectEquals(arp->getValue(), 0.0f, "not yet");
            model->turnKnobFromDevice(1, 1, false);
            expectEquals(arp->getValue(), 1.0f, "a switch takes ten counts");
            model->turnKnobFromDevice(1, -3, false);
            model->turnKnobFromDevice(1, 3, false); // the other way and back: no step, no lingering count
            expectEquals(arp->getValue(), 1.0f);

            model->turnPageKnob(1); // Groove: knob 3, Chance, a number: a step per count
            auto* chance = synth->getValueTreeState().getParameter(model->currentPage().ids[2]);
            const float before = chance->getValue();
            model->turnKnobFromDevice(3, -1, false);
            expect(chance->getValue() < before, "a number moves at once");
        }

        beginTest("A knob that does nothing now shows dimmed, with the reason, and still turns (D201)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth); // layer B empty
            model->turnPageKnob(InterfaceModel::layersPage);
            expect(model->knob(2).idle, "Detune does nothing beside an empty layer");
            expectEquals(model->knob(3).why, juce::String("B empty"));
            const float before = synth->getValueTreeState().getParameter("part.detune")->getValue();
            model->turnKnob(2, 5, false);
            expect(synth->getValueTreeState().getParameter("part.detune")->getValue() > before, "but it still turns");

            model->turnPageKnob(-(InterfaceModel::layersPage - 1)); // the Arp page
            expect(! model->knob(1).idle, "the arp switch itself always acts");
            expectEquals(model->knob(2).why, juce::String("arp off"));
            model->turnKnob(1, 1, false); // the arp on
            expect(! model->knob(2).idle);
            expect(model->knob(6).why.isEmpty(), "Glide acts on a layer that plays");
        }

        beginTest("An empty layer's Home says so; the MiniLab's keys follow the notes it plays (D192)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            expect(! model->currentLayerEmpty(), "layer A plays");
            model->togglePart();
            expect(model->currentLayerEmpty(), "layer B starts empty (D190)");
            expectEquals(model->currentPage().name, juce::String("Layer B is empty"));
            expectEquals(model->knob(1).name, juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")), "a dash, no fake name");
            expect(! model->knob(1).active);

            expectEquals(model->miniLabLowKey(), 48, "C3 to C5 until a key says otherwise");
            model->keyPlayedOnMiniLab(60);
            expectEquals(model->miniLabLowKey(), 48, "inside: nothing moves");
            model->keyPlayedOnMiniLab(80);
            expectEquals(model->miniLabLowKey(), 60, "one octave up holds it");
            model->keyPlayedOnMiniLab(40);
            expectEquals(model->miniLabLowKey(), 36, "down as little as holds it");
            model->keyPlayedOnMiniLab(0);
            expectEquals(model->miniLabLowKey(), 0, "four octaves down at most");
        }

        beginTest("The faders are volumes over the sound: at the top, taken as they are, kept across sounds (D184)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            for (int f = 0; f < 4; ++f)
                expectWithinAbsoluteError(model->faderValue(f), 1.0f, 1.0e-6f, "they start at the top: the sound as designed");
            model->moveFader(3, 0.05f, true); // the MiniLab's fader, far from the top: taken as it is
            expectWithinAbsoluteError(model->faderValue(3), 0.05f, 1.0e-3f);
            model->moveFader(0, 0.5f, false);
            synth->setCurrentProgram(3); // another sound
            expectWithinAbsoluteError(model->faderValue(0), 0.5f, 1.0e-3f, "a sound does not move the faders");
            expectWithinAbsoluteError(model->faderValue(3), 0.05f, 1.0e-3f);
            auto* volume = synth->getValueTreeState().getParameter(coolsynth::parameters::ids::volumeA);
            expectEquals(volume->getCurrentValueAsText(), juce::String("-18.1 dB"), "half way, as an audio taper");
            volume->setValueNotifyingHost(1.0f);
            expectEquals(volume->getCurrentValueAsText(), juce::String("0.0 dB"));
            volume->setValueNotifyingHost(0.0f);
            expectEquals(volume->getCurrentValueAsText(), juce::String("off"));
        }

        beginTest("Hold and the octave work on screen without a MiniLab, not with one (D184)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            expect(! model->hardwareConnected());
            model->toggleHold();
            expect(model->holdOn());
            model->shiftOctave(1);
            model->shiftOctave(5); // held at +3
            expectEquals(model->octaveShift(), InterfaceModel::maxOctaveShift);
            model->setHardwareConnected(true);
            expect(! model->holdOn(), "the MiniLab has its own Hold: the screen's lets go");
            expectEquals(model->octaveShift(), 0);
        }

        beginTest("The pads: Home first, Sound, Shape, Motion, the recorder, Effects last (D184)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            const juce::StringArray expected { "Home: A", "Sound", "Shape", "Motion", "Stop", "Play", "Record", "Effects" };
            for (int p = 0; p < 8; ++p)
                expectEquals(model->padLabel(p, false), expected[p]);
            expectEquals(model->padLabel(7, true), juce::String("Effects"), "no Shift on the pads: Shift + pads 1-3 are the MiniLab's own");
            expectEquals(model->padLabel(0, true), juce::String("Home: A"));
            model->pressPad(7, false);
            expectEquals(model->section(), 3, "the last pad opens Effects");
            model->pressPad(0, false);
            expect(model->view() == InterfaceModel::View::home, "the first pad is Home");
            // The MiniLab's LEDs get their own colours, set by eye on the device: Home a bright teal, Shape a clear blue.
            expectEquals(coolsynth::ui::miniLabColour(coolsynth::ui::Tint::home).getBrightness(), 1.0f);
            expect(coolsynth::ui::miniLabColour(coolsynth::ui::Tint::shape).getBlue() > coolsynth::ui::miniLabColour(coolsynth::ui::Tint::shape).getGreen() * 2);
            expect(coolsynth::ui::miniLabColour(coolsynth::ui::Tint::record) == coolsynth::ui::tintColour(coolsynth::ui::Tint::record));
        }

        beginTest("The MiniLab screen in a section: where small, the slot big, no counts");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(InterfaceModel::padOfSection(0), false); // the Sound section (CoolSynth opens in Home, D186)
            const auto screen = model->deviceScreen();
            expect(screen.line1.startsWith("Sound - "));
            expectEquals(screen.line2, juce::String("G1 VA"));
            expect(! screen.line1.containsChar('/'));
            expectEquals(screen.position, 0);
        }
    }
};

static InterfaceModelTests interfaceModelTests;

/* The MiniLab 3's DAW program: the bytes tested on the device (docs/minilab3-screen.md). */
class MiniLab3ProtocolTests final : public juce::UnitTest
{
public:
    MiniLab3ProtocolTests() : juce::UnitTest("MiniLab3Protocol", "CoolSynth") {}

    void runTest() override
    {
        namespace ml = coolsynth::midi::minilab3;
        auto bytes = [](const juce::MidiMessage& m)
        {
            juce::String s;
            for (int i = 0; i < m.getRawDataSize(); ++i)
                s << juce::String::toHexString(m.getRawData()[i]).paddedLeft('0', 2).toUpperCase() << " ";
            return s.trim();
        };

        beginTest("Connect, pad colour and the scroll screen are the bytes that worked on the device");
        expectEquals(bytes(ml::connect()), juce::String("F0 00 20 6B 7F 42 02 00 40 6A 21 F7"));
        expectEquals(bytes(ml::padColour(0, juce::Colour(0xff, 0xa2, 0x3a))), juce::String("F0 00 20 6B 7F 42 02 02 16 34 7F 51 1D F7"));
        expectEquals(bytes(ml::scrollScreen("Outrun", "Neon Brass *", 0)),
                     juce::String("F0 00 20 6B 7F 42 04 02 60 1F 06 00 00 00 7F 00 00 01 4F 75 74 72 75 6E 00 02 "
                                  "4E 65 6F 6E 20 42 72 61 73 73 20 2A 00 F7"));
        expect(bytes(ml::popup(false, "Cutoff", "1.2 kHz", 0.5f)).startsWith("F0 00 20 6B 7F 42 04 02 60 1F 03 02 40 00 00 01"));

        beginTest("Screen text keeps to printable ASCII and the line widths");
        const auto text = ml::screenText(juce::String(juce::CharPointer_UTF8("Sound \xc2\xb7 Wave and much more text")), 16);
        expectEquals(static_cast<int>(text.size()), 16);
        expectEquals(static_cast<int>(text[6]), static_cast<int>('-'));

        beginTest("Decoding: knobs, main knob, press, faders, pads on channel 10; keys pass through");
        auto kindOf = [](const juce::MidiMessage& m) { return ml::decode(m).kind; };
        using Kind = ml::Control::Kind;
        expect(kindOf(juce::MidiMessage::controllerEvent(1, 86, 40)) == Kind::knob);
        expectEquals(ml::decode(juce::MidiMessage::controllerEvent(1, 117, 40)).index, 7);
        expectEquals(ml::decode(juce::MidiMessage::controllerEvent(1, 28, 62)).value, -1); // one click left
        expectEquals(ml::decode(juce::MidiMessage::controllerEvent(1, 28, 61)).value, -2);
        expectEquals(ml::decode(juce::MidiMessage::controllerEvent(1, 28, 65)).value, 1);  // one click right
        expect(ml::decode(juce::MidiMessage::controllerEvent(1, 27, 127)).kind == Kind::shift);
        expect(kindOf(juce::MidiMessage::controllerEvent(1, 118, 127)) == Kind::mainPress);
        expectEquals(ml::decode(juce::MidiMessage::controllerEvent(1, 31, 100)).index, 3);
        expectEquals(ml::decode(juce::MidiMessage::noteOn(10, 43, 0.8f)).index, 7);
        expect(kindOf(juce::MidiMessage::noteOn(1, 43, 0.8f)) == Kind::other);   // a key
        expect(kindOf(juce::MidiMessage::controllerEvent(1, 1, 64)) == Kind::other); // mod wheel
        expect(kindOf(juce::MidiMessage::pitchWheel(1, 9000)) == Kind::other);
        expect(ml::isDawPort("Minilab3 MIDI"));
        expect(! ml::isDawPort("Minilab3 DIN THRU"));
        expect(! ml::isDawPort("Minilab3 ALV"));
    }
};

static MiniLab3ProtocolTests miniLab3ProtocolTests;

/* Draws the V3 panel in a few states to PNG files in the build folder, to look at (build/panel-*.png). */
class PanelSnapshotTests final : public juce::UnitTest
{
public:
    PanelSnapshotTests() : juce::UnitTest("PanelSnapshot", "CoolSynth") {}

    void runTest() override
    {
        beginTest("The panel draws in its main states");
        const juce::ScopedJuceInitialiser_GUI gui;
        auto synth = std::make_unique<SynthAudioProcessor>();
        auto& model = synth->getInterfaceModel();
        std::unique_ptr<juce::AudioProcessorEditor> editor(synth->createEditor());
        expect(editor != nullptr);
        const auto folder = juce::File::getCurrentWorkingDirectory();
        auto shoot = [&](const juce::String& state)
        {
            if (auto* panel = dynamic_cast<PanelEditor*>(editor.get()))
                panel->refresh();
            auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out(folder.getChildFile("panel-" + state + ".png"));
            out.setPosition(0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream(image, out);
            expect(image.isValid());
        };
        shoot("sound");
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false);
        shoot("home");
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false);
        shoot("shape"); // the Shape section in its colour (D184)
        model.pressMain();
        shoot("typelist");
        model.pressMain();
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(3), false);
        model.turnMain(3);
        shoot("effects");
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false);
        model.turnKnob(1, 6, false); // a macro, and Brightness: their values show in the knobs (D183)
        model.turnKnob(7, 4, false);
        shoot("knobvalue");
    }
};

static PanelSnapshotTests panelSnapshotTests;

// ---- Engine pills in the sound browser (D169) ---------------------------------------------------------------

class EnginePillTests final : public juce::UnitTest
{
public:
    EnginePillTests() : juce::UnitTest("Engine pills name what makes each sound", "Interface") {}

    void runTest() override
    {
        const auto named = [](const juce::String& wanted)
        {
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == wanted)
                    return coolsynth::ui::enginePills(i).joinIntoString(" ");
            return juce::String("(no such preset)");
        };

        beginTest("The examples of the specification, and one of each engine");
        expectEquals(named("Frozen Pluck"), juce::String("Physical Grain"));
        expectEquals(named("Steinway Grand"), juce::String("Sample"));
        expectEquals(named("Basic Saw"), juce::String("VA"));
        expectEquals(named("Basic FM"), juce::String("FM"));
        expectEquals(named("Basic Wavetable"), juce::String("WT"));
        expectEquals(named("Future Chord"), juce::String("WT"));
        expectEquals(named("Siren"), juce::String("Filter")); // no generator: the ladder rings on its own

        beginTest("Every preset has one to three, no repeats, and Noise only alone");
        std::map<juce::String, int> counts;
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
        {
            const auto pills = coolsynth::ui::enginePills(i);
            const auto& preset = coolsynth::presets::getFactoryPreset(i).name;
            expect(pills.size() >= 1 && pills.size() <= 3, preset + ": " + pills.joinIntoString(" "));
            juce::StringArray unique(pills);
            unique.removeDuplicates(false);
            expectEquals(unique.size(), pills.size(), preset);
            expect(! pills.contains("Noise") || pills.size() == 1, preset + ": Noise only when it is the sound");
            expect(! pills.contains("Filter") || pills.size() == 1, preset + ": Filter only when no generator plays");
            for (const auto& pill : pills)
                ++counts[pill];
        }
        juce::String summary;
        for (const auto& [pill, count] : counts)
            summary << pill << " " << count << ", ";
        logMessage("presets per pill: " + summary.dropLastCharacters(2));
        expectGreaterThan(counts["Sample"], 39, "the Conservatory and more");

        beginTest("The sound list carries them, row for row; the categories have none");
        auto synth = std::make_unique<SynthAudioProcessor>();
        auto& model = synth->getInterfaceModel();
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false); // Home: the playing sound's category
        auto view = model.list();
        expectEquals(static_cast<int>(view.pills.size()), view.rows.size());
        expect(view.pills.front().isEmpty(), "the Back row has none");
        for (std::size_t r = 1; r < view.pills.size(); ++r)
            expect(! view.pills[r].isEmpty(), view.rows[static_cast<int>(r)]);
        model.turnMain(-100); // up to "Back to categories"
        model.pressMain();
        view = model.list();
        expectEquals(view.title, juce::String("CATEGORIES"));
        for (const auto& row : view.pills)
            expect(row.isEmpty(), "a category row");
    }
};

static EnginePillTests enginePillTests;

// ---- A bulk edit reaches the audio whole (D157) -------------------------------------------------------------

class BulkEditTests final : public juce::UnitTest
{
public:
    BulkEditTests() : juce::UnitTest("Bulk edits reach the audio whole", "Interface") {}

    void runTest() override
    {
        beginTest("While a bulk edit runs the audio keeps the settings it had; after it, the new ones");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->setPlayConfigDetails(0, 2, 48000.0, 256);
            synth->prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            auto play = [&](int blocks)
            {
                float level = 0.0f;
                for (int i = 0; i < blocks; ++i)
                {
                    buffer.clear();
                    synth->processBlock(buffer, midi);
                    midi.clear();
                    level = std::max(level, buffer.getMagnitude(0, 0, 256));
                }
                return level;
            };
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            play(40);
            const float before = play(10);
            expectGreaterThan(before, 0.01f);

            auto* master = synth->getValueTreeState().getParameter(coolsynth::parameters::ids::masterGainDb);
            {
                const SynthAudioProcessor::BulkEdit whole(*synth);
                master->setValueNotifyingHost(master->convertTo0to1(-50.0f));
                const float during = play(10);
                expectGreaterThan(during, before * 0.7f, "the master gain is held while the edit is under way");
            }
            play(10); // the gain glides down over 20 ms
            const float after = play(10);
            expectLessThan(after, before * 0.05f, "and taken once it is whole");
        }
    }
};

static BulkEditTests bulkEditTests;

// ---- Options (D174) -----------------------------------------------------------------------------------------

class OptionsTests final : public juce::UnitTest
{
public:
    OptionsTests() : juce::UnitTest("Options: Multi-core and the CPU line", "Interface") {}

    void runTest() override
    {
        beginTest("Multi-core chosen in Options is applied and handed to where the app keeps it");
        auto synth = std::make_unique<SynthAudioProcessor>();
        juce::Array<bool> remembered;
        synth->rememberMultiCore = [&](bool on) { remembered.add(on); };
        synth->chooseMultiCore(false);
        expect(! synth->multiCore());
        synth->chooseMultiCore(true);
        expect(synth->multiCore());
        expectEquals(remembered.size(), 2);
        expect(! remembered[0] && remembered[1]);

        beginTest("The CPU line says the audio thread's share and the slowest block, after a second of sound");
        synth->setPlayConfigDetails(0, 2, 48000.0, 256);
        synth->prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        for (int b = 0; b < 200; ++b)
        {
            buffer.clear();
            synth->processBlock(buffer, midi);
            midi.clear();
        }
        const auto line = PanelEditor::cpuSummary(*synth);
        logMessage(line);
        expect(line.startsWith("CPU ") && line.contains("slowest block"), line);
        expectGreaterThan(synth->cpuLoad().percent[0], 0.0f);

        beginTest("The velocity curve: Soft lifts light touches, Hard holds them back, both keep the ends (D175)");
        {
            using P = SynthAudioProcessor;
            const float light = 32.0f / 127.0f;
            expectWithinAbsoluteError(P::shapeVelocity(light, P::normalCurve), light, 1.0e-6f);
            expectGreaterThan(P::shapeVelocity(light, P::softCurve), light);
            expectLessThan(P::shapeVelocity(light, P::hardCurve), light);
            for (const int curve : { 0, 1, 2 })
            {
                expectWithinAbsoluteError(P::shapeVelocity(1.0f, curve), 1.0f, 1.0e-6f);
                expectGreaterOrEqual(P::shapeVelocity(1.0f / 127.0f, curve), 1.0f / 127.0f, "never 0: that would end the note");
                float previous = 0.0f;
                for (int v = 1; v <= 127; ++v)
                {
                    const float shaped = P::shapeVelocity(static_cast<float>(v) / 127.0f, curve);
                    expect(shaped >= previous, "rises with the touch");
                    previous = shaped;
                }
            }
            auto keyboard = std::make_unique<SynthAudioProcessor>();
            juce::Array<int> curves;
            keyboard->rememberVelocityCurve = [&](int curve) { curves.add(curve); };
            keyboard->chooseVelocityCurve(P::hardCurve);
            expectEquals(keyboard->velocityCurve(), static_cast<int>(P::hardCurve));
            expectEquals(curves.size(), 1);
            logMessage("velocity 32 plays as " + juce::String(juce::roundToInt(127.0f * P::shapeVelocity(light, P::softCurve))) + " (Soft), "
                       + juce::String(juce::roundToInt(127.0f * P::shapeVelocity(light, P::hardCurve))) + " (Hard)");
        }
    }
};

static OptionsTests optionsTests;

// ---- Knobs 7 and 8: Brightness and Space (D176) ----------------------------------------------------------

class BrightnessSpaceTests final : public juce::UnitTest
{
public:
    BrightnessSpaceTests() : juce::UnitTest("Brightness and Space", "Interface") {}

    void runTest() override
    {
        // Renders a held note (then its release), with knob 7 or 8 at a value, and returns the left channel.
        const auto render = [](const char* knob, float value, bool withReverb)
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->setPlayConfigDetails(0, 2, 48000.0, 256);
            synth->prepareToPlay(48000.0, 256);
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == "Basic Saw")
                    synth->setCurrentProgram(i);
            auto& state = synth->getValueTreeState();
            const auto set = [&](const char* id, float plain)
            {
                auto* p = state.getParameter(id);
                p->setValueNotifyingHost(p->convertTo0to1(plain));
            };
            state.getParameter("pA.p1.p1")->setValueNotifyingHost(0.566f); // the ladder at 1 kHz, so Brightness can go either way
            if (withReverb)
            {
                set("f1.type", static_cast<float>(coolsynth::synth::v3::insertType::reverb));
                state.getParameter("f1.p1")->setValueNotifyingHost(0.6f); // size
                state.getParameter("f1.p3")->setValueNotifyingHost(0.3f); // mix
            }
            set(knob, value);
            std::vector<float> out;
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
            for (int b = 0; b < 375; ++b) // 2 s; the key let go after 0.5 s
            {
                if (b == 94)
                    midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
                buffer.clear();
                synth->processBlock(buffer, midi);
                midi.clear();
                out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 256);
            }
            return out;
        };
        const auto rms = [](const std::vector<float>& x, std::size_t from, std::size_t to)
        {
            double sum = 0.0;
            for (auto i = from; i < to; ++i)
                sum += static_cast<double>(x[i]) * x[i];
            return std::sqrt(sum / static_cast<double>(to - from));
        };
        // How bright: the first difference's level against the signal's, while the note is held.
        const auto bright = [&](const std::vector<float>& x)
        {
            std::vector<float> d(x.size());
            for (std::size_t i = 1; i < x.size(); ++i)
                d[i] = x[i] - x[i - 1];
            return rms(d, 4800, 24000) / std::max(1.0e-9, rms(x, 4800, 24000));
        };

        beginTest("Brightness opens and closes the filter; in the middle the sound is as designed");
        {
            const double dark = bright(render(coolsynth::parameters::ids::brightness, -1.0f, false));
            const double asDesigned = bright(render(coolsynth::parameters::ids::brightness, 0.0f, false));
            const double open = bright(render(coolsynth::parameters::ids::brightness, 1.0f, false));
            logMessage("brightness -1, 0, +1: " + juce::String(dark, 4) + ", " + juce::String(asDesigned, 4) + ", " + juce::String(open, 4));
            expectLessThan(dark, asDesigned);
            expectGreaterThan(open, asDesigned);
        }

        beginTest("Space makes the reverb's tail louder or softer");
        {
            const auto tail = [&](float value)
            {
                const auto x = render(coolsynth::parameters::ids::space, value, true);
                return rms(x, 24000 + 14400, 24000 + 38400); // 0.3 to 0.8 s after the key was let go
            };
            const double dry = tail(-1.0f), asDesigned = tail(0.0f), wet = tail(1.0f);
            logMessage("tail at space -1, 0, +1: " + juce::String(juce::Decibels::gainToDecibels(dry), 1) + ", "
                       + juce::String(juce::Decibels::gainToDecibels(asDesigned), 1) + ", " + juce::String(juce::Decibels::gainToDecibels(wet), 1) + " dB");
            expectLessThan(dry, asDesigned);
            expectGreaterThan(wet, asDesigned);
        }

        beginTest("Knobs 7 and 8 turn Brightness and Space, on every page");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto& model = synth->getInterfaceModel();
            auto& state = synth->getValueTreeState();
            for (const int knob : { 7, 8 })
            {
                const char* id = knob == 7 ? coolsynth::parameters::ids::brightness : coolsynth::parameters::ids::space;
                const float before = state.getRawParameterValue(id)->load();
                expectWithinAbsoluteError(before, 0.0f, 1.0e-6f, "the middle: the sound as designed");
                expect(model.knob(knob).active);
                expectEquals(model.knob(knob).value, juce::String("as the sound"));
                model.turnKnob(knob, 5, false);
                expectGreaterThan(state.getRawParameterValue(id)->load(), before);
                model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false); // another page: the same knob
                expectEquals(model.knob(knob).name, juce::String(knob == 7 ? "Brightness" : "Space"));
            }
        }
    }
};

static BrightnessSpaceTests brightnessSpaceTests;

// ---- My Sounds (D180) -------------------------------------------------------------------------------------

class MySoundsTests final : public juce::UnitTest
{
public:
    MySoundsTests() : juce::UnitTest("My Sounds: save, browse, load", "Interface") {}

    void runTest() override
    {
        using coolsynth::presets::MySounds;
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth My Sounds test");
        folder.deleteRecursively();
        MySounds::setFolderForTesting(folder);
        const auto named = [](const juce::String& wanted)
        {
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == wanted)
                    return i;
            return 0;
        };

        beginTest("A name a file may have");
        expectEquals(MySounds::cleanName("  Brass: big/bright?  "), juce::String("Brass bigbright"));
        expect(MySounds::cleanName("///").isEmpty());

        beginTest("Saving the sound as it is, refusing to overwrite unless asked, and loading it back");
        auto synth = std::make_unique<SynthAudioProcessor>();
        synth->setCurrentProgram(named("Neon Brass"));
        auto* macro = synth->getValueTreeState().getParameter("pA.macro1");
        macro->setValueNotifyingHost(0.5f);
        expect(synth->saveMySound("My Brass", false) == SynthAudioProcessor::SaveResult::saved);
        const auto file = MySounds::fileFor("My Brass");
        expect(file.existsAsFile(), file.getFullPathName());
        expect(synth->saveMySound("My Brass", false) == SynthAudioProcessor::SaveResult::exists);
        expect(synth->saveMySound("My Brass", true) == SynthAudioProcessor::SaveResult::saved);
        expect(synth->playingMySound() == file);
        expectEquals(synth->getPatchName(), juce::String("My Brass"));
        const auto factoryNames = coolsynth::presets::getFactoryPreset(named("Neon Brass")).macroNames[0];

        synth->setCurrentProgram(named("Basic Saw"));
        expect(synth->playingMySound() == juce::File(), "a factory sound again");
        expectWithinAbsoluteError(macro->getValue(), 0.0f, 1.0e-6f);
        expect(synth->loadMySound(file));
        expectWithinAbsoluteError(macro->getValue(), 0.5f, 1.0e-3f, "the macro as it was saved");
        expect(synth->playingMySound() == file);
        expectEquals(synth->getPatchName(), juce::String("My Brass"));
        expectEquals(synth->macroNames(0)[0], factoryNames[0], "the macro names travel with the sound");
        expect(! SynthAudioProcessor::mySoundPills(file).isEmpty(), "and its pills");

        beginTest("My Sounds is the first room in Home, its sounds load from there");
        auto& model = synth->getInterfaceModel();
        synth->setCurrentProgram(named("Basic Saw"));
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(0), false);
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false); // Home from a section: refreshes the rooms
        expectEquals(model.categories().front().name, juce::String("My Sounds"));
        model.turnMain(-100);
        model.pressMain(); // back to the rooms
        model.turnMain(-100); // the top: "Empty layer A" (D194)
        model.turnMain(1);    // the first room: My Sounds
        model.pressMain();
        auto view = model.list();
        expectEquals(view.title, juce::String("MY SOUNDS"));
        expectEquals(view.rows[1], juce::String("My Brass"));
        expect(! view.pills[1].isEmpty());
        model.turnMain(1);
        model.pressMain(); // loads it
        expect(synth->playingMySound() == file);
        expectEquals(model.list().used, 1, "marked as playing");

        MySounds::setFolderForTesting({});
        folder.deleteRecursively();
    }
};

static MySoundsTests mySoundsTests;

// ---- Browsing with a mouse and a computer keyboard, without a MiniLab (D181) -------------------------

class MouseBrowsingTests final : public juce::UnitTest
{
public:
    MouseBrowsingTests() : juce::UnitTest("Browsing with the mouse and keys", "Interface") {}

    void runTest() override
    {
        const juce::ScopedJuceInitialiser_GUI gui;
        auto synth = std::make_unique<SynthAudioProcessor>();
        auto& model = synth->getInterfaceModel();

        beginTest("Back goes up one level from a sound list, a type list and a section");
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false); // Home, in the playing sound's room
        expect(model.list().title != "CATEGORIES");
        model.back();
        expectEquals(model.list().title, juce::String("CATEGORIES"));
        model.back(); // already at the top: stays
        expectEquals(model.list().title, juce::String("CATEGORIES"));
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false); // Shape
        model.pressMain();        // its type list
        expect(model.typeListOpen());
        model.back();
        expect(! model.typeListOpen(), "the type list closed");
        expect(model.view() == coolsynth::ui::InterfaceModel::View::section);
        model.back();
        expect(model.view() == coolsynth::ui::InterfaceModel::View::home, "a section goes Home");

        beginTest("The trail's items go where they name");
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false);
        const auto room = model.list().title;
        model.clickTrail(0); // Categories
        expectEquals(model.list().title, juce::String("CATEGORIES"));
        model.pressMain(); // into the room the cursor is on
        model.clickTrail(0);
        expectEquals(model.list().title, juce::String("CATEGORIES"));
        const int page = model.livePage();
        model.clickTrail(static_cast<int>(model.trail().size()) - 1); // the page item: the next page
        expectEquals(model.livePage(), (page + 1) % 5);
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(2), false); // Motion
        model.clickTrail(0); // the sound's name: Home
        expect(model.view() == coolsynth::ui::InterfaceModel::View::home);
        juce::ignoreUnused(room);

        beginTest("A slot clicked again opens its type list");
        model.pressPad(coolsynth::ui::InterfaceModel::padOfSection(1), false); // Shape
        model.clickSlot(1);
        expect(! model.typeListOpen(), "the first click chooses the slot");
        model.clickSlot(1);
        expect(model.typeListOpen(), "the second opens its types");
        model.back();

        beginTest("The wheel scrolls the screen's list, and a right click goes back, on the panel itself");
        std::unique_ptr<juce::AudioProcessorEditor> editor(synth->createEditor());
        auto* panel = dynamic_cast<PanelEditor*>(editor.get());
        expect(panel != nullptr);
        model.pressPad(coolsynth::ui::InterfaceModel::homePad, false);
        model.back(); // the rooms: a list of thirteen
        panel->refresh();
        (void) editor->createComponentSnapshot(editor->getLocalBounds()); // draws once: the screen knows its areas
        juce::Component* screen = editor->getChildComponent(0)->getChildComponent(0); // the screen is the panel's first
        const auto centre = screen->getLocalBounds().getCentre().toFloat();
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto event = [&](juce::ModifierKeys mods)
        {
            return juce::MouseEvent(source, centre, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, screen, screen, juce::Time::getCurrentTime(),
                                    centre, juce::Time::getCurrentTime(), 1, false);
        };
        const int before = model.list().cursor;
        juce::MouseWheelDetails down {};
        down.deltaY = -0.35f; // three notches toward the user
        screen->mouseWheelMove(event({}), down);
        const int after = model.list().cursor;
        logMessage("wheel: cursor " + juce::String(before) + " -> " + juce::String(after));
        expectGreaterThan(after, before, "the wheel moved down the list");
        juce::MouseWheelDetails up {};
        up.deltaY = 0.35f;
        screen->mouseWheelMove(event({}), up);
        expectEquals(model.list().cursor, before, "and back up");
        model.pressMain(); // into a room
        expect(model.list().title != "CATEGORIES");
        screen->mouseDown(event(juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
        expectEquals(model.list().title, juce::String("CATEGORIES"), "a right click went back");

        beginTest("Keys: up and down move through a list, Escape goes back");
        const int top = model.list().cursor;
        expect(editor->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
        expectEquals(model.list().cursor, top + 1);
        model.pressMain();
        expect(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        expectEquals(model.list().title, juce::String("CATEGORIES"));
    }
};

static MouseBrowsingTests mouseBrowsingTests;
