#pragma once

#include <array>
#include <atomic>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/MidiMappingEngine.h"
#include "plugin/ParameterUndoHistory.h"
#include "plugin/ProcessorScopeFifo.h"
#include "parameters/v3/PartParameters.h"
#include "synth/SynthParameters.h"
#include "synth/v3/EngineV3.h"

class SynthAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener
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

    // The factory bank as host programs, named "Room: Preset".
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
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

    // ---- Undo and patch identity (message thread) ----------------------------
    // Every UI gesture becomes one undo step automatically. Bulk edits (preset,
    // init, load) are wrapped in performUndoableEdit() so they become one step too.
    void performUndoableEdit(const juce::String& label, const std::function<void()>& edit);
    bool canUndo() const noexcept { return undoHistory.canUndo(); }
    bool canRedo() const noexcept { return undoHistory.canRedo(); }
    juce::String getUndoLabel() const { return undoHistory.getUndoLabel(); }
    juce::String getRedoLabel() const { return undoHistory.getRedoLabel(); }
    bool undo();
    bool redo();

    // The name shown for the current patch and whether it has been edited since it
    // was last applied or saved. Any parameter change marks the patch dirty.
    void setPatchIdentity(juce::String name, bool dirty);
    juce::String getPatchName() const;
    bool isPatchDirty() const noexcept { return patchDirty.load(std::memory_order_relaxed); }

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

    /** Reads every parameter into the engine settings once per block; lock-free. */
    const coolsynth::synth::v3::EngineSettings& makeEngineSettings() noexcept;
    static coolsynth::synth::GlobalParameterPointers bindParameterPointers(APVTS& state);

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    coolsynth::plugin::ParameterUndoHistory::Snapshot captureSnapshot(juce::String label) const;
    std::vector<float> captureNormalisedValues() const;
    void restoreSnapshot(const coolsynth::plugin::ParameterUndoHistory::Snapshot& snapshot);


    APVTS parameters;
    juce::MidiKeyboardState keyboardState;
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    std::vector<coolsynth::midi::LearnedCcBinding> learnedMidiBindings;
    coolsynth::synth::GlobalParameterPointers parameterValues;
    juce::StringArray knownParameterIds; // every parameter, for patch and state validation
    coolsynth::parameters::v3::PartParameterPointers partParameterValues;
    coolsynth::synth::v3::EngineSettings engineSettings;
    coolsynth::synth::v3::EngineV3 synthEngine;
    std::atomic<bool> panicRequested { false };
    int currentProgram = 0;
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

    coolsynth::plugin::ParameterUndoHistory undoHistory;
    int activeGestureCount = 0;
    bool suppressGestureSnapshots = false;
    std::atomic<bool> applyingPatch { false };
    std::atomic<bool> patchDirty { false };
    juce::String patchName { "Init" };
    mutable juce::CriticalSection patchIdentityLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
