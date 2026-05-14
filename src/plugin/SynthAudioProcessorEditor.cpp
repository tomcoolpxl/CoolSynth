#include "SynthAudioProcessorEditor.h"

#include <array>

#include "BuildInfo.h"
#include "SynthAudioProcessor.h"
#include "midi/ControllerProfile.h"
#include "parameters/ParameterIDs.h"
#include "presets/PatchState.h"
#include "standalone/SettingsStore.h"
#include "standalone/StandaloneAudioSupport.h"
#include "ui/StandaloneSettingsDialog.h"
#include "ui/StandaloneStatusBar.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
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

    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    midiLearnStatusLabel.setText("", juce::dontSendNotification);
    midiLearnStatusLabel.setFont(juce::FontOptions(14.0f));
    midiLearnStatusLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    midiLearnStatusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(midiLearnStatusLabel);

    pluginStatusLabel.setText("Plugin Build", juce::dontSendNotification);
    pluginStatusLabel.setFont(juce::FontOptions(12.0f));
    pluginStatusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    pluginStatusLabel.setJustificationType(juce::Justification::centredLeft);
    pluginStatusLabel.setVisible(!isStandalone);
    addAndMakeVisible(pluginStatusLabel);

    buildInfoLabel.setText(coolsynth::build::getBuildIdentity(), juce::dontSendNotification);
    buildInfoLabel.setFont(juce::FontOptions(13.0f));
    buildInfoLabel.setColour(juce::Label::textColourId, juce::Colours::white);
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

    addAndMakeVisible(oscAWaveKnob);
    oscAWaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscAWave, oscAWaveKnob.slider());
    addAndMakeVisible(oscAOctaveKnob);
    oscAOctaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscAOctave, oscAOctaveKnob.slider());
    addAndMakeVisible(oscAFineKnob);
    oscAFineAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscAFineCents, oscAFineKnob.slider());
    addAndMakeVisible(oscAPwKnob);
    oscAPwAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscAPulseWidth, oscAPwKnob.slider());
    addAndMakeVisible(oscASyncKnob);
    oscASyncAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscASyncEnabled, oscASyncKnob.slider());
    addAndMakeVisible(oscBWaveKnob);
    oscBWaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBWave, oscBWaveKnob.slider());
    addAndMakeVisible(oscBOctaveKnob);
    oscBOctaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBOctave, oscBOctaveKnob.slider());
    addAndMakeVisible(oscBFineKnob);
    oscBFineAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBFineCents, oscBFineKnob.slider());
    addAndMakeVisible(oscBPwKnob);
    oscBPwAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBPulseWidth, oscBPwKnob.slider());
    addAndMakeVisible(oscBLoFreqKnob);
    oscBLoFreqAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBLowFrequencyMode, oscBLoFreqKnob.slider());
    addAndMakeVisible(mixOscAKnob);
    mixOscAAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscALevel, mixOscAKnob.slider());
    addAndMakeVisible(mixOscBKnob);
    mixOscBAttachment = std::make_unique<SliderAttachment>(apvts, ids::oscBLevel, mixOscBKnob.slider());
    addAndMakeVisible(mixNoiseKnob);
    mixNoiseAttachment = std::make_unique<SliderAttachment>(apvts, ids::noiseLevel, mixNoiseKnob.slider());
    addAndMakeVisible(fltCutoffKnob);
    fltCutoffAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterCutoffHz, fltCutoffKnob.slider());
    addAndMakeVisible(fltResKnob);
    fltResAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterResonance, fltResKnob.slider());
    addAndMakeVisible(fltEnvAmtKnob);
    fltEnvAmtAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterEnvAmount, fltEnvAmtKnob.slider());
    addAndMakeVisible(fltKeyTrkKnob);
    fltKeyTrkAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterKeyTracking, fltKeyTrkKnob.slider());
    addAndMakeVisible(fEnvAKnob);
    fEnvAAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterAttackMs, fEnvAKnob.slider());
    addAndMakeVisible(fEnvDKnob);
    fEnvDAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterDecayMs, fEnvDKnob.slider());
    addAndMakeVisible(fEnvSKnob);
    fEnvSAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterSustain, fEnvSKnob.slider());
    addAndMakeVisible(fEnvRKnob);
    fEnvRAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterReleaseMs, fEnvRKnob.slider());
    addAndMakeVisible(aEnvAKnob);
    aEnvAAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampAttackMs, aEnvAKnob.slider());
    addAndMakeVisible(aEnvDKnob);
    aEnvDAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampDecayMs, aEnvDKnob.slider());
    addAndMakeVisible(aEnvSKnob);
    aEnvSAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampSustain, aEnvSKnob.slider());
    addAndMakeVisible(aEnvRKnob);
    aEnvRAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampReleaseMs, aEnvRKnob.slider());
    addAndMakeVisible(lfoRateKnob);
    lfoRateAttachment = std::make_unique<SliderAttachment>(apvts, ids::lfoRateHz, lfoRateKnob.slider());
    addAndMakeVisible(lfoWaveKnob);
    lfoWaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::lfoWave, lfoWaveKnob.slider());
    addAndMakeVisible(lfoMwDepKnob);
    lfoMwDepAttachment = std::make_unique<SliderAttachment>(apvts, ids::modWheelToLfoDepth, lfoMwDepKnob.slider());
    addAndMakeVisible(lfoPitchKnob);
    lfoPitchAttachment = std::make_unique<SliderAttachment>(apvts, ids::lfoToOscPitch, lfoPitchKnob.slider());
    addAndMakeVisible(lfoPwKnob);
    lfoPwAttachment = std::make_unique<SliderAttachment>(apvts, ids::lfoToPulseWidth, lfoPwKnob.slider());
    addAndMakeVisible(lfoCutoffKnob);
    lfoCutoffAttachment = std::make_unique<SliderAttachment>(apvts, ids::lfoToFilterCutoff, lfoCutoffKnob.slider());
    addAndMakeVisible(pmodBPitchKnob);
    pmodBPitchAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModOscBToOscPitch, pmodBPitchKnob.slider());
    addAndMakeVisible(pmodBPwKnob);
    pmodBPwAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModOscBToPulseWidth, pmodBPwKnob.slider());
    addAndMakeVisible(pmodBCutoffKnob);
    pmodBCutoffAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModOscBToFilterCutoff, pmodBCutoffKnob.slider());
    addAndMakeVisible(pmodEPitchKnob);
    pmodEPitchAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModEnvToOscPitch, pmodEPitchKnob.slider());
    addAndMakeVisible(pmodEPwKnob);
    pmodEPwAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModEnvToPulseWidth, pmodEPwKnob.slider());
    addAndMakeVisible(pmodECutoffKnob);
    pmodECutoffAttachment = std::make_unique<SliderAttachment>(apvts, ids::polyModEnvToFilterCutoff, pmodECutoffKnob.slider());
    addAndMakeVisible(perfGlideKnob);
    perfGlideAttachment = std::make_unique<SliderAttachment>(apvts, ids::glideTimeMs, perfGlideKnob.slider());
    addAndMakeVisible(perfModeKnob);
    perfModeAttachment = std::make_unique<SliderAttachment>(apvts, ids::playMode, perfModeKnob.slider());
    addAndMakeVisible(perfPrioKnob);
    perfPrioAttachment = std::make_unique<SliderAttachment>(apvts, ids::keyPriority, perfPrioKnob.slider());
    addAndMakeVisible(perfPbRangeKnob);
    perfPbRangeAttachment = std::make_unique<SliderAttachment>(apvts, ids::pitchBendRangeSemitones, perfPbRangeKnob.slider());
    addAndMakeVisible(perfVintageKnob);
    perfVintageAttachment = std::make_unique<SliderAttachment>(apvts, ids::vintageAmount, perfVintageKnob.slider());
    addAndMakeVisible(perfPanKnob);
    perfPanAttachment = std::make_unique<SliderAttachment>(apvts, ids::panSpread, perfPanKnob.slider());
    addAndMakeVisible(perfVelAmpKnob);
    perfVelAmpAttachment = std::make_unique<SliderAttachment>(apvts, ids::velocityToAmp, perfVelAmpKnob.slider());
    addAndMakeVisible(perfVelFltKnob);
    perfVelFltAttachment = std::make_unique<SliderAttachment>(apvts, ids::velocityToFilter, perfVelFltKnob.slider());
    addAndMakeVisible(arpOnKnob);
    arpOnAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpEnabled, arpOnKnob.slider());
    addAndMakeVisible(arpTempoKnob);
    arpTempoAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpInternalTempoBpm, arpTempoKnob.slider());
    addAndMakeVisible(arpRateKnob);
    arpRateAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpRateDivision, arpRateKnob.slider());
    addAndMakeVisible(arpPatternKnob);
    arpPatternAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpPattern, arpPatternKnob.slider());
    addAndMakeVisible(arpOctaveKnob);
    arpOctaveAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpOctaveRange, arpOctaveKnob.slider());
    addAndMakeVisible(arpGateKnob);
    arpGateAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpGate, arpGateKnob.slider());
    addAndMakeVisible(arpLatchKnob);
    arpLatchAttachment = std::make_unique<SliderAttachment>(apvts, ids::arpLatch, arpLatchKnob.slider());
    addAndMakeVisible(drvOnKnob);
    drvOnAttachment = std::make_unique<SliderAttachment>(apvts, ids::driveEnabled, drvOnKnob.slider());
    addAndMakeVisible(drvAmtKnob);
    drvAmtAttachment = std::make_unique<SliderAttachment>(apvts, ids::driveAmount, drvAmtKnob.slider());
    addAndMakeVisible(drvMixKnob);
    drvMixAttachment = std::make_unique<SliderAttachment>(apvts, ids::driveMix, drvMixKnob.slider());
    addAndMakeVisible(choOnKnob);
    choOnAttachment = std::make_unique<SliderAttachment>(apvts, ids::chorusEnabled, choOnKnob.slider());
    addAndMakeVisible(choRateKnob);
    choRateAttachment = std::make_unique<SliderAttachment>(apvts, ids::chorusRateHz, choRateKnob.slider());
    addAndMakeVisible(choDepKnob);
    choDepAttachment = std::make_unique<SliderAttachment>(apvts, ids::chorusDepth, choDepKnob.slider());
    addAndMakeVisible(choMixKnob);
    choMixAttachment = std::make_unique<SliderAttachment>(apvts, ids::chorusMix, choMixKnob.slider());
    addAndMakeVisible(dlyOnKnob);
    dlyOnAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayEnabled, dlyOnKnob.slider());
    addAndMakeVisible(dlyTimeKnob);
    dlyTimeAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayTimeMs, dlyTimeKnob.slider());
    addAndMakeVisible(dlyFdbkKnob);
    dlyFdbkAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayFeedback, dlyFdbkKnob.slider());
    addAndMakeVisible(dlyMixKnob);
    dlyMixAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayMix, dlyMixKnob.slider());
    addAndMakeVisible(revOnKnob);
    revOnAttachment = std::make_unique<SliderAttachment>(apvts, ids::reverbEnabled, revOnKnob.slider());
    addAndMakeVisible(revSizeKnob);
    revSizeAttachment = std::make_unique<SliderAttachment>(apvts, ids::reverbSize, revSizeKnob.slider());
    addAndMakeVisible(revDampKnob);
    revDampAttachment = std::make_unique<SliderAttachment>(apvts, ids::reverbDamping, revDampKnob.slider());
    addAndMakeVisible(revMixKnob);
    revMixAttachment = std::make_unique<SliderAttachment>(apvts, ids::reverbMix, revMixKnob.slider());
    addAndMakeVisible(outGainFader);
    outGainAttachment = std::make_unique<SliderAttachment>(apvts, ids::masterGainDb, outGainFader.slider());

    
    auto applyKnobState = [](coolsynth::ui::HardwareKnob& knob, bool armed, juce::String badge)
    {
        knob.setLearnState(armed, badge);
    };
    auto applyFaderState = [](coolsynth::ui::HardwareFader& fader, bool armed, juce::String badge)
    {
        fader.setLearnState(armed, badge);
    };

    registerLearnableControl(oscAWaveKnob, ids::oscAWave, "Wave", [&](bool a, juce::String b) { applyKnobState(oscAWaveKnob, a, b); });
    registerLearnableControl(oscAOctaveKnob, ids::oscAOctave, "Octave", [&](bool a, juce::String b) { applyKnobState(oscAOctaveKnob, a, b); });
    registerLearnableControl(oscAFineKnob, ids::oscAFineCents, "Fine", [&](bool a, juce::String b) { applyKnobState(oscAFineKnob, a, b); });
    registerLearnableControl(oscAPwKnob, ids::oscAPulseWidth, "Pulse W", [&](bool a, juce::String b) { applyKnobState(oscAPwKnob, a, b); });
    registerLearnableControl(oscASyncKnob, ids::oscASyncEnabled, "Sync", [&](bool a, juce::String b) { applyKnobState(oscASyncKnob, a, b); });
    registerLearnableControl(oscBWaveKnob, ids::oscBWave, "Wave", [&](bool a, juce::String b) { applyKnobState(oscBWaveKnob, a, b); });
    registerLearnableControl(oscBOctaveKnob, ids::oscBOctave, "Octave", [&](bool a, juce::String b) { applyKnobState(oscBOctaveKnob, a, b); });
    registerLearnableControl(oscBFineKnob, ids::oscBFineCents, "Fine", [&](bool a, juce::String b) { applyKnobState(oscBFineKnob, a, b); });
    registerLearnableControl(oscBPwKnob, ids::oscBPulseWidth, "Pulse W", [&](bool a, juce::String b) { applyKnobState(oscBPwKnob, a, b); });
    registerLearnableControl(oscBLoFreqKnob, ids::oscBLowFrequencyMode, "Lo Freq", [&](bool a, juce::String b) { applyKnobState(oscBLoFreqKnob, a, b); });
    registerLearnableControl(mixOscAKnob, ids::oscALevel, "Osc A", [&](bool a, juce::String b) { applyKnobState(mixOscAKnob, a, b); });
    registerLearnableControl(mixOscBKnob, ids::oscBLevel, "Osc B", [&](bool a, juce::String b) { applyKnobState(mixOscBKnob, a, b); });
    registerLearnableControl(mixNoiseKnob, ids::noiseLevel, "Noise", [&](bool a, juce::String b) { applyKnobState(mixNoiseKnob, a, b); });
    registerLearnableControl(fltCutoffKnob, ids::filterCutoffHz, "Cutoff", [&](bool a, juce::String b) { applyKnobState(fltCutoffKnob, a, b); });
    registerLearnableControl(fltResKnob, ids::filterResonance, "Resonance", [&](bool a, juce::String b) { applyKnobState(fltResKnob, a, b); });
    registerLearnableControl(fltEnvAmtKnob, ids::filterEnvAmount, "Env Amt", [&](bool a, juce::String b) { applyKnobState(fltEnvAmtKnob, a, b); });
    registerLearnableControl(fltKeyTrkKnob, ids::filterKeyTracking, "Key Trk", [&](bool a, juce::String b) { applyKnobState(fltKeyTrkKnob, a, b); });
    registerLearnableControl(fEnvAKnob, ids::filterAttackMs, "F Atk", [&](bool a, juce::String b) { applyKnobState(fEnvAKnob, a, b); });
    registerLearnableControl(fEnvDKnob, ids::filterDecayMs, "F Dec", [&](bool a, juce::String b) { applyKnobState(fEnvDKnob, a, b); });
    registerLearnableControl(fEnvSKnob, ids::filterSustain, "F Sus", [&](bool a, juce::String b) { applyKnobState(fEnvSKnob, a, b); });
    registerLearnableControl(fEnvRKnob, ids::filterReleaseMs, "F Rel", [&](bool a, juce::String b) { applyKnobState(fEnvRKnob, a, b); });
    registerLearnableControl(aEnvAKnob, ids::ampAttackMs, "A Atk", [&](bool a, juce::String b) { applyKnobState(aEnvAKnob, a, b); });
    registerLearnableControl(aEnvDKnob, ids::ampDecayMs, "A Dec", [&](bool a, juce::String b) { applyKnobState(aEnvDKnob, a, b); });
    registerLearnableControl(aEnvSKnob, ids::ampSustain, "A Sus", [&](bool a, juce::String b) { applyKnobState(aEnvSKnob, a, b); });
    registerLearnableControl(aEnvRKnob, ids::ampReleaseMs, "A Rel", [&](bool a, juce::String b) { applyKnobState(aEnvRKnob, a, b); });
    registerLearnableControl(lfoRateKnob, ids::lfoRateHz, "Rate", [&](bool a, juce::String b) { applyKnobState(lfoRateKnob, a, b); });
    registerLearnableControl(lfoWaveKnob, ids::lfoWave, "Wave", [&](bool a, juce::String b) { applyKnobState(lfoWaveKnob, a, b); });
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
    registerLearnableControl(perfModeKnob, ids::playMode, "Mode", [&](bool a, juce::String b) { applyKnobState(perfModeKnob, a, b); });
    registerLearnableControl(perfPrioKnob, ids::keyPriority, "Priority", [&](bool a, juce::String b) { applyKnobState(perfPrioKnob, a, b); });
    registerLearnableControl(perfPbRangeKnob, ids::pitchBendRangeSemitones, "PB Range", [&](bool a, juce::String b) { applyKnobState(perfPbRangeKnob, a, b); });
    registerLearnableControl(perfVintageKnob, ids::vintageAmount, "Vintage", [&](bool a, juce::String b) { applyKnobState(perfVintageKnob, a, b); });
    registerLearnableControl(perfPanKnob, ids::panSpread, "Pan Spread", [&](bool a, juce::String b) { applyKnobState(perfPanKnob, a, b); });
    registerLearnableControl(perfVelAmpKnob, ids::velocityToAmp, "Vel->Amp", [&](bool a, juce::String b) { applyKnobState(perfVelAmpKnob, a, b); });
    registerLearnableControl(perfVelFltKnob, ids::velocityToFilter, "Vel->Flt", [&](bool a, juce::String b) { applyKnobState(perfVelFltKnob, a, b); });
    registerLearnableControl(arpOnKnob, ids::arpEnabled, "On", [&](bool a, juce::String b) { applyKnobState(arpOnKnob, a, b); });
    registerLearnableControl(arpTempoKnob, ids::arpInternalTempoBpm, "Tempo", [&](bool a, juce::String b) { applyKnobState(arpTempoKnob, a, b); });
    registerLearnableControl(arpRateKnob, ids::arpRateDivision, "Rate", [&](bool a, juce::String b) { applyKnobState(arpRateKnob, a, b); });
    registerLearnableControl(arpPatternKnob, ids::arpPattern, "Pattern", [&](bool a, juce::String b) { applyKnobState(arpPatternKnob, a, b); });
    registerLearnableControl(arpOctaveKnob, ids::arpOctaveRange, "Octave", [&](bool a, juce::String b) { applyKnobState(arpOctaveKnob, a, b); });
    registerLearnableControl(arpGateKnob, ids::arpGate, "Gate", [&](bool a, juce::String b) { applyKnobState(arpGateKnob, a, b); });
    registerLearnableControl(arpLatchKnob, ids::arpLatch, "Latch", [&](bool a, juce::String b) { applyKnobState(arpLatchKnob, a, b); });
    registerLearnableControl(drvOnKnob, ids::driveEnabled, "On", [&](bool a, juce::String b) { applyKnobState(drvOnKnob, a, b); });
    registerLearnableControl(drvAmtKnob, ids::driveAmount, "Amount", [&](bool a, juce::String b) { applyKnobState(drvAmtKnob, a, b); });
    registerLearnableControl(drvMixKnob, ids::driveMix, "Mix", [&](bool a, juce::String b) { applyKnobState(drvMixKnob, a, b); });
    registerLearnableControl(choOnKnob, ids::chorusEnabled, "On", [&](bool a, juce::String b) { applyKnobState(choOnKnob, a, b); });
    registerLearnableControl(choRateKnob, ids::chorusRateHz, "Rate", [&](bool a, juce::String b) { applyKnobState(choRateKnob, a, b); });
    registerLearnableControl(choDepKnob, ids::chorusDepth, "Depth", [&](bool a, juce::String b) { applyKnobState(choDepKnob, a, b); });
    registerLearnableControl(choMixKnob, ids::chorusMix, "Mix", [&](bool a, juce::String b) { applyKnobState(choMixKnob, a, b); });
    registerLearnableControl(dlyOnKnob, ids::delayEnabled, "On", [&](bool a, juce::String b) { applyKnobState(dlyOnKnob, a, b); });
    registerLearnableControl(dlyTimeKnob, ids::delayTimeMs, "Time", [&](bool a, juce::String b) { applyKnobState(dlyTimeKnob, a, b); });
    registerLearnableControl(dlyFdbkKnob, ids::delayFeedback, "Fdbk", [&](bool a, juce::String b) { applyKnobState(dlyFdbkKnob, a, b); });
    registerLearnableControl(dlyMixKnob, ids::delayMix, "Mix", [&](bool a, juce::String b) { applyKnobState(dlyMixKnob, a, b); });
    registerLearnableControl(revOnKnob, ids::reverbEnabled, "On", [&](bool a, juce::String b) { applyKnobState(revOnKnob, a, b); });
    registerLearnableControl(revSizeKnob, ids::reverbSize, "Size", [&](bool a, juce::String b) { applyKnobState(revSizeKnob, a, b); });
    registerLearnableControl(revDampKnob, ids::reverbDamping, "Damp", [&](bool a, juce::String b) { applyKnobState(revDampKnob, a, b); });
    registerLearnableControl(revMixKnob, ids::reverbMix, "Mix", [&](bool a, juce::String b) { applyKnobState(revMixKnob, a, b); });
    registerLearnableControl(outGainFader, ids::masterGainDb, "Master", [&](bool a, juce::String b) { applyFaderState(outGainFader, a, b); });

    allNotesOffButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible(allNotesOffButton);

    addAndMakeVisible(pianoBar);

    midiLearnManager = std::make_unique<coolsynth::midi::MidiLearnManager>();
    midiLearnManager->replaceBindings(processor.getLearnedMidiBindings());

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        patchActionsVisible = true;

        initPatchButton.onClick = [this] { triggerInitPatch(); };
        savePatchButton.onClick = [this] { triggerSavePatch(); };
        loadPatchButton.onClick = [this] { triggerLoadPatch(); };

        addAndMakeVisible(initPatchButton);
        addAndMakeVisible(savePatchButton);
        addAndMakeVisible(loadPatchButton);

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

        standaloneStatusBar = std::make_unique<StandaloneStatusBar>(*standaloneMidiController);
        addAndMakeVisible(*standaloneStatusBar);

        setSize(1400, 628);
    }
    else
    {
        setSize(1400, 600);
    }

    startTimerHz(24);
    refreshMidiLearnVisuals();
    refreshValueDisplays();
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    if (standaloneMidiController != nullptr)
        standaloneMidiController->removeChangeListener(this);

    stopTimer();
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
    if (statusText.isEmpty() && standaloneMidiController != nullptr)
        statusText = "Profile: " + getResolvedStandaloneControllerProfileDisplayName();

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
    g.fillAll(juce::Colours::black);

    if (!juce::JUCEApplicationBase::isStandaloneApp())
    {
        auto footerArea = getLocalBounds().removeFromBottom(30);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(footerArea);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawLine(static_cast<float>(footerArea.getX()),
                   static_cast<float>(footerArea.getY()),
                   static_cast<float>(footerArea.getRight()),
                   static_cast<float>(footerArea.getY()),
                   1.0f);
    }
}

