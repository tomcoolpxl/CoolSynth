#pragma once

#include <optional>
#include <span>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include "midi/MidiLearn.h"

namespace coolsynth::standalone
{
    struct PersistedAudioSelection
    {
        juce::String deviceTypeName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;

        bool isValid() const noexcept
        {
            return deviceTypeName.isNotEmpty() && sampleRateHz > 0.0 && bufferSizeSamples > 0;
        }

        bool hasNamedOutput() const noexcept
        {
            return outputDeviceName.isNotEmpty();
        }
    };

    struct PersistedMidiInputSelection
    {
        juce::String identifier;
        juce::String name;

        bool isValid() const noexcept
        {
            return identifier.isNotEmpty();
        }
    };

    class StandaloneSettingsStore final
    {
    public:
        explicit StandaloneSettingsStore(juce::PropertySet& propertySet)
            : propertySet(propertySet)
        {
        }

        bool hasPersistedAudioSetup() const
        {
            return propertySet.containsKey("audioSetup");
        }

        std::optional<PersistedAudioSelection> loadPersistedAudioSelection() const;
        std::optional<PersistedMidiInputSelection> loadPersistedMidiInputSelection() const;

        void savePersistedMidiInputSelection(const juce::MidiDeviceInfo& device)
        {
            propertySet.setValue("midiInputIdentifier", device.identifier);
            propertySet.setValue("midiInputName", device.name);
        }

        void clearPersistedMidiInputSelection()
        {
            propertySet.removeValue("midiInputIdentifier");
            propertySet.removeValue("midiInputName");
        }

        std::vector<coolsynth::midi::LearnedCcBinding> loadLearnedMidiMappings() const;
        void saveLearnedMidiMappings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
        void clearLearnedMidiMappings();

    private:
        juce::PropertySet& propertySet;
    };

    void bindStandaloneSettingsStore(StandaloneSettingsStore* store) noexcept;
    StandaloneSettingsStore* getStandaloneSettingsStore() noexcept;
}
