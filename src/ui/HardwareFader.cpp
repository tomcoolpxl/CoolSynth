#include "HardwareFader.h"

#include "UiPalette.h"

namespace coolsynth::ui
{
    namespace
    {
        class GreenFaderLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            void drawLinearSlider(juce::Graphics& g,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  float sliderPos,
                                  float minSliderPos,
                                  float maxSliderPos,
                                  const juce::Slider::SliderStyle style,
                                  juce::Slider& slider) override
            {
                if (style != juce::Slider::LinearVertical)
                {
                    juce::LookAndFeel_V4::drawLinearSlider(g,
                                                           x,
                                                           y,
                                                           width,
                                                           height,
                                                           sliderPos,
                                                           minSliderPos,
                                                           maxSliderPos,
                                                           style,
                                                           slider);
                    return;
                }

                auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                                     static_cast<float>(y),
                                                     static_cast<float>(width),
                                                     static_cast<float>(height)).reduced(10.0f, 8.0f);
                const auto centreX = bounds.getCentreX();
                const auto trackWidth = juce::jmax(6.0f, bounds.getWidth() * 0.12f);
                const auto trackTop = bounds.getY() + bounds.getHeight() * 0.06f;
                const auto trackBottom = bounds.getBottom() - bounds.getHeight() * 0.06f;
                const auto trackHeight = trackBottom - trackTop;
                const auto currentY = juce::jlimit(trackTop, trackBottom, sliderPos);
                const auto tickLength = bounds.getWidth() * 0.08f;

                auto track = juce::Rectangle<float>(centreX - trackWidth * 0.5f,
                                                    trackTop,
                                                    trackWidth,
                                                    trackHeight);

                g.setColour(palette::ledGreenDim);
                g.fillRoundedRectangle(track, trackWidth * 0.5f);

                auto activeTrack = juce::Rectangle<float>(track.getX(),
                                                          currentY,
                                                          track.getWidth(),
                                                          trackBottom - currentY);
                g.setColour(palette::ledGreen.withAlpha(0.65f));
                g.fillRoundedRectangle(activeTrack, trackWidth * 0.5f);

                g.setColour(palette::ledTextOff);
                const auto drawTick = [&](float tickY, float thickness)
                {
                    g.drawLine(centreX + trackWidth,
                               tickY,
                               centreX + trackWidth + tickLength,
                               tickY,
                               thickness);
                };

                drawTick(trackTop, 1.4f);
                drawTick(trackTop + trackHeight * 0.5f, 1.2f);
                drawTick(trackBottom, 1.4f);

                g.setColour(palette::ledGreen);
                g.drawLine(centreX - trackWidth * 0.45f,
                           currentY,
                           centreX + trackWidth * 0.45f,
                           currentY,
                           juce::jmax(3.5f, trackWidth * 0.9f));

                g.setColour(palette::panelRaised);
                g.fillEllipse(centreX - trackWidth * 0.38f,
                              currentY - trackWidth * 0.38f,
                              trackWidth * 0.76f,
                              trackWidth * 0.76f);
                g.setColour(palette::ledGreen.withAlpha(0.8f));
                g.drawEllipse(centreX - trackWidth * 0.38f,
                              currentY - trackWidth * 0.38f,
                              trackWidth * 0.76f,
                              trackWidth * 0.76f,
                              1.0f);
            }
        };

        GreenFaderLookAndFeel greenFaderLookAndFeel;
    }

    HardwareFader::HardwareFader(juce::String labelText)
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, palette::textPrimary);
        titleLabel.setFont(juce::FontOptions(15.0f));
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(fader);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, palette::textSecondary);
        valueLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(valueLabel);

        fader.setLookAndFeel(&greenFaderLookAndFeel);
        fader.setColour(juce::Slider::trackColourId, palette::ledGreenDim);
        fader.setColour(juce::Slider::thumbColourId, palette::ledGreen);
        fader.setColour(juce::Slider::backgroundColourId, palette::panelRaised);
    }

    HardwareFader::~HardwareFader()
    {
        fader.setLookAndFeel(nullptr);
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
            g.setColour(palette::learnYellow.withAlpha(0.18f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
            g.setColour(palette::learnYellow);
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 5.0f, 2.0f);
        }
        else if (learnedBadge.isNotEmpty())
        {
            auto badgeArea = getLocalBounds();
            badgeArea.removeFromTop(69);
            badgeArea.removeFromBottom(23);
            badgeArea = badgeArea.withSizeKeepingCentre(38, 8);
            g.setColour(palette::badgeGreen.withAlpha(0.72f));
            g.fillRect(badgeArea);
            
            g.setColour(palette::panelBlack);
            g.setFont(8.0f);
            g.drawText(learnedBadge, badgeArea, juce::Justification::centred, false);
        }
    }
}
