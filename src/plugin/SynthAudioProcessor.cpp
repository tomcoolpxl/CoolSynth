#include "SynthAudioProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>

#include "SynthAudioProcessorEditor.h"
#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

namespace
{
    inline constexpr char apvtsParameterChildType[] = "PARAM";
    inline constexpr char apvtsParameterIdProperty[] = "id";
    inline constexpr char apvtsParameterValueProperty[] = "value";
    inline constexpr char processorStateRootTag[] = "COOLSYNTH_PROCESSOR_STATE";
    inline constexpr char processorStateVersionProperty[] = "formatVersion";
    inline constexpr int processorStateFormatVersion = 2;
    inline constexpr char processorStateProductProperty[] = "product";
    inline constexpr char processorStateStateTypeProperty[] = "stateType";
    inline constexpr char learnedMidiMappingsTag[] = "LEARNED_MIDI_MAPPINGS";
    inline constexpr char learnedMidiMappingTag[] = "MAPPING";
    inline constexpr char learnedMidiMappingParameterIdProperty[] = "parameterId";
    inline constexpr char learnedMidiMappingChannelProperty[] = "channel";
    inline constexpr char learnedMidiMappingControllerProperty[] = "controller";

    std::optional<coolsynth::midi::ControllerMidiEvent> makeControllerMidiEvent(const juce::MidiMessage& message) noexcept
    {
        using namespace coolsynth::midi;

        ControllerMidiEvent event;

        if (message.isNoteOn())
        {
            event.type = ControllerMidiEventType::noteOn;
            event.data1 = static_cast<uint8_t> (message.getNoteNumber());
            event.data2 = static_cast<uint8_t> (message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            event.type = ControllerMidiEventType::noteOff;
            event.data1 = static_cast<uint8_t> (message.getNoteNumber());
            event.data2 = static_cast<uint8_t> (message.getVelocity());
        }
        else if (message.isController())
        {
            event.type = ControllerMidiEventType::controlChange;
            event.data1 = static_cast<uint8_t> (message.getControllerNumber());
            event.data2 = static_cast<uint8_t> (message.getControllerValue());
        }
        else
        {
            return std::nullopt;
        }

        event.channel = static_cast<uint8_t> (message.getChannel());
        return event;
    }

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
    , mappedActionDispatcher(*this)
{
}

SynthAudioProcessor::~SynthAudioProcessor() = default;

void SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthEngine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
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

        if (const auto event = makeControllerMidiEvent(metadata.getMessage()))
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

    if (auto* editor = dynamic_cast<SynthAudioProcessorEditor*>(getActiveEditor()))
        editor->getVisualizer().pushSamples(buffer.getReadPointer(0), buffer.getNumSamples());
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
        if (!seen)
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
    }
}

void SynthAudioProcessor::enqueuePluginMappedControllerEvent(const coolsynth::midi::ControllerMidiEvent& event) noexcept
{
    int start1, size1, start2, size2;
    pendingPluginMappedControllerEventQueue.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        pendingPluginMappedControllerEvents[static_cast<size_t> (start1)] = event;
        pendingPluginMappedControllerEventQueue.finishedWrite(1);
        return;
    }

    if (size2 > 0)
    {
        pendingPluginMappedControllerEvents[static_cast<size_t> (start2)] = event;
        pendingPluginMappedControllerEventQueue.finishedWrite(1);
    }
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
    delayParameters.feedback = juce::jlimit(0.0f, 0.85f, parameterValues.delayFeedback->load());

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

    coolsynth::parameters::OscillatorWaveShape decodeOscillatorWave(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 3, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::OscillatorWaveShape>(choice);
    }

    coolsynth::parameters::FilterKeyTrackingMode decodeFilterKeyTracking(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 2, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::FilterKeyTrackingMode>(choice);
    }

    coolsynth::parameters::LfoWaveShape decodeLfoWave(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 3, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::LfoWaveShape>(choice);
    }

    coolsynth::parameters::PlayModeChoice decodePlayMode(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 2, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::PlayModeChoice>(choice);
    }

    coolsynth::parameters::KeyPriorityChoice decodeKeyPriority(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 2, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::KeyPriorityChoice>(choice);
    }

    coolsynth::parameters::ArpRateChoice decodeArpRate(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 5, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::ArpRateChoice>(choice);
    }

    coolsynth::parameters::ArpPatternChoice decodeArpPattern(float rawValue) noexcept
    {
        const int choice = juce::jlimit(0, 3, static_cast<int>(std::round(rawValue)));
        return static_cast<coolsynth::parameters::ArpPatternChoice>(choice);
    }
}

