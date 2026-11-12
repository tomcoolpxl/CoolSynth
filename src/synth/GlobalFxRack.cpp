#include "GlobalFxRack.h"

#include <cmath>

namespace coolsynth::synth
{
    namespace
    {
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
        chorus.prepare(sampleRate);
        reverb.prepare(sampleRate);
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

        // prepareToPlay's block size is only a hint: a host may deliver a larger block.
        // The per-sample scratch buffers are sized to that hint,
        // so split oversized blocks into chunks that fit instead of indexing past them.
        const int chunkCapacity = static_cast<int>(mixRampScratch.size());
        const int totalSamples = buffer.getNumSamples();

        if (chunkCapacity <= 0)
            return;

        if (totalSamples <= chunkCapacity)
        {
            processChunk(buffer, driveParameters, phaserParameters, chorusParameters,
                         delayParameters, reverbParametersIn, compressorParameters);
            return;
        }

        for (int start = 0; start < totalSamples; start += chunkCapacity)
        {
            const int chunkSamples = juce::jmin(chunkCapacity, totalSamples - start);
            juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(),
                                           start,
                                           chunkSamples);
            processChunk(chunk, driveParameters, phaserParameters, chorusParameters,
                         delayParameters, reverbParametersIn, compressorParameters);
        }
    }

    void GlobalFxRack::processChunk(juce::AudioBuffer<float>& buffer,
                                    const DriveParametersV2& driveParameters,
                                    const PhaserParametersV2& phaserParameters,
                                    const ChorusParametersV2& chorusParameters,
                                    const DelayParametersV2& delayParameters,
                                    const ReverbParametersV2& reverbParametersIn,
                                    const CompressorParametersV2& compressorParameters) noexcept
    {
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
            const double delaySeconds = static_cast<double>(juce::jlimit(1.0f, 1000.0f, delayParameters.timeMs)) / 1000.0;
            const double feedback = static_cast<double>(juce::jlimit(0.0f, GlobalDelay::maxFeedback, delayParameters.feedback));

            if (feedback <= 0.0)
            {
                tailSeconds = juce::jlimit(0.0, 2.0, delaySeconds + 0.25);
            }
            else
            {
                // Repeats until the feedback path has decayed by 60 dB, plus the one-shot pad.
                // The constant is only the ceiling reached at the maximum feedback setting.
                const double repeatsToSilence = std::log(0.001) / std::log(feedback);
                tailSeconds = juce::jmin(conservativeDelayFeedbackTailSeconds,
                                         delaySeconds * repeatsToSilence + 0.25);
            }
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

        chorus.setRateHz(parameters.rateHz);
        chorus.setDepth(clampUnit(parameters.depth));

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples  = buffer.getNumSamples();

        // Save dry signal into pre-sized scratch buffer
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        {
            auto* left = buffer.getWritePointer(0);
            auto* right = numChannels > 1 ? buffer.getWritePointer(1) : left;
            chorus.process(left, right, numSamples);  // full wet; blended with the smoothed mix below
        }

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

        reverb.setSize(clampUnit(parameters.size));
        reverb.setDamping(clampUnit(parameters.damping));

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

        {
            auto* left = buffer.getWritePointer(0);
            auto* right = numChannels > 1 ? buffer.getWritePointer(1) : left;
            reverb.process(left, right, numSamples);  // full wet; blended with the smoothed mix below
        }

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
