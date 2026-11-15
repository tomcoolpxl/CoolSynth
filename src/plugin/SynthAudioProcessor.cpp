#include "SynthAudioProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <span>

#include "midi/LearnedBindingsXml.h"
#include "midi/MidiToControllerEvent.h"
#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"
#include "BuildInfo.h"
#include "diagnostics/AudioThreadAlarm.h"
#include "diagnostics/FlightRecorder.h"
#include "plugin/AuditionEditor.h"
#include "plugin/PanelEditor.h"
#include "interface/InterfaceModel.h"
#include "presets/FactoryPresets.h"
#include "presets/StateMigration.h"
#include "synth/v3/AudioWorkers.h"
#include "synth/v3/UserLibrary.h"

namespace
{
    inline constexpr char apvtsParameterChildType[] = "PARAM";
    inline constexpr char apvtsParameterIdProperty[] = "id";
    inline constexpr char apvtsParameterValueProperty[] = "value";
    inline constexpr char processorStateRootTag[] = "COOLSYNTH_PROCESSOR_STATE";
    inline constexpr char processorStateVersionProperty[] = "formatVersion";
    // 9: V3 parameter contract; 10: the master effect slots (a version 9 state is carried over, D135)
    inline constexpr int processorStateFormatVersion = 10;
    inline constexpr char processorStateProductProperty[] = "product";
    inline constexpr char processorStateStateTypeProperty[] = "stateType";
    inline constexpr char processorStatePatchNameProperty[] = "patchName";
    inline constexpr char processorStatePatchDirtyProperty[] = "patchDirty";

    // Sets an atomic flag for the current scope and restores the previous value after.
    struct AtomicFlagScope
    {
        AtomicFlagScope(std::atomic<bool>& flagToSet, bool value) noexcept
            : flag(flagToSet), previous(flagToSet.exchange(value)) {}
        ~AtomicFlagScope() noexcept { flag.store(previous); }
        std::atomic<bool>& flag;
        bool previous;
    };
    inline constexpr char learnedMidiMappingsTag[] = "LEARNED_MIDI_MAPPINGS";

    std::optional<coolsynth::synth::EngineMidiEvent> makeEngineMidiEvent(const juce::MidiMessage& message,
                                                                         int sampleOffset) noexcept
    {
        using namespace coolsynth::synth;

        EngineMidiEvent event;
        event.sampleOffset = juce::jmax(0, sampleOffset);

        if (message.isNoteOn())
        {
            event.type = EngineMidiEventType::noteOn;
            event.noteNumber = static_cast<uint8_t> (message.getNoteNumber());
            event.value = juce::jlimit(0.0f, 1.0f, message.getFloatVelocity());
            return event;
        }

        if (message.isNoteOff())
        {
            event.type = EngineMidiEventType::noteOff;
            event.noteNumber = static_cast<uint8_t> (message.getNoteNumber());
            return event;
        }

        if (message.isPitchWheel())
        {
            event.type = EngineMidiEventType::pitchBend;
            event.value = juce::jlimit(-1.0f,
                                       1.0f,
                                       static_cast<float> (message.getPitchWheelValue() - 8192) / 8192.0f);
            return event;
        }

        if (message.isControllerOfType(1))
        {
            event.type = EngineMidiEventType::modWheel;
            event.value = static_cast<float> (message.getControllerValue()) / 127.0f;
            return event;
        }

        if (message.isControllerOfType(64))
        {
            event.type = EngineMidiEventType::sustainPedal;
            event.value = message.getControllerValue() >= 64 ? 1.0f : 0.0f;
            return event;
        }

        // Key pressure and the expression pedal (decision D136).
        if (message.isChannelPressure())
        {
            event.type = EngineMidiEventType::channelPressure;
            event.value = static_cast<float> (message.getChannelPressureValue()) / 127.0f;
            return event;
        }

        if (message.isAftertouch())
        {
            event.type = EngineMidiEventType::polyPressure;
            event.noteNumber = static_cast<uint8_t> (message.getNoteNumber());
            event.value = static_cast<float> (message.getAfterTouchValue()) / 127.0f;
            return event;
        }

        if (message.isControllerOfType(11))
        {
            event.type = EngineMidiEventType::expression;
            event.value = static_cast<float> (message.getControllerValue()) / 127.0f;
            return event;
        }

        if (message.isAllNotesOff())
        {
            event.type = EngineMidiEventType::allNotesOff;
            return event;
        }

        if (message.isAllSoundOff())
        {
            event.type = EngineMidiEventType::allSoundOff;
            return event;
        }

        if (message.isResetAllControllers())
        {
            event.type = EngineMidiEventType::resetControllers;
            return event;
        }

        return std::nullopt;
    }

    bool isReservedSynthController(const coolsynth::midi::ControllerMidiEvent& event) noexcept
    {
        return coolsynth::midi::isReservedSynthControllerEvent(event);
    }

    const juce::XmlElement* findSingleMatchingChild(const juce::XmlElement& parent,
                                                    juce::StringRef tagName) noexcept
    {
        const juce::XmlElement* matchingChild = nullptr;

        for (auto* child : parent.getChildIterator())
        {
            if (!child->hasTagName(tagName))
                continue;

            if (matchingChild != nullptr)
                return nullptr;

            matchingChild = child;
        }

        return matchingChild;
    }
}

SynthAudioProcessor::SynthAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "CoolSynthState", coolsynth::parameters::createParameterLayout())
    , midiMappingEngine(parameters)
    , parameterValues(bindParameterPointers(parameters))
    , partParameterValues { coolsynth::parameters::v3::bindPartPointers(parameters, coolsynth::parameters::v3::Part::a),
                            coolsynth::parameters::v3::bindPartPointers(parameters, coolsynth::parameters::v3::Part::b) }
    , partModeValue(parameters.getRawParameterValue(coolsynth::parameters::v3::partModeParameterId))
    , masterEffectValues(coolsynth::parameters::v3::bindMasterEffectPointers(parameters))
    , mappedActionBridge(*this)
{
    for (auto* parameter : getParameters())
    {
        parameter->addListener(this);
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            knownParameterIds.add(withId->paramID);
    }
    for (auto& handle : userSoundHandles)
        handle.store(-1);
    parameters.state.addListener(this);
    keyboardState.addListener(this);

    // The plugin's Multi-core choice is the computer's, not the project's (D174); the standalone app applies its own.
    if (wrapperType != wrapperType_Standalone && wrapperType != wrapperType_Undefined
        && juce::SystemStats::getEnvironmentVariable("COOLSYNTH_MULTICORE", "1") != "0")
        if (auto& options = pluginOptions(); options.containsKey("multiCore"))
            setMultiCore(options.getBoolValue("multiCore", true));
    // So is its velocity curve (D175): the player's keyboard is the computer's.
    if (wrapperType != wrapperType_Standalone && wrapperType != wrapperType_Undefined)
        setVelocityCurve(pluginOptions().getIntValue("velocityCurve", normalCurve));
}

