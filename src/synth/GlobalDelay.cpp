#include "GlobalDelay.h"

#include <algorithm>
#include <cmath>

namespace coolsynth::synth
{
    void GlobalDelay::prepare(double sampleRate, int /*samplesPerBlock*/, int outputChannelCount)
    {
        currentSampleRate = std::max(1000.0, sampleRate);

        // Longest delay plus room for the interpolation neighbours.
        bufferLength = static_cast<int>(std::ceil(currentSampleRate * (maxDelayTimeMs / 1000.0))) + 8;

        channels.assign(static_cast<size_t>(std::max(1, outputChannelCount)), Channel {});
        for (auto& channel : channels)
            channel.buffer.assign(static_cast<size_t>(bufferLength), 0.0f);

        timeSlewCoefficient = std::exp(-1.0f / static_cast<float>(currentSampleRate * timeSlewSeconds));
        lowPassCoefficient = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * feedbackLowPassHz
                                             / static_cast<float>(currentSampleRate));
        highPassCoefficient = std::exp(-2.0f * juce::MathConstants<float>::pi * feedbackHighPassHz
                                       / static_cast<float>(currentSampleRate));

        feedbackSmoother.reset(currentSampleRate, parameterSmoothingSeconds);
        mixSmoother.reset(currentSampleRate, parameterSmoothingSeconds);

        reset();
        prepared = true;
    }

    void GlobalDelay::reset() noexcept
    {
        clear();
        currentDelaySamples = delayMsToSamples(250.0f);
        targetDelaySamples = currentDelaySamples;
        feedbackSmoother.setCurrentAndTargetValue(0.25f);
        mixSmoother.setCurrentAndTargetValue(0.0f);
    }

    void GlobalDelay::clear() noexcept
    {
        for (auto& channel : channels)
        {
            std::fill(channel.buffer.begin(), channel.buffer.end(), 0.0f);
            channel.writeIndex = 0;
            channel.lowPassState = 0.0f;
            channel.highPassState = 0.0f;
            channel.highPassPreviousInput = 0.0f;
        }
    }

    void GlobalDelay::process(juce::AudioBuffer<float>& buffer,
                              const DelayParametersV2& parameters) noexcept
    {
        if (! prepared || bufferLength < 8)
            return;

        updateTargets(parameters);

        const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels.size()));
        const int numSamples = buffer.getNumSamples();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Slew the delay time so knob moves glide the repeats like a tape head moving.
            currentDelaySamples += (targetDelaySamples - currentDelaySamples) * (1.0f - timeSlewCoefficient);
            const float feedback = feedbackSmoother.getNextValue();
            const float mix = mixSmoother.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t>(ch)];
                const float dry = buffer.getSample(ch, sample);
                const float wet = readHermite(channel, currentDelaySamples);

                // Feedback path: darken, thin out, then soft-clip so runaway settings stay musical.
                channel.lowPassState += (wet - channel.lowPassState) * lowPassCoefficient;
                const float darkened = channel.lowPassState;
                channel.highPassState = highPassCoefficient * (channel.highPassState + darkened - channel.highPassPreviousInput);
                channel.highPassPreviousInput = darkened;
                float feedbackInput = dry + std::tanh(channel.highPassState * feedback);

                if (std::abs(feedbackInput) < 1.0e-7f)
                    feedbackInput = 0.0f;

                channel.buffer[static_cast<size_t>(channel.writeIndex)] = feedbackInput;
                channel.writeIndex = (channel.writeIndex + 1) % bufferLength;

                buffer.setSample(ch, sample, dry + (wet - dry) * mix);
            }
        }
    }

    float GlobalDelay::clampDelayMs(float timeMs) const noexcept
    {
        return juce::jlimit(minDelayTimeMs, maxDelayTimeMs, timeMs);
    }

    float GlobalDelay::delayMsToSamples(float timeMs) const noexcept
    {
        const float samples = clampDelayMs(timeMs) * static_cast<float>(currentSampleRate) / 1000.0f;
        return juce::jlimit(2.0f, static_cast<float>(bufferLength - 4), samples);
    }

    float GlobalDelay::readHermite(const Channel& channel, float delaySamples) const noexcept
    {
        const float clamped = juce::jlimit(2.0f, static_cast<float>(bufferLength - 4), delaySamples);
        const int whole = static_cast<int>(clamped);
        const float t = clamped - static_cast<float>(whole);

        const auto sampleAt = [&](int offsetBack) noexcept
        {
            int index = channel.writeIndex - offsetBack;
            while (index < 0)
                index += bufferLength;
            return channel.buffer[static_cast<size_t>(index % bufferLength)];
        };

        const float xm1 = sampleAt(whole - 1);
        const float x0 = sampleAt(whole);
        const float x1 = sampleAt(whole + 1);
        const float x2 = sampleAt(whole + 2);

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void GlobalDelay::updateTargets(const DelayParametersV2& parameters) noexcept
    {
        targetDelaySamples = delayMsToSamples(parameters.timeMs);
        feedbackSmoother.setTargetValue(juce::jlimit(0.0f, maxFeedback, parameters.feedback));
        mixSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, parameters.mix));
    }
}
