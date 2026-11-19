#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "synth/v3/Inserts.h"
#include "synth/v3/StutterInsert.h"

/*
    The stutter effect (D220): when it catches, what it repeats, and what each repeat does. At 120 BPM and 48 kHz a
    beat is 24000 samples and a sixteenth 6000.
*/
namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr double kBeatsPerSample = 120.0 / 60.0 / kRate;

    std::vector<float> sine(double hz, int length, float level = 0.5f)
    {
        std::vector<float> x(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
            x[static_cast<std::size_t>(i)] = level * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
        return x;
    }

    std::vector<float> ramp(int length)
    {
        std::vector<float> x(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
            x[static_cast<std::size_t>(i)] = static_cast<float>(i % 6000) / 6000.0f;
        return x;
    }

    struct Run
    {
        std::vector<float> out;
        std::vector<bool> stuttering;
    };

    /** The same signal on both channels through the effect, block by block, the beat counting from 0. `settingsAt`
        gives the settings for a block's first sample. */
    template <typename SettingsAt>
    Run through(StutterInsert& stutter, const std::vector<float>& input, SettingsAt&& settingsAt)
    {
        Run r { std::vector<float>(input.size()), std::vector<bool>(input.size()) };
        for (std::size_t start = 0; start < input.size(); start += kBlock)
        {
            const int count = static_cast<int>(std::min<std::size_t>(kBlock, input.size() - start));
            juce::AudioBuffer<float> block(2, count);
            for (int ch = 0; ch < 2; ++ch)
                std::copy_n(input.data() + start, count, block.getWritePointer(ch));
            stutter.process(block, settingsAt(static_cast<int>(start)), static_cast<double>(start) * kBeatsPerSample, kBeatsPerSample);
            std::copy_n(block.getReadPointer(0), count, r.out.data() + start);
            for (int i = 0; i < count; ++i)
                r.stuttering[start + static_cast<std::size_t>(i)] = stutter.stutteringForTesting();
        }
        return r;
    }

    /** The sample where the grid first reaches `offset` in an interval, worked out as the effect does, block by block. */
    int gridSample(double offset, double interval)
    {
        double last = std::floor((0.0 - offset) / interval);
        for (int start = 0;; start += kBlock)
            for (int i = 0; i < kBlock; ++i)
            {
                const double beat = static_cast<double>(start) * kBeatsPerSample + kBeatsPerSample * static_cast<double>(i);
                const double cell = std::floor((beat - offset) / interval);
                if (cell != last)
                    return start + i;
                last = cell;
            }
    }

    /** The first block (its first sample) where the effect is stuttering, from `from` on, or -1. */
    int firstStutter(const Run& r, int from = 0)
    {
        for (std::size_t i = static_cast<std::size_t>(from); i < r.stuttering.size(); ++i)
            if (r.stuttering[i])
                return static_cast<int>(i);
        return -1;
    }

    std::unique_ptr<StutterInsert> makeStutter()
    {
        auto s = std::make_unique<StutterInsert>();
        s->prepare(kRate);
        return s;
    }

    float largestStep(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float step = 0.0f;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, x.size()); ++i)
            step = std::max(step, std::abs(x[i] - x[i - 1]));
        return step;
    }
}

class V3StutterTests final : public juce::UnitTest
{
public:
    V3StutterTests() : juce::UnitTest("V3 stutter", "V3") {}

