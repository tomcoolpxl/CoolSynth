#include "SynthAudioProcessorEditor.h"

#include <array>
#include <cmath>

#include <BinaryData.h>

#include "ArpAdvancedOverlay.h"
#include "BuildInfo.h"
#include "SynthAudioProcessor.h"
#include "midi/ControllerProfile.h"
#include "parameters/ParameterIDs.h"
#include "presets/FactoryPresets.h"
#include "presets/PatchState.h"
#include "standalone/SettingsStore.h"
#include "standalone/StandaloneAudioSupport.h"
#include "ui/ActionButtonLookAndFeel.h"
#include "ui/StandaloneSettingsDialog.h"
#include "ui/StandaloneStatusBar.h"
#include "ui/UiPalette.h"

namespace
{
    // ----- Editor layout metrics ------------------------------------------------
    // Vertical structure of the main editor area. The window size is derived from
    // these values so that adjusting a single row height keeps everything in sync
    // and no row is silently clipped off-screen.
    namespace Layout
    {
        // Outer chrome.
        inline constexpr int outerMargin             = 24;
        inline constexpr int standaloneStatusBarH    = 28;
        inline constexpr int pluginFooterHeight      = 30;
        inline constexpr int defaultWindowWidth      = 1550;

        // Vertical row structure of the inner content area.
        inline constexpr int titleHeight             = 48;
        inline constexpr int spacerTitleToDeck1      = 16;
        inline constexpr int deck1Height             = 240;
        inline constexpr int spacerDeck1ToDeck2      = 10;
        inline constexpr int deck2Height             = 140;
        inline constexpr int spacerDeck2ToBottom     = 16;
        inline constexpr int bottomRowHeight         = coolsynth::ui::PianoBarComponent::desiredHeight;

        // Generic horizontal gap between adjacent sections (within a deck,
        // and between the piano bar and the bottom FX cluster).
        inline constexpr int interSectionGap         = 10;

        // Section frame metrics (padding inside a SynthSection panel).
        inline constexpr int sectionContentInset     = 12;
        inline constexpr int sectionTitleBarHeight   = 24;
        inline constexpr int sectionHeaderStripH     = 32;
        inline constexpr int sectionToggleSize       = 24;

        // Title-row sub-widths.
        inline constexpr int logoAreaWidth           = 248;
        inline constexpr int visualizerLeftGap       = 40;
        inline constexpr int visualizerWidth         = 372;
        inline constexpr int presetSelectorWidth     = 180;
        inline constexpr int presetSelectorHeight    = 26;
        inline constexpr int titleControlGap         = 8;
        inline constexpr int titleClusterButtonH     = 24;
        inline constexpr int titleClusterPatchW      = 112;
        inline constexpr int titleClusterGap         = 4;

        // Bottom-row FX cluster column widths and gap.
        inline constexpr int bottomRowGap            = 8;
        inline constexpr int bottomRowMacrosWidth    = 130;
        inline constexpr int bottomRowPhaserWidth    = 175;
        inline constexpr int bottomRowCompressorW    = 175;

        // Deck section weights — relative widths used by the WeightedRowDistributor.
        // These currently match the previous pixel widths, which makes the migration
        // a no-op at the default window size; they will scale continuously once the
        // window becomes user-resizable.
        namespace Deck1
        {
            inline constexpr float osc   = 275.0f;
            inline constexpr float mix   = 110.0f;
            inline constexpr float flt   = 120.0f;
            inline constexpr float env   = 220.0f;
            inline constexpr float lfo   = 165.0f;
            inline constexpr float pmod  = 180.0f;
            inline constexpr float perf  = 220.0f;
            inline constexpr int   count = 7;
            inline constexpr float total = osc + mix + flt + env + lfo + pmod + perf;
        }

        namespace Deck2
        {
            inline constexpr float arp   = 385.0f;
            inline constexpr float drv   = 165.0f;
            inline constexpr float cho   = 220.0f;
            inline constexpr float dly   = 220.0f;
            inline constexpr float rev   = 220.0f;
            inline constexpr float out   =  92.0f;
            inline constexpr int   count = 6;
            inline constexpr float total = arp + drv + cho + dly + rev + out;
        }

        inline constexpr int sumContentHeight() noexcept
        {
            return titleHeight + spacerTitleToDeck1
                 + deck1Height + spacerDeck1ToDeck2
                 + deck2Height + spacerDeck2ToBottom
                 + bottomRowHeight;
        }

        inline constexpr int standaloneWindowHeight() noexcept
        {
            return sumContentHeight() + outerMargin * 2 + standaloneStatusBarH;
        }

        inline constexpr int pluginWindowHeight() noexcept
        {
            return sumContentHeight() + outerMargin * 2 + pluginFooterHeight;
        }
    }

    // Distributes a row's width among N weighted sections separated by fixed gaps.
    // The last section absorbs rounding residuals, so the produced rectangles always
    // sum to exactly the row width — no leftover slack on the right edge.
    //
    // This is the primitive that lets the layout scale continuously: change the row
    // width and every section grows or shrinks in proportion.
    struct WeightedRowDistributor
    {
        WeightedRowDistributor(juce::Rectangle<int>& rowRef,
                               float totalWeight,
                               int sectionCount,
                               int gapPx) noexcept
            : row(rowRef)
            , remainingWidth(juce::jmax(0, rowRef.getWidth() - juce::jmax(0, sectionCount - 1) * gapPx))
            , remainingWeight(totalWeight)
            , gap(gapPx)
            , sectionsLeft(sectionCount)
        {
        }

        juce::Rectangle<int> take(float weight) noexcept
        {
            const int width = (sectionsLeft <= 1 || remainingWeight <= 0.0f)
                ? remainingWidth
                : juce::roundToInt(static_cast<float>(remainingWidth) * (weight / remainingWeight));

            remainingWidth -= width;
            remainingWeight -= weight;
            auto rect = row.removeFromLeft(width);
            if (--sectionsLeft > 0)
                row.removeFromLeft(gap);
            return rect;
        }

    private:
        juce::Rectangle<int>& row;
        int   remainingWidth;
        float remainingWeight;
        int   gap;
        int   sectionsLeft;
    };

    class EditorTooltipWindow final : public juce::TooltipWindow
    {
    public:
        using juce::TooltipWindow::TooltipWindow;

        std::function<bool()> isEnabledProvider;

        juce::String getTipFor(juce::Component& component) override
        {
            if (isEnabledProvider != nullptr && ! isEnabledProvider())
                return {};

            for (auto* current = &component; current != nullptr; current = current->getParentComponent())
            {
                if (auto tip = juce::TooltipWindow::getTipFor(*current); tip.isNotEmpty())
                    return tip;
            }

            return {};
        }
    };

