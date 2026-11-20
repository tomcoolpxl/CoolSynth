#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

#include "synth/HalfBand2x.h"
#include "synth/v3/ChipGenerator.h"
#include "synth/v3/Voice.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Chip = ChipGenerator;
    using Model = ChipGenerator::Model;
    using Wave = ChipGenerator::Wave;
    using Sequence = ChipGenerator::Sequence;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;
    constexpr int kFftOrder = 15;
    constexpr int kFftSize = 1 << kFftOrder;

    struct Rig
    {
        SlotMemory memory;
        ChipGenerator generator;
    };

    std::unique_ptr<Rig> makeChip(std::uint32_t seed = 0)
    {
        auto r = std::make_unique<Rig>();
        r->memory.allocate(ChipGenerator::memoryFloats(kVoiceRate, kSubBlock));
        static std::uint32_t rigs = 100;
        r->memory.setSeed(seed != 0 ? seed : ++rigs);
        r->generator.prepare(kVoiceRate, kSubBlock, r->memory);
        r->generator.noteOn(NoteContext {});
        return r;
    }

    SlotParams chipParams(Model model, Wave wave, float hz)
    {
        SlotParams p;
        const auto& d = ChipGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            p.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
        p.p[Chip::chip] = static_cast<float>(model);
        p.p[Chip::wave] = static_cast<float>(wave);
        p.frequencyHz = hz;
        return p;
    }

    std::vector<float> render(ChipGenerator& g, const SlotParams& p, int samples, const GeneratorLink* link = nullptr)
    {
        std::vector<float> out(static_cast<std::size_t>(samples));
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            if (link != nullptr)
                g.setLink({ link->signal != nullptr ? link->signal + s : nullptr, link->wraps != nullptr ? link->wraps + s : nullptr });
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
                out[static_cast<std::size_t>(s + i)] = g.link().signal[i];
        }
        return out;
    }

    std::vector<float> decimate(const std::vector<float>& x)
    {
        coolsynth::synth::HalfBandDecimator2x d;
        std::vector<float> out(x.size() / 2);
        d.processBlock(x.data(), out.data(), static_cast<int>(out.size()));
        return out;
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    /** The frequency in a stretch, from its upward zero crossings. */
    double frequencyIn(const std::vector<float>& x, std::size_t from, std::size_t to, double rate)
    {
        std::vector<double> crossings;
        for (std::size_t i = from + 1; i < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - x[i - 1] / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return rate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
    }

    int zeroCrossings(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        int count = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
                ++count;
        return count;
    }

    /** The largest component off the harmonics below 20 kHz, in dB under the fundamental, at the host rate. */
    double worstAlias(const std::vector<float>& host, double fundamentalHz)
    {
        juce::dsp::FFT fft(kFftOrder);
        std::vector<float> frame(2 * kFftSize, 0.0f);
        const std::size_t from = 4096;
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * i / kFftSize;
            const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
            frame[static_cast<std::size_t>(i)] = static_cast<float>(host[from + static_cast<std::size_t>(i)] * w);
        }
        fft.performFrequencyOnlyForwardTransform(frame.data());
        const double binHz = (kVoiceRate / 2.0) / kFftSize;
        double fundamental = 0.0, worst = 0.0;
        for (std::size_t b = static_cast<std::size_t>(50.0 / binHz); b < static_cast<std::size_t>(20000.0 / binHz); ++b)
        {
            const double hz = static_cast<double>(b) * binHz;
            const double harmonic = std::round(hz / fundamentalHz);
            const bool onHarmonic = harmonic >= 1.0 && std::abs(hz - harmonic * fundamentalHz) < 8.0 * binHz;
            if (harmonic == 1.0 && onHarmonic)
                fundamental = std::max(fundamental, static_cast<double>(frame[b]));
            else if (! onHarmonic)
                worst = std::max(worst, static_cast<double>(frame[b]));
        }
        return 20.0 * std::log10(std::max(1.0e-12, worst) / std::max(1.0e-12, fundamental));
    }

    /** The cycle a register settles into (the Game Boy's short mode overwrites a bit, so a start may lead into it). */
    template <typename Function>
    int periodOf(std::uint32_t seed, Function step, int limit)
    {
        std::uint32_t start = seed;
        for (int i = 0; i < 1000; ++i)
            start = step(start);
        std::uint32_t v = step(start);
        for (int n = 1; n <= limit; ++n)
        {
            if (v == start)
                return n;
            v = step(v);
        }
        return -1;
    }
}

