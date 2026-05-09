#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/HardwareFader.h"
#include "ui/HardwareKnob.h"
#include "ui/SynthSection.h"

class SynthAudioProcessor;

namespace coolsynth::standalone
{
    class StandaloneMidiInputController;
}

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
        juce::RangedAudioParameter* filterCutoffHz = nullptr;
        juce::RangedAudioParameter* filterResonance = nullptr;
        juce::RangedAudioParameter* ampAttackMs = nullptr;
        juce::RangedAudioParameter* ampDecayMs = nullptr;
        juce::RangedAudioParameter* ampSustain = nullptr;
        juce::RangedAudioParameter* ampReleaseMs = nullptr;
        juce::RangedAudioParameter* delayTimeMs = nullptr;
        juce::RangedAudioParameter* delayFeedback = nullptr;
        juce::RangedAudioParameter* delayMix = nullptr;
        juce::RangedAudioParameter* masterGainDb = nullptr;
    };

    void timerCallback() override;
    void refreshValueDisplays();
    juce::String getCurrentParameterText(juce::RangedAudioParameter* parameter) const;

    SynthAudioProcessor& processor;
    ParameterRefs parameterRefs;

    juce::Label titleLabel;

    coolsynth::ui::SynthSection oscillatorSection { "Oscillator" };
    coolsynth::ui::SynthSection filterSection { "Filter" };
    coolsynth::ui::SynthSection envelopeSection { "Envelope" };
    coolsynth::ui::SynthSection delaySection { "Delay" };
    coolsynth::ui::SynthSection outputSection { "Output" };

    juce::Label waveformLabel;
    juce::ComboBox waveformSelector;
    coolsynth::ui::HardwareKnob cutoffKnob { "Cutoff" };
    coolsynth::ui::HardwareKnob resonanceKnob { "Resonance" };
    coolsynth::ui::HardwareKnob attackKnob { "Attack" };
    coolsynth::ui::HardwareKnob decayKnob { "Decay" };
    coolsynth::ui::HardwareKnob sustainKnob { "Sustain" };
    coolsynth::ui::HardwareKnob releaseKnob { "Release" };
    coolsynth::ui::HardwareKnob delayTimeKnob { "Time" };
    coolsynth::ui::HardwareKnob delayFeedbackKnob { "Feedback" };
    coolsynth::ui::HardwareKnob delayMixKnob { "Mix" };
    coolsynth::ui::HardwareFader masterGainFader { "Master" };
    juce::TextButton allNotesOffButton { "All Notes Off" };

    std::unique_ptr<ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;
    std::unique_ptr<SliderAttachment> resonanceAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> delayTimeAttachment;
    std::unique_ptr<SliderAttachment> delayFeedbackAttachment;
    std::unique_ptr<SliderAttachment> delayMixAttachment;
    std::unique_ptr<SliderAttachment> masterGainAttachment;

    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> standaloneMidiController;
    std::unique_ptr<juce::Component> standaloneStatusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessorEditor)
};
