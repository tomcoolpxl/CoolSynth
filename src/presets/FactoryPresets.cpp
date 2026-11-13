#include "FactoryPresets.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <FactoryBankData.h>

#include "parameters/v3/SlotParameters.h"

namespace coolsynth::presets
{
    namespace
    {
        struct Bank
        {
            std::vector<FactoryPreset> presets;
            juce::StringArray errors;
        };

        const Bank& bank()
        {
            static const Bank instance = []
            {
                Bank result;
                std::vector<std::pair<juce::String, int>> files;
                for (int i = 0; i < FactoryBankData::namedResourceListSize; ++i)
                    files.emplace_back(FactoryBankData::originalFilenames[i], i);
                std::sort(files.begin(), files.end());

                for (const auto& [fileName, index] : files)
                {
                    int size = 0;
                    const char* data = FactoryBankData::getNamedResource(FactoryBankData::namedResourceList[index], size);
                    auto parsed = parseBankFile(juce::String::fromUTF8(data, size), fileName);
                    result.errors.addArray(parsed.errors);
                    for (auto& preset : parsed.presets)
                        result.presets.push_back(std::move(preset));
                }

                if (result.presets.empty())
                    result.presets.push_back({ "Init", "Workshop", {}, {} });
                return result;
            }();
            return instance;
        }

        bool isNumber(const juce::String& text)
        {
            const auto t = text.trim();
            return t.isNotEmpty() && t.containsOnly("0123456789+-.eE") && t.containsAnyOf("0123456789");
        }

        std::optional<float> toNormalised(juce::RangedAudioParameter& parameter, const juce::String& text, juce::String& problem)
        {
            if (auto* generic = dynamic_cast<coolsynth::parameters::v3::GenericSlotParameter*>(&parameter))
            {
                float normalised = 0.0f;
                if (generic->tryNormalisedForText(text, normalised))
                    return normalised;
                problem = generic->isUsedByCurrentType() ? "not a value of " + generic->getName(64) : "unused by this slot type";
                return std::nullopt;
            }

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&parameter))
            {
                const auto wanted = coolsynth::parameters::v3::normaliseOptionName(text);
                for (int i = 0; i < choice->choices.size(); ++i)
                    if (coolsynth::parameters::v3::normaliseOptionName(choice->choices[i]) == wanted)
                        return choice->convertTo0to1(static_cast<float>(i));
                problem = "expected one of " + choice->choices.joinIntoString(", ");
                return std::nullopt;
            }

            if (dynamic_cast<juce::AudioParameterBool*>(&parameter) != nullptr)
            {
                const auto t = text.trim().toLowerCase();
                if (t == "on" || t == "true" || t == "yes" || t == "1")
                    return 1.0f;
                if (t == "off" || t == "false" || t == "no" || t == "0")
                    return 0.0f;
                problem = "expected on or off";
                return std::nullopt;
            }

            if (! isNumber(text))
            {
                problem = "not a number";
                return std::nullopt;
            }
            const float value = text.trim().getFloatValue();
            const auto& range = parameter.getNormalisableRange();
            const float tolerance = 1.0e-4f * (range.end - range.start);
            if (value < range.start - tolerance || value > range.end + tolerance)
            {
                problem = "outside " + juce::String(range.start) + " .. " + juce::String(range.end);
                return std::nullopt;
            }
            return parameter.convertTo0to1(juce::jlimit(range.start, range.end, value));
        }

        void set(juce::RangedAudioParameter& parameter, float normalised)
        {
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost(normalised);
            parameter.endChangeGesture();
        }
    }

    int getFactoryPresetCount() noexcept
    {
        return static_cast<int>(bank().presets.size());
    }

    const FactoryPreset& getFactoryPreset(int index) noexcept
    {
        const auto& presets = bank().presets;
        return presets[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(presets.size()) - 1, index))];
    }

    const juce::StringArray& getFactoryBankErrors() noexcept
    {
        return bank().errors;
    }

    juce::StringArray applyFactoryPreset(juce::AudioProcessorValueTreeState& apvts, const FactoryPreset& preset)
    {
        juce::StringArray problems;

        for (auto* base : apvts.processor.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(base))
                set(*parameter, parameter->getDefaultValue());

        for (const auto& entry : preset.entries)
        {
            auto* parameter = apvts.getParameter(entry.parameterId);
            if (parameter == nullptr)
            {
                problems.add(preset.name + ": unknown parameter " + entry.parameterId);
                continue;
            }

            juce::String problem;
            const auto normalised = toNormalised(*parameter, entry.text, problem);
            if (! normalised)
            {
                problems.add(preset.name + ": " + entry.parameterId + " = '" + entry.text + "': " + problem);
                continue;
            }
            set(*parameter, *normalised);

            // A new slot type starts from its own defaults, not from what the slot held before.
            if (entry.parameterId.endsWith(".type"))
            {
                const auto prefix = entry.parameterId.dropLastCharacters(5);
                for (int i = 1; i <= coolsynth::parameters::v3::genericParameterCount; ++i)
                    if (auto* generic = dynamic_cast<coolsynth::parameters::v3::GenericSlotParameter*>(apvts.getParameter(prefix + ".p" + juce::String(i))))
                        set(*generic, generic->currentTypeDefault());
            }
        }

        return problems;
    }
}
