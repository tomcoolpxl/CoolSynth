#include "StateMigration.h"

#include <array>
#include <set>

#include "parameters/ParameterIDs.h"
#include "parameters/v3/PartParameters.h"
#include "synth/v3/EffectRoutes.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/Voice.h"

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

    std::map<juce::String, juce::String> migrateLayerArps(juce::XmlElement& parameterState)
    {
        std::map<juce::String, juce::String> renamed;
        auto find = [&](const juce::String& id) -> juce::XmlElement*
        {
            for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
                if (child->getStringAttribute(idAttribute) == id)
                    return child;
            return nullptr;
        };
        auto setValue = [&](const juce::String& id, double value)
        {
            auto* child = find(id);
            if (child == nullptr)
            {
                child = parameterState.createNewChildElement(parameterTag);
                child->setAttribute(idAttribute, id);
            }
            child->setAttribute(valueAttribute, value);
        };
        auto follows = [&](const char* layer)
        {
            const auto* follow = find(juce::String(layer) + ".mix.arp");
            return follow == nullptr || follow->getDoubleAttribute(valueAttribute) >= 0.5;
        };
        const bool followsA = follows("pA"), followsB = follows("pB");

        namespace ids = coolsynth::parameters::ids;
        for (const auto& [a, b] : ids::arpLayerIds)
        {
            const auto oldId = juce::String(a).fromFirstOccurrenceOf(".", false, false); // "pA.arpEnabled": "arpEnabled"
            auto* old = find(oldId);
            if (old == nullptr)
                continue;
            const double value = old->getDoubleAttribute(valueAttribute);
            const bool enabledFlag = oldId == juce::String(ids::arpEnabled).fromFirstOccurrenceOf(".", false, false);
            setValue(a, enabledFlag ? (value >= 0.5 && followsA ? 1.0 : 0.0) : value);
            setValue(b, enabledFlag ? (value >= 0.5 && followsB ? 1.0 : 0.0) : value);
            parameterState.removeChildElement(old, true);
            renamed[oldId] = a;
        }
        for (const char* layer : { "pA", "pB" })
            if (auto* follow = find(juce::String(layer) + ".mix.arp"))
                parameterState.removeChildElement(follow, true);

        // Effect routes: a master slot's destinations come after the insert slots', which grew from three to six.
        namespace d = coolsynth::synth::v3::effectDestination;
        constexpr int oldMasterBase = d::insertBase + 3 * d::perSlot;
        constexpr int added = (d::insertSlots - 3) * d::perSlot;
        for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
            if (const auto id = child->getStringAttribute(idAttribute); id.matchesWildcard("p?.fx*.dst", true))
                if (const int v = juce::roundToInt(child->getDoubleAttribute(valueAttribute)); v >= oldMasterBase)
                    child->setAttribute(valueAttribute, v + added);
        return renamed;
    }

    void migrateLayerModes(juce::XmlElement& parameterState)
    {
        auto find = [&](const juce::String& id) -> juce::XmlElement*
        {
            for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
                if (child->getStringAttribute(idAttribute) == id)
                    return child;
            return nullptr;
        };
        auto setValue = [&](const juce::String& id, double value)
        {
            auto* child = find(id);
            if (child == nullptr)
            {
                child = parameterState.createNewChildElement(parameterTag);
                child->setAttribute(idAttribute, id);
            }
            child->setAttribute(valueAttribute, value);
        };
        auto empty = [&](const juce::String& layer)
        {
            for (int s = 1; s <= 3; ++s)
            {
                setValue(layer + ".g" + juce::String(s) + ".type", 0.0); // Off
                setValue(layer + ".p" + juce::String(s) + ".type", 0.0);
            }
        };

        auto* mode = find(pv3::partModeParameterId);
        const int old = mode != nullptr ? juce::roundToInt(mode->getDoubleAttribute(valueAttribute)) : 0; // absent: A
        switch (old)
        {
            case 1: setValue(pv3::partModeParameterId, 0.0); break;              // Layer: Stack
            case 2: setValue(pv3::partModeParameterId, 1.0); break;              // Split
            case 3: setValue(pv3::partModeParameterId, 0.0); empty("pA"); break; // B alone
            default: setValue(pv3::partModeParameterId, 0.0); empty("pB"); break; // A alone
        }
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

    void migrateNoiseTypes(juce::XmlElement& parameterState)
    {
        namespace sv3 = coolsynth::synth::v3;
        auto find = [&](const juce::String& id) -> juce::XmlElement*
        {
            for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
                if (child->getStringAttribute(idAttribute) == id)
                    return child;
            return nullptr;
        };
        for (const char* layer : { "pA", "pB" })
            for (int slot = 1; slot <= 3; ++slot)
            {
                const juce::String path = juce::String(layer) + ".g" + juce::String(slot) + ".";
                const auto* type = find(path + "type");
                if (type == nullptr || juce::roundToInt(type->getDoubleAttribute(valueAttribute)) != sv3::generatorType::noise)
                    continue;
                // Type is the first value: 0..1 meant 0..3 and means 0..4 now.
                if (auto* value = find(path + "p" + juce::String(sv3::NoiseGenerator::type + 1)); value != nullptr)
                    value->setAttribute(valueAttribute, value->getDoubleAttribute(valueAttribute) * 3.0 / 4.0);
            }
    }

    void migrateSvfModes(juce::XmlElement& parameterState)
    {
        namespace sv3 = coolsynth::synth::v3;
        auto find = [&](const juce::String& id) -> juce::XmlElement*
        {
            for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
                if (child->getStringAttribute(idAttribute) == id)
                    return child;
            return nullptr;
        };
        for (const char* layer : { "pA", "pB" })
            for (int slot = 1; slot <= 3; ++slot)
            {
                const juce::String path = juce::String(layer) + ".p" + juce::String(slot) + ".";
                const auto* type = find(path + "type");
                if (type == nullptr || juce::roundToInt(type->getDoubleAttribute(valueAttribute)) != sv3::processorType::svf)
                    continue;
                // Mode is the fifth value: 0..1 meant 0..3 and means 0..6 now.
                if (auto* value = find(path + "p" + juce::String(sv3::SvfProcessor::mode + 1)); value != nullptr)
                    value->setAttribute(valueAttribute, value->getDoubleAttribute(valueAttribute) * 3.0 / 6.0);
                // Morph, Character and Curve were unused: Clean with no morph is the sound as it was.
                for (const int index : { static_cast<int>(sv3::SvfProcessor::morph), static_cast<int>(sv3::SvfProcessor::character) })
                    if (auto* value = find(path + "p" + juce::String(index + 1)); value != nullptr)
                        value->setAttribute(valueAttribute, 0.0);
            }
    }

    void migrateSampleSlices(juce::XmlElement& parameterState)
    {
        namespace sv3 = coolsynth::synth::v3;
        auto find = [&](const juce::String& id) -> juce::XmlElement*
        {
            for (auto* child : parameterState.getChildWithTagNameIterator(parameterTag))
                if (child->getStringAttribute(idAttribute) == id)
                    return child;
            return nullptr;
        };
        const auto& d = sv3::SampleGenerator::descriptor();
        for (const char* layer : { "pA", "pB" })
            for (int slot = 1; slot <= 3; ++slot)
            {
                const juce::String path = juce::String(layer) + ".g" + juce::String(slot) + ".";
                const auto* type = find(path + "type");
                if (type == nullptr || juce::roundToInt(type->getDoubleAttribute(valueAttribute)) != sv3::generatorType::sample)
                    continue;
                constexpr std::array<int, 3> unused { sv3::SampleGenerator::count, sv3::SampleGenerator::sense, sv3::SampleGenerator::firstKey };
                bool allZero = true;
                for (const int index : unused)
                    if (const auto* value = find(path + "p" + juce::String(index + 1)); value != nullptr && value->getDoubleAttribute(valueAttribute) != 0.0)
                        allZero = false;
                if (! allZero)
                    continue;
                for (const int index : unused)
                {
                    auto* value = find(path + "p" + juce::String(index + 1));
                    if (value == nullptr)
                    {
                        value = parameterState.createNewChildElement(parameterTag);
                        value->setAttribute(idAttribute, path + "p" + juce::String(index + 1));
                    }
                    const auto& parameter = d.params[static_cast<std::size_t>(index)];
                    value->setAttribute(valueAttribute, static_cast<double>(sv3::unitsToNormalised(parameter, parameter.def)));
                }
            }
    }
}