    class EditorTooltipLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                              juce::Point<int> screenPos,
                                              juce::Rectangle<int> parentArea) override
        {
            auto bounds = juce::LookAndFeel_V2::getTooltipBounds(tipText, screenPos, parentArea);
            bounds.setHeight(bounds.getHeight() + 10);
            return bounds;
        }

        void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
        {
            auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
            g.setColour(coolsynth::ui::palette::tooltipBackground);
            g.fillRoundedRectangle(bounds, 6.0f);

            g.setColour(coolsynth::ui::palette::tooltipBorder);
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

            g.setColour(coolsynth::ui::palette::textPrimary);
            g.setFont(juce::FontOptions(13.0f, juce::Font::plain));
            g.drawFittedText(text,
                             juce::Rectangle<int>(width, height).reduced(8, 6).withTrimmedBottom(10),
                             juce::Justification::topLeft,
                             8);
        }
    };
}

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
    , visualizer(processor.getValueTreeState(), processor.getScopeFifo())
    , pianoBar(processor.getKeyboardState())
{
    const bool isStandalone = juce::JUCEApplicationBase::isStandaloneApp();
    setupParameterRefs();
    setupVisualsAndLabels(isStandalone);
    setupControlAttachments();
    registerLearnableControls();
    setupActionButtons();
    setupPresetSelector();
    setupStandaloneMode(isStandalone);
    setupTooltipWindow();
    applyTooltips();
    startTimerHz(24);
    refreshMidiLearnVisuals();
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::setupParameterRefs()
{
    namespace ids = coolsynth::parameters::ids;
    auto& apvts = processor.getValueTreeState();

    parameterRefs.oscAWave = apvts.getParameter(ids::oscAWave);
    parameterRefs.oscAOctave = apvts.getParameter(ids::oscAOctave);
    parameterRefs.oscAFine = apvts.getParameter(ids::oscAFineCents);
    parameterRefs.oscAPw = apvts.getParameter(ids::oscAPulseWidth);
    parameterRefs.oscASync = apvts.getParameter(ids::oscASyncEnabled);
    parameterRefs.oscBWave = apvts.getParameter(ids::oscBWave);
    parameterRefs.oscBOctave = apvts.getParameter(ids::oscBOctave);
    parameterRefs.oscBFine = apvts.getParameter(ids::oscBFineCents);
    parameterRefs.oscBPw = apvts.getParameter(ids::oscBPulseWidth);
    parameterRefs.oscBLoFreq = apvts.getParameter(ids::oscBLowFrequencyMode);
    parameterRefs.mixOscA = apvts.getParameter(ids::oscALevel);
    parameterRefs.mixOscB = apvts.getParameter(ids::oscBLevel);
    parameterRefs.mixNoise = apvts.getParameter(ids::noiseLevel);
    parameterRefs.fltCutoff = apvts.getParameter(ids::filterCutoffHz);
    parameterRefs.fltRes = apvts.getParameter(ids::filterResonance);
    parameterRefs.fltEnvAmt = apvts.getParameter(ids::filterEnvAmount);
    parameterRefs.fltKeyTrk = apvts.getParameter(ids::filterKeyTracking);
    parameterRefs.fEnvA = apvts.getParameter(ids::filterAttackMs);
    parameterRefs.fEnvD = apvts.getParameter(ids::filterDecayMs);
    parameterRefs.fEnvS = apvts.getParameter(ids::filterSustain);
    parameterRefs.fEnvR = apvts.getParameter(ids::filterReleaseMs);
    parameterRefs.aEnvA = apvts.getParameter(ids::ampAttackMs);
    parameterRefs.aEnvD = apvts.getParameter(ids::ampDecayMs);
    parameterRefs.aEnvS = apvts.getParameter(ids::ampSustain);
    parameterRefs.aEnvR = apvts.getParameter(ids::ampReleaseMs);
    parameterRefs.lfoRate = apvts.getParameter(ids::lfoRateHz);
    parameterRefs.lfoWave = apvts.getParameter(ids::lfoWave);
    parameterRefs.lfoMwDep = apvts.getParameter(ids::modWheelToLfoDepth);
    parameterRefs.lfoPitch = apvts.getParameter(ids::lfoToOscPitch);
    parameterRefs.lfoPw = apvts.getParameter(ids::lfoToPulseWidth);
    parameterRefs.lfoCutoff = apvts.getParameter(ids::lfoToFilterCutoff);
    parameterRefs.pmodBPitch = apvts.getParameter(ids::polyModOscBToOscPitch);
    parameterRefs.pmodBPw = apvts.getParameter(ids::polyModOscBToPulseWidth);
    parameterRefs.pmodBCutoff = apvts.getParameter(ids::polyModOscBToFilterCutoff);
    parameterRefs.pmodEPitch = apvts.getParameter(ids::polyModEnvToOscPitch);
    parameterRefs.pmodEPw = apvts.getParameter(ids::polyModEnvToPulseWidth);
    parameterRefs.pmodECutoff = apvts.getParameter(ids::polyModEnvToFilterCutoff);
    parameterRefs.perfGlide = apvts.getParameter(ids::glideTimeMs);
    parameterRefs.perfMode = apvts.getParameter(ids::playMode);
    parameterRefs.perfPrio = apvts.getParameter(ids::keyPriority);
    parameterRefs.perfPbRange = apvts.getParameter(ids::pitchBendRangeSemitones);
    parameterRefs.perfVintage = apvts.getParameter(ids::vintageAmount);
    parameterRefs.perfPan = apvts.getParameter(ids::panSpread);
    parameterRefs.perfVelAmp = apvts.getParameter(ids::velocityToAmp);
    parameterRefs.perfVelFlt = apvts.getParameter(ids::velocityToFilter);
    parameterRefs.arpOn = apvts.getParameter(ids::arpEnabled);
    parameterRefs.arpTempo = apvts.getParameter(ids::arpInternalTempoBpm);
    parameterRefs.arpRate = apvts.getParameter(ids::arpRateDivision);
    parameterRefs.arpPattern = apvts.getParameter(ids::arpPattern);
    parameterRefs.arpOctave = apvts.getParameter(ids::arpOctaveRange);
    parameterRefs.arpGate = apvts.getParameter(ids::arpGate);
    parameterRefs.arpSwing = apvts.getParameter(ids::arpSwing);
    parameterRefs.arpChance = apvts.getParameter(ids::arpChance);
    parameterRefs.arpRatchetCount = apvts.getParameter(ids::arpRatchetCount);
    parameterRefs.arpRatchetChance = apvts.getParameter(ids::arpRatchetChance);
    parameterRefs.arpAccentEvery = apvts.getParameter(ids::arpAccentEvery);
    parameterRefs.arpAccentAmount = apvts.getParameter(ids::arpAccentAmount);
    parameterRefs.arpRhythm = apvts.getParameter(ids::arpRhythm);
    parameterRefs.arpEuclideanPulses = apvts.getParameter(ids::arpEuclideanPulses);
    parameterRefs.arpEuclideanSteps = apvts.getParameter(ids::arpEuclideanSteps);
    parameterRefs.arpEuclideanRotation = apvts.getParameter(ids::arpEuclideanRotation);
    parameterRefs.arpLatch = apvts.getParameter(ids::arpLatch);
    parameterRefs.drvOn = apvts.getParameter(ids::driveEnabled);
    parameterRefs.drvAmt = apvts.getParameter(ids::driveAmount);
    parameterRefs.drvMix = apvts.getParameter(ids::driveMix);
    parameterRefs.choOn = apvts.getParameter(ids::chorusEnabled);
    parameterRefs.choRate = apvts.getParameter(ids::chorusRateHz);
    parameterRefs.choDep = apvts.getParameter(ids::chorusDepth);
    parameterRefs.choMix = apvts.getParameter(ids::chorusMix);
    parameterRefs.dlyOn = apvts.getParameter(ids::delayEnabled);
    parameterRefs.dlyTime = apvts.getParameter(ids::delayTimeMs);
    parameterRefs.dlyFdbk = apvts.getParameter(ids::delayFeedback);
    parameterRefs.dlyMix = apvts.getParameter(ids::delayMix);
    parameterRefs.revOn = apvts.getParameter(ids::reverbEnabled);
    parameterRefs.revSize = apvts.getParameter(ids::reverbSize);
    parameterRefs.revDamp = apvts.getParameter(ids::reverbDamping);
    parameterRefs.revMix = apvts.getParameter(ids::reverbMix);
    parameterRefs.outGain = apvts.getParameter(ids::masterGainDb);
    parameterRefs.timbre = apvts.getParameter(ids::timbre);
    parameterRefs.excite = apvts.getParameter(ids::excite);
    parameterRefs.phsOn = apvts.getParameter(ids::phaserEnabled);
    parameterRefs.phsRate = apvts.getParameter(ids::phaserRateHz);
    parameterRefs.phsDepth = apvts.getParameter(ids::phaserDepth);
    parameterRefs.cmpOn = apvts.getParameter(ids::compressorEnabled);
    parameterRefs.cmpAmt = apvts.getParameter(ids::compressorAmount);
    parameterRefs.cmpMix = apvts.getParameter(ids::compressorMix);
}

void SynthAudioProcessorEditor::setupVisualsAndLabels(bool isStandalone)
{
    titleLogoDrawable = juce::Drawable::createFromImageData(BinaryData::coolsynthlogo2_png,
                                                            BinaryData::coolsynthlogo2_pngSize);
    if (titleLogoDrawable != nullptr)
        addAndMakeVisible(*titleLogoDrawable);

    addAndMakeVisible(visualizer);

    midiLearnStatusLabel.setText("", juce::dontSendNotification);
    midiLearnStatusLabel.setFont(juce::FontOptions("Arial", 14.0f, juce::Font::bold));
    midiLearnStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::ledTextOff);
    midiLearnStatusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(midiLearnStatusLabel);

    pluginStatusLabel.setText("Plugin Build", juce::dontSendNotification);
    pluginStatusLabel.setFont(juce::FontOptions(12.0f));
    pluginStatusLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);
    pluginStatusLabel.setJustificationType(juce::Justification::centredLeft);
    pluginStatusLabel.setVisible(!isStandalone);
    addAndMakeVisible(pluginStatusLabel);

    buildInfoLabel.setText(coolsynth::build::getBuildIdentity(), juce::dontSendNotification);
    buildInfoLabel.setFont(juce::FontOptions(13.0f));
    buildInfoLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textPrimary);
    buildInfoLabel.setJustificationType(juce::Justification::centredRight);
    buildInfoLabel.setVisible(!isStandalone);
    addAndMakeVisible(buildInfoLabel);

    arpPatternLabel.setText("Pattern", juce::dontSendNotification);
    arpPatternLabel.setFont(juce::FontOptions(15.0f));
    arpPatternLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textPrimary);
    arpPatternLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(arpPatternLabel);

    coolsynth::ui::applyGreenComboBoxStyle(arpPatternChoice);
    arpPatternChoice.setJustificationType(juce::Justification::centredLeft);
    arpPatternChoice.setScrollWheelEnabled(false);
    arpPatternChoice.addItem("Up", 1);
    arpPatternChoice.addItem("Down", 2);
    arpPatternChoice.addItem("Up/Down", 3);
    arpPatternChoice.addItem("As Played", 4);
    arpPatternChoice.addItem("Converge", 5);
    arpPatternChoice.addItem("Diverge", 6);
    arpPatternChoice.addItem("Inside", 7);
    arpPatternChoice.addItem("Outside", 8);
    arpPatternChoice.addItem("Random", 9);
    arpPatternChoice.addItem("Random Walk", 10);
    arpPatternChoice.addItem("Chord", 11);

    arpAdvancedSummaryLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    arpAdvancedSummaryLabel.setColour(juce::Label::textColourId, coolsynth::ui::palette::textSecondary);
    arpAdvancedSummaryLabel.setJustificationType(juce::Justification::centredLeft);
    arpAdvancedSummaryLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(arpAdvancedSummaryLabel);

    arpAdvancedOverlay = std::make_unique<ArpAdvancedOverlay>(processor.getValueTreeState(),
                                                              isStandalone);
    arpAdvancedOverlay->setCloseCallback([this] { setArpAdvancedOverlayVisible(false); });
    addChildComponent(*arpAdvancedOverlay);
}

