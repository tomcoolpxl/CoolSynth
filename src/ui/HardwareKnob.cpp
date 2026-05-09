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
            g.setColour(juce::Colours::lightblue.withAlpha(0.2f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
            
            g.setColour(juce::Colours::lightblue);
            g.setFont(10.0f);
            g.drawText(learnedBadge, getLocalBounds().withSizeKeepingCentre(40, 16).translated(0, -15), juce::Justification::centred, false);
        }
    }
}
