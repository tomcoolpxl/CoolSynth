#include "ArpAdvancedOverlay.h"

#include <cmath>

#include "parameters/ParameterIDs.h"
#include "synth/EuclideanRhythm.h"
#include "ui/ActionButtonLookAndFeel.h"
#include "ui/UiPalette.h"

namespace
{
    namespace Layout
    {
        inline constexpr int outerMargin = 64;
        inline constexpr int panelMaxWidth = 860;
        inline constexpr int panelStandaloneHeight = 430;
        inline constexpr int panelPluginHeight = 360;
        inline constexpr int panelInset = 18;
        inline constexpr int panelHeaderHeight = 36;
        inline constexpr int sectionGap = 10;
        inline constexpr int sectionContentInset = 12;
        inline constexpr int sectionTitleHeight = 24;
        inline constexpr int closeButtonWidth = 96;
        inline constexpr int closeButtonHeight = 24;
    }

    juce::Rectangle<int> sectionContentBounds(const coolsynth::ui::SynthSection& section) noexcept
    {
        return section.getBounds().reduced(Layout::sectionContentInset)
                      .withTrimmedTop(Layout::sectionTitleHeight);
    }
}

void ArpAdvancedOverlay::EuclideanCycleVisualizer::setCycle(std::array<bool, 16> newCycle,
                                                            int newStepCount) noexcept
{
    cycle = newCycle;
    stepCount = juce::jlimit(1, 16, newStepCount);
    repaint();
}

void ArpAdvancedOverlay::EuclideanCycleVisualizer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(coolsynth::ui::palette::panelBlack.withAlpha(0.55f));
    g.fillRoundedRectangle(bounds, 10.0f);

    g.setColour(coolsynth::ui::palette::panelStroke.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

    auto content = bounds.reduced(10.0f, 12.0f);
    const float stepGap = 6.0f;
    const float totalGap = stepGap * static_cast<float>(juce::jmax(0, stepCount - 1));
    const float stepWidth = juce::jmax(10.0f, (content.getWidth() - totalGap) / static_cast<float>(stepCount));

    for (int index = 0; index < stepCount; ++index)
    {
        auto stepBounds = juce::Rectangle<float>(stepWidth, content.getHeight()).withPosition(
            content.getX() + static_cast<float>(index) * (stepWidth + stepGap),
            content.getY());

        const bool active = cycle[static_cast<size_t>(index)];
        const auto fill = active ? coolsynth::ui::palette::ledGreen
                                 : coolsynth::ui::palette::panelRaisedAlt;
        const auto stroke = active ? coolsynth::ui::palette::ledGreen.brighter(0.15f)
                                   : coolsynth::ui::palette::panelStroke;

        g.setColour(fill.withAlpha(active ? 0.9f : 0.65f));
        g.fillRoundedRectangle(stepBounds.reduced(0.5f), 6.0f);
        g.setColour(stroke);
        g.drawRoundedRectangle(stepBounds.reduced(0.5f), 6.0f, 1.0f);
    }
}

ArpAdvancedOverlay::ArpAdvancedOverlay(juce::AudioProcessorValueTreeState& valueTreeState,
                                       bool isStandaloneApp)
    : apvts(valueTreeState)
    , isStandalone(isStandaloneApp)
{
    namespace ids = coolsynth::parameters::ids;

    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);

    addAndMakeVisible(rhythmSection);
    addAndMakeVisible(modifiersSection);
    addAndMakeVisible(rhythmChoice);
    addAndMakeVisible(euclideanPulsesKnob);
    addAndMakeVisible(euclideanStepsKnob);
    addAndMakeVisible(euclideanRotationKnob);
    addAndMakeVisible(euclideanVisualizer);
    addAndMakeVisible(chanceKnob);
    addAndMakeVisible(ratchetChoice);
    addAndMakeVisible(ratchetChanceKnob);
    addAndMakeVisible(accentChoice);
    addAndMakeVisible(accentAmountKnob);

    if (isStandalone)
    {
        addAndMakeVisible(tempoSection);
        addAndMakeVisible(tempoKnob);
    }

    coolsynth::ui::applyGreenActionButtonStyle(closeButton, "patchButton");
    closeButton.onClick = [this] { requestClose(); };
    addAndMakeVisible(closeButton);

    auto attachChoiceControl = [](auto& control,
                                  std::unique_ptr<ChoiceAttachment>& attachment,
                                  juce::RangedAudioParameter* parameter)
    {
        control.onSelectionChanged = [&attachment](int value)
        {
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture(static_cast<float>(value));
        };

        if (parameter == nullptr)
            return;

        attachment = std::make_unique<ChoiceAttachment>(*parameter,
                                                        [&control](float value)
                                                        {
                                                            control.setSelectedValue(static_cast<int>(std::lround(value)));
                                                        });
        attachment->sendInitialUpdate();
    };

    tempoAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpInternalTempoBpm, tempoKnob.slider());
    chanceAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpChance, chanceKnob.slider());
    ratchetChanceAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpRatchetChance, ratchetChanceKnob.slider());
    accentAmountAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpAccentAmount, accentAmountKnob.slider());
    euclideanPulsesAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanPulses, euclideanPulsesKnob.slider());
    euclideanStepsAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanSteps, euclideanStepsKnob.slider());
    euclideanRotationAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanRotation, euclideanRotationKnob.slider());

    attachChoiceControl(rhythmChoice, rhythmAttachment, apvts.getParameter(ids::arpRhythm));
    attachChoiceControl(ratchetChoice, ratchetAttachment, apvts.getParameter(ids::arpRatchetCount));
    attachChoiceControl(accentChoice, accentAttachment, apvts.getParameter(ids::arpAccentEvery));

    refreshFromParameters();
}

