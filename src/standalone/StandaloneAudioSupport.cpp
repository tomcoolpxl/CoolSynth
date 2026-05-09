#include "StandaloneAudioSupport.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace coolsynth::standalone
{
    namespace
    {
       #if JucePlugin_Build_Standalone
        juce::StandalonePluginHolder* getStandalonePluginHolder() noexcept
        {
            return juce::StandalonePluginHolder::getInstance();
        }
       #endif
    }

    bool isStandaloneRuntimeAvailable() noexcept
    {
        return juce::JUCEApplicationBase::isStandaloneApp()
            && getStandaloneAudioDeviceManager() != nullptr;
    }

    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept
    {
       #if JucePlugin_Build_Standalone
        if (auto* holder = getStandalonePluginHolder())
            return &holder->deviceManager;
       #endif

        return nullptr;
    }

    juce::PropertySet* getStandaloneSettings() noexcept
    {
       #if JucePlugin_Build_Standalone
        if (auto* holder = getStandalonePluginHolder())
            return holder->settings.get();
       #endif

        return nullptr;
    }

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot()
    {
        if (auto* deviceManager = getStandaloneAudioDeviceManager())
            return captureAudioDeviceSnapshot(*deviceManager, getStandaloneSettingsStore());

        AudioDeviceSnapshot snapshot;
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.statusMessage = snapshot.runningInStandalone ? "Audio device manager unavailable" : "Not running in standalone mode";
        return snapshot;
    }

    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager,
                                                   const StandaloneSettingsStore* settingsStore)
    {
        AudioDeviceSnapshot snapshot;
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.backendName = deviceManager.getCurrentAudioDeviceType();

        if (settingsStore != nullptr)
        {
            if (auto persisted = settingsStore->loadPersistedAudioSelection())
            {
                snapshot.persistedConfigurationFound = true;
                snapshot.persistedSelection = *persisted;
            }
        }

        if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
        {
            snapshot.hasCurrentDevice = true;
            snapshot.hasActiveOutput = currentDevice->getActiveOutputChannels().countNumberOfSetBits() > 0;
            snapshot.outputDeviceName = currentDevice->getName();
            snapshot.sampleRateHz = currentDevice->getCurrentSampleRate();
            snapshot.bufferSizeSamples = currentDevice->getCurrentBufferSizeSamples();
            
            if (snapshot.persistedConfigurationFound)
            {
                snapshot.currentMatchesPersistedConfiguration = 
                    snapshot.backendName == snapshot.persistedSelection.deviceTypeName &&
                    snapshot.outputDeviceName == snapshot.persistedSelection.outputDeviceName &&
                    juce::approximatelyEqual(snapshot.sampleRateHz, snapshot.persistedSelection.sampleRateHz) &&
                    snapshot.bufferSizeSamples == snapshot.persistedSelection.bufferSizeSamples;
            }

            if (!snapshot.hasActiveOutput)
            {
                snapshot.statusMessage = "Device open with no active output channels";
            }
        }

        if (!snapshot.runningInStandalone)
        {
            snapshot.status = AudioDeviceStatus::managerUnavailable;
            snapshot.statusMessage = "Not running in standalone mode";
        }
        else if (snapshot.hasActiveOutput)
        {
            snapshot.status = (snapshot.persistedConfigurationFound && !snapshot.currentMatchesPersistedConfiguration)
                ? AudioDeviceStatus::fallbackConfigurationActive
                : AudioDeviceStatus::ready;
            
            if (snapshot.statusMessage.isEmpty())
                snapshot.statusMessage = "Ready";
        }
        else if (snapshot.persistedConfigurationFound)
        {
            snapshot.status = AudioDeviceStatus::rememberedConfigurationUnavailable;
            snapshot.statusMessage = "Remembered configuration unavailable";
        }
        else
        {
            snapshot.status = AudioDeviceStatus::noOutputDeviceAvailable;
            snapshot.statusMessage = "No output device available";
        }

        return snapshot;
    }

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           const StandaloneSettingsStore* settingsStore)
    {
        BackendSelectionResult result;
        result.initialBackendName = deviceManager.getCurrentAudioDeviceType();

        if (settingsStore != nullptr && settingsStore->hasPersistedAudioSetup())
        {
            result.persistedAudioSetupFound = true;
            result.activeBackendName = result.initialBackendName;
            return result;
        }

        const auto& availableTypes = deviceManager.getAvailableDeviceTypes();
        bool hasWasapi = false;
        bool hasDirectSound = false;

        for (auto* type : availableTypes)
        {
            if (type->getTypeName() == preferredWasapiSharedType)
                hasWasapi = true;
            else if (type->getTypeName() == fallbackDirectSoundType)
                hasDirectSound = true;
        }

        result.preferredBackendAvailable = hasWasapi;

        if (hasWasapi && result.initialBackendName != preferredWasapiSharedType)
        {
            deviceManager.setCurrentAudioDeviceType(preferredWasapiSharedType, false);
            
            if (deviceManager.getCurrentAudioDevice() != nullptr)
            {
                result.preferredBackendApplied = true;
            }
            else
            {
                // Restore if WASAPI failed to open a device
                deviceManager.setCurrentAudioDeviceType(result.initialBackendName, false);
            }
        }

        if (deviceManager.getCurrentAudioDevice() == nullptr && hasDirectSound)
        {
            deviceManager.setCurrentAudioDeviceType(fallbackDirectSoundType, false);
            result.fallbackBackendApplied = deviceManager.getCurrentAudioDevice() != nullptr;
        }

        result.activeBackendName = deviceManager.getCurrentAudioDeviceType();
        return result;
    }

    juce::String formatSampleRateHz(double sampleRateHz)
    {
        if (sampleRateHz <= 0)
            return "-";

        if (sampleRateHz >= 1000.0)
            return juce::String (sampleRateHz / 1000.0, 1) + " kHz";

        return juce::String (std::round(sampleRateHz)) + " Hz";
    }

    juce::String formatBufferSizeSamples(int bufferSizeSamples)
    {
        if (bufferSizeSamples <= 0)
            return "-";

        return juce::String (bufferSizeSamples) + " samples";
    }
}
