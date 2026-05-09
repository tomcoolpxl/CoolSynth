#include "HardwareKnob.h"

namespace coolsynth::ui
{
    HardwareKnob::HardwareKnob(juce::String labelText)
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(knob);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(valueLabel);
    }

    void HardwareKnob::setValueText(const juce::String& text)
    {
        valueLabel.setText(text, juce::dontSendNotification);
    }

    void HardwareKnob::setLearnState(bool armed, const juce::String& badgeText)
    {
        if (isArmed != armed || learnedBadge != badgeText)
        {
            isArmed = armed;
            learnedBadge = badgeText;
            repaint();
        }
    }

    void HardwareKnob::resized()
    {
        auto area = getLocalBounds();
        titleLabel.setBounds(area.removeFromTop(20));
        valueLabel.setBounds(area.removeFromBottom(20));
        knob.setBounds(area);
    }

    void HardwareKnob::paint(juce::Graphics& g)
    {
        if (isArmed)
        {
            g.setColour(juce::Colours::yellow.withAlpha(0.3f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
            g.setColour(juce::Colours::yellow);
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 5.0f, 2.0f);
        }
        else if (learnedBadge.isNotEmpty())
        {
            auto badgeArea = getLocalBounds().withSizeKeepingCentre(40, 16);
            g.setColour(juce::Colours::lightblue.withAlpha(0.8f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 4.0f);
            
            g.setColour(juce::Colours::black);
            g.setFont(12.0f);
            g.drawText(learnedBadge, badgeArea, juce::Justification::centred, false);
        }
    }
}
