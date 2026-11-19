#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/LoFi.h"

namespace
{
    using coolsynth::synth::LoFi;
    using coolsynth::synth::LoFiParameters;

    constexpr double kRate = 48000.0;

    LoFiParameters neutral()
    {
        LoFiParameters p;
        p.enabled = true;
        return p;
    }

    std::vector<float> run(const LoFiParameters& p, const std::vector<float>& input)
    {
        auto lofi = std::make_unique<LoFi>();
        lofi->prepare(kRate, 2);
        std::vector<float> out(input.size());
        juce::AudioBuffer<float> block(2, 256);
        for (std::size_t s = 0; s < input.size(); s += 256)
        {
            const int n = static_cast<int>(std::min<std::size_t>(256, input.size() - s));
            block.setSize(2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                block.setSample(0, i, input[s + static_cast<std::size_t>(i)]);
                block.setSample(1, i, input[s + static_cast<std::size_t>(i)]);
            }
            lofi->process(block, p);
            for (int i = 0; i < n; ++i)
                out[s + static_cast<std::size_t>(i)] = block.getSample(0, i);
        }
        return out;
    }

    std::vector<float> sine(double hz, float amplitude, int samples)
    {
        std::vector<float> x(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
            x[static_cast<std::size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
        return x;
    }

    double rms(const std::vector<float>& x, std::size_t from)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(x.size() - from));
    }

    /** Spread of the cycle lengths of a sine, in cents. */
    double periodSpreadCents(const std::vector<float>& x)
    {
        std::vector<double> crossings;
        for (std::size_t i = 4800; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - x[i - 1] / static_cast<double>(x[i] - x[i - 1]));
        double shortest = 1.0e9, longest = 0.0;
        for (std::size_t i = 1; i < crossings.size(); ++i)
        {
            const double period = crossings[i] - crossings[i - 1];
            shortest = std::min(shortest, period);
            longest = std::max(longest, period);
        }
        return 1200.0 * std::log2(longest / shortest);
    }
}

class V3LoFiTests final : public juce::UnitTest
{
public:
    V3LoFiTests() : juce::UnitTest("V3 lo-fi", "V3") {}

