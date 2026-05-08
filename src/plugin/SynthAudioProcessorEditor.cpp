#include "SynthAudioProcessorEditor.h"

#include "SynthAudioProcessor.h"
#include "parameters/ParameterIDs.h"
#include "ui/MidiMonitorPanel.h"
#include "ui/StandaloneAudioStatusPanel.h"
#include "ui/StandaloneMidiInputPanel.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
{
    namespace ids = coolsynth::parameters::ids;
    auto& apvts = processor.getValueTreeState();

    parameterRefs.waveform = apvts.getParameter(ids::waveform);
    parameterRefs.ampAttackMs = apvts.getParameter(ids::ampAttackMs);
    parameterRefs.ampDecayMs = apvts.getParameter(ids::ampDecayMs);
    parameterRefs.ampSustain = apvts.getParameter(ids::ampSustain);
    parameterRefs.ampReleaseMs = apvts.getParameter(ids::ampReleaseMs);
    parameterRefs.masterGainDb = apvts.getParameter(ids::masterGainDb);

    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    // --- Oscillator Section ---
    addAndMakeVisible(oscillatorSection);
    waveformLabel.setText("Waveform", juce::dontSendNotification);
    addAndMakeVisible(waveformLabel);
    waveformSelector.addItemList({ "sine", "square", "saw" }, 1);
    addAndMakeVisible(waveformSelector);
    waveformAttachment = std::make_unique<ComboBoxAttachment>(apvts, ids::waveform, waveformSelector);

    // --- Envelope Section ---
    addAndMakeVisible(envelopeSection);
    addAndMakeVisible(attackKnob);
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(sustainKnob);
    addAndMakeVisible(releaseKnob);
    attackAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampAttackMs, attackKnob.slider());
    decayAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampDecayMs, decayKnob.slider());
    sustainAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampSustain, sustainKnob.slider());
    releaseAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampReleaseMs, releaseKnob.slider());

    // --- Output Section ---
    addAndMakeVisible(outputSection);
    addAndMakeVisible(masterGainFader);
    panicButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible(panicButton);
    masterGainAttachment = std::make_unique<SliderAttachment>(apvts, ids::masterGainDb, masterGainFader.slider());

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

        setSize(900, 850);
    }
    else
    {
        setSize(900, 400);
    }

    startTimerHz(24);
    refreshValueDisplays();
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    stopTimer();
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    titleLabel.setBounds(area.removeFromTop(48));
    area.removeFromTop(16);

    auto synthRow = area.removeFromTop(240);
    
    // Oscillator
    auto oscArea = synthRow.removeFromLeft(180);
    oscillatorSection.setBounds(oscArea);
    auto oscContent = oscillatorSection.getContentBounds();
    waveformLabel.setBounds(oscContent.removeFromTop(24));
    waveformSelector.setBounds(oscContent.removeFromTop(32));

    synthRow.removeFromLeft(16);

    // Envelope
    auto envArea = synthRow.removeFromLeft(400);
    envelopeSection.setBounds(envArea);
    auto envContent = envelopeSection.getContentBounds();
    auto envGrid = envContent.withSizeKeepingCentre(envContent.getWidth(), 120);
    attackKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 4));
    decayKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 3));
    sustainKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 2));
    releaseKnob.setBounds(envGrid);

    synthRow.removeFromLeft(16);

    // Output
    outputSection.setBounds(synthRow);
    auto outContent = outputSection.getContentBounds();
    masterGainFader.setBounds(outContent.removeFromLeft(80));
    outContent.removeFromLeft(16);
    panicButton.setBounds(outContent.withSizeKeepingCentre(outContent.getWidth(), 32));

    if (standaloneAudioPanel != nullptr)
    {
        area.removeFromTop(24);
        standaloneAudioPanel->setBounds(area.removeFromTop(160));
        
        area.removeFromTop(16);
        standaloneMidiInputPanel->setBounds(area.removeFromTop(80));
        
        area.removeFromTop(16);
        standaloneMidiMonitorPanel->setBounds(area);
    }
}

void SynthAudioProcessorEditor::timerCallback()
{
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::refreshValueDisplays()
{
    attackKnob.setValueText(getCurrentParameterText(parameterRefs.ampAttackMs));
    decayKnob.setValueText(getCurrentParameterText(parameterRefs.ampDecayMs));
    sustainKnob.setValueText(getCurrentParameterText(parameterRefs.ampSustain));
    releaseKnob.setValueText(getCurrentParameterText(parameterRefs.ampReleaseMs));
    masterGainFader.setValueText(getCurrentParameterText(parameterRefs.masterGainDb));
}

juce::String SynthAudioProcessorEditor::getCurrentParameterText(juce::RangedAudioParameter* parameter) const
{
    if (parameter == nullptr)
        return "-";
    
    return parameter->getCurrentValueAsText();
}
