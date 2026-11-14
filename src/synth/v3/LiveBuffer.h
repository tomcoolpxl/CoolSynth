#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "synth/HalfBand2x.h"

/*
    The live source of a grain cloud (decision D139): the last second of the generator just before
    a Grain slot whose combine is Live, recorded as it plays, one per voice.

    The voice runs at twice the host rate; the recording keeps half of that, through the same
    half-band filter the voice's own output passes, so it holds everything audible and nothing that
    would fold back when a grain plays it an octave up. Like the factory samples it exists at
    three band limits: the full band, then about a fifth and a tenth of its rate (4th-order
    Butterworth low-passes), for grains transposed further up. It is a ring: the newest frame
    overwrites the oldest, and reads wrap.
*/
namespace coolsynth::synth::v3
{
    class LiveBuffer final
    {
    public:
        static constexpr int levels = 3;
        static constexpr double seconds = 1.0;

        /** Message thread. voiceRate is the rate the voice runs at; the recording keeps half of it. */
        void prepare(double voiceRate)
        {
            rate = std::max(500.0, 0.5 * voiceRate);
            length = std::max(64, static_cast<int>(std::lround(seconds * rate)));
            for (auto& level : data)
                level.assign(static_cast<std::size_t>(length), 0.0f);
            // Two 4th-order Butterworth low-passes, as the grain sources' levels: the poles' Q of 0.541 and 1.307.
            for (int level = 1; level < levels; ++level)
            {
                const double cutoff = level == 1 ? 0.21 : 0.105; // cycles per sample
                stages[static_cast<std::size_t>(level - 1)] = { butterworth(cutoff, 0.54119610), butterworth(cutoff, 1.30656296) };
            }
            clear();
        }

        /** Silence: a fresh note records from nothing. */
        void clear() noexcept
        {
            for (auto& level : data)
                std::fill(level.begin(), level.end(), 0.0f);
            for (auto& pair : stages)
                for (auto& stage : pair)
                    stage.z1 = stage.z2 = 0.0f;
            decimator.reset();
            write = 0;
            havePending = false;
        }

        /** n samples of the source at the voice's rate; every second one completes a recorded frame. */
        void record(const float* signal, int n) noexcept
        {
            if (signal == nullptr || length <= 0)
                return;
            for (int i = 0; i < n; ++i)
            {
                if (! havePending)
                {
                    pendingFirst = signal[i];
                    havePending = true;
                    continue;
                }
                havePending = false;
                const float x = decimator.process(pendingFirst, signal[i]);
                const auto at = static_cast<std::size_t>(write);
                data[0][at] = x;
                data[1][at] = stages[0][1].process(stages[0][0].process(x));
                data[2][at] = stages[1][1].process(stages[1][0].process(x));
                if (++write >= length)
                    write = 0;
            }
        }

        /** A 4-point Hermite read at any frame position; positions wrap round the ring. */
        [[nodiscard]] float read(int level, double frame) const noexcept
        {
            const auto& d = data[static_cast<std::size_t>(std::clamp(level, 0, levels - 1))];
            const double wrapped = frame - static_cast<double>(length) * std::floor(frame / static_cast<double>(length));
            const int i = std::min(length - 1, static_cast<int>(wrapped));
            const float t = static_cast<float>(wrapped - i);
            auto at = [&](int k) noexcept
            {
                int j = i + k;
                if (j < 0)
                    j += length;
                else if (j >= length)
                    j -= length;
                return d[static_cast<std::size_t>(j)];
            };
            const float xm1 = at(-1), x0 = at(0), x1 = at(1), x2 = at(2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        /** The frame the next recorded sample goes to: everything before it, back one second, is the past. */
        [[nodiscard]] int writePosition() const noexcept { return write; }
        [[nodiscard]] int frames() const noexcept { return length; }
        [[nodiscard]] double sampleRate() const noexcept { return rate; }

    private:
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            float process(float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        /** A 2nd-order low-pass section by the bilinear transform. */
        static Biquad butterworth(double cyclesPerSample, double q)
        {
            const double k = std::tan(3.14159265358979323846 * cyclesPerSample);
            const double norm = 1.0 / (1.0 + k / q + k * k);
            Biquad b;
            b.b0 = static_cast<float>(k * k * norm);
            b.b1 = 2.0f * b.b0;
            b.b2 = b.b0;
            b.a1 = static_cast<float>(2.0 * (k * k - 1.0) * norm);
            b.a2 = static_cast<float>((1.0 - k / q + k * k) * norm);
            return b;
        }

        std::array<std::vector<float>, levels> data;
        std::array<std::array<Biquad, 2>, levels - 1> stages {};
        coolsynth::synth::HalfBandDecimator2x decimator;
        double rate = 48000.0;
        int length = 0;
        int write = 0;
        float pendingFirst = 0.0f;
        bool havePending = false;
    };
}
