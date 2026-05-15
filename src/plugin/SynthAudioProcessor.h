#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/MidiMappingEngine.h"
#include "plugin/ProcessorScopeFifo.h"
#include "synth/SynthEngineV2.h"

class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SynthAudioProcessor();
    ~SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CoolSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

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

    coolsynth::plugin::ProcessorScopeFifo& getScopeFifo() noexcept { return scopeFifo; }

    uint64_t getDroppedControllerEventCount() const noexcept
        { return droppedControllerEventCount.load(std::memory_order_relaxed); }
    uint64_t getDroppedMappedControllerEventCount() const noexcept
        { return droppedMappedControllerEventCount.load(std::memory_order_relaxed); }

    void flushMappedControllerEventsSync() { dispatchPendingMappedControllerEvents(); }

private:
    class PluginMappedActionAsyncBridge : public juce::AsyncUpdater
    {
    public:
        explicit PluginMappedActionAsyncBridge(SynthAudioProcessor& o) : owner(o) {}
        void handleAsyncUpdate() override { owner.dispatchPendingMappedControllerEvents(); }
    private:
        SynthAudioProcessor& owner;
    };

    bool buildSanitizedParameterStateTree(const juce::ValueTree& incomingState,
                                          juce::ValueTree& sanitizedState);
    void enqueuePluginControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept;
    void enqueuePluginMappedControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept;
    int drainPendingMappedControllerEvents(coolsynth::midi::ControllerMidiEvent* destination, int maxEvents) noexcept;
    void dispatchPendingMappedControllerEvents();
    std::unique_ptr<juce::XmlElement> createProcessorStateXml() const;
    std::vector<coolsynth::midi::LearnedCcBinding> parseLearnedMidiBindingsXml(const juce::XmlElement& parent) const;
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                       float normalizedValue);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;

    coolsynth::synth::BlockRenderParametersV2 makeBlockRenderParameters() const noexcept;
    static coolsynth::synth::ParameterValuePointersV2 bindParameterPointers(APVTS& state);

    // C6: typed choice-parameter pointers — getIndex() avoids per-block std::round + cast
    struct ChoiceParameterPointers
    {
        juce::AudioParameterChoice* oscAWave       = nullptr;
        juce::AudioParameterChoice* oscBWave       = nullptr;
        juce::AudioParameterChoice* filterKeyTrack = nullptr;
        juce::AudioParameterChoice* lfoWave        = nullptr;
        juce::AudioParameterChoice* playMode       = nullptr;
        juce::AudioParameterChoice* keyPriority    = nullptr;
        juce::AudioParameterChoice* arpRate        = nullptr;
        juce::AudioParameterChoice* arpPattern     = nullptr;
    };
    static ChoiceParameterPointers bindChoicePointers(APVTS& state);

    APVTS parameters;
    juce::MidiKeyboardState keyboardState;
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    std::vector<coolsynth::midi::LearnedCcBinding> learnedMidiBindings;
    coolsynth::synth::ParameterValuePointersV2 parameterValues;
    ChoiceParameterPointers choiceParams;
    coolsynth::synth::SynthEngineV2 synthEngine;
    std::atomic<bool> panicRequested { false };
    mutable juce::CriticalSection midiMappingStateLock;
    std::array<coolsynth::midi::ControllerMidiEvent, 128> pendingPluginControllerEvents {};
    juce::AbstractFifo pendingPluginControllerEventQueue { static_cast<int> (pendingPluginControllerEvents.size()) };
    std::array<coolsynth::midi::ControllerMidiEvent, 128> pendingPluginMappedControllerEvents {};
    juce::AbstractFifo pendingPluginMappedControllerEventQueue { static_cast<int> (pendingPluginMappedControllerEvents.size()) };
    std::atomic<uint64_t> droppedControllerEventCount { 0 };
    std::atomic<uint64_t> droppedMappedControllerEventCount { 0 };
    coolsynth::plugin::ProcessorScopeFifo scopeFifo;
    std::vector<float> monoMixScratch;
    PluginMappedActionAsyncBridge mappedActionBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
