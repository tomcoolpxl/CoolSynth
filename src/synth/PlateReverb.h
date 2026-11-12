#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace coolsynth::synth
{
    /**
     * Stereo plate reverb after Jon Dattorro, "Effect Design, Part 1:
     * Reverberator and Other Filters", JAES 45(9), 1997.
     *
     * The mono-summed input runs through a bandwidth filter and four all-pass
     * diffusers into a figure-of-eight tank of two cross-coupled halves, each
     * with a slowly modulated all-pass, a long delay, a damping low-pass and a
     * second all-pass. Left and right outputs are sums of taps spread through
     * both halves. Every delay length is scaled from the 29.761 kHz reference
     * rate used in the paper to the prepared sample rate. Output is fully wet.
     */
    class PlateReverb final
    {
    public:
        void prepare(double sampleRate)
        {
            currentSampleRate = std::max(1000.0, sampleRate);
            const double scale = currentSampleRate / kReferenceSampleRate;

            inputDiffuser1.resize(142, scale);
            inputDiffuser2.resize(107, scale);
            inputDiffuser3.resize(379, scale);
            inputDiffuser4.resize(277, scale);

            leftModulatedAllpass.resize(672 + kMaxExcursion, scale);
            leftDelay1.resize(4453, scale);
            leftAllpass2.resize(1800, scale);
            leftDelay2.resize(3720, scale);

            rightModulatedAllpass.resize(908 + kMaxExcursion, scale);
            rightDelay1.resize(4217, scale);
            rightAllpass2.resize(2656, scale);
            rightDelay2.resize(3163, scale);

            lengthScale = static_cast<float>(scale);
            leftModulatedLength = 672.0f * lengthScale;
            rightModulatedLength = 908.0f * lengthScale;
            excursion = static_cast<float>(kMaxExcursion) * lengthScale;
            modulationIncrement = static_cast<float>(kModulationRateHz / currentSampleRate);

            reset();
        }

        void reset() noexcept
        {
            for (auto* line : { &inputDiffuser1, &inputDiffuser2, &inputDiffuser3, &inputDiffuser4,
                                &leftModulatedAllpass, &leftDelay1, &leftAllpass2, &leftDelay2,
                                &rightModulatedAllpass, &rightDelay1, &rightAllpass2, &rightDelay2 })
                line->clear();

            bandwidthState = 0.0f;
            leftDampingState = 0.0f;
            rightDampingState = 0.0f;
            rightTankOut = 0.0f;
            modulationPhase = 0.0f;
        }

        /** 0..1: short room to long plate. */
        void setSize(float size) noexcept
        {
            decay = 0.25f + 0.70f * std::clamp(size, 0.0f, 1.0f);
        }

        /** 0..1: bright to dark high-frequency decay. */
        void setDamping(float dampingNormalized) noexcept
        {
            damping = 0.05f + 0.80f * std::clamp(dampingNormalized, 0.0f, 1.0f);
        }

        void process(float* left, float* right, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float input = 0.5f * (left[i] + right[i]);

                // Input bandwidth and diffusion.
                bandwidthState = kBandwidth * input + (1.0f - kBandwidth) * bandwidthState;
                float x = bandwidthState;
                x = inputDiffuser1.allpass(x, kInputDiffusion1);
                x = inputDiffuser2.allpass(x, kInputDiffusion1);
                x = inputDiffuser3.allpass(x, kInputDiffusion2);
                x = inputDiffuser4.allpass(x, kInputDiffusion2);

                // Tank modulation: two slow sines in quadrature.
                modulationPhase += modulationIncrement;
                if (modulationPhase >= 1.0f)
                    modulationPhase -= 1.0f;
                const float modA = std::sin(modulationPhase * kTwoPi);
                const float modB = std::cos(modulationPhase * kTwoPi);

                // Left half, fed by the output of the right half.
                float l = x + rightTankOut;
                l = leftModulatedAllpass.allpassModulated(l, kDecayDiffusion1, leftModulatedLength + excursion * modA);
                leftDelay1.write(l);
                l = leftDelay1.readEnd();
                leftDampingState = (1.0f - damping) * l + damping * leftDampingState;
                l = leftDampingState * decay;
                l = leftAllpass2.allpass(l, kDecayDiffusion2);
                leftDelay2.write(l);
                const float leftTankOut = leftDelay2.readEnd() * decay;

                // Right half, fed by the output of the left half.
                float r = x + leftTankOut;
                r = rightModulatedAllpass.allpassModulated(r, kDecayDiffusion1, rightModulatedLength + excursion * modB);
                rightDelay1.write(r);
                r = rightDelay1.readEnd();
                rightDampingState = (1.0f - damping) * r + damping * rightDampingState;
                r = rightDampingState * decay;
                r = rightAllpass2.allpass(r, kDecayDiffusion2);
                rightDelay2.write(r);
                rightTankOut = rightDelay2.readEnd() * decay;

                // Output taps from the table in the paper, scaled to the sample rate.
                const float outL = kTapGain * (rightDelay1.tap(266.0f * lengthScale)
                                               + rightDelay1.tap(2974.0f * lengthScale)
                                               - rightAllpass2.tap(1913.0f * lengthScale)
                                               + rightDelay2.tap(1996.0f * lengthScale)
                                               - leftDelay1.tap(1990.0f * lengthScale)
                                               - leftAllpass2.tap(187.0f * lengthScale)
                                               - leftDelay2.tap(1066.0f * lengthScale));

                const float outR = kTapGain * (leftDelay1.tap(353.0f * lengthScale)
                                               + leftDelay1.tap(3627.0f * lengthScale)
                                               - leftAllpass2.tap(1228.0f * lengthScale)
                                               + leftDelay2.tap(2673.0f * lengthScale)
                                               - rightDelay1.tap(2111.0f * lengthScale)
                                               - rightAllpass2.tap(335.0f * lengthScale)
                                               - rightDelay2.tap(121.0f * lengthScale));

                left[i] = outL;
                right[i] = outR;
            }
        }

    private:
        static constexpr double kReferenceSampleRate = 29761.0;
        static constexpr int kMaxExcursion = 16;
        static constexpr double kModulationRateHz = 0.9;
        static constexpr float kTwoPi = 6.28318530717958647692f;
        static constexpr float kBandwidth = 0.9995f;
        static constexpr float kInputDiffusion1 = 0.75f;
        static constexpr float kInputDiffusion2 = 0.625f;
        static constexpr float kDecayDiffusion1 = -0.70f;
        static constexpr float kDecayDiffusion2 = 0.50f;
        static constexpr float kTapGain = 0.6f;

        /** Circular delay with an integer end read, a fractional modulated read and arbitrary taps. */
        class Line final
        {
        public:
            void resize(int referenceLength, double scale)
            {
                length = std::max(4, static_cast<int>(std::lround(referenceLength * scale)));
                buffer.assign(static_cast<size_t>(length), 0.0f);
                writeIndex = 0;
            }

            void clear() noexcept
            {
                std::fill(buffer.begin(), buffer.end(), 0.0f);
                writeIndex = 0;
            }

            void write(float value) noexcept
            {
                buffer[static_cast<size_t>(writeIndex)] = value;
                writeIndex = (writeIndex + 1) % length;
            }

            /** The sample written a full line length ago. Read it before writing the current sample. */
            [[nodiscard]] float readEnd() const noexcept
            {
                return buffer[static_cast<size_t>(writeIndex)];
            }

            /** The sample written offset samples ago, offset clamped to 1..length-1. */
            [[nodiscard]] float tap(float offset) const noexcept
            {
                const int o = std::clamp(static_cast<int>(offset), 1, length - 1);
                int index = writeIndex - o;
                if (index < 0)
                    index += length;
                return buffer[static_cast<size_t>(index)];
            }

            [[nodiscard]] float tapFractional(float offset) const noexcept
            {
                const float clamped = std::clamp(offset, 1.0f, static_cast<float>(length - 2));
                const int whole = static_cast<int>(clamped);
                const float frac = clamped - static_cast<float>(whole);
                return tap(static_cast<float>(whole)) * (1.0f - frac) + tap(static_cast<float>(whole + 1)) * frac;
            }

            /** Schroeder all-pass over the full line length. */
            float allpass(float input, float gain) noexcept
            {
                const float delayed = readEnd();
                const float u = input + gain * delayed;
                write(u);
                return delayed - gain * u;
            }

            /** Schroeder all-pass whose delay is a fractional, modulated length. */
            float allpassModulated(float input, float gain, float modulatedLength) noexcept
            {
                const float delayed = tapFractional(modulatedLength);
                const float u = input + gain * delayed;
                write(u);
                return delayed - gain * u;
            }

        private:
            std::vector<float> buffer;
            int length = 4;
            int writeIndex = 0;
        };

        Line inputDiffuser1, inputDiffuser2, inputDiffuser3, inputDiffuser4;
        Line leftModulatedAllpass, leftDelay1, leftAllpass2, leftDelay2;
        Line rightModulatedAllpass, rightDelay1, rightAllpass2, rightDelay2;

        double currentSampleRate = 48000.0;
        float lengthScale = 1.0f;
        float leftModulatedLength = 672.0f;
        float rightModulatedLength = 908.0f;
        float excursion = 16.0f;
        float modulationPhase = 0.0f;
        float modulationIncrement = 0.0f;
        float bandwidthState = 0.0f;
        float leftDampingState = 0.0f;
        float rightDampingState = 0.0f;
        float rightTankOut = 0.0f;
        float decay = 0.5f;
        float damping = 0.4f;
    };
}
