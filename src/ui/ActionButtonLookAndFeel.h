#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "UiPalette.h"

namespace coolsynth::ui
{
    class GreenActionButtonLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonBackground(juce::Graphics& g,
                                  juce::Button& button,
                                  const juce::Colour&,
                                  bool isMouseOverButton,
                                  bool isButtonDown) override
        {
            auto bounds = button.getLocalBounds().toFloat();
            const bool isLatched = button.getToggleState();
            auto fill = isLatched ? palette::ledGreen : palette::panelRaised;

            if (! isLatched && isMouseOverButton)
                fill = palette::panelRaisedAlt;

            if (isButtonDown)
                fill = palette::ledGreen;

            g.setColour(fill);
            g.fillRect(bounds);

            g.setColour(isButtonDown ? palette::ledGreen : palette::ledGreen);
            g.drawRect(bounds, 1.0f);
        }

        void drawButtonText(juce::Graphics& g,
                            juce::TextButton& button,
                            bool,
                            bool isButtonDown) override
        {
            auto bounds = button.getLocalBounds().reduced(4, 2);
            const bool isActive = isButtonDown || button.getToggleState();
            const auto ink = isActive ? palette::ledTextOn : palette::ledTextOff;
            const bool isPatchButton = button.getComponentID() == "patchButton";

            g.setColour(ink);

            if (button.getComponentID() == "panicButton")
            {
                const auto size = static_cast<float>(juce::jmin(bounds.getWidth(), bounds.getHeight()) - 10);
                auto icon = juce::Rectangle<float>(size, size).withCentre(bounds.toFloat().getCentre());
                g.fillRect(icon);
                return;
            }

            if (button.getComponentID() == "ledModeButton")
            {
                auto iconBounds = bounds.toFloat().reduced(5.0f, 4.0f);
                juce::Path arrow;
                const auto centreX = iconBounds.getCentreX();
                const auto topY = iconBounds.getY();
                const auto midY = iconBounds.getCentreY() + 1.0f;
                const auto bottomY = iconBounds.getBottom();

                arrow.startNewSubPath(centreX, topY);
                arrow.lineTo(iconBounds.getRight(), midY);
                arrow.startNewSubPath(centreX, topY);
                arrow.lineTo(iconBounds.getX(), midY);
                arrow.startNewSubPath(centreX, topY + 1.0f);
                arrow.lineTo(centreX, bottomY);

                g.strokePath(arrow, juce::PathStrokeType(1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }

            g.setFont(juce::FontOptions(13.0f, isPatchButton ? juce::Font::bold : juce::Font::plain));
            const auto label = button.getComponentID() == "tooltipToggleButton"
                ? button.getButtonText()
                : button.getButtonText().toUpperCase();
            g.drawFittedText(label,
                             bounds,
                             juce::Justification::centred,
                             1);
        }
    };

    inline GreenActionButtonLookAndFeel& getGreenActionButtonLookAndFeel()
    {
        static GreenActionButtonLookAndFeel lookAndFeel;
        return lookAndFeel;
    }

    inline void applyGreenActionButtonStyle(juce::TextButton& button,
                                            juce::String componentId = {})
    {
        if (componentId.isNotEmpty())
            button.setComponentID(componentId);

        button.setColour(juce::TextButton::buttonColourId, palette::panelRaised);
        button.setColour(juce::TextButton::buttonOnColourId, palette::ledGreen);
        button.setLookAndFeel(&getGreenActionButtonLookAndFeel());
    }
}
