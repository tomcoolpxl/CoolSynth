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
        void setTooltip(const juce::String& newTooltip) override;

    private:
        // Repaints on focus changes so the custom look-and-feel can draw a focus ring.
        class FocusAwareSlider final : public juce::Slider
        {
        public:
            using juce::Slider::Slider;
            void focusGained(FocusChangeType) override { repaint(); }
            void focusLost(FocusChangeType) override { repaint(); }
        };

        juce::Label titleLabel;
        FocusAwareSlider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label valueLabel;
        bool isArmed = false;
        juce::String learnedBadge;
    };
}
