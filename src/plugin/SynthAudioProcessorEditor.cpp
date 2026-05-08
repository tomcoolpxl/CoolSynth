#include "SynthAudioProcessorEditor.h"

#include "SynthAudioProcessor.h"
#include "ui/MidiMonitorPanel.h"
#include "ui/StandaloneAudioStatusPanel.h"
#include "ui/StandaloneMidiInputPanel.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
{
    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Phase 4 playable sine synth voice path", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    panicButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible(panicButton);

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        standaloneAudioPanel = std::make_unique<StandaloneAudioStatusPanel>();
        addAndMakeVisible(*standaloneAudioPanel);

        auto midiInputPanel = std::make_unique<StandaloneMidiInputPanel>([this] { processor.requestPanic(); });
        auto* monitorBuffer = &midiInputPanel->getMonitorBuffer();
        standaloneMidiInputPanel = std::move(midiInputPanel);
        addAndMakeVisible(*standaloneMidiInputPanel);

        standaloneMidiMonitorPanel = std::make_unique<MidiMonitorPanel>(*monitorBuffer);
        addAndMakeVisible(*standaloneMidiMonitorPanel);
    }

    setSize(900, 700);
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.drawRect(getLocalBounds(), 1);
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(48));
    statusLabel.setBounds(area.removeFromTop(32));

    area.removeFromTop(8);
    auto panicArea = area.removeFromTop(32);
    panicButton.setBounds(panicArea.withSizeKeepingCentre(100, 32));

    if (standaloneAudioPanel != nullptr)
    {
        area.removeFromTop(16);
        standaloneAudioPanel->setBounds(area.removeFromTop(160));
    }

    if (standaloneMidiInputPanel != nullptr)
    {
        area.removeFromTop(16);
        standaloneMidiInputPanel->setBounds(area.removeFromTop(80));
    }

    if (standaloneMidiMonitorPanel != nullptr)
    {
        area.removeFromTop(16);
        standaloneMidiMonitorPanel->setBounds(area);
    }
}
