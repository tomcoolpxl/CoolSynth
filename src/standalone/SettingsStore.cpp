#include "SettingsStore.h"

namespace coolsynth::standalone
{
    static StandaloneSettingsStore* globalSettingsStore = nullptr;

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

    std::optional<PersistedMidiInputSelection> StandaloneSettingsStore::loadPersistedMidiInputSelection() const
    {
        PersistedMidiInputSelection selection;
        selection.identifier = propertySet.getValue("midiInputIdentifier");
        selection.name = propertySet.getValue("midiInputName");

        if (!selection.isValid())
            return std::nullopt;

        return selection;
    }
}
