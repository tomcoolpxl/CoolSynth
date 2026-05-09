#include "SynthAudioProcessor.h"

#include "SynthAudioProcessorEditor.h"
#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

SynthAudioProcessor::SynthAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "CoolSynthState", coolsynth::parameters::createParameterLayout())
    , midiMappingEngine(parameters)
    , parameterValues(bindParameterPointers(parameters))
{
}

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
        midiMessages.clear();
    }

    synthEngine.render(buffer, midiMessages, makeBlockRenderParameters());
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    return new SynthAudioProcessorEditor(*this);
}

void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = createParameterStateXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        applyParameterStateXml(*xml);
}

std::unique_ptr<juce::XmlElement> SynthAudioProcessor::createParameterStateXml()
{
    auto state = parameters.copyState();
    return state.createXml();
}

bool SynthAudioProcessor::applyParameterStateXml(const juce::XmlElement& stateXml)
{
    if (!stateXml.hasTagName(parameters.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml(stateXml);
    if (!tree.isValid())
        return false;

    parameters.replaceState(tree);
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

void SynthAudioProcessor::setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings)
{
    midiMappingEngine.setLearnedBindings(bindings);
}

void SynthAudioProcessor::clearLearnedMidiBinding(juce::StringRef parameterId)
{
    midiMappingEngine.clearLearnedBinding(parameterId);
}

void SynthAudioProcessor::handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event)
{
    const auto action = midiMappingEngine.translate(event);
    applyMappedAction(action);
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
