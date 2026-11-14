#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/PlateReverb.h"

namespace
{
    using coolsynth::synth::PlateReverb;

    constexpr double kRate = 48000.0;

    std::unique_ptr<PlateReverb> makeReverb(float preDelayMs = 0.0f, float lowCut = 20.0f, float shimmer = 0.0f, float size = 0.6f)
    {
        auto r = std::make_unique<PlateReverb>();
        r->prepare(kRate);
        r->setSize(size);
        r->setDamping(0.3f);
        r->setPreDelay(preDelayMs);
        r->setLowCut(lowCut);
        r->setShimmer(shimmer);
        return r;
    }

    /** Runs a mono signal through the reverb; returns the left output. */
    std::vector<float> run(PlateReverb& reverb, std::vector<float> input)
    {
        std::vector<float> right = input;
        for (std::size_t s = 0; s < input.size(); s += 256)
        {
            const int n = static_cast<int>(std::min<std::size_t>(256, input.size() - s));
            reverb.process(input.data() + s, right.data() + s, n);
        }
        return input;
    }

    int onset(const std::vector<float>& y, float threshold)
    {
        for (std::size_t i = 0; i < y.size(); ++i)
            if (std::abs(y[i]) > threshold)
                return static_cast<int>(i);
        return -1;
    }

    double energyNear(const std::vector<float>& x, std::size_t from, std::size_t to, double hz)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = from; i < to; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * hz * static_cast<double>(i) / kRate;
            const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * static_cast<double>(i - from) / static_cast<double>(to - from));
            re += x[i] * w * std::cos(t);
            im += x[i] * w * std::sin(t);
        }
        return re * re + im * im;
    }
}

class V3ReverbTests final : public juce::UnitTest
{
public:
    V3ReverbTests() : juce::UnitTest("V3 plate: pre-delay, low cut, shimmer", "V3") {}

    void runTest() override
    {
        beginTest("pre_delay_moves_the_onset_by_its_time");
        {
            std::vector<float> impulse(48000, 0.0f);
            impulse[10] = 1.0f;
            auto plain = makeReverb(0.0f);
            auto delayed = makeReverb(100.0f);
            const int a = onset(run(*plain, impulse), 1.0e-4f);
            const int b = onset(run(*delayed, impulse), 1.0e-4f);
            expect(a > 0 && b > 0);
            expectWithinAbsoluteError(b - a, 4800, 2, "100 ms at 48 kHz");
        }

        beginTest("low_cut_takes_the_lows_out_of_the_reverb");
        {
            std::vector<float> lows(96000);
            for (std::size_t i = 0; i < lows.size(); ++i)
                lows[i] = 0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * static_cast<double>(i) / kRate));
            auto open = makeReverb(0.0f, 20.0f);
            auto cut = makeReverb(0.0f, 600.0f);
            const auto a = run(*open, lows);
            const auto b = run(*cut, lows);
            double ea = 0.0, eb = 0.0;
            for (std::size_t i = 48000; i < a.size(); ++i)
            {
                ea += static_cast<double>(a[i]) * a[i];
                eb += static_cast<double>(b[i]) * b[i];
            }
            expectLessThan(10.0 * std::log10(eb / ea), -15.0, "60 Hz under a 600 Hz low cut");
        }

        beginTest("shimmer_adds_the_octave_and_still_dies_away");
        {
            // A 440 Hz burst, then silence: with shimmer the tail carries 880 Hz, without it hardly any.
            std::vector<float> burst(48000 * 8, 0.0f);
            for (std::size_t i = 0; i < 24000; ++i)
                burst[i] = 0.4f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * static_cast<double>(i) / kRate));
            auto plain = makeReverb(0.0f, 20.0f, 0.0f, 0.8f);
            auto shimmer = makeReverb(0.0f, 20.0f, 0.8f, 0.8f);
            const auto a = run(*plain, burst);
            const auto b = run(*shimmer, burst);
            const double plainRatio = energyNear(a, 36000, 60000, 880.0) / energyNear(a, 36000, 60000, 440.0);
            const double shimmerRatio = energyNear(b, 36000, 60000, 880.0) / energyNear(b, 36000, 60000, 440.0);
            logMessage("octave over fundamental in the tail: plain " + juce::String(10.0 * std::log10(plainRatio), 1)
                       + " dB, shimmer " + juce::String(10.0 * std::log10(shimmerRatio), 1) + " dB");
            expectGreaterThan(10.0 * std::log10(shimmerRatio / plainRatio), 10.0);

            // A long tail is the point of shimmer; it must keep falling, never build up.
            float second = 0.0f, fifth = 0.0f, eighth = 0.0f;
            bool finite = true;
            for (std::size_t i = 0; i < b.size(); ++i)
            {
                finite = finite && std::isfinite(b[i]);
                const float v = std::abs(b[i]);
                if (i >= 48000 && i < 96000)
                    second = std::max(second, v);
                else if (i >= 4 * 48000 && i < 5 * 48000)
                    fifth = std::max(fifth, v);
                else if (i >= 7 * 48000)
                    eighth = std::max(eighth, v);
            }
            expect(finite);
            expectLessThan(fifth, 0.7f * second, "falling by the fifth second");
            expectLessThan(eighth, 0.7f * fifth, "and still falling by the eighth");
        }

        beginTest("bank_reverb_slot_takes_the_new_keys");
        {
            const auto result = coolsynth::presets::parseBankFile("[R]\nf1 = Reverb predelay=80 lowcut=150 modulation=0.7 shimmer=0.6 mix=0.4\n", "r.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "f1.type=Reverb", "f1.p4=80", "f1.p5=150", "f1.p6=0.7", "f1.p7=0.6" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted);
        }
    }
};

static V3ReverbTests v3ReverbTests;
