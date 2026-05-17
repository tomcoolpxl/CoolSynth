#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthParameters.h"

namespace coolsynth::synth
{
    // Feed-forward peak compressor with a stereo-linked envelope, fixed soft knee,
    // and auto-makeup gain. A single `amount` knob walks threshold from 0 dB down
    // toward -24 dB while ratio rises from 1:1 toward 6:1.
    class Compressor final
    {
    public:
        static constexpr float attackSeconds = 0.005f;     // 5 ms
        static constexpr float releaseSeconds = 0.100f;    // 100 ms
        static constexpr float kneeWidthDb = 6.0f;
        static constexpr float minThresholdDb = -24.0f;
        static constexpr float maxRatio = 6.0f;

        void prepare(double newSampleRate, int samplesPerBlock, int outputChannelCount);
        void reset() noexcept;
        void process(juce::AudioBuffer<float>& buffer,
                     const CompressorParametersV2& parameters) noexcept;

    private:
        float envelopeDb = -120.0f;
        float attackCoef = 0.0f;
        float releaseCoef = 0.0f;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;
        double sampleRate = 44100.0;
        bool prepared = false;
    };
}
