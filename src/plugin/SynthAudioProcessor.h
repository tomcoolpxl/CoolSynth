#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <map>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/MidiMappingEngine.h"
#include "plugin/ParameterUndoHistory.h"
#include "plugin/ProcessorScopeFifo.h"
#include "parameters/v3/PartParameters.h"
#include "synth/SynthParameters.h"
#include "synth/v3/EngineV3.h"

namespace coolsynth::ui { class InterfaceModel; }

class SynthAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::ValueTree::Listener,
                                  private juce::MidiKeyboardState::Listener
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

    /** Offline use (tools, tests): waits until the sampled instruments the current patch names are in memory.
        False if one of them is not installed. */
    bool loadSampledInstrumentsNow();

    /** The user's own sound a generator slot plays (decision D137): a file's path inside the User folder, or
        empty for the slot's own choice. part 0 or 1, slot 1 to 3. Message thread. */
    void setUserFile(int part, int slot, const juce::String& name);
    [[nodiscard]] juce::String getUserFile(int part, int slot) const;
    /** Empty when the slot's file is in memory (or it names none), "still loading", or why it could not be read. */
    [[nodiscard]] juce::String getUserFileStatus(int part, int slot) const;
    /** Waits until every user file the patch names is read; false when one is missing or unreadable. */
    bool loadUserSoundsNow(int timeoutMilliseconds = 30000);
    /** Offline use: the lowest and highest notes the patch's sampled instruments were recorded at, read in if
        need be. False if the patch plays no sampled instrument or it is not installed. */
    bool sampledInstrumentRange(int& lowestNote, int& highestNote);

    /** True while a sampled instrument the sound plays is still being read in (D154): the keys wait until it
        is all in memory, and Home blinks. Set by the audio thread each block; read from anywhere. */
    [[nodiscard]] bool isLoadingInstrument() const noexcept { return instrumentLoading.load(std::memory_order_relaxed); }

    /** While one lives, the audio thread goes on with the settings it had: a preset, a saved state, an undo step or
        a type change reaches it whole, never half applied (D157). Scopes may nest and come from any thread. */
    class BulkEdit final
    {
    public:
        explicit BulkEdit(SynthAudioProcessor& p) noexcept : owner(p)
        {
            if (owner.bulkEditDepth.fetch_add(1, std::memory_order_acq_rel) == 0)
                owner.settingsSequence.fetch_add(1, std::memory_order_seq_cst); // odd: under way
        }
        ~BulkEdit()
        {
            if (owner.bulkEditDepth.fetch_sub(1, std::memory_order_acq_rel) == 1)
                owner.settingsSequence.fetch_add(1, std::memory_order_release); // even: whole again
        }
        BulkEdit(const BulkEdit&) = delete;
        BulkEdit& operator=(const BulkEdit&) = delete;

    private:
        SynthAudioProcessor& owner;
    };

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
    /** One MIDI thread only (the standalone app's MiniLab driver): a note, controller, bend or pressure message for
        the sound, lock-free; it plays one block later at its own time (D159). False when the queue is full. */
    bool pushLiveMidi(const juce::MidiMessage& message) noexcept;

    /** The interface's state (docs/interface-v3.md), shared by the panel and the MiniLab driver. Message thread. */
    coolsynth::ui::InterfaceModel& getInterfaceModel();
    coolsynth::synth::v3::Part& getPartForTesting() noexcept { return synthEngine.partForTesting(); }

    coolsynth::plugin::ProcessorScopeFifo& getScopeFifo() noexcept { return scopeFifo; }

    uint64_t getDroppedControllerEventCount() const noexcept
        { return droppedControllerEventCount.load(std::memory_order_relaxed); }
    uint64_t getDroppedMappedControllerEventCount() const noexcept
        { return droppedMappedControllerEventCount.load(std::memory_order_relaxed); }

    void flushMappedControllerEventsSync()
    {
        dispatchPendingMappedControllerEvents();
        applyPendingLearnedChord();
    }