void SynthAudioProcessorEditor::setupControlAttachments()
{
    namespace ids = coolsynth::parameters::ids;
    auto& apvts = processor.getValueTreeState();

    addAndMakeVisible(oscSection);
    addAndMakeVisible(mixSection);
    addAndMakeVisible(fltSection);
    addAndMakeVisible(envSection);
    addAndMakeVisible(lfoSection);
    addAndMakeVisible(pmodSection);
    addAndMakeVisible(perfSection);
    addAndMakeVisible(arpSection);
    addAndMakeVisible(drvSection);
    addAndMakeVisible(choSection);
    addAndMakeVisible(dlySection);
    addAndMakeVisible(revSection);
    addAndMakeVisible(outSection);
    addAndMakeVisible(macrosSection);
    addAndMakeVisible(phaserSection);
    addAndMakeVisible(compressorSection);

    auto addSliderControl = [this, &apvts](auto& control,
                                           std::unique_ptr<SliderAttachment>& attachment,
                                           const char* parameterId)
    {
        addAndMakeVisible(control);
        attachment = std::make_unique<SliderAttachment>(apvts, parameterId, control.slider());
    };

    auto addToggleControl = [this, &apvts](auto& control,
                                           std::unique_ptr<ButtonAttachment>& attachment,
                                           const char* parameterId)
    {
        addAndMakeVisible(control);
        attachment = std::make_unique<ButtonAttachment>(apvts, parameterId, control.button());
    };

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

    auto attachComboChoiceControl = [](juce::ComboBox& control,
                                       std::unique_ptr<ChoiceAttachment>& attachment,
                                       juce::RangedAudioParameter* parameter)
    {
        control.onChange = [&attachment, &control]
        {
            if (attachment != nullptr && control.getSelectedId() > 0)
                attachment->setValueAsCompleteGesture(
                    static_cast<float>(control.getSelectedId() - 1));
        };

        if (parameter == nullptr)
            return;

        attachment = std::make_unique<ChoiceAttachment>(*parameter,
                                                        [&control](float value)
                                                        {
                                                            control.setSelectedId(
                                                                static_cast<int>(std::lround(value)) + 1,
                                                                juce::dontSendNotification);
                                                        });
        attachment->sendInitialUpdate();
    };

    addAndMakeVisible(oscAWaveChoice);
    attachChoiceControl(oscAWaveChoice, oscAWaveAttachment, parameterRefs.oscAWave);
    addSliderControl(oscAOctaveKnob, oscAOctaveAttachment, ids::oscAOctave);
    addSliderControl(oscAFineKnob, oscAFineAttachment, ids::oscAFineCents);
    addSliderControl(oscAPwKnob, oscAPwAttachment, ids::oscAPulseWidth);
    addToggleControl(oscASyncToggle, oscASyncAttachment, ids::oscASyncEnabled);
    addAndMakeVisible(oscBWaveChoice);
    attachChoiceControl(oscBWaveChoice, oscBWaveAttachment, parameterRefs.oscBWave);
    addSliderControl(oscBOctaveKnob, oscBOctaveAttachment, ids::oscBOctave);
    addSliderControl(oscBFineKnob, oscBFineAttachment, ids::oscBFineCents);
    addSliderControl(oscBPwKnob, oscBPwAttachment, ids::oscBPulseWidth);
    addToggleControl(oscBLoFreqToggle, oscBLoFreqAttachment, ids::oscBLowFrequencyMode);
    addSliderControl(mixOscAKnob, mixOscAAttachment, ids::oscALevel);
    addSliderControl(mixOscBKnob, mixOscBAttachment, ids::oscBLevel);
    addSliderControl(mixNoiseKnob, mixNoiseAttachment, ids::noiseLevel);
    addSliderControl(fltCutoffKnob, fltCutoffAttachment, ids::filterCutoffHz);
    addSliderControl(fltResKnob, fltResAttachment, ids::filterResonance);
    addSliderControl(fltEnvAmtKnob, fltEnvAmtAttachment, ids::filterEnvAmount);
    addAndMakeVisible(fltKeyTrkChoice);
    attachChoiceControl(fltKeyTrkChoice, fltKeyTrkAttachment, parameterRefs.fltKeyTrk);
    addSliderControl(fEnvAKnob, fEnvAAttachment, ids::filterAttackMs);
    addSliderControl(fEnvDKnob, fEnvDAttachment, ids::filterDecayMs);
    addSliderControl(fEnvSKnob, fEnvSAttachment, ids::filterSustain);
    addSliderControl(fEnvRKnob, fEnvRAttachment, ids::filterReleaseMs);
    addSliderControl(aEnvAKnob, aEnvAAttachment, ids::ampAttackMs);
    addSliderControl(aEnvDKnob, aEnvDAttachment, ids::ampDecayMs);
    addSliderControl(aEnvSKnob, aEnvSAttachment, ids::ampSustain);
    addSliderControl(aEnvRKnob, aEnvRAttachment, ids::ampReleaseMs);
    addSliderControl(lfoRateKnob, lfoRateAttachment, ids::lfoRateHz);
    addAndMakeVisible(lfoWaveChoice);
    attachChoiceControl(lfoWaveChoice, lfoWaveAttachment, parameterRefs.lfoWave);
    addSliderControl(lfoMwDepKnob, lfoMwDepAttachment, ids::modWheelToLfoDepth);
    addSliderControl(lfoPitchKnob, lfoPitchAttachment, ids::lfoToOscPitch);
    addSliderControl(lfoPwKnob, lfoPwAttachment, ids::lfoToPulseWidth);
    addSliderControl(lfoCutoffKnob, lfoCutoffAttachment, ids::lfoToFilterCutoff);
    addSliderControl(pmodBPitchKnob, pmodBPitchAttachment, ids::polyModOscBToOscPitch);
    addSliderControl(pmodBPwKnob, pmodBPwAttachment, ids::polyModOscBToPulseWidth);
    addSliderControl(pmodBCutoffKnob, pmodBCutoffAttachment, ids::polyModOscBToFilterCutoff);
    addSliderControl(pmodEPitchKnob, pmodEPitchAttachment, ids::polyModEnvToOscPitch);
    addSliderControl(pmodEPwKnob, pmodEPwAttachment, ids::polyModEnvToPulseWidth);
    addSliderControl(pmodECutoffKnob, pmodECutoffAttachment, ids::polyModEnvToFilterCutoff);
    addSliderControl(perfGlideKnob, perfGlideAttachment, ids::glideTimeMs);
    addAndMakeVisible(perfModeChoice);
    attachChoiceControl(perfModeChoice, perfModeAttachment, parameterRefs.perfMode);
    addAndMakeVisible(perfPrioChoice);
    attachChoiceControl(perfPrioChoice, perfPrioAttachment, parameterRefs.perfPrio);
    addSliderControl(perfPbRangeKnob, perfPbRangeAttachment, ids::pitchBendRangeSemitones);
    addSliderControl(perfVintageKnob, perfVintageAttachment, ids::vintageAmount);
    addSliderControl(perfPanKnob, perfPanAttachment, ids::panSpread);
    addSliderControl(perfVelAmpKnob, perfVelAmpAttachment, ids::velocityToAmp);
    addSliderControl(perfVelFltKnob, perfVelFltAttachment, ids::velocityToFilter);
    addToggleControl(arpOnToggle, arpOnAttachment, ids::arpEnabled);
    addAndMakeVisible(arpRateChoice);
    attachChoiceControl(arpRateChoice, arpRateAttachment, parameterRefs.arpRate);
    addAndMakeVisible(arpPatternChoice);
    attachComboChoiceControl(arpPatternChoice, arpPatternAttachment, parameterRefs.arpPattern);
    addAndMakeVisible(arpOctaveChoice);
    attachChoiceControl(arpOctaveChoice, arpOctaveAttachment, parameterRefs.arpOctave);
    addSliderControl(arpGateKnob, arpGateAttachment, ids::arpGate);
    addSliderControl(arpSwingKnob, arpSwingAttachment, ids::arpSwing);
    addToggleControl(arpLatchToggle, arpLatchAttachment, ids::arpLatch);
    addToggleControl(drvOnToggle, drvOnAttachment, ids::driveEnabled);
    addSliderControl(drvAmtKnob, drvAmtAttachment, ids::driveAmount);
    addSliderControl(drvMixKnob, drvMixAttachment, ids::driveMix);
    addToggleControl(choOnToggle, choOnAttachment, ids::chorusEnabled);
    addSliderControl(choRateKnob, choRateAttachment, ids::chorusRateHz);
    addSliderControl(choDepKnob, choDepAttachment, ids::chorusDepth);
    addSliderControl(choMixKnob, choMixAttachment, ids::chorusMix);
    addToggleControl(dlyOnToggle, dlyOnAttachment, ids::delayEnabled);
    addSliderControl(dlyTimeKnob, dlyTimeAttachment, ids::delayTimeMs);
    addSliderControl(dlyFdbkKnob, dlyFdbkAttachment, ids::delayFeedback);
    addSliderControl(dlyMixKnob, dlyMixAttachment, ids::delayMix);
    addToggleControl(revOnToggle, revOnAttachment, ids::reverbEnabled);
    addSliderControl(revSizeKnob, revSizeAttachment, ids::reverbSize);
    addSliderControl(revDampKnob, revDampAttachment, ids::reverbDamping);
    addSliderControl(revMixKnob, revMixAttachment, ids::reverbMix);
    addSliderControl(outGainKnob, outGainAttachment, ids::masterGainDb);
    addSliderControl(timbreKnob, timbreAttachment, ids::timbre);
    addSliderControl(exciteKnob, exciteAttachment, ids::excite);
    addToggleControl(phsOnToggle, phsOnAttachment, ids::phaserEnabled);
    addSliderControl(phsRateKnob, phsRateAttachment, ids::phaserRateHz);
    addSliderControl(phsDepthKnob, phsDepthAttachment, ids::phaserDepth);
    addToggleControl(cmpOnToggle, cmpOnAttachment, ids::compressorEnabled);
    addSliderControl(cmpAmtKnob, cmpAmtAttachment, ids::compressorAmount);
    addSliderControl(cmpMixKnob, cmpMixAttachment, ids::compressorMix);
}

