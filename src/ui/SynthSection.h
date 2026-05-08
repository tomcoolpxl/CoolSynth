#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace coolsynth::ui
{
    class SynthSection final : public juce::Component
    {
    public:
        explicit SynthSection(juce::String titleText);

        juce::Rectangle<int> getContentBounds() const noexcept;

        void paint(juce::Graphics& g) override;

    private:
        juce::String title;
    };
}