private:
    /** Mapped controller moves and learned chords reach the message thread through a flag that a 100 Hz timer
        reads: JUCE's AsyncUpdater takes a lock and may allocate when the audio thread triggers it (D159). */
    class PluginMappedActionAsyncBridge final : private juce::Timer
    {
    public:
        explicit PluginMappedActionAsyncBridge(SynthAudioProcessor& o) : owner(o) { startTimerHz(100); }
        ~PluginMappedActionAsyncBridge() override { stopTimer(); }
        /** Audio thread: one store, nothing else. */
        void triggerAsyncUpdate() noexcept { pending.store(true, std::memory_order_release); }
    private:
        void timerCallback() override
        {
            owner.showHeardKeys();
            if (! pending.exchange(false, std::memory_order_acq_rel))
                return;
            owner.dispatchPendingMappedControllerEvents();
            owner.applyPendingLearnedChord();
        }
        SynthAudioProcessor& owner;
        std::atomic<bool> pending { false };
    };

    bool buildSanitizedParameterStateTree(const juce::ValueTree& incomingState,
                                          juce::ValueTree& sanitizedState);
    void enqueuePluginControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept;
    void enqueuePluginMappedControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept;
    int drainPendingMappedControllerEvents(coolsynth::midi::ControllerMidiEvent* destination, int maxEvents) noexcept;
    void dispatchPendingMappedControllerEvents();
    /** Message thread: writes a chord the engine learned into the chord parameters and ends learning. */
    void applyPendingLearnedChord();
    std::unique_ptr<juce::XmlElement> createProcessorStateXml() const;
    /** The learned bindings saved under parent; renamed maps an old parameter ID to the one that took its place. */
    std::vector<coolsynth::midi::LearnedCcBinding> parseLearnedMidiBindingsXml(const juce::XmlElement& parent,
                                                                               const std::map<juce::String, juce::String>& renamed = {}) const;
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                       float normalizedValue);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;

    /** Reads every parameter into the engine settings once per block; lock-free. */
    /** Audio thread: the settings for this block, rebuilt from the parameters unless a bulk edit is under way or
        changed them while they were read; then the last whole settings (D157). */
    const coolsynth::synth::v3::EngineSettings& makeEngineSettings() noexcept;
    /** Reads every parameter into `settings`. Any thread. */
    void fillEngineSettings(coolsynth::synth::v3::EngineSettings& settings) const noexcept;
    /** Reads in every sampled-instrument style the patch names and hands each to `visit` (null if not installed). */
    template <typename Visit>
    void forEachSampledStyle(Visit&& visit);
    static coolsynth::synth::GlobalParameterPointers bindParameterPointers(APVTS& state);

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    coolsynth::plugin::ParameterUndoHistory::Snapshot captureSnapshot(juce::String label) const;
    std::vector<float> captureNormalisedValues() const;
    void restoreSnapshot(const coolsynth::plugin::ParameterUndoHistory::Snapshot& snapshot);


    APVTS parameters;
    // The on-screen keyboard (D159): the audio thread never touches its state, which takes a lock that a mouse click
    // holds. Clicked keys reach the audio through one lock-free queue; keys the audio heard come back through another
    // and a message-thread timer shows them on the keyboard.
    juce::MidiKeyboardState keyboardState;
    struct KeyEvent
    {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0; // 0: key up
    };
    static constexpr int keyQueueSize = 512;
    std::atomic<bool> keyboardDisplayCleared { false }; // panic or reset: the keyboard shows every key up
    juce::AbstractFifo clickedKeys { keyQueueSize };
    std::array<KeyEvent, keyQueueSize> clickedKeyEvents {};
    juce::AbstractFifo heardKeys { keyQueueSize };
    std::array<KeyEvent, keyQueueSize> heardKeyEvents {};
    bool showingHeardKeys = false; // message thread: the keyboard is being told what was heard, not clicked
    static void pushKey(juce::AbstractFifo& fifo, std::array<KeyEvent, keyQueueSize>& events, KeyEvent event) noexcept;
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    /** Message thread: shows on the keyboard the keys the audio heard. */
    void showHeardKeys();

    // Keys from the MiniLab's own port, timestamped by the MIDI thread (D159): placed one block late at their exact
    // distance apart, where JUCE's MidiMessageCollector takes a lock and squeezes a late batch into the block's end.
    struct LiveMessage
    {
        std::array<std::uint8_t, 3> bytes {};
        std::uint8_t size = 0;
        double milliseconds = 0.0; // juce::Time::getMillisecondCounterHiRes() when it arrived
    };
    static constexpr int liveQueueSize = 1024;
    juce::AbstractFifo liveMidi { liveQueueSize };
    std::array<LiveMessage, liveQueueSize> liveMessages {};
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    std::vector<coolsynth::midi::LearnedCcBinding> learnedMidiBindings;
    coolsynth::synth::GlobalParameterPointers parameterValues;
    juce::StringArray knownParameterIds; // every parameter, for patch and state validation
    std::array<coolsynth::parameters::v3::PartParameterPointers, coolsynth::synth::v3::partCount> partParameterValues;
    std::atomic<float>* partModeValue = nullptr;
    coolsynth::parameters::v3::MasterEffectPointers masterEffectValues;

    // The user's own sounds: one name and one UserLibrary handle per generator slot (part A's three, then B's).
    static constexpr int userSoundSlots = coolsynth::synth::v3::partCount * coolsynth::parameters::v3::partGeneratorSlots;
    std::array<juce::String, userSoundSlots> userSoundNames;   // message thread
    std::array<std::atomic<int>, userSoundSlots> userSoundHandles; // read by the audio thread
    void refreshUserSounds();
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeRedirected(juce::ValueTree&) override;
    // Two settings, the block's and the one being read; which is whole: audio thread (D157).
    std::array<coolsynth::synth::v3::EngineSettings, 2> settingsBuffers;
    int liveSettings = 0;
    bool settingsPrimed = false;
    std::atomic<std::uint32_t> settingsSequence { 0 }; // odd while a bulk edit runs
    std::atomic<int> bulkEditDepth { 0 };
    std::atomic<std::uint32_t> soundGeneration { 0 }; // counts presets and states loaded (D158)
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
    // Per part: a learned chord packed by the audio thread, applied on the message thread.
    std::array<std::atomic<std::uint32_t>, coolsynth::synth::v3::partCount> pendingLearnedChord {};
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

    // The flight recorder's tripwires (diagnostics/FlightRecorder.h): audio thread only.
    void checkBlockTripwires(juce::AudioBuffer<float>& buffer, double startMs) noexcept;
    void startFlightRecorderIfWanted();
    float tripwireWorstMs = 0.0f, tripwirePeak = 0.0f;
    int tripwireVoices = 0, tripwireClipped = 0, tripwireSamples = 0;

    std::atomic<bool> instrumentLoading { false };
    int notesHeldBack = 0; // keys pressed while loading, ignored: audio thread

    std::unique_ptr<coolsynth::ui::InterfaceModel> interfaceModel; // last: it refers to the parameters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
