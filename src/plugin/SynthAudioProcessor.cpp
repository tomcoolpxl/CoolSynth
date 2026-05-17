#include "SynthAudioProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>

#include "SynthAudioProcessorEditor.h"
#include "midi/MidiToControllerEvent.h"
#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

namespace
{
    inline constexpr char apvtsParameterChildType[] = "PARAM";
    inline constexpr char apvtsParameterIdProperty[] = "id";
    inline constexpr char apvtsParameterValueProperty[] = "value";
    inline constexpr char processorStateRootTag[] = "COOLSYNTH_PROCESSOR_STATE";
    inline constexpr char processorStateVersionProperty[] = "formatVersion";
    inline constexpr int processorStateFormatVersion = 3;
    inline constexpr char processorStateProductProperty[] = "product";
    inline constexpr char processorStateStateTypeProperty[] = "stateType";
    inline constexpr char learnedMidiMappingsTag[] = "LEARNED_MIDI_MAPPINGS";
    inline constexpr char learnedMidiMappingTag[] = "MAPPING";
    inline constexpr char learnedMidiMappingParameterIdProperty[] = "parameterId";
    inline constexpr char learnedMidiMappingChannelProperty[] = "channel";
    inline constexpr char learnedMidiMappingControllerProperty[] = "controller";

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
    , choiceParams(bindChoicePointers(parameters))
    , mappedActionBridge(*this)
{
}

SynthAudioProcessor::~SynthAudioProcessor() = default;

void SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthEngine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
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
                       makeBlockRenderParameters(),
                       transportInfo);

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

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    return new SynthAudioProcessorEditor(*this);
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

        if (applyParameterStateXml(*parameterStateXml))
            setLearnedMidiBindings(parseLearnedMidiBindingsXml(*xml));
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

    auto parameterState = const_cast<SynthAudioProcessor*> (this)->parameters.copyState();
    root->addChildElement(parameterState.createXml().release());

    auto learnedMappings = std::make_unique<juce::XmlElement>(learnedMidiMappingsTag);
    learnedMappings->setAttribute("version", 1);

    const juce::ScopedLock lock(midiMappingStateLock);
    for (const auto& binding : learnedMidiBindings)
    {
        if (! binding.isValid())
            continue;

        auto* child = learnedMappings->createNewChildElement(learnedMidiMappingTag);
        child->setAttribute(learnedMidiMappingParameterIdProperty, binding.parameterId);
        child->setAttribute(learnedMidiMappingChannelProperty, static_cast<int> (binding.cc.channel));
        child->setAttribute(learnedMidiMappingControllerProperty, static_cast<int> (binding.cc.controllerNumber));
    }

    root->addChildElement(learnedMappings.release());
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
    juce::StringArray seenParameterIds;
    int appliedParameterCount = 0;
    std::array<bool, coolsynth::parameters::allParameterIds.size()> seenKnownParameters {};

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

        if (seenParameterIds.contains(parameterId))
            return false;

        seenParameterIds.add(parameterId);

        size_t matchedParameterIndex = coolsynth::parameters::allParameterIds.size();
        for (size_t index = 0; index < coolsynth::parameters::allParameterIds.size(); ++index)
        {
            if (parameterId == coolsynth::parameters::allParameterIds[index])
            {
                matchedParameterIndex = index;
                break;
            }
        }

        if (matchedParameterIndex == coolsynth::parameters::allParameterIds.size())
            continue;

        seenKnownParameters[matchedParameterIndex] = true;

        for (auto sanitizedChild : sanitizedState)
        {
            if (sanitizedChild.getProperty(apvtsParameterIdProperty).toString() != parameterId)
                continue;

            sanitizedChild.setProperty(apvtsParameterValueProperty,
                                       incomingChild.getProperty(apvtsParameterValueProperty),
                                       nullptr);
            ++appliedParameterCount;
            break;
        }
    }

    if (appliedParameterCount != static_cast<int> (coolsynth::parameters::allParameterIds.size()))
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
    {
        for (const auto* child : mappings->getChildIterator())
        {
            if (! child->hasTagName(learnedMidiMappingTag))
                continue;

            const auto parameterId = child->getStringAttribute(learnedMidiMappingParameterIdProperty);
            const auto channel = child->getIntAttribute(learnedMidiMappingChannelProperty, 0);
            const auto controller = child->getIntAttribute(learnedMidiMappingControllerProperty, -1);

            coolsynth::midi::LearnedCcBinding binding
            {
                parameterId,
                { static_cast<uint8_t> (channel), static_cast<uint8_t> (controller) }
            };

            if (binding.isValid() && controller >= 0 && controller <= 127)
                bindings.push_back(std::move(binding));
        }
    }

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

