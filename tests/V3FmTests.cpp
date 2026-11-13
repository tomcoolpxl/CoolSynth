#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/HalfBand2x.h"
#include "synth/v3/Fm4Generator.h"
#include "synth/v3/FmGenerator.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;
    constexpr int kFftOrder = 15;
    constexpr int kFftSize = 1 << kFftOrder;

    struct Fm
    {
        SlotMemory memory;
        FmGenerator generator;
    };

    std::unique_ptr<Fm> makeFm()
    {
        auto f = std::make_unique<Fm>();
        f->memory.allocate(FmGenerator::memoryFloats(kVoiceRate, kSubBlock));
        f->generator.prepare(kVoiceRate, kSubBlock, f->memory);
        f->generator.noteOn(NoteContext {});
        return f;
    }

    SlotParams fmParams(float hz, float ratio, float index, float feedback = 0.0f)
    {
        SlotParams p;
        p.p[FmGenerator::ratio] = ratio;
        p.p[FmGenerator::fine] = 0.0f;
        p.p[FmGenerator::index] = index;
        p.p[FmGenerator::feedback] = feedback;
        p.p[FmGenerator::carrier] = 1.0f;
        p.p[FmGenerator::mix] = 0.0f;
        p.frequencyHz = hz;
        return p;
    }

    struct Rendered
    {
        std::vector<float> left, mono, wraps;
    };

    Rendered render(FmGenerator& g, const SlotParams& p, int samples, const std::vector<float>* modulator = nullptr)
    {
        Rendered out { std::vector<float>(static_cast<std::size_t>(samples)), std::vector<float>(static_cast<std::size_t>(samples)),
                       std::vector<float>(static_cast<std::size_t>(samples)) };
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            GeneratorLink link;
            if (modulator != nullptr)
                link.signal = modulator->data() + s;
            g.setLink(link);
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
            {
                out.left[static_cast<std::size_t>(s + i)] = l[static_cast<std::size_t>(i)];
                out.mono[static_cast<std::size_t>(s + i)] = g.link().signal[i];
                out.wraps[static_cast<std::size_t>(s + i)] = g.link().wraps[i];
            }
        }
        return out;
    }

    /** Magnitude spectrum of one Blackman-Harris window starting at `from`. */
    std::vector<double> spectrum(const std::vector<float>& x, std::size_t from)
    {
        juce::dsp::FFT fft(kFftOrder);
        std::vector<float> frame(2 * kFftSize, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * i / kFftSize;
            const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
            frame[static_cast<std::size_t>(i)] = static_cast<float>(x[from + static_cast<std::size_t>(i)] * w);
        }
        fft.performFrequencyOnlyForwardTransform(frame.data());
        return { frame.begin(), frame.begin() + kFftSize / 2 };
    }

    /** As spectrum(), for a signal at another sample rate. */
    std::vector<double> spectrumAt(const std::vector<float>& x, std::size_t from, double)
    {
        return spectrum(x, from);
    }

    double peakNear(const std::vector<double>& m, double hz)
    {
        const double bin = hz * kFftSize / kVoiceRate;
        double best = 0.0;
        for (int b = static_cast<int>(bin) - 3; b <= static_cast<int>(bin) + 3; ++b)
            if (b > 0 && b < static_cast<int>(m.size()))
                best = std::max(best, m[static_cast<std::size_t>(b)]);
        return best;
    }

    double powerAbove(const std::vector<double>& m, double hz)
    {
        double above = 0.0, total = 0.0;
        for (std::size_t b = 1; b < m.size(); ++b)
        {
            const double p = m[b] * m[b];
            total += p;
            if (static_cast<double>(b) * kVoiceRate / kFftSize > hz)
                above += p;
        }
        return 10.0 * std::log10(std::max(1.0e-30, above) / std::max(1.0e-30, total));
    }
}

class V3FmTests final : public juce::UnitTest
{
public:
    V3FmTests() : juce::UnitTest("V3 FM2 generator", "V3") {}

