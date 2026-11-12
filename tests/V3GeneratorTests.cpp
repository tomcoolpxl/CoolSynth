#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/HalfBand2x.h"
#include "synth/v3/VaGenerator.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr int kSubBlock = 32;
    constexpr double kHostRate = 48000.0;
    constexpr double kVoiceRate = 96000.0;

    struct Rendered
    {
        std::vector<float> left, right;
    };

    std::unique_ptr<VaGenerator> makeGenerator(double sampleRate, SlotMemory& memory)
    {
        memory.allocate(VaGenerator::memoryFloats(sampleRate, kSubBlock));
        auto generator = std::make_unique<VaGenerator>();
        generator->prepare(sampleRate, kSubBlock, memory);
        generator->noteOn(NoteContext {});
        return generator;
    }

    SlotParams basicParams(VaGenerator::Wave wave, float frequencyHz)
    {
        SlotParams params;
        params.p[VaGenerator::wave] = static_cast<float>(wave);
        params.p[VaGenerator::pulseWidth] = 0.5f;
        params.p[VaGenerator::unison] = 1.0f;
        params.p[VaGenerator::detune] = 0.0f;
        params.p[VaGenerator::spread] = 0.0f;
        params.p[VaGenerator::sub] = 0.0f;
        params.p[VaGenerator::fm] = 0.0f;
        params.level = 1.0f;
        params.pan = 0.0f;
        params.frequencyHz = frequencyHz;
        return params;
    }

    Rendered render(VaGenerator& generator, const SlotParams& params, int totalSamples,
                    const std::vector<float>* modulator = nullptr)
    {
        Rendered out;
        out.left.resize(static_cast<std::size_t>(totalSamples));
        out.right.resize(static_cast<std::size_t>(totalSamples));
        std::array<float, kSubBlock> left {}, right {};
        for (int start = 0; start < totalSamples; start += kSubBlock)
        {
            const int n = std::min(kSubBlock, totalSamples - start);
            for (int i = 0; i < n; ++i)
            {
                left[static_cast<std::size_t>(i)] = modulator != nullptr ? (*modulator)[static_cast<std::size_t>(start + i)] : 0.0f;
                right[static_cast<std::size_t>(i)] = left[static_cast<std::size_t>(i)];
            }
            generator.process(left.data(), right.data(), n, params);
            std::copy_n(left.begin(), n, out.left.begin() + start);
            std::copy_n(right.begin(), n, out.right.begin() + start);
        }
        return out;
    }

    // Average period from linearly interpolated upward zero crossings.
    double measureFrequency(const std::vector<float>& x, double sampleRate, std::size_t skip)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(skip, 1); i < x.size(); ++i)
        {
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double fraction = -static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
                crossings.push_back(static_cast<double>(i - 1) + fraction);
            }
        }
        if (crossings.size() < 2)
            return 0.0;
        const double period = (crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1);
        return sampleRate / period;
    }

    double centsFrom(double measuredHz, double targetHz)
    {
        return 1200.0 * std::log2(measuredHz / targetHz);
    }

    float rms(const std::vector<float>& x, std::size_t skip)
    {
        double sum = 0.0;
        for (std::size_t i = skip; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return static_cast<float>(std::sqrt(sum / static_cast<double>(x.size() - skip)));
    }

    constexpr int kFftOrder = 13;
    constexpr int kFftSize = 1 << kFftOrder;

    // Hann-windowed magnitude spectrum of the first kFftSize samples after skip.
    std::vector<float> spectrum(const std::vector<float>& x, std::size_t skip)
    {
        std::vector<float> buffer(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            buffer[static_cast<std::size_t>(i)] = x[skip + static_cast<std::size_t>(i)];
        juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(kFftSize));
        juce::dsp::FFT fft(kFftOrder);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        buffer.resize(static_cast<std::size_t>(kFftSize / 2));
        return buffer;
    }

    float peakNear(const std::vector<float>& mags, double hz, double sampleRate, int halfWidth = 3)
    {
        const int bin = static_cast<int>(std::round(hz * kFftSize / sampleRate));
        float peak = 0.0f;
        for (int b = std::max(1, bin - halfWidth); b <= std::min(kFftSize / 2 - 1, bin + halfWidth); ++b)
            peak = std::max(peak, mags[static_cast<std::size_t>(b)]);
        return peak;
    }

    // Render a saw at the 2x voice rate, decimate to the host rate, measure alias energy above Nyquist - 50 Hz.
    float aliasDbForNote(int midiNote)
    {
        const float hz = 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
        SlotMemory memory;
        auto generator = makeGenerator(kVoiceRate, memory);
        const int settleOut = 4096;
        const int totalOut = settleOut + kFftSize;
        const auto rendered = render(*generator, basicParams(VaGenerator::Wave::saw, hz), totalOut * 2);

        std::vector<float> decimated(static_cast<std::size_t>(totalOut), 0.0f);
        coolsynth::synth::HalfBandDecimator2x decimator;
        decimator.reset();
        decimator.processBlock(rendered.left.data(), decimated.data(), totalOut);

        const auto mags = spectrum(decimated, static_cast<std::size_t>(settleOut));
        const float fundamental = peakNear(mags, hz, kHostRate);
        const int aliasBinStart = static_cast<int>(std::ceil((kHostRate / 2.0 - 50.0) * kFftSize / kHostRate));
        float alias = 0.0f;
        for (int b = aliasBinStart; b < kFftSize / 2; ++b)
            alias = std::max(alias, mags[static_cast<std::size_t>(b)]);
        return 20.0f * std::log10(alias / fundamental + 1.0e-30f);
    }
}

