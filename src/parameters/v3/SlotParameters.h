#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "synth/v3/Modulation.h"
#include "synth/v3/Module.h"

/*
    V3 slot parameters (DESIGN_V3.md section 9.1).

    Every slot exposes one type choice and eight generic floats in normalised 0..1 form whose
    names, ranges, skews and units come from the descriptor of the slot's current type. Generator
    slots add fixed parameters (level, pan, octave, semitone, fine, key tracking, combine). IDs
    follow the slot path, never the type: pA.g1.type, pA.g1.p1 .. pA.g1.p8, pA.g1.level, ...
*/
namespace coolsynth::parameters::v3
{
    inline constexpr int parameterVersionHint = 8;
    inline constexpr int genericParameterCount = coolsynth::synth::v3::maxSlotParameters;

    enum class Part { a, b };
    enum class SlotKind { generator, processor, insert, master };

    struct SlotPath
    {
        Part part = Part::a;      // ignored for master slots
        SlotKind kind = SlotKind::generator;
        int index = 1;            // 1-based, as shown to the user
    };

    [[nodiscard]] juce::String slotPrefix(const SlotPath& path);                       // "pA.g1", "f3"
    [[nodiscard]] juce::String typeParameterId(const SlotPath& path);                  // "pA.g1.type"
    [[nodiscard]] juce::String genericParameterId(const SlotPath& path, int oneBased); // "pA.g1.p1"
    [[nodiscard]] juce::String fixedParameterId(const SlotPath& path, const char* suffix);

    using CombineMode = coolsynth::synth::v3::Combine;
    inline const juce::StringArray combineModeNames { "Off", "Sync", "Ring", "FM", "AM" };
    inline constexpr int octaveMin = -2, octaveMax = 2;
    inline constexpr int semitoneMin = -12, semitoneMax = 12;
    inline constexpr float fineCentsMin = -50.0f, fineCentsMax = 50.0f;

    /** Lower case without spaces, '-', '_' or '/': how option names and choices are compared. */
    [[nodiscard]] juce::String normaliseOptionName(const juce::String& name);

    /** Text for a descriptor value in its unit, e.g. "632.5 Hz", "25 %", "-3.0 dB", "Saw", "Saw > Pulse 40 %". */
    [[nodiscard]] juce::String formatDescriptorValue(const coolsynth::synth::v3::ParameterDescriptor& descriptor,
                                                     float units);
    /** Parses an option name or the number in text, honouring "kHz" and "s" multipliers; false when there is neither. */
    [[nodiscard]] bool parseDescriptorValue(const coolsynth::synth::v3::ParameterDescriptor& descriptor,
                                            const juce::String& text,
                                            float& units);

    /**
        A generic slot parameter. Stored normalised 0..1; its name and text follow the descriptor
        of the type currently selected in the slot's type parameter.
    */
    class GenericSlotParameter final : public juce::AudioParameterFloat
    {
    public:
        /** The default value is the default of this parameter for the slot's default type. */
        GenericSlotParameter(const juce::ParameterID& parameterId,
                             int zeroBasedIndex,
                             coolsynth::synth::v3::SlotTypeTable typeTable,
                             const juce::AudioParameterChoice& typeParameter,
                             int defaultType = 0);

        juce::String getName(int maximumStringLength) const override;
        juce::String getText(float normalisedValue, int maximumStringLength) const override;
        float getValueForText(const juce::String& text) const override;

        [[nodiscard]] const coolsynth::synth::v3::ModuleDescriptor& currentDescriptor() const noexcept;
        [[nodiscard]] bool isUsedByCurrentType() const noexcept;

        /** Text in the current type's units to a normalised value; false if it is not a value or lies outside the range. */
        [[nodiscard]] bool tryNormalisedForText(const juce::String& text, float& normalised) const;

        /** The current type's default for this parameter, normalised; 0 when the type does not use it. */
        [[nodiscard]] float currentTypeDefault() const noexcept;

    private:
        const int index;
        const coolsynth::synth::v3::SlotTypeTable table;
        const juce::AudioParameterChoice& typeChoice;
    };

    /** Builds the parameter group of one slot: type, p1..p8 and, for generators, the fixed parameters. */
    [[nodiscard]] std::unique_ptr<juce::AudioProcessorParameterGroup> makeSlotGroup(const SlotPath& path,
                                                                                    coolsynth::synth::v3::SlotTypeTable typeTable,
                                                                                    const juce::String& groupName,
                                                                                    int defaultType = 0);

    /** Number of parameters makeSlotGroup() creates for a kind. */
    [[nodiscard]] int parameterCountForKind(SlotKind kind) noexcept;

    /** Raw value pointers of one slot, resolved by ID once after the tree exists. Null where the kind has no such parameter. */
    struct SlotParameterPointers
    {
        std::atomic<float>* type = nullptr;
        std::array<std::atomic<float>*, genericParameterCount> p {};
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* semitone = nullptr;
        std::atomic<float>* fineCents = nullptr;
        std::atomic<float>* keyTrack = nullptr;
        std::atomic<float>* combine = nullptr;
        std::atomic<float>* amount = nullptr;
    };

    [[nodiscard]] SlotParameterPointers bindSlotPointers(juce::AudioProcessorValueTreeState& state, const SlotPath& path);

    /** What the voice needs from one slot per block, read lock-free from the pointers. */
    struct SlotTargets
    {
        int typeIndex = 0;
        std::array<float, genericParameterCount> p {};
        float level = 1.0f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f;
        bool keyTrack = true;
        CombineMode combine = CombineMode::off;
        float amount = 0.5f;
    };

    [[nodiscard]] SlotTargets readSlotTargets(const SlotParameterPointers& pointers) noexcept;

    /** Pushes the smoothed targets of a slot into its SlotBase. The type goes to Slot::requestType separately. */
    void applySlotTargets(const SlotTargets& targets, coolsynth::synth::v3::SlotBase& base) noexcept;
}
