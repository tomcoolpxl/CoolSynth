#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/HardwareFader.h"
#include "ui/HardwareKnob.h"
#include "ui/SynthSection.h"
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

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct ParameterRefs
    {
        juce::RangedAudioParameter* waveform = nullptr;
        juce::RangedAudioParameter* filterCutoffHz = nullptr;
        juce::RangedAudioParameter* filterResonance = nullptr;
        juce::RangedAudioParameter* ampAttackMs = nullptr;
        juce::RangedAudioParameter* ampDecayMs = nullptr;
        juce::RangedAudioParameter* ampSustain = nullptr;
        juce::RangedAudioParameter* ampReleaseMs = nullptr;
        juce::RangedAudioParameter* delayTimeMs = nullptr;
        juce::RangedAudioParameter* delayFeedback = nullptr;
        juce::RangedAudioParameter* delayMix = nullptr;
        juce::RangedAudioParameter* masterGainDb = nullptr;
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
    void refreshMidiLearnVisuals();
    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);

    SynthAudioProcessor& processor;
    ParameterRefs parameterRefs;

    juce::Label titleLabel;
    juce::Label midiLearnStatusLabel;

    coolsynth::ui::SynthSection oscillatorSection { "Oscillator" };
    coolsynth::ui::SynthSection filterSection { "Filter" };
    coolsynth::ui::SynthSection envelopeSection { "Envelope" };
    coolsynth::ui::SynthSection delaySection { "Delay" };
    coolsynth::ui::SynthSection outputSection { "Output" };

    juce::Label waveformLabel;
    juce::ComboBox waveformSelector;
    coolsynth::ui::HardwareKnob cutoffKnob { "Cutoff" };
    coolsynth::ui::HardwareKnob resonanceKnob { "Resonance" };
    coolsynth::ui::HardwareKnob attackKnob { "Attack" };
    coolsynth::ui::HardwareKnob decayKnob { "Decay" };
    coolsynth::ui::HardwareKnob sustainKnob { "Sustain" };
    coolsynth::ui::HardwareKnob releaseKnob { "Release" };
    coolsynth::ui::HardwareKnob delayTimeKnob { "Time" };
    coolsynth::ui::HardwareKnob delayFeedbackKnob { "Feedback" };
    coolsynth::ui::HardwareKnob delayMixKnob { "Mix" };
    coolsynth::ui::HardwareFader masterGainFader { "Master" };
    juce::TextButton allNotesOffButton { "All Notes Off" };

    std::unique_ptr<ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;
    std::unique_ptr<SliderAttachment> resonanceAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> delayTimeAttachment;
    std::unique_ptr<SliderAttachment> delayFeedbackAttachment;
    std::unique_ptr<SliderAttachment> delayMixAttachment;
    std::unique_ptr<SliderAttachment> masterGainAttachment;

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
