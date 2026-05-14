#include "SynthEngineV2.h"

namespace coolsynth::synth
{
    void SynthEngineV2::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        legacyEngine.prepare(sampleRate, samplesPerBlock, outputChannelCount);
    }

    void SynthEngineV2::releaseResources() noexcept
    {
        legacyEngine.releaseResources();
    }

    void SynthEngineV2::render(juce::AudioBuffer<float>& outputBuffer,
                               juce::MidiBuffer& midiMessages,
                               const BlockRenderParametersV2& parameters)
    {
        auto legacyParameters = mapToLegacyParameters(parameters);
        legacyEngine.render(outputBuffer, midiMessages, legacyParameters);
    }

    void SynthEngineV2::panic() noexcept
    {
        legacyEngine.panic();
    }

    BlockRenderParameters SynthEngineV2::mapToLegacyParameters(const BlockRenderParametersV2& parameters) noexcept
    {
        BlockRenderParameters legacy;
        legacy.waveform = mapLegacyWaveform(parameters);
        legacy.ampEnvelope = parameters.ampEnvelope;
        legacy.filter.cutoffHz = parameters.filter.cutoffHz;
        legacy.filter.resonanceNormalized = parameters.filter.resonanceNormalized;
        legacy.delay.timeMs = parameters.delay.timeMs;
        legacy.delay.feedback = parameters.delay.enabled ? parameters.delay.feedback : 0.0f;
        legacy.delay.mix = parameters.delay.enabled ? parameters.delay.mix : 0.0f;
        legacy.masterGainLinear = parameters.masterGainLinear;
        return legacy;
    }

    coolsynth::parameters::WaveformChoice SynthEngineV2::mapLegacyWaveform(const BlockRenderParametersV2& parameters) noexcept
    {
        const auto selectShape = [&]() noexcept
        {
            if (parameters.oscB.level > parameters.oscA.level)
                return parameters.oscB.waveShape;

            return parameters.oscA.waveShape;
        };

        switch (selectShape())
        {
            case coolsynth::parameters::OscillatorWaveShape::pulse:
                return coolsynth::parameters::WaveformChoice::square;

            case coolsynth::parameters::OscillatorWaveShape::triangle:
                return coolsynth::parameters::WaveformChoice::sine;

            case coolsynth::parameters::OscillatorWaveShape::saw:
            default:
                return coolsynth::parameters::WaveformChoice::saw;
        }
    }
}
