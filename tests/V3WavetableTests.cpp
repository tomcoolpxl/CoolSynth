#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/HalfBand2x.h"
#include "synth/v3/WavetableGenerator.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;
    constexpr int kFftOrder = 15;
    constexpr int kFftSize = 1 << kFftOrder;

    struct Wt
    {
        SlotMemory memory;
        WavetableGenerator generator;
    };

    std::unique_ptr<Wt> makeWavetable()
    {
        auto w = std::make_unique<Wt>();
        w->memory.allocate(WavetableGenerator::memoryFloats(kVoiceRate, kSubBlock));
        w->generator.prepare(kVoiceRate, kSubBlock, w->memory);
        w->generator.noteOn(NoteContext {});
        return w;
    }

    SlotParams wtParams(int table, float position, float hz)
    {
        SlotParams p;
        p.p[WavetableGenerator::table] = static_cast<float>(table);
        p.p[WavetableGenerator::position] = position;
        p.p[WavetableGenerator::unison] = 1.0f;
        p.p[WavetableGenerator::detune] = 15.0f;
        p.p[WavetableGenerator::spread] = 0.5f;
        p.p[WavetableGenerator::randomPhase] = 0.0f;
        p.frequencyHz = hz;
        return p;
    }

    std::vector<float> render(WavetableGenerator& g, const SlotParams& p, int samples, std::vector<float>* right = nullptr)
    {
        std::vector<float> out(static_cast<std::size_t>(samples));
        if (right != nullptr)
            right->assign(static_cast<std::size_t>(samples), 0.0f);
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
            {
                out[static_cast<std::size_t>(s + i)] = g.link().signal[i];
                if (right != nullptr)
                    (*right)[static_cast<std::size_t>(s + i)] = r[static_cast<std::size_t>(i)];
            }
        }
        return out;
    }

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

    double rms(const std::vector<float>& x, std::size_t from)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(x.size() - from));
    }
}

class V3WavetableTests final : public juce::UnitTest
{
public:
    V3WavetableTests() : juce::UnitTest("V3 wavetable generator", "V3") {}

    void runTest() override
    {
        const auto& bank = WavetableBank::instance();

        beginTest("the_classic_table_starts_as_a_sine_and_every_level_keeps_its_band");
        {
            const float* sine = bank.frame(0, 0, 0);
            float worst = 0.0f;
            for (int i = 0; i < WavetableBank::tableSize; ++i)
                worst = std::max(worst, std::abs(sine[i] - static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * i / WavetableBank::tableSize))));
            expectLessThan(worst, 1.0e-4f, "frame 0 is a sine");

