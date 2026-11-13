#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/*
    The read between a recording's frames that the sampled instruments and the granular effect
    share (decision D98): a 16-point sinc under a Kaiser window, in 256 phases with the
    weights interpolated between them, at three cutoffs. A read that skips frames (a recording
    played above its pitch, a grain pitched up) takes a narrower kernel, so what it would fold
    back is taken out first.
*/
namespace coolsynth::synth::v3
{
    /** Windowed-sinc kernels for reading a recording between its frames: three cutoffs, 256 phases, 16 points. */
    class SincKernels final
    {
    public:
        static constexpr int points = 16;
        static constexpr int phases = 256;
        static constexpr int cutoffs = 3; // 1, 1/2 and 1/4 of the recording's own band

        static const SincKernels& instance()
        {
            static const SincKernels kernels;
            return kernels;
        }

        /** The sixteen weights for a position `fraction` (0..1) past a frame; they reach from 7 frames before it to 8 after. */
        void weights(int cutoff, float fraction, float* out) const noexcept
        {
            const float position = std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(phases);
            const int phase = std::min(phases - 1, static_cast<int>(position));
            const float t = position - static_cast<float>(phase);
            const float* a = &table[static_cast<std::size_t>((cutoff * (phases + 1) + phase) * points)];
            const float* b = a + points;
            for (int k = 0; k < points; ++k)
                out[k] = a[k] + (b[k] - a[k]) * t;
        }

        /** The kernel for a read that advances `step` frames per output sample: narrower when it skips frames. */
        [[nodiscard]] static int cutoffFor(double step) noexcept { return step <= 1.0 ? 0 : (step <= 2.0 ? 1 : 2); }

    private:
        SincKernels() : table(static_cast<std::size_t>(cutoffs * (phases + 1) * points))
        {
            constexpr double pi = 3.14159265358979323846;
            constexpr double beta = 9.0;
            auto bessel = [](double x)
            {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 30; ++k)
                {
                    term *= (0.5 * x / k) * (0.5 * x / k);
                    sum += term;
                }
                return sum;
            };
            for (int c = 0; c < cutoffs; ++c)
            {
                const double cutoff = 1.0 / static_cast<double>(1 << c);
                for (int phase = 0; phase <= phases; ++phase)
                {
                    const double fraction = static_cast<double>(phase) / phases;
                    double total = 0.0;
                    std::array<double, points> w {};
                    for (int k = 0; k < points; ++k)
                    {
                        const double x = static_cast<double>(k - (points / 2 - 1)) - fraction; // frames from the read position
                        const double window = std::abs(x) < points / 2.0
                            ? bessel(beta * std::sqrt(1.0 - (x / (points / 2.0)) * (x / (points / 2.0)))) / bessel(beta) : 0.0;
                        const double sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(pi * cutoff * x) / (pi * cutoff * x);
                        w[static_cast<std::size_t>(k)] = cutoff * sinc * window;
                        total += w[static_cast<std::size_t>(k)];
                    }
                    for (int k = 0; k < points; ++k) // unity gain at every phase
                        table[static_cast<std::size_t>((c * (phases + 1) + phase) * points + k)] = static_cast<float>(w[static_cast<std::size_t>(k)] / total);
                }
            }
        }

        std::vector<float> table;
    };
}
