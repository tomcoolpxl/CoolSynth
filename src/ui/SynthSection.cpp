#include "SynthSection.h"

namespace coolsynth::ui
{
    SynthSection::SynthSection(juce::String titleText)
        : title(std::move(titleText))
    {
    }

    juce::Rectangle<int> SynthSection::getContentBounds() const noexcept
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(24);
        return area;
    }

    void SynthSection::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colours::grey.withAlpha(0.1f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);

        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), 10.0f, 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(18.0f);
        g.drawText(title, getLocalBounds().removeFromTop(32).reduced(12, 0), juce::Justification::left);
    }
}
