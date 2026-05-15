#include "LedToggleButton.h"

#include "UiPalette.h"

namespace coolsynth::ui
{
    LedToggleButton::StateButton::StateButton()
        : juce::Button("state")
    {
        setClickingTogglesState(true);
    }

    void LedToggleButton::StateButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown)
    {
        auto bounds = getLocalBounds().toFloat();
        const bool isActive = getToggleState();
        auto fill = isActive ? palette::ledGreen : palette::panelRaised;
        auto stroke = isActive ? palette::ledGreen : palette::ledGreen;
        auto textColour = isActive ? palette::ledTextOn : palette::ledTextOff;

        if (!isActive && isHighlighted)
            fill = palette::panelRaisedAlt;

        if (isDown)
            fill = fill.darker(0.1f);

        g.setColour(fill);
        g.fillRect(bounds);

        g.setColour(stroke);
        g.drawRect(bounds, 1.0f);

        g.setColour(textColour);
        g.setFont(juce::FontOptions(11.5f, juce::Font::plain));
        g.drawFittedText(isActive ? "ON" : "OFF", getLocalBounds(), juce::Justification::centred, 1);
    }

    LedToggleButton::LedToggleButton(juce::String labelText)
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, palette::textPrimary);
        titleLabel.setFont(juce::FontOptions(15.0f));
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(stateButton);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, palette::textSecondary);
        valueLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(valueLabel);
    }

    void LedToggleButton::setValueText(const juce::String& text)
    {
        valueLabel.setText(text, juce::dontSendNotification);
    }

    void LedToggleButton::setLearnState(bool armed, const juce::String& badgeText)
    {
        if (isArmed != armed || learnedBadge != badgeText)
        {
            isArmed = armed;
            learnedBadge = badgeText;
            repaint();
        }
    }

    void LedToggleButton::setLayoutMode(LayoutMode newMode)
    {
        if (layoutMode == newMode)
            return;

        layoutMode = newMode;
        titleLabel.setVisible(layoutMode != LayoutMode::compactHeader);
        valueLabel.setVisible(layoutMode == LayoutMode::full);
        resized();
        repaint();
    }

    void LedToggleButton::resized()
    {
        if (layoutMode == LayoutMode::compactHeader)
        {
            stateButton.setBounds(getLocalBounds());
            return;
        }

        if (layoutMode == LayoutMode::compactBody)
        {
            auto area = getLocalBounds();
            titleLabel.setBounds(area.removeFromTop(18));
            auto buttonArea = area.removeFromTop(28);
            stateButton.setBounds(buttonArea.withSizeKeepingCentre(28, 24));
            return;
        }

        auto area = getLocalBounds();
        titleLabel.setBounds(area.removeFromTop(20));
        valueLabel.setBounds(area.removeFromBottom(18));
        auto buttonArea = area.reduced(0, 6);
        const auto buttonWidth = juce::jmin(buttonArea.getWidth(), 44);
        const auto buttonHeight = juce::jmin(buttonArea.getHeight(), 28);
        stateButton.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, buttonHeight));
    }

    void LedToggleButton::paint(juce::Graphics& g)
    {
        if (isArmed)
        {
            g.setColour(palette::learnYellow.withAlpha(0.18f));
            g.fillRect(getLocalBounds().toFloat());
            g.setColour(palette::learnYellow);
            g.drawRect(getLocalBounds().toFloat(), 2.0f);
            return;
        }

        if (learnedBadge.isNotEmpty())
        {
            auto badgeArea = getLocalBounds();
            if (layoutMode == LayoutMode::compactHeader)
            {
                badgeArea = badgeArea.removeFromBottom(8).withSizeKeepingCentre(38, 8);
            }
            else if (layoutMode == LayoutMode::compactBody)
            {
                badgeArea.removeFromTop(24);
                badgeArea = badgeArea.removeFromTop(8).withSizeKeepingCentre(30, 8);
            }
            else
            {
                badgeArea.removeFromTop(54);
                badgeArea.removeFromBottom(21);
                badgeArea = badgeArea.withSizeKeepingCentre(38, 8);
            }
            g.setColour(palette::badgeGreen.withAlpha(0.72f));
            g.fillRect(badgeArea);
            g.setColour(palette::panelBlack);
            g.setFont(8.0f);
            g.drawText(learnedBadge, badgeArea, juce::Justification::centred, false);
        }
    }
}