class V3ChipTests final : public juce::UnitTest
{
public:
    V3ChipTests() : juce::UnitTest("V3 chip generator", "V3") {}

    void runTest() override
    {
        beginTest("chip_is_the_last_generator_type_with_twelve_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 11);
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::chip).name), juce::String("Chip"));
            const auto& d = ChipGenerator::descriptor();
            expectEquals(d.usedParams, maxSlotParameters);
            for (int i = 0; i < d.usedParams; ++i)
            {
                const auto& p = d.params[static_cast<std::size_t>(i)];
                if (p.options != nullptr)
                    expectEquals(juce::StringArray::fromTokens(p.options, "|", "").size(), static_cast<int>(p.max - p.min) + 1, p.name);
            }
        }

        beginTest("every_chip_plays_every_wave_at_about_a_saws_loudness");
        {
            juce::String table;
            for (int m = 0; m < ChipGenerator::modelCount; ++m)
            {
                table << "\n  " << juce::StringArray::fromTokens(ChipGenerator::descriptor().params[0].options, "|", "")[m] << ":";
                for (int w = 0; w < ChipGenerator::waveCount; ++w)
                {
                    auto rig = makeChip();
                    const auto out = render(rig->generator, chipParams(static_cast<Model>(m), static_cast<Wave>(w), 220.0f), 24000);
                    bool finite = true;
                    float peak = 0.0f;
                    for (const float v : out)
                    {
                        finite = finite && std::isfinite(v);
                        peak = std::max(peak, std::abs(v));
                    }
                    const double level = rms(out, 4000, out.size());
                    table << " " << juce::String(20.0 * std::log10(std::max(1.0e-9, level)), 1);
                    const bool combined = w >= static_cast<int>(Wave::triSaw) && w <= static_cast<int>(Wave::triSawPulse);
                    const juce::String what = juce::String(m) + "/" + juce::String(w);
                    expect(finite, "finite " + what);
                    expectLessThan(peak, 2.0f, "peak " + what);
                    expectGreaterThan(level, combined ? 0.02 : 0.1, "level " + what);
                    expectLessThan(level, 1.0, "level " + what);
                }
            }
            logMessage("RMS in dB, by wave:" + table);
        }

        beginTest("the_pulse_is_band_limited_and_raw_lets_the_aliases_through");
        {
            const float hz = 2793.83f; // F7
            auto smooth = makeChip();
            auto p = chipParams(Model::sid8580, Wave::pulse, hz);
            p.p[Chip::duty] = 0.37f;
            const double limited = worstAlias(decimate(render(smooth->generator, p, 2 * kFftSize + 16384)), hz);
            auto raw = makeChip();
            p.p[Chip::raw] = 1.0f;
            const double aliased = worstAlias(decimate(render(raw->generator, p, 2 * kFftSize + 16384)), hz);
            logMessage("largest alias at F7: " + juce::String(limited, 1) + " dB band-limited, " + juce::String(aliased, 1) + " dB raw");
            expectLessThan(limited, -60.0);
            expectGreaterThan(aliased, limited + 20.0);

            auto noise = makeChip();
            auto n = chipParams(Model::nes, Wave::triangle, 3520.0f);
            const double triangle = worstAlias(decimate(render(noise->generator, n, 2 * kFftSize + 16384)), 3520.0);
            logMessage("largest alias of the NES triangle at A7: " + juce::String(triangle, 1) + " dB");
            expectLessThan(triangle, -70.0);
        }

        beginTest("each_chip_plays_its_own_pulse_widths");
        {
            expectEquals(ChipGenerator::quantiseDuty(Model::nes, 0.3f), 0.25f);
            expectEquals(ChipGenerator::quantiseDuty(Model::gameBoy, 0.05f), 0.125f);
            expectEquals(ChipGenerator::quantiseDuty(Model::ay, 0.1f), 0.5f);
            expectEquals(ChipGenerator::quantiseDuty(Model::scc, 0.3f), 10.0f / 32.0f);
            expectWithinAbsoluteError(ChipGenerator::quantiseDuty(Model::sid6581, 0.3f), 0.3f, 1.0f / 4096.0f);
            for (const auto& [model, knob, heard] : { std::tuple { Model::nes, 0.3f, 0.25 }, std::tuple { Model::sid8580, 0.3f, 0.3 },
                                                      std::tuple { Model::ym, 0.1f, 0.5 } })
            {
                auto rig = makeChip();
                auto p = chipParams(model, Wave::pulse, 110.0f);
                p.p[Chip::duty] = knob;
                p.p[Chip::output] = 1.0f; // Clean: no high-pass droop
                const auto out = render(rig->generator, p, 96000);
                int high = 0;
                for (std::size_t i = 9600; i < out.size(); ++i)
                    high += out[i] > 0.0f ? 1 : 0;
                expectWithinAbsoluteError(static_cast<double>(high) / static_cast<double>(out.size() - 9600), heard, 0.01);
            }
        }

        beginTest("the_noise_registers_have_the_chips_lengths_and_pitches");
        {
            using R = ChipGenerator::Register;
            auto stepOf = [](R r) { return [r](std::uint32_t v) { return ChipGenerator::stepRegister(v, r); }; };
            expectEquals(periodOf(1u, stepOf(R::nesLong), 40000), 32767, "NES long");
            expectEquals(periodOf(1u, stepOf(R::nesShort), 40000), 93, "NES short");
            expectEquals(periodOf(1u, stepOf(R::gameBoyShort), 40000), 127, "Game Boy 7-bit");
            expectEquals(periodOf(1u, stepOf(R::ay17), 200000), 131071, "AY 17-bit");
            expectEquals(periodOf(0x7FFFF8u, stepOf(R::sid23), 9000000), 8388607, "SID 23-bit");

            // The NES plays sixteen noise pitches, the AY 31; the SID any.
            for (const double wanted : { 3000.0, 20000.0, 150000.0 })
            {
                const double nes = ChipGenerator::noiseClock(Model::nes, R::nesLong, wanted);
                bool one = false;
                for (const double period : { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 })
                    one = one || std::abs(nes - 1789773.0 / period) < 1.0e-6;
                expect(one, "an NES rate for " + juce::String(wanted));
                const double ay = ChipGenerator::noiseClock(Model::ay, R::ay17, wanted);
                const double n = 1773400.0 / 16.0 / ay;
                expectWithinAbsoluteError(n, std::round(n), 1.0e-6, "an AY period");
                expectEquals(ChipGenerator::noiseClock(Model::sid8580, R::sid23, wanted), wanted);
            }

            auto rig = makeChip();
            const auto out = render(rig->generator, chipParams(Model::gameBoy, Wave::noiseShort, 440.0f), 48000);
            expectGreaterThan(rms(out, 4000, out.size()), 0.2, "a Game Boy buzz");
        }

        beginTest("an_arpeggio_steps_through_the_chord_a_frame_at_a_time");
        {
            auto rig = makeChip();
            auto p = chipParams(Model::sid8580, Wave::triangle, 220.0f);
            p.p[Chip::sequence] = static_cast<float>(Sequence::arpMajor);
            p.p[Chip::depth] = 0.0f;  // one octave
            p.p[Chip::frame] = 2.0f;  // 60 Hz
            p.p[Chip::output] = 1.0f;
            const auto out = render(rig->generator, p, 16000);
            constexpr std::array<int, 3> chord { 0, 4, 7 };
            for (int k = 0; k < 9; ++k)
            {
                const auto from = static_cast<std::size_t>(k * 1600 + 150), to = static_cast<std::size_t>((k + 1) * 1600 - 50);
                const double heard = frequencyIn(out, from, to, kVoiceRate);
                const double wanted = 220.0 * std::exp2(chord[static_cast<std::size_t>(k % 3)] / 12.0);
                expectWithinAbsoluteError(1200.0 * std::log2(heard / wanted), 0.0, 15.0, "frame " + juce::String(k));
            }
        }

        beginTest("decay_steps_down_at_the_chips_clock");
        {
            // The NES: a step every quarter frame at Decay 1, all fifteen in 62 ms.
            auto nes = makeChip();
            auto p = chipParams(Model::nes, Wave::pulse, 440.0f);
            p.p[Chip::decay] = 1.0f;
            auto out = render(nes->generator, p, static_cast<int>(0.04 * kVoiceRate));
            expectGreaterThan(nes->generator.volumeForTesting(), 0, "still sounding at 40 ms");
            out = render(nes->generator, p, static_cast<int>(0.04 * kVoiceRate));
            expectEquals(nes->generator.volumeForTesting(), 0, "silent by 80 ms");
            expectLessThan(rms(out, out.size() - 480, out.size()), 1.0e-3, "and quiet in its last 5 ms");

            // The AY: a step a frame, through its logarithmic levels: halfway down is far below half the level.
            auto ay = makeChip();
            auto a = chipParams(Model::ay, Wave::pulse, 440.0f);
            a.p[Chip::decay] = 1.0f;
            render(ay->generator, a, static_cast<int>(0.15 * kVoiceRate));
            expectEquals(ay->generator.volumeForTesting(), 8);
            expectLessThan(ay->generator.gainForTesting(), 0.15f, "level 8 of the AY's DAC, an eighth of the top");

            // The SID: its decay stage, Decay 1 its fastest (6 ms on the data sheet).
            auto sid = makeChip();
            auto s = chipParams(Model::sid6581, Wave::saw, 440.0f);
            s.p[Chip::decay] = 1.0f;
            render(sid->generator, s, static_cast<int>(0.003 * kVoiceRate));
            expectGreaterThan(sid->generator.volumeForTesting(), 0, "SID still sounding at 3 ms");
            render(sid->generator, s, static_cast<int>(0.01 * kVoiceRate));
            expectEquals(sid->generator.volumeForTesting(), 0, "SID down by 13 ms");
        }

        beginTest("a_kick_starts_with_a_frame_of_noise_and_falls");
        {
            auto rig = makeChip();
            auto p = chipParams(Model::nes, Wave::triangle, 110.0f);
            p.p[Chip::sequence] = static_cast<float>(Sequence::kick);
            p.p[Chip::depth] = 0.5f; // two octaves down to the note
            p.p[Chip::frame] = 2.0f;
            p.p[Chip::output] = 1.0f;
            const auto out = render(rig->generator, p, 30 * 1600);
            const int noise = zeroCrossings(out, 100, 1550);
            const double early = frequencyIn(out, 1650, 3150, kVoiceRate), late = frequencyIn(out, 7 * 1600 + 50, 9 * 1600 - 50, kVoiceRate);
            logMessage("kick: " + juce::String(noise) + " crossings in the noise frame, " + juce::String(early, 0) + " Hz then "
                       + juce::String(late, 0) + " Hz");
            expectGreaterThan(noise, 60, "a frame of noise");
            expectGreaterThan(late, 90.0, "the tone is heard");
            expectGreaterThan(early, 1.5 * late, "the pitch falls");
            expectLessThan(rms(out, 20 * 1600, 30 * 1600), 1.0e-4, "and it ends");
        }

        beginTest("hard_restart_is_silent_for_its_first_step");
        {
            auto rig = makeChip();
            auto p = chipParams(Model::sid8580, Wave::saw, 220.0f);
            p.p[Chip::sequence] = static_cast<float>(Sequence::hardRestart);
            p.p[Chip::frame] = 2.0f;
            const auto out = render(rig->generator, p, 4 * 1600);
            expectLessThan(rms(out, 0, 1590), 1.0e-4, "the first frame");
            expectGreaterThan(rms(out, 1700, 3200), 0.2, "then the note");
        }

        beginTest("sync_restarts_the_cycle_at_the_generator_before");
        {
            const int samples = 48000;
            std::vector<float> wraps(static_cast<std::size_t>(samples), -1.0f), signal(static_cast<std::size_t>(samples), 0.0f);
            const double master = 100.0;
            double ph = 0.0;
            for (int i = 0; i < samples; ++i)
            {
                const double next = ph + master / kVoiceRate;
                if (next >= 1.0)
                    wraps[static_cast<std::size_t>(i)] = static_cast<float>((1.0 - ph) / (master / kVoiceRate));
                ph = next - std::floor(next);
            }
            auto rig = makeChip();
            auto p = chipParams(Model::sid8580, Wave::saw, 330.0f);
            p.combine = Combine::sync;
            const GeneratorLink link { signal.data(), wraps.data() };
            const auto out = render(rig->generator, p, samples, &link);
            float worst = 0.0f;
            for (std::size_t i = 20000; i < 30000; ++i)
                worst = std::max(worst, std::abs(out[i] - out[i + 960]));
            expectLessThan(worst, 0.05f, "it repeats at the master's 100 Hz");
        }

        beginTest("the_nes_output_high_passes_and_clean_does_not");
        {
            auto chip = makeChip(), clean = makeChip();
            auto p = chipParams(Model::nes, Wave::pulse, 27.5f);
            const auto a = render(chip->generator, p, 96000);
            p.p[Chip::output] = 1.0f;
            const auto b = render(clean->generator, p, 96000);
            // A 50 % pulse at 27.5 Hz: 1745 samples high. Through 90 Hz the plateau sags; clean it stays.
            auto sag = [](const std::vector<float>& x)
            {
                std::size_t rise = 48000;
                while (rise < x.size() - 1 && ! (x[rise - 1] < 0.0f && x[rise] >= 0.0f))
                    ++rise;
                return std::abs(x[rise + 100]) - std::abs(x[rise + 1600]);
            };
            expectGreaterThan(sag(a), 0.3f, "chip");
            expectLessThan(std::abs(sag(b)), 0.01f, "clean");
        }

        beginTest("the_same_settings_play_the_same_and_the_combined_waves_sweep_smoothly");
        {
            auto one = makeChip(7), two = makeChip(7), other = makeChip(8);
            auto p = chipParams(Model::nes, Wave::pulse, 440.0f);
            p.p[Chip::sequence] = static_cast<float>(Sequence::snare);
            const auto a = render(one->generator, p, 9600), b = render(two->generator, p, 9600), c = render(other->generator, p, 9600);
            expect(a == b, "one seed, one sound");
            expect(a != c, "each slot its own noise");

            // A width sweep makes no step larger than the wave's own band-limited edges do at any width held still.
            auto largestStep = [](float width)
            {
                const bool sweeping = width < 0.0f;
                auto rig = makeChip();
                auto q = chipParams(Model::sid8580, Wave::triPulse, 110.0f);
                std::array<float, kSubBlock> l {}, r {};
                float previous = 0.0f, largest = 0.0f;
                const int blocks = static_cast<int>(kVoiceRate / kSubBlock);
                for (int k = 0; k < blocks; ++k)
                {
                    q.p[Chip::duty] = sweeping ? 0.1f + 0.8f * static_cast<float>(k) / static_cast<float>(blocks) : width;
                    rig->generator.process(l.data(), r.data(), kSubBlock, q);
                    for (int i = 0; i < kSubBlock; ++i)
                    {
                        const float v = rig->generator.link().signal[i];
                        if (k > 0 || i > 2)
                            largest = std::max(largest, std::abs(v - previous));
                        previous = v;
                    }
                }
                return largest;
            };
            float still = 0.0f;
            for (int w = 1; w < ChipWaveBank::widths - 1; ++w) // the tables' own widths, where an edge is sharpest
                still = std::max(still, largestStep((static_cast<float>(w) + 0.5f) / static_cast<float>(ChipWaveBank::widths)));
            const float swept = largestStep(-1.0f);
            logMessage("largest step in Tri+Pulse: " + juce::String(still, 3) + " held at any width, " + juce::String(swept, 3) + " swept");
            expectLessThan(swept, 1.1f * still);
        }
    }
};

static V3ChipTests v3ChipTests;