class V3GeneratorTests final : public juce::UnitTest
{
public:
    V3GeneratorTests() : juce::UnitTest("V3 VA generator", "V3") {}

    void runTest() override
    {
        beginTest("descriptor_and_memory");
        {
            const auto& d = VaGenerator::descriptor();
            expect(juce::String(d.name) == "VA");
            expectEquals(d.usedParams, static_cast<int>(VaGenerator::paramCount));
            expectEquals(static_cast<int>(VaGenerator::memoryFloats(48000.0, 32)), 32);
            expect(juce::String(d.params[VaGenerator::pulseWidth].id) == "pw");
        }

        beginTest("tuning_within_one_cent_for_every_wave");
        {
            for (auto wave : { VaGenerator::Wave::sine, VaGenerator::Wave::saw, VaGenerator::Wave::pulse, VaGenerator::Wave::triangle })
            {
                for (float hz : { 440.0f, 1760.0f, 55.0f })
                {
                    SlotMemory memory;
                    auto generator = makeGenerator(kHostRate, memory);
                    const auto rendered = render(*generator, basicParams(wave, hz), 48000);
                    const double measured = measureFrequency(rendered.left, kHostRate, 2000);
                    const double cents = centsFrom(measured, hz);
                    logMessage("wave " + juce::String(static_cast<int>(wave)) + " at " + juce::String(hz) + " Hz: "
                               + juce::String(measured, 3) + " Hz, " + juce::String(cents, 3) + " cents");
                    expect(std::abs(cents) < 1.0);
                }
            }
        }

        beginTest("alias_floor_after_decimation_matches_v2");
        {
            const float a4 = aliasDbForNote(69);
            const float a7 = aliasDbForNote(105);
            logMessage("saw alias A4: " + juce::String(a4, 1) + " dB, A7: " + juce::String(a7, 1) + " dB");
            expect(a4 <= -80.0f);
            expect(a7 <= -70.0f);
        }

        beginTest("unison_level_is_compensated");
        {
            SlotMemory memoryOne, memorySeven;
            auto one = makeGenerator(kHostRate, memoryOne);
            auto seven = makeGenerator(kHostRate, memorySeven);
            auto paramsSeven = basicParams(VaGenerator::Wave::saw, 220.0f);
            paramsSeven.p[VaGenerator::unison] = 7.0f;
            paramsSeven.p[VaGenerator::detune] = 30.0f;
            const auto single = render(*one, basicParams(VaGenerator::Wave::saw, 220.0f), 96000);
            const auto stack = render(*seven, paramsSeven, 96000);
            const float ratioDb = 20.0f * std::log10(rms(stack.left, 4800) / rms(single.left, 4800));
            logMessage("7-voice detuned stack vs single: " + juce::String(ratioDb, 2) + " dB");
            expect(std::abs(ratioDb) < 1.5f);
            // Copies can align momentarily: the bound is sqrt(7) copies at centre pan (0.707), about 1.87.
            float peak = 0.0f;
            for (auto v : stack.left)
                peak = std::max(peak, std::abs(v));
            expect(peak < 2.0f);
        }

        beginTest("pan_and_spread_place_copies_in_the_stereo_field");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            auto hardLeft = basicParams(VaGenerator::Wave::saw, 220.0f);
            hardLeft.pan = -1.0f;
            const auto left = render(*generator, hardLeft, 4800);
            expect(rms(left.left, 0) > 0.3f);
            expectWithinAbsoluteError(rms(left.right, 0), 0.0f, 1.0e-6f);

            auto centred = basicParams(VaGenerator::Wave::saw, 220.0f);
            const auto centre = render(*generator, centred, 4800);
            expectWithinAbsoluteError(rms(centre.left, 0), rms(centre.right, 0), 1.0e-5f);

            auto spread = basicParams(VaGenerator::Wave::saw, 220.0f);
            spread.p[VaGenerator::unison] = 2.0f;
            spread.p[VaGenerator::detune] = 20.0f;
            spread.p[VaGenerator::spread] = 1.0f;
            const auto wide = render(*generator, spread, 4800);
            expect(rms(wide.left, 0) > 0.3f && rms(wide.right, 0) > 0.3f);
            float difference = 0.0f;
            for (std::size_t i = 0; i < wide.left.size(); ++i)
                difference = std::max(difference, std::abs(wide.left[i] - wide.right[i]));
            expect(difference > 0.1f);
        }

