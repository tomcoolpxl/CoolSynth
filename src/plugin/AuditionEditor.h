#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

class SynthAudioProcessor;

/*
    Throwaway audition editor (IMPLEMENTATION_PLAN_V3.md phase 1.6): a factory preset menu,
    grouped by room, a row of buttons that give each generator slot one of the user's own files
    (decision D137), and JUCE's generic editor under them. The real UI is designed in phase 9.
*/
class AuditionEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit AuditionEditor(SynthAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override; // the file buttons show what each slot holds and whether it is in
    void showFileMenu(int part, int slot);
    void chooseFile(int part, int slot);

    SynthAudioProcessor& synth;
    juce::ComboBox presetMenu;
    juce::Label filesLabel;
    std::array<juce::TextButton, 6> fileButtons; // part A's three generator slots, then part B's
    std::unique_ptr<juce::FileChooser> chooser;
    juce::GenericAudioProcessorEditor sliders;
};