void ArpAdvancedOverlay::paint(juce::Graphics& g)
{
    g.setColour(coolsynth::ui::palette::panelBlack.withAlpha(0.72f));
    g.fillRect(getLocalBounds());

    auto panelBounds = getLocalBounds().reduced(Layout::outerMargin).toFloat();
    panelBounds.setWidth(static_cast<float>(juce::jmin(Layout::panelMaxWidth, getWidth() - (Layout::outerMargin * 2))));
    panelBounds.setHeight(static_cast<float>(isStandalone ? Layout::panelStandaloneHeight
                                                          : Layout::panelPluginHeight));
    panelBounds.setCentre(getLocalBounds().toFloat().getCentre());

    juce::ColourGradient gradient(coolsynth::ui::palette::panelRaisedAlt,
                                  panelBounds.getX(),
                                  panelBounds.getY(),
                                  coolsynth::ui::palette::panelRaised,
                                  panelBounds.getX(),
                                  panelBounds.getBottom(),
                                  false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(panelBounds, 16.0f);

    g.setColour(coolsynth::ui::palette::panelStroke);
    g.drawRoundedRectangle(panelBounds, 16.0f, 1.0f);

    auto titleBounds = panelBounds.toNearestInt().reduced(Layout::panelInset)
                                      .removeFromTop(Layout::panelHeaderHeight);
    g.setColour(coolsynth::ui::palette::ledTextOff);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("ARP - ADVANCED", titleBounds.removeFromLeft(280), juce::Justification::centredLeft);
}

void ArpAdvancedOverlay::resized()
{
    auto panelBounds = getLocalBounds().reduced(Layout::outerMargin);
    panelBounds.setWidth(juce::jmin(Layout::panelMaxWidth, getWidth() - (Layout::outerMargin * 2)));
    panelBounds.setHeight(isStandalone ? Layout::panelStandaloneHeight
                                       : Layout::panelPluginHeight);
    panelBounds.setCentre(getLocalBounds().getCentre());

    auto content = panelBounds.reduced(Layout::panelInset);
    auto header = content.removeFromTop(Layout::panelHeaderHeight);
    closeButton.setBounds(header.removeFromRight(Layout::closeButtonWidth)
                               .withSizeKeepingCentre(Layout::closeButtonWidth,
                                                       Layout::closeButtonHeight));

    content.removeFromTop(Layout::sectionGap);

    const int rhythmHeight = euclideanControlsVisible ? 148 : 76;
    rhythmSection.setBounds(content.removeFromTop(rhythmHeight));
    content.removeFromTop(Layout::sectionGap);

    modifiersSection.setBounds(content.removeFromTop(104));

    if (isStandalone)
    {
        content.removeFromTop(Layout::sectionGap);
        tempoSection.setBounds(content.removeFromTop(78));
        auto tempoContent = sectionContentBounds(tempoSection);
        tempoKnob.setBounds(tempoContent.removeFromLeft(104));
    }

    auto rhythmContent = sectionContentBounds(rhythmSection);
    rhythmChoice.setBounds(rhythmContent.removeFromTop(40).removeFromLeft(260));

    if (euclideanControlsVisible)
    {
        rhythmContent.removeFromTop(8);
        auto euclideanControlsRow = rhythmContent.removeFromTop(60);
        euclideanPulsesKnob.setBounds(euclideanControlsRow.removeFromLeft(104));
        euclideanControlsRow.removeFromLeft(10);
        euclideanStepsKnob.setBounds(euclideanControlsRow.removeFromLeft(104));
        euclideanControlsRow.removeFromLeft(10);
        euclideanRotationKnob.setBounds(euclideanControlsRow.removeFromLeft(104));
        rhythmContent.removeFromTop(8);
        euclideanVisualizer.setBounds(rhythmContent);
    }

    auto modifiersContent = sectionContentBounds(modifiersSection);
    const float totalWeight = 5.7f;
    auto takeWeightedWidth = [](juce::Rectangle<int>& row, float weight, float& remainingWeight)
    {
        const auto width = juce::roundToInt(static_cast<float>(row.getWidth()) * (weight / remainingWeight));
        remainingWeight -= weight;
        return row.removeFromLeft(width);
    };

    float remainingWeight = totalWeight;
    chanceKnob.setBounds(takeWeightedWidth(modifiersContent, 1.0f, remainingWeight));
    modifiersContent.removeFromLeft(8);
    ratchetChoice.setBounds(takeWeightedWidth(modifiersContent, 1.35f, remainingWeight));
    modifiersContent.removeFromLeft(8);
    ratchetChanceKnob.setBounds(takeWeightedWidth(modifiersContent, 1.0f, remainingWeight));
    modifiersContent.removeFromLeft(8);
    accentChoice.setBounds(takeWeightedWidth(modifiersContent, 1.35f, remainingWeight));
    modifiersContent.removeFromLeft(8);
    accentAmountKnob.setBounds(modifiersContent);
}

bool ArpAdvancedOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        requestClose();
        return true;
    }

    return false;
}

