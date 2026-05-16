#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace coolsynth::presets
{
    struct PresetParameterValue
    {
        const char* parameterId;
        float value; // Raw value in the parameter's own units (Hz, ms, semitones, normalized 0-1, or choice/bool index).
    };

    struct FactoryPreset
    {
        const char* name;
        const char* category;
        const PresetParameterValue* values;
        int valueCount;
    };

    int getFactoryPresetCount() noexcept;
    const FactoryPreset& getFactoryPreset(int index) noexcept;

    void applyFactoryPreset(juce::AudioProcessorValueTreeState& apvts,
                            const FactoryPreset& preset);
}