float SynthAudioProcessor::shapeVelocity(float velocity, int curve) noexcept
{
    // Power curves: Soft lifts the light touches (velocity 32 of 127 plays as 56), Hard holds them back (32 as 14);
    // both leave the softest and the hardest where they are.
    const float v = std::clamp(velocity, 0.0f, 1.0f);
    float shaped = v;
    if (curve == softCurve)
        shaped = std::pow(v, 0.6f);
    else if (curve == hardCurve)
        shaped = std::pow(v, 1.6f);
    return v > 0.0f ? std::max(shaped, 1.0f / 127.0f) : 0.0f;
}

void SynthAudioProcessor::chooseVelocityCurve(int curve)
{
    setVelocityCurve(curve);
    if (rememberVelocityCurve)
        rememberVelocityCurve(velocityCurve());
    else
    {
        pluginOptions().setValue("velocityCurve", velocityCurve());
        pluginOptions().saveIfNeeded();
    }
}

juce::PropertiesFile& SynthAudioProcessor::pluginOptions()
{
    // %APPDATA%\CoolSynth\plugin.options: what the plugin remembers on this computer. Message thread.
    static juce::PropertiesFile file = []
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "plugin";
        options.filenameSuffix = ".options";
        options.folderName = "CoolSynth";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        return juce::PropertiesFile(options);
    }();
    return file;
}

void SynthAudioProcessor::chooseMultiCore(bool on)
{
    setMultiCore(on);
    if (rememberMultiCore)
        rememberMultiCore(on);
    else
    {
        pluginOptions().setValue("multiCore", on);
        pluginOptions().saveIfNeeded();
    }
}

SynthAudioProcessor::~SynthAudioProcessor()
{
    keyboardState.removeListener(this);
    parameters.state.removeListener(this);
    for (const auto& retired : retiredUserSounds)
        coolsynth::synth::v3::UserLibrary::instance().release(retired.handle);
    retiredUserSounds.clear();
    for (auto& handle : userSoundHandles)
        if (const int held = handle.exchange(-1); held >= 0)
            coolsynth::synth::v3::UserLibrary::instance().release(held);
    for (auto* parameter : getParameters())
        parameter->removeListener(this);
}

// ---- The user's own sounds (decision D137) ----------------------------------------

void SynthAudioProcessor::setUserFile(int part, int slot, const juce::String& name)
{
    namespace pv3 = coolsynth::parameters::v3;
    const auto key = pv3::userFileKey(part == 0 ? pv3::Part::a : pv3::Part::b, juce::jlimit(1, pv3::partGeneratorSlots, slot));
    auto files = parameters.state.getOrCreateChildWithName(pv3::userFilesTag, nullptr);
    if (name.trim().isEmpty())
        files.removeProperty(key, nullptr);
    else
        files.setProperty(key, name.trim().replaceCharacter('\\', '/'), nullptr);
}

juce::String SynthAudioProcessor::getUserFile(int part, int slot) const
{
    namespace pv3 = coolsynth::parameters::v3;
    const auto key = pv3::userFileKey(part == 0 ? pv3::Part::a : pv3::Part::b, juce::jlimit(1, pv3::partGeneratorSlots, slot));
    return parameters.state.getChildWithName(pv3::userFilesTag).getProperty(key).toString();
}

juce::String SynthAudioProcessor::getUserFileStatus(int part, int slot) const
{
    const auto index = static_cast<std::size_t>(juce::jlimit(0, 1, part) * coolsynth::parameters::v3::partGeneratorSlots
                                                + juce::jlimit(1, coolsynth::parameters::v3::partGeneratorSlots, slot) - 1);
    if (userSoundNames[index].isEmpty())
        return {};
    const int handle = userSoundHandles[index].load();
    if (handle < 0)
        return "too many files in use at once";
    return coolsynth::synth::v3::UserLibrary::instance().problem(handle);
}

bool SynthAudioProcessor::loadUserSoundsNow(int timeoutMilliseconds)
{
    refreshUserSounds();
    bool allThere = true;
    for (std::size_t i = 0; i < userSoundHandles.size(); ++i)
        if (userSoundNames[i].isNotEmpty())
            allThere = coolsynth::synth::v3::UserLibrary::instance().waitUntilLoaded(userSoundHandles[i].load(), timeoutMilliseconds) && allThere;
    return allThere;
}

void SynthAudioProcessor::refreshUserSounds()
{
    namespace pv3 = coolsynth::parameters::v3;
    const auto files = parameters.state.getChildWithName(pv3::userFilesTag);
    for (int part = 0; part < coolsynth::synth::v3::partCount; ++part)
        for (int slot = 1; slot <= pv3::partGeneratorSlots; ++slot)
        {
            const auto index = static_cast<std::size_t>(part * pv3::partGeneratorSlots + slot - 1);
            const auto name = files.getProperty(pv3::userFileKey(part == 0 ? pv3::Part::a : pv3::Part::b, slot)).toString();
            if (name == userSoundNames[index])
                continue;
            // The new sound's handle first, so a file both names share stays in memory. (The library and its
            // loader thread start only once a patch names a file.)
            auto& library = coolsynth::synth::v3::UserLibrary::instance();
            const int handle = library.acquire(name);
            const int previous = userSoundHandles[index].exchange(handle);
            // A sound being replaced may ring on (D158): its files are let go only once it has ended.
            if (loadingSound && previous >= 0)
                retiredUserSounds.push_back({ previous, soundGeneration.load(std::memory_order_relaxed) - 1u });
            else
                library.release(previous);
            userSoundNames[index] = name;
        }
}

void SynthAudioProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType(coolsynth::parameters::v3::userFilesTag))
        refreshUserSounds();
}

void SynthAudioProcessor::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType(coolsynth::parameters::v3::userFilesTag))
        refreshUserSounds();
}

void SynthAudioProcessor::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType(coolsynth::parameters::v3::userFilesTag))
        refreshUserSounds();
}

void SynthAudioProcessor::valueTreeRedirected(juce::ValueTree&)
{
    refreshUserSounds(); // a whole new state: a saved sound or a project
}

void SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startFlightRecorderIfWanted();
    synthEngine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples(synthEngine.getLatencySamples());
    scopeFifo.clear();
    monoMixScratch.resize(static_cast<size_t>(samplesPerBlock));
}

void SynthAudioProcessor::releaseResources()
{
    synthEngine.releaseResources();
}

void SynthAudioProcessor::reset()
{
    panicRequested.store(false, std::memory_order_release);
    synthEngine.panic();
    keyboardDisplayCleared.store(true, std::memory_order_release); // hosts may call this on the audio thread: no lock here
    scopeFifo.clear();
}

void SynthAudioProcessor::pushKey(juce::AbstractFifo& fifo, std::array<KeyEvent, keyQueueSize>& events, KeyEvent event) noexcept
{
    const auto scope = fifo.write(1);
    if (scope.blockSize1 > 0)
        events[static_cast<std::size_t>(scope.startIndex1)] = event;
    else if (scope.blockSize2 > 0)
        events[static_cast<std::size_t>(scope.startIndex2)] = event;
}

void SynthAudioProcessor::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (showingHeardKeys)
        return; // the keyboard only shows what was played elsewhere
    pushKey(clickedKeys, clickedKeyEvents,
            { static_cast<std::uint8_t>(juce::jlimit(0, 127, midiNoteNumber)), static_cast<std::uint8_t>(juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f))) });
}

