#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "standalone/StandaloneAudioSupport.h"
#include "standalone/StandaloneMidiInput.h"

class StandaloneStatusBar final : public juce::Component,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    explicit StandaloneStatusBar(coolsynth::standalone::StandaloneMidiInputController& midiController);
    ~StandaloneStatusBar() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void attachToDeviceManager();
    void detachFromDeviceManager();
    void refreshAudioSnapshot();
    void refreshMidiSnapshot();
    void refreshLastMidiSnapshot();
    void refreshLabels();

    juce::String formatAudioSummary(const coolsynth::standalone::AudioDeviceSnapshot& snapshot) const;
    juce::String formatMidiSummary(const coolsynth::standalone::MidiInputSnapshot& snapshot) const;
    juce::String formatLastMidiSummary(const coolsynth::standalone::LastMidiEventSnapshot& snapshot) const;

    juce::AudioDeviceManager* deviceManager = nullptr;
    coolsynth::standalone::StandaloneMidiInputController& midiController;
    coolsynth::standalone::AudioDeviceSnapshot audioSnapshot;
    coolsynth::standalone::MidiInputSnapshot midiSnapshot;
    coolsynth::standalone::LastMidiEventSnapshot lastMidiSnapshot;

    juce::Label audioStatusLabel;
    juce::Label midiStatusLabel;
    juce::Label lastMidiStatusLabel;
};
