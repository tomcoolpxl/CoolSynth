#include "ParameterLayout.h"

#include "synth/SynthParameters.h"

#include <cmath>

#include "ParameterIDs.h"
#include "v3/PartParameters.h"

namespace
{
    juce::NormalisableRange<float> makeLogRange(float minValue, float maxValue)
    {
        return {
            minValue,
            maxValue,
            [] (float start, float end, float proportion)
            {
                return start * std::pow(end / start, proportion);
            },
            [] (float start, float end, float value)
            {
                return std::log(value / start) / std::log(end / start);
            },
            [] (float, float, float value)
            {
                return value;
            }
        };
    }

    juce::String formatMs(float value)
    {
        if (value < 1000.0f)
            return juce::String(static_cast<int>(std::round(value))) + " ms";

        return juce::String(value / 1000.0f, 2) + " s";
    }

    juce::String formatHz(float value)
    {
        if (value < 1000.0f)
            return juce::String(value, value < 100.0f ? 2 : 1) + " Hz";

        return juce::String(value / 1000.0f, 2) + " kHz";
    }

    juce::String formatPercentFromUnit(float value)
    {
        return juce::String(static_cast<int>(std::round(value * 100.0f))) + " %";
    }

    juce::String formatCents(float value)
    {
        return juce::String(static_cast<int>(std::round(value))) + " ct";
    }

    juce::String formatDb(float value)
    {
        return juce::String(value, 1) + " dB";
    }

    juce::String formatSemitones(float value)
    {
        return juce::String(static_cast<int>(std::round(value))) + " st";
    }

    juce::String formatBpm(float value)
    {
        return juce::String(static_cast<int>(std::round(value))) + " BPM";
    }

    juce::String formatOctaveRange(int zeroBasedIndex)
    {
        return juce::String(zeroBasedIndex + 1) + " oct";
    }