void SynthAudioProcessor::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (showingHeardKeys)
        return;
    pushKey(clickedKeys, clickedKeyEvents, { static_cast<std::uint8_t>(juce::jlimit(0, 127, midiNoteNumber)), 0 });
}

void SynthAudioProcessor::releaseRetiredUserSounds()
{
    const auto oldest = oldestSoundingGeneration.load(std::memory_order_acquire);
    for (auto it = retiredUserSounds.begin(); it != retiredUserSounds.end();)
    {
        if (static_cast<std::int32_t>(it->generation - oldest) < 0) // that sound has ended
        {
            coolsynth::synth::v3::UserLibrary::instance().release(it->handle);
            it = retiredUserSounds.erase(it);
        }
        else
            ++it;
    }
}

void SynthAudioProcessor::showHeardKeys()
{
    const juce::ScopedValueSetter<bool> showing(showingHeardKeys, true);
    if (keyboardDisplayCleared.exchange(false, std::memory_order_acq_rel))
        keyboardState.allNotesOff(0);
    const auto scope = heardKeys.read(heardKeys.getNumReady());
    scope.forEach([&](int index)
    {
        const auto event = heardKeyEvents[static_cast<std::size_t>(index)];
        if (event.velocity > 0)
            keyboardState.noteOn(1, event.note, static_cast<float>(event.velocity) / 127.0f);
        else
            keyboardState.noteOff(1, event.note, 0.0f);
    });
}

bool SynthAudioProcessor::pushLiveMidi(const juce::MidiMessage& message) noexcept
{
    const int size = message.getRawDataSize();
    if (size <= 0 || size > 3)
        return false; // notes, controllers, bend and pressure only
    const auto scope = liveMidi.write(1);
    LiveMessage* slot = scope.blockSize1 > 0 ? &liveMessages[static_cast<std::size_t>(scope.startIndex1)]
                      : scope.blockSize2 > 0 ? &liveMessages[static_cast<std::size_t>(scope.startIndex2)] : nullptr;
    if (slot == nullptr)
        return false;
    std::copy_n(message.getRawData(), size, slot->bytes.begin());
    slot->size = static_cast<std::uint8_t>(size);
    slot->milliseconds = juce::Time::getMillisecondCounterHiRes();
    return true;
}

bool SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.inputBuses.isEmpty())
        return false;

    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const coolsynth::diag::AudioBlockScope audioBlock; // the allocation alarm watches this thread now (D161)
    juce::ScopedNoDenormals noDenormals;
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();
    struct BlockTripwires
    {
        SynthAudioProcessor& owner;
        juce::AudioBuffer<float>& audio;
        double startMs;
        ~BlockTripwires() { owner.checkBlockTripwires(audio, startMs); }
    } tripwires { *this, buffer, blockStartMs };
    buffer.clear();

    if (panicRequested.exchange(false, std::memory_order_acq_rel))
    {
        synthEngine.panic();
        keyboardDisplayCleared.store(true, std::memory_order_release);
        midiMessages.clear();
    }

    std::array<coolsynth::synth::EngineMidiEvent, coolsynth::synth::maxEngineEventsPerBlock> engineEvents {};
    int engineEventCount = 0;

    // While the sound's sampled instrument is still being read in, a key is ignored rather than played half
    // ready (D154); notes already sounding go on. Checked before the keys, so even the first block of a new
    // preset holds them back.
    const auto& settings = makeEngineSettings();
    const bool loading = coolsynth::synth::v3::EngineV3::sampledInstrumentPending(settings);
    if (loading != instrumentLoading.load(std::memory_order_relaxed))
    {
        instrumentLoading.store(loading, std::memory_order_relaxed);
        coolsynth::diag::record(coolsynth::diag::Event::message, loading ? 0 : notesHeldBack, 0, 0.0f, 0.0f,
                                loading ? "instrument loading: keys wait" : "instrument ready; keys ignored while loading");
        notesHeldBack = 0;
    }

    // One path for every message: the loading gate, the engine's events, the flight recorder, the keyboard's display.
    const bool standalone = juce::JUCEApplicationBase::isStandaloneApp();
    const int curve = velocityCurve(); // one curve for the whole block
    auto take = [&](const juce::MidiMessage& message, int samplePosition, bool clicked)
    {
        if (loading && message.isNoteOn())
        {
            ++notesHeldBack;
            return;
        }
        if (auto event = makeEngineMidiEvent(message, samplePosition))
        {
            // Every key, from the host, the MiniLab or the screen, through the chosen velocity curve (D175).
            if (event->type == coolsynth::synth::EngineMidiEventType::noteOn && curve != normalCurve)
                event->value = shapeVelocity(event->value, curve);
            // The last slots are kept for what ends sound: a flood of controller moves may lose a move, never a
            // key's release, a pedal's lift or All Notes Off, which would leave a note hanging (D170).
            using Type = coolsynth::synth::EngineMidiEventType;
            const bool ending = event->type == Type::noteOff || event->type == Type::allNotesOff
                             || event->type == Type::allSoundOff || event->type == Type::resetControllers
                             || (event->type == Type::sustainPedal && event->value < 0.5f);
            const int limit = static_cast<int>(engineEvents.size()) - (ending ? 0 : reservedEndingEvents);
            if (engineEventCount < limit)
                engineEvents[static_cast<size_t> (engineEventCount++)] = *event;
            else
                ++eventsDropped;
        }
        if (message.isNoteOn())
        {
            coolsynth::diag::record(coolsynth::diag::Event::noteOn, message.getNoteNumber(), message.getVelocity());
            if (! clicked)
                pushKey(heardKeys, heardKeyEvents, { static_cast<std::uint8_t>(message.getNoteNumber()), message.getVelocity() });
        }
        else if (message.isNoteOff())
        {
            coolsynth::diag::record(coolsynth::diag::Event::noteOff, message.getNoteNumber());
            if (! clicked)
                pushKey(heardKeys, heardKeyEvents, { static_cast<std::uint8_t>(message.getNoteNumber()), 0 });
        }

        if (standalone || clicked)
            return;

        if (const auto event = coolsynth::midi::toControllerMidiEvent(message))
        {
            enqueuePluginControllerEvent(*event);

            if (! isReservedSynthController(*event))
                enqueuePluginMappedControllerEvent(*event);
        }
    };

    // Keys clicked on the screen, at the block's start (D159).
    {
        const auto clicks = clickedKeys.read(clickedKeys.getNumReady());
        clicks.forEach([&](int index)
        {
            const auto key = clickedKeyEvents[static_cast<std::size_t>(index)];
            take(key.velocity > 0 ? juce::MidiMessage::noteOn(1, key.note, key.velocity) : juce::MidiMessage::noteOff(1, key.note), 0, true);
        });
    }

    for (const auto metadata : midiMessages)
        take(metadata.getMessage(), metadata.samplePosition, false);

    // The MiniLab's own messages, one block late, each at its distance from the block before (D159).
    {
        const int blockLength = buffer.getNumSamples();
        const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const double windowStartMs = blockStartMs - 1000.0 * blockLength / rate;
        const auto live = liveMidi.read(liveMidi.getNumReady());
        live.forEach([&](int index)
        {
            const auto& m = liveMessages[static_cast<std::size_t>(index)];
            const int offset = juce::jlimit(0, blockLength - 1, static_cast<int>(std::lround((m.milliseconds - windowStartMs) * rate / 1000.0)));
            take(juce::MidiMessage(m.bytes.data(), m.size, 0.0), offset, false);
        });
    }

    if (eventsDropped > 0)
    {
        coolsynth::diag::record(coolsynth::diag::Event::eventsDropped, eventsDropped, engineEventCount);
        eventsDropped = 0;
    }

    // The three sources in time order; already nearly so, and an insertion sort neither allocates nor reorders ties.
    for (int i = 1; i < engineEventCount; ++i)
    {
        const auto event = engineEvents[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && engineEvents[static_cast<std::size_t>(j)].sampleOffset > event.sampleOffset)
        {
            engineEvents[static_cast<std::size_t>(j + 1)] = engineEvents[static_cast<std::size_t>(j)];
            --j;
        }
        engineEvents[static_cast<std::size_t>(j + 1)] = event;
    }

    coolsynth::synth::EngineTransportInfo transportInfo;

    if (! juce::JUCEApplicationBase::isStandaloneApp())
    {
        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto position = hostPlayHead->getPosition())
            {
                if (const auto bpm = position->getBpm())
                {
                    transportInfo.hostHasTempo = true;
                    transportInfo.hostBpm = *bpm;
                }
                if (const auto ppq = position->getPpqPosition())
                {
                    transportInfo.hostHasPpq = true;
                    transportInfo.hostPpqAtBlockStart = *ppq;
                }
                transportInfo.hostIsPlaying = position->getIsPlaying();
            }
        }
    }

    synthEngine.setMultiCore(multiCoreWanted.load(std::memory_order_relaxed) && ! isNonRealtime());
    synthEngine.render(buffer,
                       std::span<const coolsynth::synth::EngineMidiEvent>(engineEvents.data(),
                                                                          static_cast<size_t> (engineEventCount)),
                       settings,
                       transportInfo);
    oldestSoundingGeneration.store(synthEngine.oldestSoundingGeneration(), std::memory_order_release);

    // A chord learned from the keys becomes parameter values on the message thread.
    for (int part = 0; part < coolsynth::synth::v3::partCount; ++part)
    {
        if (const auto learned = synthEngine.consumeLearnedChord(part); learned != 0)
        {
            pendingLearnedChord[static_cast<size_t>(part)].store(learned, std::memory_order_release);
            mappedActionBridge.triggerAsyncUpdate();
        }
    }

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() >= 2 && numSamples <= static_cast<int>(monoMixScratch.size()))
    {
        const float* L = buffer.getReadPointer(0);
        const float* R = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            monoMixScratch[static_cast<size_t>(i)] = (L[i] + R[i]) * 0.5f;
        scopeFifo.write(monoMixScratch.data(), numSamples);
    }
    else if (buffer.getNumChannels() == 1)
    {
        scopeFifo.write(buffer.getReadPointer(0), numSamples);
    }
}

