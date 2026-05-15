#include "HardwareKnob.h"

#include <cmath>

#include "UiPalette.h"

namespace coolsynth::ui
{
    namespace
    {
        class GreenKnobLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            void drawRotarySlider(juce::Graphics& g,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  float sliderPosProportional,
                                  float rotaryStartAngle,
                                  float rotaryEndAngle,
                                  juce::Slider&) override
            {
                auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                                     static_cast<float>(y),
                                                     static_cast<float>(width),
                                                     static_cast<float>(height)).reduced(8.0f);
                const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
                const auto centre = bounds.getCentre();
                const auto arcRadius = radius * 0.72f;
                const auto pointerLength = radius * 0.78f;
                const auto pointerThickness = juce::jmax(3.5f, radius * 0.12f);
                const auto tickStartRadius = arcRadius + radius * 0.10f;
                const auto tickEndRadius = tickStartRadius + radius * 0.10f;
                const auto currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

                juce::Path backgroundArc;
                backgroundArc.addCentredArc(centre.x,
                                            centre.y,
                                            arcRadius,
                                            arcRadius,
                                            0.0f,
                                            rotaryStartAngle,
                                            rotaryEndAngle,
                                            true);

                g.setColour(palette::ledGreenDim);
                g.strokePath(backgroundArc, juce::PathStrokeType(4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path activeArc;
                activeArc.addCentredArc(centre.x,
                                        centre.y,
                                        arcRadius,
                                        arcRadius,
                                        0.0f,
                                        rotaryStartAngle,
                                        currentAngle,
                                        true);

                g.setColour(palette::ledGreen.withAlpha(0.65f));
                g.strokePath(activeArc, juce::PathStrokeType(4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                auto drawTick = [&](float angle, float thickness)
                {
                    const auto inner = centre.getPointOnCircumference(tickStartRadius, angle);
                    const auto outer = centre.getPointOnCircumference(tickEndRadius, angle);
                    g.drawLine(inner.x, inner.y, outer.x, outer.y, thickness);
                };

                g.setColour(palette::ledTextOff);
                drawTick(rotaryStartAngle, 1.4f);
                drawTick((rotaryStartAngle + rotaryEndAngle) * 0.5f, 1.2f);
                drawTick(rotaryEndAngle, 1.4f);

                const auto pointerEnd = centre.getPointOnCircumference(pointerLength, currentAngle);
                g.setColour(palette::ledGreen);
                g.drawLine(centre.x, centre.y, pointerEnd.x, pointerEnd.y, pointerThickness);

                g.setColour(palette::panelRaised);
                g.fillEllipse(centre.x - radius * 0.12f,
                              centre.y - radius * 0.12f,
                              radius * 0.24f,
                              radius * 0.24f);
                g.setColour(palette::ledGreen.withAlpha(0.8f));
                g.drawEllipse(centre.x - radius * 0.12f,
                              centre.y - radius * 0.12f,
                              radius * 0.24f,
                              radius * 0.24f,
                              1.0f);
            }
        };

        GreenKnobLookAndFeel greenKnobLookAndFeel;
    }

    HardwareKnob::HardwareKnob(juce::String labelText)
    {
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, palette::textPrimary);
        titleLabel.setFont(juce::FontOptions(15.0f));
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(knob);

        valueLabel.setText("-", juce::dontSendNotification);
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, palette::textSecondary);
        valueLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(valueLabel);

        knob.setLookAndFeel(&greenKnobLookAndFeel);
        knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                 juce::MathConstants<float>::pi * 2.8f,
                                 true);
        knob.setColour(juce::Slider::rotarySliderFillColourId, palette::ledGreen);
        knob.setColour(juce::Slider::rotarySliderOutlineColourId, palette::ledGreenDim);
    }

    HardwareKnob::~HardwareKnob()
    {
        knob.setLookAndFeel(nullptr);
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
