#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SynthAudioProcessor;

/*
    Throwaway audition editor (IMPLEMENTATION_PLAN_V3.md phase 1.6): a factory preset menu,
    grouped by room, above JUCE's generic editor. The real UI is designed in phase 9.
*/
class AuditionEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AuditionEditor(SynthAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SynthAudioProcessor& synth;
    juce::ComboBox presetMenu;
    juce::GenericAudioProcessorEditor sliders;
};