void SynthAudioProcessorEditor::registerLearnableControls()
{
    namespace ids = coolsynth::parameters::ids;

    auto applyKnobState = [](coolsynth::ui::HardwareKnob& knob, bool armed, juce::String badge)
    {
        knob.setLearnState(armed, badge);
    };
    auto applyToggleState = [](coolsynth::ui::LedToggleButton& toggle, bool armed, juce::String badge)
    {
        toggle.setLearnState(armed, badge);
    };
    auto applyChoiceState = [](coolsynth::ui::SegmentedChoiceGroup& choice, bool armed, juce::String badge)
    {
        choice.setLearnState(armed, badge);
    };
    auto applyArpPatternState = [this](bool armed, juce::String)
    {
        const auto colour = armed ? coolsynth::ui::palette::learnYellow
                                  : coolsynth::ui::palette::ledGreen;
        arpPatternChoice.setColour(juce::ComboBox::outlineColourId, colour);
        arpPatternChoice.setColour(juce::ComboBox::focusedOutlineColourId, colour);
        arpPatternLabel.setColour(juce::Label::textColourId,
                                  armed ? coolsynth::ui::palette::learnYellow
                                        : coolsynth::ui::palette::textPrimary);
        arpPatternChoice.repaint();
        arpPatternLabel.repaint();
    };
    registerLearnableControl(oscAWaveChoice, ids::oscAWave, "Wave", [&](bool a, juce::String b) { applyChoiceState(oscAWaveChoice, a, b); });
    registerLearnableControl(oscAOctaveKnob, ids::oscAOctave, "Octave", [&](bool a, juce::String b) { applyKnobState(oscAOctaveKnob, a, b); });
    registerLearnableControl(oscAFineKnob, ids::oscAFineCents, "Fine", [&](bool a, juce::String b) { applyKnobState(oscAFineKnob, a, b); });
    registerLearnableControl(oscAPwKnob, ids::oscAPulseWidth, "Pulse W", [&](bool a, juce::String b) { applyKnobState(oscAPwKnob, a, b); });
    registerLearnableControl(oscASyncToggle, ids::oscASyncEnabled, "Sync", [&](bool a, juce::String b) { applyToggleState(oscASyncToggle, a, b); });
    registerLearnableControl(oscBWaveChoice, ids::oscBWave, "Wave", [&](bool a, juce::String b) { applyChoiceState(oscBWaveChoice, a, b); });
    registerLearnableControl(oscBOctaveKnob, ids::oscBOctave, "Octave", [&](bool a, juce::String b) { applyKnobState(oscBOctaveKnob, a, b); });
    registerLearnableControl(oscBFineKnob, ids::oscBFineCents, "Fine", [&](bool a, juce::String b) { applyKnobState(oscBFineKnob, a, b); });
    registerLearnableControl(oscBPwKnob, ids::oscBPulseWidth, "Pulse W", [&](bool a, juce::String b) { applyKnobState(oscBPwKnob, a, b); });
    registerLearnableControl(oscBLoFreqToggle, ids::oscBLowFrequencyMode, "Lo Freq", [&](bool a, juce::String b) { applyToggleState(oscBLoFreqToggle, a, b); });
    registerLearnableControl(mixOscAKnob, ids::oscALevel, "Osc A", [&](bool a, juce::String b) { applyKnobState(mixOscAKnob, a, b); });
    registerLearnableControl(mixOscBKnob, ids::oscBLevel, "Osc B", [&](bool a, juce::String b) { applyKnobState(mixOscBKnob, a, b); });
    registerLearnableControl(mixNoiseKnob, ids::noiseLevel, "Noise", [&](bool a, juce::String b) { applyKnobState(mixNoiseKnob, a, b); });
    registerLearnableControl(fltCutoffKnob, ids::filterCutoffHz, "Cutoff", [&](bool a, juce::String b) { applyKnobState(fltCutoffKnob, a, b); });
    registerLearnableControl(fltResKnob, ids::filterResonance, "Resonance", [&](bool a, juce::String b) { applyKnobState(fltResKnob, a, b); });
    registerLearnableControl(fltEnvAmtKnob, ids::filterEnvAmount, "Env Amt", [&](bool a, juce::String b) { applyKnobState(fltEnvAmtKnob, a, b); });
    registerLearnableControl(fltKeyTrkChoice, ids::filterKeyTracking, "Key Trk", [&](bool a, juce::String b) { applyChoiceState(fltKeyTrkChoice, a, b); });
    registerLearnableControl(fEnvAKnob, ids::filterAttackMs, "F Atk", [&](bool a, juce::String b) { applyKnobState(fEnvAKnob, a, b); });
    registerLearnableControl(fEnvDKnob, ids::filterDecayMs, "F Dec", [&](bool a, juce::String b) { applyKnobState(fEnvDKnob, a, b); });
    registerLearnableControl(fEnvSKnob, ids::filterSustain, "F Sus", [&](bool a, juce::String b) { applyKnobState(fEnvSKnob, a, b); });
    registerLearnableControl(fEnvRKnob, ids::filterReleaseMs, "F Rel", [&](bool a, juce::String b) { applyKnobState(fEnvRKnob, a, b); });
    registerLearnableControl(aEnvAKnob, ids::ampAttackMs, "A Atk", [&](bool a, juce::String b) { applyKnobState(aEnvAKnob, a, b); });
    registerLearnableControl(aEnvDKnob, ids::ampDecayMs, "A Dec", [&](bool a, juce::String b) { applyKnobState(aEnvDKnob, a, b); });
    registerLearnableControl(aEnvSKnob, ids::ampSustain, "A Sus", [&](bool a, juce::String b) { applyKnobState(aEnvSKnob, a, b); });
    registerLearnableControl(aEnvRKnob, ids::ampReleaseMs, "A Rel", [&](bool a, juce::String b) { applyKnobState(aEnvRKnob, a, b); });
    registerLearnableControl(lfoRateKnob, ids::lfoRateHz, "Rate", [&](bool a, juce::String b) { applyKnobState(lfoRateKnob, a, b); });
    registerLearnableControl(lfoWaveChoice, ids::lfoWave, "Wave", [&](bool a, juce::String b) { applyChoiceState(lfoWaveChoice, a, b); });
    registerLearnableControl(lfoMwDepKnob, ids::modWheelToLfoDepth, "MW->Dep", [&](bool a, juce::String b) { applyKnobState(lfoMwDepKnob, a, b); });
    registerLearnableControl(lfoPitchKnob, ids::lfoToOscPitch, "->Pitch", [&](bool a, juce::String b) { applyKnobState(lfoPitchKnob, a, b); });
    registerLearnableControl(lfoPwKnob, ids::lfoToPulseWidth, "->PW", [&](bool a, juce::String b) { applyKnobState(lfoPwKnob, a, b); });
    registerLearnableControl(lfoCutoffKnob, ids::lfoToFilterCutoff, "->Cutoff", [&](bool a, juce::String b) { applyKnobState(lfoCutoffKnob, a, b); });
    registerLearnableControl(pmodBPitchKnob, ids::polyModOscBToOscPitch, "B->Pitch", [&](bool a, juce::String b) { applyKnobState(pmodBPitchKnob, a, b); });
    registerLearnableControl(pmodBPwKnob, ids::polyModOscBToPulseWidth, "B->PW", [&](bool a, juce::String b) { applyKnobState(pmodBPwKnob, a, b); });
    registerLearnableControl(pmodBCutoffKnob, ids::polyModOscBToFilterCutoff, "B->Cutoff", [&](bool a, juce::String b) { applyKnobState(pmodBCutoffKnob, a, b); });
    registerLearnableControl(pmodEPitchKnob, ids::polyModEnvToOscPitch, "E->Pitch", [&](bool a, juce::String b) { applyKnobState(pmodEPitchKnob, a, b); });
    registerLearnableControl(pmodEPwKnob, ids::polyModEnvToPulseWidth, "E->PW", [&](bool a, juce::String b) { applyKnobState(pmodEPwKnob, a, b); });
    registerLearnableControl(pmodECutoffKnob, ids::polyModEnvToFilterCutoff, "E->Cutoff", [&](bool a, juce::String b) { applyKnobState(pmodECutoffKnob, a, b); });
    registerLearnableControl(perfGlideKnob, ids::glideTimeMs, "Glide", [&](bool a, juce::String b) { applyKnobState(perfGlideKnob, a, b); });
    registerLearnableControl(perfModeChoice, ids::playMode, "Mode", [&](bool a, juce::String b) { applyChoiceState(perfModeChoice, a, b); });
    registerLearnableControl(perfPrioChoice, ids::keyPriority, "Priority", [&](bool a, juce::String b) { applyChoiceState(perfPrioChoice, a, b); });
    registerLearnableControl(perfPbRangeKnob, ids::pitchBendRangeSemitones, "PB Range", [&](bool a, juce::String b) { applyKnobState(perfPbRangeKnob, a, b); });
    registerLearnableControl(perfVintageKnob, ids::vintageAmount, "Vintage", [&](bool a, juce::String b) { applyKnobState(perfVintageKnob, a, b); });
    registerLearnableControl(perfPanKnob, ids::panSpread, "Pan Spread", [&](bool a, juce::String b) { applyKnobState(perfPanKnob, a, b); });
    registerLearnableControl(perfVelAmpKnob, ids::velocityToAmp, "Vel->Amp", [&](bool a, juce::String b) { applyKnobState(perfVelAmpKnob, a, b); });
    registerLearnableControl(perfVelFltKnob, ids::velocityToFilter, "Vel->Flt", [&](bool a, juce::String b) { applyKnobState(perfVelFltKnob, a, b); });
    registerLearnableControl(arpOnToggle, ids::arpEnabled, "Arp On", [&](bool a, juce::String b) { applyToggleState(arpOnToggle, a, b); });
    registerLearnableControl(arpRateChoice, ids::arpRateDivision, "Rate", [&](bool a, juce::String b) { applyChoiceState(arpRateChoice, a, b); });
    registerLearnableControl(arpPatternChoice, ids::arpPattern, "Pattern", applyArpPatternState);
    registerLearnableControl(arpOctaveChoice, ids::arpOctaveRange, "Octave", [&](bool a, juce::String b) { applyChoiceState(arpOctaveChoice, a, b); });
    registerLearnableControl(arpGateKnob, ids::arpGate, "Gate", [&](bool a, juce::String b) { applyKnobState(arpGateKnob, a, b); });
    registerLearnableControl(arpSwingKnob, ids::arpSwing, "Swing", [&](bool a, juce::String b) { applyKnobState(arpSwingKnob, a, b); });
    registerLearnableControl(arpLatchToggle, ids::arpLatch, "Latch", [&](bool a, juce::String b) { applyToggleState(arpLatchToggle, a, b); });
    if (arpAdvancedOverlay != nullptr)
    {
        if (juce::JUCEApplicationBase::isStandaloneApp())
            registerLearnableControl(arpAdvancedOverlay->getTempoKnob(), ids::arpInternalTempoBpm, "Tempo", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getTempoKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getRhythmChoice(), ids::arpRhythm, "Rhythm", [&](bool a, juce::String b) { applyChoiceState(arpAdvancedOverlay->getRhythmChoice(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getChanceKnob(), ids::arpChance, "Chance", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getChanceKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getRatchetChoice(), ids::arpRatchetCount, "Ratchet", [&](bool a, juce::String b) { applyChoiceState(arpAdvancedOverlay->getRatchetChoice(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getRatchetChanceKnob(), ids::arpRatchetChance, "Rat Ch", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getRatchetChanceKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getAccentChoice(), ids::arpAccentEvery, "Accent Every", [&](bool a, juce::String b) { applyChoiceState(arpAdvancedOverlay->getAccentChoice(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getAccentAmountKnob(), ids::arpAccentAmount, "Accent", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getAccentAmountKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getEuclideanPulsesKnob(), ids::arpEuclideanPulses, "Pulses", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getEuclideanPulsesKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getEuclideanStepsKnob(), ids::arpEuclideanSteps, "Steps", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getEuclideanStepsKnob(), a, b); });
        registerLearnableControl(arpAdvancedOverlay->getEuclideanRotationKnob(), ids::arpEuclideanRotation, "Rotation", [&](bool a, juce::String b) { applyKnobState(arpAdvancedOverlay->getEuclideanRotationKnob(), a, b); });
    }
    registerLearnableControl(drvOnToggle, ids::driveEnabled, "Distortion", [&](bool a, juce::String b) { applyToggleState(drvOnToggle, a, b); });
    registerLearnableControl(drvAmtKnob, ids::driveAmount, "Amount", [&](bool a, juce::String b) { applyKnobState(drvAmtKnob, a, b); });
    registerLearnableControl(drvMixKnob, ids::driveMix, "Mix", [&](bool a, juce::String b) { applyKnobState(drvMixKnob, a, b); });
    registerLearnableControl(choOnToggle, ids::chorusEnabled, "Chorus", [&](bool a, juce::String b) { applyToggleState(choOnToggle, a, b); });
    registerLearnableControl(choRateKnob, ids::chorusRateHz, "Rate", [&](bool a, juce::String b) { applyKnobState(choRateKnob, a, b); });
    registerLearnableControl(choDepKnob, ids::chorusDepth, "Depth", [&](bool a, juce::String b) { applyKnobState(choDepKnob, a, b); });
    registerLearnableControl(choMixKnob, ids::chorusMix, "Mix", [&](bool a, juce::String b) { applyKnobState(choMixKnob, a, b); });
    registerLearnableControl(dlyOnToggle, ids::delayEnabled, "Delay", [&](bool a, juce::String b) { applyToggleState(dlyOnToggle, a, b); });
    registerLearnableControl(dlyTimeKnob, ids::delayTimeMs, "Time", [&](bool a, juce::String b) { applyKnobState(dlyTimeKnob, a, b); });
    registerLearnableControl(dlyFdbkKnob, ids::delayFeedback, "Fdbk", [&](bool a, juce::String b) { applyKnobState(dlyFdbkKnob, a, b); });
    registerLearnableControl(dlyMixKnob, ids::delayMix, "Mix", [&](bool a, juce::String b) { applyKnobState(dlyMixKnob, a, b); });
    registerLearnableControl(revOnToggle, ids::reverbEnabled, "Reverb", [&](bool a, juce::String b) { applyToggleState(revOnToggle, a, b); });
    registerLearnableControl(revSizeKnob, ids::reverbSize, "Size", [&](bool a, juce::String b) { applyKnobState(revSizeKnob, a, b); });
    registerLearnableControl(revDampKnob, ids::reverbDamping, "Damp", [&](bool a, juce::String b) { applyKnobState(revDampKnob, a, b); });
    registerLearnableControl(revMixKnob, ids::reverbMix, "Mix", [&](bool a, juce::String b) { applyKnobState(revMixKnob, a, b); });
    registerLearnableControl(outGainKnob, ids::masterGainDb, "Master", [&](bool a, juce::String b) { applyKnobState(outGainKnob, a, b); });
    registerLearnableControl(timbreKnob, ids::timbre, "Timbre", [&](bool a, juce::String b) { applyKnobState(timbreKnob, a, b); });
    registerLearnableControl(exciteKnob, ids::excite, "Excite", [&](bool a, juce::String b) { applyKnobState(exciteKnob, a, b); });
    registerLearnableControl(phsOnToggle, ids::phaserEnabled, "Phaser", [&](bool a, juce::String b) { applyToggleState(phsOnToggle, a, b); });
    registerLearnableControl(phsRateKnob, ids::phaserRateHz, "Rate", [&](bool a, juce::String b) { applyKnobState(phsRateKnob, a, b); });
    registerLearnableControl(phsDepthKnob, ids::phaserDepth, "Depth", [&](bool a, juce::String b) { applyKnobState(phsDepthKnob, a, b); });
    registerLearnableControl(cmpOnToggle, ids::compressorEnabled, "Compressor", [&](bool a, juce::String b) { applyToggleState(cmpOnToggle, a, b); });
    registerLearnableControl(cmpAmtKnob, ids::compressorAmount, "Amount", [&](bool a, juce::String b) { applyKnobState(cmpAmtKnob, a, b); });
    registerLearnableControl(cmpMixKnob, ids::compressorMix, "Mix", [&](bool a, juce::String b) { applyKnobState(cmpMixKnob, a, b); });
}

void SynthAudioProcessorEditor::setupActionButtons()
{
    coolsynth::ui::applyGreenActionButtonStyle(initPatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(savePatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(loadPatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(allNotesOffButton, "panicButton");
    coolsynth::ui::applyGreenActionButtonStyle(tooltipToggleButton, "tooltipToggleButton");
    coolsynth::ui::applyGreenActionButtonStyle(arpAdvancedButton, "patchButton");
    allNotesOffButton.setButtonText({});
    allNotesOffButton.onClick = [this] { processor.requestPanic(); };
    arpAdvancedButton.onClick = [this]
    {
        setArpAdvancedOverlayVisible(arpAdvancedOverlay == nullptr || !arpAdvancedOverlay->isVisible());
    };
    tooltipToggleButton.setClickingTogglesState(true);
    tooltipToggleButton.setToggleState(true, juce::dontSendNotification);
    tooltipToggleButton.onClick = [this]
    {
        tooltipsEnabled = tooltipToggleButton.getToggleState();

        if (tooltipWindow != nullptr)
            tooltipWindow->hideTip();
    };
    addAndMakeVisible(allNotesOffButton);
    addAndMakeVisible(tooltipToggleButton);
    addAndMakeVisible(arpAdvancedButton);

    addAndMakeVisible(pianoBar);

    midiLearnManager = std::make_unique<coolsynth::midi::MidiLearnManager>();
    midiLearnManager->replaceBindings(processor.getLearnedMidiBindings());

    patchActionsVisible = true;
    initPatchButton.onClick = [this] { triggerInitPatch(); };
    savePatchButton.onClick = [this] { triggerSavePatch(); };
    loadPatchButton.onClick = [this] { triggerLoadPatch(); };
    addAndMakeVisible(initPatchButton);
    addAndMakeVisible(savePatchButton);
    addAndMakeVisible(loadPatchButton);
}

void SynthAudioProcessorEditor::setupStandaloneMode(bool isStandalone)
{
    if (isStandalone)
    {
        auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager();
        auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();

        jassert(deviceManager != nullptr);

        if (settingsStore != nullptr)
        {
            auto mappings = settingsStore->loadLearnedMidiMappings();
            midiLearnManager->replaceBindings(mappings);
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
        }

        standaloneMidiController = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
            *deviceManager,
            settingsStore,
            [this](const coolsynth::midi::ControllerMidiEvent& event)
            {
                handleStandaloneControllerEvent(event);
            },
            [this]
            {
                processor.requestPanic();
            });

        standaloneMidiController->addChangeListener(this);
        refreshStandaloneControllerProfileSelection();

        standaloneStatusBar = std::make_unique<StandaloneStatusBar>(*standaloneMidiController,
            [this]
            {
                const auto profile = getResolvedStandaloneControllerProfileDisplayName();
                return profile.isNotEmpty() ? "PROFILE: " + profile.toUpperCase() : juce::String {};
            });
        addAndMakeVisible(*standaloneStatusBar);

        setSize(Layout::defaultWindowWidth, Layout::standaloneWindowHeight());
    }
    else
    {
        setSize(Layout::defaultWindowWidth, Layout::pluginWindowHeight());
    }
}

void SynthAudioProcessorEditor::setupTooltipWindow()
{
    tooltipLookAndFeel = std::make_unique<EditorTooltipLookAndFeel>();
    tooltipWindow = std::make_unique<EditorTooltipWindow>(this, 500);
    static_cast<EditorTooltipWindow*>(tooltipWindow.get())->isEnabledProvider = [this] { return tooltipsEnabled; };
    tooltipWindow->setLookAndFeel(tooltipLookAndFeel.get());
}

void SynthAudioProcessorEditor::setupPresetSelector()
{
    coolsynth::ui::applyGreenComboBoxStyle(presetSelector);
    presetSelector.setTextWhenNothingSelected("PRESET");
    presetSelector.setJustificationType(juce::Justification::centredLeft);
    presetSelector.setTooltip("Factory presets — selecting one replaces the current patch.");

    const auto presetCount = coolsynth::presets::getFactoryPresetCount();
    for (int i = 0; i < presetCount; ++i)
    {
        const auto& preset = coolsynth::presets::getFactoryPreset(i);
        const auto label = juce::String(preset.name) + "  \xe2\x80\x94  " + juce::String(preset.category);
        presetSelector.addItem(label, i + 1);
    }

    presetSelector.onChange = [this] { applySelectedPreset(); };

    addAndMakeVisible(presetSelector);
}

void SynthAudioProcessorEditor::applySelectedPreset()
{
    const auto selected = presetSelector.getSelectedId();
    if (selected <= 0)
        return;

    const int index = selected - 1;
    if (index < 0 || index >= coolsynth::presets::getFactoryPresetCount())
        return;

    coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(),
                                           coolsynth::presets::getFactoryPreset(index));
}

void SynthAudioProcessorEditor::setArpAdvancedOverlayVisible(bool shouldBeVisible)
{
    if (arpAdvancedOverlay == nullptr)
        return;

    arpAdvancedOverlay->setVisible(shouldBeVisible);
    arpAdvancedButton.setToggleState(shouldBeVisible, juce::dontSendNotification);

    if (tooltipWindow != nullptr)
        tooltipWindow->hideTip();

    if (shouldBeVisible)
    {
        arpAdvancedOverlay->toFront(true);
        arpAdvancedOverlay->grabKeyboardFocus();
    }
}

void SynthAudioProcessorEditor::refreshArpAdvancedSummary()
{
    arpAdvancedSummaryLabel.setText(buildArpAdvancedSummaryText(),
                                    juce::dontSendNotification);
}

juce::String SynthAudioProcessorEditor::buildArpAdvancedSummaryText() const
{
    auto getPlainValue = [](juce::RangedAudioParameter* parameter) -> float
    {
        return parameter != nullptr ? parameter->convertFrom0to1(parameter->getValue()) : 0.0f;
    };

    auto getChoiceIndex = [&getPlainValue](juce::RangedAudioParameter* parameter) -> int
    {
        return static_cast<int>(std::lround(getPlainValue(parameter)));
    };

    juce::StringArray parts;

    if (getChoiceIndex(parameterRefs.arpRhythm) == static_cast<int>(coolsynth::parameters::ArpRhythmChoice::euclidean))
    {
        auto rhythmSummary = "Euc "
            + juce::String(static_cast<int>(std::lround(getPlainValue(parameterRefs.arpEuclideanPulses))))
            + "/"
            + juce::String(static_cast<int>(std::lround(getPlainValue(parameterRefs.arpEuclideanSteps))));

        const int rotation = static_cast<int>(std::lround(getPlainValue(parameterRefs.arpEuclideanRotation)));
        if (rotation > 0)
            rhythmSummary << " r" << rotation;

        parts.add(rhythmSummary);
    }

    if (getPlainValue(parameterRefs.arpSwing) > 0.001f)
        parts.add("Swing " + getCurrentParameterText(parameterRefs.arpSwing));

    if (getPlainValue(parameterRefs.arpChance) < 0.999f)
        parts.add("Chance " + getCurrentParameterText(parameterRefs.arpChance));

    const int ratchetCount = getChoiceIndex(parameterRefs.arpRatchetCount);
    const float ratchetChance = getPlainValue(parameterRefs.arpRatchetChance);
    if (ratchetCount != static_cast<int>(coolsynth::parameters::ArpRatchetChoice::off)
        || ratchetChance > 0.001f)
    {
        auto ratchetSummary = "Rat " + getCurrentParameterText(parameterRefs.arpRatchetCount);
        if (ratchetChance > 0.001f)
            ratchetSummary << " @" << getCurrentParameterText(parameterRefs.arpRatchetChance);
        parts.add(ratchetSummary);
    }

    const int accentEvery = getChoiceIndex(parameterRefs.arpAccentEvery);
    const float accentAmount = getPlainValue(parameterRefs.arpAccentAmount);
    if (accentEvery != static_cast<int>(coolsynth::parameters::ArpAccentEveryChoice::off)
        || accentAmount > 0.001f)
    {
        auto accentSummary = "Acc " + getCurrentParameterText(parameterRefs.arpAccentEvery);
        if (accentAmount > 0.001f)
            accentSummary << " +" << getCurrentParameterText(parameterRefs.arpAccentAmount);
        parts.add(accentSummary);
    }

    if (juce::JUCEApplicationBase::isStandaloneApp()
        && std::abs(getPlainValue(parameterRefs.arpTempo) - 120.0f) > 0.5f)
    {
        parts.add("Int " + getCurrentParameterText(parameterRefs.arpTempo));
    }

    return parts.joinIntoString("  •  ");
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    if (standaloneMidiController != nullptr)
        standaloneMidiController->removeChangeListener(this);

    stopTimer();

    presetSelector.setLookAndFeel(nullptr);

    if (tooltipWindow != nullptr)
        tooltipWindow->setLookAndFeel(nullptr);
}

int SynthAudioProcessorEditor::getControlParameterIndex(juce::Component& component)
{
    if (const auto* registration = findParameterSurfaceForComponent(&component))
        if (auto* parameter = findParameterForId(registration->parameterId))
            return parameter->getParameterIndex();

    return -1;
}

void SynthAudioProcessorEditor::refreshStandaloneControllerProfileSelection()
{
    if (standaloneMidiController == nullptr)
        return;

    juce::String resolvedProfileId;
    auto selection = coolsynth::standalone::PersistedControllerProfileSelection {};

    if (auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore())
        selection = settingsStore->loadPersistedControllerProfileSelection();

    switch (selection.mode)
    {
        case coolsynth::standalone::ControllerProfileSelectionMode::none:
            break;

        case coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile:
            resolvedProfileId = selection.profileId;
            break;

        case coolsynth::standalone::ControllerProfileSelectionMode::autoDetect:
        default:
        {
            const auto& snapshot = standaloneMidiController->getSnapshot();
            if (snapshot.selectedDeviceIdentifier.isNotEmpty() || snapshot.selectedDeviceName.isNotEmpty())
            {
                const juce::MidiDeviceInfo device(snapshot.selectedDeviceName,
                                                  snapshot.selectedDeviceIdentifier);
                resolvedProfileId = coolsynth::midi::ControllerProfileRegistry::get()
                                        .findBestProfileIdForDevice(device);
            }
            break;
        }
    }

    if (!processor.setActiveControllerProfile(resolvedProfileId))
        processor.setActiveControllerProfile({});

    refreshMidiLearnVisuals();
}

juce::String SynthAudioProcessorEditor::getResolvedStandaloneControllerProfileDisplayName() const
{
    auto displayName = processor.getActiveControllerProfileDisplayName();
    return displayName.isNotEmpty() ? displayName : "None";
}

void SynthAudioProcessorEditor::resetStandaloneMidiSettings()
{
    if (auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore())
        settingsStore->clearStandaloneMidiState();

    if (midiLearnManager != nullptr)
    {
        midiLearnManager->cancelLearning();
        midiLearnManager->replaceBindings({});
    }

    processor.setLearnedMidiBindings({});
    badgeVisibilityCounter = 0;
    lastShowCcLabelsSetting = true;

    refreshStandaloneControllerProfileSelection();
    refreshMidiLearnVisuals();
}

void SynthAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == standaloneMidiController.get())
        refreshStandaloneControllerProfileSelection();
}

juce::RangedAudioParameter* SynthAudioProcessorEditor::findParameterForId(juce::StringRef parameterId) const noexcept
{
    return processor.getValueTreeState().getParameter(parameterId);
}

void SynthAudioProcessorEditor::registerParameterSurface(juce::Component& surface, juce::String parameterId)
{
    for (auto& registration : parameterSurfaces)
    {
        if (registration.surface == &surface)
        {
            registration.parameterId = std::move(parameterId);
            return;
        }
    }

    parameterSurfaces.push_back({ std::move(parameterId), &surface });
    surface.addMouseListener(this, true);
}

void SynthAudioProcessorEditor::registerLearnableControl(juce::Component& surface,
                                                         juce::String parameterId,
                                                         juce::String displayName,
                                                         std::function<void(bool, juce::String)> applyVisualState)
{
    registerParameterSurface(surface, parameterId);
    learnableControls.push_back({ parameterId, displayName, &surface, std::move(applyVisualState) });
}

const SynthAudioProcessorEditor::ParameterSurfaceRegistration*
SynthAudioProcessorEditor::findParameterSurfaceForComponent(const juce::Component* component) const noexcept
{
    for (auto* current = component; current != nullptr; current = current->getParentComponent())
        for (const auto& registration : parameterSurfaces)
            if (registration.surface == current)
                return &registration;

    return nullptr;
}

const SynthAudioProcessorEditor::LearnableControlRegistration*
SynthAudioProcessorEditor::findLearnableControl(juce::StringRef parameterId) const noexcept
{
    for (const auto& registration : learnableControls)
        if (registration.parameterId == parameterId)
            return &registration;

    return nullptr;
}

void SynthAudioProcessorEditor::handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event)
{
    if (midiLearnManager != nullptr)
    {
        const auto outcome = midiLearnManager->handleIncomingEvent(event);

        if (outcome.bindingsChanged)
        {
            auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();
            if (settingsStore != nullptr)
            {
                settingsStore->saveLearnedMidiMappings(midiLearnManager->getBindings());
            }
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
            badgeVisibilityCounter = 72; // Show briefly if settings hide them
            refreshMidiLearnVisuals();
        }

        if (event.type == coolsynth::midi::ControllerMidiEventType::controlChange)
        {
            for (const auto& b : midiLearnManager->getBindings())
            {
                if (b.cc.channel == event.channel && b.cc.controllerNumber == event.data1)
                {
                    if (!lastShowCcLabelsSetting && badgeVisibilityCounter == 0)
                    {
                        badgeVisibilityCounter = 72;
                        refreshMidiLearnVisuals();
                    }
                    else
                    {
                        badgeVisibilityCounter = 72;
                    }
                    break;
                }
            }
        }

        if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::captured)
        {
            processor.handleStandaloneControllerEvent(event);
            return;
        }
        else if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::rejectedNonCc)
        {
            midiLearnStatusLabel.setText(outcome.statusText, juce::dontSendNotification);
        }
    }

    processor.handleStandaloneControllerEvent(event);
}

void SynthAudioProcessorEditor::refreshMidiLearnVisuals()
{
    auto session = coolsynth::midi::MidiLearnSession {};
    if (midiLearnManager != nullptr)
        session = midiLearnManager->getSession();

    auto statusText = session.statusText;
    auto statusColour = coolsynth::ui::palette::learnYellow;

    midiLearnStatusLabel.setColour(juce::Label::textColourId, statusColour);
    midiLearnStatusLabel.setText(statusText, juce::dontSendNotification);

    const bool shouldShowBadges = midiLearnManager != nullptr
                               && (lastShowCcLabelsSetting || session.armed || badgeVisibilityCounter > 0);

    for (auto& ctrl : learnableControls)
    {
        bool isArmed = session.armed && session.parameterId == ctrl.parameterId;
        juce::String badge = "";

        if (shouldShowBadges && midiLearnManager != nullptr)
        {
            if (auto* binding = midiLearnManager->findBindingForParameter(ctrl.parameterId))
            {
                badge = "CC" + juce::String(binding->cc.controllerNumber);
            }
        }

        if (ctrl.applyVisualState)
        {
            ctrl.applyVisualState(isArmed, badge);
        }
    }
}

void SynthAudioProcessorEditor::pollPluginMidiLearnEvents()
{
    if (standaloneMidiController != nullptr || midiLearnManager == nullptr)
        return;

    std::array<coolsynth::midi::ControllerMidiEvent, 32> localEvents {};
    bool visualsChanged = false;

    while (true)
    {
        const auto drained = processor.drainPendingPluginControllerEvents(localEvents.data(),
                                                                          static_cast<int> (localEvents.size()));
        if (drained == 0)
            break;

        for (int i = 0; i < drained; ++i)
        {
            const auto& event = localEvents[static_cast<size_t> (i)];
            const auto outcome = midiLearnManager->handleIncomingEvent(event);

            if (outcome.bindingsChanged)
            {
                processor.setLearnedMidiBindings(midiLearnManager->getBindings());
                badgeVisibilityCounter = 72;
                visualsChanged = true;
            }

            if (event.type == coolsynth::midi::ControllerMidiEventType::controlChange)
            {
                for (const auto& binding : midiLearnManager->getBindings())
                {
                    if (binding.cc.channel == event.channel && binding.cc.controllerNumber == event.data1)
                    {
                        badgeVisibilityCounter = 72;
                        visualsChanged = true;
                        break;
                    }
                }
            }

            if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::captured)
            {
                processor.handleStandaloneControllerEvent(event);
            }
            else if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::rejectedNonCc)
            {
                visualsChanged = true;
            }
        }
    }

    if (visualsChanged)
        refreshMidiLearnVisuals();
}

void SynthAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu())
        return;

    const auto* registration = findParameterSurfaceForComponent(event.eventComponent);
    if (registration == nullptr)
        return;

    juce::String displayName = registration->parameterId;
    if (const auto* learnable = findLearnableControl(registration->parameterId))
        displayName = learnable->displayName;
    else if (auto* parameter = findParameterForId(registration->parameterId))
        displayName = parameter->getName(64);

    showParameterContextMenu(registration->parameterId, displayName, event.getScreenPosition());
}

