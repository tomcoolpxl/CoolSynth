#include "ChipWaves.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>

#include <juce_dsp/juce_dsp.h>

namespace coolsynth::synth::v3
{
    namespace
    {
        constexpr int kHarmonics = ChipWaveBank::tableSize / 2 - 1; // 1023
        constexpr double kPi = 3.14159265358979323846;

        using Spectrum = std::vector<std::complex<double>>; // index = harmonic number, 0 (the mean) unused

        // Peter Sovietov's Ayumi (github.com/true-grue/ayumi): the AY-3-8910's 16 levels, each twice, and the YM2149's 32.
        constexpr std::array<float, 32> kAyLevels {
            0.0f, 0.0f, 0.00999465934234f, 0.00999465934234f, 0.0144502937362f, 0.0144502937362f,
            0.0210574502174f, 0.0210574502174f, 0.0307011520562f, 0.0307011520562f, 0.0455481803616f, 0.0455481803616f,
            0.0644998855573f, 0.0644998855573f, 0.107362478065f, 0.107362478065f, 0.126588845655f, 0.126588845655f,
            0.20498970016f, 0.20498970016f, 0.292210269322f, 0.292210269322f, 0.372838941024f, 0.372838941024f,
            0.492530708782f, 0.492530708782f, 0.635324635691f, 0.635324635691f, 0.805584802014f, 0.805584802014f,
            1.0f, 1.0f };
        constexpr std::array<float, 32> kYmLevels {
            0.0f, 0.0f, 0.00465400167849f, 0.00772106507973f, 0.0109559777218f, 0.0139620050355f,
            0.0169985503929f, 0.0200198367285f, 0.024368657969f, 0.029694056611f, 0.0350652323186f, 0.0403906309606f,
            0.0485389486534f, 0.0583352407111f, 0.0680552376593f, 0.0777752346075f, 0.0925154497597f, 0.111085679408f,
            0.129747463188f, 0.148485542077f, 0.17666895552f, 0.211551079576f, 0.246387426566f, 0.281101701381f,
            0.333730067903f, 0.400427252613f, 0.467383840696f, 0.53443198291f, 0.635172045472f, 0.75800717174f,
            0.879926756695f, 1.0f };

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

        /** The SID's 24-bit phase accumulator at a phase in 0..1, and the 12 bits its waveforms read from it
            (reSID's wave.h: the saw is the top 12 bits; the triangle folds them at the top bit). */
        std::uint32_t accumulatorAt(double t)
        {
            return static_cast<std::uint32_t>(std::floor(t * 16777216.0)) & 0xFFFFFFu;
        }
        std::uint32_t sid12Saw(std::uint32_t a) { return a >> 12; }
        std::uint32_t sid12Triangle(std::uint32_t a) { return (((a & 0x800000u) != 0 ? ~a : a) >> 11) & 0xFFFu; }
        double fromSid12(std::uint32_t v) { return static_cast<double>(v) / 4095.0 * 2.0 - 1.0; }

        /** The pulse of a combined wave, high for the share `high` of the cycle, at its end as on the SID (its pulse is
            high while the saw's 12 bits are at or above the width register). */
        std::uint32_t sid12Pulse(std::uint32_t a, double high)
        {
            const auto width = static_cast<std::uint32_t>(std::lround((1.0 - high) * 4096.0));
            return sid12Saw(a) >= width ? 0xFFFu : 0u;
        }

        double nesMixed(int v)
        {
            // nesdev.org, APU Mixer: the triangle alone through the tnd curve, scaled so 15 is the top.
            auto tnd = [](int level) { return level <= 0 ? 0.0 : 159.79 / (8227.0 / level + 100.0); };
            return tnd(v) / tnd(15) * 2.0 - 1.0;
        }

        int step32(double t, int steps) { return std::min(steps - 1, static_cast<int>(std::floor(t * steps))); }

        int nesTriangleLevel(double t)
        {
            const int k = step32(t, 32);
            return k < 16 ? 15 - k : k - 16;
        }

