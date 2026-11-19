#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>

#include "midi/ControllerSetup.h"
#include "midi/DuplicateNoteFilter.h"
#include "plugin/RemoteControlPages.h"
#include "plugin/ParameterUndoHistory.h"
#include "plugin/ProcessorScopeFifo.h"
#include "parameters/v3/PartParameters.h"
#include "synth/SynthParameters.h"
#include "synth/v3/EngineV3.h"

namespace coolsynth::ui { class InterfaceModel; class ControllerInput; class MiniLab3Driver; }

class SynthAudioProcessor final : public juce::AudioProcessor,
                                  public clap_juce_extensions::clap_properties,                    // is_clap (D216)
                                  public clap_juce_extensions::clap_juce_audio_processor_capabilities, // remote control pages
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::ValueTree::Listener,
                                  private juce::MidiKeyboardState::Listener
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    /** A CoolSynth a host or the app made, as against a test or a tool. Under CLAP JUCE reports the wrapper type as
        Undefined, which tests and tools also have: the CLAP wrapper's own flag tells them apart (D216). */
    [[nodiscard]] bool madeByHostOrApp() const noexcept { return wrapperType != wrapperType_Undefined || is_clap; }
    /** In a DAW, as VST3 or CLAP: not the app, not a test. */
    [[nodiscard]] bool inDaw() const noexcept { return wrapperType != wrapperType_Standalone && madeByHostOrApp(); }

    SynthAudioProcessor();
    ~SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }

    // ---- CLAP remote control pages (D216): a controller's eight knobs follow the panel's page ------------------
    bool supportsRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPageFill(uint32_t pageIndex, juce::String& sectionName, uint32_t& pageID, juce::String& pageName,
                                std::array<juce::AudioProcessorParameter*, CLAP_REMOTE_CONTROLS_COUNT>& params) noexcept override;
    /** Message thread, CLAP only: rebuilds the pages when the panel's layer or a slot's type changed, and tells the
        host which page the panel shows. */
    void followRemoteControls();

    const juce::String getName() const override { return "CoolSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // The factory bank as host programs, named "Room: Preset".
    int getNumPrograms() override;
    /** The sound a new CoolSynth starts with, the user's choice (D193: Contact; D212: Main Stage's Reese), and its
        program number (-1 if gone). Reset the sound comes back to it. */
    static constexpr char startSoundName[] = "Reese";
    [[nodiscard]] static int startSoundIndex();
    /** A state that is the built-in default untouched, "Init" (what CoolSynth started on before D193): nobody chose
        it, so the app and a host start on the start sound instead (D198). */
    [[nodiscard]] static bool isUntouchedDefault(const juce::String& name, bool dirty) { return name == "Init" && ! dirty; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::unique_ptr<juce::XmlElement> createParameterStateXml();
    bool applyParameterStateXml(const juce::XmlElement& stateXml);
    /** Adds every parameter `stateXml` lacks, at its default: for a state of an older version (D194). */
    void addMissingParameters(juce::XmlElement& stateXml) const;
    void resetAutomatableParametersToDefaults();
    juce::String getParameterStateTypeName() const;

    /** This CoolSynth's controller (D211): plays what the computer's controller setup links on the panel. Message thread. */
    coolsynth::ui::ControllerInput& getControllerInput();
    /** A plugin's window opened (D211): its MiniLab driver takes the MiniLab from any other CoolSynth here. */
    void claimMiniLab();
    /** Stop: every sound ends in the next block, as MIDI's All Sound Off (the keys held, the pedal, the releases, the
        arps, the delay's and reverb's tails), after a 3 ms fade so it does not click. Any thread. */
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
    /** For the layer screen (D195): the voices sounding at the last block, and the tempo the arps and synced effects
        follow (the host's while it plays, else the internal one). */
    [[nodiscard]] int voicesPlaying() const noexcept { return voicesNow.load(std::memory_order_relaxed); }
    /** What the playing sound may use after the voice cap (D222). */
    [[nodiscard]] int voiceLimit() const noexcept { return voiceLimitNow.load(std::memory_order_relaxed); }
    [[nodiscard]] float tempoBpm() const noexcept { return tempoNow.load(std::memory_order_relaxed); }

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
    [[nodiscard]] const coolsynth::synth::v3::EngineSettings& engineSettingsForTesting() noexcept { return makeEngineSettings(); }
    /** Live, a block finished late lets a sound or a released voice go (D158, D224): a test about the sound itself
        turns that off, so a busy computer cannot change what it hears. */
    void ignoreLoadForTesting(bool ignore) noexcept { loadIgnoredForTesting = ignore; }
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
    [[nodiscard]] juce::File playingMySound() const { return layerSounds[0].mySound; }

    /** What a layer plays since D194: a factory program, a saved sound, or nothing; its name and macro names. */
    struct LayerSound
    {
        int program = -1;
        juce::File mySound;
        juce::String name;
        std::array<juce::String, 4> macros {};
        bool combination = false; // loaded whole as one of a combination's two layers; a copy is not
        [[nodiscard]] bool combinedWith(const LayerSound& other) const
        {
            return (program >= 0 && program == other.program) || (mySound != juce::File() && mySound == other.mySound);
        }
    };
    [[nodiscard]] const LayerSound& layerSound(int layer) const noexcept { return layerSounds[layer == 1 ? 1u : 0u]; }
    /** A preset that fills both layers: it loads whole, wherever it is picked (D194). */
    [[nodiscard]] static bool isCombination(int program);
    /** Picks a factory sound for one layer (D194). A combination loads whole. A single sound fills `layer` and leaves
        the other layer, the chain at the end and the master as they are; if a combination was playing, the pick takes
        it apart: the other layer empties and the global settings become the sound's. One undo step. */
    void loadSoundIntoLayer(int program, int layer);
    /** The same for a saved sound: one that fills both layers loads whole. */
    bool loadMySoundIntoLayer(const juce::File& file, int layer);
    /** Every generator and processor slot of `layer` Off: the layer is left out (D190). */
    void emptyLayer(int layer);
    /** Preferences' Reset (D199): the sound only. The start sound on layer A, layer B empty, the faders at the top,
        the Layers page and knobs 7-8 centred, solo off. The Preferences choices and MIDI learn stay. */
    void resetSound();

    /** The Layers page's actions (D196), each one undo step: the whole layer, its effects and arp included. */
    void copyLayer(int from, int to);
    void swapLayers();
    /** Copies `from` onto the other layer and pulls the two apart: 14 cents of detune, half the stereo spread, each
        layer 3 dB down so the pair is as loud as one. */
    void doubleLayer(int from);
    /** Copies `from` onto the other layer `semitones` away (an octave below or above), 6 dB under it. */
    void octaveLayer(int from, int semitones);
    /** Hears one layer alone while it is edited, or both again (-1). Not saved: a working aid. */
    void setSoloLayer(int layer) noexcept { soloLayerValue.store(layer == 0 || layer == 1 ? layer : -1, std::memory_order_relaxed); }
    [[nodiscard]] int soloLayer() const noexcept { return soloLayerValue.load(std::memory_order_relaxed); }
    /** The factory sound playing, or -1 when the state holds one the bank does not have (D193). The host's program
        number (getCurrentProgram) stays a valid one either way. */
    [[nodiscard]] int playingFactoryProgram() const noexcept { return layerSounds[0].program; }
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

    /** Tests: what the audio thread passed on for the panel and the chord parameters, now. */
    void flushMappedControllerEventsSync()
    {
        dispatchPendingControllerEvents();
        applyPendingLearnedChord();
    }

private:
    // Private as in JUCE 9: an editor is made through createEditorAndMakeActive, so getActiveEditor knows it.
    juce::AudioProcessorEditor* createEditor() override;

    /** Controller moves and learned chords reach the message thread through a flag that a 100 Hz timer reads:
        JUCE's AsyncUpdater takes a lock and may allocate when the audio thread triggers it (D159). */
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
            // A second after the plugin is made: a host's plugin scan, which makes one and drops it, never wakes the
            // MiniLab (D211).
            if (ticks < 100 && ++ticks == 100)
                owner.startMiniLabDriver();
            owner.followRemoteControls(); // CLAP: the pages follow the panel (D216)
            if (! pending.exchange(false, std::memory_order_acq_rel))
                return;
            owner.dispatchPendingControllerEvents();
            owner.applyPendingLearnedChord();
        }
        SynthAudioProcessor& owner;
        std::atomic<bool> pending { false };
        int ticks = 0;
    };

    bool buildSanitizedParameterStateTree(const juce::ValueTree& incomingState,
                                          juce::ValueTree& sanitizedState);
    /** Audio thread: a controller's message for the panel, lock-free (D211). */
    void enqueueControllerEvent(const coolsynth::midi::ControllerEvent& event) noexcept;
    /** Message thread, once, in a plugin: the MiniLab driver (D211). The app makes its own, with its devices. */
    void startMiniLabDriver();
    void dispatchPendingControllerEvents();
    /** Message thread: writes a chord the engine learned into the chord parameters and ends learning. */
    void applyPendingLearnedChord();
    std::unique_ptr<juce::XmlElement> createProcessorStateXml() const;
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                       float normalizedValue);

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

    /** Lives while a sound loads (D194): at its end the sound counts as new, so notes ringing keep the one before
        (D158), only if a parameter changed. The engine compared its settings byte for byte, padding and all, so the
        same sound loaded twice could count as new; values compare without that. Declare it after the BulkEdit. */
    class NewSoundScope final
    {
    public:
        explicit NewSoundScope(SynthAudioProcessor& p) : owner(p), before(p.captureNormalisedValues()) {}
        ~NewSoundScope()
        {
            if (owner.captureNormalisedValues() != before)
                owner.soundGeneration.fetch_add(1, std::memory_order_relaxed);
        }
        NewSoundScope(const NewSoundScope&) = delete;
        NewSoundScope& operator=(const NewSoundScope&) = delete;

    private:
        SynthAudioProcessor& owner;
        std::vector<float> before;
    };
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
    coolsynth::synth::GlobalParameterPointers parameterValues;
    juce::StringArray knownParameterIds; // every parameter, for patch and state validation
    std::array<coolsynth::parameters::v3::PartParameterPointers, coolsynth::synth::v3::partCount> partParameterValues;
    std::atomic<float>* partModeValue = nullptr;
    std::atomic<float>* velocitySplitValue = nullptr; // D190
    std::atomic<float>* velocityFadeValue = nullptr;
    std::atomic<float>* layerBalanceValue = nullptr; // D196
    std::atomic<float>* layerDetuneValue = nullptr;
    std::atomic<float>* layerSpreadValue = nullptr;
    std::atomic<int> soloLayerValue { -1 };          // D196: the layer heard alone, or -1
    void copyLayerValues(int from, int to);
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
    bool hostWasPlaying = false; // audio thread: the transport starting reseeds the engine (D222)
    static constexpr int bounceWaitMilliseconds = 10000; // a bounce's wait for each sound (D222)
    bool bounceGaveUp = false; // audio thread: a sound did not come within that wait
    bool loadIgnoredForTesting = false;
    std::atomic<bool> panicRequested { false };
    std::vector<coolsynth::plugin::RemotePage> remotePagesNow; // message thread: the CLAP pages the host has (D216)
    int remotePageShown = -1;
    int remoteModelRevision = -1;
    coolsynth::midi::DuplicateNoteFilter duplicateNotes; // audio thread: one note by two routes plays once (D216)
    std::int64_t samplesSoFar = 0;                       // audio thread: the filter's clock
    int currentProgram = 0;
    juce::File playingMySoundFile;                                  // D180: a saved sound playing, else none
    bool playingFactorySound = true;                                // D193: currentProgram is the sound playing
    std::array<LayerSound, 2> layerSounds {};                       // D194: what each layer plays
    /** Loads a whole sound with `applyWhole`, then keeps only its layer A, in `layer` (D194). */
    bool loadIntoLayer(int layer, const std::function<bool()>& applyWhole, LayerSound sound);
    [[nodiscard]] bool layerHasSound(int layer) const;
    void updatePatchName();
    /** After a state is loaded: which sound it holds, by its My Sounds file or its name (D193). */
    void restorePlayingSound(const juce::String& name, const juce::String& mySoundPath);
    std::array<std::array<juce::String, 4>, 2> mySoundMacroNames {}; // its macro names, part A and B
    std::array<coolsynth::midi::ControllerEvent, 256> controllerEventSlots {};
    juce::AbstractFifo controllerEvents { static_cast<int> (controllerEventSlots.size()) };
    std::atomic<uint64_t> droppedControllerEventCount { 0 };
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
    std::atomic<int> voicesNow { 0 };       // D195: the layer screen's voice count
    std::atomic<int> voiceLimitNow { 0 };   // D222: and what the cap allows
    std::atomic<float> tempoNow { 120.0f }; // D195: the tempo followed
    double secondBusyMs = 0.0; // audio thread: this second's block time (D172)
    float secondWorstMs = 0.0f;
    int secondSamples = 0;
    float tripwireWorstMs = 0.0f, tripwirePeak = 0.0f;
    int tripwireVoices = 0, tripwireClipped = 0, tripwireSamples = 0;
    float loadTookMs = 0.0f; // the engine's load over at least 64 samples (FL's slivers): audio thread
    int loadSamples = 0;

    std::atomic<bool> instrumentLoading { false };
    int notesHeldBack = 0; // keys pressed while loading, ignored: audio thread
    static constexpr int reservedEndingEvents = 64; // a block's last event slots, kept for releases (D170)
    int eventsDropped = 0;                          // this block's events that found no slot: audio thread

    std::unique_ptr<coolsynth::ui::InterfaceModel> interfaceModel; // last: it refers to the parameters
    std::unique_ptr<coolsynth::ui::ControllerInput> controllerInput; // after it: it plays on the model (D211)
    std::unique_ptr<coolsynth::ui::MiniLab3Driver> miniLab;          // a plugin's own (D211); last, gone first
    bool miniLabStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
