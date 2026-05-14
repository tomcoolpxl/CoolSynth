#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SynthEngine.h"
#include "SynthParameters.h"

namespace coolsynth::synth
{
    class SynthEngineV2 final
    {
    public:
        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiMessages,
                    const BlockRenderParametersV2& parameters);
        void panic() noexcept;

    private:
        static BlockRenderParameters mapToLegacyParameters(const BlockRenderParametersV2& parameters) noexcept;
        static coolsynth::parameters::WaveformChoice mapLegacyWaveform(const BlockRenderParametersV2& parameters) noexcept;

        SynthEngine legacyEngine;
    };
}
