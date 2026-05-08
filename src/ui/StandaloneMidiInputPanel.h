#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "midi/MidiMonitor.h"

namespace coolsynth::standalone
{
    class StandaloneMidiInputController;
}

class StandaloneMidiInputPanel final : public juce::Component,
                                       private juce::ChangeListener
{
public:
    explicit StandaloneMidiInputPanel(std::function<void()> onSelectedDeviceDisconnected = {});
    ~StandaloneMidiInputPanel() override;

    coolsynth::midi::MidiMonitorBuffer& getMonitorBuffer() noexcept { return monitorBuffer; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshFromController();
    void repopulateDeviceSelector();
    void handleDeviceSelectionChanged();

    bool isRefreshingSelector = false;
    coolsynth::midi::MidiMonitorBuffer monitorBuffer;
    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> controller;

    juce::Label deviceTitleLabel;
    juce::ComboBox deviceSelector;
    juce::Label statusTitleLabel;
    juce::Label statusValueLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneMidiInputPanel)
};
