#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace coolsynth::ui
{
    class LedToggleButton final : public juce::Component,
                                  public juce::SettableTooltipClient
    {
    public:
        enum class LayoutMode
        {
            full,
            compactHeader,
            compactBody,
        };

        explicit LedToggleButton(juce::String labelText);

        juce::Button& button() noexcept { return stateButton; }
        void setValueText(const juce::String& text);
        void setLearnState(bool isArmed, const juce::String& badgeText);
        void setLayoutMode(LayoutMode newMode);

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        class StateButton final : public juce::Button
        {
        public:
            StateButton();
            void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;
        };

        juce::Label titleLabel;
        StateButton stateButton;
        juce::Label valueLabel;
        LayoutMode layoutMode = LayoutMode::full;
        bool isArmed = false;
        juce::String learnedBadge;
    };
}
