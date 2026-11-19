#include "SettingsStore.h"

#include <juce_data_structures/juce_data_structures.h>

namespace coolsynth::standalone
{
    static StandaloneSettingsStore* globalSettingsStore = nullptr;

    StandaloneSettingsStore::StandaloneSettingsStore(juce::PropertySet& set) : propertySet(set)
    {
        // What the V2 MIDI input, MIDI learn and controller profiles kept here, gone with them (D212): never read again.
        for (const char* retired : { "midiInputIdentifier", "midiInputName", "midiLearnMappings",
                                     "controllerProfileSelectionMode", "controllerProfileId", "showCcLabels" })
            propertySet.removeValue(retired);
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
}