void SynthAudioProcessor::checkBlockTripwires(juce::AudioBuffer<float>& buffer, double startMs) noexcept
{
    namespace diag = coolsynth::diag;
    const int n = buffer.getNumSamples();
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const float budget = static_cast<float>(1000.0 * n / rate);
    const float took = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - startMs);
    // The engine lets an earlier sound go when the blocks run short of time (D158).
    synthEngine.setLoad(budget > 0.0f ? took / budget : 0.0f);

    // Each second of audio, the work of every thread: the audio thread's blocks and each helper's tasks (D172). The
    // screen shows it (cpuLoad); the flight recorder logs it. The helpers are the program's, so with two CoolSynths
    // in one host each sees the part of their work that falls in its own seconds.
    secondBusyMs += took;
    secondWorstMs = std::max(secondWorstMs, took);
    secondSamples += n;
    const bool secondOver = secondSamples >= static_cast<int>(rate);
    coolsynth::synth::v3::AudioWorkers::Activity activity;
    if (secondOver)
    {
        const double secondMs = 1000.0 * secondSamples / rate;
        activity = coolsynth::synth::v3::AudioWorkers::instance().takeActivity();
        const int threads = std::min(activity.threads, static_cast<int>(threadLoad.size()));
        threadLoad[0].store(static_cast<float>(100.0 * secondBusyMs / secondMs), std::memory_order_relaxed);
        for (int t = 1; t < threads; ++t)
            threadLoad[static_cast<std::size_t>(t)].store(static_cast<float>(100.0 * activity.taskMs[static_cast<std::size_t>(t)] / secondMs),
                                                          std::memory_order_relaxed);
        threadLoadCount.store(threads, std::memory_order_relaxed);
        worstBlockShare.store(budget > 0.0f ? secondWorstMs / budget : 0.0f, std::memory_order_relaxed);
        if (diag::FlightRecorder::instance().running())
        {
            diag::record(diag::Event::threadLoad, 0, 0, threadLoad[0].load(std::memory_order_relaxed), static_cast<float>(activity.worstWaitMs),
                         nullptr, static_cast<float>(activity.sharedBlocks));
            for (int t = 1; t < threads; ++t)
            {
                const auto i = static_cast<std::size_t>(t);
                if (activity.sharedBlocks > 0 || activity.lateWakes[i] > 0)
                    diag::record(diag::Event::threadLoad, t, 0, threadLoad[i].load(std::memory_order_relaxed),
                                 static_cast<float>(activity.worstWakeMs[i]), nullptr, static_cast<float>(activity.lateWakes[i]));
            }
        }
        secondBusyMs = 0.0;
        secondWorstMs = 0.0f;
        secondSamples = 0;
    }

    if (! diag::FlightRecorder::instance().running())
        return;

    // broken values make clicks: count and silence them
    int nonFinite = 0;
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite(data[i]))
            {
                data[i] = 0.0f;
                ++nonFinite;
            }
            peak = std::max(peak, std::abs(data[i]));
        }
    }
    if (nonFinite > 0)
        diag::record(diag::Event::nonFinite, nonFinite);

    const auto& stage = synthEngine.slowestStage();
    if (took > budget)
        diag::record(diag::Event::blockOverrun, stage.code, static_cast<int>(stage.milliseconds * 100.0f), took, budget);
    else if (took > 0.5f * budget)
        diag::record(diag::Event::blockSlow, stage.code, static_cast<int>(stage.milliseconds * 100.0f), took, budget);

    // once per second of audio: a summary line
    tripwireWorstMs = std::max(tripwireWorstMs, took);
    tripwirePeak = std::max(tripwirePeak, peak);
    tripwireVoices = std::max(tripwireVoices, synthEngine.activeVoiceCount());
    tripwireClipped += synthEngine.consumeClippedSamples();
    tripwireSamples += n;
    if (tripwireSamples >= static_cast<int>(rate))
    {
        // The peak travels as a number; the writer thread formats it (no snprintf on the audio thread, D170).
        diag::record(diag::Event::secondSummary, tripwireVoices, tripwireClipped, tripwireWorstMs, budget, nullptr,
                     juce::Decibels::gainToDecibels(tripwirePeak, -120.0f));
        tripwireWorstMs = 0.0f;
        tripwirePeak = 0.0f;
        tripwireVoices = 0;
        tripwireClipped = 0;
        tripwireSamples = 0;
    }
}