void SynthAudioProcessorEditor::showParameterContextMenu(juce::String parameterId,
                                                         juce::String displayName,
                                                         juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    constexpr int cancelMidiLearnItemId = 1001;
    constexpr int startMidiLearnItemId = 1002;
    constexpr int clearMidiLearnItemId = 1003;

    if (midiLearnManager != nullptr)
    {
        if (const auto* learnable = findLearnableControl(parameterId))
        {
            const auto session = midiLearnManager->getSession();
            const bool isCurrentlyArmed = session.armed && session.parameterId == parameterId;

            if (isCurrentlyArmed)
                menu.addItem(cancelMidiLearnItemId, "Cancel MIDI Learn", true, false);
            else
                menu.addItem(startMidiLearnItemId, "Learn MIDI CC", true, false);

            if (midiLearnManager->findBindingForParameter(parameterId) != nullptr)
                menu.addItem(clearMidiLearnItemId, "Clear MIDI CC Mapping", true, false);
        }
    }

    if (auto* parameter = findParameterForId(parameterId))
    {
        if (auto* context = getHostContext())
        {
            if (auto hostMenu = context->getContextMenuForParameter(parameter))
            {
                auto hostPopup = hostMenu->getEquivalentPopupMenu();
                if (hostPopup.getNumItems() > 0)
                {
                    if (menu.getNumItems() > 0)
                        menu.addSeparator();

                    menu.addSubMenu("Host", hostPopup);
                }
            }
        }
    }

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [this, parameterId = std::move(parameterId), displayName = std::move(displayName)](int result)
        {
            if (result == cancelMidiLearnItemId)
            {
                if (midiLearnManager != nullptr)
                {
                    midiLearnManager->cancelLearning();
                    refreshMidiLearnVisuals();
                }
            }
            else if (result == startMidiLearnItemId)
            {
                if (midiLearnManager != nullptr)
                {
                    midiLearnManager->beginLearning(parameterId, displayName);
                    refreshMidiLearnVisuals();
                }
            }
            else if (result == clearMidiLearnItemId)
            {
                if (midiLearnManager != nullptr && midiLearnManager->clearBinding(parameterId))
                {
                    auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();
                    if (settingsStore != nullptr)
                    {
                        settingsStore->saveLearnedMidiMappings(midiLearnManager->getBindings());
                    }
                    processor.setLearnedMidiBindings(midiLearnManager->getBindings());
                    refreshMidiLearnVisuals();
                }
            }
        });
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient background(coolsynth::ui::palette::panelBlack,
                                    0.0f,
                                    0.0f,
                                    coolsynth::ui::palette::panelRaised.darker(0.8f),
                                    0.0f,
                                    static_cast<float>(getHeight()),
                                    false);
    g.setGradientFill(background);
    g.fillRect(getLocalBounds());

    if (!juce::JUCEApplicationBase::isStandaloneApp())
    {
        auto footerArea = getLocalBounds().removeFromBottom(30);
        g.setColour(coolsynth::ui::palette::panelRaised.withAlpha(0.92f));
        g.fillRect(footerArea);
        g.setColour(coolsynth::ui::palette::panelStroke);
        g.drawLine(static_cast<float>(footerArea.getX()),
                   static_cast<float>(footerArea.getY()),
                   static_cast<float>(footerArea.getRight()),
                   static_cast<float>(footerArea.getY()),
                   1.0f);
    }
}

