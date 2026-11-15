#include <memory>

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "interface/InterfaceModel.h"
#include "midi/MiniLab3Protocol.h"
#include "plugin/PanelEditor.h"
#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"

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

        beginTest("Starts in the Sound section on G1, with knobs on its first page");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
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
            model->turnMain(1);
            expectEquals(model->currentSlot().code, juce::String("G2"));
            model->turnMain(10);
            expectEquals(model->currentSlot().code, juce::String("G3"));
            model->pressPad(0, false);
            expectEquals(model->currentSlot().code, juce::String("G1"));
            model->pressPad(3, false);
            expectEquals(model->section(), 3);
            expectEquals(model->currentSlot().code, juce::String("I1"));
            expectEquals(static_cast<int>(model->slotsOf(3).size()), 9);
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
            model->pressPad(1, false); // Shape: P1 is the ladder
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

        beginTest("A sampled slot's instrument knob only shows the choice; a press loads it (D156)");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
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
            model->pressPad(1, false); // leaving drops the choice
            expect(! model->choicePending());
            model->pressPad(0, false);
            model->turnKnob(1, 1, false);
            model->pressKnob(1); // a click on the knob loads too
            expect(! model->choicePending());
        }

        beginTest("The type list opens on the current type; its ‹ Back row keeps the type");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
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
            auto model = std::make_unique<InterfaceModel>(*synth);
            model->pressPad(7, false);
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
            model->pressPad(7, false);
            expectEquals(model->knob(1).role, juce::String("Tone"));
            expect(model->pageTint() == coolsynth::ui::Tint::home);
            model->turnPageKnob(1);
            expectEquals(model->currentPage().ids[0], juce::String(coolsynth::parameters::ids::arpEnabled));
            expect(model->pageTint() == coolsynth::ui::Tint::perform);
        }

        beginTest("Shift + pad 8 switches the part the sections edit");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            expect(model->currentPage().ids[0].startsWith("pA."));
            model->pressPad(7, true);
            expectEquals(model->part(), 1);
            expect(model->currentPage().ids[0].startsWith("pB."));
        }

        beginTest("A fader with pickup takes over only once it meets the stored value");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
            const float stored = model->faderValue(3);
            const float far = stored > 0.5f ? 0.05f : 0.95f;
            model->moveFader(3, far, true);
            expectWithinAbsoluteError(model->faderValue(3), stored, 1.0e-4f);
            model->moveFader(3, stored + (stored > 0.5f ? 0.01f : -0.01f), true); // crosses the stored value
            model->moveFader(3, 0.5f, true);
            expectWithinAbsoluteError(model->faderValue(3), 0.5f, 1.0e-3f);
        }

        beginTest("The MiniLab screen in a section: where small, the slot big, no counts");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            auto model = std::make_unique<InterfaceModel>(*synth);
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
        model.pressPad(7, false);
        shoot("home");
        model.pressPad(1, false);
        model.pressMain();
        shoot("typelist");
        model.pressMain();
        model.pressPad(3, false);
        model.turnMain(3);
        shoot("effects");
    }
};

static PanelSnapshotTests panelSnapshotTests;
