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

            const bool isKeysToggle = button.getComponentID() == "keysToggleButton";
            const float fontSize = isKeysToggle ? 9.0f : 13.0f;
            const auto fontStyle = (isPatchButton || isKeysToggle) ? juce::Font::bold : juce::Font::plain;
            g.setFont(juce::FontOptions(fontSize, fontStyle));
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

    class GreenComboBoxLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawComboBox(juce::Graphics& g,
                          int width,
                          int height,
                          bool isButtonDown,
                          int /*buttonX*/,
                          int /*buttonY*/,
                          int /*buttonW*/,
                          int /*buttonH*/,
                          juce::ComboBox& box) override
        {
            auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                                 static_cast<float>(width),
                                                 static_cast<float>(height));
            auto fill = palette::panelRaised;
            if (box.isMouseOver(true) && box.isEnabled())
                fill = palette::panelRaisedAlt;
            if (isButtonDown)
                fill = palette::ledGreen;

            g.setColour(fill);
            g.fillRect(bounds);

            g.setColour(palette::ledGreen);
            g.drawRect(bounds, 1.0f);

            auto arrowZone = juce::Rectangle<float>(static_cast<float>(width - 18),
                                                    0.0f,
                                                    16.0f,
                                                    static_cast<float>(height)).reduced(2.0f, 8.0f);
            juce::Path arrow;
            arrow.addTriangle(arrowZone.getX(), arrowZone.getY(),
                              arrowZone.getRight(), arrowZone.getY(),
                              arrowZone.getCentreX(), arrowZone.getBottom());
            g.setColour(isButtonDown ? palette::ledTextOn : palette::ledTextOff);
            g.fillPath(arrow);
        }

        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions(12.0f, juce::Font::bold));
        }

        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds(8, 0, box.getWidth() - 24, box.getHeight());
            label.setFont(getComboBoxFont(box));
        }
    };

    inline GreenComboBoxLookAndFeel& getGreenComboBoxLookAndFeel()
    {
        static GreenComboBoxLookAndFeel lookAndFeel;
        return lookAndFeel;
    }

    inline void applyGreenComboBoxStyle(juce::ComboBox& box)
    {
        box.setColour(juce::ComboBox::backgroundColourId, palette::panelRaised);
        box.setColour(juce::ComboBox::textColourId, palette::ledTextOff);
        box.setColour(juce::ComboBox::outlineColourId, palette::ledGreen);
        box.setColour(juce::ComboBox::arrowColourId, palette::ledTextOff);
        box.setColour(juce::ComboBox::focusedOutlineColourId, palette::ledGreen);
        box.setLookAndFeel(&getGreenComboBoxLookAndFeel());
    }
}
