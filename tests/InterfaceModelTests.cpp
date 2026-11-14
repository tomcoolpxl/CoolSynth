#include <memory>

#include <juce_core/juce_core.h>

#include "interface/InterfaceModel.h"
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