int SynthAudioProcessor::getNumPrograms()
{
    return coolsynth::presets::getFactoryPresetCount();
}

const juce::String SynthAudioProcessor::getProgramName(int index)
{
    const auto& preset = coolsynth::presets::getFactoryPreset(index);
    return preset.category + ": " + (preset.section.isNotEmpty() ? preset.section + ": " : juce::String()) + preset.name;
}

bool SynthAudioProcessor::loadSampledInstrumentsNow()
{
    bool allThere = true;
    forEachSampledStyle([&](const coolsynth::synth::v3::LoadedStyle* style) { allThere = allThere && style != nullptr; });
    return allThere;
}

bool SynthAudioProcessor::sampledInstrumentRange(int& lowestNote, int& highestNote)
{
    int lowest = 128, highest = -1;
    forEachSampledStyle([&](const coolsynth::synth::v3::LoadedStyle* style)
    {
        if (style != nullptr && ! style->zones.empty())
        {
            lowest = std::min(lowest, style->zones.front().root);
            highest = std::max(highest, style->zones.back().root);
        }
    });
    if (highest < 0)
        return false;
    lowestNote = lowest;
    highestNote = highest;
    return true;
}

template <typename Visit>
void SynthAudioProcessor::forEachSampledStyle(Visit&& visit)
{
    namespace sv3 = coolsynth::synth::v3;
    // Its own copy: the audio thread's settings are the audio thread's alone.
    const auto own = std::make_unique<sv3::EngineSettings>();
    fillEngineSettings(*own);
    const auto& settings = *own;
    const auto& d = sv3::SampleGenerator::descriptor();
    for (const auto& part : settings.parts)
        for (const auto& generator : part.voice.generators)
        {
            if (generator.type != sv3::generatorType::sample)
                continue;
            const int which = static_cast<int>(std::lround(sv3::descriptorToUnits(d.params[sv3::SampleGenerator::sample], generator.p[sv3::SampleGenerator::sample])));
            if (which < sv3::SampleGenerator::firstInstrument)
                continue;
            const int style = static_cast<int>(std::lround(sv3::descriptorToUnits(d.params[sv3::SampleGenerator::style], generator.p[sv3::SampleGenerator::style])));
            visit(sv3::SampleLibrary::instance().loadNow(which - sv3::SampleGenerator::firstInstrument, style));
        }
}

void SynthAudioProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;
    currentProgram = index;
    const auto& preset = coolsynth::presets::getFactoryPreset(index);
    coolsynth::diag::record(coolsynth::diag::Event::presetLoaded, index, 0, 0.0f, 0.0f, preset.name.toRawUTF8());
    const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true); // one bulk edit, not a gesture per parameter
    const BulkEdit whole(*this); // the audio thread sees the old preset or the new one, never a mix (D157)
    soundGeneration.fetch_add(1, std::memory_order_relaxed); // notes ringing now keep the old sound (D158)
    const juce::ScopedValueSetter<bool> loading(loadingSound, true);
    coolsynth::presets::applyFactoryPreset(parameters, preset);
    setPatchIdentity(preset.name, false);
}

void SynthAudioProcessor::startFlightRecorderIfWanted()
{
    // The standalone app and the plugin record; tools and tests only when asked (COOLSYNTH_FLIGHT_LOG).
    if (wrapperType != wrapperType_Undefined || juce::SystemStats::getEnvironmentVariable("COOLSYNTH_FLIGHT_LOG", {}).isNotEmpty())
    {
        auto& recorder = coolsynth::diag::FlightRecorder::instance();
        const bool first = ! recorder.running();
        recorder.start();
        if (first)
            coolsynth::diag::record(coolsynth::diag::Event::sessionStart, 0, 0, 0.0f, 0.0f,
                                    coolsynth::build::getBuildIdentity().toRawUTF8());
    }
}

coolsynth::ui::InterfaceModel& SynthAudioProcessor::getInterfaceModel()
{
    if (interfaceModel == nullptr)
        interfaceModel = std::make_unique<coolsynth::ui::InterfaceModel>(*this);
    return *interfaceModel;
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    // The V3 panel (docs/interface-v3.md). The throwaway AuditionEditor stays in the sources for now.
    return new PanelEditor(*this);
}

void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = createProcessorStateXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (! xml->hasTagName(processorStateRootTag))
            return;

        const int version = xml->getIntAttribute(processorStateVersionProperty, 0);
        const bool fixedEffects = version == coolsynth::presets::processorStateVersionWithFixedEffects;
        if (version != processorStateFormatVersion && ! fixedEffects)
            return;

        const auto expectedStateType = getParameterStateTypeName();
        if (xml->getStringAttribute(processorStateStateTypeProperty) != expectedStateType)
            return;

        auto* found = findSingleMatchingChild(*xml, expectedStateType);
        if (found == nullptr)
            return;

        // A state saved before the master effect slots: its fixed effects move into the slots (D135).
        juce::XmlElement parameterStateXml(*found);
        std::map<juce::String, juce::String> renamed;
        if (fixedEffects)
            renamed = coolsynth::presets::migrateFixedMasterEffects(parameterStateXml);

        const AtomicFlagScope applying(applyingPatch, true);
        const BulkEdit whole(*this);
        // A whole new sound (D158). A state that changes nothing (rejected, or the same) keeps no earlier sound: the
        // engine compares the settings.
        soundGeneration.fetch_add(1, std::memory_order_relaxed);
        const juce::ScopedValueSetter<bool> loading(loadingSound, true);
        suppressGestureSnapshots = true;
        const bool applied = applyParameterStateXml(parameterStateXml);
        suppressGestureSnapshots = false;

        if (applied)
        {
            setLearnedMidiBindings(parseLearnedMidiBindingsXml(*xml, renamed));
            setPatchIdentity(xml->getStringAttribute(processorStatePatchNameProperty, "Init"),
                             xml->getBoolAttribute(processorStatePatchDirtyProperty, false));
            undoHistory.clear();
        }
    }
}

std::unique_ptr<juce::XmlElement> SynthAudioProcessor::createParameterStateXml()
{
    auto state = parameters.copyState();
    return state.createXml();
}

std::unique_ptr<juce::XmlElement> SynthAudioProcessor::createProcessorStateXml() const
{
    auto root = std::make_unique<juce::XmlElement>(processorStateRootTag);
    root->setAttribute(processorStateVersionProperty, processorStateFormatVersion);
    root->setAttribute(processorStateProductProperty, "CoolSynth");
    root->setAttribute(processorStateStateTypeProperty, parameters.state.getType().toString());
    root->setAttribute(processorStatePatchNameProperty, getPatchName());
    root->setAttribute(processorStatePatchDirtyProperty, isPatchDirty());

    auto parameterState = const_cast<SynthAudioProcessor*> (this)->parameters.copyState();
    root->addChildElement(parameterState.createXml().release());

    const juce::ScopedLock lock(midiMappingStateLock);
    root->addChildElement(coolsynth::midi::createLearnedBindingsXml(learnedMidiMappingsTag,
                                                                    learnedMidiBindings).release());
    return root;
}