        /** The named waves: harmonic amplitudes, drawn after the Game Boy's and the SCC's classic waves. */
        std::vector<double> namedWave(int index)
        {
            switch (index)
            {
                case 0: return { 1.0 };                                                                   // Sine
                case 1: return { 1.0, 0.0, -1.0 / 9.0, 0.0, 1.0 / 25.0, 0.0, -1.0 / 49.0, 0.0, 1.0 / 81.0 }; // Triangle
                case 2: return { 1.0, 1.0 / 2, 1.0 / 3, 1.0 / 4, 1.0 / 5, 1.0 / 6, 1.0 / 7, 1.0 / 8, 1.0 / 9,
                                 1.0 / 10, 1.0 / 11, 1.0 / 12, 1.0 / 13, 1.0 / 14, 1.0 / 15 };           // Saw
                case 3: return { 1.0, 0.0, 1.0 / 3, 0.0, 1.0 / 5, 0.0, 1.0 / 7, 0.0, 1.0 / 9, 0.0, 1.0 / 11,
                                 0.0, 1.0 / 13, 0.0, 1.0 / 15 };                                          // Square
                case 4: return { 1.0, 0.7, 0.5, 0.35, 0.0, 0.25, 0.0, 0.2 };                              // Organ
                case 5: return { 1.0, 0.55, 0.25, 0.12, 0.06 };                                           // Bass
                case 6: return { 1.0, 0.85, 0.7, 0.55, 0.42, 0.3, 0.2, 0.12 };                            // Brass
                case 7: return { 1.0, 0.45, 0.32, 0.26, 0.21, 0.17, 0.14, 0.11, 0.09 };                   // Strings
                case 8: return { 1.0, 0.0, 0.0, 0.75, 0.0, 0.5, 0.0, 0.0, 0.4, 0.0, 0.25 };               // Bell
                default: return { 0.45, 0.9, 1.0, 0.45, 0.2, 0.12, 0.18, 0.26, 0.15, 0.06 };             // Voice
            }
        }

        /** A named wave as 32 steps of the given resolution (16 levels or 256), peak at full scale. */
        std::function<double(double)> namedSteps(int index, int levelCount)
        {
            const auto harmonics = namedWave(index);
            std::array<double, 32> steps {};
            double peak = 0.0;
            for (int k = 0; k < 32; ++k)
            {
                double v = 0.0;
                for (std::size_t n = 0; n < harmonics.size(); ++n)
                    v += harmonics[n] * std::sin(2.0 * kPi * static_cast<double>(n + 1) * k / 32.0);
                steps[static_cast<std::size_t>(k)] = v;
                peak = std::max(peak, std::abs(v));
            }
            const double half = (levelCount - 1) / 2.0;
            for (auto& v : steps)
                v = std::round((v / peak + 1.0) * half) / half - 1.0;
            return [steps](double t) { return steps[static_cast<std::size_t>(step32(t, 32))]; };
        }

        struct Built
        {
            std::vector<Spectrum> spectra;
            std::vector<int> group; // frames of a group share one scale; -1: each its own
        };

        Built buildSpectra()
        {
            Built b;
            b.spectra.resize(ChipWaveBank::frameCount);
            b.group.assign(ChipWaveBank::frameCount, -1);
            auto set = [&](int index, const std::function<double(double)>& cycle, int group = -1)
            {
                b.spectra[static_cast<std::size_t>(index)] = spectrumOf(cycle);
                b.group[static_cast<std::size_t>(index)] = group;
            };

            set(ChipWaveBank::triangle4, [](double t) { return nesTriangleLevel(t) / 7.5 - 1.0; });
            set(ChipWaveBank::triangleNes, [](double t) { return nesMixed(nesTriangleLevel(t)); });
            set(ChipWaveBank::triangleSmooth, [](double t) { return fromSid12(sid12Triangle(accumulatorAt(t))); });
            set(ChipWaveBank::saw4, [](double t) { return (step32(t, 32) / 2) / 7.5 - 1.0; });
            set(ChipWaveBank::saw8, [](double t) { return std::round(step32(t, 32) / 31.0 * 255.0) / 127.5 - 1.0; });
            set(ChipWaveBank::sawSmooth, [](double t) { return fromSid12(sid12Saw(accumulatorAt(t))); });
            set(ChipWaveBank::buzzSawAy, [](double t) { return 2.0 * kAyLevels[static_cast<std::size_t>(2 * step32(t, 16) + 1)] - 1.0; });
            set(ChipWaveBank::buzzSawYm, [](double t) { return 2.0 * kYmLevels[static_cast<std::size_t>(step32(t, 32))] - 1.0; });
            set(ChipWaveBank::buzzTriangleAy, [](double t)
            {
                const int k = step32(t, 32);
                return 2.0 * kAyLevels[static_cast<std::size_t>(2 * (k < 16 ? k : 31 - k) + 1)] - 1.0;
            });
            set(ChipWaveBank::buzzTriangleYm, [](double t)
            {
                const int k = step32(t, 64);
                return 2.0 * kYmLevels[static_cast<std::size_t>(k < 32 ? k : 63 - k)] - 1.0;
            });
            set(ChipWaveBank::triSaw, [](double t)
            {
                const auto a = accumulatorAt(t);
                return fromSid12(sid12Triangle(a) & sid12Saw(a));
            }, 0);
            for (int w = 0; w < ChipWaveBank::widths; ++w)
            {
                const double high = (w + 0.5) / ChipWaveBank::widths;
                set(ChipWaveBank::triPulse + w, [high](double t)
                {
                    const auto a = accumulatorAt(t);
                    return fromSid12(sid12Triangle(a) & sid12Pulse(a, high));
                }, 1);
                set(ChipWaveBank::sawPulse + w, [high](double t)
                {
                    const auto a = accumulatorAt(t);
                    return fromSid12(sid12Saw(a) & sid12Pulse(a, high));
                }, 2);
                set(ChipWaveBank::triSawPulse + w, [high](double t)
                {
                    const auto a = accumulatorAt(t);
                    return fromSid12(sid12Triangle(a) & sid12Saw(a) & sid12Pulse(a, high));
                }, 3);
            }
            for (int i = 0; i < ChipWaveBank::tableWaves; ++i)
            {
                set(ChipWaveBank::table4 + i, namedSteps(i, 16));
                set(ChipWaveBank::table8 + i, namedSteps(i, 256));
            }
            return b;
        }
    }

