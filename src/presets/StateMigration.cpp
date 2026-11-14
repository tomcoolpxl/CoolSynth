#include "StateMigration.h"

#include <array>
#include <set>

#include "parameters/v3/PartParameters.h"
#include "synth/v3/Inserts.h"

namespace coolsynth::presets
{
    namespace
    {
        namespace pv3 = coolsynth::parameters::v3;
        namespace sv3 = coolsynth::synth::v3;

        /** One effect of the old fixed chain: its on switch, the slot type it becomes, and its parameters in the
            order of that type's values. */
        struct FixedEffect
        {
            const char* enabled;
            int type;
            std::array<const char*, 8> values;
        };

        // The old chain's order: the order the migrated effects take the slots in.
        const std::array<FixedEffect, 10> fixedChain {{
            { "driveEnabled", sv3::insertType::drive, { "driveAmount", "driveMix" } },
            { "lofiEnabled", sv3::insertType::lofi, { "lofiWow", "lofiFlutter", "lofiHiss", "lofiCrackle", "lofiBandwidth",
                                                      "lofiBits", "lofiRate", "lofiMix" } },
            { "phaserEnabled", sv3::insertType::phaser, { "phaserRateHz", "phaserDepth" } },
            { "flangerEnabled", sv3::insertType::flanger, { "flangerRate", "flangerDepth", "flangerFeedback", "flangerMix" } },
            { "chorusEnabled", sv3::insertType::chorus, { "chorusRateHz", "chorusDepth", "chorusMix" } },
            { "delayEnabled", sv3::insertType::delay, { "delayTimeMs", "delayFeedback", "delayMix", "delaySync", "delayDivision",
                                                        "delayMode" } },
            { "reverbEnabled", sv3::insertType::reverb, { "reverbSize", "reverbDamping", "reverbMix", "reverbPreDelay",
                                                          "reverbLowCut", "reverbModulation", "reverbShimmer" } },
            { "compressorEnabled", sv3::insertType::compressor, { "compressorAmount", "compressorMix" } },
            { "eqEnabled", sv3::insertType::eq, { "eqLow", "eqTilt", "eqHigh" } },
            { "pumpEnabled", sv3::insertType::pump, { "pumpDepth", "pumpDivision", "pumpRelease" } },
        }};

        constexpr char parameterTag[] = "PARAM";
        constexpr char idAttribute[] = "id";
        constexpr char valueAttribute[] = "value";
    }

    std::map<juce::String, juce::String> migrateFixedMasterEffects(juce::XmlElement& parameterState)
    {
        // The stored values are in each parameter's own units (a bool or a choice as its index).
        std::map<juce::String, double> stored;
        for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
            stored[child->getStringAttribute(idAttribute)] = child->getDoubleAttribute(valueAttribute);

        std::map<juce::String, juce::String> renamed;
        std::map<juce::String, float> slotValues; // master slot parameter ID -> its stored value
        std::set<juce::String> fixedIds;
        int slot = 0;
        for (const auto& effect : fixedChain)
        {
            fixedIds.insert(effect.enabled);
            const auto& descriptor = sv3::insertDescriptor(effect.type);
            for (int i = 0; i < descriptor.usedParams; ++i)
                fixedIds.insert(effect.values[static_cast<std::size_t>(i)]);

            const auto on = stored.find(effect.enabled);
            if (on == stored.end() || on->second < 0.5 || slot >= pv3::masterEffectSlots)
                continue;
            ++slot;
            const pv3::SlotPath path { pv3::Part::a, pv3::SlotKind::master, slot };
            slotValues[pv3::typeParameterId(path)] = static_cast<float>(effect.type);
            for (int i = 0; i < descriptor.usedParams; ++i)
            {
                const auto& parameter = descriptor.params[static_cast<std::size_t>(i)];
                const juce::String oldId = effect.values[static_cast<std::size_t>(i)];
                const auto value = stored.find(oldId);
                const float units = value != stored.end() ? static_cast<float>(value->second) : parameter.def;
                const auto newId = pv3::genericParameterId(path, i + 1);
                slotValues[newId] = sv3::unitsToNormalised(parameter, units);
                renamed[oldId] = newId;
            }
        }

        // Out with the fixed effects' parameters; in with every master slot parameter the state lacks.
        for (int i = parameterState.getNumChildElements(); --i >= 0;)
            if (auto* child = parameterState.getChildElement(i);
                child->hasTagName(parameterTag) && fixedIds.count(child->getStringAttribute(idAttribute)) != 0)
                parameterState.removeChildElement(child, true);

        for (int n = 1; n <= pv3::masterEffectSlots; ++n)
        {
            const pv3::SlotPath path { pv3::Part::a, pv3::SlotKind::master, n };
            juce::StringArray ids { pv3::typeParameterId(path) };
            for (int i = 1; i <= pv3::genericParameterCount; ++i)
                ids.add(pv3::genericParameterId(path, i));
            for (const auto& id : ids)
            {
                if (stored.count(id) != 0)
                    continue;
                auto* child = parameterState.createNewChildElement(parameterTag);
                child->setAttribute(idAttribute, id);
                const auto value = slotValues.find(id);
                child->setAttribute(valueAttribute, value != slotValues.end() ? static_cast<double>(value->second) : 0.0);
            }
        }
        return renamed;
    }
}
