#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "standalone/StandaloneMidiInput.h"

class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    explicit StandaloneMidiInputPanel(coolsynth::standalone::StandaloneMidiInputController& controller);
    ~StandaloneMidiInputPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshFromController();
    void repopulateDeviceSelector();
    void handleDeviceSelectionChanged();

    coolsynth::standalone::StandaloneMidiInputController& controller;
    bool isRefreshingSelector = false;

    juce::Label deviceTitleLabel;
    juce::ComboBox deviceSelector;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputPanel)
};