    void runTest() override
    {
        beginTest("index_zero_is_a_pure_sine_at_the_carrier");
        {
            auto fm = makeFm();
            const auto out = render(fm->generator, fmParams(440.0f, 3.0f, 0.0f), kFftSize + 4096);
            const auto m = spectrum(out.mono, 4096);
            const double fundamental = peakNear(m, 440.0);
            double others = 0.0;
            for (int k = 2; k <= 10; ++k)
                others = std::max(others, peakNear(m, 440.0 * k));
            expectLessThan(20.0 * std::log10(others / fundamental), -90.0, "no harmonics without modulation");
        }

        beginTest("sidebands_follow_the_bessel_functions");
        {
            // Ratio 3.5 at index 1: the carrier at J0(1) and the first sidebands at 2.5 f and 4.5 f at
            // J1(1). The note sits on an FFT bin so that every component does (no scalloping loss).
            auto fm = makeFm();
            const double f = 136.0 * kVoiceRate / kFftSize;
            const auto out = render(fm->generator, fmParams(static_cast<float>(f), 3.5f, 1.0f), kFftSize + 4096);
            const auto m = spectrum(out.mono, 4096);
            const double carrier = peakNear(m, f);
            const double upper = peakNear(m, 4.5 * f);
            const double lower = peakNear(m, 2.5 * f);
            const double expected = 20.0 * std::log10(0.44005 / 0.76520);
            expectWithinAbsoluteError(20.0 * std::log10(upper / carrier), expected, 0.3);
            expectWithinAbsoluteError(20.0 * std::log10(lower / carrier), expected, 0.3);
        }

        beginTest("the_ratio_and_fine_set_the_modulator_pitch");
        {
            auto fm = makeFm();
            auto p = fmParams(220.0f, 2.0f, 0.0f);
            p.p[FmGenerator::mix] = 1.0f; // the modulator alone
            p.p[FmGenerator::fine] = 50.0f;
            const auto m = spectrum(render(fm->generator, p, kFftSize + 4096).mono, 4096);
            const double expected = 440.0 * std::exp2(50.0 / 1200.0);
            std::size_t best = 1;
            for (std::size_t b = 1; b < m.size(); ++b)
                if (m[b] > m[best])
                    best = b;
            expectWithinAbsoluteError(static_cast<double>(best) * kVoiceRate / kFftSize, expected, kVoiceRate / kFftSize);
        }

        beginTest("a_high_index_leaves_no_aliases_in_the_audible_band");
        {
            expectEquals(FmGenerator::indexLimit(220.0, 220.0, kVoiceRate), 10.0, "low notes are never limited");
            // Carrier 2 kHz, modulator at the inharmonic ratio 4.37, index 10: capped, and after the
            // voice's decimation every peak below 20 kHz must be a true sideband, 2000 + 8740 n Hz.
            auto fm = makeFm();
            const auto p = fmParams(2000.0f, 4.37f, 10.0f);
            const auto out = render(fm->generator, p, 2 * kFftSize + 8192);
            const double modulatorHz = 2000.0 * 4.37;
            expectWithinAbsoluteError(fm->generator.effectiveIndexForTesting(), FmGenerator::indexLimit(2000.0, modulatorHz, kVoiceRate), 1.0e-6);
            expectLessThan(fm->generator.effectiveIndexForTesting(), 10.0, "the limit applies here");

            coolsynth::synth::HalfBandDecimator2x decimator;
            std::vector<float> host(out.mono.size() / 2);
            decimator.processBlock(out.mono.data(), host.data(), static_cast<int>(host.size()));
            const auto m = spectrumAt(host, 2048, kVoiceRate / 2.0);
            const double binHz = (kVoiceRate / 2.0) / kFftSize;
            std::vector<double> legit;
            for (int n = -12; n <= 12; ++n)
                legit.push_back(std::abs(2000.0 + modulatorHz * n));
            double strongest = 0.0, worstAlias = 0.0;
            for (std::size_t b = static_cast<std::size_t>(50.0 / binHz); b < static_cast<std::size_t>(20000.0 / binHz); ++b)
            {
                const double hz = static_cast<double>(b) * binHz;
                const bool nearSideband = std::any_of(legit.begin(), legit.end(), [&](double f) { return std::abs(f - hz) < 8.0 * binHz; });
                if (nearSideband)
                    strongest = std::max(strongest, m[b]);
                else
                    worstAlias = std::max(worstAlias, m[b]);
            }
            const double db = 20.0 * std::log10(std::max(1.0e-12, worstAlias) / strongest);
            logMessage("index limited to " + juce::String(fm->generator.effectiveIndexForTesting(), 2) + "; largest alias below 20 kHz: "
                       + juce::String(db, 1) + " dB against the strongest sideband");
            expectLessThan(db, -70.0);
        }

        beginTest("full_feedback_stays_bounded_and_clean_at_the_top");
        {
            auto fm = makeFm();
            auto p = fmParams(220.0f, 1.0f, 0.0f, 1.0f);
            p.p[FmGenerator::mix] = 1.0f;
            const auto out = render(fm->generator, p, kFftSize + 4096);
            float largest = 0.0f;
            for (float v : out.mono)
                largest = std::max(largest, std::abs(v));
            expectLessOrEqual(largest, 1.0f + 1.0e-6f);
            const double top = powerAbove(spectrum(out.mono, 4096), 40000.0);
            logMessage("full feedback: power above 40 kHz " + juce::String(top, 1) + " dB");
            expectLessThan(top, -40.0, "no buzz near half the sample rate");
        }

        beginTest("the_wrap_track_marks_each_carrier_cycle");
        {
            auto fm = makeFm();
            const auto out = render(fm->generator, fmParams(300.0f, 2.0f, 2.0f), 96000);
            int wraps = 0;
            for (float w : out.wraps)
                wraps += w > 0.0f ? 1 : 0;
            expect(std::abs(wraps - 300) <= 1, "wraps in one second: " + juce::String(wraps));
        }

        beginTest("an_index_ramp_does_not_step");
        {
            auto fm = makeFm();
            auto p = fmParams(110.0f, 1.0f, 0.0f);
            std::array<float, kSubBlock> l {}, r {};
            float largest = 0.0f, previous = 0.0f;
            for (int block = 0; block < 300; ++block)
            {
                p.p[FmGenerator::index] = block % 2 == 0 ? 0.0f : 3.0f; // a jump every sub-block
                fm->generator.process(l.data(), r.data(), kSubBlock, p);
                for (float v : l)
                {
                    largest = std::max(largest, std::abs(v - previous));
                    previous = v;
                }
            }
            // A 110 Hz sine changes by at most 0.005 per sample; index jumps spread over a sub-block
            // add a few hundredths, an unramped jump would add up to 1.
            expectLessThan(largest, 0.2f);
        }

        beginTest("combine_fm_moves_the_carrier");
        {
            auto plain = makeFm();
            auto driven = makeFm();
            std::vector<float> modulator(9600);
            for (std::size_t i = 0; i < modulator.size(); ++i)
                modulator[i] = static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 660.0 * i / kVoiceRate));
            auto p = fmParams(220.0f, 1.0f, 0.0f);
            const auto a = render(plain->generator, p, 9600, &modulator);
            p.combine = Combine::fm;
            p.combineAmount = 0.5f;
            const auto b = render(driven->generator, p, 9600, &modulator);
            double difference = 0.0;
            for (std::size_t i = 0; i < a.mono.size(); ++i)
                difference = std::max(difference, static_cast<double>(std::abs(a.mono[i] - b.mono[i])));
            expectGreaterThan(difference, 0.5);
        }
    }
};