    juce::AudioParameterFloatAttributes percentAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return formatPercentFromUnit(value); });
    }

    juce::AudioParameterFloatAttributes millisecondsAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return formatMs(value); });
    }

    juce::AudioParameterFloatAttributes hertzAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return formatHz(value); });
    }

    juce::AudioParameterFloatAttributes dbAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return formatDb(value); });
    }

    juce::ParameterID makeV2ParameterId(const char* id)
    {
        return { id, coolsynth::parameters::ids::parameterVersionHint };
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercentParameter(const char* id,
                                                                    int versionHint,
                                                                    const juce::String& name,
                                                                    float defaultValue)
    {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, versionHint },
            name,
            juce::NormalisableRange<float>(0.0f, 1.0f),
            defaultValue,
            percentAttributes());
    }

    std::unique_ptr<juce::AudioParameterBool> makeBoolParameter(const char* id,
                                                                int versionHint,
                                                                const juce::String& name,
                                                                bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, versionHint },
            name,
            defaultValue);
    }

    /** A layer's arpeggiator (0 A, 1 B; D194): the same settings for each, named for the layer. */
    std::unique_ptr<juce::AudioProcessorParameterGroup> makeArpGroup(int layer)
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;
        const auto L = [layer](const char* layerAId) { return juce::String(arpOfLayer(layer, layerAId)); };
        const auto makeV2ParameterId = [](const juce::String& id) { return juce::ParameterID { id, parameterVersionHint }; };
        const juce::String layerName = layer == 0 ? "Layer A" : "Layer B";
        const auto makeBoolParameter = [](const juce::String& id, int hint, const juce::String& name, bool on)
        {
            return std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, hint }, name, on);
        };
        const auto makePercentParameter = [](const juce::String& id, int hint, const juce::String& name, float value)
        {
            return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, hint }, name,
                                                               juce::NormalisableRange<float>(0.0f, 1.0f), value, percentAttributes());
        };

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(layer == 0 ? "pA.arp" : "pB.arp", layerName + " Arpeggiator", " / ");
        group->addChild(makeBoolParameter(L(arpEnabled), parameterVersionHint, layerName + " Arp", false));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(L(arpRateDivision)),
            layerName + " Arp Rate",
            juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" },
            static_cast<int>(ArpRateChoice::sixteenth)));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(L(arpPattern)),
            layerName + " Arp Pattern",
            juce::StringArray {
                "Up",
                "Down",
                "Up/Down",
                "As Played",
                "Converge",
                "Diverge",
                "Inside",
                "Outside",
                "Random",
                "Random Walk",
                "Chord",
            },
            static_cast<int>(ArpPatternChoice::up)));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            makeV2ParameterId(L(arpOctaveRange)),
            layerName + " Arp Octaves",
            1,
            3,
            1,
            juce::AudioParameterIntAttributes().withStringFromValueFunction([](int value, int) { return formatOctaveRange(value - 1); })));
        group->addChild(makePercentParameter(L(arpGate), parameterVersionHint, layerName + " Arp Gate", 0.5f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { L(arpSwing), parameterVersionHintV4 },
            layerName + " Arp Swing",
            juce::NormalisableRange<float>(0.0f, 0.75f),
            0.0f,
            percentAttributes()));
        group->addChild(makePercentParameter(L(arpChance), parameterVersionHintV4, layerName + " Arp Chance", 1.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { L(arpRatchetCount), parameterVersionHintV5 },
            layerName + " Arp Ratchet",
            juce::StringArray { "Off", "x2", "x3", "x4" },
            static_cast<int>(ArpRatchetChoice::off)));
        group->addChild(makePercentParameter(L(arpRatchetChance), parameterVersionHintV5, layerName + " Arp Ratchet Chance", 0.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { L(arpAccentEvery), parameterVersionHintV5 },
            layerName + " Arp Accent Every",
            juce::StringArray { "Off", "/2", "/3", "/4" },
            static_cast<int>(ArpAccentEveryChoice::off)));
        group->addChild(makePercentParameter(L(arpAccentAmount), parameterVersionHintV5, layerName + " Arp Accent Amount", 0.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { L(arpRhythm), parameterVersionHintV6 },
            layerName + " Arp Rhythm",
            juce::StringArray { "Straight", "Euclidean" },
            static_cast<int>(ArpRhythmChoice::straight)));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { L(arpEuclideanPulses), parameterVersionHintV6 },
            layerName + " Arp Euclidean Pulses",
            1,
            16,
            4));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { L(arpEuclideanSteps), parameterVersionHintV6 },
            layerName + " Arp Euclidean Steps",
            1,
            16,
            8));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { L(arpEuclideanRotation), parameterVersionHintV6 },
            layerName + " Arp Euclidean Rotation",
            0,
            15,
            0));
        group->addChild(makeBoolParameter(L(arpLatch), parameterVersionHint, layerName + " Arp Latch", false));
        return group;
    }

    /** The tempo while no host plays, one for the sound (D194: the arps became per layer, the tempo stayed one). */
    std::unique_ptr<juce::AudioProcessorParameterGroup> makeTempoGroup()
    {
        using namespace coolsynth::parameters::ids;
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("tempo", "Tempo", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(arpInternalTempoBpm),
            "Internal BPM",
            juce::NormalisableRange<float>(40.0f, 240.0f),
            120.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) { return formatBpm(value); })));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeOutputGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("output", "Output", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(masterGainDb),
            "Master Gain",
            juce::NormalisableRange<float>(-60.0f, 6.0f),
            -6.0f,
            dbAttributes()));

        // Knobs 7 and 8, the same on every page (D176): the sound as designed in the middle.
        const auto centred = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int)
        {
            const int percent = juce::roundToInt(100.0f * value);
            return percent == 0 ? juce::String("as the sound") : (percent > 0 ? "+" : "") + juce::String(percent) + " %";
        });
        group->addChild(std::make_unique<juce::AudioParameterFloat>(makeV2ParameterId(brightness), "Brightness",
                                                                    juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f, centred));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(makeV2ParameterId(space), "Space",
                                                                    juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f, centred));

        // The four faders (D184): volumes over the sound as designed, the top (1) as designed; shown in dB.
        const auto volume = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int)
        {
            const float gain = coolsynth::synth::faderGain(value);
            if (gain <= 1.0e-6f)
                return juce::String("off");
            const float db = juce::Decibels::gainToDecibels(gain);
            return db > -0.05f ? juce::String("0.0 dB") : juce::String(db, 1) + " dB";
        });
        for (const auto& [id, name] : { std::pair { volumeA, "Layer A Volume" }, std::pair { volumeB, "Layer B Volume" },
                                        std::pair { volumeFx, "Effects Volume" }, std::pair { volumeMaster, "Master Volume" } })
            group->addChild(std::make_unique<juce::AudioParameterFloat>(makeV2ParameterId(id), name,
                                                                        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f, volume));
        return group;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout coolsynth::parameters::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(coolsynth::parameters::v3::makePartsGroup(),
               coolsynth::parameters::v3::makePartGroup(coolsynth::parameters::v3::Part::a),
               makeArpGroup(0),
               makeTempoGroup(),
               coolsynth::parameters::v3::makeMasterEffectsGroup(),
               makeOutputGroup(),
               coolsynth::parameters::v3::makePartGroup(coolsynth::parameters::v3::Part::b),
               makeArpGroup(1));
    return layout;
}
