#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "synth/HalfBand2x.h"
#include "synth/v3/NoiseGenerator.h"
#include "synth/v3/VaGenerator.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Kind = NoiseGenerator::Kind;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;
    constexpr int kFftOrder = 14;
    constexpr int kFftSize = 1 << kFftOrder;

    struct Noise
    {
        SlotMemory memory;
        NoiseGenerator generator;
    };

    std::unique_ptr<Noise> makeNoise()
    {
        auto n = std::make_unique<Noise>();
        n->memory.allocate(NoiseGenerator::memoryFloats(kVoiceRate, kSubBlock));
        n->generator.prepare(kVoiceRate, kSubBlock, n->memory);
        n->generator.noteOn(NoteContext {});
        return n;
    }

    SlotParams noiseParams(Kind kind, float frequencyHz = 440.0f)
    {
        SlotParams p;
        p.p[NoiseGenerator::type] = static_cast<float>(kind);
        p.p[NoiseGenerator::tone] = 1.0f;
        p.p[NoiseGenerator::resonance] = 0.3f;
        p.p[NoiseGenerator::chipMode] = 0.0f;
        p.p[NoiseGenerator::clock] = 0.0f;
        p.p[NoiseGenerator::width] = 0.0f;
        p.frequencyHz = frequencyHz;
        return p;
    }

    struct Stereo
    {
        std::vector<float> left, right;
    };

    Stereo render(NoiseGenerator& g, const SlotParams& p, int samples)
    {
        Stereo out { std::vector<float>(static_cast<std::size_t>(samples)), std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(out.left.data() + s, out.right.data() + s, n, p);
        }
        return out;
    }

    /** Left channel at the host rate, through the voice's decimator. */
    std::vector<float> decimate(const std::vector<float>& x)
    {
        coolsynth::synth::HalfBandDecimator2x d;
        std::vector<float> out(x.size() / 2);
        d.processBlock(x.data(), out.data(), static_cast<int>(out.size()));
        return out;
    }

    double rms(const std::vector<float>& x, std::size_t from = 0)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(x.size() - from));
    }

    /** Averaged power spectrum (Hann windows, half overlap), one value per bin. */
    std::vector<double> spectrum(const std::vector<float>& x)
    {
        juce::dsp::FFT fft(kFftOrder);
        std::vector<double> power(kFftSize / 2, 0.0);
        std::vector<float> frame(2 * kFftSize);
        int frames = 0;
        for (std::size_t start = 0; start + kFftSize <= x.size(); start += kFftSize / 2)
        {
            std::fill(frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < kFftSize; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / kFftSize);
                frame[static_cast<std::size_t>(i)] = x[start + static_cast<std::size_t>(i)] * w;
            }
            fft.performFrequencyOnlyForwardTransform(frame.data());
            for (int b = 0; b < kFftSize / 2; ++b)
                power[static_cast<std::size_t>(b)] += static_cast<double>(frame[static_cast<std::size_t>(b)]) * frame[static_cast<std::size_t>(b)];
            ++frames;
        }
        for (auto& v : power)
            v /= std::max(1, frames);
        return power;
    }

    double bandPowerDb(const std::vector<double>& power, double sampleRate, double from, double to)
    {
        const double binHz = sampleRate / kFftSize;
        double sum = 0.0;
        for (int b = std::max(1, static_cast<int>(from / binHz)); b < static_cast<int>(to / binHz) && b < static_cast<int>(power.size()); ++b)
            sum += power[static_cast<std::size_t>(b)];
        return 10.0 * std::log10(std::max(1.0e-30, sum));
    }

    double peakFrequency(const std::vector<double>& power, double sampleRate)
    {
        std::size_t best = 1;
        for (std::size_t b = 1; b < power.size(); ++b)
            if (power[b] > power[best])
                best = b;
        return static_cast<double>(best) * sampleRate / kFftSize;
    }
}

class V3NoiseTests final : public juce::UnitTest
{
public:
    V3NoiseTests() : juce::UnitTest("V3 noise generator", "V3") {}

