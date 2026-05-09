#include "StandaloneMidiInputPanel.h"
#include "standalone/StandaloneAudioSupport.h"
#include "standalone/StandaloneMidiInput.h"

StandaloneMidiInputPanel::StandaloneMidiInputPanel(ControllerEventHandler onControllerEvent,
                                                   std::function<void()> onDisconnected)
{
    deviceTitleLabel.setText("MIDI Input:", juce::dontSendNotification);
    addAndMakeVisible(deviceTitleLabel);

    deviceSelector.addItem("None", 1);
    deviceSelector.onChange = [this] { handleDeviceSelectionChanged(); };
    addAndMakeVisible(deviceSelector);

    statusTitleLabel.setText("Status:", juce::dontSendNotification);
    addAndMakeVisible(statusTitleLabel);

    statusValueLabel.setText("-", juce::dontSendNotification);
    addAndMakeVisible(statusValueLabel);

    if (auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager())
    {
        auto* settings = coolsynth::standalone::getStandaloneSettings();
        controller = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
            *deviceManager, 
            settings, 
            monitorBuffer, 
            std::move(onControllerEvent),
            std::move(onDisconnected));
        controller->addChangeListener(this);
        refreshFromController();
    }
}

StandaloneMidiInputPanel::~StandaloneMidiInputPanel()
{
    if (controller != nullptr)
        controller->removeChangeListener(this);
}

void StandaloneMidiInputPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    auto row = bounds.removeFromTop(24);

    deviceTitleLabel.setBounds(row.removeFromLeft(80));
    deviceSelector.setBounds(row.removeFromLeft(250));

    bounds.removeFromTop(10);
    row = bounds.removeFromTop(24);
    statusTitleLabel.setBounds(row.removeFromLeft(80));
    statusValueLabel.setBounds(row.removeFromLeft(300));
}

void StandaloneMidiInputPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::grey.withAlpha(0.2f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 5.0f, 1.0f);
}

void StandaloneMidiInputPanel::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    refreshFromController();
}

void StandaloneMidiInputPanel::refreshFromController()
{
    if (controller == nullptr)
        return;

    const auto& snapshot = controller->getSnapshot();

    repopulateDeviceSelector();

    statusValueLabel.setText(snapshot.statusMessage, juce::dontSendNotification);
    
    // Optional: use color to emphasize status
    using Status = coolsynth::standalone::MidiInputStatus;
    if (snapshot.status == Status::connected)
        statusValueLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    else if (snapshot.status == Status::disconnected || snapshot.status == Status::rememberedDeviceUnavailable)
        statusValueLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    else
        statusValueLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
}

void StandaloneMidiInputPanel::repopulateDeviceSelector()
{
    if (controller == nullptr)
        return;

    const auto& snapshot = controller->getSnapshot();

    isRefreshingSelector = true;
    deviceSelector.clear(juce::dontSendNotification);
    deviceSelector.addItem("None", 1);

    int selectedId = 1;
    for (int i = 0; i < snapshot.availableInputs.size(); ++i)
    {
        const auto& info = snapshot.availableInputs[i];
        int id = i + 2;
        deviceSelector.addItem(info.name, id);

        if (info.identifier == snapshot.selectedDeviceIdentifier)
            selectedId = id;
    }

    deviceSelector.setSelectedId(selectedId, juce::dontSendNotification);
    isRefreshingSelector = false;
}

void StandaloneMidiInputPanel::handleDeviceSelectionChanged()
{
    if (isRefreshingSelector || controller == nullptr)
        return;

    int selectedId = deviceSelector.getSelectedId();
    if (selectedId <= 1)
    {
        controller->clearSelection();
    }
    else
    {
        const auto& snapshot = controller->getSnapshot();
        int index = selectedId - 2;
        if (index >= 0 && index < snapshot.availableInputs.size())
        {
            controller->selectDeviceByIdentifier(snapshot.availableInputs[index].identifier);
        }
    }
}