static V3FmTests v3FmTests;

namespace
{
    struct Fm4
    {
        SlotMemory memory;
        Fm4Generator generator;
    };

    std::unique_ptr<Fm4> makeFm4()
    {
        auto f = std::make_unique<Fm4>();
        f->memory.allocate(Fm4Generator::memoryFloats(kVoiceRate, kSubBlock));
        f->generator.prepare(kVoiceRate, kSubBlock, f->memory);
        f->generator.noteOn(NoteContext {});
        return f;
    }

    SlotParams fm4Params(int algorithm, float hz)
    {
        SlotParams p;
        p.p[Fm4Generator::algorithm] = static_cast<float>(algorithm);
        p.p[Fm4Generator::ratio2] = 1.0f;
        p.p[Fm4Generator::ratio3] = 1.0f;
        p.p[Fm4Generator::ratio4] = 1.0f;
        p.p[Fm4Generator::level2] = 0.0f;
        p.p[Fm4Generator::level3] = 0.0f;
        p.p[Fm4Generator::level4] = 0.0f;
        p.p[Fm4Generator::feedback] = 0.0f;
        p.frequencyHz = hz;
        return p;
    }

    std::vector<float> renderFm4(Fm4Generator& g, const SlotParams& p, int samples)
    {
        std::vector<float> out(static_cast<std::size_t>(samples));
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.setLink({});
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
                out[static_cast<std::size_t>(s + i)] = g.link().signal[i];
        }
        return out;
    }
}

