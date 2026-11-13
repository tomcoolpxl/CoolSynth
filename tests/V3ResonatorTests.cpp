#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/ResonatorProcessors.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 96000.0;
    constexpr int kSubBlock = 32;

    template <typename Processor>
    std::vector<float> run(Processor& processor, const SlotParams& p, std::vector<float> x)
    {
        std::vector<float> right = x;
        for (std::size_t s = 0; s < x.size(); s += kSubBlock)
        {
            const int n = static_cast<int>(std::min<std::size_t>(kSubBlock, x.size() - s));
            processor.process(x.data() + s, right.data() + s, n, p);
        }
        return x;
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

    SlotParams formantParams(float vowel, float size, float resonance)
    {
        SlotParams p;
        p.p[FormantProcessor::vowel] = vowel;
        p.p[FormantProcessor::size] = size;
        p.p[FormantProcessor::resonance] = resonance;
        p.p[FormantProcessor::mix] = 1.0f;
        return p;
    }

    double formantGainDb(const SlotParams& p, double hz)
    {
        SlotMemory memory;
        auto f = std::make_unique<FormantProcessor>();
        f->prepare(kRate, kSubBlock, memory);
        const auto out = run(*f, p, sine(hz, 0.01f, 48000));
        return 20.0 * std::log10(rms(out, 24000) / (0.01 / std::sqrt(2.0)));
    }

    SlotParams combParams(float feedback, bool negative, float tune = 0.0f)
    {
        SlotParams p;
        p.p[CombProcessor::tune] = tune;
        p.p[CombProcessor::feedback] = feedback;
        p.p[CombProcessor::damping] = 0.0f;
        p.p[CombProcessor::keyTrack] = 1.0f;
        p.p[CombProcessor::polarity] = negative ? 1.0f : 0.0f;
        p.p[CombProcessor::mix] = 1.0f;
        p.frequencyHz = 220.0f;
        return p;
    }

    struct Comb
    {
        SlotMemory memory;
        CombProcessor processor;
    };

    std::unique_ptr<Comb> makeComb()
    {
        auto c = std::make_unique<Comb>();
        c->memory.allocate(CombProcessor::memoryFloats(kRate, kSubBlock));
        c->processor.prepare(kRate, kSubBlock, c->memory);
        return c;
    }

    /** Positions and signs of the echoes of an impulse through the comb. */
    std::vector<std::pair<double, float>> echoes(CombProcessor& comb, const SlotParams& p, int count)
    {
        std::vector<float> x(static_cast<std::size_t>(96000 / 4), 0.0f);
        x[100] = 0.5f;
        const auto y = run(comb, p, x);
        std::vector<std::pair<double, float>> found;
        for (std::size_t i = 102; i + 1 < y.size() && static_cast<int>(found.size()) < count; ++i)
        {
            const float a = std::abs(y[i]);
            // Above 0.1: the echoes (0.5 times 0.9 per pass), not the small lobes the fractional read leaves beside them.
            if (a > 0.1f && a >= std::abs(y[i - 1]) && a >= std::abs(y[i + 1]))
            {
                // Sub-sample peak by a parabola through the three samples.
                const double l = std::abs(y[i - 1]), c = a, r = std::abs(y[i + 1]);
                const double offset = 0.5 * (l - r) / (l - 2.0 * c + r);
                found.emplace_back(static_cast<double>(i) + offset - 100.0, y[i]);
                i += 20;
            }
        }
        return found;
    }
}

class V3ResonatorTests final : public juce::UnitTest
{
public:
    V3ResonatorTests() : juce::UnitTest("V3 formant and comb", "V3") {}

