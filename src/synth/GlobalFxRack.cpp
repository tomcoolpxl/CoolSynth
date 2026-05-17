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

        float applyDriveShape(float input, float preGain, float normalizer) noexcept
        {
            if (normalizer <= 0.0f)
                return input;
            return std::tanh(input * preGain) / normalizer;
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
        phaser.prepare(sampleRate, samplesPerBlock, outputChannelCount);
        compressor.prepare(sampleRate, samplesPerBlock, outputChannelCount);

        constexpr double kFxSmoothRampSeconds = 0.004;
        driveMixSmoothed.reset(sampleRate, kFxSmoothRampSeconds);
        chorusMixSmoothed.reset(sampleRate, kFxSmoothRampSeconds);
        reverbMixSmoothed.reset(sampleRate, kFxSmoothRampSeconds);

        dryBuffer.setSize(juce::jmax(1, outputChannelCount), samplesPerBlock, false, true, true);
        mixRampScratch.resize(static_cast<size_t>(samplesPerBlock));

        reset();
        prepared = true;
    }

    void GlobalFxRack::reset() noexcept
    {
        chorus.reset();
        delay.reset();
        reverb.reset();
        phaser.reset();
        compressor.reset();
        chorusWasAudible = false;
        delayWasAudible = false;
        reverbWasAudible = false;
    }

    void GlobalFxRack::clear() noexcept
    {
        chorus.reset();
        delay.clear();
        reverb.reset();
        phaser.reset();
        compressor.reset();
        chorusWasAudible = false;
        delayWasAudible = false;
        reverbWasAudible = false;
    }

    void GlobalFxRack::process(juce::AudioBuffer<float>& buffer,
                               const DriveParametersV2& driveParameters,
                               const PhaserParametersV2& phaserParameters,
                               const ChorusParametersV2& chorusParameters,
                               const DelayParametersV2& delayParameters,
                               const ReverbParametersV2& reverbParametersIn,
                               const CompressorParametersV2& compressorParameters) noexcept
    {
        if (! prepared)
            return;

        processDrive(buffer, driveParameters);
        phaser.process(buffer, phaserParameters);
        processChorus(buffer, chorusParameters);
        processDelay(buffer, delayParameters);
        processReverb(buffer, reverbParametersIn);
        compressor.process(buffer, compressorParameters);

        // Ultimate host-safety net: catch any stray NaNs or Infinities
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite(data[i]))
                    data[i] = 0.0f;
            }
        }
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
        {
            driveMixSmoothed.setCurrentAndTargetValue(0.0f);
            return;
        }

        driveMixSmoothed.setTargetValue(clampUnit(parameters.mix));
        if (driveMixSmoothed.getCurrentValue() <= 0.0f && ! driveMixSmoothed.isSmoothing())
            return;

        const float preGain = computeDrivePreGain(parameters.amount);
        const float normalizer = std::tanh(preGain);
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        // Build the per-sample mix ramp once; apply the same ramp to all channels
        for (int i = 0; i < numSamples; ++i)
            mixRampScratch[static_cast<size_t>(i)] = driveMixSmoothed.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = buffer.getWritePointer(channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const float dry = samples[sample];
                const float wet = applyDriveShape(dry, preGain, normalizer);
                samples[sample] = dry + ((wet - dry) * mixRampScratch[static_cast<size_t>(sample)]);
            }
        }
    }

    void GlobalFxRack::processChorus(juce::AudioBuffer<float>& buffer,
                                     const ChorusParametersV2& parameters) noexcept
    {
        // When explicitly disabled, snap immediately (no ramp) so the tail clears in one block
        if (! parameters.enabled || parameters.depth <= 0.0f)
        {
            if (chorusWasAudible)
                chorus.reset();
            chorusWasAudible = false;
            chorusMixSmoothed.setCurrentAndTargetValue(0.0f);
            return;
        }

        chorusMixSmoothed.setTargetValue(clampUnit(parameters.mix));

        const bool audible = chorusMixSmoothed.getCurrentValue() > 0.0f || chorusMixSmoothed.isSmoothing();
        if (! audible)
        {
            chorusWasAudible = false;
            return;
        }

        chorus.setRate(juce::jlimit(0.05f, 99.0f, parameters.rateHz));
        chorus.setDepth(clampUnit(parameters.depth));
        chorus.setCentreDelay(chorusCentreDelayMs);
        chorus.setFeedback(chorusFeedback);
        chorus.setMix(1.0f);  // C5: full-wet from JUCE; we blend with smoothed mix below

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples  = buffer.getNumSamples();

        // Save dry signal into pre-sized scratch buffer
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        chorus.process(context);

        // Build per-sample mix ramp once, apply to all channels
        for (int i = 0; i < numSamples; ++i)
            mixRampScratch[static_cast<size_t>(i)] = chorusMixSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] + (wet[i] - dry[i]) * mixRampScratch[static_cast<size_t>(i)];
        }

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

        delay.process(buffer, parameters);
        delayWasAudible = true;
    }

    void GlobalFxRack::processReverb(juce::AudioBuffer<float>& buffer,
                                     const ReverbParametersV2& parameters) noexcept
    {
        // When explicitly disabled, snap immediately (no ramp) so the tail clears in one block
        if (! parameters.enabled)
        {
            if (reverbWasAudible)
                reverb.reset();
            reverbWasAudible = false;
            reverbMixSmoothed.setCurrentAndTargetValue(0.0f);
            return;
        }

        reverbMixSmoothed.setTargetValue(clampUnit(parameters.mix));

        const bool audible = reverbMixSmoothed.getCurrentValue() > 0.0f || reverbMixSmoothed.isSmoothing();
        if (! audible)
        {
            reverbWasAudible = false;
            return;
        }

        reverbParameters.roomSize = clampUnit(parameters.size);
        reverbParameters.damping = clampUnit(parameters.damping);
        reverbParameters.wetLevel = 1.0f;  // C5: full-wet from JUCE; we blend with smoothed mix below
        reverbParameters.dryLevel = 0.0f;
        reverbParameters.width = 1.0f;
        reverbParameters.freezeMode = 0.0f;

        reverb.setEnabled(true);
        reverb.setParameters(reverbParameters);

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples  = buffer.getNumSamples();

        // Save dry signal into pre-sized scratch buffer, and soft-clip the input to the reverb
        for (int ch = 0; ch < numChannels; ++ch)
        {
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

            auto* wet = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = std::tanh(wet[i]);
        }

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        reverb.process(context);

        // Build per-sample mix ramp once, apply to all channels
        for (int i = 0; i < numSamples; ++i)
            mixRampScratch[static_cast<size_t>(i)] = reverbMixSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] + (wet[i] - dry[i]) * mixRampScratch[static_cast<size_t>(i)];
        }

        reverbWasAudible = true;
    }

}