    void runTest() override
    {
        beginTest("neutral_settings_are_a_clean_fixed_delay");
        {
            const auto x = sine(700.0, 0.5f, 9600);
            const auto y = run(neutral(), x);
            const auto delay = static_cast<std::size_t>(std::lround(LoFi::baseDelaySeconds * kRate));
            float error = 0.0f;
            for (std::size_t i = delay + 4; i < x.size(); ++i)
                error = std::max(error, std::abs(y[i] - x[i - delay]));
            expectLessThan(error, 1.0e-4f);
        }

        beginTest("wow_and_flutter_move_the_pitch");
        {
            const auto x = sine(1000.0, 0.5f, static_cast<int>(kRate * 4));
            auto p = neutral();
            const double still = periodSpreadCents(run(p, x));
            p.wow = 1.0f;
            p.flutter = 1.0f;
            const double wobbly = periodSpreadCents(run(p, x));
            logMessage("cycle-length spread: " + juce::String(still, 2) + " cents still, " + juce::String(wobbly, 1) + " cents with wow and flutter");
            expectLessThan(still, 1.0);
            expectGreaterThan(wobbly, 15.0);
        }

        beginTest("bandwidth_closes_the_top");
        {
            auto p = neutral();
            p.bandwidth = 0.0f;
            const double high = rms(run(p, sine(10000.0, 0.5f, 24000)), 4800);
            const double low = rms(run(p, sine(500.0, 0.5f, 24000)), 4800);
            expectGreaterThan(20.0 * std::log10(low / high), 20.0);
        }

        // The noise as tape and records make it (D221, docs/research/noise-modelling.md).
        auto noiseOf = [](const LoFiParameters& p, const std::vector<float>& x, std::uint32_t seed = 1, double rate = kRate)
        {
            // What the noise adds: the output with it, less the output without it.
            auto withNoise = std::make_unique<LoFi>();
            auto without = std::make_unique<LoFi>();
            withNoise->setSeed(seed);
            withNoise->prepare(rate, 2);
            without->prepare(rate, 2);
            auto plain = p;
            plain.hiss = plain.crackle = 0.0f;
            std::vector<float> left(x.size()), right(x.size());
            juce::AudioBuffer<float> a(2, 256), b(2, 256);
            for (std::size_t s = 0; s < x.size(); s += 256)
            {
                const int n = static_cast<int>(std::min<std::size_t>(256, x.size() - s));
                a.setSize(2, n, false, false, true);
                b.setSize(2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        a.setSample(ch, i, x[s + static_cast<std::size_t>(i)]);
                        b.setSample(ch, i, x[s + static_cast<std::size_t>(i)]);
                    }
                withNoise->process(a, p);
                without->process(b, plain);
                for (int i = 0; i < n; ++i)
                {
                    left[s + static_cast<std::size_t>(i)] = a.getSample(0, i) - b.getSample(0, i);
                    right[s + static_cast<std::size_t>(i)] = a.getSample(1, i) - b.getSample(1, i);
                }
            }
            return std::make_pair(left, right);
        };
        auto rmsDb = [](const std::vector<float>& x, std::size_t from, std::size_t to)
        {
            double sum = 0.0;
            for (std::size_t i = from; i < to; ++i)
                sum += static_cast<double>(x[i]) * x[i];
            return 10.0 * std::log10(std::max(1.0e-30, sum / static_cast<double>(to - from)));
        };

        beginTest("silence_stays_silent_and_the_noise_holds_its_level_while_anything_plays");
        {
            auto p = neutral();
            p.hiss = 1.0f;
            p.crackle = 1.0f;
            const auto silence = run(p, std::vector<float>(48000, 0.0f));
            float loudest = 0.0f;
            for (float v : silence)
                loudest = std::max(loudest, std::abs(v));
            expectLessThan(loudest, 1.0e-6f, "silence stays silent");

            // Hiss alone under a tone at -10 dBFS and one at -60 dBFS: the same level, not the music's.
            auto hissOnly = neutral();
            hissOnly.hiss = 1.0f;
            const double loud = rmsDb(noiseOf(hissOnly, sine(200.0, 0.3f, 96000)).first, 24000, 96000);
            const double quiet = rmsDb(noiseOf(hissOnly, sine(200.0, 0.001f, 96000)).first, 24000, 96000);
            logMessage("hiss at full Hiss: " + juce::String(loud, 1) + " dBFS under a loud tone, " + juce::String(quiet, 1) + " dBFS under a quiet one");
            expectWithinAbsoluteError(loud, quiet, 1.0, "steady, whatever the music's level");
            expectWithinAbsoluteError(loud, -33.0, 1.5, "as loud as before D221 at full Hiss");

            // A second of tone, then silence: gone a second and a fraction after the music stops.
            std::vector<float> toneThenSilence(static_cast<std::size_t>(kRate * 4), 0.0f);
            const auto tone = sine(200.0, 0.3f, 48000);
            std::copy(tone.begin(), tone.end(), toneThenSilence.begin());
            const auto tail = noiseOf(p, toneThenSilence).first;
            expectWithinAbsoluteError(rmsDb(tail, 48000 + 400, 48000 + 24000), loud, 1.0, "still there just after the music");
            expectLessThan(rmsDb(tail, 48000 + 2 * 48000 + 9600, tail.size()), -120.0, "gone after the fade");
        }

        beginTest("hiss_has_tapes_tilt_and_bandwidth_dulls_it");
        {
            auto p = neutral();
            p.hiss = 1.0f;
            const auto x = sine(100.0, 0.3f, 48000 * 3);
            const auto hiss = noiseOf(p, x).first;
            // Power per hertz in a band: an averaged Hann-windowed spectrum.
            auto bandDb = [&](const std::vector<float>& v, double lowHz, double highHz)
            {
                constexpr int order = 12, size = 1 << order;
                juce::dsp::FFT fft(order);
                juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(size), juce::dsp::WindowingFunction<float>::hann);
                std::vector<double> power(static_cast<std::size_t>(size / 2), 0.0);
                std::vector<float> frame(static_cast<std::size_t>(2 * size));
                int frames = 0;
                for (std::size_t start = 9600; start + size <= v.size(); start += size / 2, ++frames)
                {
                    std::fill(frame.begin(), frame.end(), 0.0f);
                    std::copy_n(v.begin() + static_cast<std::ptrdiff_t>(start), size, frame.begin());
                    window.multiplyWithWindowingTable(frame.data(), static_cast<std::size_t>(size));
                    fft.performFrequencyOnlyForwardTransform(frame.data());
                    for (int b = 0; b < size / 2; ++b)
                        power[static_cast<std::size_t>(b)] += static_cast<double>(frame[static_cast<std::size_t>(b)]) * frame[static_cast<std::size_t>(b)];
                }
                const int from = static_cast<int>(lowHz * size / kRate), to = static_cast<int>(highHz * size / kRate);
                double sum = 0.0;
                for (int b = from; b < to; ++b)
                    sum += power[static_cast<std::size_t>(b)];
                return 10.0 * std::log10(sum / static_cast<double>((to - from) * frames));
            };
            const double lows = bandDb(hiss, 150.0, 400.0), highs = bandDb(hiss, 4000.0, 8000.0);
            logMessage("hiss per hertz: 150-400 Hz " + juce::String(lows, 1) + " dB, 4-8 kHz " + juce::String(highs, 1) + " dB");
            expectGreaterThan(highs - lows, 6.0, "rising into the treble, as tape hiss after playback EQ");

            auto dull = p;
            dull.bandwidth = 0.0f;
            const double open = bandDb(hiss, 6000.0, 12000.0);
            const double closed = bandDb(noiseOf(dull, x).first, 6000.0, 12000.0);
            expectGreaterThan(open - closed, 20.0, "Bandwidth dulls the hiss with the music");
        }