void SynthAudioProcessorEditor::resized()
{
    auto takeWeightedWidth = [](juce::Rectangle<int>& row, float weight, float& remainingWeight)
    {
        const auto width = juce::roundToInt(static_cast<float>(row.getWidth()) * (weight / remainingWeight));
        remainingWeight -= weight;
        return row.removeFromLeft(width);
    };

    auto bounds = getLocalBounds();
    if (standaloneStatusBar != nullptr)
    {
        standaloneStatusBar->setBounds(bounds.removeFromBottom(Layout::standaloneStatusBarH));
    }
    else
    {
        auto footerArea = bounds.removeFromBottom(Layout::pluginFooterHeight).reduced(12, 0);
        pluginStatusLabel.setBounds(footerArea.removeFromLeft(120));
        buildInfoLabel.setBounds(footerArea);
    }

    auto area = bounds.reduced(Layout::outerMargin);
    auto titleArea = area.removeFromTop(Layout::titleHeight);
    auto logoArea = titleArea.removeFromLeft(Layout::logoAreaWidth);

    // 7 panes with labels below, hosted inside a framed panel.
    titleArea.removeFromLeft(Layout::visualizerLeftGap);
    visualizer.setBounds(titleArea.removeFromLeft(Layout::visualizerWidth));

    if (titleLogoDrawable != nullptr)
    {
        auto enlargedLogoArea = logoArea.toFloat();
        enlargedLogoArea.setSize(enlargedLogoArea.getWidth() * 1.25f,
                                 enlargedLogoArea.getHeight() * 1.25f);
        enlargedLogoArea.setX(static_cast<float>(logoArea.getX()));
        enlargedLogoArea.setY(static_cast<float>(logoArea.getCentreY()) - (enlargedLogoArea.getHeight() * 0.5f));

        titleLogoDrawable->setTransformToFit(enlargedLogoArea,
                                             juce::RectanglePlacement(juce::RectanglePlacement::xLeft
                                                                      | juce::RectanglePlacement::yMid
                                                                      | juce::RectanglePlacement::onlyReduceInSize));
    }

    if (patchActionsVisible)
    {
        const auto buttonHeight = Layout::titleClusterButtonH;
        const auto panicSize = Layout::titleClusterButtonH;
        const auto tooltipButtonWidth = Layout::titleClusterButtonH;
        const auto patchButtonWidth = Layout::titleClusterPatchW;
        const auto gap = Layout::titleClusterGap;
        const auto totalWidth = panicSize + tooltipButtonWidth + (gap * 4) + (patchButtonWidth * 3);

        auto clusterArea = titleArea.removeFromRight(totalWidth);

        initPatchButton.setBounds(clusterArea.removeFromLeft(patchButtonWidth).withSizeKeepingCentre(patchButtonWidth, buttonHeight));
        clusterArea.removeFromLeft(gap);
        savePatchButton.setBounds(clusterArea.removeFromLeft(patchButtonWidth).withSizeKeepingCentre(patchButtonWidth, buttonHeight));
        clusterArea.removeFromLeft(gap);
        loadPatchButton.setBounds(clusterArea.removeFromLeft(patchButtonWidth).withSizeKeepingCentre(patchButtonWidth, buttonHeight));
        clusterArea.removeFromLeft(gap);
        allNotesOffButton.setBounds(clusterArea.removeFromLeft(panicSize).withSizeKeepingCentre(panicSize, panicSize));
        clusterArea.removeFromLeft(gap);
        tooltipToggleButton.setBounds(clusterArea.removeFromLeft(tooltipButtonWidth).withSizeKeepingCentre(tooltipButtonWidth, buttonHeight));
    }
    else
    {
        const auto sz = Layout::titleClusterButtonH;
        tooltipToggleButton.setBounds(titleArea.removeFromRight(sz).withSizeKeepingCentre(sz, sz));
        titleArea.removeFromRight(Layout::titleClusterGap);
        allNotesOffButton.setBounds(titleArea.removeFromRight(sz).withSizeKeepingCentre(sz, sz));
    }

    // Preset selector sits immediately left of the patch cluster, themed to match.
    titleArea.removeFromRight(Layout::titleControlGap);
    presetSelector.setBounds(titleArea.removeFromRight(Layout::presetSelectorWidth)
                                 .withSizeKeepingCentre(Layout::presetSelectorWidth,
                                                         Layout::presetSelectorHeight));
    titleArea.removeFromRight(Layout::titleControlGap);

    // MIDI learn status fills the remaining centre slack between visualizer and preset.
    midiLearnStatusLabel.setBounds(titleArea.withTrimmedTop(12));

    area.removeFromTop(Layout::spacerTitleToDeck1);

    // Helper that yields the inside-the-frame content rectangle for a section.
    auto sectionContentArea = [](juce::Rectangle<int> area) -> juce::Rectangle<int>
    {
        return area.reduced(Layout::sectionContentInset)
                   .withTrimmedTop(Layout::sectionTitleBarHeight);
    };

    // Helper that places a SynthSection's enable-toggle into its header strip and
    // returns the content rectangle below the title bar.
    auto layoutSectionWithToggle = [&](juce::Rectangle<int> area,
                                       coolsynth::ui::SynthSection& section,
                                       coolsynth::ui::LedToggleButton& toggle)
    {
        section.setBounds(area);
        auto header = area.reduced(Layout::sectionContentInset, 0).removeFromTop(Layout::sectionHeaderStripH);
        toggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
        toggle.setBounds(header.removeFromRight(Layout::sectionToggleSize)
                                .withSizeKeepingCentre(Layout::sectionToggleSize,
                                                        Layout::sectionToggleSize));
        return sectionContentArea(area);
    };

    // ---- Deck 1: Synth Core. Section widths come from Layout::Deck1 weights so
    //      the row always fills the available width.
    auto synthRow = area.removeFromTop(Layout::deck1Height);
    WeightedRowDistributor deck1Cols { synthRow, Layout::Deck1::total, Layout::Deck1::count, Layout::interSectionGap };

    // Oscillators
    {
        auto oscArea = deck1Cols.take(Layout::Deck1::osc);
        oscSection.setBounds(oscArea);
        auto oscContent = sectionContentArea(oscArea);
        auto oscRow1 = oscContent.removeFromTop(oscContent.getHeight() / 2);
        auto oscRow2 = oscContent;
        float oscWeightsTop = 6.2f;
        oscAWaveChoice.setBounds(takeWeightedWidth(oscRow1, 2.2f, oscWeightsTop));
        oscAOctaveKnob.setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
        oscAFineKnob  .setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
        oscAPwKnob    .setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
        oscASyncToggle.setBounds(oscRow1);

        float oscWeightsBottom = 6.2f;
        oscBWaveChoice .setBounds(takeWeightedWidth(oscRow2, 2.2f, oscWeightsBottom));
        oscBOctaveKnob .setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
        oscBFineKnob   .setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
        oscBPwKnob     .setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
        oscBLoFreqToggle.setBounds(oscRow2);
    }

    // Mixer
    {
        auto mixArea = deck1Cols.take(Layout::Deck1::mix);
        mixSection.setBounds(mixArea);
        auto mixContent = sectionContentArea(mixArea);
        auto mixRow1 = mixContent.removeFromTop(mixContent.getHeight() / 2);
        auto mixRow2 = mixContent;
        mixOscAKnob .setBounds(mixRow1.removeFromLeft(mixRow1.getWidth() / 2));
        mixOscBKnob .setBounds(mixRow1);
        mixNoiseKnob.setBounds(mixRow2.removeFromLeft(mixRow2.getWidth() / 2));
    }

    // Filter
    {
        auto fltArea = deck1Cols.take(Layout::Deck1::flt);
        fltSection.setBounds(fltArea);
        auto fltContent = sectionContentArea(fltArea);
        auto fltRow1 = fltContent.removeFromTop(fltContent.getHeight() / 2);
        auto fltRow2 = fltContent;
        fltCutoffKnob  .setBounds(fltRow1.removeFromLeft(fltRow1.getWidth() / 2));
        fltResKnob     .setBounds(fltRow1);
        fltEnvAmtKnob  .setBounds(fltRow2.removeFromLeft(fltRow2.getWidth() / 2));
        fltKeyTrkChoice.setBounds(fltRow2);
    }

    // Envelopes
    {
        auto envArea = deck1Cols.take(Layout::Deck1::env);
        envSection.setBounds(envArea);
        auto envContent = sectionContentArea(envArea);
        auto envRow1 = envContent.removeFromTop(envContent.getHeight() / 2);
        auto envRow2 = envContent;
        fEnvAKnob.setBounds(envRow1.removeFromLeft(envRow1.getWidth() / 4));
        fEnvDKnob.setBounds(envRow1.removeFromLeft(envRow1.getWidth() / 3));
        fEnvSKnob.setBounds(envRow1.removeFromLeft(envRow1.getWidth() / 2));
        fEnvRKnob.setBounds(envRow1);
        aEnvAKnob.setBounds(envRow2.removeFromLeft(envRow2.getWidth() / 4));
        aEnvDKnob.setBounds(envRow2.removeFromLeft(envRow2.getWidth() / 3));
        aEnvSKnob.setBounds(envRow2.removeFromLeft(envRow2.getWidth() / 2));
        aEnvRKnob.setBounds(envRow2);
    }

    // LFO
    {
        auto lfoArea = deck1Cols.take(Layout::Deck1::lfo);
        lfoSection.setBounds(lfoArea);
        auto lfoContent = sectionContentArea(lfoArea);
        auto lfoRow1 = lfoContent.removeFromTop(lfoContent.getHeight() / 2);
        auto lfoRow2 = lfoContent;
        float lfoTopWeights = 4.0f;
        lfoWaveChoice.setBounds(takeWeightedWidth(lfoRow1, 2.0f, lfoTopWeights));
        lfoRateKnob  .setBounds(takeWeightedWidth(lfoRow1, 1.0f, lfoTopWeights));
        lfoMwDepKnob .setBounds(lfoRow1);
        lfoPitchKnob .setBounds(lfoRow2.removeFromLeft(lfoRow2.getWidth() / 3));
        lfoPwKnob    .setBounds(lfoRow2.removeFromLeft(lfoRow2.getWidth() / 2));
        lfoCutoffKnob.setBounds(lfoRow2);
    }

    // Poly Mod
    {
        auto pmodArea = deck1Cols.take(Layout::Deck1::pmod);
        pmodSection.setBounds(pmodArea);
        auto pmodContent = sectionContentArea(pmodArea);
        auto pmodRow1 = pmodContent.removeFromTop(pmodContent.getHeight() / 2);
        auto pmodRow2 = pmodContent;
        pmodBPitchKnob .setBounds(pmodRow1.removeFromLeft(pmodRow1.getWidth() / 3));
        pmodBPwKnob    .setBounds(pmodRow1.removeFromLeft(pmodRow1.getWidth() / 2));
        pmodBCutoffKnob.setBounds(pmodRow1);
        pmodEPitchKnob .setBounds(pmodRow2.removeFromLeft(pmodRow2.getWidth() / 3));
        pmodEPwKnob    .setBounds(pmodRow2.removeFromLeft(pmodRow2.getWidth() / 2));
        pmodECutoffKnob.setBounds(pmodRow2);
    }

    // Performance
    {
        auto perfArea = deck1Cols.take(Layout::Deck1::perf);
        perfSection.setBounds(perfArea);
        auto perfContent = sectionContentArea(perfArea);
        auto perfRow1 = perfContent.removeFromTop(perfContent.getHeight() / 2);
        auto perfRow2 = perfContent;
        float perfTopWeights = 6.0f;
        perfGlideKnob  .setBounds(takeWeightedWidth(perfRow1, 1.0f, perfTopWeights));
        perfModeChoice .setBounds(takeWeightedWidth(perfRow1, 2.0f, perfTopWeights));
        perfPrioChoice .setBounds(takeWeightedWidth(perfRow1, 2.0f, perfTopWeights));
        perfPbRangeKnob.setBounds(perfRow1);
        perfVintageKnob.setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 4));
        perfPanKnob    .setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 3));
        perfVelAmpKnob .setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 2));
        perfVelFltKnob .setBounds(perfRow2);
    }

    area.removeFromTop(Layout::spacerDeck1ToDeck2);

    // ---- Deck 2: Effects rack and output. Same weighted distribution as Deck 1.
    auto lowerRow = area.removeFromTop(Layout::deck2Height);
    WeightedRowDistributor deck2Cols { lowerRow, Layout::Deck2::total, Layout::Deck2::count, Layout::interSectionGap };

    // Arpeggiator
    {
        auto arpArea = deck2Cols.take(Layout::Deck2::arp);
        auto arpContent = layoutSectionWithToggle(arpArea, arpSection, arpOnToggle);
        auto arpTopRow = arpContent.removeFromTop(46);
        arpContent.removeFromTop(6);
        auto arpMidRow = arpContent.removeFromTop(36);
        arpContent.removeFromTop(4);
        auto arpStatusRow = arpContent;

        float arpTopWeights = 5.2f;
        arpRateChoice  .setBounds(takeWeightedWidth(arpTopRow, 1.55f, arpTopWeights));
        auto arpPatternArea = takeWeightedWidth(arpTopRow, 2.85f, arpTopWeights);
        arpPatternLabel .setBounds(arpPatternArea.removeFromTop(18));
        arpPatternChoice.setBounds(arpPatternArea);
        arpOctaveChoice.setBounds(takeWeightedWidth(arpTopRow, 0.8f, arpTopWeights));
        arpGateKnob    .setBounds(arpTopRow);

        float arpMidWeights = 4.2f;
        arpSwingKnob.setBounds(takeWeightedWidth(arpMidRow, 1.0f, arpMidWeights));
        auto arpAdvancedArea = takeWeightedWidth(arpMidRow, 2.4f, arpMidWeights);
        arpAdvancedButton.setBounds(arpAdvancedArea.withSizeKeepingCentre(arpAdvancedArea.getWidth(), 24));
        arpLatchToggle.setBounds(arpMidRow.withWidth(52));
        arpAdvancedSummaryLabel.setBounds(arpStatusRow.reduced(4, 0).withTrimmedTop(2));
    }

    // Distortion (formerly Drive)
    {
        auto drvArea = deck2Cols.take(Layout::Deck2::drv);
        auto drvContent = layoutSectionWithToggle(drvArea, drvSection, drvOnToggle);
        drvAmtKnob.setBounds(drvContent.removeFromLeft(drvContent.getWidth() / 2));
        drvMixKnob.setBounds(drvContent);
    }

    // Chorus
    {
        auto choArea = deck2Cols.take(Layout::Deck2::cho);
        auto choContent = layoutSectionWithToggle(choArea, choSection, choOnToggle);
        choRateKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 3));
        choDepKnob .setBounds(choContent.removeFromLeft(choContent.getWidth() / 2));
        choMixKnob .setBounds(choContent);
    }

    // Delay
    {
        auto dlyArea = deck2Cols.take(Layout::Deck2::dly);
        auto dlyContent = layoutSectionWithToggle(dlyArea, dlySection, dlyOnToggle);
        dlyTimeKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 3));
        dlyFdbkKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 2));
        dlyMixKnob .setBounds(dlyContent);
    }

    // Reverb
    {
        auto revArea = deck2Cols.take(Layout::Deck2::rev);
        auto revContent = layoutSectionWithToggle(revArea, revSection, revOnToggle);
        revSizeKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 3));
        revDampKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 2));
        revMixKnob .setBounds(revContent);
    }

    // Output
    {
        auto outArea = deck2Cols.take(Layout::Deck2::out);
        outSection.setBounds(outArea);
        auto outContent = sectionContentArea(outArea);
        outGainKnob.setBounds(outContent);
    }

    area.removeFromTop(Layout::spacerDeck2ToBottom);
    auto bottomRow = area.removeFromTop(pianoBar.getDesiredHeight());
    const int pianoWidth = juce::jmin(pianoBar.getDesiredWidth(), bottomRow.getWidth());
    pianoBar.setBounds(bottomRow.removeFromLeft(pianoWidth));
    bottomRow.removeFromLeft(Layout::interSectionGap);

    // ---- Bottom-row FX cluster: Vibe | Phaser | Compressor.
    // Weighted distribution that always lays out, even if the parent row is narrower
    // than the natural total (each column then scales proportionally).
    const float fxTotalWeight = static_cast<float>(Layout::bottomRowMacrosWidth
                                                  + Layout::bottomRowPhaserWidth
                                                  + Layout::bottomRowCompressorW);
    constexpr int fxColumnCount = 3;
    WeightedRowDistributor fxCols { bottomRow, fxTotalWeight, fxColumnCount, Layout::bottomRowGap };

    // -- Vibe (macros)
    {
        auto vibeArea = fxCols.take(static_cast<float>(Layout::bottomRowMacrosWidth));
        macrosSection.setBounds(vibeArea);
        auto vibeContent = sectionContentArea(vibeArea);
        timbreKnob.setBounds(vibeContent.removeFromLeft(vibeContent.getWidth() / 2));
        exciteKnob.setBounds(vibeContent);
    }

    // -- Phaser
    {
        auto phaserArea = fxCols.take(static_cast<float>(Layout::bottomRowPhaserWidth));
        auto phaserContent = layoutSectionWithToggle(phaserArea, phaserSection, phsOnToggle);
        phsRateKnob .setBounds(phaserContent.removeFromLeft(phaserContent.getWidth() / 2));
        phsDepthKnob.setBounds(phaserContent);
    }

    // -- Compressor
    {
        auto compArea = fxCols.take(static_cast<float>(Layout::bottomRowCompressorW));
        auto compContent = layoutSectionWithToggle(compArea, compressorSection, cmpOnToggle);
        cmpAmtKnob.setBounds(compContent.removeFromLeft(compContent.getWidth() / 2));
        cmpMixKnob.setBounds(compContent);
    }

    if (arpAdvancedOverlay != nullptr)
    {
        arpAdvancedOverlay->setBounds(getLocalBounds());
        if (arpAdvancedOverlay->isVisible())
            arpAdvancedOverlay->toFront(false);
    }
}

