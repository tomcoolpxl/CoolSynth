#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "standalone/StandaloneMidiInput.h"
#include "ui/StandaloneMidiInputPanel.h"
#include "ui/MidiMonitorPanel.h"

class StandaloneSettingsDialog final : public juce::Component
{
public:
    StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                             coolsynth::standalone::StandaloneMidiInputController& midiController);

    void resized() override;

private:
    class MidiTab final : public juce::Component
    {
    public:
        explicit MidiTab(coolsynth::standalone::StandaloneMidiInputController& midiController);
        void resized() override;

    private:
        StandaloneMidiInputPanel midiInputPanel;
        MidiMonitorPanel midiMonitorPanel;
    };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
    std::unique_ptr<MidiTab> midiTab;
};

bool showStandaloneSettingsDialog(juce::Component* parentComponent,
                                  juce::AudioDeviceManager& deviceManager,
                                  coolsynth::standalone::StandaloneMidiInputController& midiController);