        beginTest("crackle_is_mostly_one_sided_and_independent_of_the_rate");
        {
            auto p = neutral();
            p.crackle = 1.0f;
            const auto x = sine(150.0, 0.3f, static_cast<int>(kRate * 6));
            const auto [left, right] = noiseOf(p, x);
            // Each click: where either side rises past 0.01 after 5 ms of quiet; one-sided if the other side stays
            // under a tenth of it over the next 4 ms.
            int oneSided = 0, both = 0, quiet = 0;
            for (std::size_t i = 24000; i + 200 < left.size(); ++i)
            {
                const float level = std::max(std::abs(left[i]), std::abs(right[i]));
                if (level < 0.01f)
                {
                    ++quiet;
                    continue;
                }
                if (quiet >= 240)
                {
                    float peakLeft = 0.0f, peakRight = 0.0f;
                    for (std::size_t k = i; k < i + 192; ++k)
                    {
                        peakLeft = std::max(peakLeft, std::abs(left[k]));
                        peakRight = std::max(peakRight, std::abs(right[k]));
                    }
                    (std::min(peakLeft, peakRight) < 0.1f * std::max(peakLeft, peakRight) ? oneSided : both)++;
                }
                quiet = 0;
            }
            logMessage("clicks: " + juce::String(oneSided) + " in one channel, " + juce::String(both) + " in both");
            expectGreaterThan(oneSided, both / 2, "a groove wall at a time, as often as not");
            expectGreaterThan(oneSided + both, 30, "it crackles");

            // At 96 kHz the clicks last as long in time and sound as loud.
            const auto x96 = [&]
            {
                std::vector<float> v(static_cast<std::size_t>(192000 * 6));
                for (std::size_t i = 0; i < v.size(); ++i)
                    v[i] = 0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 150.0 * static_cast<double>(i) / 192000.0));
                return v;
            }();
            const double at48 = rmsDb(left, 24000, left.size());
            const auto at96l = noiseOf(p, x96, 1, 192000.0).first;
            const double at96 = rmsDb(at96l, 96000, at96l.size());
            logMessage("crackle level: " + juce::String(at48, 1) + " dBFS at 48 kHz, " + juce::String(at96, 1) + " dBFS at 192 kHz");
            expectWithinAbsoluteError(at96, at48, 3.0, "the same crackle at any sample rate");
        }

        beginTest("each_slot_its_own_noise_and_the_same_again_after_a_reset");
        {
            auto p = neutral();
            p.hiss = 0.5f;
            p.crackle = 0.5f;
            const auto x = sine(300.0, 0.3f, 48000);
            const auto one = noiseOf(p, x, 1).first;
            const auto two = noiseOf(p, x, 2).first;
            const auto again = noiseOf(p, x, 1).first;
            expect(one != two, "two seeds, two noises");
            expect(one == again, "one seed, the same noise every time");
        }

        beginTest("bank_lofi_slot");
        {
            const auto result = coolsynth::presets::parseBankFile("[L]\nf1 = LoFi wow=0.4 flutter=0.3 hiss=0.2 crackle=0.1 bandwidth=0.6 bits=12 rate=22000 mix=1\n", "l.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "f1.type=LoFi", "f1.p1=0.4", "f1.p5=0.6", "f1.p6=12", "f1.p7=22000" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted);
        }
    }
};

static V3LoFiTests v3LoFiTests;
