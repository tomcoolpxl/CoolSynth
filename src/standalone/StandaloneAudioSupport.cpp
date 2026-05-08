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
            return captureAudioDeviceSnapshot(*deviceManager);

        AudioDeviceSnapshot snapshot;
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.statusMessage = snapshot.runningInStandalone ? "Audio device manager unavailable" : "Not running in standalone mode";
        return snapshot;
    }

    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager)
    {
        AudioDeviceSnapshot snapshot;
        snapshot.runningInStandalone = juce::JUCEApplicationBase::isStandaloneApp();
        snapshot.backendName = deviceManager.getCurrentAudioDeviceType();

        if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
        {
            snapshot.hasCurrentDevice = true;
            snapshot.hasActiveOutput = currentDevice->getActiveOutputChannels().countNumberOfSetBits() > 0;
            snapshot.outputDeviceName = currentDevice->getName();
            snapshot.sampleRateHz = currentDevice->getCurrentSampleRate();
            snapshot.bufferSizeSamples = currentDevice->getCurrentBufferSizeSamples();
            snapshot.statusMessage = snapshot.hasActiveOutput ? "Ready" : "Device open with no active output channels";
        }
        else
        {
            snapshot.statusMessage = "No output device available";
        }

        return snapshot;
    }

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           juce::PropertySet* settings)
    {
        BackendSelectionResult result;
        result.initialBackendName = deviceManager.getCurrentAudioDeviceType();

        if (settings != nullptr && settings->containsKey(audioSetupPropertyKey))
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

    bool showStandaloneAudioSettingsDialog()
    {
       #if JucePlugin_Build_Standalone
        if (auto* holder = getStandalonePluginHolder())
        {
            juce::AudioDeviceSelectorComponent selector (holder->deviceManager,
                                                         0, 0, // input channels
                                                         1, 2, // output channels
                                                         false, // showMidiInputOptions
                                                         false, // showMidiOutputOptions
                                                         false, // showChannelsAsStereoPairs
                                                         false); // hideAdvancedOptions

            selector.setSize (500, 450);

            juce::DialogWindow::LaunchOptions dialog;
            dialog.content.setNonOwned (&selector);
            dialog.dialogTitle = "Audio Settings";
            dialog.componentToCentreAround = nullptr;
            dialog.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
            dialog.escapeKeyTriggersCloseButton = true;
            dialog.useNativeTitleBar = true;
            dialog.resizable = false;

            dialog.launchAsync();
            
            return true;
        }
       #endif

        return false;
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
