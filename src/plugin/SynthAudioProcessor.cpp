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
#include "plugin/AuditionEditor.h"
#include "presets/FactoryPresets.h"

namespace
{
    inline constexpr char apvtsParameterChildType[] = "PARAM";
    inline constexpr char apvtsParameterIdProperty[] = "id";
    inline constexpr char apvtsParameterValueProperty[] = "value";
    inline constexpr char processorStateRootTag[] = "COOLSYNTH_PROCESSOR_STATE";
    inline constexpr char processorStateVersionProperty[] = "formatVersion";
    inline constexpr int processorStateFormatVersion = 9; // 9: V3 parameter contract
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
    , mappedActionBridge(*this)
{
    for (auto* parameter : getParameters())
    {
        parameter->addListener(this);
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            knownParameterIds.add(withId->paramID);
    }
}

SynthAudioProcessor::~SynthAudioProcessor()
{
    for (auto* parameter : getParameters())
        parameter->removeListener(this);
}

void SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthEngine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples(synthEngine.getLatencySamples());
    scopeFifo.clear();
    monoMixScratch.resize(static_cast<size_t>(samplesPerBlock));
    // Prime the AsyncUpdater's lazy first-call allocation before the audio thread touches it.
    mappedActionBridge.triggerAsyncUpdate();
    mappedActionBridge.cancelPendingUpdate();
}

void SynthAudioProcessor::releaseResources()
{
    synthEngine.releaseResources();
}

