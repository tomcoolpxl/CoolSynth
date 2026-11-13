#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "synth/HalfBand2x.h"
#include "synth/v3/ModMatrix.h"
#include "synth/v3/ShaperProcessors.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 96000.0;
    constexpr int kSubBlock = 32;
    constexpr int kFftOrder = 15;
    constexpr int kFftSize = 1 << kFftOrder;

    std::vector<float> sine(double hz, float amplitude, int samples)
    {
        std::vector<float> x(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
            x[static_cast<std::size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
        return x;
    }

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

    /** Largest spectral peak below 20 kHz that is not a harmonic of f, after the voice's decimation, against the fundamental. */
    double worstAliasDb(const std::vector<float>& voiceRate, double f)
    {
        coolsynth::synth::HalfBandDecimator2x decimator;
        std::vector<float> host(voiceRate.size() / 2);
        decimator.processBlock(voiceRate.data(), host.data(), static_cast<int>(host.size()));
        juce::dsp::FFT fft(kFftOrder);
        std::vector<float> frame(2 * kFftSize, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * i / kFftSize;
            const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
            frame[static_cast<std::size_t>(i)] = static_cast<float>(host[2048 + static_cast<std::size_t>(i)] * w);
        }
        fft.performFrequencyOnlyForwardTransform(frame.data());
        const double binHz = (kRate / 2.0) / kFftSize;
        double fundamental = 0.0, worst = 0.0;
        for (int b = static_cast<int>(50.0 / binHz); b < static_cast<int>(20000.0 / binHz); ++b)
        {
            const double hz = b * binHz;
            const double k = std::round(hz / f);
            const bool harmonic = k >= 1.0 && std::abs(hz - k * f) < 8.0 * binHz;
            const double m = frame[static_cast<std::size_t>(b)];
            if (harmonic && k == 1.0)
                fundamental = std::max(fundamental, m);
            else if (! harmonic)
                worst = std::max(worst, m);
        }
        return 20.0 * std::log10(std::max(1.0e-12, worst) / fundamental);
    }
}

class V3ShaperTests final : public juce::UnitTest
{
public:
    V3ShaperTests() : juce::UnitTest("V3 crusher and folder", "V3") {}

    void runTest() override
    {
        SlotMemory memory;

        beginTest("the_crusher_rounds_to_its_bits_and_holds_at_its_rate");
        {
            auto crusher = std::make_unique<CrusherProcessor>();
            crusher->prepare(kRate, kSubBlock, memory);
            SlotParams p;
            p.p[CrusherProcessor::bits] = 4.0f;
            p.p[CrusherProcessor::rate] = 48000.0f;
            p.p[CrusherProcessor::mix] = 1.0f;
            const auto four = run(*crusher, p, sine(441.0, 0.9f, 9600));
            float offGrid = 0.0f;
            for (float v : four)
                offGrid = std::max(offGrid, std::abs(v / 0.125f - std::round(v / 0.125f)));
            expectLessThan(offGrid, 1.0e-4f, "sixteen levels, 0.125 apart");

            p.p[CrusherProcessor::bits] = 16.0f;
            const auto x = sine(441.0, 0.9f, 9600);
            const auto clean = run(*crusher, p, x);
            float error = 0.0f;
            for (std::size_t i = 0; i < x.size(); ++i)
                error = std::max(error, std::abs(clean[i] - x[i]));
            expectLessThan(error, 2.0e-5f, "16 bits at the top rate is nearly transparent");

            p.p[CrusherProcessor::rate] = 1000.0f;
            crusher->reset();
            const auto held = run(*crusher, p, sine(441.0, 0.9f, 96000));
            int changes = 0;
            for (std::size_t i = 1; i < held.size(); ++i)
                changes += held[i] != held[i - 1] ? 1 : 0;
            expectLessOrEqual(changes, 1001, "at most 1000 new values a second");
            expectGreaterThan(changes, 900);

            p.p[CrusherProcessor::mix] = 0.0f;
            const auto dry = run(*crusher, p, x);
            for (std::size_t i = 0; i < x.size(); ++i)
                error = std::max(error, std::abs(dry[i] - x[i]));
            expectLessThan(error, 2.0e-5f, "mix 0 is dry");
        }

        beginTest("the_folder_adds_harmonics_with_drive_and_stays_bounded");
        {
            auto spectralSpread = [](const std::vector<float>& y)
            {
                double low = 0.0, high = 0.0;
                for (std::size_t i = 1; i < y.size(); ++i)
                {
                    low += static_cast<double>(y[i]) * y[i];
                    const double d = y[i] - y[i - 1];
                    high += d * d;
                }
                return high / low; // grows with the high-frequency content
            };
            double previous = 0.0;
            for (float drive : { 0.0f, 12.0f, 24.0f })
            {
                auto folder = std::make_unique<FolderProcessor>();
                folder->prepare(kRate, kSubBlock, memory);
                SlotParams p;
                p.p[FolderProcessor::drive] = drive;
                p.p[FolderProcessor::symmetry] = 0.0f;
                p.p[FolderProcessor::mix] = 1.0f;
                const auto y = run(*folder, p, sine(220.0, 0.8f, 48000));
                float peak = 0.0f;
                for (float v : y)
                    peak = std::max(peak, std::abs(v));
                expectLessOrEqual(peak, 1.05f, "drive " + juce::String(drive));
                const double spread = spectralSpread(y);
                expectGreaterThan(spread, previous, "more drive, more harmonics");
                previous = spread;
            }
        }

        beginTest("antiderivative_anti_aliasing_beats_a_naive_folder");
        {
            // A hard fold of a high note: harmonics reach far past the 72 kHz beyond which they fold
            // into the audible band.
            const double f = 4000.0;
            const auto x = sine(f, 0.9f, 2 * kFftSize + 8192);
            auto folder = std::make_unique<FolderProcessor>();
            folder->prepare(kRate, kSubBlock, memory);
            SlotParams p;
            p.p[FolderProcessor::drive] = 30.0f;
            p.p[FolderProcessor::symmetry] = 0.0f;
            p.p[FolderProcessor::mix] = 1.0f;
            const double adaa = worstAliasDb(run(*folder, p, x), f);

            const double gain = std::pow(10.0, 30.0 / 20.0);
            std::vector<float> naive(x.size());
            for (std::size_t i = 0; i < x.size(); ++i)
                naive[i] = static_cast<float>(FolderProcessor::fold(gain * x[i]));
            const double plain = worstAliasDb(naive, f);
            logMessage("worst alias below 20 kHz: naive " + juce::String(plain, 1) + " dB, ADAA " + juce::String(adaa, 1) + " dB");
            expectLessThan(adaa, plain - 6.0);
        }

        beginTest("symmetry_leaves_no_dc_and_mix_zero_is_the_delayed_dry_signal");
        {
            auto folder = std::make_unique<FolderProcessor>();
            folder->prepare(kRate, kSubBlock, memory);
            SlotParams p;
            p.p[FolderProcessor::drive] = 12.0f;
            p.p[FolderProcessor::symmetry] = 0.6f;
            p.p[FolderProcessor::mix] = 1.0f;
            const auto y = run(*folder, p, sine(110.0, 0.7f, 96000 * 2));
            double mean = 0.0;
            for (std::size_t i = 96000; i < y.size(); ++i)
                mean += y[i];
            mean /= 96000.0;
            expectLessThan(std::abs(mean), 0.01);

            auto clean = std::make_unique<FolderProcessor>();
            clean->prepare(kRate, kSubBlock, memory);
            p.p[FolderProcessor::mix] = 0.0f;
            const auto x = sine(330.0, 0.5f, 4800);
            const auto z = run(*clean, p, x);
            float error = 0.0f;
            for (std::size_t i = 1; i < x.size(); ++i)
                error = std::max(error, std::abs(z[i] - 0.5f * (x[i] + x[i - 1])));
            expectLessThan(error, 1.0e-6f);
        }

        beginTest("a_quantized_route_lands_on_whole_semitones");
        {
            ModSources sources;
            sources.set(ModSource::lfo1, 0.3f);
            MatrixSettings m;
            MatrixSlot slot;
            slot.source = ModSource::lfo1;
            slot.destination = modDestination::pitch;
            slot.amount = 0.25f; // 0.3 * 0.25 * 48 = 3.6 semitones
            slot.curve = ModCurve::quantized;
            m.add(slot);
            slot.destination = modDestination::generator(1, modDestination::generatorField::pitch);
            m.add(slot);
            slot.destination = modDestination::processorParameter(0, 0); // 0.075 of the range: 1/16
            m.add(slot);
            MatrixOffsets out;
            evaluateMatrix(m, sources, out);
            expectWithinAbsoluteError(out.pitchSemitones, 4.0f, 1.0e-5f);
            expectWithinAbsoluteError(out.generators[1].pitchSemitones, 4.0f, 1.0e-5f);
            expectWithinAbsoluteError(out.processors[0].p[0], 1.0f / 16.0f, 1.0e-6f);
        }
    }
};

static V3ShaperTests v3ShaperTests;
