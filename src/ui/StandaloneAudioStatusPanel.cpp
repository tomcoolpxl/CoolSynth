#include "StandaloneAudioStatusPanel.h"

StandaloneAudioStatusPanel::StandaloneAudioStatusPanel()
{
    auto setupLabel = [this](juce::Label& label, const juce::String& text, juce::Justification justification)
    {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(justification);
        addAndMakeVisible(label);
    };

    setupLabel(backendTitleLabel, "Backend:", juce::Justification::centredRight);
    setupLabel(backendValueLabel, "-", juce::Justification::centredLeft);
    setupLabel(outputTitleLabel, "Device:", juce::Justification::centredRight);
    setupLabel(outputValueLabel, "-", juce::Justification::centredLeft);
    setupLabel(sampleRateTitleLabel, "Sample Rate:", juce::Justification::centredRight);
    setupLabel(sampleRateValueLabel, "-", juce::Justification::centredLeft);
    setupLabel(bufferSizeTitleLabel, "Buffer Size:", juce::Justification::centredRight);
    setupLabel(bufferSizeValueLabel, "-", juce::Justification::centredLeft);
    setupLabel(statusTitleLabel, "Status:", juce::Justification::centredRight);
    setupLabel(statusValueLabel, "-", juce::Justification::centredLeft);

    audioSettingsButton.onClick = [this] { handleOpenSettings(); };
    addAndMakeVisible(audioSettingsButton);

    attachToDeviceManager();
    refreshSnapshot();
    refreshLabels();
}

StandaloneAudioStatusPanel::~StandaloneAudioStatusPanel()
{
    detachFromDeviceManager();
}

void StandaloneAudioStatusPanel::resized()
{
    auto area = getLocalBounds();
    
    auto topRow = area.removeFromTop(100);
    auto leftCol = topRow.removeFromLeft(topRow.getWidth() / 2);
    auto rightCol = topRow;

    auto layoutRow = [](juce::Rectangle<int>& col, juce::Label& title, juce::Label& value)
    {
        auto row = col.removeFromTop(20);
        title.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(4, 0));
        value.setBounds(row.reduced(4, 0));
    };

    layoutRow(leftCol, backendTitleLabel, backendValueLabel);
    layoutRow(leftCol, outputTitleLabel, outputValueLabel);
    layoutRow(leftCol, sampleRateTitleLabel, sampleRateValueLabel);

    layoutRow(rightCol, bufferSizeTitleLabel, bufferSizeValueLabel);
    layoutRow(rightCol, statusTitleLabel, statusValueLabel);

    area.removeFromTop(10);
    audioSettingsButton.setBounds(area.removeFromTop(32).withSizeKeepingCentre(160, 32));
}

void StandaloneAudioStatusPanel::paint(juce::Graphics& g)
{
    g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).brighter(0.05f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    
    g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).brighter(0.2f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 8.0f, 1.0f);
}

void StandaloneAudioStatusPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == deviceManager)
    {
        refreshSnapshot();
        refreshLabels();
    }
}

void StandaloneAudioStatusPanel::attachToDeviceManager()
{
    deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager();
    if (deviceManager != nullptr)
        deviceManager->addChangeListener(this);
}

void StandaloneAudioStatusPanel::detachFromDeviceManager()
{
    if (deviceManager != nullptr)
        deviceManager->removeChangeListener(this);
    deviceManager = nullptr;
}

void StandaloneAudioStatusPanel::refreshSnapshot()
{
    snapshot = coolsynth::standalone::captureCurrentAudioDeviceSnapshot();
}

void StandaloneAudioStatusPanel::refreshLabels()
{
    backendValueLabel.setText(snapshot.backendName.isEmpty() ? "-" : snapshot.backendName, juce::dontSendNotification);
    outputValueLabel.setText(snapshot.outputDeviceName.isEmpty() ? "-" : snapshot.outputDeviceName, juce::dontSendNotification);
    sampleRateValueLabel.setText(coolsynth::standalone::formatSampleRateHz(snapshot.sampleRateHz), juce::dontSendNotification);
    bufferSizeValueLabel.setText(coolsynth::standalone::formatBufferSizeSamples(snapshot.bufferSizeSamples), juce::dontSendNotification);
    statusValueLabel.setText(snapshot.statusMessage, juce::dontSendNotification);

    statusValueLabel.setColour(juce::Label::textColourId, 
                               snapshot.hasActiveOutput ? juce::Colours::lightgreen : juce::Colours::orange);
}

void StandaloneAudioStatusPanel::handleOpenSettings()
{
    coolsynth::standalone::showStandaloneAudioSettingsDialog();
    refreshSnapshot();
    refreshLabels();
}
