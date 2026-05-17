#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace coolsynth::synth
{
    inline constexpr int maxEuclideanSteps = 16;

    namespace detail
    {
        inline int positiveModulo(int value, int modulus) noexcept
        {
            if (modulus <= 0)
                return 0;

            const int result = value % modulus;
            return result < 0 ? result + modulus : result;
        }

        inline int buildBjorklundSequence(int level,
                                          const std::array<int, maxEuclideanSteps>& counts,
                                          const std::array<int, maxEuclideanSteps>& remainders,
                                          std::array<bool, maxEuclideanSteps>& pattern,
                                          int writeIndex) noexcept
        {
            if (level == -1)
            {
                pattern[static_cast<size_t>(writeIndex++)] = false;
                return writeIndex;
            }

            if (level == -2)
            {
                pattern[static_cast<size_t>(writeIndex++)] = true;
                return writeIndex;
            }

            for (int i = 0; i < counts[static_cast<size_t>(level)]; ++i)
                writeIndex = buildBjorklundSequence(level - 1, counts, remainders, pattern, writeIndex);

            if (remainders[static_cast<size_t>(level)] != 0)
                writeIndex = buildBjorklundSequence(level - 2, counts, remainders, pattern, writeIndex);

            return writeIndex;
        }
    }

    inline std::array<bool, maxEuclideanSteps> makeEuclideanCycle(int pulses,
                                                                  int steps,
                                                                  int rotation) noexcept
    {
        std::array<bool, maxEuclideanSteps> cycle {};

        const int clampedSteps = juce::jlimit(1, maxEuclideanSteps, steps);
        const int clampedPulses = juce::jlimit(0, clampedSteps, pulses);

        if (clampedPulses == 0)
            return cycle;

        if (clampedPulses >= clampedSteps)
        {
            for (int index = 0; index < clampedSteps; ++index)
                cycle[static_cast<size_t>(index)] = true;
            return cycle;
        }

        std::array<int, maxEuclideanSteps> counts {};
        std::array<int, maxEuclideanSteps> remainders {};

        remainders[0] = clampedPulses;
        int divisor = clampedSteps - clampedPulses;
        int level = 0;

        while (remainders[static_cast<size_t>(level)] > 1)
        {
            counts[static_cast<size_t>(level)] =
                divisor / remainders[static_cast<size_t>(level)];
            remainders[static_cast<size_t>(level + 1)] =
                divisor % remainders[static_cast<size_t>(level)];
            divisor = remainders[static_cast<size_t>(level)];
            ++level;
        }

        counts[static_cast<size_t>(level)] = divisor;

        std::array<bool, maxEuclideanSteps> rawPattern {};
        const int generatedLength =
            detail::buildBjorklundSequence(level, counts, remainders, rawPattern, 0);

        const int rotationOffset = detail::positiveModulo(rotation, clampedSteps);
        const int firstPulseIndex =
            generatedLength > 0
                ? [&rawPattern, generatedLength]()
                {
                    for (int index = 0; index < generatedLength; ++index)
                    {
                        if (rawPattern[static_cast<size_t>(index)])
                            return index;
                    }
                    return 0;
                }()
                : 0;

        for (int index = 0; index < clampedSteps; ++index)
        {
            const int sourceIndex =
                (index + firstPulseIndex + rotationOffset) % clampedSteps;
            cycle[static_cast<size_t>(index)] = rawPattern[static_cast<size_t>(sourceIndex)];
        }

        return cycle;
    }
}
