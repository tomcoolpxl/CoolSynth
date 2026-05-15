#include "SynthSection.h"

#include "UiPalette.h"

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
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient gradient(palette::panelRaisedAlt,
                                      bounds.getX(),
                                      bounds.getY(),
                                      palette::panelRaised,
                                      bounds.getX(),
                                      bounds.getBottom(),
                                      false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, 12.0f);

        g.setColour(palette::panelStroke);
        g.drawRoundedRectangle(bounds, 12.0f, 1.0f);

        g.setColour(palette::ledTextOff);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(),
                   getLocalBounds().removeFromTop(32).reduced(12, 0),
                   juce::Justification::left);
    }
}
