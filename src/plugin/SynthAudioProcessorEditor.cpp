#include "SynthAudioProcessorEditor.h"

#include <array>
#include <cmath>

#include <BinaryData.h>

#include "BuildInfo.h"
#include "SynthAudioProcessor.h"
#include "midi/ControllerProfile.h"
#include "parameters/ParameterIDs.h"
#include "presets/PatchState.h"
#include "standalone/SettingsStore.h"
#include "standalone/StandaloneAudioSupport.h"
#include "ui/ActionButtonLookAndFeel.h"
#include "ui/StandaloneSettingsDialog.h"
#include "ui/StandaloneStatusBar.h"
#include "ui/UiPalette.h"

namespace
{
    juce::String wrapTooltipBody(juce::String text, int maxCharsPerLine = 38)
    {
        juce::StringArray paragraphs;
        paragraphs.addLines(text);

        juce::StringArray wrappedParagraphs;

        for (auto paragraph : paragraphs)
        {
            paragraph = paragraph.trim();

            if (paragraph.isEmpty())
            {
                wrappedParagraphs.add({});
                continue;
            }

            juce::StringArray words;
            words.addTokens(paragraph, " ", {});
            words.removeEmptyStrings();

            juce::String currentLine;
            juce::StringArray lines;

            for (const auto& word : words)
            {
                const auto candidate = currentLine.isEmpty() ? word : currentLine + " " + word;

                if (! currentLine.isEmpty() && candidate.length() > maxCharsPerLine)
                {
                    lines.add(currentLine);
                    currentLine = word;
                }
                else
                {
                    currentLine = candidate;
                }
            }

            if (currentLine.isNotEmpty())
                lines.add(currentLine);

            wrappedParagraphs.add(lines.joinIntoString("\n"));
        }

        return wrappedParagraphs.joinIntoString("\n");
    }