void SynthAudioProcessorEditor::timerCallback()
{
    pollPluginMidiLearnEvents();
    refreshValueDisplays();

    if (badgeVisibilityCounter > 0)
    {
        badgeVisibilityCounter--;
        if (badgeVisibilityCounter == 0)
            refreshMidiLearnVisuals();
    }
    else
    {
        auto* store = coolsynth::standalone::getStandaloneSettingsStore();
        if (store != nullptr)
        {
            bool currentSetting = store->getShowCcLabels();
            if (currentSetting != lastShowCcLabelsSetting)
            {
                lastShowCcLabelsSetting = currentSetting;
                refreshMidiLearnVisuals();
            }
        }
    }
}

void SynthAudioProcessorEditor::refreshValueDisplays()
{
    oscAWaveChoice.setValueText(getCurrentParameterText(parameterRefs.oscAWave));
    oscAOctaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscAOctave));
    oscAFineKnob.setValueText(getCurrentParameterText(parameterRefs.oscAFine));
    oscAPwKnob.setValueText(getCurrentParameterText(parameterRefs.oscAPw));
    oscASyncToggle.setValueText(getCurrentParameterText(parameterRefs.oscASync));
    oscBWaveChoice.setValueText(getCurrentParameterText(parameterRefs.oscBWave));
    oscBOctaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscBOctave));
    oscBFineKnob.setValueText(getCurrentParameterText(parameterRefs.oscBFine));
    oscBPwKnob.setValueText(getCurrentParameterText(parameterRefs.oscBPw));
    oscBLoFreqToggle.setValueText(getCurrentParameterText(parameterRefs.oscBLoFreq));
    mixOscAKnob.setValueText(getCurrentParameterText(parameterRefs.mixOscA));
    mixOscBKnob.setValueText(getCurrentParameterText(parameterRefs.mixOscB));
    mixNoiseKnob.setValueText(getCurrentParameterText(parameterRefs.mixNoise));
    fltCutoffKnob.setValueText(getCurrentParameterText(parameterRefs.fltCutoff));
    fltResKnob.setValueText(getCurrentParameterText(parameterRefs.fltRes));
    fltEnvAmtKnob.setValueText(getCurrentParameterText(parameterRefs.fltEnvAmt));
    fltKeyTrkChoice.setValueText(getCurrentParameterText(parameterRefs.fltKeyTrk));
    fEnvAKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvA));
    fEnvDKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvD));
    fEnvSKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvS));
    fEnvRKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvR));
    aEnvAKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvA));
    aEnvDKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvD));
    aEnvSKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvS));
    aEnvRKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvR));
    lfoRateKnob.setValueText(getCurrentParameterText(parameterRefs.lfoRate));
    lfoWaveChoice.setValueText(getCurrentParameterText(parameterRefs.lfoWave));
    lfoMwDepKnob.setValueText(getCurrentParameterText(parameterRefs.lfoMwDep));
    lfoPitchKnob.setValueText(getCurrentParameterText(parameterRefs.lfoPitch));
    lfoPwKnob.setValueText(getCurrentParameterText(parameterRefs.lfoPw));
    lfoCutoffKnob.setValueText(getCurrentParameterText(parameterRefs.lfoCutoff));
    pmodBPitchKnob.setValueText(getCurrentParameterText(parameterRefs.pmodBPitch));
    pmodBPwKnob.setValueText(getCurrentParameterText(parameterRefs.pmodBPw));
    pmodBCutoffKnob.setValueText(getCurrentParameterText(parameterRefs.pmodBCutoff));
    pmodEPitchKnob.setValueText(getCurrentParameterText(parameterRefs.pmodEPitch));
    pmodEPwKnob.setValueText(getCurrentParameterText(parameterRefs.pmodEPw));
    pmodECutoffKnob.setValueText(getCurrentParameterText(parameterRefs.pmodECutoff));
    perfGlideKnob.setValueText(getCurrentParameterText(parameterRefs.perfGlide));
    perfModeChoice.setValueText(getCurrentParameterText(parameterRefs.perfMode));
    perfPrioChoice.setValueText(getCurrentParameterText(parameterRefs.perfPrio));
    perfPbRangeKnob.setValueText(getCurrentParameterText(parameterRefs.perfPbRange));
    perfVintageKnob.setValueText(getCurrentParameterText(parameterRefs.perfVintage));
    perfPanKnob.setValueText(getCurrentParameterText(parameterRefs.perfPan));
    perfVelAmpKnob.setValueText(getCurrentParameterText(parameterRefs.perfVelAmp));
    perfVelFltKnob.setValueText(getCurrentParameterText(parameterRefs.perfVelFlt));
    arpOnToggle.setValueText(getCurrentParameterText(parameterRefs.arpOn));
    arpRateChoice.setValueText(getCurrentParameterText(parameterRefs.arpRate));
    arpOctaveChoice.setValueText(getCurrentParameterText(parameterRefs.arpOctave));
    arpGateKnob.setValueText(getCurrentParameterText(parameterRefs.arpGate));
    arpSwingKnob.setValueText(getCurrentParameterText(parameterRefs.arpSwing));
    arpLatchToggle.setValueText(getCurrentParameterText(parameterRefs.arpLatch));
    if (arpAdvancedOverlay != nullptr)
        arpAdvancedOverlay->refreshFromParameters();
    refreshArpAdvancedSummary();
    drvOnToggle.setValueText(getCurrentParameterText(parameterRefs.drvOn));
    drvAmtKnob.setValueText(getCurrentParameterText(parameterRefs.drvAmt));
    drvMixKnob.setValueText(getCurrentParameterText(parameterRefs.drvMix));
    choOnToggle.setValueText(getCurrentParameterText(parameterRefs.choOn));
    choRateKnob.setValueText(getCurrentParameterText(parameterRefs.choRate));
    choDepKnob.setValueText(getCurrentParameterText(parameterRefs.choDep));
    choMixKnob.setValueText(getCurrentParameterText(parameterRefs.choMix));
    dlyOnToggle.setValueText(getCurrentParameterText(parameterRefs.dlyOn));
    dlyTimeKnob.setValueText(getCurrentParameterText(parameterRefs.dlyTime));
    dlyFdbkKnob.setValueText(getCurrentParameterText(parameterRefs.dlyFdbk));
    dlyMixKnob.setValueText(getCurrentParameterText(parameterRefs.dlyMix));
    revOnToggle.setValueText(getCurrentParameterText(parameterRefs.revOn));
    revSizeKnob.setValueText(getCurrentParameterText(parameterRefs.revSize));
    revDampKnob.setValueText(getCurrentParameterText(parameterRefs.revDamp));
    revMixKnob.setValueText(getCurrentParameterText(parameterRefs.revMix));
    outGainKnob.setValueText(getCurrentParameterText(parameterRefs.outGain));
}

