#include "ParameterLayout.h"

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

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeArpGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("arp", "Arpeggiator", " / ");
        group->addChild(makeBoolParameter(arpEnabled, parameterVersionHint, "Enabled", false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(arpInternalTempoBpm),
            "Internal BPM",
            juce::NormalisableRange<float>(40.0f, 240.0f),
            120.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) { return formatBpm(value); })));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(arpRateDivision),
            "Rate",
            juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" },
            static_cast<int>(ArpRateChoice::sixteenth)));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(arpPattern),
            "Pattern",
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
            makeV2ParameterId(arpOctaveRange),
            "Octaves",
            1,
            3,
            1,
            juce::AudioParameterIntAttributes().withStringFromValueFunction([](int value, int) { return formatOctaveRange(value - 1); })));
        group->addChild(makePercentParameter(arpGate, parameterVersionHint, "Gate", 0.5f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { arpSwing, parameterVersionHintV4 },
            "Swing",
            juce::NormalisableRange<float>(0.0f, 0.75f),
            0.0f,
            percentAttributes()));
        group->addChild(makePercentParameter(arpChance, parameterVersionHintV4, "Chance", 1.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { arpRatchetCount, parameterVersionHintV5 },
            "Ratchet",
            juce::StringArray { "Off", "x2", "x3", "x4" },
            static_cast<int>(ArpRatchetChoice::off)));
        group->addChild(makePercentParameter(arpRatchetChance, parameterVersionHintV5, "Ratchet Chance", 0.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { arpAccentEvery, parameterVersionHintV5 },
            "Accent Every",
            juce::StringArray { "Off", "/2", "/3", "/4" },
            static_cast<int>(ArpAccentEveryChoice::off)));
        group->addChild(makePercentParameter(arpAccentAmount, parameterVersionHintV5, "Accent Amount", 0.0f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { arpRhythm, parameterVersionHintV6 },
            "Rhythm",
            juce::StringArray { "Straight", "Euclidean" },
            static_cast<int>(ArpRhythmChoice::straight)));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { arpEuclideanPulses, parameterVersionHintV6 },
            "Euclidean Pulses",
            1,
            16,
            4));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { arpEuclideanSteps, parameterVersionHintV6 },
            "Euclidean Steps",
            1,
            16,
            8));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { arpEuclideanRotation, parameterVersionHintV6 },
            "Euclidean Rotation",
            0,
            15,
            0));
        group->addChild(makeBoolParameter(arpLatch, parameterVersionHint, "Latch", false));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeDriveGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("drive", "Distortion", " / ");
        group->addChild(makeBoolParameter(driveEnabled, parameterVersionHint, "Enabled", false));
        group->addChild(makePercentParameter(driveAmount, parameterVersionHint, "Amount", 0.0f));
        group->addChild(makePercentParameter(driveMix, parameterVersionHint, "Mix", 1.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeChorusGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("chorus", "Chorus", " / ");
        group->addChild(makeBoolParameter(chorusEnabled, parameterVersionHint, "Enabled", false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(chorusRateHz),
            "Rate",
            makeLogRange(0.05f, 10.0f),
            0.6f,
            hertzAttributes()));
        group->addChild(makePercentParameter(chorusDepth, parameterVersionHint, "Depth", 0.4f));
        group->addChild(makePercentParameter(chorusMix, parameterVersionHint, "Mix", 0.3f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeDelayGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("delay", "Delay", " / ");
        group->addChild(makeBoolParameter(delayEnabled, parameterVersionHint, "Enabled", false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(delayTimeMs),
            "Time",
            makeLogRange(1.0f, 1000.0f),
            250.0f,
            millisecondsAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(delayFeedback),
            "Feedback",
            juce::NormalisableRange<float>(0.0f, 0.85f),
            0.25f,
            percentAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(delayMix),
            "Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f,
            percentAttributes()));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeReverbGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("reverb", "Reverb", " / ");
        group->addChild(makeBoolParameter(reverbEnabled, parameterVersionHint, "Enabled", false));
        group->addChild(makePercentParameter(reverbSize, parameterVersionHint, "Size", 0.4f));
        group->addChild(makePercentParameter(reverbDamping, parameterVersionHint, "Damping", 0.5f));
        group->addChild(makePercentParameter(reverbMix, parameterVersionHint, "Mix", 0.2f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makePhaserGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("phaser", "Phaser", " / ");
        group->addChild(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { phaserEnabled, parameterVersionHintV3 },
            "Enabled",
            false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { phaserRateHz, parameterVersionHintV3 },
            "Rate",
            makeLogRange(0.05f, 8.0f),
            0.5f,
            hertzAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { phaserDepth, parameterVersionHintV3 },
            "Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.6f,
            percentAttributes()));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeCompressorGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("compressor", "Compressor", " / ");
        group->addChild(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { compressorEnabled, parameterVersionHintV3 },
            "Enabled",
            false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { compressorAmount, parameterVersionHintV3 },
            "Amount",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.3f,
            percentAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { compressorMix, parameterVersionHintV3 },
            "Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            1.0f,
            percentAttributes()));
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
        return group;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout coolsynth::parameters::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(coolsynth::parameters::v3::makePartGroup(coolsynth::parameters::v3::Part::a),
               makeArpGroup(),
               makeDriveGroup(),
               makePhaserGroup(),
               makeChorusGroup(),
               makeDelayGroup(),
               makeReverbGroup(),
               makeCompressorGroup(),
               makeOutputGroup());
    return layout;
}