bool SynthAudioProcessor::applyParameterStateXml(const juce::XmlElement& stateXml)
{
    if (!stateXml.hasTagName(parameters.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml(stateXml);
    if (!tree.isValid())
        return false;

    juce::ValueTree sanitizedTree;
    if (!buildSanitizedParameterStateTree(tree, sanitizedTree))
        return false;
    const BulkEdit whole(*this); // the state arrives whole (D157)

    parameters.replaceState(sanitizedTree);
    return true;
}

bool SynthAudioProcessor::buildSanitizedParameterStateTree(const juce::ValueTree& incomingState,
                                                           juce::ValueTree& sanitizedState)
{
    if (!incomingState.isValid() || !incomingState.hasType(parameters.state.getType()))
        return false;

    sanitizedState = parameters.copyState();
    int appliedParameterCount = 0;
    std::vector<bool> seenKnownParameters(static_cast<size_t>(knownParameterIds.size()), false);

    // Several hundred parameters: look IDs up instead of scanning the lists for each one.
    std::map<juce::String, int> knownIndexById;
    for (int i = 0; i < knownParameterIds.size(); ++i)
        knownIndexById.emplace(knownParameterIds[i], i);
    std::map<juce::String, juce::ValueTree> sanitizedChildById;
    for (auto sanitizedChild : sanitizedState)
        sanitizedChildById.emplace(sanitizedChild.getProperty(apvtsParameterIdProperty).toString(), sanitizedChild);
    std::set<juce::String> seenParameterIds;

    for (auto incomingChild : incomingState)
    {
        if (!incomingChild.hasType(apvtsParameterChildType)
            || !incomingChild.hasProperty(apvtsParameterIdProperty)
            || !incomingChild.hasProperty(apvtsParameterValueProperty))
        {
            continue;
        }

        const auto parameterId = incomingChild.getProperty(apvtsParameterIdProperty).toString();
        if (parameterId.isEmpty())
            continue;

        if (! seenParameterIds.insert(parameterId).second)
            return false;

        const auto known = knownIndexById.find(parameterId);
        if (known == knownIndexById.end())
            continue;

        seenKnownParameters[static_cast<size_t>(known->second)] = true;

        if (auto sanitizedChild = sanitizedChildById.find(parameterId); sanitizedChild != sanitizedChildById.end())
        {
            sanitizedChild->second.setProperty(apvtsParameterValueProperty,
                                               incomingChild.getProperty(apvtsParameterValueProperty),
                                               nullptr);
            ++appliedParameterCount;
        }
    }

    if (appliedParameterCount != knownParameterIds.size())
        return false;

    for (const auto seen : seenKnownParameters)
    {
        if (! seen)
            return false;
    }

    // The user's own files the state names come with it (a state without them names none).
    namespace pv3 = coolsynth::parameters::v3;
    sanitizedState.removeChild(sanitizedState.getChildWithName(pv3::userFilesTag), nullptr);
    if (const auto files = incomingState.getChildWithName(pv3::userFilesTag); files.isValid())
        sanitizedState.appendChild(files.createCopy(), nullptr);
    return true;
}

juce::String SynthAudioProcessor::getParameterStateTypeName() const
{
    return parameters.state.getType().toString();
}

void SynthAudioProcessor::applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                                        float normalizedValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(normalizedValue);
    parameter.endChangeGesture();
}

void SynthAudioProcessor::resetAutomatableParametersToDefaults()
{
    for (auto* parameterBase : getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(parameterBase))
            applyNormalizedParameterValue(*parameter, parameter->getDefaultValue());
    // The init patch plays no file of the user's.
    parameters.state.removeChild(parameters.state.getChildWithName(coolsynth::parameters::v3::userFilesTag), nullptr);
}

// ---- Undo and patch identity ---------------------------------------------------

void SynthAudioProcessor::parameterValueChanged(int, float)
{
    if (! applyingPatch.load(std::memory_order_relaxed))
        patchDirty.store(true, std::memory_order_relaxed);
}

void SynthAudioProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Gestures come from this plugin's own UI, so they arrive on the message thread;
    // anything else (and the bulk edits below) must not open snapshots.
    if (suppressGestureSnapshots || ! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (gestureIsStarting)
    {
        if (activeGestureCount++ == 0)
        {
            juce::String label;
            if (auto* parameter = getParameters()[parameterIndex])
                label = parameter->getName(32);
            undoHistory.beginEdit(captureSnapshot(label));
        }
        return;
    }

    if (activeGestureCount > 0 && --activeGestureCount == 0)
        undoHistory.commitEdit(captureNormalisedValues());
}

std::vector<float> SynthAudioProcessor::captureNormalisedValues() const
{
    std::vector<float> values;
    const auto& allParameters = getParameters();
    values.reserve(static_cast<size_t>(allParameters.size()));
    for (auto* parameter : allParameters)
        values.push_back(parameter->getValue());
    return values;
}

coolsynth::plugin::ParameterUndoHistory::Snapshot SynthAudioProcessor::captureSnapshot(juce::String label) const
{
    return { std::move(label), captureNormalisedValues(), getPatchName(), isPatchDirty() };
}

void SynthAudioProcessor::restoreSnapshot(const coolsynth::plugin::ParameterUndoHistory::Snapshot& snapshot)
{
    const AtomicFlagScope applying(applyingPatch, true);
    const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true);
    const BulkEdit whole(*this);

    const auto& allParameters = getParameters();
    for (int i = 0; i < allParameters.size() && i < static_cast<int>(snapshot.normalisedValues.size()); ++i)
    {
        auto* parameter = allParameters[i];
        const float value = snapshot.normalisedValues[static_cast<size_t>(i)];
        if (! juce::approximatelyEqual(parameter->getValue(), value))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    }

    setPatchIdentity(snapshot.patchName, snapshot.patchDirty);
}

void SynthAudioProcessor::performUndoableEdit(const juce::String& label, const std::function<void()>& edit)
{
    jassert(juce::MessageManager::existsAndIsCurrentThread());

    // A gesture that is still open would otherwise swallow this edit into its step.
    if (undoHistory.isEditPending())
        undoHistory.commitEdit(captureNormalisedValues());
    activeGestureCount = 0;

    undoHistory.beginEdit(captureSnapshot(label));
    {
        const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true);
        const BulkEdit whole(*this);
        edit();
    }
    undoHistory.commitEdit(captureNormalisedValues());
}

bool SynthAudioProcessor::undo()
{
    if (! undoHistory.canUndo())
        return false;

    if (undoHistory.isEditPending())
        undoHistory.commitEdit(captureNormalisedValues());
    activeGestureCount = 0;

    restoreSnapshot(undoHistory.popUndo(captureSnapshot({})));
    return true;
}

bool SynthAudioProcessor::redo()
{
    if (! undoHistory.canRedo())
        return false;

    restoreSnapshot(undoHistory.popRedo(captureSnapshot({})));
    return true;
}

void SynthAudioProcessor::setPatchIdentity(juce::String name, bool dirty)
{
    {
        const juce::ScopedLock lock(patchIdentityLock);
        patchName = std::move(name);
    }
    patchDirty.store(dirty, std::memory_order_relaxed);
}

