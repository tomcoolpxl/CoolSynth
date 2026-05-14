#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/HardwareFader.h"
#include "ui/HardwareKnob.h"
#include "ui/SynthSection.h"
#include "ui/PianoBarComponent.h"
#include "midi/MidiLearn.h"

class SynthAudioProcessor;

namespace coolsynth::standalone
{
    class StandaloneMidiInputController;
}

class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        private juce::ChangeListener
{
public:
    explicit SynthAudioProcessorEditor(SynthAudioProcessor& processor);
    ~SynthAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    int getControlParameterIndex(juce::Component& component) override;
    void refreshStandaloneControllerProfileSelection();
    juce::String getResolvedStandaloneControllerProfileDisplayName() const;
    void resetStandaloneMidiSettings();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Generated Code
    struct ParameterRefs {
        juce::RangedAudioParameter* oscAWave = nullptr;
        juce::RangedAudioParameter* oscAOctave = nullptr;
        juce::RangedAudioParameter* oscAFine = nullptr;
        juce::RangedAudioParameter* oscAPw = nullptr;
        juce::RangedAudioParameter* oscASync = nullptr;
        juce::RangedAudioParameter* oscBWave = nullptr;
        juce::RangedAudioParameter* oscBOctave = nullptr;
        juce::RangedAudioParameter* oscBFine = nullptr;
        juce::RangedAudioParameter* oscBPw = nullptr;
        juce::RangedAudioParameter* oscBLoFreq = nullptr;
        juce::RangedAudioParameter* mixOscA = nullptr;
        juce::RangedAudioParameter* mixOscB = nullptr;
        juce::RangedAudioParameter* mixNoise = nullptr;
        juce::RangedAudioParameter* fltCutoff = nullptr;
        juce::RangedAudioParameter* fltRes = nullptr;
        juce::RangedAudioParameter* fltEnvAmt = nullptr;
        juce::RangedAudioParameter* fltKeyTrk = nullptr;
        juce::RangedAudioParameter* fEnvA = nullptr;
        juce::RangedAudioParameter* fEnvD = nullptr;
        juce::RangedAudioParameter* fEnvS = nullptr;
        juce::RangedAudioParameter* fEnvR = nullptr;
        juce::RangedAudioParameter* aEnvA = nullptr;
        juce::RangedAudioParameter* aEnvD = nullptr;
        juce::RangedAudioParameter* aEnvS = nullptr;
        juce::RangedAudioParameter* aEnvR = nullptr;
        juce::RangedAudioParameter* lfoRate = nullptr;
        juce::RangedAudioParameter* lfoWave = nullptr;
        juce::RangedAudioParameter* lfoMwDep = nullptr;
        juce::RangedAudioParameter* lfoPitch = nullptr;
        juce::RangedAudioParameter* lfoPw = nullptr;
        juce::RangedAudioParameter* lfoCutoff = nullptr;
        juce::RangedAudioParameter* pmodBPitch = nullptr;
        juce::RangedAudioParameter* pmodBPw = nullptr;
        juce::RangedAudioParameter* pmodBCutoff = nullptr;
        juce::RangedAudioParameter* pmodEPitch = nullptr;
        juce::RangedAudioParameter* pmodEPw = nullptr;
        juce::RangedAudioParameter* pmodECutoff = nullptr;
        juce::RangedAudioParameter* perfGlide = nullptr;
        juce::RangedAudioParameter* perfMode = nullptr;
        juce::RangedAudioParameter* perfPrio = nullptr;
        juce::RangedAudioParameter* perfPbRange = nullptr;
        juce::RangedAudioParameter* perfVintage = nullptr;
        juce::RangedAudioParameter* perfPan = nullptr;
        juce::RangedAudioParameter* perfVelAmp = nullptr;
        juce::RangedAudioParameter* perfVelFlt = nullptr;
        juce::RangedAudioParameter* arpOn = nullptr;
        juce::RangedAudioParameter* arpTempo = nullptr;
        juce::RangedAudioParameter* arpRate = nullptr;
        juce::RangedAudioParameter* arpPattern = nullptr;
        juce::RangedAudioParameter* arpOctave = nullptr;
        juce::RangedAudioParameter* arpGate = nullptr;
        juce::RangedAudioParameter* arpLatch = nullptr;
        juce::RangedAudioParameter* drvOn = nullptr;
        juce::RangedAudioParameter* drvAmt = nullptr;
        juce::RangedAudioParameter* drvMix = nullptr;
        juce::RangedAudioParameter* choOn = nullptr;
        juce::RangedAudioParameter* choRate = nullptr;
        juce::RangedAudioParameter* choDep = nullptr;
        juce::RangedAudioParameter* choMix = nullptr;
        juce::RangedAudioParameter* dlyOn = nullptr;
        juce::RangedAudioParameter* dlyTime = nullptr;
        juce::RangedAudioParameter* dlyFdbk = nullptr;
        juce::RangedAudioParameter* dlyMix = nullptr;
        juce::RangedAudioParameter* revOn = nullptr;
        juce::RangedAudioParameter* revSize = nullptr;
        juce::RangedAudioParameter* revDamp = nullptr;
        juce::RangedAudioParameter* revMix = nullptr;
        juce::RangedAudioParameter* outGain = nullptr;
    };

