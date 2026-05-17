#include "Compressor.h"

#include <cmath>

namespace coolsynth::synth
{
    namespace
    {
        constexpr double kCompressorSmoothRampSeconds = 0.020;
        constexpr float kMinusInfDb = -120.0f;

        float linearToDb(float x) noexcept
        {
            return x > 1.0e-6f ? 20.0f * std::log10(x) : kMinusInfDb;
        }

        float dbToLinear(float db) noexcept
        {
            return std::pow(10.0f, db * 0.05f);
        }

        // Soft-knee gain reduction in dB. envDb is the detector level (dB),
        // thresholdDb the knee centre, kneeDb the total knee width, ratio the
        // compression ratio. Returns a non-negative number of dB to subtract.
        float computeGainReductionDb(float envDb, float thresholdDb, float kneeDb, float ratio) noexcept
        {
            const float overshoot = envDb - thresholdDb;
            const float halfKnee = kneeDb * 0.5f;
            const float compFactor = 1.0f - 1.0f / juce::jmax(1.0f, ratio);

            if (overshoot <= -halfKnee)
                return 0.0f;

            if (overshoot >= halfKnee)
                return overshoot * compFactor;

            // Inside the knee: quadratic interpolation
            const float t = overshoot + halfKnee;  // 0..kneeDb
            return (t * t) / (2.0f * juce::jmax(0.0001f, kneeDb)) * compFactor;
        }
    }

    void Compressor::prepare(double newSampleRate, int /*samplesPerBlock*/, int /*outputChannelCount*/)
    {
        sampleRate = newSampleRate;
        attackCoef = std::exp(-1.0f / static_cast<float>(newSampleRate * attackSeconds));
        releaseCoef = std::exp(-1.0f / static_cast<float>(newSampleRate * releaseSeconds));
        mixSmoothed.reset(newSampleRate, kCompressorSmoothRampSeconds);
        amountSmoothed.reset(newSampleRate, kCompressorSmoothRampSeconds);
        reset();
        prepared = true;
    }

    void Compressor::reset() noexcept
    {
        gainReductionDb = 0.0f;
        mixSmoothed.setCurrentAndTargetValue(0.0f);
        amountSmoothed.setCurrentAndTargetValue(0.0f);
    }

    void Compressor::process(juce::AudioBuffer<float>& buffer,
                             const CompressorParametersV2& parameters) noexcept
    {
        if (! prepared)
            return;

        const float mix = juce::jlimit(0.0f, 1.0f, parameters.mix);
        const float amount = juce::jlimit(0.0f, 1.0f, parameters.amount);

        if (! parameters.enabled || mix <= 0.0f)
        {
            mixSmoothed.setCurrentAndTargetValue(0.0f);
            amountSmoothed.setCurrentAndTargetValue(amount);
            gainReductionDb = 0.0f;
            return;
        }

        mixSmoothed.setTargetValue(mix);
        amountSmoothed.setTargetValue(amount);

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels <= 0 || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float amt = amountSmoothed.getNextValue();
            const float mixNow = mixSmoothed.getNextValue();

            // amount=0 -> threshold=0dB, ratio=1 (no compression); amount=1 -> -24dB / 6:1
            const float thresholdDb = amt * minThresholdDb;
            const float ratio = 1.0f + amt * (maxRatio - 1.0f);

            // Stereo-linked instantaneous peak detector: take max(|sample|) across channels
            float detectorPeak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                detectorPeak = juce::jmax(detectorPeak, std::abs(buffer.getSample(ch, i)));

            const float inDb = linearToDb(detectorPeak);
            const float targetGrDb = computeGainReductionDb(inDb, thresholdDb, kneeWidthDb, ratio);

            // Smooth the gain reduction itself with attack (faster on rising GR) / release
            // (slower on falling GR). This catches sharp peaks without the envelope-follower
            // lag that would otherwise let transients slip through.
            const float coef = (targetGrDb > gainReductionDb) ? attackCoef : releaseCoef;
            gainReductionDb = coef * gainReductionDb + (1.0f - coef) * targetGrDb;

            // Conservative makeup: capped at +6 dB even at amount=1.0 to avoid amplifying
            // the dry signal louder than the bypass case for source material whose envelope
            // doesn't sit near the threshold.
            const float makeupDb = amt * 6.0f;
            const float gainLinear = dbToLinear(-gainReductionDb + makeupDb);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                const float dry = data[i];
                const float wet = dry * gainLinear;
                data[i] = dry + (wet - dry) * mixNow;
            }
        }
    }
}
