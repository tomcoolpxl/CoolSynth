#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "standalone/StandaloneAudioSupport.h"

class StandaloneAudioStatusPanel final : public juce::Component,
                                         private juce::ChangeListener
{
public:
    StandaloneAudioStatusPanel();
    ~StandaloneAudioStatusPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshSnapshot();
    void refreshLabels();
    void handleOpenSettings();

    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::AudioDeviceSnapshot snapshot;

    juce::Label backendTitleLabel;
    juce::Label backendValueLabel;
    juce::Label outputTitleLabel;
    juce::Label outputValueLabel;
    juce::Label sampleRateTitleLabel;
    juce::Label sampleRateValueLabel;
    juce::Label bufferSizeTitleLabel;
    juce::Label bufferSizeValueLabel;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;
    juce::TextButton audioSettingsButton { "Audio Settings..." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneAudioStatusPanel)
};