juce::String SynthAudioProcessor::getPatchName() const
{
    const juce::ScopedLock lock(patchIdentityLock);
    return patchName;
}

bool SynthAudioProcessor::setActiveControllerProfile(juce::StringRef profileId)
{
    const juce::ScopedLock lock(midiMappingStateLock);
    return midiMappingEngine.setActiveProfile(profileId);
}

juce::String SynthAudioProcessor::getActiveControllerProfileId() const
{
    const juce::ScopedLock lock(midiMappingStateLock);
    return midiMappingEngine.getActiveProfileId();
}

juce::String SynthAudioProcessor::getActiveControllerProfileDisplayName() const
{
    const juce::ScopedLock lock(midiMappingStateLock);
    return midiMappingEngine.getActiveProfileDisplayName();
}

void SynthAudioProcessor::setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings)
{
    const juce::ScopedLock lock(midiMappingStateLock);
    learnedMidiBindings.assign(bindings.begin(), bindings.end());
    midiMappingEngine.setLearnedBindings(bindings);
}

std::vector<coolsynth::midi::LearnedCcBinding> SynthAudioProcessor::getLearnedMidiBindings() const
{
    const juce::ScopedLock lock(midiMappingStateLock);
    return learnedMidiBindings;
}

void SynthAudioProcessor::clearLearnedMidiBinding(juce::StringRef parameterId)
{
    const juce::ScopedLock lock(midiMappingStateLock);
    learnedMidiBindings.erase(std::remove_if(learnedMidiBindings.begin(),
                                             learnedMidiBindings.end(),
                                             [&](const auto& binding)
                                             {
                                                 return binding.parameterId == parameterId;
                                             }),
                              learnedMidiBindings.end());
    midiMappingEngine.clearLearnedBinding(parameterId);
}

int SynthAudioProcessor::drainPendingPluginControllerEvents(coolsynth::midi::ControllerMidiEvent* destination,
                                                            int maxEvents) noexcept
{
    int start1, size1, start2, size2;
    pendingPluginControllerEventQueue.prepareToRead(maxEvents, start1, size1, start2, size2);

    int totalRead = 0;
    for (int i = 0; i < size1; ++i)
        destination[totalRead++] = pendingPluginControllerEvents[static_cast<size_t> (start1 + i)];

    for (int i = 0; i < size2; ++i)
        destination[totalRead++] = pendingPluginControllerEvents[static_cast<size_t> (start2 + i)];

    pendingPluginControllerEventQueue.finishedRead(totalRead);
    return totalRead;
}

void SynthAudioProcessor::handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event)
{
    if (isReservedSynthController(event))
        return;

    coolsynth::midi::MappedAction action;
    {
        const juce::ScopedLock lock(midiMappingStateLock);
        action = midiMappingEngine.translate(event);
    }

    applyMappedAction(action);
}

void SynthAudioProcessor::enqueuePluginControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept
{
    int start1, size1, start2, size2;
    pendingPluginControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        pendingPluginControllerEvents[static_cast<size_t> (start1)] = event;
        pendingPluginControllerEventQueue.finishedWrite(1);
        return;
    }

    if (size2 > 0)
    {
        pendingPluginControllerEvents[static_cast<size_t> (start2)] = event;
        pendingPluginControllerEventQueue.finishedWrite(1);
        return;
    }

    droppedControllerEventCount.fetch_add(1, std::memory_order_relaxed);
}

void SynthAudioProcessor::enqueuePluginMappedControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept
{
    int start1, size1, start2, size2;
    pendingPluginMappedControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        pendingPluginMappedControllerEvents[static_cast<size_t> (start1)] = event;
        pendingPluginMappedControllerEventQueue.finishedWrite(1);
        mappedActionBridge.triggerAsyncUpdate();
        return;
    }

    if (size2 > 0)
    {
        pendingPluginMappedControllerEvents[static_cast<size_t> (start2)] = event;
        pendingPluginMappedControllerEventQueue.finishedWrite(1);
        mappedActionBridge.triggerAsyncUpdate();
        return;
    }

    droppedMappedControllerEventCount.fetch_add(1, std::memory_order_relaxed);
}

int SynthAudioProcessor::drainPendingMappedControllerEvents(coolsynth::midi::ControllerMidiEvent* destination,
                                                            int maxEvents) noexcept
{
    int start1, size1, start2, size2;
    pendingPluginMappedControllerEventQueue.prepareToRead(maxEvents, start1, size1, start2, size2);

    int totalRead = 0;
    for (int i = 0; i < size1; ++i)
        destination[totalRead++] = pendingPluginMappedControllerEvents[static_cast<size_t> (start1 + i)];

    for (int i = 0; i < size2; ++i)
        destination[totalRead++] = pendingPluginMappedControllerEvents[static_cast<size_t> (start2 + i)];

    pendingPluginMappedControllerEventQueue.finishedRead(totalRead);
    return totalRead;
}

void SynthAudioProcessor::dispatchPendingMappedControllerEvents()
{
    std::array<coolsynth::midi::ControllerMidiEvent, 32> localEvents {};

    while (true)
    {
        const auto drained = drainPendingMappedControllerEvents(localEvents.data(),
                                                                static_cast<int> (localEvents.size()));
        if (drained == 0)
            break;

        for (int i = 0; i < drained; ++i)
        {
            coolsynth::midi::MappedAction action;
            {
                const juce::ScopedLock lock(midiMappingStateLock);
                action = midiMappingEngine.translate(localEvents[static_cast<size_t> (i)]);
            }

            applyMappedAction(action);
        }
    }
}

void SynthAudioProcessor::applyPendingLearnedChord()
{
    namespace pv3 = coolsynth::parameters::v3;
    namespace sv3 = coolsynth::synth::v3;
    auto set = [this](const juce::String& id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
            parameter->endChangeGesture();
        }
    };

    for (std::size_t index = 0; index < pendingLearnedChord.size(); ++index)
    {
        const auto packed = pendingLearnedChord[index].exchange(0, std::memory_order_acq_rel);
        if (packed == 0)
            continue;

        const auto part = index == 0 ? pv3::Part::a : pv3::Part::b;
        const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true);
        const auto intervals = sv3::unpackLearnedChord(packed);
        for (std::size_t i = 0; i < intervals.size(); ++i)
            set(pv3::chordParameterId(part, "n" + juce::String(static_cast<int>(i) + 1)), static_cast<float>(intervals[i]));
        set(pv3::chordParameterId(part, "type"), static_cast<float>(sv3::chordType::learned));
        set(pv3::chordParameterId(part, "learn"), 0.0f);
    }
}

void SynthAudioProcessor::applyMappedAction(const coolsynth::midi::MappedAction& action)
{
    using Kind = coolsynth::midi::MappedAction::Kind;

    switch (action.kind)
    {
        case Kind::parameterChange: applyParameterChange(action.parameterChange); break;
        case Kind::command:         applyMappedCommand(action.command); break;
        case Kind::none:
        default: break;
    }
}

void SynthAudioProcessor::applyParameterChange(const coolsynth::midi::MappedParameterChange& change)
{
    if (change.parameter == nullptr)
        return;

    const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true);
    change.parameter->beginChangeGesture();
    change.parameter->setValueNotifyingHost(change.normalizedValue);
    change.parameter->endChangeGesture();
}

