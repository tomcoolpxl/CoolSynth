#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 crusher and wavefolder processors (DESIGN_V3.md section 3.2; REQUIREMENTS_V3.md: crusher
    with bit depth 1 to 16 continuous, sample-rate reduction to 1 kHz, dry/wet; wavefolder with
    symmetry and pre-gain).

    Crusher: Rate holds the signal for 1 / Rate seconds (at the top of the knob it does nothing),
    then Bits rounds it to 2^bits levels, the bit count continuous so a sweep moves smoothly
    through the steps. The aliasing and the steps are the sound; Mix blends it with the clean
    signal.

    Folder: Drive (0 to 30 dB) and Symmetry (an offset) push the signal into a sine folder,
    sin(pi/2 u), which folds back every time the signal passes full scale, so harmonics grow
    with the drive. The fold runs with first-order antiderivative anti-aliasing (Parker,
    Zavalishin and Le Bivic, DAFx 2016): the output is the mean of the folder over the segment
    between two samples, which takes most of the folded-back aliasing away for half a sample of
    delay. A DC blocker removes the offset Symmetry leaves. Mix blends with the clean signal.
*/
namespace coolsynth::synth::v3
{
    class CrusherProcessor final
    {
    public:
        enum Param : int { bits = 0, rate, mix, paramCount };

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Crusher", 1,
                {{ { "bits", "Bits", 1.0f,    16.0f,    8.0f,     Skew::linear,      Unit::generic, true },
                   { "rate", "Rate", 1000.0f, 48000.0f, 48000.0f, Skew::logarithmic, Unit::hertz,   true },
                   { "mix",  "Mix",  0.0f,    1.0f,     1.0f,     Skew::linear,      Unit::percent, true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            reset();
        }

        void reset() noexcept
        {
            holdPhase = 1.0;
            heldLeft = heldRight = 0.0f;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const float levels = std::exp2(juce::jlimit(1.0f, 16.0f, params.p[bits]));
            const float step = 2.0f / levels;
            const float inverseStep = 1.0f / step;
            const double holdStep = juce::jlimit(1000.0, sampleRate, static_cast<double>(params.p[rate])) / sampleRate;
            const bool holding = params.p[rate] < 47900.0f; // the top of the knob is no reduction
            const float wet = juce::jlimit(0.0f, 1.0f, params.p[mix]);

            for (int i = 0; i < numSamples; ++i)
            {
                if (holding)
                {
                    holdPhase += holdStep;
                    if (holdPhase >= 1.0)
                    {
                        holdPhase -= std::floor(holdPhase);
                        heldLeft = left[i];
                        heldRight = right[i];
                    }
                }
                else
                {
                    heldLeft = left[i];
                    heldRight = right[i];
                }
                const float crushedLeft = std::round(heldLeft * inverseStep) * step;
                const float crushedRight = std::round(heldRight * inverseStep) * step;
                left[i] = wet >= 1.0f ? crushedLeft : left[i] + (crushedLeft - left[i]) * wet;
                right[i] = wet >= 1.0f ? crushedRight : right[i] + (crushedRight - right[i]) * wet;
            }
        }

    private:
        double sampleRate = 96000.0;
        double holdPhase = 1.0;
        float heldLeft = 0.0f, heldRight = 0.0f;
    };

    class FolderProcessor final
    {
    public:
        enum Param : int { drive = 0, symmetry, mix, paramCount };

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Folder", 1,
                {{ { "drive", "Drive",    0.0f,  30.0f, 6.0f, Skew::linear, Unit::decibels, true },
                   { "sym",   "Symmetry", -1.0f, 1.0f,  0.0f, Skew::linear, Unit::generic,  true },
                   { "mix",   "Mix",      0.0f,  1.0f,  1.0f, Skew::linear, Unit::percent,  true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            dcCoefficient = static_cast<float>(1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / sampleRate);
            reset();
        }

        void reset() noexcept
        {
            left = {};
            right = {};
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* leftSamples, float* rightSamples, int numSamples, const SlotParams& params) noexcept
        {
            const float targetGain = std::pow(10.0f, juce::jlimit(0.0f, 30.0f, params.p[drive]) * 0.05f);
            const float targetBias = juce::jlimit(-1.0f, 1.0f, params.p[symmetry]);
            const float wet = juce::jlimit(0.0f, 1.0f, params.p[mix]);
            if (! primed)
            {
                gain = targetGain;
                bias = targetBias;
                primed = true;
            }
            const float inverse = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float gainStep = (targetGain - gain) * inverse;
            const float biasStep = (targetBias - bias) * inverse;

            for (int i = 0; i < numSamples; ++i)
            {
                gain += gainStep;
                bias += biasStep;
                leftSamples[i] = foldChannel(left, leftSamples[i], wet);
                rightSamples[i] = foldChannel(right, rightSamples[i], wet);
            }
        }

        /** The folding curve, and its antiderivative. */
        static double fold(double u) noexcept { return std::sin(kHalfPi * u); }
        static double foldIntegral(double u) noexcept { return -std::cos(kHalfPi * u) / kHalfPi; }

    private:
        static constexpr double kHalfPi = 1.57079632679489661923;

        struct Channel
        {
            double previousU = 0.0;
            float previousDry = 0.0f;
            float dcIn = 0.0f, dcOut = 0.0f;
        };

        float foldChannel(Channel& c, float x, float wet) noexcept
        {
            const double u = static_cast<double>(gain) * x + bias;
            const double du = u - c.previousU;
            const double y = std::abs(du) > 1.0e-5 ? (foldIntegral(u) - foldIntegral(c.previousU)) / du
                                                   : fold(0.5 * (u + c.previousU));
            c.previousU = u;

            // The averaged fold sits half a sample late; so does the dry signal it is mixed with.
            const float dry = 0.5f * (x + c.previousDry);
            c.previousDry = x;

            const float folded = static_cast<float>(y);
            const float blocked = folded - c.dcIn + dcCoefficient * c.dcOut;
            c.dcIn = folded;
            c.dcOut = blocked;
            return dry + (blocked - dry) * wet;
        }

        double sampleRate = 96000.0;
        float dcCoefficient = 0.9993f;
        float gain = 1.0f, bias = 0.0f;
        bool primed = false;
        Channel left, right;
    };

    static_assert(Module<CrusherProcessor>);
    static_assert(Module<FolderProcessor>);
}