void SynthAudioProcessorEditor::resized()
{
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
    titleLabel.setBounds(titleArea.removeFromLeft(200));
    midiLearnStatusLabel.setBounds(titleArea.removeFromLeft(400).withTrimmedTop(12));
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        titleArea.removeFromRight(16);
    }
    allNotesOffButton.setBounds(titleArea.removeFromRight(120).withSizeKeepingCentre(100, 24));
    
    if (patchActionsVisible)
    {
        titleArea.removeFromRight(16);
        loadPatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
        titleArea.removeFromRight(8);
        savePatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
        titleArea.removeFromRight(8);
        initPatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
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
    oscAWaveKnob.setBounds(oscRow1.removeFromLeft(oscRow1.getWidth() / 5));
    oscAOctaveKnob.setBounds(oscRow1.removeFromLeft(oscRow1.getWidth() / 4));
    oscAFineKnob.setBounds(oscRow1.removeFromLeft(oscRow1.getWidth() / 3));
    oscAPwKnob.setBounds(oscRow1.removeFromLeft(oscRow1.getWidth() / 2));
    oscASyncKnob.setBounds(oscRow1);
    
    oscBWaveKnob.setBounds(oscRow2.removeFromLeft(oscRow2.getWidth() / 5));
    oscBOctaveKnob.setBounds(oscRow2.removeFromLeft(oscRow2.getWidth() / 4));
    oscBFineKnob.setBounds(oscRow2.removeFromLeft(oscRow2.getWidth() / 3));
    oscBPwKnob.setBounds(oscRow2.removeFromLeft(oscRow2.getWidth() / 2));
    oscBLoFreqKnob.setBounds(oscRow2);

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
    fltKeyTrkKnob.setBounds(fltRow2);

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
    lfoRateKnob.setBounds(lfoRow1.removeFromLeft(lfoRow1.getWidth() / 3));
    lfoWaveKnob.setBounds(lfoRow1.removeFromLeft(lfoRow1.getWidth() / 2));
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
    perfGlideKnob.setBounds(perfRow1.removeFromLeft(perfRow1.getWidth() / 4));
    perfModeKnob.setBounds(perfRow1.removeFromLeft(perfRow1.getWidth() / 3));
    perfPrioKnob.setBounds(perfRow1.removeFromLeft(perfRow1.getWidth() / 2));
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
    auto arpContent = arpArea.reduced(12).withTrimmedTop(24);
    arpOnKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 7));
    arpTempoKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 6));
    arpRateKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 5));
    arpPatternKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 4));
    arpOctaveKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 3));
    arpGateKnob.setBounds(arpContent.removeFromLeft(arpContent.getWidth() / 2));
    arpLatchKnob.setBounds(arpContent);

    lowerRow.removeFromLeft(10);

    // Drive (3 cols * 55 = 165)
    auto drvArea = lowerRow.removeFromLeft(165);
    drvSection.setBounds(drvArea);
    auto drvContent = drvArea.reduced(12).withTrimmedTop(24);
    drvOnKnob.setBounds(drvContent.removeFromLeft(drvContent.getWidth() / 3));
    drvAmtKnob.setBounds(drvContent.removeFromLeft(drvContent.getWidth() / 2));
    drvMixKnob.setBounds(drvContent);

    lowerRow.removeFromLeft(10);

    // Chorus (4 cols * 55 = 220)
    auto choArea = lowerRow.removeFromLeft(220);
    choSection.setBounds(choArea);
    auto choContent = choArea.reduced(12).withTrimmedTop(24);
    choOnKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 4));
    choRateKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 3));
    choDepKnob.setBounds(choContent.removeFromLeft(choContent.getWidth() / 2));
    choMixKnob.setBounds(choContent);

    lowerRow.removeFromLeft(10);

    // Delay (4 cols * 55 = 220)
    auto dlyArea = lowerRow.removeFromLeft(220);
    dlySection.setBounds(dlyArea);
    auto dlyContent = dlyArea.reduced(12).withTrimmedTop(24);
    dlyOnKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 4));
    dlyTimeKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 3));
    dlyFdbkKnob.setBounds(dlyContent.removeFromLeft(dlyContent.getWidth() / 2));
    dlyMixKnob.setBounds(dlyContent);

    lowerRow.removeFromLeft(10);

    // Reverb (4 cols * 55 = 220)
    auto revArea = lowerRow.removeFromLeft(220);
    revSection.setBounds(revArea);
    auto revContent = revArea.reduced(12).withTrimmedTop(24);
    revOnKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 4));
    revSizeKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 3));
    revDampKnob.setBounds(revContent.removeFromLeft(revContent.getWidth() / 2));
    revMixKnob.setBounds(revContent);

    lowerRow.removeFromLeft(10);

    // Output (1 fader = 80)
    auto outArea = lowerRow.removeFromLeft(80);
    outSection.setBounds(outArea);
    auto outContent = outArea.reduced(12).withTrimmedTop(24);
    outGainFader.setBounds(outContent);

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
    oscAWaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscAWave));
    oscAOctaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscAOctave));
    oscAFineKnob.setValueText(getCurrentParameterText(parameterRefs.oscAFine));
    oscAPwKnob.setValueText(getCurrentParameterText(parameterRefs.oscAPw));
    oscASyncKnob.setValueText(getCurrentParameterText(parameterRefs.oscASync));
    oscBWaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscBWave));
    oscBOctaveKnob.setValueText(getCurrentParameterText(parameterRefs.oscBOctave));
    oscBFineKnob.setValueText(getCurrentParameterText(parameterRefs.oscBFine));
    oscBPwKnob.setValueText(getCurrentParameterText(parameterRefs.oscBPw));
    oscBLoFreqKnob.setValueText(getCurrentParameterText(parameterRefs.oscBLoFreq));
    mixOscAKnob.setValueText(getCurrentParameterText(parameterRefs.mixOscA));
    mixOscBKnob.setValueText(getCurrentParameterText(parameterRefs.mixOscB));
    mixNoiseKnob.setValueText(getCurrentParameterText(parameterRefs.mixNoise));
    fltCutoffKnob.setValueText(getCurrentParameterText(parameterRefs.fltCutoff));
    fltResKnob.setValueText(getCurrentParameterText(parameterRefs.fltRes));
    fltEnvAmtKnob.setValueText(getCurrentParameterText(parameterRefs.fltEnvAmt));
    fltKeyTrkKnob.setValueText(getCurrentParameterText(parameterRefs.fltKeyTrk));
    fEnvAKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvA));
    fEnvDKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvD));
    fEnvSKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvS));
    fEnvRKnob.setValueText(getCurrentParameterText(parameterRefs.fEnvR));
    aEnvAKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvA));
    aEnvDKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvD));
    aEnvSKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvS));
    aEnvRKnob.setValueText(getCurrentParameterText(parameterRefs.aEnvR));
    lfoRateKnob.setValueText(getCurrentParameterText(parameterRefs.lfoRate));
    lfoWaveKnob.setValueText(getCurrentParameterText(parameterRefs.lfoWave));
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
    perfModeKnob.setValueText(getCurrentParameterText(parameterRefs.perfMode));
    perfPrioKnob.setValueText(getCurrentParameterText(parameterRefs.perfPrio));
    perfPbRangeKnob.setValueText(getCurrentParameterText(parameterRefs.perfPbRange));
    perfVintageKnob.setValueText(getCurrentParameterText(parameterRefs.perfVintage));
    perfPanKnob.setValueText(getCurrentParameterText(parameterRefs.perfPan));
    perfVelAmpKnob.setValueText(getCurrentParameterText(parameterRefs.perfVelAmp));
    perfVelFltKnob.setValueText(getCurrentParameterText(parameterRefs.perfVelFlt));
    arpOnKnob.setValueText(getCurrentParameterText(parameterRefs.arpOn));
    arpTempoKnob.setValueText(getCurrentParameterText(parameterRefs.arpTempo));
    arpRateKnob.setValueText(getCurrentParameterText(parameterRefs.arpRate));
    arpPatternKnob.setValueText(getCurrentParameterText(parameterRefs.arpPattern));
    arpOctaveKnob.setValueText(getCurrentParameterText(parameterRefs.arpOctave));
    arpGateKnob.setValueText(getCurrentParameterText(parameterRefs.arpGate));
    arpLatchKnob.setValueText(getCurrentParameterText(parameterRefs.arpLatch));
    drvOnKnob.setValueText(getCurrentParameterText(parameterRefs.drvOn));
    drvAmtKnob.setValueText(getCurrentParameterText(parameterRefs.drvAmt));
    drvMixKnob.setValueText(getCurrentParameterText(parameterRefs.drvMix));
    choOnKnob.setValueText(getCurrentParameterText(parameterRefs.choOn));
    choRateKnob.setValueText(getCurrentParameterText(parameterRefs.choRate));
    choDepKnob.setValueText(getCurrentParameterText(parameterRefs.choDep));
    choMixKnob.setValueText(getCurrentParameterText(parameterRefs.choMix));
    dlyOnKnob.setValueText(getCurrentParameterText(parameterRefs.dlyOn));
    dlyTimeKnob.setValueText(getCurrentParameterText(parameterRefs.dlyTime));
    dlyFdbkKnob.setValueText(getCurrentParameterText(parameterRefs.dlyFdbk));
    dlyMixKnob.setValueText(getCurrentParameterText(parameterRefs.dlyMix));
    revOnKnob.setValueText(getCurrentParameterText(parameterRefs.revOn));
    revSizeKnob.setValueText(getCurrentParameterText(parameterRefs.revSize));
    revDampKnob.setValueText(getCurrentParameterText(parameterRefs.revDamp));
    revMixKnob.setValueText(getCurrentParameterText(parameterRefs.revMix));
    outGainFader.setValueText(getCurrentParameterText(parameterRefs.outGain));
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
        "Save Patch", juce::File(), "*" + juce::String(coolsynth::presets::defaultPatchExtension));

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
        "Load Patch", juce::File(), "*" + juce::String(coolsynth::presets::defaultPatchExtension));

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
