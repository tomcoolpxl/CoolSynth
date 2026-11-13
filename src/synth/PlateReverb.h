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
     *
     * Around the paper's design: a pre-delay before the bandwidth filter, a
     * one-pole high-pass on the input (Low Cut), a trim on the tank's
     * modulation depth, and Shimmer: the tank's output, band-limited, shifted
     * an octave up by a two-head delay-line shifter and soft-limited, fed back
     * into the tank's input. Inside the loop every pass climbs another octave,
     * as on Eventide's shimmer; the tank's own decay keeps it from running away.
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

            leftModulatedAllpass.resize(672 + 2 * kMaxExcursion, scale);
            leftDelay1.resize(4453, scale);
            leftAllpass2.resize(1800, scale);
            leftDelay2.resize(3720, scale);

            rightModulatedAllpass.resize(908 + 2 * kMaxExcursion, scale);
            rightDelay1.resize(4217, scale);
            rightAllpass2.resize(2656, scale);
            rightDelay2.resize(3163, scale);

            lengthScale = static_cast<float>(scale);
            leftModulatedLength = 672.0f * lengthScale;
            rightModulatedLength = 908.0f * lengthScale;
            maxExcursion = static_cast<float>(kMaxExcursion) * lengthScale;
            excursion = maxExcursion * 2.0f * modulation;
            modulationIncrement = static_cast<float>(kModulationRateHz / currentSampleRate);

            preDelayLine.assign(static_cast<size_t>(std::ceil(kMaxPreDelaySeconds * currentSampleRate)) + 4, 0.0f);
            shifterLength = std::max(64, static_cast<int>(std::lround(kShifterWindowSeconds * currentSampleRate)));
            shifterLine.assign(static_cast<size_t>(shifterLength) + 4, 0.0f);
            const auto onePole = [this](double hz) { return static_cast<float>(1.0 - std::exp(-2.0 * kPiDouble * hz / currentSampleRate)); };
            shimmerLowPassCoefficient = onePole(kShimmerLowPassHz);
            shimmerHighPassCoefficient = onePole(kShimmerHighPassHz);
            setLowCut(lowCutHz);
            setPreDelay(preDelayMs);

            reset();
        }

        /** 0 .. 250 ms before the reverb starts. */
        void setPreDelay(float milliseconds) noexcept
        {
            preDelayMs = std::clamp(milliseconds, 0.0f, static_cast<float>(kMaxPreDelaySeconds * 1000.0));
            preDelaySamples = std::min(static_cast<int>(preDelayLine.size()) - 2,
                                       static_cast<int>(std::lround(preDelayMs * 0.001 * currentSampleRate)));
        }

        /** Input high-pass, 20 Hz (open) to 1 kHz. */
        void setLowCut(float hz) noexcept
        {
            lowCutHz = std::clamp(hz, 20.0f, 1000.0f);
            lowCutCoefficient = static_cast<float>(std::exp(-2.0 * kPiDouble * lowCutHz / currentSampleRate));
        }

        /** Tank modulation depth 0..1; 0.5 is the paper's excursion. */
        void setModulation(float amount) noexcept
        {
            modulation = std::clamp(amount, 0.0f, 1.0f);
            excursion = maxExcursion * 2.0f * modulation;
        }

        /** 0 (a plain plate) .. 1: how much octave-up signal returns into the tank. */
        void setShimmer(float amount) noexcept { shimmer = std::clamp(amount, 0.0f, 1.0f); }

        void reset() noexcept
        {
            for (auto* line : { &inputDiffuser1, &inputDiffuser2, &inputDiffuser3, &inputDiffuser4,
                                &leftModulatedAllpass, &leftDelay1, &leftAllpass2, &leftDelay2,
                                &rightModulatedAllpass, &rightDelay1, &rightAllpass2, &rightDelay2 })
                line->clear();

            std::fill(preDelayLine.begin(), preDelayLine.end(), 0.0f);
            std::fill(shifterLine.begin(), shifterLine.end(), 0.0f);
            preDelayWrite = shifterWrite = 0;
            shifterPhase = 0.0f;
            shimmerFeed = shimmerLowA = shimmerLowB = shimmerHighState = 0.0f;
            lowCutState = lowCutPrevious = 0.0f;
            bandwidthState = 0.0f;
            leftDampingState = 0.0f;
            rightDampingState = 0.0f;
            rightTankOut = 0.0f;
            modulationPhase = 0.0f;
        }

        /** 0..1: short room to long plate. Decay is applied once per tank half, as in the paper. */
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
                float input = 0.5f * (left[i] + right[i]);

                // Pre-delay, then the input high-pass.
                if (preDelaySamples > 0)
                {
                    const int size = static_cast<int>(preDelayLine.size());
                    preDelayLine[static_cast<size_t>(preDelayWrite)] = input;
                    int read = preDelayWrite - preDelaySamples;
                    if (read < 0)
                        read += size;
                    input = preDelayLine[static_cast<size_t>(read)];
                    preDelayWrite = (preDelayWrite + 1) % size;
                }
                if (lowCutHz > 20.5f)
                {
                    lowCutState = lowCutCoefficient * (lowCutState + input - lowCutPrevious);
                    lowCutPrevious = input;
                    input = lowCutState;
                }
                input += shimmerFeed;

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
                const float leftTankOut = leftDelay2.readEnd();

                // Right half, fed by the output of the left half.
                float r = x + leftTankOut;
                r = rightModulatedAllpass.allpassModulated(r, kDecayDiffusion1, rightModulatedLength + excursion * modB);
                rightDelay1.write(r);
                r = rightDelay1.readEnd();
                rightDampingState = (1.0f - damping) * r + damping * rightDampingState;
                r = rightDampingState * decay;
                r = rightAllpass2.allpass(r, kDecayDiffusion2);
                rightDelay2.write(r);
                rightTankOut = rightDelay2.readEnd();

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

                shimmerFeed = shimmer > 0.0f ? shimmer * kShimmerGain * shiftUp(0.5f * (outL + outR)) : 0.0f;
            }
        }

    private:
        /**
            One sample through the octave-up shifter: band-limited first (so the doubled signal stays
            below Nyquist and the low end does not pile up), then two heads reading a short window at
            twice the speed, each faded out (sin^2) where it wraps, then soft-limited.
        */
        float shiftUp(float x) noexcept
        {
            shimmerLowA += (x - shimmerLowA) * shimmerLowPassCoefficient;
            shimmerLowB += (shimmerLowA - shimmerLowB) * shimmerLowPassCoefficient;
            shimmerHighState += (shimmerLowB - shimmerHighState) * shimmerHighPassCoefficient;
            const float band = shimmerLowB - shimmerHighState;

            const int size = static_cast<int>(shifterLine.size());
            shifterLine[static_cast<size_t>(shifterWrite)] = band;

            // Ratio 2: the read heads gain one sample on the write head every sample.
            shifterPhase += 1.0f / static_cast<float>(shifterLength);
            if (shifterPhase >= 1.0f)
                shifterPhase -= 1.0f;
            auto head = [&](float phase)
            {
                const float delay = 1.0f + (1.0f - phase) * static_cast<float>(shifterLength - 2);
                float position = static_cast<float>(shifterWrite) - delay;
                if (position < 0.0f)
                    position += static_cast<float>(size);
                const int whole = static_cast<int>(position);
                const float frac = position - static_cast<float>(whole);
                const float a = shifterLine[static_cast<size_t>(whole % size)];
                const float b = shifterLine[static_cast<size_t>((whole + 1) % size)];
                const float s = std::sin(kTwoPi * 0.5f * phase);
                return (a + (b - a) * frac) * s * s;
            };
            float phaseB = shifterPhase + 0.5f;
            if (phaseB >= 1.0f)
                phaseB -= 1.0f;
            const float shifted = head(shifterPhase) + head(phaseB);
            shifterWrite = (shifterWrite + 1) % size;
            return std::tanh(shifted);
        }

        static constexpr double kPiDouble = 3.14159265358979323846;
        static constexpr double kMaxPreDelaySeconds = 0.25;
        static constexpr double kShifterWindowSeconds = 0.045;
        static constexpr double kShimmerLowPassHz = 5000.0;
        static constexpr double kShimmerHighPassHz = 180.0;
        static constexpr float kShimmerGain = 0.7f;
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
        float maxExcursion = 16.0f;
        float modulation = 0.5f;

        std::vector<float> preDelayLine, shifterLine;
        int preDelayWrite = 0, preDelaySamples = 0, shifterWrite = 0, shifterLength = 2048;
        float preDelayMs = 0.0f, lowCutHz = 20.0f, lowCutCoefficient = 1.0f, lowCutState = 0.0f, lowCutPrevious = 0.0f;
        float shimmer = 0.0f, shimmerFeed = 0.0f, shifterPhase = 0.0f;
        float shimmerLowPassCoefficient = 0.5f, shimmerHighPassCoefficient = 0.02f;
        float shimmerLowA = 0.0f, shimmerLowB = 0.0f, shimmerHighState = 0.0f;
    };
}
