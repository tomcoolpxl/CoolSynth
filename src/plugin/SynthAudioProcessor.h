#pragma once

#include <algorithm>
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

    /** The last second's work (D172), for the screen: each thread's busy share of the second in percent (0 the
        audio thread, then the helpers), and the slowest block against its time. Any thread. */
    struct CpuLoad
    {
        int threads = 1;
        std::array<float, 8> percent {};
        float worstBlock = 0.0f; // 1 = the slowest block took all of its time
    };
    [[nodiscard]] CpuLoad cpuLoad() const noexcept
    {
        CpuLoad load;
        load.threads = std::clamp(threadLoadCount.load(std::memory_order_relaxed), 1, static_cast<int>(load.percent.size()));
        for (int t = 0; t < load.threads; ++t)
            load.percent[static_cast<std::size_t>(t)] = threadLoad[static_cast<std::size_t>(t)].load(std::memory_order_relaxed);
        load.worstBlock = worstBlockShare.load(std::memory_order_relaxed);
        return load;
    }

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
    [[nodiscard]] coolsynth::synth::v3::EngineV3& engineForTesting() noexcept { return synthEngine; }
    /** Multi-core (D164): Auto, the default, renders sounds that overlap on several cores; off keeps them all on the
        audio thread. Serial anyway while a host renders offline. COOLSYNTH_MULTICORE=0 switches it off. */
    void setMultiCore(bool on) noexcept { multiCoreWanted.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool multiCore() const noexcept { return multiCoreWanted.load(std::memory_order_relaxed); }
    /** Multi-core as chosen in Options (D174): applied now and remembered, by the standalone app in its own settings
        (rememberMultiCore), else for the plugin on this computer, read again by every CoolSynth plugin it opens. */
    void chooseMultiCore(bool on);
    /** Set by the standalone app: where it keeps the Multi-core choice. Message thread. */
    std::function<void(bool)> rememberMultiCore;
    /** Set by the standalone app: opens its audio and MIDI settings; the panel's Options offers it when set. */
    std::function<void()> showAudioSettings;

    // ---- My Sounds (D180): the player's own saved sounds, message thread ------------------------
    enum class SaveResult { saved, exists, failed };
    /** Saves the sound as it is now under `name` in My Sounds (presets/MySounds.h); `exists` when a sound has that
        name and replace is false. The saved sound is then the one playing. */
    SaveResult saveMySound(const juce::String& name, bool replace);
    /** Loads a saved sound whole, as a host's saved state is (D157); notes ringing keep the sound before (D158). */
    bool loadMySound(const juce::File& file);
    /** The saved sound playing, or no file when a factory sound plays. */
    [[nodiscard]] juce::File playingMySound() const { return playingMySoundFile; }
    /** A part's four macro names: the playing saved sound's, else the factory sound's; empty where it has none. */
    [[nodiscard]] std::array<juce::String, 4> macroNames(int part) const;
    /** The engine pills a saved sound's file carries. */
    [[nodiscard]] static juce::StringArray mySoundPills(const juce::File& file);

    /** The keyboard's velocity curve (D175), for every key that reaches the sound: Soft, a light touch plays loud;
        Normal, as played; Hard, loud takes a firm touch. Power curves, the same at the softest and the hardest. */
    enum VelocityCurve { softCurve = 0, normalCurve = 1, hardCurve = 2 };
    void setVelocityCurve(int curve) noexcept { velocityCurveWanted.store(juce::jlimit(0, 2, curve), std::memory_order_relaxed); }
    [[nodiscard]] int velocityCurve() const noexcept { return velocityCurveWanted.load(std::memory_order_relaxed); }
    /** As chooseMultiCore: applied now and remembered (rememberVelocityCurve in the app, else per computer). */
    void chooseVelocityCurve(int curve);
    std::function<void(int)> rememberVelocityCurve;
    /** The screen's Hold (D184): a sustain pedal held down while the arp is off, the arp's latch while it runs. */
    void setScreenHold(bool on) noexcept { screenHold.store(on, std::memory_order_relaxed); }
    /** A note-on's velocity (0..1) through a curve; never 0, which would end the note. */
    [[nodiscard]] static float shapeVelocity(float velocity, int curve) noexcept;
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
            owner.releaseRetiredUserSounds();
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
    std::atomic<float>* velocitySplitValue = nullptr; // D190
    std::atomic<float>* velocityFadeValue = nullptr;
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
    std::atomic<bool> multiCoreWanted { juce::SystemStats::getEnvironmentVariable("COOLSYNTH_MULTICORE", "1") != "0" };
    std::atomic<int> velocityCurveWanted { normalCurve }; // D175
    std::atomic<bool> screenHold { false }; // D184
    bool screenSustainSent = false;         // audio thread
    static constexpr float brightnessRangeNormalised = 0.15f; // knob 7 at its ends: 1.5 octaves of cutoff (D176)
    std::atomic<std::uint32_t> oldestSoundingGeneration { 0 }; // the audio's oldest sound still ringing
    bool loadingSound = false; // message thread: a preset or a state is being applied
    struct RetiredUserSound
    {
        int handle = -1;
        std::uint32_t generation = 0; // the sound that used it
    };
    std::vector<RetiredUserSound> retiredUserSounds; // message thread: kept while that sound still rings
    /** Message thread: lets go of the user's files that only sounds which have ended still used. */
    void releaseRetiredUserSounds();
    coolsynth::synth::v3::EngineV3 synthEngine;
    std::atomic<bool> panicRequested { false };
    int currentProgram = 0;
    juce::File playingMySoundFile;                                  // D180: a saved sound playing, else none
    std::array<std::array<juce::String, 4>, 2> mySoundMacroNames {}; // its macro names, part A and B
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
    static juce::PropertiesFile& pluginOptions(); // the plugin's per-computer choices (D174)

    // The flight recorder's tripwires (diagnostics/FlightRecorder.h): audio thread only.
    void checkBlockTripwires(juce::AudioBuffer<float>& buffer, double startMs) noexcept;
    void startFlightRecorderIfWanted();
    std::array<std::atomic<float>, 8> threadLoad {}; // the last second, percent per thread (D172)
    std::atomic<int> threadLoadCount { 1 };
    std::atomic<float> worstBlockShare { 0.0f };
    double secondBusyMs = 0.0; // audio thread: this second's block time (D172)
    float secondWorstMs = 0.0f;
    int secondSamples = 0;
    float tripwireWorstMs = 0.0f, tripwirePeak = 0.0f;
    int tripwireVoices = 0, tripwireClipped = 0, tripwireSamples = 0;

    std::atomic<bool> instrumentLoading { false };
    int notesHeldBack = 0; // keys pressed while loading, ignored: audio thread
    static constexpr int reservedEndingEvents = 64; // a block's last event slots, kept for releases (D170)
    int eventsDropped = 0;                          // this block's events that found no slot: audio thread

    std::unique_ptr<coolsynth::ui::InterfaceModel> interfaceModel; // last: it refers to the parameters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
