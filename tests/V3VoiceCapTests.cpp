// The voice cap and the live emergency (D222): a patch plays no more voices than fit its share of a block, and when a
// dropout is near the quietest released voice fades out.

#include <algorithm>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/EngineV3.h"
#include "synth/v3/VoiceCost.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    float vaNorm(int param, float units)
    {
        return unitsToNormalised(VaGenerator::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    PartSettings vaPart(int unison)
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        g.p[VaGenerator::shape] = vaNorm(VaGenerator::shape, 2.0f);
        g.p[VaGenerator::unison] = vaNorm(VaGenerator::unison, static_cast<float>(unison));
        g.level = 0.5f;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 4.0f; // long: released voices ring through the test
        s.voice.envelopes = { held, held, held };
        s.polyphony = Part::maxVoices;
        return s;
    }

    coolsynth::synth::EngineMidiEvent event(Type type, int note)
    {
        coolsynth::synth::EngineMidiEvent e;
        e.type = type;
        e.noteNumber = static_cast<std::uint8_t>(note);
        e.value = type == Type::noteOn ? 0.8f : 0.0f;
        return e;
    }

    /** Made-up costs, round numbers to check the arithmetic: a voice 1, a VA 2 and 1 for each copy past the first. */
    voiceCost::Costs madeUp()
    {
        voiceCost::Costs c;
        c.voice = 1.0f;
        c.generator[static_cast<std::size_t>(generatorType::va)] = 2.0f;
        c.vaCopy = 1.0f;
        c.effect[static_cast<std::size_t>(insertType::reverb)] = 30.0f;
        c.effect[static_cast<std::size_t>(insertType::delay)] = 70.0f;
        return c;
    }
}

class V3VoiceCapTests final : public juce::UnitTest
{
public:
    V3VoiceCapTests() : juce::UnitTest("V3 voice cap", "V3") {}

