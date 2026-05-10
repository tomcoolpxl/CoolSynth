#include "SynthAudioProcessor.h"

#include <algorithm>
#include <array>
#include <optional>

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
    inline constexpr int processorStateFormatVersion = 1;
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

    if (! juce::JUCEApplicationBase::isStandaloneApp())
    {
        for (const auto metadata : midiMessages)
        {
            if (const auto event = makeControllerMidiEvent(metadata.getMessage()))
            {
                enqueuePluginControllerEvent(*event);

                const auto action = midiMappingEngine.translate(*event);
                if (action.kind != coolsynth::midi::MappedAction::Kind::none)
                    enqueuePluginMappedAction(action);
            }
        }
    }

    synthEngine.render(buffer, midiMessages, makeBlockRenderParameters());
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
        if (xml->hasTagName(parameters.state.getType()))
        {
            if (applyParameterStateXml(*xml))
                setLearnedMidiBindings({});

            return;
        }

        if (! xml->hasTagName(processorStateRootTag))
            return;

        if (auto* parameterStateXml = xml->getChildByName(parameters.state.getType()))
        {
            if (applyParameterStateXml(*parameterStateXml))
                setLearnedMidiBindings(parseLearnedMidiBindingsXml(*xml));
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

    auto parameterState = const_cast<SynthAudioProcessor*> (this)->parameters.copyState();
    root->addChildElement(parameterState.createXml().release());

    auto learnedMappings = std::make_unique<juce::XmlElement>(learnedMidiMappingsTag);
    learnedMappings->setAttribute("version", 1);

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

    return appliedParameterCount > 0;
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
    const juce::ScopedLock lock(getCallbackLock());
    return midiMappingEngine.setActiveProfile(profileId);
}

juce::String SynthAudioProcessor::getActiveControllerProfileId() const
{
    return midiMappingEngine.getActiveProfileId();
}

juce::String SynthAudioProcessor::getActiveControllerProfileDisplayName() const
{
    return midiMappingEngine.getActiveProfileDisplayName();
}

void SynthAudioProcessor::setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings)
{
    const juce::ScopedLock lock(getCallbackLock());
    learnedMidiBindings.assign(bindings.begin(), bindings.end());
    midiMappingEngine.setLearnedBindings(bindings);
}

std::vector<coolsynth::midi::LearnedCcBinding> SynthAudioProcessor::getLearnedMidiBindings() const
{
    return learnedMidiBindings;
}

void SynthAudioProcessor::clearLearnedMidiBinding(juce::StringRef parameterId)
{
    const juce::ScopedLock lock(getCallbackLock());
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
    const juce::ScopedLock lock(getCallbackLock());
    const auto action = midiMappingEngine.translate(event);
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
    }
}

void SynthAudioProcessor::enqueuePluginMappedAction(const coolsynth::midi::MappedAction& action) noexcept
{
    int start1, size1, start2, size2;
    pendingMappedActionQueue.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        pendingMappedActions[static_cast<size_t> (start1)] = action;
        pendingMappedActionQueue.finishedWrite(1);
    }
}

int SynthAudioProcessor::drainPendingMappedActions(coolsynth::midi::MappedAction* destination,
                                                   int maxActions) noexcept
{
    int start1, size1, start2, size2;
    pendingMappedActionQueue.prepareToRead(maxActions, start1, size1, start2, size2);

    int totalRead = 0;
    for (int i = 0; i < size1; ++i)
        destination[totalRead++] = pendingMappedActions[static_cast<size_t> (start1 + i)];

    for (int i = 0; i < size2; ++i)
        destination[totalRead++] = pendingMappedActions[static_cast<size_t> (start2 + i)];

    pendingMappedActionQueue.finishedRead(totalRead);
    return totalRead;
}

void SynthAudioProcessor::dispatchPendingMappedActions()
{
    const juce::ScopedLock lock(getCallbackLock());
    std::array<coolsynth::midi::MappedAction, 32> localActions {};

    while (true)
    {
        const auto drained = drainPendingMappedActions(localActions.data(),
                                                       static_cast<int> (localActions.size()));
        if (drained == 0)
            break;

        for (int i = 0; i < drained; ++i)
            applyMappedAction(localActions[static_cast<size_t> (i)]);
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

static coolsynth::parameters::WaveformChoice decodeWaveformChoice(float rawValue) noexcept
{
    const int choice = juce::jlimit(0, 2, static_cast<int>(std::round(rawValue)));
    return static_cast<coolsynth::parameters::WaveformChoice>(choice);
}

coolsynth::synth::BlockRenderParameters SynthAudioProcessor::makeBlockRenderParameters() const noexcept
{
    using namespace coolsynth::parameters;

    coolsynth::synth::BlockRenderParameters params;
    params.waveform = decodeWaveformChoice(parameterValues.waveform->load());
    params.ampEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.ampAttackMs->load() * 0.001f);
    params.ampEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.ampDecayMs->load() * 0.001f);
    params.ampEnvelope.sustainLevel = juce::jlimit(0.0f, 1.0f, parameterValues.ampSustain->load());
    params.ampEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.ampReleaseMs->load() * 0.001f);
    params.filter.cutoffHz = juce::jlimit(20.0f, 20000.0f, parameterValues.filterCutoffHz->load());
    params.filter.resonanceNormalized = juce::jlimit(0.0f, 1.0f, parameterValues.filterResonance->load());
    
    params.delay.timeMs = juce::jlimit(1.0f, 1000.0f, parameterValues.delayTimeMs->load());
    params.delay.feedback = juce::jlimit(0.0f, 0.85f, parameterValues.delayFeedback->load());
    params.delay.mix = juce::jlimit(0.0f, 1.0f, parameterValues.delayMix->load());

    params.masterGainLinear = juce::Decibels::decibelsToGain(parameterValues.masterGainDb->load());
    return params;
}

coolsynth::synth::ParameterValuePointers SynthAudioProcessor::bindParameterPointers(APVTS& state)
{
    namespace ids = coolsynth::parameters::ids;

    coolsynth::synth::ParameterValuePointers pointers;
    pointers.waveform = state.getRawParameterValue(ids::waveform);
    pointers.ampAttackMs = state.getRawParameterValue(ids::ampAttackMs);
    pointers.ampDecayMs = state.getRawParameterValue(ids::ampDecayMs);
    pointers.ampSustain = state.getRawParameterValue(ids::ampSustain);
    pointers.ampReleaseMs = state.getRawParameterValue(ids::ampReleaseMs);
    pointers.filterCutoffHz = state.getRawParameterValue(ids::filterCutoffHz);
    pointers.filterResonance = state.getRawParameterValue(ids::filterResonance);
    pointers.delayTimeMs = state.getRawParameterValue(ids::delayTimeMs);
    pointers.delayFeedback = state.getRawParameterValue(ids::delayFeedback);
    pointers.delayMix = state.getRawParameterValue(ids::delayMix);
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
        owner.dispatchPendingMappedActions();
        wait(4);
    }

    owner.dispatchPendingMappedActions();
}
