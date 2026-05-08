#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/HardwareFader.h"
#include "ui/HardwareKnob.h"
#include "ui/SynthSection.h"

class SynthAudioProcessor;

class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit SynthAudioProcessorEditor(SynthAudioProcessor& processor);
    ~SynthAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct ParameterRefs
    {
        juce::RangedAudioParameter* waveform = nullptr;
        juce::RangedAudioParameter* ampAttackMs = nullptr;
        juce::RangedAudioParameter* ampDecayMs = nullptr;
        juce::RangedAudioParameter* ampSustain = nullptr;
        juce::RangedAudioParameter* ampReleaseMs = nullptr;
        juce::RangedAudioParameter* masterGainDb = nullptr;
    };

    void timerCallback() override;
    void refreshValueDisplays();
    juce::String getCurrentParameterText(juce::RangedAudioParameter* parameter) const;

    SynthAudioProcessor& processor;
    ParameterRefs parameterRefs;

    juce::Label titleLabel;

    coolsynth::ui::SynthSection oscillatorSection { "Oscillator" };
    coolsynth::ui::SynthSection envelopeSection { "Envelope" };
    coolsynth::ui::SynthSection outputSection { "Output" };

    juce::Label waveformLabel;
    juce::ComboBox waveformSelector;
    coolsynth::ui::HardwareKnob attackKnob { "Attack" };
    coolsynth::ui::HardwareKnob decayKnob { "Decay" };
    coolsynth::ui::HardwareKnob sustainKnob { "Sustain" };
    coolsynth::ui::HardwareKnob releaseKnob { "Release" };
    coolsynth::ui::HardwareFader masterGainFader { "Master" };
    juce::TextButton panicButton { "Panic" };

    std::unique_ptr<ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> masterGainAttachment;

    std::unique_ptr<juce::Component> standaloneAudioPanel;
    std::unique_ptr<juce::Component> standaloneMidiInputPanel;
    std::unique_ptr<juce::Component> standaloneMidiMonitorPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessorEditor)
};
