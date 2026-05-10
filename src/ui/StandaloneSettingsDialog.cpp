#include "StandaloneSettingsDialog.h"

#include "midi/ControllerProfile.h"
#include "plugin/SynthAudioProcessorEditor.h"

namespace
{
    constexpr int autoDetectProfileItemId = 1;
    constexpr int noFactoryProfileItemId = 2;
    constexpr int explicitProfileItemIdBase = 100;
}

StandaloneSettingsDialog::MidiTab::MidiTab(coolsynth::standalone::StandaloneMidiInputController& midiControllerIn,
                                           SynthAudioProcessorEditor& editorIn)
    : midiController(midiControllerIn)
    , editor(editorIn)
    , midiInputPanel(midiController)
    , midiMonitorPanel(midiController.getMonitorBuffer())
{
    midiController.addChangeListener(this);

    addAndMakeVisible(midiInputPanel);
    addAndMakeVisible(showCcLabelsToggle);
    addAndMakeVisible(midiMonitorPanel);
    addAndMakeVisible(controllerProfileLabel);
    addAndMakeVisible(controllerProfileCombo);
    addAndMakeVisible(resolvedProfileLabel);
    addAndMakeVisible(resolvedProfileValueLabel);
    addAndMakeVisible(resetMidiSettingsButton);

    controllerProfileLabel.setText("Controller Profile", juce::dontSendNotification);
    resolvedProfileLabel.setText("Resolved Profile", juce::dontSendNotification);
    resolvedProfileValueLabel.setJustificationType(juce::Justification::centredLeft);

    auto* store = coolsynth::standalone::getStandaloneSettingsStore();
    if (store != nullptr)
    {
        showCcLabelsToggle.setToggleState(store->getShowCcLabels(), juce::dontSendNotification);
    }

    showCcLabelsToggle.onClick = [this] {
        auto* store = coolsynth::standalone::getStandaloneSettingsStore();
        if (store != nullptr)
        {
            store->setShowCcLabels(showCcLabelsToggle.getToggleState());
        }
    };

    resetMidiSettingsButton.onClick = [this] { handleResetMidiSettings(); };

    populateControllerProfileOptions();
    applyPersistedControllerProfileSelection();
    updateResolvedProfileLabel();
}

StandaloneSettingsDialog::MidiTab::~MidiTab()
{
    midiController.removeChangeListener(this);
}

void StandaloneSettingsDialog::MidiTab::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &midiController)
        updateResolvedProfileLabel();
}

void StandaloneSettingsDialog::MidiTab::populateControllerProfileOptions()
{
    controllerProfileCombo.clear(juce::dontSendNotification);
    explicitProfileIds.clear();

    controllerProfileCombo.addItem("Auto Detect", autoDetectProfileItemId);
    controllerProfileCombo.addItem("No Factory Profile", noFactoryProfileItemId);

    int itemId = explicitProfileItemIdBase;
    for (const auto& profile : coolsynth::midi::ControllerProfileRegistry::get().getProfiles())
    {
        controllerProfileCombo.addItem(profile.displayName, itemId++);
        explicitProfileIds.push_back(profile.profileId);
    }

    controllerProfileCombo.onChange = [this]
    {
        auto selection = coolsynth::standalone::PersistedControllerProfileSelection {};

        switch (controllerProfileCombo.getSelectedId())
        {
            case autoDetectProfileItemId:
                selection.mode = coolsynth::standalone::ControllerProfileSelectionMode::autoDetect;
                break;

            case noFactoryProfileItemId:
                selection.mode = coolsynth::standalone::ControllerProfileSelectionMode::none;
                break;

            default:
            {
                const int profileIndex = controllerProfileCombo.getSelectedId() - explicitProfileItemIdBase;
                if (juce::isPositiveAndBelow(profileIndex, static_cast<int>(explicitProfileIds.size())))
                {
                    selection.mode = coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile;
                    selection.profileId = explicitProfileIds[static_cast<size_t>(profileIndex)];
                }
                else
                {
                    selection.mode = coolsynth::standalone::ControllerProfileSelectionMode::autoDetect;
                }
                break;
            }
        }

        if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
            store->savePersistedControllerProfileSelection(selection);

        editor.refreshStandaloneControllerProfileSelection();
        updateResolvedProfileLabel();
    };
}

