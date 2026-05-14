#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/MidiMappingEngine.h"
#include "synth/SynthEngineV2.h"

class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SynthAudioProcessor();
    ~SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CoolSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::unique_ptr<juce::XmlElement> createParameterStateXml();
    bool applyParameterStateXml(const juce::XmlElement& stateXml);
    void resetAutomatableParametersToDefaults();
    juce::String getParameterStateTypeName() const;

    bool setActiveControllerProfile(juce::StringRef profileId);
    juce::String getActiveControllerProfileId() const;
    juce::String getActiveControllerProfileDisplayName() const;

    void setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
    std::vector<coolsynth::midi::LearnedCcBinding> getLearnedMidiBindings() const;
    void clearLearnedMidiBinding(juce::StringRef parameterId);
    int drainPendingPluginControllerEvents(coolsynth::midi::ControllerMidiEvent* destination,
                                           int maxEvents) noexcept;

    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);
    void requestPanic() noexcept;

    APVTS& getValueTreeState() noexcept { return parameters; }
    const APVTS& getValueTreeState() const noexcept { return parameters; }

    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

private:
    class PluginMappedActionDispatcher final : private juce::Thread
    {
    public:
        explicit PluginMappedActionDispatcher(SynthAudioProcessor& ownerIn);
        ~PluginMappedActionDispatcher() override;

    private:
        void run() override;
        SynthAudioProcessor& owner;
    };

    bool buildSanitizedParameterStateTree(const juce::ValueTree& incomingState,
                                          juce::ValueTree& sanitizedState);
    void enqueuePluginControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept;
    void enqueuePluginMappedAction(const coolsynth::midi::MappedAction& action) noexcept;
    int drainPendingMappedActions(coolsynth::midi::MappedAction* destination, int maxActions) noexcept;
    void dispatchPendingMappedActions();
    std::unique_ptr<juce::XmlElement> createProcessorStateXml() const;
    std::vector<coolsynth::midi::LearnedCcBinding> parseLearnedMidiBindingsXml(const juce::XmlElement& parent) const;
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                       float normalizedValue);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;

    coolsynth::synth::BlockRenderParametersV2 makeBlockRenderParameters() const noexcept;
    static coolsynth::synth::ParameterValuePointersV2 bindParameterPointers(APVTS& state);

    APVTS parameters;
    juce::MidiKeyboardState keyboardState;
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    std::vector<coolsynth::midi::LearnedCcBinding> learnedMidiBindings;
    coolsynth::synth::ParameterValuePointersV2 parameterValues;
    coolsynth::synth::SynthEngineV2 synthEngine;
    std::atomic<bool> panicRequested { false };
    std::array<coolsynth::midi::ControllerMidiEvent, 128> pendingPluginControllerEvents {};
    juce::AbstractFifo pendingPluginControllerEventQueue { static_cast<int> (pendingPluginControllerEvents.size()) };
    std::array<coolsynth::midi::MappedAction, 128> pendingMappedActions {};
    juce::AbstractFifo pendingMappedActionQueue { static_cast<int> (pendingMappedActions.size()) };
    PluginMappedActionDispatcher mappedActionDispatcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