coolsynth::synth::BlockRenderParametersV2 SynthAudioProcessor::makeBlockRenderParameters() const noexcept
{
    coolsynth::synth::BlockRenderParametersV2 params;

    params.oscA.waveShape = decodeOscillatorWave(parameterValues.oscAWave->load());
    params.oscA.octaveIndex = juce::jlimit(0, 4, static_cast<int>(std::round(parameterValues.oscAOctave->load())));
    params.oscA.fineCents = juce::jlimit(-50.0f, 50.0f, parameterValues.oscAFineCents->load());
    params.oscA.level = juce::jlimit(0.0f, 1.0f, parameterValues.oscALevel->load());
    params.oscA.pulseWidth = juce::jlimit(0.05f, 0.95f, parameterValues.oscAPulseWidth->load());
    params.oscA.syncEnabled = decodeBool(parameterValues.oscASyncEnabled->load());

    params.oscB.waveShape = decodeOscillatorWave(parameterValues.oscBWave->load());
    params.oscB.octaveIndex = juce::jlimit(0, 4, static_cast<int>(std::round(parameterValues.oscBOctave->load())));
    params.oscB.fineCents = juce::jlimit(-50.0f, 50.0f, parameterValues.oscBFineCents->load());
    params.oscB.level = juce::jlimit(0.0f, 1.0f, parameterValues.oscBLevel->load());
    params.oscB.pulseWidth = juce::jlimit(0.05f, 0.95f, parameterValues.oscBPulseWidth->load());
    params.oscB.lowFrequencyMode = decodeBool(parameterValues.oscBLowFrequencyMode->load());

    params.mixer.noiseLevel = juce::jlimit(0.0f, 1.0f, parameterValues.noiseLevel->load());

    params.filter.cutoffHz = juce::jlimit(20.0f, 20000.0f, parameterValues.filterCutoffHz->load());
    params.filter.resonanceNormalized = juce::jlimit(0.0f, 1.0f, parameterValues.filterResonance->load());
    params.filter.envelopeAmount = juce::jlimit(0.0f, 1.0f, parameterValues.filterEnvAmount->load());
    params.filter.keyTracking = decodeFilterKeyTracking(parameterValues.filterKeyTracking->load());

    params.filterEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.filterAttackMs->load() * 0.001f);
    params.filterEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.filterDecayMs->load() * 0.001f);
    params.filterEnvelope.sustainLevel = juce::jlimit(0.0f, 1.0f, parameterValues.filterSustain->load());
    params.filterEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.filterReleaseMs->load() * 0.001f);

    params.ampEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.ampAttackMs->load() * 0.001f);
    params.ampEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.ampDecayMs->load() * 0.001f);
    params.ampEnvelope.sustainLevel = juce::jlimit(0.0f, 1.0f, parameterValues.ampSustain->load());
    params.ampEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.ampReleaseMs->load() * 0.001f);

    params.lfo.rateHz = juce::jlimit(0.05f, 20.0f, parameterValues.lfoRateHz->load());
    params.lfo.waveShape = decodeLfoWave(parameterValues.lfoWave->load());
    params.lfo.oscPitchDepth = juce::jlimit(0.0f, 1.0f, parameterValues.lfoToOscPitch->load());
    params.lfo.pulseWidthDepth = juce::jlimit(0.0f, 1.0f, parameterValues.lfoToPulseWidth->load());
    params.lfo.filterCutoffDepth = juce::jlimit(0.0f, 1.0f, parameterValues.lfoToFilterCutoff->load());
    params.lfo.modWheelDepth = juce::jlimit(0.0f, 1.0f, parameterValues.modWheelToLfoDepth->load());

    params.polyMod.oscBToOscPitch = juce::jlimit(0.0f, 1.0f, parameterValues.polyModOscBToOscPitch->load());
    params.polyMod.envToOscPitch = juce::jlimit(0.0f, 1.0f, parameterValues.polyModEnvToOscPitch->load());
    params.polyMod.oscBToPulseWidth = juce::jlimit(0.0f, 1.0f, parameterValues.polyModOscBToPulseWidth->load());
    params.polyMod.envToPulseWidth = juce::jlimit(0.0f, 1.0f, parameterValues.polyModEnvToPulseWidth->load());
    params.polyMod.oscBToFilterCutoff = juce::jlimit(0.0f, 1.0f, parameterValues.polyModOscBToFilterCutoff->load());
    params.polyMod.envToFilterCutoff = juce::jlimit(0.0f, 1.0f, parameterValues.polyModEnvToFilterCutoff->load());

    params.performance.glideTimeSeconds = juce::jlimit(0.0f, 2.0f, parameterValues.glideTimeMs->load() * 0.001f);
    params.performance.playMode = decodePlayMode(parameterValues.playMode->load());
    params.performance.keyPriority = decodeKeyPriority(parameterValues.keyPriority->load());
    params.performance.pitchBendRangeSemitones = juce::jlimit(1.0f, 24.0f, parameterValues.pitchBendRangeSemitones->load());
    params.performance.vintageAmount = juce::jlimit(0.0f, 1.0f, parameterValues.vintageAmount->load());
    params.performance.panSpread = juce::jlimit(0.0f, 1.0f, parameterValues.panSpread->load());
    params.performance.velocityToAmp = juce::jlimit(0.0f, 1.0f, parameterValues.velocityToAmp->load());
    params.performance.velocityToFilter = juce::jlimit(0.0f, 1.0f, parameterValues.velocityToFilter->load());

    params.arp.enabled = decodeBool(parameterValues.arpEnabled->load());
    params.arp.internalTempoBpm = juce::jlimit(40.0f, 240.0f, parameterValues.arpInternalTempoBpm->load());
    params.arp.rate = decodeArpRate(parameterValues.arpRateDivision->load());
    params.arp.pattern = decodeArpPattern(parameterValues.arpPattern->load());
    params.arp.octaveRange = juce::jlimit(1, 3, static_cast<int>(std::round(parameterValues.arpOctaveRange->load())));
    params.arp.gateLength = juce::jlimit(0.0f, 1.0f, parameterValues.arpGate->load());
    params.arp.latch = decodeBool(parameterValues.arpLatch->load());

    params.drive.enabled = decodeBool(parameterValues.driveEnabled->load());
    params.drive.amount = juce::jlimit(0.0f, 1.0f, parameterValues.driveAmount->load());
    params.drive.mix = juce::jlimit(0.0f, 1.0f, parameterValues.driveMix->load());

    params.chorus.enabled = decodeBool(parameterValues.chorusEnabled->load());
    params.chorus.rateHz = juce::jlimit(0.05f, 10.0f, parameterValues.chorusRateHz->load());
    params.chorus.depth = juce::jlimit(0.0f, 1.0f, parameterValues.chorusDepth->load());
    params.chorus.mix = juce::jlimit(0.0f, 1.0f, parameterValues.chorusMix->load());

    params.delay.enabled = decodeBool(parameterValues.delayEnabled->load());
    params.delay.timeMs = juce::jlimit(1.0f, 1000.0f, parameterValues.delayTimeMs->load());
    params.delay.feedback = juce::jlimit(0.0f, 0.85f, parameterValues.delayFeedback->load());
    params.delay.mix = juce::jlimit(0.0f, 1.0f, parameterValues.delayMix->load());

    params.reverb.enabled = decodeBool(parameterValues.reverbEnabled->load());
    params.reverb.size = juce::jlimit(0.0f, 1.0f, parameterValues.reverbSize->load());
    params.reverb.damping = juce::jlimit(0.0f, 1.0f, parameterValues.reverbDamping->load());
    params.reverb.mix = juce::jlimit(0.0f, 1.0f, parameterValues.reverbMix->load());

    params.masterGainLinear = juce::Decibels::decibelsToGain(parameterValues.masterGainDb->load());
    return params;
}