juce::String SynthAudioProcessorEditor::getCurrentParameterText(juce::RangedAudioParameter* parameter) const
{
    if (parameter == nullptr)
        return "-";

    return parameter->getCurrentValueAsText();
}

void SynthAudioProcessorEditor::triggerInitPatch()
{
    processor.resetAutomatableParametersToDefaults();
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::triggerSavePatch()
{
    launchPatchSaveChooser();
}

void SynthAudioProcessorEditor::triggerLoadPatch()
{
    launchPatchLoadChooser();
}

void SynthAudioProcessorEditor::launchPatchSaveChooser()
{
    activePatchChooser = std::make_unique<juce::FileChooser>(
        "Save Patch",
        juce::File(),
        "*" + juce::String(coolsynth::presets::defaultPatchExtension),
        true,
        false,
        this);

    auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResult() != juce::File())
            handlePatchSaveSelection(chooser.getResult());
    });
}

void SynthAudioProcessorEditor::launchPatchLoadChooser()
{
    activePatchChooser = std::make_unique<juce::FileChooser>(
        "Load Patch",
        juce::File(),
        "*" + juce::String(coolsynth::presets::defaultPatchExtension),
        true,
        false,
        this);

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResult() != juce::File())
            handlePatchLoadSelection(chooser.getResult());
    });
}

void SynthAudioProcessorEditor::handlePatchSaveSelection(const juce::File& selectedFile)
{
    auto destination = selectedFile;
    if (!destination.hasFileExtension(coolsynth::presets::defaultPatchExtension))
        destination = destination.withFileExtension(coolsynth::presets::defaultPatchExtension);

    auto stateXml = processor.createParameterStateXml();
    if (stateXml == nullptr)
    {
        showPatchError("Failed to capture synth parameter state.");
        return;
    }

    auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                              processor.getParameterStateTypeName());
    auto result = coolsynth::presets::writePatchFile(destination, *patchXml);
    if (!result.succeeded())
        showPatchError(result.message);
}

void SynthAudioProcessorEditor::handlePatchLoadSelection(const juce::File& selectedFile)
{
    auto result = coolsynth::presets::readPatchFile(selectedFile,
                                                    processor.getParameterStateTypeName());
    if (!result.succeeded() || result.parameterStateXml == nullptr)
    {
        showPatchError(result.message);
        return;
    }

    if (!processor.applyParameterStateXml(*result.parameterStateXml))
    {
        showPatchError("Patch file contained incompatible parameter state.");
        return;
    }

    refreshValueDisplays();
}

void SynthAudioProcessorEditor::showPatchError(juce::String message)
{
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                "Patch Error",
                                                message,
                                                this);
}

bool SynthAudioProcessorEditor::isArpAdvancedOverlayVisibleForTesting() const noexcept
{
    return arpAdvancedOverlay != nullptr && arpAdvancedOverlay->isVisible();
}

bool SynthAudioProcessorEditor::areArpEuclideanControlsVisibleForTesting() const noexcept
{
    return arpAdvancedOverlay != nullptr
        && arpAdvancedOverlay->areEuclideanControlsVisibleForTesting();
}

juce::String SynthAudioProcessorEditor::getArpAdvancedSummaryTextForTesting() const
{
    return arpAdvancedSummaryLabel.getText();
}

void SynthAudioProcessorEditor::setArpAdvancedOverlayVisibleForTesting(bool shouldBeVisible)
{
    setArpAdvancedOverlayVisible(shouldBeVisible);
}

void SynthAudioProcessorEditor::refreshArpUiForTesting()
{
    refreshValueDisplays();
}
