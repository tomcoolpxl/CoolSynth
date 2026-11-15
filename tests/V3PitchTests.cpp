#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/PitchInsert.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    /** A stereo signal through the pitch effect, block by block. `settingsAt` may change the settings as it goes. */
    template <typename SettingsAt>
    juce::AudioBuffer<float> through(PitchInsert& pitch, const juce::AudioBuffer<float>& input, SettingsAt&& settingsAt)
    {
        juce::AudioBuffer<float> out(2, input.getNumSamples());
        for (int start = 0; start < input.getNumSamples(); start += kBlock)
        {
            const int count = std::min(kBlock, input.getNumSamples() - start);
            juce::AudioBuffer<float> block(2, count);
            block.copyFrom(0, 0, input, 0, start, count);
            block.copyFrom(1, 0, input, 1, start, count);
            pitch.process(block, settingsAt(start));
            out.copyFrom(0, start, block, 0, 0, count);
            out.copyFrom(1, start, block, 1, 0, count);
        }
        return out;
    }

    juce::AudioBuffer<float> sines(std::initializer_list<double> frequencies, int length, float level = 0.3f)
    {
        juce::AudioBuffer<float> b(2, length);
        for (int i = 0; i < length; ++i)
        {
            double v = 0.0;
            for (const double f : frequencies)
                v += std::sin(juce::MathConstants<double>::twoPi * f * i / kRate);
            b.setSample(0, i, level * static_cast<float>(v));
            b.setSample(1, i, level * static_cast<float>(v));
        }
        return b;
    }

    juce::AudioBuffer<float> noise(int length, float level = 0.25f)
    {
        juce::AudioBuffer<float> b(2, length);
        juce::Random random(20261115);
        for (int i = 0; i < length; ++i)
        {
            const float v = level * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample(0, i, v);
            b.setSample(1, i, v);
        }
        return b;
    }

    /** The frequency of a near-sine from its rising zero crossings, found between samples. */
    double frequency(const juce::AudioBuffer<float>& b, int channel, int from, int to)
    {
        const float* x = b.getReadPointer(channel);
        double first = -1.0, last = -1.0;
        int periods = -1;
        for (int i = std::max(1, from); i < std::min(to, b.getNumSamples()); ++i)
        {
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (i - 1) - static_cast<double>(x[i - 1]) / (static_cast<double>(x[i]) - x[i - 1]);
                if (first < 0.0)
                    first = t;
                last = t;
                ++periods;
            }
        }
        return periods > 0 ? periods * kRate / (last - first) : 0.0;
    }

    double rms(const juce::AudioBuffer<float>& b, int channel, int from, int to)
    {
        double sum = 0.0;
        int count = 0;
        for (int i = std::max(0, from); i < std::min(to, b.getNumSamples()); ++i, ++count)
            sum += static_cast<double>(b.getSample(channel, i)) * b.getSample(channel, i);
        return count > 0 ? std::sqrt(sum / count) : 0.0;
    }

    /** Energy at one frequency over a stretch (Goertzel), normalised by its length. */
    double energyAt(const juce::AudioBuffer<float>& b, int channel, int from, int to, double hz)
    {
        const double w = juce::MathConstants<double>::twoPi * hz / kRate;
        const double coefficient = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        int count = 0;
        for (int i = std::max(0, from); i < std::min(to, b.getNumSamples()); ++i, ++count)
        {
            const double s0 = b.getSample(channel, i) + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
        return count > 0 ? power / (static_cast<double>(count) * count) : 0.0;
    }

    /** The largest step of the slope (second difference) in a stretch: what a click is made of. */
    double roughest(const juce::AudioBuffer<float>& b, int channel, int from, int to)
    {
        double worst = 0.0;
        for (int i = std::max(2, from); i < std::min(to, b.getNumSamples()); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(b.getSample(channel, i)) - 2.0 * b.getSample(channel, i - 1) + b.getSample(channel, i - 2)));
        return worst;
    }

    /** The level's swing in 5 ms pieces, in dB from the quietest piece to the loudest. */
    double swingDb(const juce::AudioBuffer<float>& b, int channel, int from, int to)
    {
        const int piece = static_cast<int>(0.005 * kRate);
        double lowest = 1.0e9, highest = 0.0;
        for (int at = from; at + piece <= to; at += piece)
        {
            const double level = rms(b, channel, at, at + piece);
            lowest = std::min(lowest, level);
            highest = std::max(highest, level);
        }
        return 20.0 * std::log10((highest + 1.0e-12) / (lowest + 1.0e-12));
    }

    PitchInsert::Settings shift(float semitones, int mode = 0, float character = 0.25f)
    {
        PitchInsert::Settings s;
        s.semitones = semitones;
        s.mode = mode;
        s.character = character;
        s.mix = 1.0f;
        return s;
    }
}

