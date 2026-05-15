#include "GlobalFxRack.h"

#include <cmath>

namespace coolsynth::synth
{
    namespace
    {
        constexpr float chorusCentreDelayMs = 7.5f;
        constexpr float chorusFeedback = 0.1f;
        constexpr float maxDrivePreGain = 16.0f;
        constexpr double conservativeReverbTailSeconds = 12.0;
        constexpr double conservativeDelayFeedbackTailSeconds = 48.0;

        float clampUnit(float value) noexcept
        {
            return juce::jlimit(0.0f, 1.0f, value);
        }

        float computeDrivePreGain(float amount) noexcept
        {
            return 1.0f + (clampUnit(amount) * (maxDrivePreGain - 1.0f));
        }

        float applyDriveShape(float input, float preGain) noexcept
        {
            const float wet = std::tanh(input * preGain);
            const float normalizer = std::tanh(preGain);
            if (normalizer <= 0.0f)
                return input;

            return wet / normalizer;
        }

        bool hasAudibleDelayTail(const DelayParametersV2& parameters) noexcept
        {
            return parameters.enabled && parameters.mix > 0.0f;
        }

        bool hasAudibleReverbTail(const ReverbParametersV2& parameters) noexcept
        {
            return parameters.enabled && parameters.mix > 0.0f;
        }
    }

    void GlobalFxRack::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = static_cast<juce::uint32>(juce::jmax(1, outputChannelCount));

        chorus.prepare(spec);
        reverb.prepare(spec);
        delay.prepare(sampleRate, samplesPerBlock, outputChannelCount);

        reset();
        prepared = true;
    }

    void GlobalFxRack::reset() noexcept
    {
        chorus.reset();
        delay.reset();
        reverb.reset();
        chorusWasAudible = false;
        delayWasAudible = false;
        reverbWasAudible = false;
    }

    void GlobalFxRack::clear() noexcept
    {
        chorus.reset();
        delay.clear();
        reverb.reset();
        chorusWasAudible = false;
        delayWasAudible = false;
        reverbWasAudible = false;
    }

    void GlobalFxRack::process(juce::AudioBuffer<float>& buffer,
                               const DriveParametersV2& driveParameters,
                               const ChorusParametersV2& chorusParameters,
                               const DelayParametersV2& delayParameters,
                               const ReverbParametersV2& reverbParametersIn) noexcept
    {
        if (! prepared)
            return;

        processDrive(buffer, driveParameters);
        processChorus(buffer, chorusParameters);
        processDelay(buffer, delayParameters);
        processReverb(buffer, reverbParametersIn);
    }

    double GlobalFxRack::estimateTailLengthSeconds(const DelayParametersV2& delayParameters,
                                                   const ReverbParametersV2& reverbParametersIn) noexcept
    {
        double tailSeconds = 0.0;

        if (hasAudibleDelayTail(delayParameters))
        {
            const auto oneShotTail = juce::jlimit(0.0, 2.0,
                                                  static_cast<double>(juce::jlimit(1.0f, 1000.0f, delayParameters.timeMs)) / 1000.0
                                                      + 0.25);
            tailSeconds = delayParameters.feedback > 0.0f ? conservativeDelayFeedbackTailSeconds
                                                          : oneShotTail;
        }

        if (hasAudibleReverbTail(reverbParametersIn))
            tailSeconds = juce::jmax(tailSeconds, conservativeReverbTailSeconds);

        return tailSeconds;
    }

    void GlobalFxRack::processDrive(juce::AudioBuffer<float>& buffer,
                                    const DriveParametersV2& parameters) noexcept
    {
        if (! parameters.enabled)
            return;

        const float mix = clampUnit(parameters.mix);
        if (mix <= 0.0f)
            return;

        const float preGain = computeDrivePreGain(parameters.amount);
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = buffer.getWritePointer(channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const float dry = samples[sample];
                const float wet = applyDriveShape(dry, preGain);
                samples[sample] = dry + ((wet - dry) * mix);
            }
        }
    }

    void GlobalFxRack::processChorus(juce::AudioBuffer<float>& buffer,
                                     const ChorusParametersV2& parameters) noexcept
    {
        const bool audible = parameters.enabled
                             && parameters.mix > 0.0f
                             && parameters.depth > 0.0f;

        if (! audible)
        {
            if (chorusWasAudible)
                chorus.reset();

            chorusWasAudible = false;
            return;
        }

        chorus.setRate(juce::jlimit(0.05f, 99.0f, parameters.rateHz));
        chorus.setDepth(clampUnit(parameters.depth));
        chorus.setCentreDelay(chorusCentreDelayMs);
        chorus.setFeedback(chorusFeedback);
        chorus.setMix(clampUnit(parameters.mix));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        chorus.process(context);
        chorusWasAudible = true;
    }

    void GlobalFxRack::processDelay(juce::AudioBuffer<float>& buffer,
                                    const DelayParametersV2& parameters) noexcept
    {
        const bool audible = hasAudibleDelayTail(parameters);

        if (! audible)
        {
            if (delayWasAudible)
                delay.clear();

            delayWasAudible = false;
            return;
        }

        delay.process(buffer, mapDelayParameters(parameters));
        delayWasAudible = true;
    }

    void GlobalFxRack::processReverb(juce::AudioBuffer<float>& buffer,
                                     const ReverbParametersV2& parameters) noexcept
    {
        const bool audible = hasAudibleReverbTail(parameters);

        if (! audible)
        {
            if (reverbWasAudible)
                reverb.reset();

            reverbWasAudible = false;
            return;
        }

        reverbParameters.roomSize = clampUnit(parameters.size);
        reverbParameters.damping = clampUnit(parameters.damping);
        reverbParameters.wetLevel = clampUnit(parameters.mix);
        reverbParameters.dryLevel = 1.0f - reverbParameters.wetLevel;
        reverbParameters.width = 1.0f;
        reverbParameters.freezeMode = 0.0f;

        reverb.setEnabled(true);
        reverb.setParameters(reverbParameters);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        reverb.process(context);
        reverbWasAudible = true;
    }

    DelayParameters GlobalFxRack::mapDelayParameters(const DelayParametersV2& parameters) noexcept
    {
        DelayParameters mapped;
        mapped.timeMs = parameters.timeMs;
        mapped.feedback = parameters.feedback;
        mapped.mix = parameters.mix;
        return mapped;
    }
}
