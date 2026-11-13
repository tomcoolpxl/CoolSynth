#include "FactoryPresets.h"

/*
    The factory bank. The V2 bank went with the V2 engine; the rooms of SOUND_BANK_V3.md arrive
    with the v3 preset pipeline in phase 1.5. Applying a preset first returns every parameter to
    its default and then sets the listed values, so a preset never inherits the previous patch.
*/
namespace coolsynth::presets
{
    namespace
    {
        const FactoryPreset kPresets[] = {
            { "Init", "Workshop", nullptr, 0 },
        };

        constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    }

    int getFactoryPresetCount() noexcept
    {
        return kPresetCount;
    }

    const FactoryPreset& getFactoryPreset(int index) noexcept
    {
        return kPresets[juce::jlimit(0, kPresetCount - 1, index)];
    }

    void applyFactoryPreset(juce::AudioProcessorValueTreeState& apvts,
                            const FactoryPreset& preset)
    {
        for (auto* base : apvts.processor.getParameters())
        {
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(base))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        }

        for (int i = 0; i < preset.valueCount; ++i)
        {
            const auto& entry = preset.values[i];
            if (auto* parameter = apvts.getParameter(entry.parameterId))
            {
                const auto normalised = parameter->getNormalisableRange().convertTo0to1(entry.value);
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
                parameter->endChangeGesture();
            }
        }
    }
}
