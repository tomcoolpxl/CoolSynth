#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "standalone/StandaloneMidiInput.h"
#include "ui/StandaloneMidiInputPanel.h"
#include "ui/MidiMonitorPanel.h"

class SynthAudioProcessorEditor;

class StandaloneSettingsDialog final : public juce::Component
{
public:
    StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                             coolsynth::standalone::StandaloneMidiInputController& midiController,
                             SynthAudioProcessorEditor& editor);

    void resized() override;

private:
    class MidiTab final : public juce::Component,
                          private juce::ChangeListener
    {
    public:
        MidiTab(coolsynth::standalone::StandaloneMidiInputController& midiController,
                SynthAudioProcessorEditor& editor);
        ~MidiTab() override;
        void resized() override;

    private:
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
        void handleResetMidiSettings();
        void applyPersistedControllerProfileSelection();
        void populateControllerProfileOptions();
        void updateResolvedProfileLabel();

        coolsynth::standalone::StandaloneMidiInputController& midiController;
        SynthAudioProcessorEditor& editor;
        StandaloneMidiInputPanel midiInputPanel;
        MidiMonitorPanel midiMonitorPanel;
        juce::ToggleButton showCcLabelsToggle { "Show CC Labels on Controls" };
        juce::Label controllerProfileLabel;
        juce::ComboBox controllerProfileCombo;
        juce::Label resolvedProfileLabel;
        juce::Label resolvedProfileValueLabel;
        juce::TextButton resetMidiSettingsButton { "Reset MIDI Settings" };
        std::vector<juce::String> explicitProfileIds;
    };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
    std::unique_ptr<MidiTab> midiTab;
};

bool showStandaloneSettingsDialog(juce::Component* parentComponent,
                                  juce::AudioDeviceManager& deviceManager,
                                  coolsynth::standalone::StandaloneMidiInputController& midiController,
                                  SynthAudioProcessorEditor& editor);