    void runTest() override
    {
        beginTest("descriptor_lists_the_four_kinds");
        {
            const auto& d = NoiseGenerator::descriptor();
            expectEquals(juce::String(d.name), juce::String("Noise"));
            expectEquals(d.usedParams, static_cast<int>(NoiseGenerator::paramCount));
            expectEquals(juce::String(d.params[NoiseGenerator::type].options), juce::String("White|Pink|Band|Chip"));
        }

        beginTest("every_kind_sits_near_a_saw_after_decimation");
        {
            // A saw at the same slot level, centred: the reference.
            SlotMemory vaMemory;
            vaMemory.allocate(VaGenerator::memoryFloats(kVoiceRate, kSubBlock));
            auto saw = std::make_unique<VaGenerator>();
            saw->prepare(kVoiceRate, kSubBlock, vaMemory);
            saw->noteOn(NoteContext {});
            SlotParams sp;
            sp.p[VaGenerator::shape] = 2.0f;
            sp.p[VaGenerator::pulseWidth] = 0.5f;
            sp.p[VaGenerator::unison] = 1.0f;
            sp.p[VaGenerator::mix] = 0.67f;
            sp.frequencyHz = 220.0f;
            std::vector<float> l(96000), r(96000);
            for (int s = 0; s < 96000; s += kSubBlock)
                saw->process(l.data() + s, r.data() + s, kSubBlock, sp);
            const double reference = rms(decimate(l), 1000);

            struct Case { Kind kind; float hz; float res; float chip; };
            for (const auto c : { Case { Kind::white, 440.0f, 0.3f, 0.0f }, Case { Kind::pink, 440.0f, 0.3f, 0.0f },
                                  Case { Kind::band, 200.0f, 0.0f, 0.0f }, Case { Kind::band, 3000.0f, 1.0f, 0.0f },
                                  Case { Kind::band, 440.0f, 0.5f, 0.0f }, Case { Kind::chip, 440.0f, 0.3f, 0.0f },
                                  Case { Kind::chip, 220.0f, 0.3f, 1.0f } })
            {
                auto noise = makeNoise();
                auto p = noiseParams(c.kind, c.hz);
                p.p[NoiseGenerator::resonance] = c.res;
                p.p[NoiseGenerator::chipMode] = c.chip;
                const double level = rms(decimate(render(noise->generator, p, 192000).left), 2000);
                const double db = 20.0 * std::log10(level / reference);
                logMessage("kind " + juce::String(static_cast<int>(c.kind)) + " at " + juce::String(c.hz) + " Hz res " + juce::String(c.res)
                           + ": " + juce::String(db, 2) + " dB against a saw");
                expectLessThan(std::abs(db), 4.0, "noise level against a saw");
            }
        }

        beginTest("pink_has_equal_power_per_octave_and_white_rises_three_db");
        {
            auto noise = makeNoise();
            const auto pink = spectrum(render(noise->generator, noiseParams(Kind::pink), 96000 * 8).left);
            const auto white = spectrum(render(noise->generator, noiseParams(Kind::white), 96000 * 8).left);
            std::vector<double> pinkOctaves, whiteOctaves;
            for (double f = 62.5; f < 16000.0; f *= 2.0)
            {
                pinkOctaves.push_back(bandPowerDb(pink, kVoiceRate, f, 2.0 * f));
                whiteOctaves.push_back(bandPowerDb(white, kVoiceRate, f, 2.0 * f));
            }
            const auto [low, high] = std::minmax_element(pinkOctaves.begin(), pinkOctaves.end());
            logMessage("pink octave spread " + juce::String(*high - *low, 2) + " dB");
            expectLessThan(*high - *low, 2.0, "pink: equal power per octave from 62 Hz to 16 kHz");
            const double whiteSlope = (whiteOctaves.back() - whiteOctaves.front()) / static_cast<double>(whiteOctaves.size() - 1);
            expectWithinAbsoluteError(whiteSlope, 3.01, 0.3, "white: +3 dB per octave");
        }

        beginTest("band_noise_centres_on_the_note");
        {
            for (float hz : { 440.0f, 1760.0f })
            {
                auto noise = makeNoise();
                auto p = noiseParams(Kind::band, hz);
                p.p[NoiseGenerator::resonance] = 1.0f;
                const double peak = peakFrequency(spectrum(render(noise->generator, p, 96000 * 4).left), kVoiceRate);
                expectWithinAbsoluteError(peak, static_cast<double>(hz), static_cast<double>(hz) * 0.03, "band centre");
            }
        }

        beginTest("the_register_has_the_nes_periods");
        {
            for (bool shortMode : { false, true })
            {
                std::uint32_t v = 1;
                int period = 0;
                do
                {
                    v = NoiseGenerator::stepShift(v, shortMode);
                    ++period;
                } while (v != 1 && period < 40000);
                expectEquals(period, shortMode ? 93 : 32767);
            }
        }

        beginTest("short_mode_repeats_at_the_note_period");
        {
            auto noise = makeNoise();
            auto p = noiseParams(Kind::chip, 375.0f); // 256 samples a period at 96 kHz
            p.p[NoiseGenerator::chipMode] = 1.0f;
            // Start the register on its 93-step cycle (the generator starts it at 1).
            const auto x = render(noise->generator, p, 96000).left;
            float worst = 0.0f;
            for (std::size_t i = 4000; i + 256 < x.size(); ++i)
                worst = std::max(worst, std::abs(x[i] - x[i + 256]));
            expectLessThan(worst, 1.0e-3f, "a Short register clocked at 93 times the note repeats at the note");
        }

        beginTest("the_register_steps_are_band_limited");
        {
            // A fast clock: a naive register would fold its steps back below 20 kHz.
            auto noise = makeNoise();
            auto p = noiseParams(Kind::chip, 2000.0f);
            p.p[NoiseGenerator::clock] = 12.0f; // 372 kHz, about four steps a sample
            const auto x = render(noise->generator, p, 96000 * 2).left;
            float largest = 0.0f;
            for (float v : x)
                largest = std::max(largest, std::abs(v));
            expectLessThan(largest, 0.75f, "band-limited steps stay near the +-0.5 level");
            expect(rms(x, 1000) > 0.05, "it still sounds");
        }

        beginTest("width_decorrelates_and_tone_darkens");
        {
            auto correlation = [](const Stereo& s)
            {
                double lr = 0.0, ll = 0.0, rr = 0.0;
                for (std::size_t i = 100; i < s.left.size(); ++i)
                {
                    lr += static_cast<double>(s.left[i]) * s.right[i];
                    ll += static_cast<double>(s.left[i]) * s.left[i];
                    rr += static_cast<double>(s.right[i]) * s.right[i];
                }
                return lr / std::sqrt(ll * rr);
            };
            auto noise = makeNoise();
            auto p = noiseParams(Kind::white);
            expectGreaterThan(correlation(render(noise->generator, p, 48000)), 0.999);
            p.p[NoiseGenerator::width] = 1.0f;
            expectLessThan(std::abs(correlation(render(noise->generator, p, 48000))), 0.05);

            auto difference = [](const std::vector<float>& x)
            {
                double sum = 0.0;
                for (std::size_t i = 1; i < x.size(); ++i)
                    sum += std::abs(x[i] - x[i - 1]);
                return sum / static_cast<double>(x.size());
            };
            p.p[NoiseGenerator::width] = 0.0f;
            const double open = difference(render(noise->generator, p, 48000).left);
            p.p[NoiseGenerator::tone] = 0.0f;
            const double dark = difference(render(noise->generator, p, 48000).left);
            expectLessThan(dark, 0.1 * open, "tone at zero leaves only the lows");
        }

        beginTest("voices_get_different_noise");
        {
            auto a = makeNoise();
            auto b = makeNoise();
            const auto x = render(a->generator, noiseParams(Kind::white), 4096).left;
            const auto y = render(b->generator, noiseParams(Kind::white), 4096).left;
            int same = 0;
            for (std::size_t i = 10; i < x.size(); ++i)
                same += x[i] == y[i] ? 1 : 0;
            expectLessThan(same, 10);
        }
    }
};

static V3NoiseTests v3NoiseTests;
