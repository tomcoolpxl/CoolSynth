#include "SegmentedChoiceGroup.h"

#include <cmath>
#include <utility>

#include "UiPalette.h"

namespace coolsynth::ui
{
    SegmentedChoiceGroup::SegmentButton::SegmentButton(Option optionToUse)
        : juce::Button(optionToUse.label),
          option(std::move(optionToUse))
    {
    }

    void SegmentedChoiceGroup::SegmentButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown)
    {
        auto bounds = getLocalBounds().toFloat();
        const bool isActive = getToggleState();
        auto fill = isActive ? palette::ledGreen : palette::panelRaised;
        auto stroke = isActive ? palette::ledGreen : palette::ledGreen;
        auto ink = isActive ? palette::ledTextOn : palette::ledTextOff;

        if (!isActive && isHighlighted)
            fill = palette::panelRaisedAlt;

        if (isDown)
            fill = fill.darker(0.08f);

        g.setColour(fill);
        g.fillRect(bounds);

        g.setColour(stroke);
        g.drawRect(bounds, 1.0f);

        if (option.icon == Icon::none)
        {
            g.setColour(ink);
            g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
            g.drawFittedText(option.label.toUpperCase(), getLocalBounds().reduced(3, 2), juce::Justification::centred, 2);
            return;
        }

        auto iconArea = bounds.reduced(5.0f, 4.0f);
        auto textArea = iconArea.removeFromBottom(14.0f);
        drawWaveformIcon(g, iconArea, option.icon, ink);

        g.setColour(ink);
        g.setFont(juce::FontOptions(8.8f, juce::Font::plain));
        g.drawFittedText(option.label.toUpperCase(), textArea.toNearestInt(), juce::Justification::centred, 1);
    }

    void SegmentedChoiceGroup::SegmentButton::drawWaveformIcon(juce::Graphics& g,
                                                               juce::Rectangle<float> area,
                                                               Icon icon,
                                                               juce::Colour colour)
    {
        juce::Path path;
        const auto left = area.getX();
        const auto right = area.getRight();
        const auto top = area.getY() + area.getHeight() * 0.25f;
        const auto bottom = area.getBottom() - area.getHeight() * 0.2f;

        switch (icon)
        {
            case Icon::pulse:
                path.startNewSubPath(left, bottom);
                path.lineTo(left + area.getWidth() * 0.15f, bottom);
                path.lineTo(left + area.getWidth() * 0.15f, top);
                path.lineTo(left + area.getWidth() * 0.55f, top);
                path.lineTo(left + area.getWidth() * 0.55f, bottom);
                path.lineTo(right, bottom);
                break;

            case Icon::triangle:
                path.startNewSubPath(left, bottom);
                path.lineTo(area.getCentreX(), top);
                path.lineTo(right, bottom);
                break;

            case Icon::saw:
                path.startNewSubPath(left, bottom);
                path.lineTo(right - area.getWidth() * 0.12f, top);
                path.lineTo(right - area.getWidth() * 0.12f, bottom);
                break;

            case Icon::square:
                path.startNewSubPath(left, bottom);
                path.lineTo(left + area.getWidth() * 0.18f, bottom);
                path.lineTo(left + area.getWidth() * 0.18f, top);
                path.lineTo(area.getCentreX(), top);
                path.lineTo(area.getCentreX(), bottom);
                path.lineTo(right, bottom);
                break;

            case Icon::none:
                return;
        }

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(1.7f));
    }

    SegmentedChoiceGroup::SegmentedChoiceGroup(juce::String labelText,
                                               std::vector<Option> options,
                                               int columnCount)
        : columns(columnCount > 0 ? columnCount : static_cast<int>(options.size()))
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, palette::textPrimary);
        titleLabel.setFont(juce::FontOptions(15.0f));
        addAndMakeVisible(titleLabel);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, palette::textSecondary);
        valueLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(valueLabel);

        buttons.reserve(options.size());
        for (auto& option : options)
        {
            auto button = std::make_unique<SegmentButton>(std::move(option));
            const auto value = button->getOptionValue();
            button->onClick = [this, value] { handleSelection(value); };
            addAndMakeVisible(*button);
            buttons.push_back(std::move(button));
        }
    }

    void SegmentedChoiceGroup::setSelectedValue(int value)
    {
        selectedValue = value;
        for (const auto& button : buttons)
            button->setToggleState(button->getOptionValue() == value, juce::dontSendNotification);
    }

    void SegmentedChoiceGroup::setValueText(const juce::String& text)
    {
        valueLabel.setText(text, juce::dontSendNotification);
    }

    void SegmentedChoiceGroup::setLearnState(bool armed, const juce::String& badgeText)
    {
        if (isArmed != armed || learnedBadge != badgeText)
        {
            isArmed = armed;
            learnedBadge = badgeText;
            repaint();
        }
    }

    void SegmentedChoiceGroup::setOptionTooltip(int value, const juce::String& tooltipText)
    {
        for (const auto& button : buttons)
            if (button->getOptionValue() == value)
                button->setTooltip(tooltipText);
    }

    void SegmentedChoiceGroup::resized()
    {
        auto area = getLocalBounds();
        titleLabel.setBounds(area.removeFromTop(20));
        valueLabel.setBounds(area.removeFromBottom(18));
        auto buttonArea = area.reduced(0, 4);

        const auto rows = juce::jmax(1, static_cast<int>(std::ceil(static_cast<float>(buttons.size())
                                                                    / static_cast<float>(columns))));

        for (int index = 0; index < static_cast<int>(buttons.size()); ++index)
        {
            const auto row = index / columns;
            const auto column = index % columns;

            const auto x = buttonArea.getX() + (buttonArea.getWidth() * column) / columns;
            const auto nextX = buttonArea.getX() + (buttonArea.getWidth() * (column + 1)) / columns;
            const auto y = buttonArea.getY() + (buttonArea.getHeight() * row) / rows;
            const auto nextY = buttonArea.getY() + (buttonArea.getHeight() * (row + 1)) / rows;
            buttons[static_cast<size_t>(index)]->setBounds(x, y, nextX - x, nextY - y);

            int edges = 0;
            if (column > 0)
                edges |= juce::Button::ConnectedOnLeft;
            if (column + 1 < columns && index + 1 < static_cast<int>(buttons.size()))
                edges |= juce::Button::ConnectedOnRight;
            if (row > 0 && index - columns >= 0)
                edges |= juce::Button::ConnectedOnTop;
            if (index + columns < static_cast<int>(buttons.size()))
                edges |= juce::Button::ConnectedOnBottom;

            buttons[static_cast<size_t>(index)]->setConnectedEdges(edges);
        }
    }

    void SegmentedChoiceGroup::paint(juce::Graphics& g)
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
            badgeArea.removeFromTop(54);
            badgeArea.removeFromBottom(21);
            badgeArea = badgeArea.withSizeKeepingCentre(38, 8);
            g.setColour(palette::badgeGreen.withAlpha(0.72f));
            g.fillRect(badgeArea);
            g.setColour(palette::panelBlack);
            g.setFont(8.0f);
            g.drawText(learnedBadge, badgeArea, juce::Justification::centred, false);
        }
    }

    void SegmentedChoiceGroup::handleSelection(int value)
    {
        if (selectedValue == value)
            return;

        setSelectedValue(value);

        if (onSelectionChanged != nullptr)
            onSelectionChanged(value);
    }
}
