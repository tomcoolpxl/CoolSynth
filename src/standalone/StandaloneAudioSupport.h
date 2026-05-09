#pragma once

#include "SettingsStore.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

namespace coolsynth::standalone
{
    inline constexpr char preferredWasapiSharedType[] = "Windows Audio";
    inline constexpr char fallbackDirectSoundType[] = "DirectSound";
    inline constexpr char audioSetupPropertyKey[] = "audioSetup";

    enum class AudioDeviceStatus
    {
        managerUnavailable,
        noOutputDeviceAvailable,
        ready,
        fallbackConfigurationActive,
        rememberedConfigurationUnavailable,
    };

    struct AudioDeviceSnapshot
    {
        bool runningInStandalone = false;
        bool hasCurrentDevice = false;
        bool hasActiveOutput = false;
        bool persistedConfigurationFound = false;
        bool currentMatchesPersistedConfiguration = false;
        AudioDeviceStatus status = AudioDeviceStatus::managerUnavailable;

        juce::String backendName;
        juce::String outputDeviceName;
        double sampleRateHz = 0.0;
        int bufferSizeSamples = 0;

        PersistedAudioSelection persistedSelection;
        juce::String statusMessage;
    };

    struct BackendSelectionResult
    {
        bool persistedAudioSetupFound = false;
        bool preferredBackendAvailable = false;
        bool preferredBackendApplied = false;
        bool fallbackBackendApplied = false;
        juce::String initialBackendName;
        juce::String activeBackendName;
    };

    bool isStandaloneRuntimeAvailable() noexcept;
    juce::AudioDeviceManager* getStandaloneAudioDeviceManager() noexcept;
    juce::PropertySet* getStandaloneSettings() noexcept;

    AudioDeviceSnapshot captureCurrentAudioDeviceSnapshot();
    AudioDeviceSnapshot captureAudioDeviceSnapshot(const juce::AudioDeviceManager& deviceManager,
                                                   const StandaloneSettingsStore* settingsStore = nullptr);

    BackendSelectionResult maybeApplyPreferredAudioBackend(juce::AudioDeviceManager& deviceManager,
                                                           const StandaloneSettingsStore* settingsStore);

    juce::String formatSampleRateHz(double sampleRateHz);
    juce::String formatBufferSizeSamples(int bufferSizeSamples);
}