void SynthAudioProcessor::reset()
{
    panicRequested.store(false, std::memory_order_release);
    synthEngine.panic();
    keyboardState.reset();
    scopeFifo.clear();
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
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (panicRequested.exchange(false, std::memory_order_acq_rel))
    {
        synthEngine.panic();
        keyboardState.allNotesOff(0);
        midiMessages.clear();
    }

    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    std::array<coolsynth::synth::EngineMidiEvent, coolsynth::synth::maxEngineEventsPerBlock> engineEvents {};
    int engineEventCount = 0;

    for (const auto metadata : midiMessages)
    {
        if (engineEventCount < static_cast<int> (engineEvents.size()))
        {
            if (const auto event = makeEngineMidiEvent(metadata.getMessage(), metadata.samplePosition))
                engineEvents[static_cast<size_t> (engineEventCount++)] = *event;
        }

        if (juce::JUCEApplicationBase::isStandaloneApp())
            continue;

        if (const auto event = coolsynth::midi::toControllerMidiEvent(metadata.getMessage()))
        {
            enqueuePluginControllerEvent(*event);

            if (! isReservedSynthController(*event))
                enqueuePluginMappedControllerEvent(*event);
        }
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

    synthEngine.render(buffer,
                       std::span<const coolsynth::synth::EngineMidiEvent>(engineEvents.data(),
                                                                          static_cast<size_t> (engineEventCount)),
                       makeEngineSettings(),
                       transportInfo);

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

float SynthAudioProcessor::sampledVelocityTrack()
{
    float track = 0.0f;
    forEachSampledStyle([&](const coolsynth::synth::v3::LoadedStyle* style)
    {
        if (style != nullptr)
            track = std::max(track, style->velocityTrack);
    });
    return track;
}

template <typename Visit>
void SynthAudioProcessor::forEachSampledStyle(Visit&& visit)
{
    namespace sv3 = coolsynth::synth::v3;
    const auto& settings = makeEngineSettings();
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
    const juce::ScopedValueSetter<bool> suppress(suppressGestureSnapshots, true); // one bulk edit, not a gesture per parameter
    coolsynth::presets::applyFactoryPreset(parameters, preset);
    setPatchIdentity(preset.name, false);
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    // Throwaway until phase 9: a preset menu over every parameter as a slider, named by its slot's type.
    return new AuditionEditor(*this);
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

        if (xml->getIntAttribute(processorStateVersionProperty, 0) != processorStateFormatVersion)
            return;

        const auto expectedStateType = getParameterStateTypeName();
        if (xml->getStringAttribute(processorStateStateTypeProperty) != expectedStateType)
            return;

        auto* parameterStateXml = findSingleMatchingChild(*xml, expectedStateType);
        if (parameterStateXml == nullptr)
            return;

        const AtomicFlagScope applying(applyingPatch, true);
        suppressGestureSnapshots = true;
        const bool applied = applyParameterStateXml(*parameterStateXml);
        suppressGestureSnapshots = false;

        if (applied)
        {
            setLearnedMidiBindings(parseLearnedMidiBindingsXml(*xml));
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
    if (parameterValues.delayEnabled == nullptr
        || parameterValues.delayTimeMs == nullptr
        || parameterValues.delayMix == nullptr
        || parameterValues.delayFeedback == nullptr
        || parameterValues.reverbEnabled == nullptr
        || parameterValues.reverbMix == nullptr)
    {
        return 0.0;
    }

    coolsynth::synth::DelayParametersV2 delayParameters;
    delayParameters.enabled = parameterValues.delayEnabled->load() >= 0.5f;
    delayParameters.timeMs = juce::jlimit(1.0f, 1000.0f, parameterValues.delayTimeMs->load());
    delayParameters.mix = juce::jlimit(0.0f, 1.0f, parameterValues.delayMix->load());
    delayParameters.feedback = juce::jlimit(0.0f, coolsynth::synth::GlobalDelay::maxFeedback, parameterValues.delayFeedback->load());

    coolsynth::synth::ReverbParametersV2 reverbParameters;
    reverbParameters.enabled = parameterValues.reverbEnabled->load() >= 0.5f;
    reverbParameters.mix = juce::jlimit(0.0f, 1.0f, parameterValues.reverbMix->load());

    return coolsynth::synth::GlobalFxRack::estimateTailLengthSeconds(delayParameters, reverbParameters);
}

std::vector<coolsynth::midi::LearnedCcBinding> SynthAudioProcessor::parseLearnedMidiBindingsXml(const juce::XmlElement& parent) const
{
    std::vector<coolsynth::midi::LearnedCcBinding> bindings;

    if (const auto* mappings = parent.getChildByName(learnedMidiMappingsTag))
        bindings = coolsynth::midi::parseLearnedBindingsXml(*mappings);

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
    auto& settings = engineSettings;
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

    settings.drive.enabled = decodeBool(v.driveEnabled->load());
    settings.drive.amount = v.driveAmount->load();
    settings.drive.mix = v.driveMix->load();

    settings.chorus.enabled = decodeBool(v.chorusEnabled->load());
    settings.chorus.rateHz = v.chorusRateHz->load();
    settings.chorus.depth = v.chorusDepth->load();
    settings.chorus.mix = v.chorusMix->load();

    settings.delay.enabled = decodeBool(v.delayEnabled->load());
    settings.delay.timeMs = juce::jlimit(1.0f, 1000.0f, v.delayTimeMs->load());
    settings.delay.feedback = juce::jlimit(0.0f, coolsynth::synth::GlobalDelay::maxFeedback, v.delayFeedback->load());
    settings.delay.mix = v.delayMix->load();
    settings.delay.sync = decodeBool(v.delaySync->load());
    settings.delay.division = juce::jlimit(0, static_cast<int>(coolsynth::synth::delayDivisions.size()) - 1, index(v.delayDivision));
    settings.delay.pingPong = index(v.delayMode) == 1;

    settings.lofi.enabled = decodeBool(v.lofiEnabled->load());
    settings.lofi.wow = v.lofiWow->load();
    settings.lofi.flutter = v.lofiFlutter->load();
    settings.lofi.hiss = v.lofiHiss->load();
    settings.lofi.crackle = v.lofiCrackle->load();
    settings.lofi.bandwidth = v.lofiBandwidth->load();
    settings.lofi.bits = v.lofiBits->load();
    settings.lofi.rateHz = v.lofiRate->load();
    settings.lofi.mix = v.lofiMix->load();

    settings.flanger.enabled = decodeBool(v.flangerEnabled->load());
    settings.flanger.rateHz = v.flangerRate->load();
    settings.flanger.depth = v.flangerDepth->load();
    settings.flanger.feedback = v.flangerFeedback->load();
    settings.flanger.mix = v.flangerMix->load();
    settings.eq.enabled = decodeBool(v.eqEnabled->load());
    settings.eq.lowDb = v.eqLow->load();
    settings.eq.tiltDb = v.eqTilt->load();
    settings.eq.highDb = v.eqHigh->load();
    settings.pump.enabled = decodeBool(v.pumpEnabled->load());
    settings.pump.depth = v.pumpDepth->load();
    settings.pump.division = index(v.pumpDivision);
    settings.pump.release = v.pumpRelease->load();

    settings.reverb.enabled = decodeBool(v.reverbEnabled->load());
    settings.reverb.size = v.reverbSize->load();
    settings.reverb.damping = v.reverbDamping->load();
    settings.reverb.mix = v.reverbMix->load();
    settings.reverb.preDelayMs = v.reverbPreDelay->load();
    settings.reverb.lowCutHz = v.reverbLowCut->load();
    settings.reverb.modulation = v.reverbModulation->load();
    settings.reverb.shimmer = v.reverbShimmer->load();

    settings.phaser.enabled = decodeBool(v.phaserEnabled->load());
    settings.phaser.rateHz = juce::jlimit(0.05f, 8.0f, v.phaserRateHz->load());
    settings.phaser.depth = juce::jlimit(0.0f, 1.0f, v.phaserDepth->load());

    settings.compressor.enabled = decodeBool(v.compressorEnabled->load());
    settings.compressor.amount = juce::jlimit(0.0f, 1.0f, v.compressorAmount->load());
    settings.compressor.mix = juce::jlimit(0.0f, 1.0f, v.compressorMix->load());

    settings.masterGainLinear = juce::Decibels::decibelsToGain(v.masterGainDb->load());
    return settings;
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