    ChipWaveBank::ChipWaveBank()
    {
        data.assign(static_cast<std::size_t>(frameCount * levels * stride), 0.0f);
        constexpr int order = 11;
        static_assert((1 << order) == tableSize);
        juce::dsp::FFT fft(order);
        std::vector<float> buffer(2 * tableSize);

        auto inverse = [&](const Spectrum& spectrum, int limit)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            for (int h = 1; h <= limit; ++h)
            {
                const auto c = spectrum[static_cast<std::size_t>(h)];
                buffer[static_cast<std::size_t>(2 * h)] = static_cast<float>(c.real());
                buffer[static_cast<std::size_t>(2 * h + 1)] = static_cast<float>(c.imag());
            }
            fft.performRealOnlyInverseTransform(buffer.data());
        };

        const auto built = buildSpectra();

        // Each fixed shape peaks at 1 in full band; a combined wave's frames share the scale of their loudest, and are
        // never made louder than the AND leaves them.
        std::vector<float> peaks(static_cast<std::size_t>(frameCount), 0.0f);
        for (int f = 0; f < frameCount; ++f)
        {
            inverse(built.spectra[static_cast<std::size_t>(f)], kHarmonics);
            for (int i = 0; i < tableSize; ++i)
                peaks[static_cast<std::size_t>(f)] = std::max(peaks[static_cast<std::size_t>(f)], std::abs(buffer[static_cast<std::size_t>(i)]));
        }
        std::array<float, 4> groupPeaks {};
        for (int f = 0; f < frameCount; ++f)
            if (const int g = built.group[static_cast<std::size_t>(f)]; g >= 0)
                groupPeaks[static_cast<std::size_t>(g)] = std::max(groupPeaks[static_cast<std::size_t>(g)], peaks[static_cast<std::size_t>(f)]);

        for (int f = 0; f < frameCount; ++f)
        {
            const int g = built.group[static_cast<std::size_t>(f)];
            const float peak = g >= 0 ? std::max(1.0f, groupPeaks[static_cast<std::size_t>(g)]) : peaks[static_cast<std::size_t>(f)];
            const float scale = peak > 0.0f ? 1.0f / peak : 1.0f;
            for (int level = 0; level < levels; ++level)
            {
                inverse(built.spectra[static_cast<std::size_t>(f)], std::min(kHarmonics, (tableSize / 2) >> level));
                float* out = data.data() + static_cast<std::size_t>((f * levels + level) * stride);
                for (int i = 0; i < tableSize; ++i)
                    out[i + 1] = buffer[static_cast<std::size_t>(i)] * scale;
                out[0] = out[tableSize];
                out[tableSize + 1] = out[1];
                out[tableSize + 2] = out[2];
            }
        }
    }

    const ChipWaveBank& ChipWaveBank::instance()
    {
        static const ChipWaveBank bank;
        return bank;
    }

    const float* ChipWaveBank::frame(int index, int level) const noexcept
    {
        const int f = std::clamp(index, 0, frameCount - 1);
        const int l = std::clamp(level, 0, levels - 1);
        return data.data() + static_cast<std::size_t>((f * levels + l) * stride) + 1;
    }

    const float* ChipWaveBank::ayLevels() noexcept { return kAyLevels.data(); }
    const float* ChipWaveBank::ymLevels() noexcept { return kYmLevels.data(); }
}
