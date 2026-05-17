#include "Phaser.h"

#include <cmath>

namespace coolsynth::synth
{
    namespace
    {
        constexpr double kPhaserSmoothRampSeconds = 0.004;

        float channelStartPhase(int channelIndex) noexcept
        {
            // Stereo widening: right channel LFO is a quarter cycle ahead of the left.
            return (channelIndex == 1) ? 0.25f : 0.0f;
        }
    }

    void Phaser::prepare(double newSampleRate, int /*samplesPerBlock*/, int outputChannelCount)
    {
        sampleRate = newSampleRate;
        channels.assign(static_cast<size_t>(juce::jmax(1, outputChannelCount)), ChannelState {});
        wetMixSmoothed.reset(newSampleRate, kPhaserSmoothRampSeconds);
        reset();
        prepared = true;
    }

    void Phaser::reset() noexcept
    {
        for (size_t i = 0; i < channels.size(); ++i)
        {
            auto& cs = channels[i];
            for (auto& stage : cs.stages)
            {
                stage.x1 = 0.0f;
                stage.y1 = 0.0f;
            }
            cs.feedback = 0.0f;
            cs.lfoPhase = channelStartPhase(static_cast<int>(i));
        }
        wetMixSmoothed.setCurrentAndTargetValue(0.0f);
        wasAudible = false;
    }

    void Phaser::process(juce::AudioBuffer<float>& buffer, const PhaserParametersV2& parameters) noexcept
    {
        if (! prepared)
            return;

        const float depth = juce::jlimit(0.0f, 1.0f, parameters.depth);

        if (! parameters.enabled || depth <= 0.0f)
        {
            if (wasAudible)
                reset();
            wasAudible = false;
            wetMixSmoothed.setCurrentAndTargetValue(0.0f);
            return;
        }

        const float targetWetMix = depth * maxWetMix;
        wetMixSmoothed.setTargetValue(targetWetMix);

        const float rateHz = juce::jlimit(0.05f, 8.0f, parameters.rateHz);
        const float lfoIncrement = rateHz / static_cast<float>(sampleRate);

        const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels.size()));
        const int numSamples = buffer.getNumSamples();

        const float logMin = std::log(notchMinHz);
        const float logMax = std::log(notchMaxHz);
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float pi = juce::MathConstants<float>::pi;
        const float invSampleRate = 1.0f / static_cast<float>(sampleRate);

        // Snapshot the smoother once per block-pass per channel; both channels see the
        // same per-sample ramp because the smoother is shared.
        std::array<float, 2048> mixRampScratch {};
        const int ramp = juce::jmin(numSamples, static_cast<int>(mixRampScratch.size()));
        for (int i = 0; i < ramp; ++i)
            mixRampScratch[static_cast<size_t>(i)] = wetMixSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& cs = channels[static_cast<size_t>(ch)];
            auto* data = buffer.getWritePointer(ch);
            float phase = cs.lfoPhase;
            float feedback = cs.feedback;

            for (int i = 0; i < numSamples; ++i)
            {
                const float lfo = std::sin(twoPi * phase);

                // LFO drives the notch frequency on a log scale, scaled by depth so a
                // small depth produces a tight sweep rather than a full top-to-bottom one.
                const float logF = logMin + (logMax - logMin) * (0.5f + 0.5f * lfo * depth);
                const float f0 = std::exp(logF);
                const float w = std::tan(pi * f0 * invSampleRate);
                const float d = (1.0f - w) / (1.0f + w);

                const float dry = data[i];
                float x = dry + feedbackAmount * feedback;

                for (auto& stage : cs.stages)
                {
                    const float y = -d * x + stage.x1 + d * stage.y1;
                    stage.x1 = x;
                    stage.y1 = y;
                    x = y;
                }

                feedback = x;

                const float mix = mixRampScratch[static_cast<size_t>(juce::jmin(i, ramp - 1))];
                data[i] = dry + (x - dry) * mix;

                phase += lfoIncrement;
                if (phase >= 1.0f)
                    phase -= 1.0f;
            }

            cs.lfoPhase = phase;
            cs.feedback = feedback;
        }

        wasAudible = true;
    }
}
