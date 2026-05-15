#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace coolsynth::ui
{
    class HardwareKnob final : public juce::Component,
                               public juce::SettableTooltipClient
    {
    public:
        explicit HardwareKnob(juce::String labelText);
        ~HardwareKnob() override;

        juce::Slider& slider() noexcept { return knob; }
        void setValueText(const juce::String& text);
        void setLearnState(bool isArmed, const juce::String& badgeText);

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label valueLabel;
        bool isArmed = false;
        juce::String learnedBadge;
    };
}