    void runTest() override
    {
        const auto costs = madeUp();
        const std::array<bool, partCount> onlyA { true, false };
        const std::array<int, partCount> sixteen { Part::maxVoices, Part::maxVoices };

        beginTest("a light patch keeps every voice, a heavy one fewer, the budget's share of them");
        {
            EngineSettings s;
            s.parts[0] = vaPart(1); // 3 a voice: 48 for sixteen, within 50
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, costs)[0], 16);
            s.parts[0] = vaPart(7); // 9 a voice: 144 for sixteen, so 50 / 144 of them
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, costs)[0], 5);
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, costs)[1], 16, "a layer not playing keeps what it asks for");
        }

        beginTest("the effects come off the budget first, and the voices keep a fifth of it");
        {
            EngineSettings s;
            s.parts[0] = vaPart(1);
            s.master[0] = InsertSettings::of(insertType::reverb); // 30: 20 left, 48 wanted
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, costs)[0], 6);
            s.master[1] = InsertSettings::of(insertType::delay); // 100 in all: the voices keep 10
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, costs)[0], 3);
        }

        beginTest("at twice the rate a voice costs twice as much; never fewer than one voice");
        {
            EngineSettings s;
            s.parts[0] = vaPart(1);
            expectEquals(voiceCost::cap(s, onlyA, sixteen, 2.0 * kRate, costs)[0], 8);
            voiceCost::Costs heavy = costs;
            heavy.voice = 1000.0f;
            expectEquals(voiceCost::cap(s, onlyA, sixteen, kRate, heavy)[0], 1);
        }

        beginTest("two layers share the budget in proportion to what they ask for");
        {
            EngineSettings s;
            s.parts[0] = vaPart(7);
            s.parts[1] = vaPart(1);
            const auto both = voiceCost::cap(s, { true, true }, { 8, 8 }, kRate, costs); // 72 + 24 = 96 for 50
            expectEquals(both[0], 4);
            expectEquals(both[1], 4);
        }

        beginTest("the live emergency: a dropout near, the quietest released voice fades out, one a block");
        {
            auto engine = std::make_unique<EngineV3>();
            engine->prepare(kRate, kBlock, 2);
            EngineSettings s;
            s.parts[0] = vaPart(1);
            juce::AudioBuffer<float> buffer(2, kBlock);
            std::vector<coolsynth::synth::EngineMidiEvent> chord, release;
            for (const int note : { 48, 52, 55, 59 })
            {
                chord.push_back(event(Type::noteOn, note));
                release.push_back(event(Type::noteOff, note));
            }
            auto block = [&](const std::vector<coolsynth::synth::EngineMidiEvent>& events)
            {
                buffer.clear();
                engine->render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(events), s);
            };
            block(chord);
            for (int b = 0; b < 20; ++b)
                block({});
            engine->setLoad(0.95f); // held keys: nothing to let go
            block({});
            for (int b = 0; b < 4; ++b)
                block({});
            expectEquals(engine->activeVoiceCount(), 4, "a held key's voice is never taken");
            block(release);
            for (int b = 0; b < 4; ++b)
                block({});
            expectEquals(engine->activeVoiceCount(), 4, "released, ringing, and time to spare");
            engine->setLoad(0.95f);
            block({});
            for (int b = 0; b < 4; ++b)
                block({}); // the steal fade's 5 ms, and more
            expectEquals(engine->activeVoiceCount(), 3, "one released voice let go");
            engine->setLoad(0.8f); // short of time, but not a dropout: with no earlier sound, nothing goes
            block({});
            for (int b = 0; b < 4; ++b)
                block({});
            expectEquals(engine->activeVoiceCount(), 3);
        }

        beginTest("the factory presets under the cap, at three budgets");
        {
            // What each budget would cost the bank: a preset listed plays fewer voices than its patch asks for.
            auto synth = std::make_unique<SynthAudioProcessor>();
            synth->setPlayConfigDetails(0, 2, kRate, kBlock);
            synth->prepareToPlay(kRate, kBlock);
            {
                // The cost table's own estimate of each preset with every voice playing: to hold against the measured.
                std::vector<std::pair<float, juce::String>> estimates;
                for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                {
                    synth->setCurrentProgram(i);
                    const auto& s = synth->engineSettingsForTesting();
                    const std::array<bool, partCount> active { ! layerEmpty(s.parts[0]), ! layerEmpty(s.parts[1]) };
                    std::array<int, partCount> wanted { SoundUnit::voicesWanted(s.parts[0]), SoundUnit::voicesWanted(s.parts[1]) };
                    if (active[0] && active[1])
                        wanted = SoundUnit::voiceLimits(wanted);
                    float total = 0.0f;
                    for (int p = 0; p < partCount; ++p)
                        if (active[static_cast<std::size_t>(p)])
                        {
                            total += voiceCost::ofVoice(s.parts[static_cast<std::size_t>(p)]) * static_cast<float>(wanted[static_cast<std::size_t>(p)]);
                            for (const auto& slot : s.inserts[static_cast<std::size_t>(p)])
                                total += voiceCost::ofEffect(slot);
                        }
                    for (const auto& slot : s.master)
                        total += voiceCost::ofEffect(slot);
                    estimates.emplace_back(total, coolsynth::presets::getFactoryPreset(i).name + " " + juce::String(total, 1));
                }
                std::sort(estimates.begin(), estimates.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                juce::StringArray top;
                for (std::size_t i = 0; i < std::min<std::size_t>(12, estimates.size()); ++i)
                    top.add(estimates[i].second);
                logMessage("the table's estimates, every voice playing: " + top.joinIntoString(", "));
            }
            for (const float budget : { 35.0f, 50.0f, 75.0f })
            {
                juce::StringArray capped;
                for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                {
                    synth->setCurrentProgram(i);
                    const auto& s = synth->engineSettingsForTesting();
                    const std::array<bool, partCount> active { ! layerEmpty(s.parts[0]), ! layerEmpty(s.parts[1]) };
                    std::array<int, partCount> wanted { SoundUnit::voicesWanted(s.parts[0]), SoundUnit::voicesWanted(s.parts[1]) };
                    if (active[0] && active[1])
                        wanted = SoundUnit::voiceLimits(wanted);
                    const auto got = voiceCost::cap(s, active, wanted, kRate, voiceCost::measured, budget);
                    for (int p = 0; p < partCount; ++p)
                        if (active[static_cast<std::size_t>(p)] && got[static_cast<std::size_t>(p)] < wanted[static_cast<std::size_t>(p)])
                        {
                            capped.add(coolsynth::presets::getFactoryPreset(i).name + (p == 0 ? " A " : " B ")
                                       + juce::String(got[static_cast<std::size_t>(p)]) + "/" + juce::String(wanted[static_cast<std::size_t>(p)]));
                        }
                }
                logMessage("budget " + juce::String(budget, 0) + " %: " + juce::String(capped.size()) + " layers capped: " + capped.joinIntoString(", "));
                if (budget == voiceCost::budgetPercent)
                    expect(capped.isEmpty(), "a factory preset loses voices at the chosen budget: " + capped.joinIntoString(", "));
            }
        }
    }
};

static V3VoiceCapTests v3VoiceCapTests;
