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
        expectEquals(named("Init Saw"), juce::String("VA"));
        expectEquals(named("Init FM"), juce::String("FM"));
        expectEquals(named("Init Wavetable"), juce::String("WT"));
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
        model.pressPad(7, false); // Home: the playing sound's category
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
    }
};

static OptionsTests optionsTests;