void StandaloneSettingsDialog::MidiTab::applyPersistedControllerProfileSelection()
{
    auto selection = coolsynth::standalone::PersistedControllerProfileSelection {};
    if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
        selection = store->loadPersistedControllerProfileSelection();

    if (selection.mode == coolsynth::standalone::ControllerProfileSelectionMode::none)
    {
        controllerProfileCombo.setSelectedId(noFactoryProfileItemId, juce::dontSendNotification);
        return;
    }

    if (selection.mode == coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile)
    {
        for (size_t i = 0; i < explicitProfileIds.size(); ++i)
        {
            if (explicitProfileIds[i] == selection.profileId)
            {
                controllerProfileCombo.setSelectedId(explicitProfileItemIdBase + static_cast<int>(i),
                                                     juce::dontSendNotification);
                return;
            }
        }
    }

    controllerProfileCombo.setSelectedId(autoDetectProfileItemId, juce::dontSendNotification);
}

void StandaloneSettingsDialog::MidiTab::updateResolvedProfileLabel()
{
    resolvedProfileValueLabel.setText(editor.getResolvedStandaloneControllerProfileDisplayName(),
                                      juce::dontSendNotification);
}

void StandaloneSettingsDialog::MidiTab::handleResetMidiSettings()
{
    editor.resetStandaloneMidiSettings();
    showCcLabelsToggle.setToggleState(true, juce::dontSendNotification);
    controllerProfileCombo.setSelectedId(autoDetectProfileItemId, juce::dontSendNotification);
    updateResolvedProfileLabel();
}
void StandaloneSettingsDialog::MidiTab::resized()
{
    auto bounds = getLocalBounds().reduced(4);

    // MIDI Input Selection (70px)
    midiInputPanel.setBounds(bounds.removeFromTop(70));
    bounds.removeFromTop(10);

    // CC Labels Toggle (24px)
    showCcLabelsToggle.setBounds(bounds.removeFromTop(24).reduced(4, 0));
    bounds.removeFromTop(12);

    // Profile Selection (Label 20px + Combo 28px)
    controllerProfileLabel.setBounds(bounds.removeFromTop(20).reduced(4, 0));
    controllerProfileCombo.setBounds(bounds.removeFromTop(28).reduced(4, 0));
    bounds.removeFromTop(8);

    // Resolved Profile Info (24px)
    auto resolvedBounds = bounds.removeFromTop(24).reduced(4, 0);
    resolvedProfileLabel.setBounds(resolvedBounds.removeFromLeft(120));
    resolvedProfileValueLabel.setBounds(resolvedBounds);

    bounds.removeFromTop(16);

    // Reset Button (32px high for better visibility)
    resetMidiSettingsButton.setBounds(bounds.removeFromTop(32).reduced(4, 0));

    bounds.removeFromTop(16);

    // MIDI Monitor takes the rest of the space
    midiMonitorPanel.setBounds(bounds);
}

StandaloneSettingsDialog::StandaloneSettingsDialog(juce::AudioDeviceManager& deviceManager,
                                                   coolsynth::standalone::StandaloneMidiInputController& midiController,
                                                   SynthAudioProcessorEditor& editor)
{
    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager,
        0, 0, // input channels
        1, 2, // output channels
        false, // showMidiInputOptions
        false, // showMidiOutputOptions
        false, // showChannelsAsStereoPairs
        false); // hideAdvancedOptions

    midiTab = std::make_unique<MidiTab>(midiController, editor);

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
                                  coolsynth::standalone::StandaloneMidiInputController& midiController,
                                  SynthAudioProcessorEditor& editor)
{
    auto* dialogComponent = new StandaloneSettingsDialog(deviceManager, midiController, editor);
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
