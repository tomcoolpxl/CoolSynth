#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace coolsynth::synth
{
    /**
     * Juno-style stereo chorus: one delay line fed with the mono sum, read by
     * two taps whose modulation runs in anti-phase, so the left and right wet
     * signals pitch-wobble against each other and the stereo image opens up.
     * Hermite interpolation keeps the modulated reads free of zipper noise.
     * Output is fully wet; the caller blends it with the dry signal.
     */
    class StereoChorus final
    {
    public:
        void prepare(double sampleRate)
        {
            currentSampleRate = std::max(1000.0, sampleRate);
            const int maxDelaySamples = static_cast<int>(std::ceil(kMaxDelayMs * 0.001 * currentSampleRate)) + 4;
            buffer.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
            centreSamples = static_cast<float>(kCentreDelayMs * 0.001 * currentSampleRate);
            depthSmoothingCoefficient = std::exp(-1.0f / static_cast<float>(0.02 * currentSampleRate));
            reset();
        }

        void reset() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
            lfoPhase = 0.0f;
            smoothedDepthSamples = 0.0f;
        }

        void setRateHz(float rateHz) noexcept
        {
            lfoIncrement = std::clamp(rateHz, 0.01f, 20.0f) / static_cast<float>(currentSampleRate);
        }

        /** 0..1 modulation depth, up to +/- kMaxDepthMs around the centre delay. */
        void setDepth(float depthNormalized) noexcept
        {
            targetDepthSamples = std::clamp(depthNormalized, 0.0f, 1.0f)
                                 * static_cast<float>(kMaxDepthMs * 0.001 * currentSampleRate);
        }

        void process(float* left, float* right, int numSamples) noexcept
        {
            const int length = static_cast<int>(buffer.size());
            if (length < 8)
                return;

            for (int i = 0; i < numSamples; ++i)
            {
                buffer[static_cast<size_t>(writeIndex)] = 0.5f * (left[i] + right[i]);

                smoothedDepthSamples += (targetDepthSamples - smoothedDepthSamples) * (1.0f - depthSmoothingCoefficient);

                lfoPhase += lfoIncrement;
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;

                const float lfo = triangle(lfoPhase);
                const float delayLeft = centreSamples + smoothedDepthSamples * lfo;
                const float delayRight = centreSamples - smoothedDepthSamples * lfo;

                left[i] = readHermite(delayLeft, length);
                right[i] = readHermite(delayRight, length);

                writeIndex = (writeIndex + 1) % length;
            }
        }

    private:
        static constexpr double kCentreDelayMs = 5.0;
        static constexpr double kMaxDepthMs = 2.2;
        static constexpr double kMaxDelayMs = kCentreDelayMs + kMaxDepthMs + 2.0;

        static float triangle(float phase) noexcept
        {
            return phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        }

        [[nodiscard]] float sampleAt(int offsetBack, int length) const noexcept
        {
            int index = writeIndex - offsetBack;
            while (index < 0)
                index += length;
            return buffer[static_cast<size_t>(index % length)];
        }

        [[nodiscard]] float readHermite(float delaySamples, int length) const noexcept
        {
            const float clamped = std::clamp(delaySamples, 2.0f, static_cast<float>(length - 3));
            const int whole = static_cast<int>(clamped);
            const float t = clamped - static_cast<float>(whole);

            const float xm1 = sampleAt(whole - 1, length);
            const float x0 = sampleAt(whole, length);
            const float x1 = sampleAt(whole + 1, length);
            const float x2 = sampleAt(whole + 2, length);

            const float c0 = x0;
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + c0;
        }

        std::vector<float> buffer;
        double currentSampleRate = 48000.0;
        int writeIndex = 0;
        float centreSamples = 0.0f;
        float lfoPhase = 0.0f;
        float lfoIncrement = 0.0f;
        float targetDepthSamples = 0.0f;
        float smoothedDepthSamples = 0.0f;
        float depthSmoothingCoefficient = 0.0f;
    };
}