coolsynth::synth::ParameterValuePointersV2 SynthAudioProcessor::bindParameterPointers(APVTS& state)
{
    namespace ids = coolsynth::parameters::ids;

    coolsynth::synth::ParameterValuePointersV2 pointers;
    pointers.oscAWave = state.getRawParameterValue(ids::oscAWave);
    pointers.oscAOctave = state.getRawParameterValue(ids::oscAOctave);
    pointers.oscAFineCents = state.getRawParameterValue(ids::oscAFineCents);
    pointers.oscALevel = state.getRawParameterValue(ids::oscALevel);
    pointers.oscAPulseWidth = state.getRawParameterValue(ids::oscAPulseWidth);
    pointers.oscASyncEnabled = state.getRawParameterValue(ids::oscASyncEnabled);
    pointers.oscBWave = state.getRawParameterValue(ids::oscBWave);
    pointers.oscBOctave = state.getRawParameterValue(ids::oscBOctave);
    pointers.oscBFineCents = state.getRawParameterValue(ids::oscBFineCents);
    pointers.oscBLevel = state.getRawParameterValue(ids::oscBLevel);
    pointers.oscBPulseWidth = state.getRawParameterValue(ids::oscBPulseWidth);
    pointers.oscBLowFrequencyMode = state.getRawParameterValue(ids::oscBLowFrequencyMode);
    pointers.noiseLevel = state.getRawParameterValue(ids::noiseLevel);
    pointers.filterCutoffHz = state.getRawParameterValue(ids::filterCutoffHz);
    pointers.filterResonance = state.getRawParameterValue(ids::filterResonance);
    pointers.filterEnvAmount = state.getRawParameterValue(ids::filterEnvAmount);
    pointers.filterKeyTracking = state.getRawParameterValue(ids::filterKeyTracking);
    pointers.filterAttackMs = state.getRawParameterValue(ids::filterAttackMs);
    pointers.filterDecayMs = state.getRawParameterValue(ids::filterDecayMs);
    pointers.filterSustain = state.getRawParameterValue(ids::filterSustain);
    pointers.filterReleaseMs = state.getRawParameterValue(ids::filterReleaseMs);
    pointers.ampAttackMs = state.getRawParameterValue(ids::ampAttackMs);
    pointers.ampDecayMs = state.getRawParameterValue(ids::ampDecayMs);
    pointers.ampSustain = state.getRawParameterValue(ids::ampSustain);
    pointers.ampReleaseMs = state.getRawParameterValue(ids::ampReleaseMs);
    pointers.lfoRateHz = state.getRawParameterValue(ids::lfoRateHz);
    pointers.lfoWave = state.getRawParameterValue(ids::lfoWave);
    pointers.lfoToOscPitch = state.getRawParameterValue(ids::lfoToOscPitch);
    pointers.lfoToPulseWidth = state.getRawParameterValue(ids::lfoToPulseWidth);
    pointers.lfoToFilterCutoff = state.getRawParameterValue(ids::lfoToFilterCutoff);
    pointers.modWheelToLfoDepth = state.getRawParameterValue(ids::modWheelToLfoDepth);
    pointers.polyModOscBToOscPitch = state.getRawParameterValue(ids::polyModOscBToOscPitch);
    pointers.polyModEnvToOscPitch = state.getRawParameterValue(ids::polyModEnvToOscPitch);
    pointers.polyModOscBToPulseWidth = state.getRawParameterValue(ids::polyModOscBToPulseWidth);
    pointers.polyModEnvToPulseWidth = state.getRawParameterValue(ids::polyModEnvToPulseWidth);
    pointers.polyModOscBToFilterCutoff = state.getRawParameterValue(ids::polyModOscBToFilterCutoff);
    pointers.polyModEnvToFilterCutoff = state.getRawParameterValue(ids::polyModEnvToFilterCutoff);
    pointers.glideTimeMs = state.getRawParameterValue(ids::glideTimeMs);
    pointers.playMode = state.getRawParameterValue(ids::playMode);
    pointers.keyPriority = state.getRawParameterValue(ids::keyPriority);
    pointers.pitchBendRangeSemitones = state.getRawParameterValue(ids::pitchBendRangeSemitones);
    pointers.vintageAmount = state.getRawParameterValue(ids::vintageAmount);
    pointers.panSpread = state.getRawParameterValue(ids::panSpread);
    pointers.velocityToAmp = state.getRawParameterValue(ids::velocityToAmp);
    pointers.velocityToFilter = state.getRawParameterValue(ids::velocityToFilter);
    pointers.arpEnabled = state.getRawParameterValue(ids::arpEnabled);
    pointers.arpInternalTempoBpm = state.getRawParameterValue(ids::arpInternalTempoBpm);
    pointers.arpRateDivision = state.getRawParameterValue(ids::arpRateDivision);
    pointers.arpPattern = state.getRawParameterValue(ids::arpPattern);
    pointers.arpOctaveRange = state.getRawParameterValue(ids::arpOctaveRange);
    pointers.arpGate = state.getRawParameterValue(ids::arpGate);
    pointers.arpLatch = state.getRawParameterValue(ids::arpLatch);
    pointers.driveEnabled = state.getRawParameterValue(ids::driveEnabled);
    pointers.driveAmount = state.getRawParameterValue(ids::driveAmount);
    pointers.driveMix = state.getRawParameterValue(ids::driveMix);
    pointers.chorusEnabled = state.getRawParameterValue(ids::chorusEnabled);
    pointers.chorusRateHz = state.getRawParameterValue(ids::chorusRateHz);
    pointers.chorusDepth = state.getRawParameterValue(ids::chorusDepth);
    pointers.chorusMix = state.getRawParameterValue(ids::chorusMix);
    pointers.delayEnabled = state.getRawParameterValue(ids::delayEnabled);
    pointers.delayTimeMs = state.getRawParameterValue(ids::delayTimeMs);
    pointers.delayFeedback = state.getRawParameterValue(ids::delayFeedback);
    pointers.delayMix = state.getRawParameterValue(ids::delayMix);
    pointers.reverbEnabled = state.getRawParameterValue(ids::reverbEnabled);
    pointers.reverbSize = state.getRawParameterValue(ids::reverbSize);
    pointers.reverbDamping = state.getRawParameterValue(ids::reverbDamping);
    pointers.reverbMix = state.getRawParameterValue(ids::reverbMix);
    pointers.masterGainDb = state.getRawParameterValue(ids::masterGainDb);
    return pointers;
}

// Static creation function required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}

SynthAudioProcessor::PluginMappedActionDispatcher::PluginMappedActionDispatcher(SynthAudioProcessor& ownerIn)
    : juce::Thread("CoolSynthPluginMappedActionDispatcher")
    , owner(ownerIn)
{
    startThread();
}

SynthAudioProcessor::PluginMappedActionDispatcher::~PluginMappedActionDispatcher()
{
    stopThread(500);
}

void SynthAudioProcessor::PluginMappedActionDispatcher::run()
{
    while (! threadShouldExit())
    {
        owner.dispatchPendingMappedControllerEvents();
        wait(4);
    }

    owner.dispatchPendingMappedControllerEvents();
}
