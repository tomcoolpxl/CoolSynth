#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SynthEngineV2.h"
#include "SynthParameters.h"

namespace coolsynth::synth
{
    class SynthEngine final
    {
    public:
        SynthEngine() = default;

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiMessages,
                    const BlockRenderParameters& parameters);
        void panic() noexcept;

    private:
        static BlockRenderParametersV2 mapToV2Parameters(const BlockRenderParameters& parameters) noexcept;

        SynthEngineV2 v2Engine;
    };
}
