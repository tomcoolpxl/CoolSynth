#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace coolsynth::ui
{
    class HardwareFader final : public juce::Component
    {
    public:
        explicit HardwareFader(juce::String labelText);

        juce::Slider& slider() noexcept { return fader; }
        void setValueText(const juce::String& text);

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::Label valueLabel;
    };
}
