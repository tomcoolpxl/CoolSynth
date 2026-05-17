#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/HardwareKnob.h"
#include "ui/SegmentedChoiceGroup.h"
#include "ui/SynthSection.h"

class ArpAdvancedOverlay final : public juce::Component
{
public:
    explicit ArpAdvancedOverlay(juce::AudioProcessorValueTreeState& valueTreeState,
                                bool isStandaloneApp);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void visibilityChanged() override;

    void refreshFromParameters();
    void setCloseCallback(std::function<void()> callback);

    coolsynth::ui::HardwareKnob& getTempoKnob() noexcept { return tempoKnob; }
    coolsynth::ui::SegmentedChoiceGroup& getRhythmChoice() noexcept { return rhythmChoice; }
    coolsynth::ui::HardwareKnob& getChanceKnob() noexcept { return chanceKnob; }
    coolsynth::ui::SegmentedChoiceGroup& getRatchetChoice() noexcept { return ratchetChoice; }
    coolsynth::ui::HardwareKnob& getRatchetChanceKnob() noexcept { return ratchetChanceKnob; }
    coolsynth::ui::SegmentedChoiceGroup& getAccentChoice() noexcept { return accentChoice; }
    coolsynth::ui::HardwareKnob& getAccentAmountKnob() noexcept { return accentAmountKnob; }
    coolsynth::ui::HardwareKnob& getEuclideanPulsesKnob() noexcept { return euclideanPulsesKnob; }
    coolsynth::ui::HardwareKnob& getEuclideanStepsKnob() noexcept { return euclideanStepsKnob; }
    coolsynth::ui::HardwareKnob& getEuclideanRotationKnob() noexcept { return euclideanRotationKnob; }
    juce::TextButton& getCloseButton() noexcept { return closeButton; }

    [[nodiscard]] bool areEuclideanControlsVisibleForTesting() const noexcept;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ChoiceAttachment = juce::ParameterAttachment;

    class EuclideanCycleVisualizer final : public juce::Component
    {
    public:
        void setCycle(std::array<bool, 16> newCycle, int newStepCount) noexcept;
        void paint(juce::Graphics& g) override;

    private:
        std::array<bool, 16> cycle {};
        int stepCount = 8;
    };

    void refreshEuclideanVisualizer();
    void refreshEuclideanVisibility();
    void requestClose();
    juce::String getCurrentParameterText(const char* parameterId) const;

    juce::AudioProcessorValueTreeState& apvts;
    bool isStandalone = false;
    bool euclideanControlsVisible = false;
    std::function<void()> onRequestClose;

    coolsynth::ui::SynthSection rhythmSection { "Rhythm" };
    coolsynth::ui::SynthSection modifiersSection { "Modifiers" };
    coolsynth::ui::SynthSection tempoSection { "Internal Tempo" };

    juce::TextButton closeButton { "Close" };
    coolsynth::ui::SegmentedChoiceGroup rhythmChoice {
        "Rhythm",
        {
            { "Straight", 0 },
            { "Euclidean", 1 },
        },
        2
    };
    coolsynth::ui::HardwareKnob euclideanPulsesKnob { "Pulses" };
    coolsynth::ui::HardwareKnob euclideanStepsKnob { "Steps" };
    coolsynth::ui::HardwareKnob euclideanRotationKnob { "Rotation" };
    EuclideanCycleVisualizer euclideanVisualizer;
    coolsynth::ui::HardwareKnob chanceKnob { "Chance" };
    coolsynth::ui::SegmentedChoiceGroup ratchetChoice {
        "Ratchet",
        {
            { "Off", 0 },
            { "x2", 1 },
            { "x3", 2 },
            { "x4", 3 },
        },
        4
    };
    coolsynth::ui::HardwareKnob ratchetChanceKnob { "Rat Ch" };
    coolsynth::ui::SegmentedChoiceGroup accentChoice {
        "Accent Every",
        {
            { "Off", 0 },
            { "/2", 1 },
            { "/3", 2 },
            { "/4", 3 },
        },
        4
    };
    coolsynth::ui::HardwareKnob accentAmountKnob { "Accent" };
    coolsynth::ui::HardwareKnob tempoKnob { "Tempo" };

    std::unique_ptr<SliderAttachment> tempoAttachment;
    std::unique_ptr<ChoiceAttachment> rhythmAttachment;
    std::unique_ptr<SliderAttachment> chanceAttachment;
    std::unique_ptr<ChoiceAttachment> ratchetAttachment;
    std::unique_ptr<SliderAttachment> ratchetChanceAttachment;
    std::unique_ptr<ChoiceAttachment> accentAttachment;
    std::unique_ptr<SliderAttachment> accentAmountAttachment;
    std::unique_ptr<SliderAttachment> euclideanPulsesAttachment;
    std::unique_ptr<SliderAttachment> euclideanStepsAttachment;
    std::unique_ptr<SliderAttachment> euclideanRotationAttachment;
};
