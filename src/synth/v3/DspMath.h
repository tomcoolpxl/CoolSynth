#pragma once

#include <cmath>

namespace coolsynth::synth::v3::dsp
{
    /**
        tanh(x) / x from the (7, 6) Lambert continued fraction: within 2e-5 of the true value,
        never divides by x, and is exactly 1 at x = 0. Beyond |x| = 4.9 tanh is 1 to within 1e-4,
        so the ratio becomes 1/|x|.
    */
    [[nodiscard]] inline float tanhOverX(float x) noexcept
    {
        const float x2 = x * x;
        if (x2 >= 24.0f)
            return 1.0f / std::sqrt(x2);
        const float numerator = 135135.0f + x2 * (17325.0f + x2 * (378.0f + x2));
        const float denominator = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
        return numerator / denominator;
    }

    [[nodiscard]] inline float fastTanh(float x) noexcept { return x * tanhOverX(x); }

    [[nodiscard]] inline float pitchToHz(float midiPitch) noexcept
    {
        return 440.0f * std::exp2((midiPitch - 69.0f) * (1.0f / 12.0f));
    }
}
