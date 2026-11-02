#include "ArpAdvancedOverlay.h"

#include <cmath>

#include "parameters/ParameterIDs.h"
#include "synth/EuclideanRhythm.h"
#include "ui/ActionButtonLookAndFeel.h"
#include "ui/ChoiceParameterAttachment.h"
#include "ui/UiPalette.h"
#include "ui/WeightedRowDistributor.h"

namespace
{
    namespace Layout
    {
        inline constexpr int outerMargin = 64;
        inline constexpr int panelMaxWidth = 860;
        inline constexpr int panelInset = 18;
        inline constexpr int panelHeaderHeight = 36;
        inline constexpr int sectionGap = 10;
        inline constexpr int sectionContentInset = 12;
        inline constexpr int sectionTitleHeight = 24;
        inline constexpr int closeButtonWidth = 96;
        inline constexpr int closeButtonHeight = 24;

        // Section heights are derived from what the controls need: a HardwareKnob
        // is 20 (title) + 56 (knob) + 20 (value) px, and a one-row
        // SegmentedChoiceGroup fits comfortably in the same height.
        inline constexpr int knobRowHeight = 96;
        inline constexpr int knobWidth = 104;
        inline constexpr int knobGap = 10;
        inline constexpr int rhythmChoiceWidth = 260;
        inline constexpr int euclideanGap = 8;
        inline constexpr int euclideanVisualizerHeight = 44;
        inline constexpr int sectionChromeHeight = sectionContentInset * 2 + sectionTitleHeight;
        inline constexpr int rhythmSectionStraightHeight = sectionChromeHeight + knobRowHeight;
        inline constexpr int rhythmSectionEuclideanHeight = rhythmSectionStraightHeight
                                                          + euclideanGap + euclideanVisualizerHeight;
        inline constexpr int modifiersSectionHeight = sectionChromeHeight + knobRowHeight;
        inline constexpr int tempoSectionHeight = sectionChromeHeight + knobRowHeight;
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

    tempoAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpInternalTempoBpm, tempoKnob.slider());
    chanceAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpChance, chanceKnob.slider());
    ratchetChanceAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpRatchetChance, ratchetChanceKnob.slider());
    accentAmountAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpAccentAmount, accentAmountKnob.slider());
    euclideanPulsesAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanPulses, euclideanPulsesKnob.slider());
    euclideanStepsAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanSteps, euclideanStepsKnob.slider());
    euclideanRotationAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEuclideanRotation, euclideanRotationKnob.slider());

    coolsynth::ui::attachChoiceControl(rhythmChoice, rhythmAttachment, apvts.getParameter(ids::arpRhythm));
    coolsynth::ui::attachChoiceControl(ratchetChoice, ratchetAttachment, apvts.getParameter(ids::arpRatchetCount));
    coolsynth::ui::attachChoiceControl(accentChoice, accentAttachment, apvts.getParameter(ids::arpAccentEvery));

    refreshFromParameters();
}

void ArpAdvancedOverlay::paint(juce::Graphics& g)
{
    g.setColour(coolsynth::ui::palette::panelBlack.withAlpha(0.72f));
    g.fillRect(getLocalBounds());

    auto panelBounds = getPanelBounds().toFloat();

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

int ArpAdvancedOverlay::getPanelHeight() const noexcept
{
    int height = Layout::panelInset * 2
               + Layout::panelHeaderHeight
               + Layout::sectionGap
               + (euclideanControlsVisible ? Layout::rhythmSectionEuclideanHeight
                                           : Layout::rhythmSectionStraightHeight)
               + Layout::sectionGap
               + Layout::modifiersSectionHeight;

    if (isStandalone)
        height += Layout::sectionGap + Layout::tempoSectionHeight;

    return height;
}

juce::Rectangle<int> ArpAdvancedOverlay::getPanelBounds() const noexcept
{
    auto panelBounds = getLocalBounds().reduced(Layout::outerMargin);
    panelBounds.setWidth(juce::jmin(Layout::panelMaxWidth, getWidth() - (Layout::outerMargin * 2)));
    panelBounds.setHeight(getPanelHeight());
    panelBounds.setCentre(getLocalBounds().getCentre());
    return panelBounds;
}

void ArpAdvancedOverlay::resized()
{
    auto content = getPanelBounds().reduced(Layout::panelInset);
    auto header = content.removeFromTop(Layout::panelHeaderHeight);
    closeButton.setBounds(header.removeFromRight(Layout::closeButtonWidth)
                               .withSizeKeepingCentre(Layout::closeButtonWidth,
                                                       Layout::closeButtonHeight));

    content.removeFromTop(Layout::sectionGap);

    rhythmSection.setBounds(content.removeFromTop(euclideanControlsVisible ? Layout::rhythmSectionEuclideanHeight
                                                                           : Layout::rhythmSectionStraightHeight));
    content.removeFromTop(Layout::sectionGap);

    modifiersSection.setBounds(content.removeFromTop(Layout::modifiersSectionHeight));

    if (isStandalone)
    {
        content.removeFromTop(Layout::sectionGap);
        tempoSection.setBounds(content.removeFromTop(Layout::tempoSectionHeight));
        auto tempoContent = sectionContentBounds(tempoSection);
        tempoKnob.setBounds(tempoContent.removeFromLeft(Layout::knobWidth));
    }

    // Rhythm row: the Straight/Euclidean selector on the left, the three Euclidean
    // knobs beside it when that mode is active, and the cycle preview underneath.
    auto rhythmContent = sectionContentBounds(rhythmSection);
    auto rhythmRow = rhythmContent.removeFromTop(Layout::knobRowHeight);
    rhythmChoice.setBounds(rhythmRow.removeFromLeft(Layout::rhythmChoiceWidth));

    if (euclideanControlsVisible)
    {
        rhythmRow.removeFromLeft(Layout::knobGap);
        euclideanPulsesKnob.setBounds(rhythmRow.removeFromLeft(Layout::knobWidth));
        rhythmRow.removeFromLeft(Layout::knobGap);
        euclideanStepsKnob.setBounds(rhythmRow.removeFromLeft(Layout::knobWidth));
        rhythmRow.removeFromLeft(Layout::knobGap);
        euclideanRotationKnob.setBounds(rhythmRow.removeFromLeft(Layout::knobWidth));
        rhythmContent.removeFromTop(Layout::euclideanGap);
        euclideanVisualizer.setBounds(rhythmContent.removeFromTop(Layout::euclideanVisualizerHeight));
    }

    auto modifiersContent = sectionContentBounds(modifiersSection);
    coolsynth::ui::WeightedRowDistributor modifiers { modifiersContent, 5.7f, 5, 8 };
    chanceKnob.setBounds(modifiers.take(1.0f));
    ratchetChoice.setBounds(modifiers.take(1.35f));
    ratchetChanceKnob.setBounds(modifiers.take(1.0f));
    accentChoice.setBounds(modifiers.take(1.35f));
    accentAmountKnob.setBounds(modifiers.take(1.0f));
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