    juce::String makeTooltipText(juce::String title, juce::String body)
    {
        return title.toUpperCase() + "\n" + wrapTooltipBody(body);
    }

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
    , visualizer(processor.getValueTreeState())
    , pianoBar(processor.getKeyboardState())
{
    const bool isStandalone = juce::JUCEApplicationBase::isStandaloneApp();
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
    addSliderControl(arpTempoKnob, arpTempoAttachment, ids::arpInternalTempoBpm);
    addAndMakeVisible(arpRateChoice);
    attachChoiceControl(arpRateChoice, arpRateAttachment, parameterRefs.arpRate);
    addAndMakeVisible(arpPatternChoice);
    attachChoiceControl(arpPatternChoice, arpPatternAttachment, parameterRefs.arpPattern);
    addAndMakeVisible(arpOctaveChoice);
    attachChoiceControl(arpOctaveChoice, arpOctaveAttachment, parameterRefs.arpOctave);
    addSliderControl(arpGateKnob, arpGateAttachment, ids::arpGate);
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
    registerLearnableControl(arpTempoKnob, ids::arpInternalTempoBpm, "Tempo", [&](bool a, juce::String b) { applyKnobState(arpTempoKnob, a, b); });
    registerLearnableControl(arpRateChoice, ids::arpRateDivision, "Rate", [&](bool a, juce::String b) { applyChoiceState(arpRateChoice, a, b); });
    registerLearnableControl(arpPatternChoice, ids::arpPattern, "Pattern", [&](bool a, juce::String b) { applyChoiceState(arpPatternChoice, a, b); });
    registerLearnableControl(arpOctaveChoice, ids::arpOctaveRange, "Octave", [&](bool a, juce::String b) { applyChoiceState(arpOctaveChoice, a, b); });
    registerLearnableControl(arpGateKnob, ids::arpGate, "Gate", [&](bool a, juce::String b) { applyKnobState(arpGateKnob, a, b); });
    registerLearnableControl(arpLatchToggle, ids::arpLatch, "Latch", [&](bool a, juce::String b) { applyToggleState(arpLatchToggle, a, b); });
    registerLearnableControl(drvOnToggle, ids::driveEnabled, "Drive", [&](bool a, juce::String b) { applyToggleState(drvOnToggle, a, b); });
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

    coolsynth::ui::applyGreenActionButtonStyle(initPatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(savePatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(loadPatchButton, "patchButton");
    coolsynth::ui::applyGreenActionButtonStyle(allNotesOffButton, "panicButton");
    coolsynth::ui::applyGreenActionButtonStyle(tooltipToggleButton, "tooltipToggleButton");
    allNotesOffButton.setButtonText({});
    allNotesOffButton.onClick = [this] { processor.requestPanic(); };
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

    if (juce::JUCEApplicationBase::isStandaloneApp())
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

        setSize(1400, 628);
    }
    else
    {
        setSize(1400, 600);
    }

    tooltipLookAndFeel = std::make_unique<EditorTooltipLookAndFeel>();
    tooltipWindow = std::make_unique<EditorTooltipWindow>(this, 500);
    static_cast<EditorTooltipWindow*>(tooltipWindow.get())->isEnabledProvider = [this] { return tooltipsEnabled; };
    tooltipWindow->setLookAndFeel(tooltipLookAndFeel.get());

    oscSection.setTooltip(makeTooltipText("Oscillators",
                                          "Choose the source tone for oscillator A and B.\n"
                                          "This section sets waveform, pitch, pulse width,\n"
                                          "sync, and low-frequency mode."));
    mixSection.setTooltip(makeTooltipText("Mixer",
                                          "Blend oscillator A, oscillator B, and noise\n"
                                          "before the signal reaches the filter."));
    fltSection.setTooltip(makeTooltipText("Filter",
                                          "Shape brightness and emphasis in the main\n"
                                          "low-pass filter.\n"
                                          "Cutoff, resonance, envelope amount, and key\n"
                                          "tracking all meet here."));
    envSection.setTooltip(makeTooltipText("Envelopes",
                                          "Set the time shape for both the filter and amp.\n"
                                          "Attack, decay, sustain, and release define how\n"
                                          "each note starts, holds, and fades."));
    lfoSection.setTooltip(makeTooltipText("LFO",
                                          "Set the global low-frequency modulator.\n"
                                          "Choose its shape and route it to pitch,\n"
                                          "pulse width, and filter cutoff."));
    pmodSection.setTooltip(makeTooltipText("Poly Mod",
                                           "Route oscillator B or the filter envelope into\n"
                                           "extra modulation destinations.\n"
                                           "Use this for more aggressive or animated tones."));
    perfSection.setTooltip(makeTooltipText("Performance",
                                           "Configure how the synth responds while played.\n"
                                           "This includes glide, voice mode, note priority,\n"
                                           "bend range, drift, spread, and velocity."));
    arpSection.setTooltip(makeTooltipText("Arpeggiator",
                                          "Turn held notes into repeating patterns.\n"
                                          "Set tempo, timing division, pattern order,\n"
                                          "octave span, gate length, and latch behavior."));
    drvSection.setTooltip(makeTooltipText("Drive",
                                          "Add saturation before the time-based effects.\n"
                                          "Use amount and mix to control strength and blend."));
    choSection.setTooltip(makeTooltipText("Chorus",
                                          "Widen and thicken the sound with modulation.\n"
                                          "Rate controls movement, depth controls range,\n"
                                          "and mix controls blend."));
    dlySection.setTooltip(makeTooltipText("Delay",
                                          "Create repeating echoes after the dry signal.\n"
                                          "Time sets spacing, feedback sets repeats,\n"
                                          "and mix sets blend."));
    revSection.setTooltip(makeTooltipText("Reverb",
                                          "Add space and tail after the delay stage.\n"
                                          "Size sets the room, damping darkens the tail,\n"
                                          "and mix sets blend."));
    outSection.setTooltip(makeTooltipText("Output",
                                          "Set the final master level after the full\n"
                                          "effects chain.\n"
                                          "Use this to match loudness without changing\n"
                                          "the patch balance upstream."));

    setParameterTooltip(oscAWaveChoice, "Wave",
                        "Choose the basic tone shape for oscillator A.\n"
                        "Different waveforms change how bright, hollow,\n"
                        "or smooth the sound starts out.");
    oscAWaveChoice.setOptionTooltip(0, makeTooltipText("Pulse",
                                                       "Pulse is a hollow, buzzy waveform.\n"
                                                       "Its width can be changed for thinner or fatter tone.\n"
                                                       "Great for basses, leads, and animated movement."));
    oscAWaveChoice.setOptionTooltip(1, makeTooltipText("Tri - Triangle",
                                                       "Triangle sounds softer and rounder than saw or pulse.\n"
                                                       "Useful for gentle leads and smoother tones."));
    oscAWaveChoice.setOptionTooltip(2, makeTooltipText("Saw",
                                                       "Saw is bright and rich in harmonics.\n"
                                                       "It is the classic starting point for synth brass,\n"
                                                       "strings, basses, and many leads."));
    oscAWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                       "Sine is the smoothest and purest oscillator shape.\n"
                                                       "Use it for softer fundamentals, rounded support tone,\n"
                                                       "or cleaner low-end reinforcement."));
    setParameterTooltip(oscAOctaveKnob, "Octave",
                        "Move oscillator A up or down in whole octaves.\n"
                        "Use it to set the register before fine tuning.");
    setParameterTooltip(oscAFineKnob, "Fine",
                        "Fine tune oscillator A in small pitch steps.\n"
                        "Tiny offsets between oscillators add beating and width.");
    setParameterTooltip(oscAPwKnob, "Pulse Width",
                        "Change the width of oscillator A's pulse wave.\n"
                        "This matters most when the waveform is set to Pulse.");
    setParameterTooltip(oscASyncToggle, "Sync",
                        "Hard sync forces oscillator A to restart from oscillator B.\n"
                        "This gives sharper, more tearing sweeps when pitch moves.");
    setParameterTooltip(oscBWaveChoice, "Wave",
                        "Choose the basic tone shape for oscillator B.\n"
                        "This oscillator can act as a second voice or a modulator.");
    oscBWaveChoice.setOptionTooltip(0, makeTooltipText("Pulse",
                                                       "Pulse is a hollow, buzzy waveform.\n"
                                                       "Layer it with oscillator A for thicker classic synth tone.\n"
                                                       "Its width can also be modulated for animation."));
    oscBWaveChoice.setOptionTooltip(1, makeTooltipText("Tri - Triangle",
                                                       "Triangle has less bite than saw or pulse.\n"
                                                       "Good for support tone or gentler modulation."));
    oscBWaveChoice.setOptionTooltip(2, makeTooltipText("Saw",
                                                       "Saw is bright, full, and rich in harmonics.\n"
                                                       "Use it when you want edge, body,\n"
                                                       "or strong filter sweeps."));
    oscBWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                       "Sine is smooth and low in harmonics.\n"
                                                       "Useful when oscillator B should add weight,\n"
                                                       "behave gently, or modulate with a rounder shape."));
    setParameterTooltip(oscBOctaveKnob, "Octave",
                        "Move oscillator B up or down in whole octaves.\n"
                        "Use it to separate the two oscillators by register.");
    setParameterTooltip(oscBFineKnob, "Fine",
                        "Fine tune oscillator B in small pitch steps.\n"
                        "A slight detune makes the pair sound wider and thicker.");
    setParameterTooltip(oscBPwKnob, "Pulse Width",
                        "Change the width of oscillator B's pulse wave.\n"
                        "This only matters when oscillator B uses Pulse.");
    setParameterTooltip(oscBLoFreqToggle, "Lo Freq - Low Frequency",
                        "Oscillator B drops into a slow range for modulation\n"
                        "instead of acting like a normal pitched oscillator.");
    setParameterTooltip(mixOscAKnob, "Osc A",
                        "Set how much oscillator A reaches the mixer.\n"
                        "Turn it down if oscillator B or noise should dominate.");
    setParameterTooltip(mixOscBKnob, "Osc B",
                        "Set how much oscillator B reaches the mixer.\n"
                        "Blend it against oscillator A to build the core tone.");
    setParameterTooltip(mixNoiseKnob, "Noise",
                        "Add hiss and texture to the sound.\n"
                        "Small amounts can add bite without sounding obviously noisy.");
    setParameterTooltip(fltCutoffKnob, "Cutoff",
                        "Cutoff sets how much high end the low-pass filter removes.\n"
                        "Lower values sound darker. Higher values sound brighter.");
    setParameterTooltip(fltResKnob, "Resonance",
                        "Resonance emphasizes frequencies near the cutoff point.\n"
                        "It makes sweeps speak more clearly and can sound sharper.");
    setParameterTooltip(fltEnvAmtKnob, "Env Amt - Envelope Amount",
                        "It sets how far the filter envelope pushes the cutoff\n"
                        "each time a note starts.");
    setParameterTooltip(fltKeyTrkChoice, "Key Trk - Key Tracking",
                        "It decides how much higher notes open the filter more.");
    fltKeyTrkChoice.setOptionTooltip(0, makeTooltipText("Off",
                                                        "The filter stays at the same base brightness across the keyboard.\n"
                                                        "Higher notes will not automatically open the filter."));
    fltKeyTrkChoice.setOptionTooltip(1, makeTooltipText("Half",
                                                        "Higher notes open the filter a little as you play upward.\n"
                                                        "This keeps the keyboard more even without going too bright."));
    fltKeyTrkChoice.setOptionTooltip(2, makeTooltipText("Full",
                                                        "Higher notes open the filter strongly as pitch rises.\n"
                                                        "This helps preserve brightness across the keyboard range."));
    setParameterTooltip(fEnvAKnob, "F Atk - Filter Attack",
                        "This sets how long the filter takes to open after a note starts.");
    setParameterTooltip(fEnvDKnob, "F Dec - Filter Decay",
                        "This sets how fast the filter falls after the attack peak.");
    setParameterTooltip(fEnvSKnob, "F Sus - Filter Sustain",
                        "This is the filter level held while a key stays down.");
    setParameterTooltip(fEnvRKnob, "F Rel - Filter Release",
                        "This sets how long the filter takes to fade after key release.");
    setParameterTooltip(aEnvAKnob, "A Atk - Amp Attack",
                        "This controls how fast the note reaches full volume.");
    setParameterTooltip(aEnvDKnob, "A Dec - Amp Decay",
                        "This controls how fast the note falls from the peak level.");
    setParameterTooltip(aEnvSKnob, "A Sus - Amp Sustain",
                        "This is the level held while the key stays down.");
    setParameterTooltip(aEnvRKnob, "A Rel - Amp Release",
                        "This controls how long the note rings out after release.");
    setParameterTooltip(lfoWaveChoice, "Wave",
                        "Choose the shape of the global low-frequency oscillator.\n"
                        "That shape changes whether modulation feels smooth,\n"
                        "stepped, or slanted.");
    lfoWaveChoice.setOptionTooltip(0, makeTooltipText("Saw",
                                                      "A repeating ramp shape.\n"
                                                      "It creates a steady sweep or rise before snapping back."));
    lfoWaveChoice.setOptionTooltip(1, makeTooltipText("Tri",
                                                      "A smooth up-and-down shape.\n"
                                                      "This is the most even and gentle modulation option."));
    lfoWaveChoice.setOptionTooltip(2, makeTooltipText("Sqr - Square",
                                                      "This is a sharp on-off shape.\n"
                                                      "Use it for stepped vibrato, trill-like motion,\n"
                                                      "or abrupt filter changes."));
    lfoWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                      "A pure, smooth sine wave.\n"
                                                      "Perfect for gentle, organic vibrato or tremolo."));
    setParameterTooltip(lfoRateKnob, "Rate",
                        "Rate sets how fast the low-frequency oscillator cycles.\n"
                        "Faster values create quicker vibrato, wah,\n"
                        "or pulse-width movement.");
    setParameterTooltip(lfoMwDepKnob, "MW->Dep - Mod Wheel To Depth",
                        "This lets the mod wheel add more LFO amount while you play.");
    setParameterTooltip(lfoPitchKnob, "->Pitch",
                        "Send the LFO to oscillator pitch.\n"
                        "Small amounts create vibrato. Large amounts sound wilder.");
    setParameterTooltip(lfoPwKnob, "->PW - To Pulse Width",
                        "This sends the LFO to pulse width for animated hollow tone.");
    setParameterTooltip(lfoCutoffKnob, "->Cutoff",
                        "Send the LFO to filter cutoff.\n"
                        "Use it for repeating wah, sweep, or tremble-like brightness changes.");
    setParameterTooltip(pmodBPitchKnob, "B->Pitch - Oscillator B To Pitch",
                        "This lets oscillator B modulate pitch for harsher, richer motion.");
    setParameterTooltip(pmodBPwKnob, "B->PW - Oscillator B To Pulse Width",
                        "This lets oscillator B bend pulse width for complex movement.");
    setParameterTooltip(pmodBCutoffKnob, "B->Cutoff - Oscillator B To Cutoff",
                        "This lets oscillator B modulate filter cutoff for aggressive animation.");
    setParameterTooltip(pmodEPitchKnob, "E->Pitch - Envelope To Pitch",
                        "This sends the envelope to pitch for punch or attack snap.");
    setParameterTooltip(pmodEPwKnob, "E->PW - Envelope To Pulse Width",
                        "This makes pulse width change over the life of each note.");
    setParameterTooltip(pmodECutoffKnob, "E->Cutoff - Envelope To Cutoff",
                        "This adds extra envelope movement to the filter cutoff itself.");
    setParameterTooltip(perfGlideKnob, "Glide",
                        "Glide sets how long pitch takes to slide\n"
                        "from one note to the next.");
    setParameterTooltip(perfModeChoice, "Mode",
                        "Choose how voices are allocated while you play.\n"
                        "Poly plays chords, mono plays one note,\n"
                        "and unison stacks voices together.");
    perfModeChoice.setOptionTooltip(0, makeTooltipText("Poly",
                                                       "Each new note can sound with its own voice.\n"
                                                       "Best for chords and layered playing."));
    perfModeChoice.setOptionTooltip(1, makeTooltipText("Mono",
                                                       "Only one note sounds at a time.\n"
                                                       "Best for lead lines, basses, and glide playing."));
    perfModeChoice.setOptionTooltip(2, makeTooltipText("Unison",
                                                       "Multiple voices stack on the same note.\n"
                                                       "This makes the sound thicker, wider, and heavier."));
    setParameterTooltip(perfPrioChoice, "Priority",
                        "Priority matters in mono mode.\n"
                        "It chooses which held note wins when several keys are down.");
    perfPrioChoice.setOptionTooltip(0, makeTooltipText("Last",
                                                       "The newest held note wins.\n"
                                                       "This feels natural for most lead and solo playing."));
    perfPrioChoice.setOptionTooltip(1, makeTooltipText("Low",
                                                       "The lowest held note wins.\n"
                                                       "Useful for bass-oriented mono playing."));
    perfPrioChoice.setOptionTooltip(2, makeTooltipText("High",
                                                       "The highest held note wins.\n"
                                                       "Useful when you want upper notes to take over."));
    setParameterTooltip(perfPbRangeKnob, "PB Range - Pitch Bend Range",
                        "This sets how far the bend wheel can move the note.");
    setParameterTooltip(perfVintageKnob, "Vintage",
                        "Add controlled drift and slight mismatch between voices.\n"
                        "Higher values feel less perfect and more analog-like.");
    setParameterTooltip(perfPanKnob, "Pan Spread - Stereo Spread",
                        "This spreads different voices left and right for width.");
    setParameterTooltip(perfVelAmpKnob, "Vel->Amp - Velocity To Loudness",
                        "Higher values make harder key strikes play louder.");
    setParameterTooltip(perfVelFltKnob, "Vel->Flt - Velocity To Filter",
                        "Higher values make harder key strikes sound brighter.");
    setParameterTooltip(arpOnToggle, "Arp On",
                        "Turn the arpeggiator on or off.\n"
                        "When on, held notes are replayed as a repeating pattern.");
    setParameterTooltip(arpTempoKnob, "Tempo",
                        "Set the arpeggiator speed in beats per minute.\n"
                        "This is used when host timing is not driving the arp.");
    setParameterTooltip(arpRateChoice, "Rate",
                        "Choose the note division for each arpeggiated step.\n"
                        "Smaller fractions play faster.");
    arpRateChoice.setOptionTooltip(0, makeTooltipText("1/4",
                                                      "One arpeggiated step per quarter note.\n"
                                                      "Slow and spacious."));
    arpRateChoice.setOptionTooltip(1, makeTooltipText("1/8",
                                                      "One step per eighth note.\n"
                                                      "A common medium pulse."));
    arpRateChoice.setOptionTooltip(2, makeTooltipText("1/8T - Eighth-Note Triplet",
                                                      "This plays one step per eighth-note triplet\n"
                                                      "for a swinging three-part feel."));
    arpRateChoice.setOptionTooltip(3, makeTooltipText("1/16",
                                                      "One step per sixteenth note.\n"
                                                      "Fast and common for synth sequences."));
    arpRateChoice.setOptionTooltip(4, makeTooltipText("1/16T - Sixteenth-Note Triplet",
                                                      "This plays one step per sixteenth-note triplet\n"
                                                      "for a fast rolling feel."));
    arpRateChoice.setOptionTooltip(5, makeTooltipText("1/32",
                                                      "One step per thirty-second note.\n"
                                                      "Very fast and dense."));
    setParameterTooltip(arpPatternChoice, "Pattern",
                        "Choose the order in which held notes are replayed.\n"
                        "This changes the shape of the sequence without changing the notes.");
    arpPatternChoice.setOptionTooltip(0, makeTooltipText("Up",
                                                         "Plays held notes from low to high,\n"
                                                         "then repeats from the bottom."));
    arpPatternChoice.setOptionTooltip(1, makeTooltipText("Down",
                                                         "Plays held notes from high to low,\n"
                                                         "then repeats from the top."));
    arpPatternChoice.setOptionTooltip(2, makeTooltipText("Up/Dn - Up/Down",
                                                         "This climbs upward, then comes back down.\n"
                                                         "It gives a classic back-and-forth arp motion."));
    arpPatternChoice.setOptionTooltip(3, makeTooltipText("Play - As Played",
                                                         "It reuses the order you actually played the notes.\n"
                                                         "Good when finger order matters."));
    setParameterTooltip(arpOctaveChoice, "Octave",
                        "Choose how many octaves the arpeggiator will cover.\n"
                        "Larger ranges make the pattern feel more sweeping.");
    arpOctaveChoice.setOptionTooltip(1, makeTooltipText("1",
                                                        "Keep the arpeggio within the notes you played.\n"
                                                        "No extra octave repeats are added."));
    arpOctaveChoice.setOptionTooltip(2, makeTooltipText("2",
                                                        "Repeat the arpeggio across two octaves.\n"
                                                        "This makes the pattern feel larger and more animated."));
    arpOctaveChoice.setOptionTooltip(3, makeTooltipText("3",
                                                        "Repeat the arpeggio across three octaves.\n"
                                                        "This creates a broad, cascading motion."));
    setParameterTooltip(arpGateKnob, "Gate",
                        "Gate sets how long each arp note stays open.\n"
                        "Lower values sound choppier. Higher values sound more connected.");
    setParameterTooltip(arpLatchToggle, "Latch",
                        "Latch keeps the current held-note set playing\n"
                        "after you release the keys.");
    setParameterTooltip(drvOnToggle, "Drive",
                        "Turn the drive stage on or off.\n"
                        "Drive adds saturation before the time-based effects.");
    setParameterTooltip(drvAmtKnob, "Amount",
                        "Set how hard the drive stage pushes the signal.\n"
                        "More amount means more grit and saturation.");
    setParameterTooltip(drvMixKnob, "Mix",
                        "Blend the driven sound with the clean sound.\n"
                        "Lower values stay subtler. Higher values sound more processed.");
    setParameterTooltip(choOnToggle, "Chorus",
                        "Turn the chorus stage on or off.\n"
                        "Chorus thickens the sound by adding moving detuned copies.");
    setParameterTooltip(choRateKnob, "Rate",
                        "Set how fast the chorus motion cycles.\n"
                        "Slow values feel lush. Faster values feel more obvious.");
    setParameterTooltip(choDepKnob, "Depth",
                        "Depth sets how far the chorus modulation moves.\n"
                        "More depth gives a wider, more dramatic swirl.");
    setParameterTooltip(choMixKnob, "Mix",
                        "Blend the chorus signal with the dry signal.\n"
                        "Use this to keep width under control.");
    setParameterTooltip(dlyOnToggle, "Delay",
                        "Turn the delay stage on or off.\n"
                        "Delay creates repeating echoes after the dry sound.");
    setParameterTooltip(dlyTimeKnob, "Time",
                        "Time sets the space between echoes.\n"
                        "Short values feel tight. Long values feel spacious.");
    setParameterTooltip(dlyFdbkKnob, "Fdbk - Feedback",
                        "It feeds some of each echo back into the delay line\n"
                        "to create more repeats.");
    setParameterTooltip(dlyMixKnob, "Mix",
                        "Blend the echoes with the dry signal.\n"
                        "Lower values tuck delay behind the main sound.");
    setParameterTooltip(revOnToggle, "Reverb",
                        "Turn the reverb stage on or off.\n"
                        "Reverb adds room, air, and a fading tail.");
    setParameterTooltip(revSizeKnob, "Size",
                        "Size changes the apparent room or space.\n"
                        "Higher values sound larger and longer.");
    setParameterTooltip(revDampKnob, "Damp",
                        "Damp controls how quickly high frequencies fade in the tail.\n"
                        "Higher damping makes the reverb darker and softer.");
    setParameterTooltip(revMixKnob, "Mix",
                        "Blend the reverb with the dry sound.\n"
                        "Use small amounts to add space without washing out the patch.");
    setParameterTooltip(outGainKnob, "Master",
                        "Set the final output level of the whole synth.\n"
                        "Use this to match loudness after shaping the patch.");

    initPatchButton.setTooltip(makeTooltipText("Init Patch",
                                               "Reset the panel to the default patch.\n"
                                               "Useful when you want a clean baseline."));
    savePatchButton.setTooltip(makeTooltipText("Save Patch",
                                               "Write the current synth state to a\n"
                                               ".cspatch file for later recall."));
    loadPatchButton.setTooltip(makeTooltipText("Load Patch",
                                               "Load a saved .cspatch file into the synth.\n"
                                               "The current patch state will be replaced."));
    allNotesOffButton.setTooltip(makeTooltipText("All Notes Off",
                                                 "Immediate panic.\n"
                                                 "Stops active notes and clears any\n"
                                                 "stuck playing state."));
    tooltipToggleButton.setTooltip(makeTooltipText("i",
                                                   "Toggle hover help on or off.\n"
                                                   "Turn it off if you want a cleaner panel."));
    pianoBar.setTooltip(makeTooltipText("Keyboard / LED Strip",
                                        "Click to expand or collapse the keyboard area.\n"
                                        "The compact strip shows note activity.\n"
                                        "The expanded view gives octave controls."));

    startTimerHz(24);
    refreshMidiLearnVisuals();
    refreshValueDisplays();
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    if (standaloneMidiController != nullptr)
        standaloneMidiController->removeChangeListener(this);

    stopTimer();

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