        beginTest("sync_locks_the_slave_to_the_master_period");
        {
            // Master at 468.75 Hz: period 102.4 samples, five periods = 512 samples exactly.
            const float masterHz = 468.75f;
            const float slaveHz = masterHz * 1.5f;
            const int total = 48000;

            SlotMemory masterMemory, slaveMemory, freeMemory;
            auto master = makeGenerator(kHostRate, masterMemory);
            auto slave = makeGenerator(kHostRate, slaveMemory);
            auto freeRunning = makeGenerator(kHostRate, freeMemory);

            auto masterParams = basicParams(VaGenerator::Wave::saw, masterHz);
            auto slaveParams = basicParams(VaGenerator::Wave::saw, slaveHz);
            slaveParams.combine = Combine::sync;

            std::vector<float> synced(static_cast<std::size_t>(total)), unsynced(static_cast<std::size_t>(total));
            std::array<float, kSubBlock> l {}, r {}, l2 {}, r2 {};
            for (int start = 0; start < total; start += kSubBlock)
            {
                l.fill(0.0f); r.fill(0.0f);
                master->process(l.data(), r.data(), kSubBlock, masterParams);
                slave->setSyncInput(master->syncTrack().data());
                l.fill(0.0f); r.fill(0.0f);
                slave->process(l.data(), r.data(), kSubBlock, slaveParams);
                std::copy_n(l.begin(), kSubBlock, synced.begin() + start);

                l2.fill(0.0f); r2.fill(0.0f);
                freeRunning->process(l2.data(), r2.data(), kSubBlock, basicParams(VaGenerator::Wave::saw, slaveHz));
                std::copy_n(l2.begin(), kSubBlock, unsynced.begin() + start);
            }

            float syncedDifference = 0.0f, unsyncedDifference = 0.0f, peak = 0.0f;
            for (std::size_t i = 4096; i + 512 < synced.size(); ++i)
            {
                syncedDifference = std::max(syncedDifference, std::abs(synced[i] - synced[i + 512]));
                unsyncedDifference = std::max(unsyncedDifference, std::abs(unsynced[i] - unsynced[i + 512]));
                peak = std::max(peak, std::abs(synced[i]));
            }
            logMessage("periodicity error over five master periods: synced " + juce::String(syncedDifference, 6)
                       + ", free-running " + juce::String(unsyncedDifference, 3));
            expect(syncedDifference < 1.0e-3f);
            expect(unsyncedDifference > 0.5f);
            expect(peak < 1.5f);
            for (auto v : synced)
                expect(std::isfinite(v));
        }

