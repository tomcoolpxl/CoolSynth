#include "SlotParameters.h"

#include <cmath>

namespace coolsynth::parameters::v3
{
    namespace
    {
        using coolsynth::synth::v3::ParameterDescriptor;
        using coolsynth::synth::v3::Unit;

        const char* kindPrefix(SlotKind kind) noexcept
        {
            switch (kind)
            {
                case SlotKind::generator: return "g";
                case SlotKind::processor: return "p";
                case SlotKind::insert:    return "i";
                case SlotKind::master:    return "f";
            }
            return "x";
        }

        juce::ParameterID makeId(const juce::String& id)
        {
            return { id, parameterVersionHint };
        }

        juce::String formatGeneric(float value)
        {
            const float magnitude = std::abs(value);
            if (magnitude >= 100.0f)
                return juce::String(static_cast<int>(std::round(value)));
            if (magnitude >= 10.0f)
                return juce::String(value, 1);
            return juce::String(value, 2);
        }
    }

    juce::String slotPrefix(const SlotPath& path)
    {
        const juce::String slot = juce::String(kindPrefix(path.kind)) + juce::String(path.index);
        if (path.kind == SlotKind::master)
            return slot;
        return juce::String(path.part == Part::a ? "pA." : "pB.") + slot;
    }

    juce::String typeParameterId(const SlotPath& path)
    {
        return slotPrefix(path) + ".type";
    }

    juce::String genericParameterId(const SlotPath& path, int oneBased)
    {
        jassert(oneBased >= 1 && oneBased <= genericParameterCount);
        return slotPrefix(path) + ".p" + juce::String(oneBased);
    }

    juce::String fixedParameterId(const SlotPath& path, const char* suffix)
    {
        return slotPrefix(path) + "." + suffix;
    }

    juce::String formatDescriptorValue(const ParameterDescriptor& descriptor, float units)
    {
        switch (descriptor.unit)
        {
            case Unit::hertz:
                if (units >= 1000.0f)
                    return juce::String(units / 1000.0f, 2) + " kHz";
                return juce::String(units, units < 100.0f ? 2 : 1) + " Hz";
            case Unit::milliseconds:
                if (units >= 1000.0f)
                    return juce::String(units / 1000.0f, 2) + " s";
                return juce::String(static_cast<int>(std::round(units))) + " ms";
            case Unit::decibels:
                return juce::String(units, 1) + " dB";
            case Unit::semitones:
                return juce::String(units, 1) + " st";
            case Unit::cents:
                return juce::String(static_cast<int>(std::round(units))) + " ct";
            case Unit::percent:
                return juce::String(static_cast<int>(std::round(units * 100.0f))) + " %";
            case Unit::choice:
                return juce::String(static_cast<int>(std::round(units)));
            case Unit::generic:
                break;
        }
        return formatGeneric(units);
    }

    bool parseDescriptorValue(const ParameterDescriptor& descriptor, const juce::String& text, float& units)
    {
        const juce::String trimmed = text.trim();
        int end = 0;
        while (end < trimmed.length())
        {
            const auto c = trimmed[end];
            const bool numeric = juce::CharacterFunctions::isDigit(c) || c == '.' || c == '-' || c == '+' || c == ',';
            if (! numeric)
                break;
            ++end;
        }
        if (end == 0)
            return false;

        float value = trimmed.substring(0, end).replaceCharacter(',', '.').getFloatValue();
        const juce::String suffix = trimmed.substring(end).trim().toLowerCase();

        if (descriptor.unit == Unit::hertz && suffix.startsWith("k"))
            value *= 1000.0f;
        else if (descriptor.unit == Unit::milliseconds && suffix.startsWith("s"))
            value *= 1000.0f;
        else if (descriptor.unit == Unit::percent)
            value /= 100.0f;

        units = value;
        return true;
    }

    GenericSlotParameter::GenericSlotParameter(const juce::ParameterID& parameterId,
                                               int zeroBasedIndex,
                                               coolsynth::synth::v3::SlotTypeTable typeTable,
                                               const juce::AudioParameterChoice& typeParameter)
        : juce::AudioParameterFloat(parameterId,
                                    "P" + juce::String(zeroBasedIndex + 1),
                                    juce::NormalisableRange<float>(0.0f, 1.0f),
                                    [&]
                                    {
                                        const auto& d = typeTable.descriptor(0);
                                        if (zeroBasedIndex >= d.usedParams)
                                            return 0.0f;
                                        const auto& p = d.params[static_cast<std::size_t>(zeroBasedIndex)];
                                        return coolsynth::synth::v3::unitsToNormalised(p, p.def);
                                    }())
        , index(zeroBasedIndex)
        , table(typeTable)
        , typeChoice(typeParameter)
    {
    }

    const coolsynth::synth::v3::ModuleDescriptor& GenericSlotParameter::currentDescriptor() const noexcept
    {
        const int typeIndex = juce::jlimit(0, table.count - 1, typeChoice.getIndex());
        return table.descriptor(typeIndex);
    }

    bool GenericSlotParameter::isUsedByCurrentType() const noexcept
    {
        return index < currentDescriptor().usedParams;
    }

    juce::String GenericSlotParameter::getName(int maximumStringLength) const
    {
        if (! isUsedByCurrentType())
            return juce::AudioParameterFloat::getName(maximumStringLength);
        return juce::String(currentDescriptor().params[static_cast<std::size_t>(index)].name).substring(0, maximumStringLength);
    }

    juce::String GenericSlotParameter::getText(float normalisedValue, int maximumStringLength) const
    {
        if (! isUsedByCurrentType())
            return "-";
        const auto& d = currentDescriptor().params[static_cast<std::size_t>(index)];
        return formatDescriptorValue(d, coolsynth::synth::v3::descriptorToUnits(d, normalisedValue)).substring(0, maximumStringLength);
    }

