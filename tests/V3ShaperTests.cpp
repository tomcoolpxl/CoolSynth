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
#include "synth/v3/VaGenerator.h"

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

namespace
{
    double harmonicDb(const std::vector<float>& x, double f, int k, double rate)
    {
        // Single-bin DFT at k f over a whole number of cycles of f.
        const int cycles = static_cast<int>(std::floor(static_cast<double>(x.size() - 4800) * f / rate));
        const int n = static_cast<int>(std::lround(cycles * rate / f));
        double re1 = 0.0, im1 = 0.0, re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = x[static_cast<std::size_t>(4800 + i)];
            const double t = 2.0 * juce::MathConstants<double>::pi * f * i / rate;
            re1 += v * std::cos(t);
            im1 += v * std::sin(t);
            re += v * std::cos(k * t);
            im += v * std::sin(k * t);
        }
        return 10.0 * std::log10((re * re + im * im) / (re1 * re1 + im1 * im1));
    }
}

class V3DriveTests final : public juce::UnitTest
{
public:
    V3DriveTests() : juce::UnitTest("V3 per-voice drive and ring audit", "V3") {}

    void runTest() override
    {
        SlotMemory memory;
        auto drive = [&](DriveProcessor::Curve curve, float db, std::vector<float> x)
        {
            auto d = std::make_unique<DriveProcessor>();
            d->prepare(kRate, kSubBlock, memory);
            SlotParams p;
            p.p[DriveProcessor::drive] = db;
            p.p[DriveProcessor::curve] = static_cast<float>(curve);
            p.p[DriveProcessor::tone] = 1.0f;
            p.p[DriveProcessor::mix] = 1.0f;
            return run(*d, p, std::move(x));
        };

        beginTest("zero_db_is_nearly_clean_and_every_curve_stays_bounded");
        {
            const auto x = sine(440.0, 0.5f, 9600);
            const auto y = drive(DriveProcessor::Curve::soft, 0.0f, x);
            float error = 0.0f;
            for (std::size_t i = 1; i < x.size(); ++i)
                error = std::max(error, std::abs(y[i] - 0.5f * (x[i] + x[i - 1])));
            expectLessThan(error, 0.02f, "0 dB soft drive");
            for (auto curve : { DriveProcessor::Curve::soft, DriveProcessor::Curve::hard, DriveProcessor::Curve::tube })
            {
                float peak = 0.0f;
                for (float v : drive(curve, 36.0f, sine(220.0, 1.0f, 48000)))
                    peak = std::max(peak, std::abs(v));
                expectLessOrEqual(peak, 1.1f, "curve " + juce::String(static_cast<int>(curve)));
            }
        }

        beginTest("soft_is_odd_and_tube_adds_even_harmonics");
        {
            const auto soft = drive(DriveProcessor::Curve::soft, 24.0f, sine(500.0, 0.8f, 96000));
            const auto tube = drive(DriveProcessor::Curve::tube, 24.0f, sine(500.0, 0.8f, 96000));
            expectGreaterThan(harmonicDb(soft, 500.0, 3, kRate), -20.0, "soft makes a third harmonic");
            expectLessThan(harmonicDb(soft, 500.0, 2, kRate), -80.0, "soft makes no second harmonic");
            expectGreaterThan(harmonicDb(tube, 500.0, 2, kRate), -30.0, "tube makes a second harmonic");
        }

        beginTest("the_hard_curve_aliases_less_than_a_naive_clipper");
        {
            // 4.1 kHz: not a divisor of the sample rate, so aliases do not land on harmonics.
            const double f = 4100.0;
            const auto x = sine(f, 0.9f, 2 * kFftSize + 8192);
            const double adaa = worstAliasDb(drive(DriveProcessor::Curve::hard, 30.0f, x), f);
            const double gain = DriveProcessor::kHeadroom * std::pow(10.0, 30.0 / 20.0);
            std::vector<float> naive(x.size());
            for (std::size_t i = 0; i < x.size(); ++i)
                naive[i] = static_cast<float>(juce::jlimit(-1.0, 1.0, gain * x[i]));
            const double plain = worstAliasDb(naive, f);
            logMessage("hard drive, worst alias below 20 kHz: naive " + juce::String(plain, 1) + " dB, ADAA " + juce::String(adaa, 1) + " dB");
            expectLessThan(adaa, plain - 6.0);
        }

        beginTest("ring_combine_audit_level_and_aliasing");
        {
            // A sine carrier ringed by a saw: every legit component sits at f1 +- n f2.
            SlotMemory sawMemory, sineMemory;
            sawMemory.allocate(VaGenerator::memoryFloats(kRate, kSubBlock));
            sineMemory.allocate(VaGenerator::memoryFloats(kRate, kSubBlock));
            auto saw = std::make_unique<VaGenerator>();
            auto carrier = std::make_unique<VaGenerator>();
            saw->prepare(kRate, kSubBlock, sawMemory);
            carrier->prepare(kRate, kSubBlock, sineMemory);
            saw->noteOn(NoteContext {});
            carrier->noteOn(NoteContext {});
            SlotParams sp;
            sp.p[VaGenerator::shape] = 2.0f;
            sp.p[VaGenerator::pulseWidth] = 0.5f;
            sp.p[VaGenerator::unison] = 1.0f;
            sp.p[VaGenerator::mix] = 0.67f;
            sp.p[VaGenerator::randomPhase] = 0.0f;
            const double f2 = 1370.0, f1 = 3000.0;
            sp.frequencyHz = static_cast<float>(f2);
            SlotParams cp = sp;
            cp.p[VaGenerator::shape] = 0.0f;
            cp.frequencyHz = static_cast<float>(f1);
            cp.combine = Combine::ring;
            cp.combineAmount = 1.0f;
            const int total = 2 * kFftSize + 8192;
            std::vector<float> ringed(static_cast<std::size_t>(total));
            std::array<float, kSubBlock> l {}, r {};
            for (int s = 0; s < total; s += kSubBlock)
            {
                saw->process(l.data(), r.data(), kSubBlock, sp);
                carrier->setLink(saw->link());
                carrier->process(l.data(), r.data(), kSubBlock, cp);
                for (int i = 0; i < kSubBlock; ++i)
                    ringed[static_cast<std::size_t>(s + i)] = carrier->link().signal[i];
            }
            double level = 0.0;
            for (std::size_t i = 4096; i < ringed.size(); ++i)
                level += static_cast<double>(ringed[i]) * ringed[i];
            level = std::sqrt(level / static_cast<double>(ringed.size() - 4096));
            logMessage("ring of a sine by a saw: rms " + juce::String(level, 3));
            expect(level > 0.2 && level < 0.6, "the product of two unit waves sits near their rms product (0.41)");

            coolsynth::synth::HalfBandDecimator2x decimator;
            std::vector<float> host(ringed.size() / 2);
            decimator.processBlock(ringed.data(), host.data(), static_cast<int>(host.size()));
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
            double strongest = 0.0, worst = 0.0;
            for (int b = static_cast<int>(50.0 / binHz); b < static_cast<int>(20000.0 / binHz); ++b)
            {
                const double hz = b * binHz;
                bool legit = false;
                for (int n = 0; n < 40 && ! legit; ++n)
                    legit = std::abs(std::abs(f1 + n * f2) - hz) < 8.0 * binHz || std::abs(std::abs(f1 - n * f2) - hz) < 8.0 * binHz;
                const double m = frame[static_cast<std::size_t>(b)];
                if (legit)
                    strongest = std::max(strongest, m);
                else
                    worst = std::max(worst, m);
            }
            const double db = 20.0 * std::log10(std::max(1.0e-12, worst) / strongest);
            logMessage("ring of a sine by a saw: worst alias below 20 kHz " + juce::String(db, 1) + " dB");
            expectLessThan(db, -70.0);

            // FM combine: the same carrier phase-modulated by a sine at half amount.
            SlotMemory modMemory, fmMemory;
            modMemory.allocate(VaGenerator::memoryFloats(kRate, kSubBlock));
            fmMemory.allocate(VaGenerator::memoryFloats(kRate, kSubBlock));
            auto modulator = std::make_unique<VaGenerator>();
            auto fmCarrier = std::make_unique<VaGenerator>();
            modulator->prepare(kRate, kSubBlock, modMemory);
            fmCarrier->prepare(kRate, kSubBlock, fmMemory);
            modulator->noteOn(NoteContext {});
            fmCarrier->noteOn(NoteContext {});
            SlotParams mp = sp;
            mp.p[VaGenerator::shape] = 0.0f;
            SlotParams fp = cp;
            fp.combine = Combine::fm;
            fp.combineAmount = 0.5f;
            std::vector<float> fmOut(static_cast<std::size_t>(total));
            for (int s = 0; s < total; s += kSubBlock)
            {
                modulator->process(l.data(), r.data(), kSubBlock, mp);
                fmCarrier->setLink(modulator->link());
                fmCarrier->process(l.data(), r.data(), kSubBlock, fp);
                for (int i = 0; i < kSubBlock; ++i)
                    fmOut[static_cast<std::size_t>(s + i)] = fmCarrier->link().signal[i];
            }
            float fmPeak = 0.0f;
            for (float v : fmOut)
                fmPeak = std::max(fmPeak, std::abs(v));
            expectLessOrEqual(fmPeak, 1.01f, "combine FM keeps a sine carrier at unit level");
            std::vector<float> fmHost(fmOut.size() / 2);
            coolsynth::synth::HalfBandDecimator2x fmDecimator;
            fmDecimator.processBlock(fmOut.data(), fmHost.data(), static_cast<int>(fmHost.size()));
            std::fill(frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < kFftSize; ++i)
            {
                const double t = 2.0 * juce::MathConstants<double>::pi * i / kFftSize;
                const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
                frame[static_cast<std::size_t>(i)] = static_cast<float>(fmHost[2048 + static_cast<std::size_t>(i)] * w);
            }
            fft.performFrequencyOnlyForwardTransform(frame.data());
            strongest = 0.0;
            worst = 0.0;
            for (int b = static_cast<int>(50.0 / binHz); b < static_cast<int>(20000.0 / binHz); ++b)
            {
                const double hz = b * binHz;
                bool legit = false;
                for (int n = 0; n < 60 && ! legit; ++n)
                    legit = std::abs(std::abs(f1 + n * f2) - hz) < 8.0 * binHz || std::abs(std::abs(f1 - n * f2) - hz) < 8.0 * binHz;
                const double m = frame[static_cast<std::size_t>(b)];
                if (legit)
                    strongest = std::max(strongest, m);
                else
                    worst = std::max(worst, m);
            }
            const double fmDb = 20.0 * std::log10(std::max(1.0e-12, worst) / strongest);
            logMessage("combine FM of a sine by a sine: worst alias below 20 kHz " + juce::String(fmDb, 1) + " dB");
            expectLessThan(fmDb, -70.0);
        }
    }
};

static V3DriveTests v3DriveTests;