class V3PitchTests final : public juce::UnitTest
{
public:
    V3PitchTests() : juce::UnitTest("V3 pitch insert", "V3") {}

    void runTest() override
    {
        const int second = static_cast<int>(kRate);

        beginTest("heads_move_a_note_by_the_interval_at_its_level");
        {
            for (const float semitones : { 12.0f, 7.0f, -5.0f, -12.0f, 24.0f, -24.0f })
            {
                auto pitch = std::make_unique<PitchInsert>();
                pitch->prepare(kRate);
                const auto input = sines({ 440.0 }, second * 2);
                const auto out = through(*pitch, input, [&](int) { return shift(semitones); });
                const double wanted = 440.0 * std::exp2(semitones / 12.0);
                const double got = frequency(out, 0, second / 2, second * 2);
                expectWithinAbsoluteError(got / wanted, 1.0, 0.003, "shift " + juce::String(semitones) + " st: " + juce::String(got) + " Hz");
                const double gainDb = 20.0 * std::log10(rms(out, 0, second / 2, second * 2) / rms(input, 0, second / 2, second * 2));
                expectWithinAbsoluteError(gainDb, 0.0, 1.5, "level after " + juce::String(semitones) + " st");
            }
        }

        beginTest("clean_splices_keep_a_steady_level_rough_ones_glitch");
        {
            auto clean = std::make_unique<PitchInsert>();
            clean->prepare(kRate);
            const auto input = sines({ 330.0 }, second * 2);
            const auto smooth = through(*clean, input, [](int) { return shift(7.0f, 0, 0.0f); });
            auto rough = std::make_unique<PitchInsert>();
            rough->prepare(kRate);
            const auto glitchy = through(*rough, input, [](int) { return shift(7.0f, 0, 1.0f); });
            const double cleanSwing = swingDb(smooth, 0, second / 2, second * 2);
            const double roughSwing = swingDb(glitchy, 0, second / 2, second * 2);
            expectLessThan(cleanSwing, 1.0);
            expectGreaterThan(roughSwing, cleanSwing + 1.0);
        }

        beginTest("no_shift_is_a_plain_delay_in_both_modes");
        {
            const auto input = noise(second);
            for (const int mode : { 0, 1 })
            {
                auto pitch = std::make_unique<PitchInsert>();
                pitch->prepare(kRate);
                const auto out = through(*pitch, input, [&](int) { return shift(0.0f, mode, 0.0f); });
                double bestError = 1.0e9;
                int bestLag = -1;
                for (int lag = 0; lag < 4000; ++lag)
                {
                    double error = 0.0;
                    for (int i = second / 2; i < second / 2 + 2000; ++i)
                    {
                        const double d = out.getSample(0, i) - input.getSample(0, i - lag);
                        error += d * d;
                    }
                    if (error < bestError)
                    {
                        bestError = error;
                        bestLag = lag;
                    }
                }
                double residual = 0.0, signal = 0.0;
                for (int i = second / 4; i < second; ++i)
                {
                    const double d = out.getSample(0, i) - input.getSample(0, i - bestLag);
                    residual += d * d;
                    signal += static_cast<double>(input.getSample(0, i - bestLag)) * input.getSample(0, i - bestLag);
                }
                const double db = 10.0 * std::log10(residual / signal + 1.0e-30);
                expectLessThan(db, -60.0, (mode == 0 ? "Heads" : "Spectral") + juce::String(" differs from the input by ") + juce::String(db) + " dB");
                if (mode == 1)
                    expectEquals(bestLag, 2049, "the Spectral mode is late by its frame and a sample");
            }
        }

        beginTest("spectral_moves_a_note_and_a_chord");
        {
            auto pitch = std::make_unique<PitchInsert>();
            pitch->prepare(kRate);
            const auto note = through(*pitch, sines({ 440.0 }, second * 2), [](int) { return shift(7.0f, 1); });
            const double wanted = 440.0 * std::exp2(7.0 / 12.0);
            expectWithinAbsoluteError(frequency(note, 0, second / 2, second * 2) / wanted, 1.0, 0.003);

            auto chordPitch = std::make_unique<PitchInsert>();
            chordPitch->prepare(kRate);
            const auto chord = through(*chordPitch, sines({ 300.0, 400.0 }, second * 2, 0.2f), [](int) { return shift(12.0f, 1); });
            for (const double hz : { 600.0, 800.0 })
            {
                const double moved = energyAt(chord, 0, second / 2, second * 2, hz);
                const double left = std::max(energyAt(chord, 0, second / 2, second * 2, hz * 0.5), 1.0e-20);
                expectGreaterThan(10.0 * std::log10(moved / left), 30.0, "the chord's note at " + juce::String(hz) + " Hz");
            }
        }

        beginTest("spread_tunes_the_left_down_and_the_right_up");
        {
            for (const int mode : { 0, 1 })
            {
                auto pitch = std::make_unique<PitchInsert>();
                pitch->prepare(kRate);
                auto settings = shift(0.0f, mode);
                settings.spreadCents = 20.0f;
                const auto out = through(*pitch, sines({ 440.0 }, second * 2), [&](int) { return settings; });
                expectWithinAbsoluteError(frequency(out, 0, second / 2, second * 2), 440.0 * std::exp2(-20.0 / 1200.0), 0.15);
                expectWithinAbsoluteError(frequency(out, 1, second / 2, second * 2), 440.0 * std::exp2(20.0 / 1200.0), 0.15);
            }
        }

        beginTest("delay_and_feedback_climb_a_step_each_repeat");
        {
            auto pitch = std::make_unique<PitchInsert>();
            pitch->prepare(kRate);
            juce::AudioBuffer<float> input(2, second * 2);
            input.clear();
            const int burst = static_cast<int>(0.04 * kRate);
            for (int i = 0; i < burst; ++i)
            {
                const float v = 0.4f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 1000.0 * i / kRate))
                              * static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * i / burst));
                input.setSample(0, i, v);
                input.setSample(1, i, v);
            }
            auto settings = shift(12.0f);
            settings.delayMs = 250.0f;
            settings.feedback = 0.6f;
            const auto out = through(*pitch, input, [&](int) { return settings; });
            auto dominant = [&](int from, int to)
            {
                double best = 0.0, at = 0.0;
                for (const double hz : { 1000.0, 2000.0, 4000.0 })
                    if (const double e = energyAt(out, 0, from, to, hz); e > best)
                    {
                        best = e;
                        at = hz;
                    }
                return at;
            };
            const int q = static_cast<int>(0.25 * kRate);
            expectWithinAbsoluteError(rms(out, 0, 0, q - static_cast<int>(0.05 * kRate)), 0.0, 1.0e-4, "nothing before the delay");
            expectEquals(dominant(q - static_cast<int>(0.03 * kRate), q + static_cast<int>(0.09 * kRate)), 2000.0, "the first repeat an octave up");
            expectEquals(dominant(2 * q - static_cast<int>(0.03 * kRate), 2 * q + static_cast<int>(0.09 * kRate)), 4000.0, "the second two octaves up");
        }

        beginTest("mode_and_frame_changes_do_not_click");
        {
            auto pitch = std::make_unique<PitchInsert>();
            pitch->prepare(kRate);
            const auto input = sines({ 220.0 }, second * 2);
            const auto out = through(*pitch, input, [&](int at)
            {
                auto s = shift(5.0f, at < second / 2 || at >= 3 * second / 2 ? 0 : 1, 0.0f);
                s.windowMs = at < second ? 50.0f : 100.0f;
                return s;
            });
            const double steady = roughest(out, 0, second / 4, second / 2 - 2000);
            for (const int change : { second / 2, second, 3 * second / 2 })
                expectLessThan(roughest(out, 0, change - 500, change + static_cast<int>(0.2 * kRate)), 3.0 * steady + 1.0e-4,
                               "around " + juce::String(change) + " samples");
        }

        beginTest("wild_settings_stay_finite_and_silence_dies_away");
        {
            auto pitch = std::make_unique<PitchInsert>();
            pitch->prepare(kRate);
            juce::AudioBuffer<float> input(2, second * 6);
            input.clear();
            const auto loud = noise(second * 3, 0.8f);
            input.copyFrom(0, 0, loud, 0, 0, second * 3);
            input.copyFrom(1, 0, loud, 1, 0, second * 3);
            juce::Random random(7);
            PitchInsert::Settings s;
            const auto out = through(*pitch, input, [&](int at)
            {
                if (at < second * 3 && (at / kBlock) % 20 == 0)
                {
                    s.semitones = random.nextFloat() * 48.0f - 24.0f;
                    s.cents = random.nextFloat() * 200.0f - 100.0f;
                    s.spreadCents = random.nextFloat() * 50.0f;
                    s.windowMs = 10.0f + random.nextFloat() * 190.0f;
                    s.character = random.nextFloat();
                    s.mode = random.nextInt(2);
                    s.delayMs = random.nextFloat() * 1000.0f;
                    s.feedback = 0.5f * random.nextFloat();
                    s.mix = random.nextFloat();
                }
                return s;
            });
            float peak = 0.0f;
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite(out.getSample(ch, i));
                    peak = std::max(peak, std::abs(out.getSample(ch, i)));
                }
            expect(finite, "every sample is a number");
            expectLessThan(peak, 6.0f);
            expectLessThan(rms(out, 0, second * 5, second * 6), 1.0e-5);
        }

        beginTest("pitch_is_the_fourteenth_insert_type_and_a_bank_line_sets_it");
        {
            expectEquals(static_cast<int>(insertType::pitch), 13); // saved in patches: it stays the fourteenth
            expectEquals(juce::String(insertDescriptor(insertType::pitch).name), juce::String("Pitch"));
            expectEquals(insertDescriptor(insertType::pitch).usedParams, 11);

            const auto parsed = coolsynth::presets::parseBankFile(R"([Up A Fifth]
room = Night Bus
g1 = VA shape=saw
i1 = Pitch pitch=7 spread=12 mix=0.4 delay=125 feedback=0.3 mode=spectral window=100 character=0.1
)", "pitch.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            expectEquals(static_cast<int>(parsed.presets.size()), 1);
            bool typed = false, pitched = false, spectral = false;
            for (const auto& entry : parsed.presets.front().entries)
            {
                typed = typed || (entry.parameterId == "pA.i1.type" && entry.text == "Pitch");
                pitched = pitched || (entry.parameterId == "pA.i1.p1" && entry.text == "7");
                spectral = spectral || (entry.parameterId == "pA.i1.p9" && entry.text.equalsIgnoreCase("spectral"));
            }
            expect(typed, "the slot's type is Pitch");
            expect(pitched, "its first value is the shift");
            expect(spectral, "its ninth is the mode");

            // In a slot it runs, and its tail covers its repeats.
            auto slot = std::make_unique<InsertSlot>();
            slot->prepare(kRate, kBlock);
            auto settings = InsertSettings::of(insertType::pitch, { { 3, 1.0f }, { 4, 300.0f }, { 7, 0.5f } });
            expectGreaterThan(insertTailSeconds(settings), 2.0);
            double answered = 0.0;
            for (int block = 0; block < 100; ++block)
            {
                juce::AudioBuffer<float> buffer(2, kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = 0.3f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 440.0 * (block * kBlock + i) / kRate));
                    buffer.setSample(0, i, v);
                    buffer.setSample(1, i, v);
                }
                slot->process(buffer, settings, InsertContext {});
                if (block > 80)
                    answered += rms(buffer, 0, 0, kBlock);
            }
            expectGreaterThan(answered, 0.5);
        }
    }
};

static V3PitchTests v3PitchTests;
