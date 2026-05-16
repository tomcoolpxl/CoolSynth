#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/HardwareKnob.h"
#include "ui/LedToggleButton.h"
#include "ui/PianoBarComponent.h"
#include "ui/SegmentedChoiceGroup.h"
#include "ui/SynthSection.h"
#include "ui/SignalChainVisualizer.h"
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

    /** Returns the visualizer for the processor to push samples into. */
    coolsynth::ui::SignalChainVisualizer& getVisualizer() { return visualizer; }

    void refreshStandaloneControllerProfileSelection();
    juce::String getResolvedStandaloneControllerProfileDisplayName() const;
    void resetStandaloneMidiSettings();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceAttachment = juce::ParameterAttachment;

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
    void setParameterTooltip(juce::SettableTooltipClient& surface,
                             juce::String name,
                             juce::String description);
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

    std::unique_ptr<juce::Drawable> titleLogoDrawable;
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

    coolsynth::ui::SegmentedChoiceGroup oscAWaveChoice {
        "Wave",
        {
            { "Pulse", 0, coolsynth::ui::SegmentedChoiceGroup::Icon::pulse },
            { "Tri", 1, coolsynth::ui::SegmentedChoiceGroup::Icon::triangle },
            { "Saw", 2, coolsynth::ui::SegmentedChoiceGroup::Icon::saw },
            { "Sin", 3, coolsynth::ui::SegmentedChoiceGroup::Icon::sine },
        },
        4
    };
    coolsynth::ui::HardwareKnob oscAOctaveKnob { "Octave" };
    coolsynth::ui::HardwareKnob oscAFineKnob { "Fine" };
    coolsynth::ui::HardwareKnob oscAPwKnob { "Pulse W" };
    coolsynth::ui::LedToggleButton oscASyncToggle { "Sync" };
    coolsynth::ui::SegmentedChoiceGroup oscBWaveChoice {
        "Wave",
        {
            { "Pulse", 0, coolsynth::ui::SegmentedChoiceGroup::Icon::pulse },
            { "Tri", 1, coolsynth::ui::SegmentedChoiceGroup::Icon::triangle },
            { "Saw", 2, coolsynth::ui::SegmentedChoiceGroup::Icon::saw },
            { "Sin", 3, coolsynth::ui::SegmentedChoiceGroup::Icon::sine },
        },
        4
    };
    coolsynth::ui::HardwareKnob oscBOctaveKnob { "Octave" };
    coolsynth::ui::HardwareKnob oscBFineKnob { "Fine" };
    coolsynth::ui::HardwareKnob oscBPwKnob { "Pulse W" };
    coolsynth::ui::LedToggleButton oscBLoFreqToggle { "Lo Freq" };
    coolsynth::ui::HardwareKnob mixOscAKnob { "Osc A" };
    coolsynth::ui::HardwareKnob mixOscBKnob { "Osc B" };
    coolsynth::ui::HardwareKnob mixNoiseKnob { "Noise" };
    coolsynth::ui::HardwareKnob fltCutoffKnob { "Cutoff" };
    coolsynth::ui::HardwareKnob fltResKnob { "Resonance" };
    coolsynth::ui::HardwareKnob fltEnvAmtKnob { "Env Amt" };
    coolsynth::ui::SegmentedChoiceGroup fltKeyTrkChoice {
        "Key Trk",
        {
            { "Off", 0 },
            { "Half", 1 },
            { "Full", 2 },
        },
        1
    };
    coolsynth::ui::HardwareKnob fEnvAKnob { "F Atk" };
    coolsynth::ui::HardwareKnob fEnvDKnob { "F Dec" };
    coolsynth::ui::HardwareKnob fEnvSKnob { "F Sus" };
    coolsynth::ui::HardwareKnob fEnvRKnob { "F Rel" };
    coolsynth::ui::HardwareKnob aEnvAKnob { "A Atk" };
    coolsynth::ui::HardwareKnob aEnvDKnob { "A Dec" };
    coolsynth::ui::HardwareKnob aEnvSKnob { "A Sus" };
    coolsynth::ui::HardwareKnob aEnvRKnob { "A Rel" };
    coolsynth::ui::HardwareKnob lfoRateKnob { "Rate" };
    coolsynth::ui::SegmentedChoiceGroup lfoWaveChoice {
        "Wave",
        {
            { "Saw", 0, coolsynth::ui::SegmentedChoiceGroup::Icon::saw },
            { "Tri", 1, coolsynth::ui::SegmentedChoiceGroup::Icon::triangle },
            { "Sqr", 2, coolsynth::ui::SegmentedChoiceGroup::Icon::square },
            { "Sin", 3, coolsynth::ui::SegmentedChoiceGroup::Icon::sine },
        },
        4
    };
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
    coolsynth::ui::SegmentedChoiceGroup perfModeChoice {
        "Mode",
        {
            { "Poly", 0 },
            { "Mono", 1 },
            { "Unison", 2 },
        },
        1
    };
    coolsynth::ui::SegmentedChoiceGroup perfPrioChoice {
        "Priority",
        {
            { "Last", 0 },
            { "Low", 1 },
            { "High", 2 },
        },
        1
    };
    coolsynth::ui::HardwareKnob perfPbRangeKnob { "PB Range" };
    coolsynth::ui::HardwareKnob perfVintageKnob { "Vintage" };
    coolsynth::ui::HardwareKnob perfPanKnob { "Pan Spread" };
    coolsynth::ui::HardwareKnob perfVelAmpKnob { "Vel->Amp" };
    coolsynth::ui::HardwareKnob perfVelFltKnob { "Vel->Flt" };
    coolsynth::ui::LedToggleButton arpOnToggle { "Arp On" };
    coolsynth::ui::HardwareKnob arpTempoKnob { "Tempo" };
    coolsynth::ui::SegmentedChoiceGroup arpRateChoice {
        "Rate",
        {
            { "1/4", 0 },
            { "1/8", 1 },
            { "1/8T", 2 },
            { "1/16", 3 },
            { "1/16T", 4 },
            { "1/32", 5 },
        },
        3
    };
    coolsynth::ui::SegmentedChoiceGroup arpPatternChoice {
        "Pattern",
        {
            { "Up", 0 },
            { "Down", 1 },
            { "Up/Dn", 2 },
            { "Play", 3 },
        },
        2
    };
    coolsynth::ui::SegmentedChoiceGroup arpOctaveChoice {
        "Octave",
        {
            { "1", 1 },
            { "2", 2 },
            { "3", 3 },
        },
        1
    };
    coolsynth::ui::HardwareKnob arpGateKnob { "Gate" };
    coolsynth::ui::LedToggleButton arpLatchToggle { "Latch" };
    coolsynth::ui::LedToggleButton drvOnToggle { "Drive" };
    coolsynth::ui::HardwareKnob drvAmtKnob { "Amount" };
    coolsynth::ui::HardwareKnob drvMixKnob { "Mix" };
    coolsynth::ui::LedToggleButton choOnToggle { "Chorus" };
    coolsynth::ui::HardwareKnob choRateKnob { "Rate" };
    coolsynth::ui::HardwareKnob choDepKnob { "Depth" };
    coolsynth::ui::HardwareKnob choMixKnob { "Mix" };
    coolsynth::ui::LedToggleButton dlyOnToggle { "Delay" };
    coolsynth::ui::HardwareKnob dlyTimeKnob { "Time" };
    coolsynth::ui::HardwareKnob dlyFdbkKnob { "Fdbk" };
    coolsynth::ui::HardwareKnob dlyMixKnob { "Mix" };
    coolsynth::ui::LedToggleButton revOnToggle { "Reverb" };
    coolsynth::ui::HardwareKnob revSizeKnob { "Size" };
    coolsynth::ui::HardwareKnob revDampKnob { "Damp" };
    coolsynth::ui::HardwareKnob revMixKnob { "Mix" };
    coolsynth::ui::HardwareKnob outGainKnob { "Master" };

    std::unique_ptr<ChoiceAttachment> oscAWaveAttachment;
    std::unique_ptr<SliderAttachment> oscAOctaveAttachment;
    std::unique_ptr<SliderAttachment> oscAFineAttachment;
    std::unique_ptr<SliderAttachment> oscAPwAttachment;
    std::unique_ptr<ButtonAttachment> oscASyncAttachment;
    std::unique_ptr<ChoiceAttachment> oscBWaveAttachment;
    std::unique_ptr<SliderAttachment> oscBOctaveAttachment;
    std::unique_ptr<SliderAttachment> oscBFineAttachment;
    std::unique_ptr<SliderAttachment> oscBPwAttachment;
    std::unique_ptr<ButtonAttachment> oscBLoFreqAttachment;
    std::unique_ptr<SliderAttachment> mixOscAAttachment;
    std::unique_ptr<SliderAttachment> mixOscBAttachment;
    std::unique_ptr<SliderAttachment> mixNoiseAttachment;
    std::unique_ptr<SliderAttachment> fltCutoffAttachment;
    std::unique_ptr<SliderAttachment> fltResAttachment;
    std::unique_ptr<SliderAttachment> fltEnvAmtAttachment;
    std::unique_ptr<ChoiceAttachment> fltKeyTrkAttachment;
    std::unique_ptr<SliderAttachment> fEnvAAttachment;
    std::unique_ptr<SliderAttachment> fEnvDAttachment;
    std::unique_ptr<SliderAttachment> fEnvSAttachment;
    std::unique_ptr<SliderAttachment> fEnvRAttachment;
    std::unique_ptr<SliderAttachment> aEnvAAttachment;
    std::unique_ptr<SliderAttachment> aEnvDAttachment;
    std::unique_ptr<SliderAttachment> aEnvSAttachment;
    std::unique_ptr<SliderAttachment> aEnvRAttachment;
    std::unique_ptr<SliderAttachment> lfoRateAttachment;
    std::unique_ptr<ChoiceAttachment> lfoWaveAttachment;
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
    std::unique_ptr<ChoiceAttachment> perfModeAttachment;
    std::unique_ptr<ChoiceAttachment> perfPrioAttachment;
    std::unique_ptr<SliderAttachment> perfPbRangeAttachment;
    std::unique_ptr<SliderAttachment> perfVintageAttachment;
    std::unique_ptr<SliderAttachment> perfPanAttachment;
    std::unique_ptr<SliderAttachment> perfVelAmpAttachment;
    std::unique_ptr<SliderAttachment> perfVelFltAttachment;
    std::unique_ptr<ButtonAttachment> arpOnAttachment;
    std::unique_ptr<SliderAttachment> arpTempoAttachment;
    std::unique_ptr<ChoiceAttachment> arpRateAttachment;
    std::unique_ptr<ChoiceAttachment> arpPatternAttachment;
    std::unique_ptr<ChoiceAttachment> arpOctaveAttachment;
    std::unique_ptr<SliderAttachment> arpGateAttachment;
    std::unique_ptr<ButtonAttachment> arpLatchAttachment;
    std::unique_ptr<ButtonAttachment> drvOnAttachment;
    std::unique_ptr<SliderAttachment> drvAmtAttachment;
    std::unique_ptr<SliderAttachment> drvMixAttachment;
    std::unique_ptr<ButtonAttachment> choOnAttachment;
    std::unique_ptr<SliderAttachment> choRateAttachment;
    std::unique_ptr<SliderAttachment> choDepAttachment;
    std::unique_ptr<SliderAttachment> choMixAttachment;
    std::unique_ptr<ButtonAttachment> dlyOnAttachment;
    std::unique_ptr<SliderAttachment> dlyTimeAttachment;
    std::unique_ptr<SliderAttachment> dlyFdbkAttachment;
    std::unique_ptr<SliderAttachment> dlyMixAttachment;
    std::unique_ptr<ButtonAttachment> revOnAttachment;
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
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<juce::LookAndFeel_V4> tooltipLookAndFeel;
    bool tooltipsEnabled = true;

    int badgeVisibilityCounter = 0;
    bool lastShowCcLabelsSetting = true;

    void setupParameterRefs();
    void setupVisualsAndLabels(bool isStandalone);
    void setupControlAttachments();
    void registerLearnableControls();
    void setupActionButtons();
    void setupStandaloneMode(bool isStandalone);
    void setupTooltipWindow();
    void applyTooltips();

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
    juce::TextButton tooltipToggleButton { "i" };
    coolsynth::ui::SignalChainVisualizer visualizer;
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