void SynthAudioProcessorEditor::setParameterTooltip(juce::SettableTooltipClient& surface,
                                                    juce::String name,
                                                    juce::String description)
{
    auto body = std::move(description).trim();
    if (! body.endsWithChar('.'))
        body << ".";
    surface.setTooltip(makeTooltipText(std::move(name), std::move(body)));
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
        standaloneStatusBar->setBounds(bounds.removeFromBottom(28));
    }
    else
    {
        auto footerArea = bounds.removeFromBottom(30).reduced(12, 0);
        pluginStatusLabel.setBounds(footerArea.removeFromLeft(120));
        buildInfoLabel.setBounds(footerArea);
    }

    auto area = bounds.reduced(24);
    auto titleArea = area.removeFromTop(48);
    auto logoArea = titleArea.removeFromLeft(248);
    
    // 5 panes at 4:3 (48x36 each) + 4 gaps of 8px + 4px borders = 276px total
    // Shifted 60px right to clear logo tail space
    titleArea.removeFromLeft(60);
    visualizer.setBounds(titleArea.removeFromLeft(276).reduced(0, 4));

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
    midiLearnStatusLabel.setBounds(titleArea.removeFromLeft(250).withTrimmedTop(12));

    if (patchActionsVisible)
    {
        const auto buttonHeight = 24;
        const auto panicSize = 24;
        const auto tooltipButtonWidth = 24;
        const auto patchButtonWidth = 112;
        const auto gap = 4;
        const auto outerRightMargin = juce::JUCEApplicationBase::isStandaloneApp() ? 0 : 0;
        const auto totalWidth = panicSize + tooltipButtonWidth + (gap * 4) + (patchButtonWidth * 3);

        auto clusterArea = titleArea.removeFromRight(totalWidth + outerRightMargin);
        if (outerRightMargin > 0)
            clusterArea.removeFromRight(outerRightMargin);

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
        tooltipToggleButton.setBounds(titleArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        titleArea.removeFromRight(4);
        allNotesOffButton.setBounds(titleArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
    }
    area.removeFromTop(16);

    // Deck 1: Synth Core (Height: 240)
    auto synthRow = area.removeFromTop(240);
    
    // Oscillators (5 cols * 55 = 275)
    auto oscArea = synthRow.removeFromLeft(275);
    oscSection.setBounds(oscArea);
    auto oscContent = oscArea.reduced(12).withTrimmedTop(24);
    auto oscRow1 = oscContent.removeFromTop(oscContent.getHeight() / 2);
    auto oscRow2 = oscContent;
    float oscWeightsTop = 6.2f;
    oscAWaveChoice.setBounds(takeWeightedWidth(oscRow1, 2.2f, oscWeightsTop));
    oscAOctaveKnob.setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
    oscAFineKnob.setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
    oscAPwKnob.setBounds(takeWeightedWidth(oscRow1, 1.0f, oscWeightsTop));
    oscASyncToggle.setBounds(oscRow1);

    float oscWeightsBottom = 6.2f;
    oscBWaveChoice.setBounds(takeWeightedWidth(oscRow2, 2.2f, oscWeightsBottom));
    oscBOctaveKnob.setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
    oscBFineKnob.setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
    oscBPwKnob.setBounds(takeWeightedWidth(oscRow2, 1.0f, oscWeightsBottom));
    oscBLoFreqToggle.setBounds(oscRow2);

    synthRow.removeFromLeft(10); // gap

    // Mixer (2 cols * 55 = 110)
    auto mixArea = synthRow.removeFromLeft(110);
    mixSection.setBounds(mixArea);
    auto mixContent = mixArea.reduced(12).withTrimmedTop(24);
    auto mixRow1 = mixContent.removeFromTop(mixContent.getHeight() / 2);
    auto mixRow2 = mixContent;
    mixOscAKnob.setBounds(mixRow1.removeFromLeft(mixRow1.getWidth() / 2));
    mixOscBKnob.setBounds(mixRow1);
    mixNoiseKnob.setBounds(mixRow2.removeFromLeft(mixRow2.getWidth() / 2));

    synthRow.removeFromLeft(10); // gap

    // Filter (2 cols * 60 = 120)
    auto fltArea = synthRow.removeFromLeft(120);
    fltSection.setBounds(fltArea);
    auto fltContent = fltArea.reduced(12).withTrimmedTop(24);
    auto fltRow1 = fltContent.removeFromTop(fltContent.getHeight() / 2);
    auto fltRow2 = fltContent;
    fltCutoffKnob.setBounds(fltRow1.removeFromLeft(fltRow1.getWidth() / 2));
    fltResKnob.setBounds(fltRow1);
    fltEnvAmtKnob.setBounds(fltRow2.removeFromLeft(fltRow2.getWidth() / 2));
    fltKeyTrkChoice.setBounds(fltRow2);

    synthRow.removeFromLeft(10); // gap

    // Envelopes (4 cols * 55 = 220)
    auto envArea = synthRow.removeFromLeft(220);
    envSection.setBounds(envArea);
    auto envContent = envArea.reduced(12).withTrimmedTop(24);
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

    synthRow.removeFromLeft(10); // gap

    // LFO (3 cols * 55 = 165)
    auto lfoArea = synthRow.removeFromLeft(165);
    lfoSection.setBounds(lfoArea);
    auto lfoContent = lfoArea.reduced(12).withTrimmedTop(24);
    auto lfoRow1 = lfoContent.removeFromTop(lfoContent.getHeight() / 2);
    auto lfoRow2 = lfoContent;
    float lfoTopWeights = 4.0f;
    lfoWaveChoice.setBounds(takeWeightedWidth(lfoRow1, 2.0f, lfoTopWeights));
    lfoRateKnob.setBounds(takeWeightedWidth(lfoRow1, 1.0f, lfoTopWeights));
    lfoMwDepKnob.setBounds(lfoRow1);
    lfoPitchKnob.setBounds(lfoRow2.removeFromLeft(lfoRow2.getWidth() / 3));
    lfoPwKnob.setBounds(lfoRow2.removeFromLeft(lfoRow2.getWidth() / 2));
    lfoCutoffKnob.setBounds(lfoRow2);

    synthRow.removeFromLeft(10); // gap

    // Poly Mod (3 cols * 60 = 180)
    auto pmodArea = synthRow.removeFromLeft(180);
    pmodSection.setBounds(pmodArea);
    auto pmodContent = pmodArea.reduced(12).withTrimmedTop(24);
    auto pmodRow1 = pmodContent.removeFromTop(pmodContent.getHeight() / 2);
    auto pmodRow2 = pmodContent;
    pmodBPitchKnob.setBounds(pmodRow1.removeFromLeft(pmodRow1.getWidth() / 3));
    pmodBPwKnob.setBounds(pmodRow1.removeFromLeft(pmodRow1.getWidth() / 2));
    pmodBCutoffKnob.setBounds(pmodRow1);
    pmodEPitchKnob.setBounds(pmodRow2.removeFromLeft(pmodRow2.getWidth() / 3));
    pmodEPwKnob.setBounds(pmodRow2.removeFromLeft(pmodRow2.getWidth() / 2));
    pmodECutoffKnob.setBounds(pmodRow2);

    synthRow.removeFromLeft(10); // gap

    // Performance (4 cols * 55 = 220)
    auto perfArea = synthRow.removeFromLeft(220);
    perfSection.setBounds(perfArea);
    auto perfContent = perfArea.reduced(12).withTrimmedTop(24);
    auto perfRow1 = perfContent.removeFromTop(perfContent.getHeight() / 2);
    auto perfRow2 = perfContent;
    float perfTopWeights = 6.0f;
    perfGlideKnob.setBounds(takeWeightedWidth(perfRow1, 1.0f, perfTopWeights));
    perfModeChoice.setBounds(takeWeightedWidth(perfRow1, 2.0f, perfTopWeights));
    perfPrioChoice.setBounds(takeWeightedWidth(perfRow1, 2.0f, perfTopWeights));
    perfPbRangeKnob.setBounds(perfRow1);
    perfVintageKnob.setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 4));
    perfPanKnob.setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 3));
    perfVelAmpKnob.setBounds(perfRow2.removeFromLeft(perfRow2.getWidth() / 2));
    perfVelFltKnob.setBounds(perfRow2);

    area.removeFromTop(10); // Vertical gap between decks

    // Deck 2: Lower Deck (Height: 140)
    auto lowerRow = area.removeFromTop(140);

    // Arp (7 cols * 55 = 385)
    auto arpArea = lowerRow.removeFromLeft(385);
    arpSection.setBounds(arpArea);
    auto arpHeader = arpArea.reduced(12, 0).removeFromTop(32);
    arpOnToggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
    arpOnToggle.setBounds(arpHeader.removeFromRight(24).withSizeKeepingCentre(24, 24));
    auto arpContent = arpArea.reduced(12).withTrimmedTop(24);
    float arpWeights = 8.6f;
    arpTempoKnob.setBounds(takeWeightedWidth(arpContent, 1.2f, arpWeights));
    arpRateChoice.setBounds(takeWeightedWidth(arpContent, 2.2f, arpWeights));
    arpPatternChoice.setBounds(takeWeightedWidth(arpContent, 2.0f, arpWeights));
    arpOctaveChoice.setBounds(takeWeightedWidth(arpContent, 1.0f, arpWeights));
    arpGateKnob.setBounds(takeWeightedWidth(arpContent, 1.2f, arpWeights));
    arpLatchToggle.setBounds(arpContent.withWidth(48));

    lowerRow.removeFromLeft(10);

    // Drive (3 cols * 55 = 165)
    auto drvArea = lowerRow.removeFromLeft(165);
    drvSection.setBounds(drvArea);
    auto drvHeader = drvArea.reduced(12, 0).removeFromTop(32);
    drvOnToggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
    drvOnToggle.setBounds(drvHeader.removeFromRight(24).withSizeKeepingCentre(24, 24));
    auto drvContent = drvArea.reduced(12).withTrimmedTop(24);
    drvAmtKnob.setBounds(drvContent.removeFromLeft(drvContent.getWidth() / 2));
    drvMixKnob.setBounds(drvContent);

    lowerRow.removeFromLeft(10);

    // Chorus (4 cols * 55 = 220)
    auto choArea = lowerRow.removeFromLeft(220);
    choSection.setBounds(choArea);
    auto choHeader = choArea.reduced(12, 0).removeFromTop(32);
    choOnToggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
    choOnToggle.setBounds(choHeader.removeFromRight(24).withSizeKeepingCentre(24, 24));
    auto choContent = choArea.reduced(12).withTrimmedTop(24);
    choRateKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 3));
    choDepKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 2));
    choMixKnob.setBounds(choContent);

    lowerRow.removeFromLeft(10);

    // Delay (4 cols * 55 = 220)
    auto dlyArea = lowerRow.removeFromLeft(220);
    dlySection.setBounds(dlyArea);
    auto dlyHeader = dlyArea.reduced(12, 0).removeFromTop(32);
    dlyOnToggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
    dlyOnToggle.setBounds(dlyHeader.removeFromRight(24).withSizeKeepingCentre(24, 24));
    auto dlyContent = dlyArea.reduced(12).withTrimmedTop(24);
    dlyTimeKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 3));
    dlyFdbkKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 2));
    dlyMixKnob.setBounds(dlyContent);

    lowerRow.removeFromLeft(10);

    // Reverb (4 cols * 55 = 220)
    auto revArea = lowerRow.removeFromLeft(220);
    revSection.setBounds(revArea);
    auto revHeader = revArea.reduced(12, 0).removeFromTop(32);
    revOnToggle.setLayoutMode(coolsynth::ui::LedToggleButton::LayoutMode::compactHeader);
    revOnToggle.setBounds(revHeader.removeFromRight(24).withSizeKeepingCentre(24, 24));
    auto revContent = revArea.reduced(12).withTrimmedTop(24);
    revSizeKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 3));
    revDampKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 2));
    revMixKnob.setBounds(revContent);

    lowerRow.removeFromLeft(10);

    // Output (1 knob = 92)
    auto outArea = lowerRow.removeFromLeft(92);
    outSection.setBounds(outArea);
    auto outContent = outArea.reduced(12).withTrimmedTop(24);
    outGainKnob.setBounds(outContent);

    area.removeFromTop(16);
    pianoBar.setBounds(area.removeFromTop(pianoBar.getDesiredHeight()));
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
    arpTempoKnob.setValueText(getCurrentParameterText(parameterRefs.arpTempo));
    arpRateChoice.setValueText(getCurrentParameterText(parameterRefs.arpRate));
    arpPatternChoice.setValueText(getCurrentParameterText(parameterRefs.arpPattern));
    arpOctaveChoice.setValueText(getCurrentParameterText(parameterRefs.arpOctave));
    arpGateKnob.setValueText(getCurrentParameterText(parameterRefs.arpGate));
    arpLatchToggle.setValueText(getCurrentParameterText(parameterRefs.arpLatch));
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
