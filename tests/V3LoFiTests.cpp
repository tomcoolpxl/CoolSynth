#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

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

        beginTest("hiss_and_crackle_follow_the_signal");
        {
            auto p = neutral();
            p.hiss = 1.0f;
            p.crackle = 1.0f;
            const auto silence = run(p, std::vector<float>(48000, 0.0f));
            float loudest = 0.0f;
            for (float v : silence)
                loudest = std::max(loudest, std::abs(v));
            expectLessThan(loudest, 1.0e-6f, "silence stays silent");

            const auto x = sine(200.0, 0.3f, static_cast<int>(kRate * 2));
            const auto clean = run(neutral(), x);
            const auto dirty = run(p, x);
            // A high-passed difference picks up the hiss and the clicks, not the 200 Hz tone.
            double hissEnergy = 0.0, cleanEnergy = 0.0;
            int clicks = 0;
            for (std::size_t i = 4800; i + 1 < x.size(); ++i)
            {
                const double d = (dirty[i] - dirty[i - 1]) - (clean[i] - clean[i - 1]);
                hissEnergy += d * d;
                const double c = clean[i] - clean[i - 1];
                cleanEnergy += c * c;
                if (std::abs(d) > 0.05)
                    ++clicks;
            }
            expectGreaterThan(hissEnergy, 0.01 * cleanEnergy, "audible noise under the tone");
            expectGreaterThan(clicks, 20, "crackle clicks");
        }

        beginTest("bank_lofi_section");
        {
            const auto result = coolsynth::presets::parseBankFile("[L]\nlofi = on wow=0.4 flutter=0.3 hiss=0.2 crackle=0.1 bandwidth=0.6 bits=12 rate=22000 mix=1\n", "l.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "lofiEnabled=on", "lofiWow=0.4", "lofiBandwidth=0.6", "lofiBits=12", "lofiRate=22000" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted);
        }
    }
};

static V3LoFiTests v3LoFiTests;
