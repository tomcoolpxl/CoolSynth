#include "Wavetables.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>

#include <juce_dsp/juce_dsp.h>

namespace coolsynth::synth::v3
{
    namespace
    {
        constexpr int kHarmonics = WavetableBank::tableSize / 2 - 1; // 1023
        constexpr double kPi = 3.14159265358979323846;

        using Spectrum = std::vector<std::complex<double>>; // index = harmonic number, 0 unused

        /** A sine series: sum of b[n] sin(n x). */
        Spectrum sineSeries(const std::vector<double>& b)
        {
            Spectrum s(kHarmonics + 1);
            for (std::size_t n = 1; n < b.size() && n <= static_cast<std::size_t>(kHarmonics); ++n)
                s[n] = { 0.0, -b[n] };
            return s;
        }

        std::vector<double> classicKey(int shape)
        {
            std::vector<double> b(kHarmonics + 1, 0.0);
            for (int n = 1; n <= kHarmonics; ++n)
            {
                const auto i = static_cast<std::size_t>(n);
                switch (shape)
                {
                    case 0: b[i] = n == 1 ? 1.0 : 0.0; break;                                              // sine
                    case 1: b[i] = n % 2 == 1 ? (((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (double(n) * n) : 0.0; break; // triangle
                    case 2: b[i] = (n % 2 == 1 ? 1.0 : -1.0) / n; break;                                   // saw, jump at the half cycle
                    default: b[i] = n % 2 == 1 ? 1.0 / n : 0.0; break;                                     // square
                }
            }
            return b;
        }

        /** The spectrum of one cycle given in the time domain, sampled finely enough that folding is negligible. */
        Spectrum spectrumOf(const std::function<double(double)>& cycle)
        {
            constexpr int order = 14;
            constexpr int n = 1 << order;
            juce::dsp::FFT fft(order);
            std::vector<float> buffer(2 * n, 0.0f);
            for (int i = 0; i < n; ++i)
                buffer[static_cast<std::size_t>(i)] = static_cast<float>(cycle(static_cast<double>(i) / n));
            fft.performRealOnlyForwardTransform(buffer.data(), true);
            Spectrum s(kHarmonics + 1);
            for (int h = 1; h <= kHarmonics; ++h)
                s[static_cast<std::size_t>(h)] = { buffer[static_cast<std::size_t>(2 * h)], buffer[static_cast<std::size_t>(2 * h + 1)] };
            return s;
        }

        /** 32 steps of 16 levels: what a 4-bit, 32-sample chip wave plays. */
        double chipStep(double t, const std::function<double(double)>& shape)
        {
            const double step = std::floor(t * 32.0) / 32.0;
            const double v = std::clamp(shape(step), -1.0, 1.0);
            return std::round((v + 1.0) * 7.5) / 7.5 - 1.0;
        }

        std::vector<Spectrum> buildTable(int table)
        {
            std::vector<Spectrum> frames;
            const int count = WavetableBank::frames;
            for (int f = 0; f < count; ++f)
            {
                const double position = static_cast<double>(f) / (count - 1);
                switch (table)
                {
                    case 0: // Classic: keyframes at 0, 5, 10 and 15
                    {
                        const double x = position * 3.0;
                        const int lower = std::min(2, static_cast<int>(x));
                        const double t = x - lower;
                        const auto a = classicKey(lower), b = classicKey(lower + 1);
                        std::vector<double> mix(a.size());
                        for (std::size_t i = 0; i < a.size(); ++i)
                            mix[i] = a[i] + (b[i] - a[i]) * t;
                        frames.push_back(sineSeries(mix));
                        break;
                    }
                    case 1: // Digital: hard sync, slave ratio 1 .. 8
                    {
                        const double ratio = 1.0 + 7.0 * position;
                        frames.push_back(spectrumOf([ratio](double t)
                        {
                            const double p = ratio * t;
                            return 2.0 * (p - std::floor(p)) - 1.0;
                        }));
                        break;
                    }
                    case 2: // Vocal: a saw through the vowel formants, a e i o u, the harmonic grid at 110 Hz
                    {
                        static const double formants[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                               { 570, 840, 2410 },  { 300, 870, 2240 } };
                        static const double gains[3] = { 1.0, 0.5, 0.25 };
                        static const double widths[3] = { 90.0, 110.0, 160.0 };
                        const double x = position * 4.0;
                        const int lower = std::min(3, static_cast<int>(x));
                        const double t = x - lower;
                        std::vector<double> b(kHarmonics + 1, 0.0);
                        for (int n = 1; n <= kHarmonics; ++n)
                        {
                            const double hz = 110.0 * n;
                            double envelope[2] = { 0.02, 0.02 };
                            for (int v = 0; v < 2; ++v)
                                for (int k = 0; k < 3; ++k)
                                {
                                    const double d = (hz - formants[lower + v][k]) / widths[k];
                                    envelope[v] += gains[k] * std::exp(-0.5 * d * d);
                                }
                            b[static_cast<std::size_t>(n)] = (envelope[0] + (envelope[1] - envelope[0]) * t) / n;
                        }
                        frames.push_back(sineSeries(b));
                        break;
                    }
                    default: // Chip
                    {
                        std::function<double(double)> shape;
                        if (f < 8)
                        {
                            const double duty = (4.0 + 2.0 * f) / 32.0; // 1/8 .. 1/2 in sixteenths
                            shape = [duty](double t) { return t < duty ? 1.0 : -1.0; };
                        }
                        else if (f == 8)
                            shape = [](double t) { return t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t; };   // NES triangle
                        else if (f == 9)
                            shape = [](double t) { return 2.0 * t - 1.0; };                            // stepped saw
                        else
                        {
                            // SCC-style waves: a few harmonics, quantised.
                            static const double recipes[6][4] = { { 1, 0, 0, 0 }, { 1, 0.5, 0.33, 0 }, { 1, 0, 0.6, 0.4 },
                                                                  { 0.6, 1, 0, 0.5 }, { 1, 0.8, 0.6, 0.4 }, { 0.3, 0, 1, 0.7 } };
                            const auto& r = recipes[f - 10];
                            shape = [&r](double t)
                            {
                                double v = 0.0, sum = 0.0;
                                for (int k = 0; k < 4; ++k)
                                {
                                    v += r[k] * std::sin(2.0 * kPi * (k + 1) * t);
                                    sum += r[k];
                                }
                                return v / std::max(1.0, sum * 0.8);
                            };
                        }
                        frames.push_back(spectrumOf([shape](double t) { return chipStep(t, shape); }));
                        break;
                    }
                }
            }
            return frames;
        }
    }

    WavetableBank::WavetableBank()
    {
        data.assign(static_cast<std::size_t>(tableCount * frames * levels * stride), 0.0f);
        constexpr int order = 11;
        static_assert((1 << order) == tableSize);
        juce::dsp::FFT fft(order);
        std::vector<float> buffer(2 * tableSize);

        for (int table = 0; table < tableCount; ++table)
        {
            const auto spectra = buildTable(table);
            for (int f = 0; f < frames; ++f)
            {
                float scale = 1.0f;
                for (int level = 0; level < levels; ++level)
                {
                    const int limit = std::min(kHarmonics, (tableSize / 2) >> level);
                    std::fill(buffer.begin(), buffer.end(), 0.0f);
                    for (int h = 1; h <= limit; ++h)
                    {
                        const auto c = spectra[static_cast<std::size_t>(f)][static_cast<std::size_t>(h)];
                        buffer[static_cast<std::size_t>(2 * h)] = static_cast<float>(c.real());
                        buffer[static_cast<std::size_t>(2 * h + 1)] = static_cast<float>(c.imag());
                    }
                    fft.performRealOnlyInverseTransform(buffer.data());

                    if (level == 0)
                    {
                        float peak = 0.0f;
                        for (int i = 0; i < tableSize; ++i)
                            peak = std::max(peak, std::abs(buffer[static_cast<std::size_t>(i)]));
                        scale = peak > 0.0f ? 1.0f / peak : 1.0f;
                    }

                    float* out = data.data() + static_cast<std::size_t>(((table * frames + f) * levels + level) * stride);
                    for (int i = 0; i < tableSize; ++i)
                        out[i + 1] = buffer[static_cast<std::size_t>(i)] * scale;
                    out[0] = out[tableSize];
                    out[tableSize + 1] = out[1];
                    out[tableSize + 2] = out[2];
                }
            }
        }
    }

    const WavetableBank& WavetableBank::instance()
    {
        static const WavetableBank bank;
        return bank;
    }

    const float* WavetableBank::frame(int table, int frameIndex, int level) const noexcept
    {
        const int t = std::clamp(table, 0, tableCount - 1);
        const int f = std::clamp(frameIndex, 0, frames - 1);
        const int l = std::clamp(level, 0, levels - 1);
        return data.data() + static_cast<std::size_t>(((t * frames + f) * levels + l) * stride) + 1;
    }

    int WavetableBank::levelFor(double frequencyHz, double sampleRate) noexcept
    {
        const double ratio = (tableSize / 2.0) * std::max(1.0e-3, frequencyHz) / (0.75 * sampleRate);
        if (ratio <= 1.0)
            return 0;
        return std::min(levels - 1, static_cast<int>(std::ceil(std::log2(ratio))));
    }
}
