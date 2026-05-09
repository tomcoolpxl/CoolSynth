#include "HardwareFader.h"

namespace coolsynth::ui
{
    HardwareFader::HardwareFader(juce::String labelText)
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(fader);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(valueLabel);
    }

    void HardwareFader::setValueText(const juce::String& text)
    {
        valueLabel.setText(text, juce::dontSendNotification);
    }

    void HardwareFader::setLearnState(bool armed, const juce::String& badgeText)
    {
        if (isArmed != armed || learnedBadge != badgeText)
        {
            isArmed = armed;
            learnedBadge = badgeText;
            repaint();
        }
    }

    void HardwareFader::resized()
    {
        auto area = getLocalBounds();
        titleLabel.setBounds(area.removeFromTop(20));
        valueLabel.setBounds(area.removeFromBottom(20));
        fader.setBounds(area);
    }

    void HardwareFader::paint(juce::Graphics& g)
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
