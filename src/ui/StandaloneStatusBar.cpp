#include "StandaloneStatusBar.h"

#include "ui/UiPalette.h"

StandaloneStatusBar::StandaloneStatusBar(coolsynth::standalone::StandaloneMidiInputController& controller,
                                         std::function<juce::String()> profileTextProvider)
    : midiController(controller),
      profileProvider(std::move(profileTextProvider))
{
    addAndMakeVisible(audioStatusLabel);
    audioStatusLabel.setFont(juce::FontOptions(12.0f));
    audioStatusLabel.setJustificationType(juce::Justification::centredLeft);
    audioStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);

    addAndMakeVisible(midiStatusLabel);
    midiStatusLabel.setFont(juce::FontOptions(12.0f));
    midiStatusLabel.setJustificationType(juce::Justification::centred);
    midiStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);

    addAndMakeVisible(lastMidiStatusLabel);
    lastMidiStatusLabel.setFont(juce::FontOptions(12.0f));
    lastMidiStatusLabel.setJustificationType(juce::Justification::centred);
    lastMidiStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);

    addAndMakeVisible(buildStatusLabel);
    buildStatusLabel.setFont(juce::FontOptions(12.0f));
    buildStatusLabel.setJustificationType(juce::Justification::centredRight);
    buildStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);
    buildStatusLabel.setText(coolsynth::build::getBuildIdentity(), juce::dontSendNotification);

    deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager();
    attachToDeviceManager();

    midiController.addChangeListener(this);

    refreshAudioSnapshot();
    refreshMidiSnapshot();
    lastMidiSnapshot = midiController.getLastMidiEventSnapshot();
    refreshLastMidiSnapshot();
    refreshLabels();

    startTimerHz(20);
}

StandaloneStatusBar::~StandaloneStatusBar()
{
    stopTimer();
    detachFromDeviceManager();
    midiController.removeChangeListener(this);
}

void StandaloneStatusBar::resized()
{
    auto bounds = getLocalBounds().reduced(8, 0);
    const int portion = bounds.getWidth() / 4;

    audioStatusLabel.setBounds(bounds.removeFromLeft(portion));
    buildStatusLabel.setBounds(bounds.removeFromRight(portion));
    lastMidiStatusLabel.setBounds(bounds.removeFromRight(portion));
    midiStatusLabel.setBounds(bounds.removeFromRight(portion));
}

void StandaloneStatusBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.2f));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawLine(0.0f, 0.0f, static_cast<float>(getWidth()), 0.0f, 1.0f);
}

void StandaloneStatusBar::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == deviceManager)
    {
        refreshAudioSnapshot();
        refreshLabels();
    }
    else if (source == &midiController)
    {
        refreshMidiSnapshot();
        refreshLabels();
    }
}

void StandaloneStatusBar::timerCallback()
{
    auto snap = midiController.getLastMidiEventSnapshot();
    if (snap.eventOrder != lastMidiSnapshot.eventOrder)
    {
        lastMidiSnapshot = snap;
        refreshLastMidiSnapshot();
    }
}

void StandaloneStatusBar::attachToDeviceManager()
{
    if (deviceManager != nullptr)
        deviceManager->addChangeListener(this);
}

void StandaloneStatusBar::detachFromDeviceManager()
{
    if (deviceManager != nullptr)
        deviceManager->removeChangeListener(this);
}

void StandaloneStatusBar::refreshAudioSnapshot()
{
    if (deviceManager != nullptr)
        audioSnapshot = coolsynth::standalone::captureAudioDeviceSnapshot(*deviceManager);
    else
        audioSnapshot = coolsynth::standalone::captureCurrentAudioDeviceSnapshot();
}

void StandaloneStatusBar::refreshMidiSnapshot()
{
    midiSnapshot = midiController.getSnapshot();
}

void StandaloneStatusBar::refreshLastMidiSnapshot()
{
    lastMidiStatusLabel.setText(formatLastMidiSummary(lastMidiSnapshot), juce::dontSendNotification);
}

void StandaloneStatusBar::refreshLabels()
{
    audioStatusLabel.setText(formatAudioSummary(audioSnapshot), juce::dontSendNotification);
    auto midiSummary = formatMidiSummary(midiSnapshot);
    if (profileProvider != nullptr)
    {
        const auto profileText = profileProvider();
        if (profileText.isNotEmpty())
            midiSummary << " | " << profileText;
    }

    midiStatusLabel.setText(midiSummary, juce::dontSendNotification);
}

juce::String StandaloneStatusBar::formatAudioSummary(const coolsynth::standalone::AudioDeviceSnapshot& snapshot) const
{
    using Status = coolsynth::standalone::AudioDeviceStatus;

    if (snapshot.status == Status::managerUnavailable)
        return "Audio: Unavailable";
    if (snapshot.status == Status::rememberedConfigurationUnavailable)
        return "Audio: Remembered config unavailable";
    if (snapshot.status == Status::noOutputDeviceAvailable)
        return "Audio: No output";

    juce::String text = "Audio: ";
    if (snapshot.status == Status::fallbackConfigurationActive)
        text << "(Fallback) ";

    text << coolsynth::standalone::formatSampleRateHz(snapshot.sampleRateHz);
    text << ", " << coolsynth::standalone::formatBufferSizeSamples(snapshot.bufferSizeSamples);
    return text;
}

juce::String StandaloneStatusBar::formatMidiSummary(const coolsynth::standalone::MidiInputSnapshot& snapshot) const
{
    using Status = coolsynth::standalone::MidiInputStatus;
    
    if (snapshot.status == Status::connected)
        return "MIDI: " + snapshot.selectedDeviceName;
    if (snapshot.status == Status::rememberedDeviceUnavailable)
        return "MIDI: Unavailable (" + snapshot.selectedDeviceName + ")";
    if (snapshot.status == Status::disconnected)
        return "MIDI: Disconnected (" + snapshot.selectedDeviceName + ")";
        
    return "MIDI: -";
}

juce::String StandaloneStatusBar::formatLastMidiSummary(const coolsynth::standalone::LastMidiEventSnapshot& snapshot) const
{
    if (!snapshot.hasEvent)
        return "Last MIDI: -";

    juce::String text = "Last MIDI: ";
    using namespace coolsynth::midi;

    if (snapshot.type == MidiMonitorMessageType::noteOn)
        text << "Note On " << snapshot.primaryValue << " vel " << snapshot.secondaryValue;
    else if (snapshot.type == MidiMonitorMessageType::noteOff)
        text << "Note Off " << snapshot.primaryValue << " vel " << snapshot.secondaryValue;
    else if (snapshot.type == MidiMonitorMessageType::controlChange)
        text << "CC " << snapshot.primaryValue << " val " << snapshot.secondaryValue;
    else
        text << "Unknown";

    return text;
}