    float GenericSlotParameter::getValueForText(const juce::String& text) const
    {
        if (! isUsedByCurrentType())
            return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
        const auto& d = currentDescriptor().params[static_cast<std::size_t>(index)];
        float units = 0.0f;
        if (! parseDescriptorValue(d, text, units))
            return get();
        return coolsynth::synth::v3::unitsToNormalised(d, units);
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makeSlotGroup(const SlotPath& path,
                                                                      coolsynth::synth::v3::SlotTypeTable typeTable,
                                                                      const juce::String& groupName)
    {
        jassert(typeTable.count > 0 && typeTable.descriptor != nullptr);

        juce::StringArray typeNames;
        for (int i = 0; i < typeTable.count; ++i)
            typeNames.add(typeTable.descriptor(i).name);

        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(slotPrefix(path), groupName, "|");

        auto typeParameter = std::make_unique<juce::AudioParameterChoice>(makeId(typeParameterId(path)), "Type", typeNames, 0);
        const auto& typeRef = *typeParameter;
        group->addChild(std::move(typeParameter));

        for (int i = 0; i < genericParameterCount; ++i)
            group->addChild(std::make_unique<GenericSlotParameter>(makeId(genericParameterId(path, i + 1)), i, typeTable, typeRef));

        if (path.kind == SlotKind::generator)
        {
            group->addChild(std::make_unique<juce::AudioParameterFloat>(
                makeId(fixedParameterId(path, "level")), "Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
            group->addChild(std::make_unique<juce::AudioParameterFloat>(
                makeId(fixedParameterId(path, "pan")), "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
            group->addChild(std::make_unique<juce::AudioParameterInt>(
                makeId(fixedParameterId(path, "oct")), "Octave", octaveMin, octaveMax, 0));
            group->addChild(std::make_unique<juce::AudioParameterInt>(
                makeId(fixedParameterId(path, "semi")), "Semitone", semitoneMin, semitoneMax, 0));
            group->addChild(std::make_unique<juce::AudioParameterFloat>(
                makeId(fixedParameterId(path, "fine")), "Fine", juce::NormalisableRange<float>(fineCentsMin, fineCentsMax), 0.0f));
            group->addChild(std::make_unique<juce::AudioParameterBool>(
                makeId(fixedParameterId(path, "key")), "Key Track", true));
            group->addChild(std::make_unique<juce::AudioParameterChoice>(
                makeId(fixedParameterId(path, "combine")), "Combine", combineModeNames, 0));
        }

        return group;
    }

    int parameterCountForKind(SlotKind kind) noexcept
    {
        constexpr int common = 1 + genericParameterCount;
        return kind == SlotKind::generator ? common + 7 : common;
    }

    SlotParameterPointers bindSlotPointers(juce::AudioProcessorValueTreeState& state, const SlotPath& path)
    {
        SlotParameterPointers pointers;
        pointers.type = state.getRawParameterValue(typeParameterId(path));
        for (int i = 0; i < genericParameterCount; ++i)
            pointers.p[static_cast<std::size_t>(i)] = state.getRawParameterValue(genericParameterId(path, i + 1));

        if (path.kind == SlotKind::generator)
        {
            pointers.level = state.getRawParameterValue(fixedParameterId(path, "level"));
            pointers.pan = state.getRawParameterValue(fixedParameterId(path, "pan"));
            pointers.octave = state.getRawParameterValue(fixedParameterId(path, "oct"));
            pointers.semitone = state.getRawParameterValue(fixedParameterId(path, "semi"));
            pointers.fineCents = state.getRawParameterValue(fixedParameterId(path, "fine"));
            pointers.keyTrack = state.getRawParameterValue(fixedParameterId(path, "key"));
            pointers.combine = state.getRawParameterValue(fixedParameterId(path, "combine"));
        }
        return pointers;
    }

    SlotTargets readSlotTargets(const SlotParameterPointers& pointers) noexcept
    {
        SlotTargets targets;
        if (pointers.type != nullptr)
            targets.typeIndex = static_cast<int>(std::lround(pointers.type->load()));
        for (std::size_t i = 0; i < pointers.p.size(); ++i)
            targets.p[i] = pointers.p[i] != nullptr ? pointers.p[i]->load() : 0.0f;

        if (pointers.level != nullptr)
            targets.level = pointers.level->load();
        if (pointers.pan != nullptr)
            targets.pan = pointers.pan->load();

        float semitones = 0.0f;
        if (pointers.octave != nullptr)
            semitones += 12.0f * pointers.octave->load();
        if (pointers.semitone != nullptr)
            semitones += pointers.semitone->load();
        if (pointers.fineCents != nullptr)
            semitones += pointers.fineCents->load() / 100.0f;
        targets.pitchSemitones = semitones;

        if (pointers.keyTrack != nullptr)
            targets.keyTrack = pointers.keyTrack->load() >= 0.5f;
        if (pointers.combine != nullptr)
            targets.combine = static_cast<CombineMode>(juce::jlimit(0, combineModeNames.size() - 1,
                                                                   static_cast<int>(std::lround(pointers.combine->load()))));
        return targets;
    }

    void applySlotTargets(const SlotTargets& targets, coolsynth::synth::v3::SlotBase& base) noexcept
    {
        for (int i = 0; i < genericParameterCount; ++i)
            base.setTarget(i, targets.p[static_cast<std::size_t>(i)]);
        base.setLevelTarget(targets.level);
        base.setPanTarget(targets.pan);
        base.setPitchTarget(targets.pitchSemitones);
    }
}
