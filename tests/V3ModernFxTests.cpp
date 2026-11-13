#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/GlobalFxRack.h"

namespace
{
    using namespace coolsynth::synth;

    constexpr double kRate = 48000.0;

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

    template <typename Effect, typename Parameters>
    std::vector<float> run(Effect& effect, const Parameters& p, const std::vector<float>& input)
    {
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
            effect.process(block, p);
            for (int i = 0; i < n; ++i)
                out[s + static_cast<std::size_t>(i)] = block.getSample(0, i);
        }
        return out;
    }
}

class V3ModernFxTests final : public juce::UnitTest
{
public:
    V3ModernFxTests() : juce::UnitTest("V3 pump, EQ and flanger", "V3") {}

    void runTest() override
    {
        beginTest("the_pump_ducks_at_the_beat_and_recovers_by_its_release");
        {
            const double period = 24000.0; // a quarter note at 120 BPM
            const double attack = 96.0;    // 2 ms
            expectWithinAbsoluteError(Pump::gainAt(0.0, 0.6f, 0.5f, period, attack), 1.0f, 1.0e-6f); // the very start of the attack
            expectWithinAbsoluteError(Pump::gainAt(96.0 / period, 0.6f, 0.5f, period, attack), 1.0f - 0.6f * 0.984f, 0.01f);
            expectWithinAbsoluteError(Pump::gainAt(0.25, 0.6f, 0.5f, period, attack), 1.0f - 0.6f * 0.25f, 1.0e-4f);
            expectWithinAbsoluteError(Pump::gainAt(0.5, 0.6f, 0.5f, period, attack), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(Pump::gainAt(1.75, 0.6f, 0.5f, period, attack), 1.0f, 1.0e-6f);

            auto pump = std::make_unique<Pump>();
            pump->prepare(kRate);
            PumpParameters p;
            p.enabled = true;
            p.depth = 0.6f;
            p.release = 0.5f;
            p.phasePerSample = 1.0 / period;
            juce::AudioBuffer<float> block(2, 512);
            float lowest = 1.0f, largestStep = 0.0f, previous = 1.0f;
            for (int start = 0; start < 48000; start += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        block.setSample(ch, i, 1.0f);
                p.phaseAtStart = start / period;
                pump->process(block, p);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = block.getSample(0, i);
                    lowest = std::min(lowest, v);
                    largestStep = std::max(largestStep, std::abs(v - previous));
                    previous = v;
                }
            }
            expectWithinAbsoluteError(lowest, 0.4f, 0.02f, "depth 0.6 leaves 0.4 at the bottom");
            expectLessThan(largestStep, 0.01f, "the duck is a 2 ms ramp, not a click");
        }

        beginTest("the_eq_shelves_and_tilt_land_where_they_say");
        {
            auto eq = std::make_unique<MasterEq>();
            eq->prepare(kRate, 2);
            EqParameters p;
            p.enabled = true;
            const auto x = sine(1000.0, 0.5f, 4800);
            const auto flat = run(*eq, p, x);
            float error = 0.0f;
            for (std::size_t i = 0; i < x.size(); ++i)
                error = std::max(error, std::abs(flat[i] - x[i]));
            expectLessThan(error, 1.0e-6f, "all at zero is transparent");

            p.lowDb = 6.0f;
            run(*eq, p, x);
            expectWithinAbsoluteError(eq->responseDb(30.0), 6.0, 0.5);
            expectWithinAbsoluteError(eq->responseDb(5000.0), 0.0, 0.3);

            p.lowDb = 0.0f;
            p.highDb = -9.0f;
            run(*eq, p, x);
            expectWithinAbsoluteError(eq->responseDb(20000.0), -9.0, 0.7);
            expectWithinAbsoluteError(eq->responseDb(200.0), 0.0, 0.3);

            p.highDb = 0.0f;
            p.tiltDb = 6.0f;
            run(*eq, p, x);
            expectWithinAbsoluteError(eq->responseDb(50.0), -3.0, 0.4);
            expectWithinAbsoluteError(eq->responseDb(18000.0), 3.0, 0.4);
            expectWithinAbsoluteError(eq->responseDb(1000.0), 0.0, 0.2);

            // What it measures is what it does: a 30 Hz sine through the low shelf.
            auto second = std::make_unique<MasterEq>();
            second->prepare(kRate, 2);
            EqParameters low;
            low.enabled = true;
            low.lowDb = 6.0f;
            const auto bass = sine(30.0, 0.2f, 96000);
            const double measured = 20.0 * std::log10(rms(run(*second, low, bass), 48000) / rms(bass, 48000));
            expectWithinAbsoluteError(measured, second->responseDb(30.0), 0.2);
        }

        beginTest("the_flanger_combs_and_stays_bounded");
        {
            // Depth 0 holds the delay at 0.3 ms (14.4 samples): notches at odd multiples of 1667 Hz.
            FlangerParameters p;
            p.enabled = true;
            p.depth = 0.0f;
            p.feedback = 0.0f;
            p.mix = 1.0f;
            auto notch = std::make_unique<Flanger>();
            notch->prepare(kRate, 2);
            const double inNotch = rms(run(*notch, p, sine(1666.7, 0.5f, 24000)), 12000);
            auto peak = std::make_unique<Flanger>();
            peak->prepare(kRate, 2);
            const double onPeak = rms(run(*peak, p, sine(3333.3, 0.5f, 24000)), 12000);
            expectGreaterThan(20.0 * std::log10(onPeak / inNotch), 25.0);

            p.depth = 1.0f;
            p.feedback = 0.95f;
            p.rateHz = 2.0f;
            auto wild = std::make_unique<Flanger>();
            wild->prepare(kRate, 2);
            juce::Random random(5);
            std::vector<float> noise(96000);
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            float largest = 0.0f;
            bool finite = true;
            for (float v : run(*wild, p, noise))
            {
                finite = finite && std::isfinite(v);
                largest = std::max(largest, std::abs(v));
            }
            expect(finite);
            expectLessThan(largest, 2.0f);
        }

        beginTest("the_rack_runs_the_new_stages_and_bank_files_name_them");
        {
            auto rack = std::make_unique<GlobalFxRack>();
            rack->prepare(kRate, 512, 2);
            ExtraFxParameters extra;
            extra.pump.enabled = true;
            extra.pump.depth = 1.0f;
            extra.pump.release = 1.0f;
            extra.pump.phasePerSample = 1.0 / 24000.0;
            extra.pump.phaseAtStart = 0.01; // just after a beat: nearly fully ducked
            juce::AudioBuffer<float> block(2, 64);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 64; ++i)
                    block.setSample(ch, i, 0.5f);
            rack->process(block, {}, {}, {}, {}, {}, {}, extra);
            expectLessThan(block.getSample(0, 32), 0.05f, "the pump at the end of the rack ducks the output");

            const auto result = coolsynth::presets::parseBankFile(
                "[F]\nflanger = on rate=0.4 depth=0.8 feedback=-0.6 mix=0.5\neq = on low=3 tilt=-2 high=4\npump = on depth=0.6 division=1/4 release=0.5\n",
                "f.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "flangerEnabled=on", "flangerFeedback=-0.6", "eqTilt=-2", "eqHigh=4", "pumpEnabled=on", "pumpDivision=1/4", "pumpRelease=0.5" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted);
        }
    }
};

static V3ModernFxTests v3ModernFxTests;
