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

    /** Integral of the cubic B-spline from -2 to t, for t in (-2, 0]. */
    [[nodiscard]] inline float splineIntegral(float t) noexcept
    {
        if (t <= -1.0f)
        {
            const float u = t + 2.0f;
            return u * u * u * u * (1.0f / 24.0f);
        }
        return 1.0f / 24.0f + (2.75f + t * (4.0f + t * t * (-2.0f - 0.75f * t))) * (1.0f / 6.0f);
    }

    /**
        The 4-point band-limited unit step (integrated cubic B-spline, Valimaki, Pekonen and Nam,
        JASA 2012) minus the ideal step, x samples from the step; zero outside (-2, 2). Adding
        jump * blepResidual over the two samples before and after a step makes it band-limited.
    */
    [[nodiscard]] inline float blepResidual(float x) noexcept
    {
        if (x <= -2.0f || x >= 2.0f)
            return 0.0f;
        return x < 0.0f ? splineIntegral(x) : -splineIntegral(-x);
    }
}