void SynthAudioProcessor::applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept
{
    using Command = coolsynth::midi::MappedCommand;

    switch (command)
    {
        case Command::panic: requestPanic(); break;
        case Command::none:
        default: break;
    }
}

void SynthAudioProcessor::requestPanic() noexcept
{
    panicRequested.store(true, std::memory_order_release);
}

double SynthAudioProcessor::getTailLengthSeconds() const
{
    // The longest tail among the master effect slots (a synced delay at the internal tempo).
    std::array<coolsynth::synth::v3::InsertSettings, coolsynth::parameters::v3::masterEffectSlots> slots {};
    coolsynth::parameters::v3::readMasterEffects(masterEffectValues, slots);
    const double bpm = parameterValues.arpInternalTempoBpm != nullptr ? parameterValues.arpInternalTempoBpm->load() : 120.0;
    double tail = 0.0;
    for (const auto& slot : slots)
        tail = std::max(tail, coolsynth::synth::v3::insertTailSeconds(slot, bpm));
    return tail;
}

std::vector<coolsynth::midi::LearnedCcBinding> SynthAudioProcessor::parseLearnedMidiBindingsXml(
    const juce::XmlElement& parent, const std::map<juce::String, juce::String>& renamed) const
{
    std::vector<coolsynth::midi::LearnedCcBinding> bindings;

    if (const auto* mappings = parent.getChildByName(learnedMidiMappingsTag))
        bindings = coolsynth::midi::parseLearnedBindingsXml(*mappings);

    // A knob learned on a parameter that moved (an old master effect's value into its slot, D135) follows it;
    // the normaliser then drops what has no place any more.
    for (auto& binding : bindings)
        if (const auto moved = renamed.find(binding.parameterId); moved != renamed.end())
            binding.parameterId = moved->second;

    coolsynth::midi::MidiLearnManager normalizer;
    normalizer.replaceBindings(std::move(bindings));
    const auto normalized = normalizer.getBindings();
    return { normalized.begin(), normalized.end() };
}

namespace
{
    bool decodeBool(float rawValue) noexcept
    {
        return rawValue >= 0.5f;
    }

}

const coolsynth::synth::v3::EngineSettings& SynthAudioProcessor::makeEngineSettings() noexcept
{
    // A seqlock over the bulk edits: read only when none is under way, and keep what was read only if none began
    // meanwhile. Otherwise the last whole settings play on, one more block (D157).
    const auto before = settingsSequence.load(std::memory_order_acquire);
    if ((before & 1u) != 0u && settingsPrimed)
        return settingsBuffers[static_cast<std::size_t>(liveSettings)];
    auto& next = settingsBuffers[static_cast<std::size_t>(1 - liveSettings)];
    fillEngineSettings(next);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (settingsSequence.load(std::memory_order_relaxed) != before && settingsPrimed)
        return settingsBuffers[static_cast<std::size_t>(liveSettings)];
    liveSettings = 1 - liveSettings;
    settingsPrimed = true;
    return next;
}

void SynthAudioProcessor::fillEngineSettings(coolsynth::synth::v3::EngineSettings& settings) const noexcept
{
    for (std::size_t part = 0; part < partParameterValues.size(); ++part)
    {
        coolsynth::parameters::v3::readPartSettings(partParameterValues[part], settings.parts[part]);
        coolsynth::parameters::v3::readPartMix(partParameterValues[part], settings.mix[part]);
        coolsynth::parameters::v3::readPartInserts(partParameterValues[part], settings.inserts[part]);
    }
    settings.partMode = static_cast<coolsynth::synth::v3::PartMode>(
        juce::jlimit(0, 3, static_cast<int>(std::lround(partModeValue != nullptr ? partModeValue->load() : 0.0f))));

    const auto& v = parameterValues;
    auto index = [](const std::atomic<float>* p) { return static_cast<int>(std::lround(p->load())); };

    auto& arp = settings.parts[0].arp;
    arp.enabled = decodeBool(v.arpEnabled->load());
    arp.internalTempoBpm = v.arpInternalTempoBpm->load();
    arp.rate = static_cast<coolsynth::parameters::ArpRateChoice>(index(v.arpRateDivision));
    arp.pattern = static_cast<coolsynth::parameters::ArpPatternChoice>(index(v.arpPattern));
    arp.octaveRange = index(v.arpOctaveRange);
    arp.gateLength = v.arpGate->load();
    arp.swingAmount = v.arpSwing->load();
    arp.chance = v.arpChance->load();
    arp.ratchetCount = static_cast<coolsynth::parameters::ArpRatchetChoice>(index(v.arpRatchetCount));
    arp.ratchetChance = v.arpRatchetChance->load();
    arp.accentEvery = static_cast<coolsynth::parameters::ArpAccentEveryChoice>(index(v.arpAccentEvery));
    arp.accentAmount = v.arpAccentAmount->load();
    arp.rhythm = static_cast<coolsynth::parameters::ArpRhythmChoice>(index(v.arpRhythm));
    arp.euclideanPulses = index(v.arpEuclideanPulses);
    arp.euclideanSteps = index(v.arpEuclideanSteps);
    arp.euclideanRotation = index(v.arpEuclideanRotation);
    arp.latch = decodeBool(v.arpLatch->load());
    settings.parts[1].arp = arp; // one arpeggiator setting; each part's mix says whether it follows

    coolsynth::parameters::v3::readMasterEffects(masterEffectValues, settings.master);

    // The user's own sounds the generator slots play, by their UserLibrary handles.
    for (int part = 0; part < coolsynth::synth::v3::partCount; ++part)
        for (int slot = 0; slot < coolsynth::parameters::v3::partGeneratorSlots; ++slot)
            settings.parts[static_cast<std::size_t>(part)].voice.generators[static_cast<std::size_t>(slot)].userSound =
                userSoundHandles[static_cast<std::size_t>(part * coolsynth::parameters::v3::partGeneratorSlots + slot)].load(std::memory_order_relaxed);

    settings.masterGainLinear = juce::Decibels::decibelsToGain(v.masterGainDb->load());
    // Knobs 7 and 8 (D176): Brightness moves every filter's cutoff by up to 1.5 octaves either way (0.15 of the
    // cutoff's 20 Hz - 20 kHz range, ten octaves); Space makes every reverb, delay and room up to twice or half as wet.
    for (auto& part : settings.parts)
        part.voice.brightness = brightnessRangeNormalised * juce::jlimit(-1.0f, 1.0f, v.brightness->load());
    settings.space = juce::jlimit(-1.0f, 1.0f, v.space->load());
    const auto generation = soundGeneration.load(std::memory_order_relaxed);
    for (auto& part : settings.parts)
        part.soundGeneration = generation;
}

coolsynth::synth::GlobalParameterPointers SynthAudioProcessor::bindParameterPointers(APVTS& state)
{
    coolsynth::synth::GlobalParameterPointers pointers;
    for (auto& [id, memberPtr] : coolsynth::synth::kGlobalParamPtrBindings)
        pointers.*memberPtr = state.getRawParameterValue(id);
    return pointers;
}

// Static creation function required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}