            juce::dsp::FFT fft(11);
            for (int level : { 0, 3, 7, 10 })
            {
                std::vector<float> buffer(2 * WavetableBank::tableSize, 0.0f);
                const float* square = bank.frame(0, 15, level);
                std::copy_n(square, WavetableBank::tableSize, buffer.begin());
                fft.performFrequencyOnlyForwardTransform(buffer.data());
                const int limit = (WavetableBank::tableSize / 2) >> level;
                double inside = 0.0, outside = 0.0;
                for (int h = 1; h < WavetableBank::tableSize / 2; ++h)
                    (h <= limit ? inside : outside) += static_cast<double>(buffer[static_cast<std::size_t>(h)]) * buffer[static_cast<std::size_t>(h)];
                expectLessThan(10.0 * std::log10(std::max(1.0e-30, outside) / inside), -100.0, "level " + juce::String(level));
            }
            float peak = 0.0f;
            for (int i = 0; i < WavetableBank::tableSize; ++i)
                peak = std::max(peak, std::abs(bank.frame(1, 9, 0)[i]));
            expectWithinAbsoluteError(peak, 1.0f, 1.0e-4f, "full-band frames peak at 1");
        }

        beginTest("band_limits_follow_the_frequency");
        {
            expectEquals(WavetableBank::levelFor(20.0, kVoiceRate), 0);
            expectEquals(WavetableBank::levelFor(3520.0, kVoiceRate), 6);
            expectEquals(WavetableBank::levelFor(30000.0, kVoiceRate), 9);  // two harmonics, the second at 60 kHz
            expectEquals(WavetableBank::levelFor(40000.0, kVoiceRate), 10); // the fundamental alone
        }

        beginTest("a_saw_at_a7_has_no_audible_aliases_after_decimation");
        {
            auto wt = makeWavetable();
            const auto out = render(wt->generator, wtParams(0, 10.0f / 15.0f, 3520.0f), 2 * kFftSize + 8192);
            coolsynth::synth::HalfBandDecimator2x decimator;
            std::vector<float> host(out.size() / 2);
            decimator.processBlock(out.data(), host.data(), static_cast<int>(host.size()));
            const auto m = spectrum(host, 2048);
            const double binHz = (kVoiceRate / 2.0) / kFftSize;
            double fundamental = 0.0, worst = 0.0;
            for (std::size_t b = static_cast<std::size_t>(50.0 / binHz); b < static_cast<std::size_t>(20000.0 / binHz); ++b)
            {
                const double hz = static_cast<double>(b) * binHz;
                const double harmonic = std::round(hz / 3520.0);
                const bool onHarmonic = harmonic >= 1.0 && std::abs(hz - harmonic * 3520.0) < 8.0 * binHz;
                if (harmonic == 1.0 && onHarmonic)
                    fundamental = std::max(fundamental, m[b]);
                else if (! onHarmonic)
                    worst = std::max(worst, m[b]);
            }
            const double db = 20.0 * std::log10(std::max(1.0e-12, worst) / fundamental);
            logMessage("largest alias at A7: " + juce::String(db, 1) + " dB");
            expectLessThan(db, -80.0);
        }

        beginTest("it_plays_in_tune");
        {
            auto wt = makeWavetable();
            const auto out = render(wt->generator, wtParams(0, 0.0f, 440.0f), 96000);
            std::vector<double> crossings;
            for (std::size_t i = 1000; i < out.size(); ++i)
                if (out[i - 1] < 0.0f && out[i] >= 0.0f)
                    crossings.push_back(static_cast<double>(i - 1) - out[i - 1] / static_cast<double>(out[i] - out[i - 1]));
            const double hz = kVoiceRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
            expectWithinAbsoluteError(1200.0 * std::log2(hz / 440.0), 0.0, 0.05, "cents");
        }

        beginTest("a_position_sweep_morphs_without_steps");
        {
            auto wt = makeWavetable();
            auto p = wtParams(0, 0.0f, 110.0f);
            std::array<float, kSubBlock> l {}, r {};
            float previous = 0.0f, largest = 0.0f;
            const int blocks = static_cast<int>(kVoiceRate / kSubBlock); // one second from sine to triangle
            for (int b = 0; b < blocks; ++b)
            {
                p.p[WavetableGenerator::position] = (5.0f / 15.0f) * static_cast<float>(b) / static_cast<float>(blocks);
                wt->generator.process(l.data(), r.data(), kSubBlock, p);
                for (int i = 0; i < kSubBlock; ++i)
                {
                    const float v = wt->generator.link().signal[i];
                    if (b > 0 || i > 2)
                        largest = std::max(largest, std::abs(v - previous));
                    previous = v;
                }
            }
            // A 110 Hz sine or triangle at peak 1 moves at most 0.015 per sample.
            expectLessThan(largest, 0.02f);
        }

        beginTest("unison_keeps_the_level_and_phase_rand_varies_the_start");
        {
            auto one = makeWavetable();
            auto five = makeWavetable();
            // Wide detune and four seconds, so the copies' beating averages out whatever the start phases.
            auto p = wtParams(0, 10.0f / 15.0f, 220.0f);
            p.p[WavetableGenerator::detune] = 60.0f;
            const double a = rms(render(one->generator, p, 4 * 96000), 2000);
            p.p[WavetableGenerator::unison] = 5.0f;
            p.p[WavetableGenerator::randomPhase] = 1.0f;
            five->generator.noteOn(NoteContext {});
            const double b = rms(render(five->generator, p, 4 * 96000), 2000);
            expectWithinAbsoluteError(20.0 * std::log10(b / a), 0.0, 1.5, "five copies against one");

            auto first = makeWavetable();
            auto second = makeWavetable();
            p.p[WavetableGenerator::unison] = 1.0f;
            const auto x = render(first->generator, p, 512);
            const auto y = render(second->generator, p, 512);
            float difference = 0.0f;
            for (std::size_t i = 0; i < x.size(); ++i)
                difference = std::max(difference, std::abs(x[i] - y[i]));
            expectGreaterThan(difference, 0.1f, "random start phases differ between voices");
        }

        beginTest("bank_files_name_the_table");
        {
            const auto result = coolsynth::presets::parseBankFile("[W]\ng1 = Wavetable table=vocal pos=0.4 unison=3 detune=12\nmod = src=lfo1 dst=g1.pos amount=0.3\n", "w.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            expect(entries.contains("pA.g1.type=Wavetable"), entries.joinIntoString(", "));
            expect(entries.contains("pA.g1.p1=vocal"), entries.joinIntoString(", "));
            expect(entries.contains("pA.mod1.dst=G1 Param 2"), entries.joinIntoString(", "));
        }
    }
};

static V3WavetableTests v3WavetableTests;
