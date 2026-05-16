#include "GlobalDelay.h"

namespace coolsynth::synth
{
    void GlobalDelay::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        currentSampleRate = sampleRate;
        preparedOutputChannels = outputChannelCount;
        
        // Maximum 1 second delay + safety padding
        maximumDelaySamples = static_cast<int>(std::ceil(sampleRate * 1.0)) + 4;
        
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = static_cast<juce::uint32>(outputChannelCount);
        
        delayLine.prepare(spec);
        delayLine.setMaximumDelayInSamples(maximumDelaySamples);
        
        delaySamplesSmoother.reset(sampleRate, parameterSmoothingSeconds);
        feedbackSmoother.reset(sampleRate, parameterSmoothingSeconds);
        mixSmoother.reset(sampleRate, parameterSmoothingSeconds);
        
        reset();
        
        prepared = true;
    }

    void GlobalDelay::reset() noexcept
    {
        clear();
        
        delaySamplesSmoother.setCurrentAndTargetValue(delayMsToSamples(250.0f));
        feedbackSmoother.setCurrentAndTargetValue(0.25f);
        mixSmoother.setCurrentAndTargetValue(0.0f);
    }

    void GlobalDelay::clear() noexcept
    {
        delayLine.reset();
    }

    void GlobalDelay::process(juce::AudioBuffer<float>& buffer,
                              const DelayParametersV2& parameters) noexcept
    {
        if (!prepared)
            return;

        updateTargets(parameters);

        const auto numChannels = juce::jmin(buffer.getNumChannels(), preparedOutputChannels);
        const auto numSamples = buffer.getNumSamples();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto delaySamples = delaySamplesSmoother.getNextValue();
            const auto feedback = feedbackSmoother.getNextValue();
            const auto mix = mixSmoother.getNextValue();

            for (int channel = 0; channel < numChannels; ++channel)
            {
                const float dry = buffer.getSample(channel, sample);
                const float wet = delayLine.popSample(channel, delaySamples);
                const float feedbackInput = dry + (wet * feedback);

                delayLine.pushSample(channel, feedbackInput);
                
                // Crossfade: dry + ((wet - dry) * mix)
                buffer.setSample(channel, sample, dry + ((wet - dry) * mix));
            }
        }
    }

    float GlobalDelay::clampDelayMs(float timeMs) const noexcept
    {
        return juce::jlimit(minDelayTimeMs, maxDelayTimeMs, timeMs);
    }

    float GlobalDelay::clampFeedback(float feedback) const noexcept
    {
        return juce::jlimit(0.0f, maxFeedback, feedback);
    }

    float GlobalDelay::clampMix(float mix) const noexcept
    {
        return juce::jlimit(0.0f, 1.0f, mix);
    }

    float GlobalDelay::delayMsToSamples(float timeMs) const noexcept
    {
        const float samples = clampDelayMs(timeMs) * static_cast<float>(currentSampleRate) / 1000.0f;
        return juce::jmin(samples, static_cast<float>(maximumDelaySamples - 1));
    }

    void GlobalDelay::updateTargets(const DelayParametersV2& parameters) noexcept
    {
        delaySamplesSmoother.setTargetValue(delayMsToSamples(parameters.timeMs));
        feedbackSmoother.setTargetValue(clampFeedback(parameters.feedback));
        mixSmoother.setTargetValue(clampMix(parameters.mix));
    }
}