    void runTest() override
    {
        const StutterInsert::Settings defaults = stutterSettings(InsertSettings::of(insertType::stutter));

        beginTest("a_new_type_at_the_end_with_beat_repeats_defaults");
        {
            expectEquals(static_cast<int>(insertType::stutter), 14, "saved in patches: it stays the fifteenth");
            expectEquals(juce::String(insertDescriptor(insertType::stutter).name), juce::String("Stutter"));
            expectEquals(insertDescriptor(insertType::stutter).usedParams, 12);
            expectEquals(defaults.intervalBeats, 4.0, "every bar");
            expectEquals(defaults.offsetBeats, 2.0, "on beat three");
            expectEquals(defaults.gridIndex, 4, "sixteenths");
            expectEquals(defaults.gateBeats, 1.0, "for a quarter note");
            expect(defaults.mode == StutterInsert::Mode::insert && ! defaults.hold && defaults.chance == 1.0f);
            expect(StutterInsert::tailSeconds(defaults, 120.0) > 0.6, "the slice and the repeats");
        }

        beginTest("on_the_grid_it_repeats_the_slice_it_caught");
        {
            auto stutter = makeStutter();
            const auto input = sine(997.0, 96000);
            const auto r = through(*stutter, input, [&](int) { return defaults; });
            const int start = gridSample(2.0, 4.0);
            expectWithinAbsoluteError(start, 48000, 1, "beat three of the first bar");
            expectEquals(firstStutter(r), start / kBlock * kBlock, "and the effect started in that block");
            for (int i = 0; i < start; ++i)
                if (r.out[static_cast<std::size_t>(i)] != input[static_cast<std::size_t>(i)])
                {
                    expect(false, "before it, the sound as it is");
                    break;
                }
            // The slice plays through, then comes back four times (a quarter note of sixteenths), the same samples.
            const int slice = stutter->sliceLengthForTesting();
            expectEquals(slice, 6000);
            float worst = 0.0f;
            for (int repeat = 1; repeat <= 4; ++repeat)
                for (int k = 200; k < slice - 200; ++k)
                    worst = std::max(worst, std::abs(r.out[static_cast<std::size_t>(start + repeat * slice + k)] - input[static_cast<std::size_t>(start + k)]));
            expectLessThan(worst, 1.0e-6f, "each repeat is the slice");
            const int end = start + 5 * slice + 500;
            expect(! r.stuttering[static_cast<std::size_t>(end)], "over after the gate");
            expectWithinAbsoluteError(r.out[static_cast<std::size_t>(end + 100)], input[static_cast<std::size_t>(end + 100)], 1.0e-6f, "the sound again");
        }

        beginTest("chance_decides_and_hold_catches_at_once");
        {
            auto never = defaults;
            never.chance = 0.0f;
            auto stutter = makeStutter();
            const auto input = sine(440.0, 200000);
            const auto quiet = through(*stutter, input, [&](int) { return never; });
            expectEquals(firstStutter(quiet), -1, "Chance 0: never on the grid");

            // Held from sample 10000 to 40000: it catches at once and repeats past the gate until let go.
            auto held = makeStutter();
            const auto r = through(*held, input, [&](int at)
            {
                auto s = never;
                s.hold = at >= 10000 && at < 40000;
                return s;
            });
            expectWithinAbsoluteError(firstStutter(r), 10240, 1, "the block where Hold came on");
            expect(r.stuttering[30000], "still repeating past a quarter note's gate");
            expect(! r.stuttering[40000 + 2 * kBlock], "over within the edge time once let go");
        }

        beginTest("insert_mix_and_gate");
        {
            const auto input = sine(300.0, 72000);
            auto run = [&](StutterInsert::Mode mode)
            {
                auto s = defaults;
                s.mode = mode;
                auto stutter = makeStutter();
                return through(*stutter, input, [&](int) { return s; }).out;
            };
            const auto insert = run(StutterInsert::Mode::insert);
            const auto mix = run(StutterInsert::Mode::mix);
            const auto gate = run(StutterInsert::Mode::gate);
            const std::size_t inRepeat = 48000 + 6000 + 3000; // the middle of the first repeat
            expectWithinAbsoluteError(mix[inRepeat], input[inRepeat] + insert[inRepeat], 1.0e-5f, "Mix: the sound and the repeat");
            expectWithinAbsoluteError(gate[inRepeat], insert[inRepeat], 1.0e-6f, "Gate: the repeat alone");
            expectWithinAbsoluteError(gate[20000], 0.0f, 1.0e-6f, "Gate: nothing between stutters");
            expectWithinAbsoluteError(gate[48000 + 3000], 0.0f, 1.0e-6f, "Gate: nothing while the slice is caught");
        }

        beginTest("each_repeat_lower_shorter_quieter_or_backwards");
        {
            const auto input = ramp(96000);
            const int caught = gridSample(2.0, 4.0); // the slice from here, then the repeats
            // The highest the ramp gets in a repeat that starts `from` samples after the slice and lasts `length`.
            auto repeatPeak = [&](const StutterInsert::Settings& s, int from, int length)
            {
                auto stutter = makeStutter();
                const auto r = through(*stutter, input, [&](int) { return s; });
                float peak = 0.0f;
                for (int k = 200; k < length - 200; ++k)
                    peak = std::max(peak, r.out[static_cast<std::size_t>(caught + 6000 + from + k)]);
                return peak;
            };
            auto decaying = defaults;
            decaying.decay = 0.5f;
            expectWithinAbsoluteError(repeatPeak(decaying, 6000, 6000) / repeatPeak(decaying, 0, 6000), 0.5f, 0.01f, "Decay 50 %: half as loud");

            auto rolling = defaults;
            rolling.roll = 1.0f;
            // The ramp climbs over the slice: the second repeat, half as long, reaches half as high.
            expectWithinAbsoluteError(repeatPeak(rolling, 6000, 3000), 0.5f * repeatPeak(rolling, 0, 6000), 0.03f, "Roll full: half as long");

            // An octave lower: the second repeat lasts twice as long and climbs half as fast.
            auto dropping = defaults;
            dropping.pitchSemitones = 12.0f;
            dropping.gateBeats = 2.0;
            {
                auto stutter = makeStutter();
                const auto r = through(*stutter, input, [&](int) { return dropping; });
                const int second = caught + 2 * 6000;
                const float slope = r.out[static_cast<std::size_t>(second + 2000)] - r.out[static_cast<std::size_t>(second + 1000)];
                expectWithinAbsoluteError(slope, 1000.0f / 6000.0f / 2.0f, 0.002f, "half speed, an octave down");
            }

            auto reversed = defaults;
            reversed.reverse = StutterInsert::Reverse::all;
            {
                auto stutter = makeStutter();
                const auto r = through(*stutter, input, [&](int) { return reversed; });
                const int first = caught + 6000;
                expect(r.out[static_cast<std::size_t>(first + 1000)] > r.out[static_cast<std::size_t>(first + 4000)], "backwards: the ramp falls");
            }
        }

        beginTest("variation_picks_other_lengths_of_the_same_kind");
        {
            auto varied = defaults;
            varied.variation = 1.0f;
            varied.intervalBeats = 1.0; // a stutter every beat
            varied.offsetBeats = 0.0;
            varied.gateBeats = 0.25;
            auto stutter = makeStutter();
            std::set<int> lengths;
            const auto input = sine(220.0, 24000 * 40);
            juce::AudioBuffer<float> block(2, kBlock);
            for (std::size_t start = 0; start < input.size(); start += kBlock)
            {
                block.clear();
                stutter->process(block, varied, static_cast<double>(start) * kBeatsPerSample, kBeatsPerSample);
                if (stutter->stutteringForTesting())
                    lengths.insert(stutter->sliceLengthForTesting());
            }
            expectGreaterThan(static_cast<int>(lengths.size()), 1, "more than one length");
            for (const int length : lengths)
            {
                const double beats = length * kBeatsPerSample;
                const double straight = std::log2(beats);
                expect(std::abs(straight - std::round(straight)) < 1.0e-3, "straight steps from a straight grid: " + juce::String(beats, 4));
            }
        }

        beginTest("no_clicks_at_the_cuts");
        {
            for (const int grid : { 4, 6, 8 }) // sixteenths, thirty-seconds, sixty-fourths
            {
                auto s = defaults;
                s.gridIndex = grid;
                s.reverse = StutterInsert::Reverse::alternate;
                s.roll = 0.5f;
                auto stutter = makeStutter();
                const auto input = sine(220.0, 96000);
                const auto r = through(*stutter, input, [&](int) { return s; });
                const float own = largestStep(input, 0, input.size());
                const float step = largestStep(r.out, 40000, 96000);
                logMessage("grid " + juce::String(StutterInsert::gridBeats[static_cast<std::size_t>(grid)], 4) + " beats: largest step "
                           + juce::String(step, 4) + " (the sine's own " + juce::String(own, 4) + ")");
                expectLessThan(step, 0.03f, "the cuts fade");
            }
        }
    }
};

static V3StutterTests v3StutterTests;