        beginTest("ring_and_am_scale_by_the_modulator_in_the_buffer");
        {
            const std::vector<float> half(4800, 0.5f);
            SlotMemory memoryA, memoryB, memoryC;
            auto reference = makeGenerator(kHostRate, memoryA);
            auto ring = makeGenerator(kHostRate, memoryB);
            auto am = makeGenerator(kHostRate, memoryC);

            auto params = basicParams(VaGenerator::Wave::saw, 220.0f);
            const auto dry = render(*reference, params, 4800);
            params.combine = Combine::ring;
            const auto ringed = render(*ring, params, 4800, &half);
            params.combine = Combine::am;
            const auto modulated = render(*am, params, 4800, &half);

            float ringError = 0.0f, amError = 0.0f;
            for (std::size_t i = 0; i < dry.left.size(); ++i)
            {
                ringError = std::max(ringError, std::abs(ringed.left[i] - 0.5f * dry.left[i]));
                amError = std::max(amError, std::abs(modulated.left[i] - 0.75f * dry.left[i]));
            }
            expect(ringError < 1.0e-6f);
            expect(amError < 1.0e-6f);
        }

        beginTest("sub_adds_a_sine_one_octave_below");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            // 468.75 Hz is bin 80 and its sub 234.375 Hz is bin 40, so neither peak suffers window scalloping.
            const float hz = 468.75f;
            auto params = basicParams(VaGenerator::Wave::sine, hz);
            params.p[VaGenerator::sub] = 1.0f;
            const auto rendered = render(*generator, params, 4096 + kFftSize);
            const auto mags = spectrum(rendered.left, 4096);
            const float main = peakNear(mags, hz, kHostRate);
            const float sub = peakNear(mags, hz * 0.5, kHostRate);
            const float ratioDb = 20.0f * std::log10(sub / main);
            logMessage("sub vs main: " + juce::String(ratioDb, 2) + " dB");
            expect(std::abs(ratioDb) < 0.5f);
        }

        beginTest("fm_changes_the_output_and_stays_bounded");
        {
            std::vector<float> modulator(4800);
            for (std::size_t i = 0; i < modulator.size(); ++i)
                modulator[i] = std::sin(static_cast<float>(i) * 2.0f * juce::MathConstants<float>::pi * 220.0f / 48000.0f);

            SlotMemory memoryA, memoryB;
            auto reference = makeGenerator(kHostRate, memoryA);
            auto fm = makeGenerator(kHostRate, memoryB);
            auto params = basicParams(VaGenerator::Wave::sine, 440.0f);
            const auto dry = render(*reference, params, 4800);
            params.combine = Combine::fm;
            params.p[VaGenerator::fm] = 1.0f;
            const auto modulated = render(*fm, params, 4800, &modulator);

            float difference = 0.0f, peak = 0.0f;
            for (std::size_t i = 0; i < dry.left.size(); ++i)
            {
                difference = std::max(difference, std::abs(modulated.left[i] - dry.left[i]));
                peak = std::max(peak, std::abs(modulated.left[i]));
                expect(std::isfinite(modulated.left[i]));
            }
            expect(difference > 0.2f);
            expect(peak <= 1.0f / std::sqrt(2.0f) + 1.0e-4f); // one sine copy, centre pan
        }
    }
};

static V3GeneratorTests v3GeneratorTests;
