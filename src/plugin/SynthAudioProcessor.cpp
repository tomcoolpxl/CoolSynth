#include "SynthAudioProcessor.h"

#include "SynthAudioProcessorEditor.h"
#include "parameters/ParameterIDs.h"
#include "parameters/ParameterLayout.h"

SynthAudioProcessor::SynthAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "CoolSynthState", coolsynth::parameters::createParameterLayout())
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
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

void SynthAudioProcessor::requestPanic() noexcept
{
    panicRequested.store(true, std::memory_order_release);
}

coolsynth::synth::BlockRenderParameters SynthAudioProcessor::makeBlockRenderParameters() const noexcept
{
    using namespace coolsynth::parameters;

    coolsynth::synth::BlockRenderParameters params;
    params.ampEnvelope.attackSeconds = juce::jmax(0.001f, parameterValues.ampAttackMs->load() * 0.001f);
    params.ampEnvelope.decaySeconds = juce::jmax(0.005f, parameterValues.ampDecayMs->load() * 0.001f);
    params.ampEnvelope.sustainLevel = juce::jlimit(0.0f, 1.0f, parameterValues.ampSustain->load());
    params.ampEnvelope.releaseSeconds = juce::jmax(0.005f, parameterValues.ampReleaseMs->load() * 0.001f);
    params.masterGainLinear = juce::Decibels::decibelsToGain(parameterValues.masterGainDb->load());
    return params;
}

coolsynth::synth::ParameterValuePointers SynthAudioProcessor::bindParameterPointers(APVTS& state)
{
    namespace ids = coolsynth::parameters::ids;

    coolsynth::synth::ParameterValuePointers pointers;
    pointers.ampAttackMs = state.getRawParameterValue(ids::ampAttackMs);
    pointers.ampDecayMs = state.getRawParameterValue(ids::ampDecayMs);
    pointers.ampSustain = state.getRawParameterValue(ids::ampSustain);
    pointers.ampReleaseMs = state.getRawParameterValue(ids::ampReleaseMs);
    pointers.masterGainDb = state.getRawParameterValue(ids::masterGainDb);
    return pointers;
}

// Static creation function required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}
