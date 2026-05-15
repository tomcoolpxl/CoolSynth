#include "ParameterLayout.h"

#include <cmath>

#include "ParameterIDs.h"

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

    juce::ParameterID makeLegacyParameterId(const char* id)
    {
        return { id, coolsynth::parameters::ids::legacyVersionHint };
    }

    juce::ParameterID makeV2ParameterId(const char* id)
    {
        return { id, coolsynth::parameters::ids::v2VersionHint };
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

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeOscillatorAGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("oscA", "Oscillator A", " / ");
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(oscAWave),
            "Wave Shape",
            juce::StringArray { "Pulse", "Triangle", "Saw", "Sine" },
            static_cast<int>(OscillatorWaveShape::saw)));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(oscAOctave),
            "Octave",
            juce::StringArray { "32'", "16'", "8'", "4'", "2'" },
            2));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(oscAFineCents),
            "Fine Tune",
            juce::NormalisableRange<float>(-50.0f, 50.0f),
            0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) { return formatCents(value); })));
        group->addChild(makePercentParameter(oscALevel, v2VersionHint, "Level", 0.80f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(oscAPulseWidth),
            "Pulse Width",
            juce::NormalisableRange<float>(0.05f, 0.95f),
            0.5f,
            percentAttributes()));
        group->addChild(makeBoolParameter(oscASyncEnabled, v2VersionHint, "Sync", false));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeOscillatorBGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("oscB", "Oscillator B", " / ");
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(oscBWave),
            "Wave Shape",
            juce::StringArray { "Pulse", "Triangle", "Saw", "Sine" },
            static_cast<int>(OscillatorWaveShape::saw)));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(oscBOctave),
            "Octave",
            juce::StringArray { "32'", "16'", "8'", "4'", "2'" },
            2));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(oscBFineCents),
            "Fine Tune",
            juce::NormalisableRange<float>(-50.0f, 50.0f),
            4.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) { return formatCents(value); })));
        group->addChild(makePercentParameter(oscBLevel, v2VersionHint, "Level", 0.68f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(oscBPulseWidth),
            "Pulse Width",
            juce::NormalisableRange<float>(0.05f, 0.95f),
            0.5f,
            percentAttributes()));
        group->addChild(makeBoolParameter(oscBLowFrequencyMode, v2VersionHint, "Low Freq", false));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeMixerGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("mixer", "Mixer", " / ");
        group->addChild(makePercentParameter(noiseLevel, v2VersionHint, "Noise", 0.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeFilterGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("filter", "Filter", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(filterCutoffHz),
            "Cutoff",
            makeLogRange(20.0f, 20000.0f),
            3200.0f,
            hertzAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(filterResonance),
            "Resonance",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.08f,
            percentAttributes()));
        group->addChild(makePercentParameter(filterEnvAmount, v2VersionHint, "Env Amount", 0.35f));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(filterKeyTracking),
            "Key Track",
            juce::StringArray { "Off", "Half", "Full" },
            static_cast<int>(coolsynth::parameters::FilterKeyTrackingMode::half)));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeFilterEnvelopeGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("filterEnv", "Filter Envelope", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(filterAttackMs),
            "Attack",
            makeLogRange(1.0f, 5000.0f),
            10.0f,
            millisecondsAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(filterDecayMs),
            "Decay",
            makeLogRange(5.0f, 5000.0f),
            220.0f,
            millisecondsAttributes()));
        group->addChild(makePercentParameter(filterSustain, v2VersionHint, "Sustain", 0.12f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(filterReleaseMs),
            "Release",
            makeLogRange(5.0f, 5000.0f),
            220.0f,
            millisecondsAttributes()));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeAmpEnvelopeGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("ampEnv", "Amp Envelope", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(ampAttackMs),
            "Attack",
            makeLogRange(1.0f, 5000.0f),
            5.0f,
            millisecondsAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(ampDecayMs),
            "Decay",
            makeLogRange(5.0f, 5000.0f),
            260.0f,
            millisecondsAttributes()));
        group->addChild(makePercentParameter(ampSustain, legacyVersionHint, "Sustain", 0.72f));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(ampReleaseMs),
            "Release",
            makeLogRange(5.0f, 5000.0f),
            240.0f,
            millisecondsAttributes()));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeLfoGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("lfo", "LFO", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(lfoRateHz),
            "Rate",
            makeLogRange(0.05f, 20.0f),
            5.0f,
            hertzAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(lfoWave),
            "Wave Shape",
            juce::StringArray { "Saw", "Triangle", "Square", "Sine" },
            static_cast<int>(LfoWaveShape::triangle)));
        group->addChild(makePercentParameter(lfoToOscPitch, v2VersionHint, "To Pitch", 0.0f));
        group->addChild(makePercentParameter(lfoToPulseWidth, v2VersionHint, "To Pulse Width", 0.0f));
        group->addChild(makePercentParameter(lfoToFilterCutoff, v2VersionHint, "To Cutoff", 0.0f));
        group->addChild(makePercentParameter(modWheelToLfoDepth, v2VersionHint, "Wheel Depth", 1.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makePolyModGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("polyMod", "Poly Mod", " / ");
        group->addChild(makePercentParameter(polyModOscBToOscPitch, v2VersionHint, "Osc B -> Pitch", 0.0f));
        group->addChild(makePercentParameter(polyModEnvToOscPitch, v2VersionHint, "Env -> Pitch", 0.0f));
        group->addChild(makePercentParameter(polyModOscBToPulseWidth, v2VersionHint, "Osc B -> PW", 0.0f));
        group->addChild(makePercentParameter(polyModEnvToPulseWidth, v2VersionHint, "Env -> PW", 0.0f));
        group->addChild(makePercentParameter(polyModOscBToFilterCutoff, v2VersionHint, "Osc B -> Cutoff", 0.0f));
        group->addChild(makePercentParameter(polyModEnvToFilterCutoff, v2VersionHint, "Env -> Cutoff", 0.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makePerformanceGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("performance", "Performance", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(glideTimeMs),
            "Glide",
            juce::NormalisableRange<float>(0.0f, 2000.0f),
            0.0f,
            millisecondsAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(playMode),
            "Play Mode",
            juce::StringArray { "Poly", "Mono", "Unison" },
            static_cast<int>(PlayModeChoice::poly)));
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            makeV2ParameterId(keyPriority),
            "Key Priority",
            juce::StringArray { "Last", "Low", "High" },
            static_cast<int>(KeyPriorityChoice::last)));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            makeV2ParameterId(pitchBendRangeSemitones),
            "Bend Range",
            1,
            24,
            2,
            juce::AudioParameterIntAttributes().withStringFromValueFunction([](int value, int) { return formatSemitones(static_cast<float>(value)); })));
        group->addChild(makePercentParameter(vintageAmount, v2VersionHint, "Vintage", 0.0f));
        group->addChild(makePercentParameter(panSpread, v2VersionHint, "Pan Spread", 0.0f));
        group->addChild(makePercentParameter(velocityToAmp, v2VersionHint, "Vel -> Amp", 1.0f));
        group->addChild(makePercentParameter(velocityToFilter, v2VersionHint, "Vel -> Filter", 0.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeArpGroup()
    {
        using namespace coolsynth::parameters;
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("arp", "Arpeggiator", " / ");
        group->addChild(makeBoolParameter(arpEnabled, v2VersionHint, "Enabled", false));
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
            juce::StringArray { "Up", "Down", "Up/Down", "As Played" },
            static_cast<int>(ArpPatternChoice::up)));
        group->addChild(std::make_unique<juce::AudioParameterInt>(
            makeV2ParameterId(arpOctaveRange),
            "Octaves",
            1,
            3,
            1,
            juce::AudioParameterIntAttributes().withStringFromValueFunction([](int value, int) { return formatOctaveRange(value - 1); })));
        group->addChild(makePercentParameter(arpGate, v2VersionHint, "Gate", 0.5f));
        group->addChild(makeBoolParameter(arpLatch, v2VersionHint, "Latch", false));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeDriveGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("drive", "Drive", " / ");
        group->addChild(makeBoolParameter(driveEnabled, v2VersionHint, "Enabled", false));
        group->addChild(makePercentParameter(driveAmount, v2VersionHint, "Amount", 0.0f));
        group->addChild(makePercentParameter(driveMix, v2VersionHint, "Mix", 1.0f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeChorusGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("chorus", "Chorus", " / ");
        group->addChild(makeBoolParameter(chorusEnabled, v2VersionHint, "Enabled", false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeV2ParameterId(chorusRateHz),
            "Rate",
            makeLogRange(0.05f, 10.0f),
            0.6f,
            hertzAttributes()));
        group->addChild(makePercentParameter(chorusDepth, v2VersionHint, "Depth", 0.4f));
        group->addChild(makePercentParameter(chorusMix, v2VersionHint, "Mix", 0.3f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeDelayGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("delay", "Delay", " / ");
        group->addChild(makeBoolParameter(delayEnabled, v2VersionHint, "Enabled", false));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(delayTimeMs),
            "Time",
            makeLogRange(1.0f, 1000.0f),
            250.0f,
            millisecondsAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(delayFeedback),
            "Feedback",
            juce::NormalisableRange<float>(0.0f, 0.85f),
            0.25f,
            percentAttributes()));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(delayMix),
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
        group->addChild(makeBoolParameter(reverbEnabled, v2VersionHint, "Enabled", false));
        group->addChild(makePercentParameter(reverbSize, v2VersionHint, "Size", 0.4f));
        group->addChild(makePercentParameter(reverbDamping, v2VersionHint, "Damping", 0.5f));
        group->addChild(makePercentParameter(reverbMix, v2VersionHint, "Mix", 0.2f));
        return group;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeOutputGroup()
    {
        using namespace coolsynth::parameters::ids;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("output", "Output", " / ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            makeLegacyParameterId(masterGainDb),
            "Master Gain",
            juce::NormalisableRange<float>(-60.0f, 0.0f),
            -12.0f,
            dbAttributes()));
        return group;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout coolsynth::parameters::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(makeOscillatorAGroup(),
               makeOscillatorBGroup(),
               makeMixerGroup(),
               makeFilterGroup(),
               makeFilterEnvelopeGroup(),
               makeAmpEnvelopeGroup(),
               makeLfoGroup(),
               makePolyModGroup(),
               makePerformanceGroup(),
               makeArpGroup(),
               makeDriveGroup(),
               makeChorusGroup(),
               makeDelayGroup(),
               makeReverbGroup(),
               makeOutputGroup());
    return layout;
}
