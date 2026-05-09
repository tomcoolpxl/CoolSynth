#include "StandaloneSettingsDialog.h"

StandaloneSettingsDialog::MidiTab::MidiTab(coolsynth::standalone::StandaloneMidiInputController& midiController)
    : midiInputPanel(midiController)
    , midiMonitorPanel(midiController.getMonitorBuffer())
{
    addAndMakeVisible(midiInputPanel);
    addAndMakeVisible(midiMonitorPanel);
}

void StandaloneSettingsDialog::MidiTab::resized()
{
    auto bounds = getLocalBounds();
    midiInputPanel.setBounds(bounds.removeFromTop(70));
    bounds.removeFromTop(10); // spacing
    midiMonitorPanel.setBounds(bounds);
}

StandaloneSettingsDialog::StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                                                   coolsynth::standalone::StandaloneMidiInputController& midiController)
{
    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager,
        0, 0, // input channels
        1, 2, // output channels
        false, // showMidiInputOptions
        false, // showMidiOutputOptions
        false, // showChannelsAsStereoPairs
        false); // hideAdvancedOptions

    midiTab = std::make_unique<MidiTab>(midiController);

    tabs.addTab("Audio", juce::Colours::transparentBlack, audioSelector.get(), false);
    tabs.addTab("MIDI", juce::Colours::transparentBlack, midiTab.get(), false);
    
    addAndMakeVisible(tabs);
}

void StandaloneSettingsDialog::resized()
{
    tabs.setBounds(getLocalBounds());
}

bool showStandaloneSettingsDialog(juce::Component* parentComponent,
                                  juce::AudioDeviceManager& deviceManager,
                                  coolsynth::standalone::StandaloneMidiInputController& midiController)
{
    auto* dialogComponent = new StandaloneSettingsDialog(deviceManager, midiController);
    dialogComponent->setSize(500, 450);

    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned(dialogComponent);
    dialog.dialogTitle = "Settings";
    dialog.componentToCentreAround = parentComponent;
    dialog.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    dialog.escapeKeyTriggersCloseButton = true;
    dialog.useNativeTitleBar = true;
    dialog.resizable = false;

    dialog.launchAsync();

    return true;
}
