#include "ParameterLayout.h"

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
            return juce::String(static_cast<int>(value)) + " ms";
        
        return juce::String(value / 1000.0f, 1) + " s";
    }

    juce::String formatHz(float value)
    {
        if (value < 1000.0f)
            return juce::String(static_cast<int>(value)) + " Hz";
        
        return juce::String(value / 1000.0f, 1) + " kHz";
    }

    juce::String formatPercent(float value)
    {
        return juce::String(static_cast<int>(value * 100.0f)) + " %";
    }

    juce::String formatDb(float value)
    {
        return juce::String(value, 1) + " dB";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout coolsynth::parameters::createParameterLayout()
{
    using namespace coolsynth::parameters;
    using namespace coolsynth::parameters::ids;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { waveform, versionHint },
        "Waveform",
        juce::StringArray { "sine", "square", "saw" },
        static_cast<int>(WaveformChoice::saw)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ampAttackMs, versionHint },
        "Attack",
        makeLogRange(1.0f, 5000.0f),
        10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatMs(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ampDecayMs, versionHint },
        "Decay",
        makeLogRange(5.0f, 5000.0f),
        200.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatMs(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ampSustain, versionHint },
        "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        0.8f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatPercent(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ampReleaseMs, versionHint },
        "Release",
        makeLogRange(5.0f, 5000.0f),
        300.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatMs(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { filterCutoffHz, versionHint },
        "Cutoff",
        makeLogRange(20.0f, 20000.0f),
        10000.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatHz(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { filterResonance, versionHint },
        "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        0.1f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatPercent(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { delayTimeMs, versionHint },
        "Delay Time",
        makeLogRange(1.0f, 1000.0f),
        250.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatMs(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { delayFeedback, versionHint },
        "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.85f),
        0.25f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatPercent(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { delayMix, versionHint },
        "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatPercent(v); })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { masterGainDb, versionHint },
        "Master Gain",
        juce::NormalisableRange<float>(-60.0f, 0.0f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return formatDb(v); })));

    return layout;
}