    struct LearnableControlRegistration
    {
        juce::String parameterId;
        juce::String displayName;
        juce::Component* surface = nullptr;
        std::function<void(bool isArmed, juce::String badgeText)> applyVisualState;
    };

    struct ParameterSurfaceRegistration
    {
        juce::String parameterId;
        juce::Component* surface = nullptr;
    };

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshValueDisplays();
    juce::String getCurrentParameterText(juce::RangedAudioParameter* parameter) const;
    juce::RangedAudioParameter* findParameterForId(juce::StringRef parameterId) const noexcept;
    void registerParameterSurface(juce::Component& surface, juce::String parameterId);

    void registerLearnableControl(juce::Component& surface,
                                  juce::String parameterId,
                                  juce::String displayName,
                                  std::function<void(bool, juce::String)> applyVisualState);
    const ParameterSurfaceRegistration* findParameterSurfaceForComponent(const juce::Component* component) const noexcept;
    const LearnableControlRegistration* findLearnableControl(juce::StringRef parameterId) const noexcept;
    void mouseUp(const juce::MouseEvent& event) override;
    void showParameterContextMenu(juce::String parameterId,
                                  juce::String displayName,
                                  juce::Point<int> screenPosition);
    void pollPluginMidiLearnEvents();
    void refreshMidiLearnVisuals();
    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);

    SynthAudioProcessor& processor;
    ParameterRefs parameterRefs;

    juce::Label titleLabel;
    juce::Label midiLearnStatusLabel;
    juce::Label pluginStatusLabel;
    juce::Label buildInfoLabel;

    coolsynth::ui::SynthSection oscSection { "Oscillators" };
    coolsynth::ui::SynthSection mixSection { "Mixer" };
    coolsynth::ui::SynthSection fltSection { "Filter" };
    coolsynth::ui::SynthSection envSection { "Envelopes" };
    coolsynth::ui::SynthSection lfoSection { "LFO" };
    coolsynth::ui::SynthSection pmodSection { "Poly Mod" };
    coolsynth::ui::SynthSection perfSection { "Performance" };
    coolsynth::ui::SynthSection arpSection { "Arpeggiator" };
    coolsynth::ui::SynthSection drvSection { "Drive" };
    coolsynth::ui::SynthSection choSection { "Chorus" };
    coolsynth::ui::SynthSection dlySection { "Delay" };
    coolsynth::ui::SynthSection revSection { "Reverb" };
    coolsynth::ui::SynthSection outSection { "Output" };

    coolsynth::ui::HardwareKnob oscAWaveKnob { "Wave" };
    coolsynth::ui::HardwareKnob oscAOctaveKnob { "Octave" };
    coolsynth::ui::HardwareKnob oscAFineKnob { "Fine" };
    coolsynth::ui::HardwareKnob oscAPwKnob { "Pulse W" };
    coolsynth::ui::HardwareKnob oscASyncKnob { "Sync" };
    coolsynth::ui::HardwareKnob oscBWaveKnob { "Wave" };
    coolsynth::ui::HardwareKnob oscBOctaveKnob { "Octave" };
    coolsynth::ui::HardwareKnob oscBFineKnob { "Fine" };
    coolsynth::ui::HardwareKnob oscBPwKnob { "Pulse W" };
    coolsynth::ui::HardwareKnob oscBLoFreqKnob { "Lo Freq" };
    coolsynth::ui::HardwareKnob mixOscAKnob { "Osc A" };
    coolsynth::ui::HardwareKnob mixOscBKnob { "Osc B" };
    coolsynth::ui::HardwareKnob mixNoiseKnob { "Noise" };
    coolsynth::ui::HardwareKnob fltCutoffKnob { "Cutoff" };
    coolsynth::ui::HardwareKnob fltResKnob { "Resonance" };
    coolsynth::ui::HardwareKnob fltEnvAmtKnob { "Env Amt" };
    coolsynth::ui::HardwareKnob fltKeyTrkKnob { "Key Trk" };
    coolsynth::ui::HardwareKnob fEnvAKnob { "F Atk" };
    coolsynth::ui::HardwareKnob fEnvDKnob { "F Dec" };
    coolsynth::ui::HardwareKnob fEnvSKnob { "F Sus" };
    coolsynth::ui::HardwareKnob fEnvRKnob { "F Rel" };
    coolsynth::ui::HardwareKnob aEnvAKnob { "A Atk" };
    coolsynth::ui::HardwareKnob aEnvDKnob { "A Dec" };
    coolsynth::ui::HardwareKnob aEnvSKnob { "A Sus" };
    coolsynth::ui::HardwareKnob aEnvRKnob { "A Rel" };
    coolsynth::ui::HardwareKnob lfoRateKnob { "Rate" };
    coolsynth::ui::HardwareKnob lfoWaveKnob { "Wave" };
    coolsynth::ui::HardwareKnob lfoMwDepKnob { "MW->Dep" };
    coolsynth::ui::HardwareKnob lfoPitchKnob { "->Pitch" };
    coolsynth::ui::HardwareKnob lfoPwKnob { "->PW" };
    coolsynth::ui::HardwareKnob lfoCutoffKnob { "->Cutoff" };
    coolsynth::ui::HardwareKnob pmodBPitchKnob { "B->Pitch" };
    coolsynth::ui::HardwareKnob pmodBPwKnob { "B->PW" };
    coolsynth::ui::HardwareKnob pmodBCutoffKnob { "B->Cutoff" };
    coolsynth::ui::HardwareKnob pmodEPitchKnob { "E->Pitch" };
    coolsynth::ui::HardwareKnob pmodEPwKnob { "E->PW" };
    coolsynth::ui::HardwareKnob pmodECutoffKnob { "E->Cutoff" };
    coolsynth::ui::HardwareKnob perfGlideKnob { "Glide" };
    coolsynth::ui::HardwareKnob perfModeKnob { "Mode" };
    coolsynth::ui::HardwareKnob perfPrioKnob { "Priority" };
    coolsynth::ui::HardwareKnob perfPbRangeKnob { "PB Range" };
    coolsynth::ui::HardwareKnob perfVintageKnob { "Vintage" };
    coolsynth::ui::HardwareKnob perfPanKnob { "Pan Spread" };
    coolsynth::ui::HardwareKnob perfVelAmpKnob { "Vel->Amp" };
    coolsynth::ui::HardwareKnob perfVelFltKnob { "Vel->Flt" };
    coolsynth::ui::HardwareKnob arpOnKnob { "On" };
    coolsynth::ui::HardwareKnob arpTempoKnob { "Tempo" };
    coolsynth::ui::HardwareKnob arpRateKnob { "Rate" };
    coolsynth::ui::HardwareKnob arpPatternKnob { "Pattern" };
    coolsynth::ui::HardwareKnob arpOctaveKnob { "Octave" };
    coolsynth::ui::HardwareKnob arpGateKnob { "Gate" };
    coolsynth::ui::HardwareKnob arpLatchKnob { "Latch" };
    coolsynth::ui::HardwareKnob drvOnKnob { "On" };
    coolsynth::ui::HardwareKnob drvAmtKnob { "Amount" };
    coolsynth::ui::HardwareKnob drvMixKnob { "Mix" };
    coolsynth::ui::HardwareKnob choOnKnob { "On" };
    coolsynth::ui::HardwareKnob choRateKnob { "Rate" };
    coolsynth::ui::HardwareKnob choDepKnob { "Depth" };
    coolsynth::ui::HardwareKnob choMixKnob { "Mix" };
    coolsynth::ui::HardwareKnob dlyOnKnob { "On" };
    coolsynth::ui::HardwareKnob dlyTimeKnob { "Time" };
    coolsynth::ui::HardwareKnob dlyFdbkKnob { "Fdbk" };
    coolsynth::ui::HardwareKnob dlyMixKnob { "Mix" };
    coolsynth::ui::HardwareKnob revOnKnob { "On" };
    coolsynth::ui::HardwareKnob revSizeKnob { "Size" };
    coolsynth::ui::HardwareKnob revDampKnob { "Damp" };
    coolsynth::ui::HardwareKnob revMixKnob { "Mix" };
    coolsynth::ui::HardwareFader outGainFader { "Master" };

    std::unique_ptr<SliderAttachment> oscAWaveAttachment;
    std::unique_ptr<SliderAttachment> oscAOctaveAttachment;
    std::unique_ptr<SliderAttachment> oscAFineAttachment;
    std::unique_ptr<SliderAttachment> oscAPwAttachment;
    std::unique_ptr<SliderAttachment> oscASyncAttachment;
    std::unique_ptr<SliderAttachment> oscBWaveAttachment;
    std::unique_ptr<SliderAttachment> oscBOctaveAttachment;
    std::unique_ptr<SliderAttachment> oscBFineAttachment;
    std::unique_ptr<SliderAttachment> oscBPwAttachment;
    std::unique_ptr<SliderAttachment> oscBLoFreqAttachment;
    std::unique_ptr<SliderAttachment> mixOscAAttachment;
    std::unique_ptr<SliderAttachment> mixOscBAttachment;
    std::unique_ptr<SliderAttachment> mixNoiseAttachment;
    std::unique_ptr<SliderAttachment> fltCutoffAttachment;
    std::unique_ptr<SliderAttachment> fltResAttachment;
    std::unique_ptr<SliderAttachment> fltEnvAmtAttachment;
    std::unique_ptr<SliderAttachment> fltKeyTrkAttachment;
    std::unique_ptr<SliderAttachment> fEnvAAttachment;
    std::unique_ptr<SliderAttachment> fEnvDAttachment;
    std::unique_ptr<SliderAttachment> fEnvSAttachment;
    std::unique_ptr<SliderAttachment> fEnvRAttachment;
    std::unique_ptr<SliderAttachment> aEnvAAttachment;
    std::unique_ptr<SliderAttachment> aEnvDAttachment;
    std::unique_ptr<SliderAttachment> aEnvSAttachment;
    std::unique_ptr<SliderAttachment> aEnvRAttachment;
    std::unique_ptr<SliderAttachment> lfoRateAttachment;
    std::unique_ptr<SliderAttachment> lfoWaveAttachment;
    std::unique_ptr<SliderAttachment> lfoMwDepAttachment;
    std::unique_ptr<SliderAttachment> lfoPitchAttachment;
    std::unique_ptr<SliderAttachment> lfoPwAttachment;
    std::unique_ptr<SliderAttachment> lfoCutoffAttachment;
    std::unique_ptr<SliderAttachment> pmodBPitchAttachment;
    std::unique_ptr<SliderAttachment> pmodBPwAttachment;
    std::unique_ptr<SliderAttachment> pmodBCutoffAttachment;
    std::unique_ptr<SliderAttachment> pmodEPitchAttachment;
    std::unique_ptr<SliderAttachment> pmodEPwAttachment;
    std::unique_ptr<SliderAttachment> pmodECutoffAttachment;
    std::unique_ptr<SliderAttachment> perfGlideAttachment;
    std::unique_ptr<SliderAttachment> perfModeAttachment;
    std::unique_ptr<SliderAttachment> perfPrioAttachment;
    std::unique_ptr<SliderAttachment> perfPbRangeAttachment;
    std::unique_ptr<SliderAttachment> perfVintageAttachment;
    std::unique_ptr<SliderAttachment> perfPanAttachment;
    std::unique_ptr<SliderAttachment> perfVelAmpAttachment;
    std::unique_ptr<SliderAttachment> perfVelFltAttachment;
    std::unique_ptr<SliderAttachment> arpOnAttachment;
    std::unique_ptr<SliderAttachment> arpTempoAttachment;
    std::unique_ptr<SliderAttachment> arpRateAttachment;
    std::unique_ptr<SliderAttachment> arpPatternAttachment;
    std::unique_ptr<SliderAttachment> arpOctaveAttachment;
    std::unique_ptr<SliderAttachment> arpGateAttachment;
    std::unique_ptr<SliderAttachment> arpLatchAttachment;
    std::unique_ptr<SliderAttachment> drvOnAttachment;
    std::unique_ptr<SliderAttachment> drvAmtAttachment;
    std::unique_ptr<SliderAttachment> drvMixAttachment;
    std::unique_ptr<SliderAttachment> choOnAttachment;
    std::unique_ptr<SliderAttachment> choRateAttachment;
    std::unique_ptr<SliderAttachment> choDepAttachment;
    std::unique_ptr<SliderAttachment> choMixAttachment;
    std::unique_ptr<SliderAttachment> dlyOnAttachment;
    std::unique_ptr<SliderAttachment> dlyTimeAttachment;
    std::unique_ptr<SliderAttachment> dlyFdbkAttachment;
    std::unique_ptr<SliderAttachment> dlyMixAttachment;
    std::unique_ptr<SliderAttachment> revOnAttachment;
    std::unique_ptr<SliderAttachment> revSizeAttachment;
    std::unique_ptr<SliderAttachment> revDampAttachment;
    std::unique_ptr<SliderAttachment> revMixAttachment;
    std::unique_ptr<SliderAttachment> outGainAttachment;

    juce::TextButton allNotesOffButton { "All Notes Off" };

    std::unique_ptr<coolsynth::midi::MidiLearnManager> midiLearnManager;
    std::vector<LearnableControlRegistration> learnableControls;
    std::vector<ParameterSurfaceRegistration> parameterSurfaces;

    std::unique_ptr<coolsynth::standalone::StandaloneMidiInputController> standaloneMidiController;
    std::unique_ptr<juce::Component> standaloneStatusBar;

    int badgeVisibilityCounter = 0;
    bool lastShowCcLabelsSetting = true;

    void triggerInitPatch();
    void triggerSavePatch();
    void triggerLoadPatch();
    void launchPatchSaveChooser();
    void launchPatchLoadChooser();
    void handlePatchSaveSelection(const juce::File& selectedFile);
    void handlePatchLoadSelection(const juce::File& selectedFile);
    void showPatchError(juce::String message);

    juce::TextButton initPatchButton { "Init Patch" };
    juce::TextButton savePatchButton { "Save Patch" };
    juce::TextButton loadPatchButton { "Load Patch" };
    coolsynth::ui::PianoBarComponent pianoBar;
    std::unique_ptr<juce::FileChooser> activePatchChooser;
    bool patchActionsVisible = false;

public:
    coolsynth::standalone::StandaloneMidiInputController* getStandaloneMidiController() const noexcept
    {
        return standaloneMidiController.get();
    }
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessorEditor)
};
