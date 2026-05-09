#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SynthParameters.h"

namespace coolsynth::synth
{
    /**
     * Global delay effect stage.
     * Uses juce::dsp::DelayLine with linear interpolation for real-time safety.
     */
    class GlobalDelay final
    {
    public:
        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void reset() noexcept;
        void process(juce::AudioBuffer<float>& buffer,
                     const DelayParameters& parameters) noexcept;

    private:
        using DelayLine =
            juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

        static constexpr float minDelayTimeMs = 1.0f;
        static constexpr float maxDelayTimeMs = 1000.0f;
        static constexpr float maxFeedback = 0.85f;
        static constexpr double parameterSmoothingSeconds = 0.02;

        float clampDelayMs(float timeMs) const noexcept;
        float clampFeedback(float feedback) const noexcept;
        float clampMix(float mix) const noexcept;
        float delayMsToSamples(float timeMs) const noexcept;
        void updateTargets(const DelayParameters& parameters) noexcept;

        DelayLine delayLine;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamplesSmoother;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoother;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
        double currentSampleRate = 0.0;
        int preparedOutputChannels = 0;
        int maximumDelaySamples = 0;
        bool prepared = false;
    };
}