    void runTest() override
    {
        beginTest("each_vowel_peaks_at_its_formants");
        {
            const auto a = formantParams(0.0f, 1.0f, 0.6f);
            expectGreaterThan(formantGainDb(a, 730.0) - formantGainDb(a, 1500.0), 6.0, "a: F1 above the gap");
            expectGreaterThan(formantGainDb(a, 1090.0) - formantGainDb(a, 1500.0), 3.0, "a: F2 above the gap");
            const auto i = formantParams(2.0f, 1.0f, 0.6f);
            expectGreaterThan(formantGainDb(i, 270.0) - formantGainDb(i, 730.0), 6.0, "i: low F1");
            expectGreaterThan(formantGainDb(i, 2290.0) - formantGainDb(i, 1500.0), 6.0, "i: high F2");
        }

        beginTest("size_scales_the_formants");
        {
            const auto big = formantParams(0.0f, 2.0f, 0.6f); // twice the size: formants an octave down
            expectGreaterThan(formantGainDb(big, 365.0) - formantGainDb(big, 730.0), 3.0);
        }

        beginTest("a_vowel_sweep_is_smooth_and_full_resonance_is_bounded");
        {
            SlotMemory memory;
            auto f = std::make_unique<FormantProcessor>();
            f->prepare(kRate, kSubBlock, memory);
            auto p = formantParams(0.0f, 1.0f, 0.5f);
            auto x = sine(500.0, 0.5f, 96000);
            std::vector<float> r = x;
            float largestStep = 0.0f, previous = 0.0f;
            for (std::size_t s = 0; s < x.size(); s += kSubBlock)
            {
                p.p[FormantProcessor::vowel] = 4.0f * static_cast<float>(s) / static_cast<float>(x.size());
                f->process(x.data() + s, r.data() + s, kSubBlock, p);
                for (std::size_t i = s; i < s + kSubBlock; ++i)
                {
                    if (i > 2000)
                        largestStep = std::max(largestStep, std::abs(x[i] - previous));
                    previous = x[i];
                }
            }
            expectLessThan(largestStep, 0.1f, "no clicks while the vowel moves");

            auto loud = std::make_unique<FormantProcessor>();
            loud->prepare(kRate, kSubBlock, memory);
            juce::Random random(3);
            std::vector<float> noise(96000);
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            float peak = 0.0f;
            for (float v : run(*loud, formantParams(1.0f, 1.0f, 1.0f), noise))
                peak = std::max(peak, std::abs(v));
            expect(std::isfinite(peak) && peak < 8.0f, "peak " + juce::String(peak));
        }

        beginTest("comb_echoes_land_at_the_note_period");
        {
            auto comb = makeComb();
            const auto found = echoes(comb->processor, combParams(0.9f, false), 5);
            const double period = kRate / 220.0;
            expect(found.size() >= 5);
            for (std::size_t k = 0; k < found.size(); ++k)
            {
                expectWithinAbsoluteError(found[k].first, period * static_cast<double>(k + 1), 1.0, "echo " + juce::String(static_cast<int>(k + 1)));
                expectGreaterThan(found[k].second, 0.0f, "positive polarity keeps the sign");
            }
        }

        beginTest("negative_polarity_alternates_and_tune_moves_the_period");
        {
            auto comb = makeComb();
            const auto found = echoes(comb->processor, combParams(0.9f, true), 4);
            expect(found.size() >= 4);
            for (std::size_t k = 0; k < found.size(); ++k)
                expect((found[k].second > 0.0f) == (k % 2 == 1), "echo " + juce::String(static_cast<int>(k + 1)) + " sign alternates");

            auto up = makeComb();
            const auto octave = echoes(up->processor, combParams(0.9f, false, 12.0f), 2);
            expect(octave.size() >= 2);
            if (octave.size() >= 2)
                expectWithinAbsoluteError(octave[1].first - octave[0].first, kRate / 440.0, 1.0);
        }

        beginTest("full_feedback_stays_bounded");
        {
            auto comb = makeComb();
            auto p = combParams(0.98f, false);
            juce::Random random(11);
            std::vector<float> noise(96000);
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            float peak = 0.0f;
            bool finite = true;
            for (float v : run(comb->processor, p, noise))
            {
                finite = finite && std::isfinite(v);
                peak = std::max(peak, std::abs(v));
            }
            expect(finite);
            expectLessThan(peak, 2.5f);
        }
    }
};

static V3ResonatorTests v3ResonatorTests;
