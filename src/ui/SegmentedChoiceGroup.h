#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace coolsynth::ui
{
    class SegmentedChoiceGroup final : public juce::Component,
                                       public juce::SettableTooltipClient
    {
    public:
        enum class Icon
        {
            none,
            pulse,
            triangle,
            saw,
            sine,
            square,
        };

        struct Option
        {
            juce::String label;
            int value = 0;
            Icon icon = Icon::none;
        };

        SegmentedChoiceGroup(juce::String labelText,
                             std::vector<Option> options,
                             int columnCount = 0);

        void setSelectedValue(int value);
        int getSelectedValue() const noexcept { return selectedValue; }
        void setValueText(const juce::String& text);
        void setLearnState(bool isArmed, const juce::String& badgeText);
        void setOptionTooltip(int value, const juce::String& tooltipText);

        void resized() override;
        void paint(juce::Graphics& g) override;

        std::function<void(int)> onSelectionChanged;

    private:
        class SegmentButton final : public juce::Button
        {
        public:
            explicit SegmentButton(Option optionToUse);

            int getOptionValue() const noexcept { return option.value; }
            void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

        private:
            static void drawWaveformIcon(juce::Graphics& g,
                                         juce::Rectangle<float> area,
                                         Icon icon,
                                         juce::Colour colour);

            Option option;
        };

        void handleSelection(int value);

        juce::Label titleLabel;
        juce::Label valueLabel;
        std::vector<std::unique_ptr<SegmentButton>> buttons;
        int columns = 1;
        int selectedValue = 0;
        bool isArmed = false;
        juce::String learnedBadge;
    };
}
