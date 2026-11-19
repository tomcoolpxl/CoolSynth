#pragma once

#include <optional>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

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

    /** The app's own settings: its audio device, multi-core, the velocity curve, the zoom. The controller setup is
        the computer's, in its own file (D211). */
    class StandaloneSettingsStore final
    {
    public:
        explicit StandaloneSettingsStore(juce::PropertySet& propertySet);

        bool hasPersistedAudioSetup() const
        {
            return propertySet.containsKey("audioSetup");
        }

        std::optional<PersistedAudioSelection> loadPersistedAudioSelection() const;

        // Editor zoom in percent; 0 means "not set yet" (fit to the display).
        int getUiScalePercent() const
        {
            return propertySet.getIntValue("uiScalePercent", 0);
        }

        void setUiScalePercent(int percent)
        {
            propertySet.setValue("uiScalePercent", percent);
        }

        // Multi-core (D164, D167): true is Auto, sounds that overlap render on several cores; false keeps all on the
        // audio thread. Auto when never set.
        bool getMultiCore() const
        {
            return propertySet.getBoolValue("multiCore", true);
        }

        void setMultiCore(bool on)
        {
            propertySet.setValue("multiCore", on);
        }

        /** The keyboard's velocity curve (D175): 0 Soft, 1 Normal, 2 Hard. */
        int getVelocityCurve() const
        {
            return juce::jlimit(0, 2, propertySet.getIntValue("velocityCurve", 1));
        }

        void setVelocityCurve(int curve)
        {
            propertySet.setValue("velocityCurve", juce::jlimit(0, 2, curve));
        }

    private:
        juce::PropertySet& propertySet;
    };

    void bindStandaloneSettingsStore(StandaloneSettingsStore* store) noexcept;
    StandaloneSettingsStore* getStandaloneSettingsStore() noexcept;
}
