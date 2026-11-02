#include "SettingsStore.h"

#include <juce_data_structures/juce_data_structures.h>

#include "midi/LearnedBindingsXml.h"

namespace coolsynth::standalone
{
    static StandaloneSettingsStore* globalSettingsStore = nullptr;

    namespace
    {
        juce::String toSelectionModeString(ControllerProfileSelectionMode mode)
        {
            switch (mode)
            {
                case ControllerProfileSelectionMode::none:            return "none";
                case ControllerProfileSelectionMode::explicitProfile: return "explicit";
                case ControllerProfileSelectionMode::autoDetect:
                default: return "auto";
            }
        }

        ControllerProfileSelectionMode parseSelectionMode(juce::StringRef mode) noexcept
        {
            const juce::String value(mode);

            if (value == "none")
                return ControllerProfileSelectionMode::none;

            if (value == "explicit")
                return ControllerProfileSelectionMode::explicitProfile;

            return ControllerProfileSelectionMode::autoDetect;
        }

        void removeMidiInputChildren(juce::XmlElement& xml)
        {
            for (auto* child = xml.getFirstChildElement(); child != nullptr;)
            {
                auto* next = child->getNextElement();
                if (child->hasTagName("MIDIINPUT"))
                    xml.removeChildElement(child, true);

                child = next;
            }
        }
    }

    void bindStandaloneSettingsStore(StandaloneSettingsStore* store) noexcept
    {
        globalSettingsStore = store;
    }

    StandaloneSettingsStore* getStandaloneSettingsStore() noexcept
    {
        return globalSettingsStore;
    }

    std::optional<PersistedAudioSelection> StandaloneSettingsStore::loadPersistedAudioSelection() const
    {
        auto xml = propertySet.getXmlValue("audioSetup");

        if (xml == nullptr || !xml->hasTagName("DEVICESETUP"))
            return std::nullopt;

        PersistedAudioSelection selection;
        selection.deviceTypeName = xml->getStringAttribute("deviceType");
        selection.outputDeviceName = xml->getStringAttribute("audioOutputDeviceName");
        selection.sampleRateHz = xml->getDoubleAttribute("audioDeviceRate", 0.0);
        selection.bufferSizeSamples = xml->getIntAttribute("audioDeviceBufferSize", 0);

        if (!selection.isValid())
            return std::nullopt;

        return selection;
    }

    PersistedControllerProfileSelection StandaloneSettingsStore::loadPersistedControllerProfileSelection() const
    {
        PersistedControllerProfileSelection selection;
        selection.mode = parseSelectionMode(propertySet.getValue("controllerProfileSelectionMode", "auto"));
        selection.profileId = propertySet.getValue("controllerProfileId");

        if (!selection.isValid())
            selection = {};

        return selection;
    }

    std::optional<PersistedMidiInputSelection> StandaloneSettingsStore::loadPersistedMidiInputSelection() const
    {
        PersistedMidiInputSelection selection;
        selection.identifier = propertySet.getValue("midiInputIdentifier");
        selection.name = propertySet.getValue("midiInputName");

        if (!selection.isValid())
            return std::nullopt;

        return selection;
    }

    std::vector<coolsynth::midi::LearnedCcBinding> StandaloneSettingsStore::loadLearnedMidiMappings() const
    {
        auto xml = propertySet.getXmlValue("midiLearnMappings");

        if (xml == nullptr || !xml->hasTagName("MIDI_LEARN_MAPPINGS"))
            return {};

        return coolsynth::midi::parseLearnedBindingsXml(*xml);
    }

    void StandaloneSettingsStore::saveLearnedMidiMappings(std::span<const coolsynth::midi::LearnedCcBinding> bindings)
    {
        const auto xml = coolsynth::midi::createLearnedBindingsXml("MIDI_LEARN_MAPPINGS", bindings);
        propertySet.setValue("midiLearnMappings", xml.get());
    }

    void StandaloneSettingsStore::savePersistedControllerProfileSelection(PersistedControllerProfileSelection selection)
    {
        if (!selection.isValid())
            selection = {};

        propertySet.setValue("controllerProfileSelectionMode", toSelectionModeString(selection.mode));
        propertySet.setValue("controllerProfileId", selection.profileId);
    }

    void StandaloneSettingsStore::clearLearnedMidiMappings()
    {
        propertySet.removeValue("midiLearnMappings");
    }

    void StandaloneSettingsStore::clearStandaloneMidiState()
    {
        clearPersistedMidiInputSelection();
        clearPersistedControllerProfileSelection();
        clearLearnedMidiMappings();
        propertySet.removeValue("showCcLabels");

        if (auto xml = propertySet.getXmlValue("audioSetup"))
        {
            if (xml->hasTagName("DEVICESETUP"))
            {
                removeMidiInputChildren(*xml);
                propertySet.setValue("audioSetup", xml.get());
            }
        }

        if (auto* propertiesFile = dynamic_cast<juce::PropertiesFile*>(&propertySet))
            propertiesFile->saveIfNeeded();
    }
}