void ArpAdvancedOverlay::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void ArpAdvancedOverlay::visibilityChanged()
{
    if (isVisible())
    {
        toFront(true);
        grabKeyboardFocus();
    }
}

void ArpAdvancedOverlay::refreshFromParameters()
{
    namespace ids = coolsynth::parameters::ids;

    rhythmChoice.setValueText(getCurrentParameterText(ids::arpRhythm));
    chanceKnob.setValueText(getCurrentParameterText(ids::arpChance));
    ratchetChoice.setValueText(getCurrentParameterText(ids::arpRatchetCount));
    ratchetChanceKnob.setValueText(getCurrentParameterText(ids::arpRatchetChance));
    accentChoice.setValueText(getCurrentParameterText(ids::arpAccentEvery));
    accentAmountKnob.setValueText(getCurrentParameterText(ids::arpAccentAmount));
    euclideanPulsesKnob.setValueText(getCurrentParameterText(ids::arpEuclideanPulses));
    euclideanStepsKnob.setValueText(getCurrentParameterText(ids::arpEuclideanSteps));
    euclideanRotationKnob.setValueText(getCurrentParameterText(ids::arpEuclideanRotation));

    if (isStandalone)
        tempoKnob.setValueText(getCurrentParameterText(ids::arpInternalTempoBpm));

    refreshEuclideanVisibility();
    refreshEuclideanVisualizer();
}

void ArpAdvancedOverlay::setCloseCallback(std::function<void()> callback)
{
    onRequestClose = std::move(callback);
}

bool ArpAdvancedOverlay::areEuclideanControlsVisibleForTesting() const noexcept
{
    return euclideanPulsesKnob.isVisible()
        && euclideanStepsKnob.isVisible()
        && euclideanRotationKnob.isVisible()
        && euclideanVisualizer.isVisible();
}

void ArpAdvancedOverlay::refreshEuclideanVisualizer()
{
    namespace ids = coolsynth::parameters::ids;

    auto* pulsesParameter = apvts.getParameter(ids::arpEuclideanPulses);
    auto* stepsParameter = apvts.getParameter(ids::arpEuclideanSteps);
    auto* rotationParameter = apvts.getParameter(ids::arpEuclideanRotation);

    if (pulsesParameter == nullptr || stepsParameter == nullptr || rotationParameter == nullptr)
        return;

    const int pulses = static_cast<int>(std::lround(pulsesParameter->convertFrom0to1(pulsesParameter->getValue())));
    const int steps = static_cast<int>(std::lround(stepsParameter->convertFrom0to1(stepsParameter->getValue())));
    const int rotation = static_cast<int>(std::lround(rotationParameter->convertFrom0to1(rotationParameter->getValue())));

    euclideanVisualizer.setCycle(coolsynth::synth::makeEuclideanCycle(pulses, steps, rotation), steps);
}

void ArpAdvancedOverlay::refreshEuclideanVisibility()
{
    namespace ids = coolsynth::parameters::ids;

    const auto* rhythmParameter = apvts.getParameter(ids::arpRhythm);
    const bool shouldShowEuclidean =
        rhythmParameter != nullptr
            && static_cast<int>(std::lround(rhythmParameter->convertFrom0to1(rhythmParameter->getValue())))
                   == static_cast<int>(coolsynth::parameters::ArpRhythmChoice::euclidean);

    euclideanPulsesKnob.setVisible(shouldShowEuclidean);
    euclideanStepsKnob.setVisible(shouldShowEuclidean);
    euclideanRotationKnob.setVisible(shouldShowEuclidean);
    euclideanVisualizer.setVisible(shouldShowEuclidean);

    if (euclideanControlsVisible != shouldShowEuclidean)
    {
        euclideanControlsVisible = shouldShowEuclidean;
        resized();
    }
}

void ArpAdvancedOverlay::requestClose()
{
    if (onRequestClose != nullptr)
        onRequestClose();
}

juce::String ArpAdvancedOverlay::getCurrentParameterText(const char* parameterId) const
{
    if (auto* parameter = apvts.getParameter(parameterId))
        return parameter->getCurrentValueAsText();

    return "-";
}