class V3Fm4Tests final : public juce::UnitTest
{
public:
    V3Fm4Tests() : juce::UnitTest("V3 FM4 generator", "V3") {}

    void runTest() override
    {
        beginTest("algorithms_route_as_documented");
        {
            const auto pairs = Fm4Generator::routing(3);
            expect(pairs.carrier[0] && pairs.carrier[2] && ! pairs.carrier[1] && ! pairs.carrier[3]);
            expect(pairs.modulatedBy[0][1] && pairs.modulatedBy[2][3] && ! pairs.modulatedBy[0][3]);
            const auto stack = Fm4Generator::routing(0);
            expect(stack.modulatedBy[2][3] && stack.modulatedBy[1][2] && stack.modulatedBy[0][1]);
            const auto organ = Fm4Generator::routing(5);
            for (bool c : organ.carrier)
                expect(c);
        }

        beginTest("two_pairs_with_one_pair_silent_equals_the_two_operator_generator");
        {
            auto four = makeFm4();
            auto p4 = fm4Params(3, 330.0f);
            p4.p[Fm4Generator::ratio2] = 2.0f;
            p4.p[Fm4Generator::level2] = 0.25f; // index 2
            const auto a = renderFm4(four->generator, p4, 9600);
            auto two = makeFm();
            const auto b = render(two->generator, fmParams(330.0f, 2.0f, 2.0f), 9600).mono;
            float worst = 0.0f;
            for (std::size_t i = 0; i < a.size(); ++i)
                worst = std::max(worst, std::abs(a[i] - b[i]));
            expectLessThan(worst, 1.0e-5f);
        }

        beginTest("the_organ_sums_four_equal_partials");
        {
            auto fm = makeFm4();
            const double f = 64.0 * kVoiceRate / kFftSize; // every partial on a bin
            auto p = fm4Params(5, static_cast<float>(f));
            p.p[Fm4Generator::ratio2] = 2.0f;
            p.p[Fm4Generator::ratio3] = 3.0f;
            p.p[Fm4Generator::ratio4] = 4.0f;
            p.p[Fm4Generator::level2] = p.p[Fm4Generator::level3] = p.p[Fm4Generator::level4] = 1.0f;
            const auto m = spectrum(renderFm4(fm->generator, p, kFftSize + 4096), 4096);
            const double first = peakNear(m, f);
            for (int k = 2; k <= 4; ++k)
                expectWithinAbsoluteError(20.0 * std::log10(peakNear(m, f * k) / first), 0.0, 0.1, "partial " + juce::String(k));
        }

        beginTest("modulation_is_limited_for_the_operator_it_drives_and_output_stays_bounded");
        {
            auto fm = makeFm4();
            auto p = fm4Params(0, 3000.0f);
            p.p[Fm4Generator::ratio2] = 5.0f;
            p.p[Fm4Generator::level2] = 1.0f;
            renderFm4(fm->generator, p, 2048);
            expectWithinAbsoluteError(fm->generator.indexForTesting(2), FmGenerator::indexLimit(3000.0, 15000.0, kVoiceRate), 1.0e-6);

            for (int algorithm = 0; algorithm < Fm4Generator::algorithmCount; ++algorithm)
            {
                auto g = makeFm4();
                auto q = fm4Params(algorithm, 220.0f);
                q.p[Fm4Generator::ratio2] = 3.0f;
                q.p[Fm4Generator::ratio3] = 7.0f;
                q.p[Fm4Generator::ratio4] = 0.5f;
                q.p[Fm4Generator::level2] = q.p[Fm4Generator::level3] = q.p[Fm4Generator::level4] = 1.0f;
                q.p[Fm4Generator::feedback] = 1.0f;
                float largest = 0.0f;
                bool finite = true;
                for (float v : renderFm4(g->generator, q, 48000))
                {
                    finite = finite && std::isfinite(v);
                    largest = std::max(largest, std::abs(v));
                }
                expect(finite);
                expectLessOrEqual(largest, 1.0f + 1.0e-5f, "algorithm " + juce::String(algorithm + 1));
            }
        }
    }
};

static V3Fm4Tests v3Fm4Tests;