coolsynth::synth::BlockRenderParametersV2 SynthAudioProcessor::makeBlockRenderParameters() const noexcept
{
    coolsynth::synth::BlockRenderParametersV2 params;

    params.oscA.waveShape = static_cast<coolsynth::parameters::OscillatorWaveShape>(choiceParams.oscAWave->getIndex());
    params.oscA.octaveIndex = static_cast<int>(std::round(parameterValues.oscAOctave->load()));
    params.oscA.fineCents = parameterValues.oscAFineCents->load();
    params.oscA.level = parameterValues.oscALevel->load();
    params.oscA.pulseWidth = parameterValues.oscAPulseWidth->load();
    params.oscA.syncEnabled = decodeBool(parameterValues.oscASyncEnabled->load());

    params.oscB.waveShape = static_cast<coolsynth::parameters::OscillatorWaveShape>(choiceParams.oscBWave->getIndex());
    params.oscB.octaveIndex = static_cast<int>(std::round(parameterValues.oscBOctave->load()));
    params.oscB.fineCents = parameterValues.oscBFineCents->load();
    params.oscB.level = parameterValues.oscBLevel->load();
    params.oscB.pulseWidth = parameterValues.oscBPulseWidth->load();
    params.oscB.lowFrequencyMode = decodeBool(parameterValues.oscBLowFrequencyMode->load());

    params.mixer.noiseLevel = parameterValues.noiseLevel->load();

    params.filter.cutoffHz = parameterValues.filterCutoffHz->load();
    params.filter.resonanceNormalized = parameterValues.filterResonance->load();
    params.filter.envelopeAmount = parameterValues.filterEnvAmount->load();
    params.filter.keyTracking = static_cast<coolsynth::parameters::FilterKeyTrackingMode>(choiceParams.filterKeyTrack->getIndex());

    params.filterEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.filterAttackMs->load() * 0.001f);
    params.filterEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.filterDecayMs->load() * 0.001f);
    params.filterEnvelope.sustainLevel = parameterValues.filterSustain->load();
    params.filterEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.filterReleaseMs->load() * 0.001f);

    params.ampEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.ampAttackMs->load() * 0.001f);
    params.ampEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.ampDecayMs->load() * 0.001f);
    params.ampEnvelope.sustainLevel = parameterValues.ampSustain->load();
    params.ampEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.ampReleaseMs->load() * 0.001f);

    params.lfo.rateHz = parameterValues.lfoRateHz->load();
    params.lfo.waveShape = static_cast<coolsynth::parameters::LfoWaveShape>(choiceParams.lfoWave->getIndex());
    params.lfo.oscPitchDepth = parameterValues.lfoToOscPitch->load();
    params.lfo.pulseWidthDepth = parameterValues.lfoToPulseWidth->load();
    params.lfo.filterCutoffDepth = parameterValues.lfoToFilterCutoff->load();
    params.lfo.modWheelDepth = parameterValues.modWheelToLfoDepth->load();

    params.polyMod.oscBToOscPitch = parameterValues.polyModOscBToOscPitch->load();
    params.polyMod.envToOscPitch = parameterValues.polyModEnvToOscPitch->load();
    params.polyMod.oscBToPulseWidth = parameterValues.polyModOscBToPulseWidth->load();
    params.polyMod.envToPulseWidth = parameterValues.polyModEnvToPulseWidth->load();
    params.polyMod.oscBToFilterCutoff = parameterValues.polyModOscBToFilterCutoff->load();
    params.polyMod.envToFilterCutoff = parameterValues.polyModEnvToFilterCutoff->load();

    params.performance.glideTimeSeconds = parameterValues.glideTimeMs->load() * 0.001f;
    params.performance.playMode = static_cast<coolsynth::parameters::PlayModeChoice>(choiceParams.playMode->getIndex());
    params.performance.keyPriority = static_cast<coolsynth::parameters::KeyPriorityChoice>(choiceParams.keyPriority->getIndex());
    params.performance.pitchBendRangeSemitones = parameterValues.pitchBendRangeSemitones->load();
    params.performance.vintageAmount = parameterValues.vintageAmount->load();
    params.performance.panSpread = parameterValues.panSpread->load();
    params.performance.velocityToAmp = parameterValues.velocityToAmp->load();
    params.performance.velocityToFilter = parameterValues.velocityToFilter->load();

    params.arp.enabled = decodeBool(parameterValues.arpEnabled->load());
    params.arp.internalTempoBpm = parameterValues.arpInternalTempoBpm->load();
    params.arp.rate = static_cast<coolsynth::parameters::ArpRateChoice>(choiceParams.arpRate->getIndex());
    params.arp.pattern = static_cast<coolsynth::parameters::ArpPatternChoice>(choiceParams.arpPattern->getIndex());
    params.arp.octaveRange = static_cast<int>(std::round(parameterValues.arpOctaveRange->load()));
    params.arp.gateLength = parameterValues.arpGate->load();
    params.arp.latch = decodeBool(parameterValues.arpLatch->load());

    params.drive.enabled = decodeBool(parameterValues.driveEnabled->load());
    params.drive.amount = parameterValues.driveAmount->load();
    params.drive.mix = parameterValues.driveMix->load();

    params.chorus.enabled = decodeBool(parameterValues.chorusEnabled->load());
    params.chorus.rateHz = parameterValues.chorusRateHz->load();
    params.chorus.depth = parameterValues.chorusDepth->load();
    params.chorus.mix = parameterValues.chorusMix->load();

    params.delay.enabled = decodeBool(parameterValues.delayEnabled->load());
    params.delay.timeMs = juce::jlimit(1.0f, 1000.0f, parameterValues.delayTimeMs->load());
    params.delay.feedback = juce::jlimit(0.0f, coolsynth::synth::GlobalDelay::maxFeedback, parameterValues.delayFeedback->load());
    params.delay.mix = parameterValues.delayMix->load();

    params.reverb.enabled = decodeBool(parameterValues.reverbEnabled->load());
    params.reverb.size = parameterValues.reverbSize->load();
    params.reverb.damping = parameterValues.reverbDamping->load();
    params.reverb.mix = parameterValues.reverbMix->load();

    params.masterGainLinear = juce::Decibels::decibelsToGain(parameterValues.masterGainDb->load());

    // --- Phaser ---
    if (parameterValues.phaserEnabled != nullptr)
    {
        params.phaser.enabled = decodeBool(parameterValues.phaserEnabled->load());
        params.phaser.rateHz = juce::jlimit(0.05f, 8.0f, parameterValues.phaserRateHz->load());
        params.phaser.depth = juce::jlimit(0.0f, 1.0f, parameterValues.phaserDepth->load());
    }

    // --- Compressor ---
    if (parameterValues.compressorEnabled != nullptr)
    {
        params.compressor.enabled = decodeBool(parameterValues.compressorEnabled->load());
        params.compressor.amount = juce::jlimit(0.0f, 1.0f, parameterValues.compressorAmount->load());
        params.compressor.mix = juce::jlimit(0.0f, 1.0f, parameterValues.compressorMix->load());
    }

    // --- Macros: fold timbre and excite into the already-resolved snapshot.
    // These macros never write back to the underlying knobs; they only adjust the per-block
    // snapshot, so the source parameter values displayed in the UI stay where the user put them.
    const float timbre = parameterValues.timbre != nullptr
        ? juce::jlimit(-1.0f, 1.0f, parameterValues.timbre->load())
        : 0.0f;
    const float excite = parameterValues.excite != nullptr
        ? juce::jlimit(0.0f, 1.0f, parameterValues.excite->load())
        : 0.0f;

    if (timbre != 0.0f)
    {
        // Cutoff: multiplicative shift up to ±2.83x (i.e. ±1.5 octaves)
        const float cutoffMultiplier = std::pow(2.0f, timbre * 1.5f);
        params.filter.cutoffHz = juce::jlimit(20.0f, 20000.0f,
                                              params.filter.cutoffHz * cutoffMultiplier);

        // Pulse width: push toward narrow at -1, wide at +1 (stays inside legal 0.05..0.95)
        params.oscA.pulseWidth = juce::jlimit(0.05f, 0.95f, params.oscA.pulseWidth + timbre * 0.30f);
        params.oscB.pulseWidth = juce::jlimit(0.05f, 0.95f, params.oscB.pulseWidth + timbre * 0.30f);

        // Drive amount: only positive timbre adds drive; negative leaves drive untouched
        if (timbre > 0.0f)
            params.drive.amount = juce::jlimit(0.0f, 1.0f, params.drive.amount + timbre * 0.25f);
    }

    if (excite > 0.0f)
    {
        // Shorten amp attack toward 1 ms at full excite
        params.ampEnvelope.attackSeconds = juce::jmax(0.001f,
            params.ampEnvelope.attackSeconds * (1.0f - excite * 0.95f));

        // Push filter envelope and velocity->filter routing harder
        params.filter.envelopeAmount = juce::jlimit(0.0f, 1.0f,
            params.filter.envelopeAmount + excite * 0.40f);
        params.performance.velocityToFilter = juce::jlimit(0.0f, 1.0f,
            params.performance.velocityToFilter + excite * 0.30f);
    }

    return params;
}

coolsynth::synth::ParameterValuePointersV2 SynthAudioProcessor::bindParameterPointers(APVTS& state)
{
    coolsynth::synth::ParameterValuePointersV2 pointers;
    for (auto& [id, memberPtr] : coolsynth::synth::kParamPtrBindings)
        pointers.*memberPtr = state.getRawParameterValue(id);
    return pointers;
}

SynthAudioProcessor::ChoiceParameterPointers SynthAudioProcessor::bindChoicePointers(APVTS& state)
{
    namespace ids = coolsynth::parameters::ids;

    auto getChoice = [&](const juce::String& id) -> juce::AudioParameterChoice*
    {
        return dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id));
    };

    ChoiceParameterPointers p;
    p.oscAWave       = getChoice(ids::oscAWave);
    p.oscBWave       = getChoice(ids::oscBWave);
    p.filterKeyTrack = getChoice(ids::filterKeyTracking);
    p.lfoWave        = getChoice(ids::lfoWave);
    p.playMode       = getChoice(ids::playMode);
    p.keyPriority    = getChoice(ids::keyPriority);
    p.arpRate        = getChoice(ids::arpRateDivision);
    p.arpPattern     = getChoice(ids::arpPattern);
    return p;
}

// Static creation function required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}

