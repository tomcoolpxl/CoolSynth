#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>

#include "parameters/v3/SlotParameters.h"

namespace
{
    using namespace coolsynth::parameters::v3;
    using coolsynth::synth::v3::ModuleDescriptor;
    using coolsynth::synth::v3::Skew;
    using coolsynth::synth::v3::SlotTypeTable;
    using coolsynth::synth::v3::Unit;

    const ModuleDescriptor& filterDescriptor()
    {
        static const ModuleDescriptor d { "Filter", 2,
                                          {{ { "cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f, Skew::logarithmic, Unit::hertz, true },
                                             { "res", "Resonance", 0.0f, 1.0f, 0.2f, Skew::linear, Unit::generic, true } }},
                                          2 };
        return d;
    }

    const ModuleDescriptor& shaperDescriptor()
    {
        static const ModuleDescriptor d { "Shaper", 1,
                                          {{ { "drive", "Drive", -12.0f, 24.0f, 0.0f, Skew::linear, Unit::decibels, true },
                                             { "tone", "Tone", 0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                                             { "mode", "Mode", 0.0f, 3.0f, 0.0f, Skew::linear, Unit::choice, false } }},
                                          3 };
        return d;
    }

    const ModuleDescriptor& testDescriptor(int index)
    {
        return index == 0 ? filterDescriptor() : shaperDescriptor();
    }

    SlotTypeTable testTable() { return { 2, &testDescriptor }; }

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (auto part : { Part::a, Part::b })
            for (auto kind : { SlotKind::generator, SlotKind::processor, SlotKind::insert })
                for (int index = 1; index <= 3; ++index)
                    layout.add(makeSlotGroup({ part, kind, index }, testTable(), "Slot"));
        for (int index = 1; index <= 6; ++index)
            layout.add(makeSlotGroup({ Part::a, SlotKind::master, index }, testTable(), "Master"));
        return layout;
    }

    class V3ParameterHarness final : public juce::AudioProcessor
    {
    public:
        V3ParameterHarness() : state(*this, nullptr, "V3", makeLayout()) {}

        const juce::String getName() const override { return "V3ParameterHarness"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}

        juce::AudioProcessorValueTreeState state;
    };

    void setParameterValue(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        auto* parameter = state.getParameter(id);
        jassert(parameter != nullptr);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
}

class V3ParameterTests final : public juce::UnitTest
{
public:
    V3ParameterTests() : juce::UnitTest("V3 slot parameters", "V3") {}

    void runTest() override
    {
        beginTest("slot_paths_build_stable_ids");
        {
            expect(slotPrefix({ Part::a, SlotKind::generator, 1 }) == "pA.g1");
            expect(slotPrefix({ Part::b, SlotKind::processor, 3 }) == "pB.p3");
            expect(slotPrefix({ Part::a, SlotKind::insert, 2 }) == "pA.i2");
            expect(slotPrefix({ Part::b, SlotKind::master, 6 }) == "f6");
            expect(typeParameterId({ Part::a, SlotKind::generator, 1 }) == "pA.g1.type");
            expect(genericParameterId({ Part::a, SlotKind::generator, 1 }, 1) == "pA.g1.p1");
            expect(genericParameterId({ Part::b, SlotKind::master, 2 }, 8) == "f2.p8");
            expect(fixedParameterId({ Part::a, SlotKind::generator, 1 }, "level") == "pA.g1.level");
            expectEquals(parameterCountForKind(SlotKind::generator), 17);
            expectEquals(parameterCountForKind(SlotKind::processor), 9);
            expectEquals(parameterCountForKind(SlotKind::master), 9);
        }

        beginTest("layout_registers_unique_ids_and_the_designed_count");
        {
            auto harness = std::make_unique<V3ParameterHarness>();
            juce::StringArray ids;
            for (auto* base : harness->getParameters())
            {
                auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(base);
                expect(withId != nullptr);
                if (withId != nullptr)
                {
                    expect(! ids.contains(withId->paramID), "duplicate id " + withId->paramID);
                    ids.add(withId->paramID);
                }
            }
            // two parts x (3 generators x 17 + 3 processors x 9 + 3 inserts x 9) + 6 master x 9
            expectEquals(ids.size(), 2 * (3 * 17 + 3 * 9 + 3 * 9) + 6 * 9);
            expect(ids.contains("pA.g1.amount"));
            expect(ids.contains("pA.g1.combine"));
            expect(! ids.contains("pA.p1.combine"));
            expect(ids.contains("f6.p8"));
        }

        beginTest("generic_parameter_name_text_and_parsing_follow_the_selected_type");
        {
            auto harness = std::make_unique<V3ParameterHarness>();
            auto& state = harness->state;
            const SlotPath path { Part::a, SlotKind::generator, 1 };

            auto* p1 = dynamic_cast<GenericSlotParameter*>(state.getParameter(genericParameterId(path, 1)));
            auto* p2 = dynamic_cast<GenericSlotParameter*>(state.getParameter(genericParameterId(path, 2)));
            auto* p3 = dynamic_cast<GenericSlotParameter*>(state.getParameter(genericParameterId(path, 3)));
            expect(p1 != nullptr && p2 != nullptr && p3 != nullptr);

            // Type 0 (Filter): p1 is a logarithmic cutoff, p3 is unused.
            expect(p1->getName(64) == "Cutoff");
            expect(p1->getText(0.5f, 64) == "632.5 Hz");
            expectWithinAbsoluteError(p1->getValueForText("632.5 Hz"), 0.5f, 1.0e-3f);
            expectWithinAbsoluteError(p1->getValueForText("2 kHz"), std::log(100.0f) / std::log(1000.0f), 1.0e-4f);
            expectWithinAbsoluteError(p1->get(), std::log(50.0f) / std::log(1000.0f), 1.0e-5f); // default 1000 Hz
            expect(p2->getName(64) == "Resonance");
            expect(p3->getName(64) == "P3");
            expect(p3->getText(0.3f, 64) == "-");
            expect(! p3->isUsedByCurrentType());

            // Switch the slot to type 1 (Shaper): names, units and the used set change with it.
            setParameterValue(state, typeParameterId(path), 1.0f);
            expect(p1->getName(64) == "Drive");
            expect(p1->getText(0.5f, 64) == "6.0 dB");
            expectWithinAbsoluteError(p1->getValueForText("-12 dB"), 0.0f, 1.0e-6f);
            expect(p2->getName(64) == "Tone");
            expect(p2->getText(0.25f, 64) == "25 %");
            expectWithinAbsoluteError(p2->getValueForText("25 %"), 0.25f, 1.0e-6f);
            expect(p3->isUsedByCurrentType());
            expect(p3->getName(64) == "Mode");
            expect(p3->getText(1.0f, 64) == "3");

            // Text without a number leaves the value alone.
            p1->setValueNotifyingHost(0.4f);
            expectWithinAbsoluteError(p1->getValueForText("abc"), 0.4f, 1.0e-6f);
        }

        beginTest("pointer_table_reads_targets_and_feeds_the_slot_base");
        {
            auto harness = std::make_unique<V3ParameterHarness>();
            auto& state = harness->state;
            const SlotPath generator { Part::a, SlotKind::generator, 1 };
            const SlotPath processor { Part::a, SlotKind::processor, 1 };

            setParameterValue(state, typeParameterId(generator), 1.0f);
            setParameterValue(state, genericParameterId(generator, 1), 0.7f);
            setParameterValue(state, fixedParameterId(generator, "level"), 0.6f);
            setParameterValue(state, fixedParameterId(generator, "pan"), -0.25f);
            setParameterValue(state, fixedParameterId(generator, "oct"), 1.0f);
            setParameterValue(state, fixedParameterId(generator, "semi"), 7.0f);
            setParameterValue(state, fixedParameterId(generator, "fine"), 50.0f);
            setParameterValue(state, fixedParameterId(generator, "key"), 0.0f);
            setParameterValue(state, fixedParameterId(generator, "combine"), static_cast<float>(CombineMode::ring));
            setParameterValue(state, fixedParameterId(generator, "amount"), 0.3f);

            const auto pointers = bindSlotPointers(state, generator);
            expect(pointers.type != nullptr && pointers.p[0] != nullptr && pointers.combine != nullptr);
            const auto targets = readSlotTargets(pointers);
            expectEquals(targets.typeIndex, 1);
            expectWithinAbsoluteError(targets.p[0], 0.7f, 1.0e-6f);
            expectWithinAbsoluteError(targets.level, 0.6f, 1.0e-6f);
            expectWithinAbsoluteError(targets.pan, -0.25f, 1.0e-6f);
            expectWithinAbsoluteError(targets.pitchSemitones, 19.5f, 1.0e-5f);
            expect(! targets.keyTrack);
            expect(targets.combine == CombineMode::ring);
            expectWithinAbsoluteError(targets.amount, 0.3f, 1.0e-6f);

            const auto processorPointers = bindSlotPointers(state, processor);
            expect(processorPointers.type != nullptr && processorPointers.level == nullptr);
            const auto processorTargets = readSlotTargets(processorPointers);
            expectEquals(processorTargets.typeIndex, 0);
            expectEquals(processorTargets.level, 1.0f);
            expectEquals(processorTargets.pitchSemitones, 0.0f);
            expect(processorTargets.keyTrack);

            coolsynth::synth::v3::SlotBase base;
            base.prepare(96000.0, 32);
            applySlotTargets(targets, base);
            base.snapToTargets();
            expectWithinAbsoluteError(base.value(0), 0.7f, 1.0e-6f);
            expectWithinAbsoluteError(base.levelValue(), 0.6f, 1.0e-6f);
            expectWithinAbsoluteError(base.panValue(), -0.25f, 1.0e-6f);
            expectWithinAbsoluteError(base.pitchValue(), 19.5f, 1.0e-5f);
            expectWithinAbsoluteError(base.amountValue(), 0.3f, 1.0e-6f);
        }
    }
};

static V3ParameterTests v3ParameterTests;
