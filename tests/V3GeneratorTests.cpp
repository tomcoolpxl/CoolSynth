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
    using Wave = VaGenerator::Wave;

    constexpr int kSubBlock = 32;
    constexpr double kHostRate = 48000.0;
    constexpr double kVoiceRate = 96000.0;
    constexpr int kFftOrder = 13;
    constexpr int kFftSize = 1 << kFftOrder;

    struct Rendered
    {
        std::vector<float> left, right, mono;
    };

    std::unique_ptr<VaGenerator> makeGenerator(double sampleRate, SlotMemory& memory)
    {
        memory.allocate(VaGenerator::memoryFloats(sampleRate, kSubBlock));
        auto generator = std::make_unique<VaGenerator>();
        generator->prepare(sampleRate, kSubBlock, memory);
        generator->noteOn(NoteContext {});
        return generator;
    }

    SlotParams basicParams(Wave wave, float frequencyHz)
    {
        SlotParams params;
        params.p[VaGenerator::shape] = static_cast<float>(wave);
        params.p[VaGenerator::pulseWidth] = 0.5f;
        params.p[VaGenerator::unison] = 1.0f;
        params.p[VaGenerator::detune] = 0.0f;
        params.p[VaGenerator::spread] = 0.0f;
        params.p[VaGenerator::mix] = 0.67f;
        params.p[VaGenerator::randomPhase] = 0.0f;
        params.p[VaGenerator::sub] = 0.0f;
        params.frequencyHz = frequencyHz;
        return params;
    }

    Rendered render(VaGenerator& generator, const SlotParams& params, int totalSamples, const std::vector<float>* modulator = nullptr)
    {
        Rendered out;
        out.left.resize(static_cast<std::size_t>(totalSamples));
        out.right.resize(static_cast<std::size_t>(totalSamples));
        out.mono.resize(static_cast<std::size_t>(totalSamples));
        std::array<float, kSubBlock> left {}, right {};
        for (int start = 0; start < totalSamples; start += kSubBlock)
        {
            const int n = std::min(kSubBlock, totalSamples - start);
            GeneratorLink link;
            if (modulator != nullptr)
                link.signal = modulator->data() + start;
            generator.setLink(link);
            generator.process(left.data(), right.data(), n, params);
            const auto offset = static_cast<std::ptrdiff_t>(start);
            std::copy_n(left.begin(), n, out.left.begin() + offset);
            std::copy_n(right.begin(), n, out.right.begin() + offset);
            std::copy_n(generator.link().signal, n, out.mono.begin() + offset);
        }
        return out;
    }

    double measureFrequency(const std::vector<float>& x, double sampleRate, std::size_t skip)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(skip, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return sampleRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
    }

    float rms(const std::vector<float>& x, std::size_t skip)
    {
        double sum = 0.0;
        for (std::size_t i = skip; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return static_cast<float>(std::sqrt(sum / static_cast<double>(x.size() - skip)));
    }

    float maxDifference(const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            d = std::max(d, std::abs(a[i] - b[i]));
        return d;
    }

    std::vector<float> magnitudes(const std::vector<float>& x, std::size_t skip, bool window)
    {
        std::vector<float> buffer(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            buffer[static_cast<std::size_t>(i)] = x[skip + static_cast<std::size_t>(i)];
        if (window)
        {
            juce::dsp::WindowingFunction<float> w(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::blackmanHarris);
            w.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(kFftSize));
        }
        juce::dsp::FFT fft(kFftOrder);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        buffer.resize(static_cast<std::size_t>(kFftSize / 2));
        return buffer;
    }

    float peakNear(const std::vector<float>& mags, double hz, double sampleRate)
    {
        const int bin = static_cast<int>(std::round(hz * kFftSize / sampleRate));
        float peak = 0.0f;
        for (int b = std::max(1, bin - 4); b <= std::min(kFftSize / 2 - 1, bin + 4); ++b)
            peak = std::max(peak, mags[static_cast<std::size_t>(b)]);
        return peak;
    }

    /**
        Energy that is not a harmonic, relative to the harmonics, from 20 Hz to 20 kHz. The
        fundamental sits exactly on fundamentalBin, so the frame holds whole periods, every
        component (aliases included) lands on a bin and nothing leaks: no window is needed.
    */
    float inharmonicDb(const std::vector<float>& x, std::size_t skip, int fundamentalBin, double sampleRate)
    {
        const auto mags = magnitudes(x, skip, false);
        const int lowest = static_cast<int>(std::ceil(20.0 * kFftSize / sampleRate));
        const int highest = static_cast<int>(std::floor(20000.0 * kFftSize / sampleRate));
        double harmonic = 0.0, inharmonic = 0.0;
        for (int b = lowest; b <= highest; ++b)
        {
            const double power = static_cast<double>(mags[static_cast<std::size_t>(b)]) * static_cast<double>(mags[static_cast<std::size_t>(b)]);
            if (b % fundamentalBin == 0)
                harmonic += power;
            else
                inharmonic += power;
        }
        return static_cast<float>(10.0 * std::log10(inharmonic / harmonic + 1.0e-30));
    }

    std::vector<float> decimate(const std::vector<float>& voiceRate)
    {
        std::vector<float> out(voiceRate.size() / 2, 0.0f);
        coolsynth::synth::HalfBandDecimator2x decimator;
        decimator.reset();
        decimator.processBlock(voiceRate.data(), out.data(), static_cast<int>(out.size()));
        return out;
    }

    // The common 2-point polyBLEP saw, kept here only as a yardstick for the 4-point events.
    std::vector<float> referencePolyBlepSaw(float frequencyHz, double sampleRate, int samples)
    {
        auto polyBlep = [](float t, float dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0f;
            }
            if (t > 1.0f - dt)
            {
                t = (t - 1.0f) / dt;
                return t * t + t + t + 1.0f;
            }
            return 0.0f;
        };
        std::vector<float> out(static_cast<std::size_t>(samples));
        const float dt = frequencyHz / static_cast<float>(sampleRate);
        float phase = 0.0f;
        for (auto& v : out)
        {
            v = 0.70710678f * (2.0f * phase - 1.0f - polyBlep(phase, dt));
            phase += dt;
            phase -= std::floor(phase);
        }
        return out;
    }

    /** Saw through generator and decimator, fundamental on bin `bin` of the host-rate frame. */
    float generatorInharmonicDb(int bin, Wave wave = Wave::saw)
    {
        const float hz = static_cast<float>(bin * kHostRate / kFftSize);
        SlotMemory memory;
        auto generator = makeGenerator(kVoiceRate, memory);
        const int settle = 4096;
        const auto rendered = render(*generator, basicParams(wave, hz), 2 * (settle + kFftSize));
        return inharmonicDb(decimate(rendered.left), static_cast<std::size_t>(settle), bin, kHostRate);
    }

    float referenceInharmonicDb(int bin)
    {
        const float hz = static_cast<float>(bin * kHostRate / kFftSize);
        const int settle = 4096;
        return inharmonicDb(decimate(referencePolyBlepSaw(hz, kVoiceRate, 2 * (settle + kFftSize))),
                            static_cast<std::size_t>(settle), bin, kHostRate);
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
            expectEquals(static_cast<int>(VaGenerator::memoryFloats(48000.0, 32)), 64);
            expect(d.params[VaGenerator::detune].skew == Skew::squared);
        }

        beginTest("tuning_within_one_cent_for_every_wave");
        {
            for (auto wave : { Wave::sine, Wave::triangle, Wave::saw, Wave::pulse })
            {
                for (float hz : { 55.0f, 440.0f, 1760.0f })
                {
                    SlotMemory memory;
                    auto generator = makeGenerator(kHostRate, memory);
                    const auto rendered = render(*generator, basicParams(wave, hz), 48000);
                    const double measured = measureFrequency(rendered.left, kHostRate, 2000);
                    const double cents = 1200.0 * std::log2(measured / hz);
                    expect(std::abs(cents) < 1.0, "wave " + juce::String(static_cast<int>(wave)) + " at " + juce::String(hz)
                                                      + " Hz is off by " + juce::String(cents, 3) + " cents");
                }
            }
        }

        beginTest("output_runs_two_samples_late");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            const float hz = 440.0f;
            const auto rendered = render(*generator, basicParams(Wave::sine, hz), 256);
            const float dt = hz / static_cast<float>(kHostRate);
            float error = 0.0f;
            for (std::size_t s = 2; s < rendered.left.size(); ++s)
            {
                // Sample n carries the phase reached after n + 1 steps; the output is n - 2.
                const float expected = 0.70710678f * std::sin(6.2831853f * dt * static_cast<float>(s - 1));
                error = std::max(error, std::abs(rendered.left[s] - expected));
            }
            expect(error < 1.0e-4f, "latency error " + juce::String(error, 6));
            expectEquals(rendered.left[0], 0.0f);
            expectEquals(rendered.left[1], 0.0f);
        }

        beginTest("alias_energy_far_below_two_point_polyblep");
        {
            for (int bin : { 75, 601, 1201 }) // 439 Hz, 3521 Hz (A7), 7037 Hz
            {
                const float ours = generatorInharmonicDb(bin);
                const float reference = referenceInharmonicDb(bin);
                const float hz = static_cast<float>(bin * kHostRate / kFftSize);
                logMessage("saw at " + juce::String(hz, 1) + " Hz, inharmonic energy: 4-point events "
                           + juce::String(ours, 1) + " dB, 2-point polyBLEP " + juce::String(reference, 1) + " dB");
                expect(ours < reference - 20.0f);
            }
            expect(generatorInharmonicDb(75) < -85.0f);
            expect(generatorInharmonicDb(601) < -75.0f);

            for (auto wave : { Wave::triangle, Wave::pulse })
            {
                const float db = generatorInharmonicDb(601, wave);
                logMessage("wave " + juce::String(static_cast<int>(wave)) + " at 3521 Hz, inharmonic energy " + juce::String(db, 1) + " dB");
                expect(db < -80.0f);
            }
        }

        beginTest("shape_morph_is_the_blend_of_its_neighbours");
        {
            SlotMemory a, b, c;
            auto triangle = makeGenerator(kHostRate, a);
            auto saw = makeGenerator(kHostRate, b);
            auto between = makeGenerator(kHostRate, c);
            auto params = basicParams(Wave::triangle, 330.0f);
            const auto t = render(*triangle, params, 4800);
            params.p[VaGenerator::shape] = 2.0f;
            const auto s = render(*saw, params, 4800);
            params.p[VaGenerator::shape] = 1.5f;
            const auto m = render(*between, params, 4800);
            float error = 0.0f;
            for (std::size_t i = 0; i < m.left.size(); ++i)
                error = std::max(error, std::abs(m.left[i] - 0.5f * (t.left[i] + s.left[i])));
            expect(error < 1.0e-5f, "morph error " + juce::String(error, 7));
        }

        beginTest("unison_is_power_normalised_and_mix_zero_is_the_centre_copy");
        {
            SlotMemory m1, m7, mc;
            auto one = makeGenerator(kHostRate, m1);
            auto seven = makeGenerator(kHostRate, m7);
            auto centreOnly = makeGenerator(kHostRate, mc);

            const auto single = render(*one, basicParams(Wave::saw, 220.0f), 96000);

            auto stackParams = basicParams(Wave::saw, 220.0f);
            stackParams.p[VaGenerator::unison] = 7.0f;
            stackParams.p[VaGenerator::detune] = 30.0f;
            stackParams.p[VaGenerator::randomPhase] = 1.0f;
            seven->noteOn(NoteContext {});
            const auto stack = render(*seven, stackParams, 96000);
            const float ratioDb = 20.0f * std::log10(rms(stack.left, 4800) / rms(single.left, 4800));
            logMessage("7-copy stack at 30 cents vs one copy: " + juce::String(ratioDb, 2) + " dB");
            expect(std::abs(ratioDb) < 1.5f);
            float peak = 0.0f;
            for (auto v : stack.left)
                peak = std::max(peak, std::abs(v));
            expect(peak < 2.0f);

            auto centreParams = basicParams(Wave::saw, 220.0f);
            centreParams.p[VaGenerator::unison] = 7.0f;
            centreParams.p[VaGenerator::detune] = 50.0f;
            centreParams.p[VaGenerator::mix] = 0.0f;
            const auto centre = render(*centreOnly, centreParams, 4800);
            expect(maxDifference(centre.left, std::vector<float>(single.left.begin(), single.left.begin() + 4800)) < 1.0e-5f);
        }

        beginTest("pan_and_spread_place_copies_in_the_stereo_field");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            auto hardLeft = basicParams(Wave::saw, 220.0f);
            hardLeft.pan = -1.0f;
            const auto l = render(*generator, hardLeft, 4800);
            expect(rms(l.left, 0) > 0.3f);
            expectWithinAbsoluteError(rms(l.right, 0), 0.0f, 1.0e-6f);
            expect(rms(l.mono, 0) > 0.5f); // the link carries the unpanned signal

            auto wide = basicParams(Wave::saw, 220.0f);
            wide.p[VaGenerator::unison] = 2.0f;
            wide.p[VaGenerator::detune] = 20.0f;
            wide.p[VaGenerator::spread] = 1.0f;
            const auto w = render(*generator, wide, 4800);
            expect(rms(w.left, 0) > 0.3f && rms(w.right, 0) > 0.3f);
            expect(maxDifference(w.left, w.right) > 0.1f);
        }

        beginTest("sync_locks_the_slave_to_the_master_and_stays_clean");
        {
            // Master 468.75 Hz: five periods are 512 samples exactly at 48 kHz.
            const float masterHz = 468.75f;
            const int total = 48000;
            SlotMemory mm, sm, fm;
            auto master = makeGenerator(kHostRate, mm);
            auto slave = makeGenerator(kHostRate, sm);
            auto freeRunning = makeGenerator(kHostRate, fm);
            const auto masterParams = basicParams(Wave::saw, masterHz);
            auto slaveParams = basicParams(Wave::saw, masterHz * 1.5f);
            slaveParams.combine = Combine::sync;
            const auto freeParams = basicParams(Wave::saw, masterHz * 1.5f);

            std::vector<float> synced(static_cast<std::size_t>(total)), unsynced(static_cast<std::size_t>(total));
            std::array<float, kSubBlock> l {}, r {};
            for (int start = 0; start < total; start += kSubBlock)
            {
                master->setLink({});
                master->process(l.data(), r.data(), kSubBlock, masterParams);
                slave->setLink(master->link());
                slave->process(l.data(), r.data(), kSubBlock, slaveParams);
                std::copy_n(l.begin(), kSubBlock, synced.begin() + start);
                freeRunning->setLink({});
                freeRunning->process(l.data(), r.data(), kSubBlock, freeParams);
                std::copy_n(l.begin(), kSubBlock, unsynced.begin() + start);
            }

            float syncedError = 0.0f, unsyncedError = 0.0f;
            for (std::size_t i = 4096; i + 512 < synced.size(); ++i)
            {
                syncedError = std::max(syncedError, std::abs(synced[i] - synced[i + 512]));
                unsyncedError = std::max(unsyncedError, std::abs(unsynced[i] - unsynced[i + 512]));
            }
            logMessage("periodicity over five master periods: synced " + juce::String(syncedError, 6)
                       + ", free-running " + juce::String(unsyncedError, 3));
            expect(syncedError < 1.0e-3f);
            expect(unsyncedError > 0.5f);

            // Synced saw at 1.5x, master on bin 75 at the voice rate, through the decimator.
            const int bin = 75;
            const float hz = static_cast<float>(bin * kHostRate / kFftSize);
            SlotMemory m2, s2;
            auto master2 = makeGenerator(kVoiceRate, m2);
            auto slave2 = makeGenerator(kVoiceRate, s2);
            auto p2 = basicParams(Wave::saw, hz * 1.5f);
            p2.combine = Combine::sync;
            const int voiceSamples = 2 * (4096 + kFftSize);
            std::vector<float> slaveOut(static_cast<std::size_t>(voiceSamples));
            for (int start = 0; start < voiceSamples; start += kSubBlock)
            {
                master2->setLink({});
                master2->process(l.data(), r.data(), kSubBlock, basicParams(Wave::saw, hz));
                slave2->setLink(master2->link());
                slave2->process(l.data(), r.data(), kSubBlock, p2);
                std::copy_n(l.begin(), kSubBlock, slaveOut.begin() + start);
            }
            const float db = inharmonicDb(decimate(slaveOut), 4096, bin, kHostRate);
            logMessage("hard-synced saw (ratio 1.5, 439 Hz master), inharmonic energy " + juce::String(db, 1) + " dB");
            expect(db < -80.0f);
        }

        beginTest("ring_and_am_follow_the_combine_amount");
        {
            const std::vector<float> half(4800, 0.5f);
            SlotMemory a, b, c, d;
            auto reference = makeGenerator(kHostRate, a);
            auto ring = makeGenerator(kHostRate, b);
            auto ringHalf = makeGenerator(kHostRate, c);
            auto am = makeGenerator(kHostRate, d);

            auto params = basicParams(Wave::saw, 220.0f);
            const auto dry = render(*reference, params, 4800);
            params.combine = Combine::ring;
            params.combineAmount = 1.0f;
            const auto full = render(*ring, params, 4800, &half);
            params.combineAmount = 0.5f;
            const auto partial = render(*ringHalf, params, 4800, &half);
            params.combine = Combine::am;
            params.combineAmount = 1.0f;
            const auto modulated = render(*am, params, 4800, &half);

            float ringError = 0.0f, partialError = 0.0f, amError = 0.0f;
            for (std::size_t i = 0; i < dry.left.size(); ++i)
            {
                ringError = std::max(ringError, std::abs(full.left[i] - 0.5f * dry.left[i]));
                partialError = std::max(partialError, std::abs(partial.left[i] - 0.75f * dry.left[i]));
                amError = std::max(amError, std::abs(modulated.left[i] - 0.75f * dry.left[i]));
            }
            expect(ringError < 1.0e-5f);
            expect(partialError < 1.0e-5f);
            expect(amError < 1.0e-5f);
        }

        beginTest("sub_adds_a_sine_one_octave_below");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            const float hz = 468.75f; // bin 80; the sub lands on bin 40
            auto params = basicParams(Wave::sine, hz);
            params.p[VaGenerator::sub] = 1.0f;
            const auto rendered = render(*generator, params, 4096 + kFftSize);
            const auto mags = magnitudes(rendered.left, 4096, false);
            const float ratioDb = 20.0f * std::log10(peakNear(mags, hz * 0.5, kHostRate) / peakNear(mags, hz, kHostRate));
            logMessage("sub vs main: " + juce::String(ratioDb, 2) + " dB");
            expect(std::abs(ratioDb) < 0.5f);
        }

        beginTest("fm_depth_follows_the_amount_and_stays_bounded");
        {
            std::vector<float> modulator(4800);
            for (std::size_t i = 0; i < modulator.size(); ++i)
                modulator[i] = std::sin(static_cast<float>(i) * 6.2831853f * 220.0f / 48000.0f);

            SlotMemory a, b, c;
            auto reference = makeGenerator(kHostRate, a);
            auto zero = makeGenerator(kHostRate, b);
            auto fm = makeGenerator(kHostRate, c);
            auto params = basicParams(Wave::sine, 440.0f);
            const auto dry = render(*reference, params, 4800);
            params.combine = Combine::fm;
            params.combineAmount = 0.0f;
            const auto none = render(*zero, params, 4800, &modulator);
            params.combineAmount = 0.6f;
            const auto modulated = render(*fm, params, 4800, &modulator);

            expect(maxDifference(none.left, dry.left) < 1.0e-6f);
            expect(maxDifference(modulated.left, dry.left) > 0.2f);
            float peak = 0.0f;
            for (auto v : modulated.left)
            {
                peak = std::max(peak, std::abs(v));
                expect(std::isfinite(v));
            }
            expect(peak <= 0.70711f);
        }

        beginTest("phase_rand_zero_repeats_and_one_varies_per_note");
        {
            SlotMemory memory;
            auto generator = makeGenerator(kHostRate, memory);
            auto params = basicParams(Wave::saw, 220.0f);
            const auto first = render(*generator, params, 64);
            generator->reset();
            generator->noteOn(NoteContext {});
            const auto second = render(*generator, params, 64);
            expect(maxDifference(first.left, second.left) < 1.0e-7f);

            params.p[VaGenerator::randomPhase] = 1.0f;
            generator->reset();
            generator->noteOn(NoteContext {});
            const auto third = render(*generator, params, 64);
            generator->reset();
            generator->noteOn(NoteContext {});
            const auto fourth = render(*generator, params, 64);
            expect(maxDifference(third.left, fourth.left) > 0.01f);
        }
    }
};

static V3GeneratorTests v3GeneratorTests;
