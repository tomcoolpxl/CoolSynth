#pragma once

#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace coolsynth::synth
{
    /**
     * Polyphase IIR half-band filters for 2x oversampling, one channel each.
     *
     * Two parallel chains of first-order all-pass sections, fed with the even
     * and odd samples, sum to an elliptic half-band low-pass. The all-pass
     * coefficients come from an elliptic design (Valenzuela/Constantinides),
     * computed once at first use; JUCE's FilterDesign is used only for that
     * arithmetic. Group delay is a few samples at the lower rate and the phase
     * is not linear, neither of which is audible on a synth voice.
     */
    namespace halfband2x
    {
        /** Stop band about -96 dB with a transition band of 6 % of the lower rate. */
        inline constexpr float kTransitionWidth = 0.06f;
        inline constexpr float kStopbandDb = -96.0f;

        struct Design
        {
            std::vector<float> directPath;   // the D(z^2) chain
            std::vector<float> delayedPath;  // the E(z^2) chain that sits behind the unit delay
        };

        inline const Design& design()
        {
            static const Design cached = []
            {
                const auto structure = juce::dsp::FilterDesign<float>::designIIRLowpassHalfBandPolyphaseAllpassMethod(
                    kTransitionWidth, kStopbandDb);

                Design d;
                for (int i = 0; i < structure.directPath.size(); ++i)
                    d.directPath.push_back(structure.directPath.getObjectPointer(i)->coefficients[0]);

                // Entry 0 of the delayed path is the unit delay itself, realised here by the
                // sample pairing in the decimator and the output ordering in the upsampler.
                for (int i = 1; i < structure.delayedPath.size(); ++i)
                    d.delayedPath.push_back(structure.delayedPath.getObjectPointer(i)->coefficients[0]);
                return d;
            }();
            return cached;
        }

        /** Chain of first-order all-pass sections: y = a * x + v; v = x - a * y. */
        class Branch
        {
        public:
            explicit Branch(const std::vector<float>& coefficients) : alphas(coefficients), states(coefficients.size(), 0.0f) {}

            float process(float input) noexcept
            {
                float x = input;
                for (size_t i = 0; i < alphas.size(); ++i)
                {
                    const float output = alphas[i] * x + states[i];
                    states[i] = x - alphas[i] * output;
                    x = output;
                }
                return x;
            }

            void reset() noexcept
            {
                for (auto& s : states)
                    s = 0.0f;
            }

        private:
            std::vector<float> alphas;
            std::vector<float> states;
        };
    }

    /** Takes pairs of samples at 2x and returns one sample at 1x. */
    class HalfBandDecimator2x final
    {
    public:
        HalfBandDecimator2x()
            : directBranch(halfband2x::design().directPath),
              delayedBranch(halfband2x::design().delayedPath)
        {
        }

        void reset() noexcept
        {
            directBranch.reset();
            delayedBranch.reset();
        }

        /**
         * `first` is the earlier of the two input samples, `second` the later one.
         * The half-band is H(z) = D(z^2) + z^-1 E(z^2): at the output instant the direct
         * path D sees the later sample and the delayed path E the one before it.
         */
        float process(float first, float second) noexcept
        {
            return 0.5f * (directBranch.process(second) + delayedBranch.process(first));
        }

        /** Decimates `numOutputSamples * 2` input samples into `numOutputSamples` outputs. In place is allowed. */
        void processBlock(const float* input, float* output, int numOutputSamples) noexcept
        {
            for (int i = 0; i < numOutputSamples; ++i)
                output[i] = process(input[2 * i], input[2 * i + 1]);
        }

    private:
        halfband2x::Branch directBranch;
        halfband2x::Branch delayedBranch;
    };

    /** Takes one sample at 1x and returns two samples at 2x. */
    class HalfBandUpsampler2x final
    {
    public:
        HalfBandUpsampler2x()
            : directBranch(halfband2x::design().directPath),
              delayedBranch(halfband2x::design().delayedPath)
        {
        }

        void reset() noexcept
        {
            directBranch.reset();
            delayedBranch.reset();
        }

        void process(float input, float& first, float& second) noexcept
        {
            first = directBranch.process(input);
            second = delayedBranch.process(input);
        }

    private:
        halfband2x::Branch directBranch;
        halfband2x::Branch delayedBranch;
    };
}
