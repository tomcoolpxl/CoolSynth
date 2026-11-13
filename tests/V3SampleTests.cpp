#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/GrainGenerator.h"
#include "synth/v3/Part.h"
#include "synth/v3/SampleGenerator.h"
#include "synth/v3/Samples.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 96000.0; // a voice's rate: twice the samples' own
    constexpr int kSubBlock = 32;
    constexpr int kLength = SampleBank::length;
    constexpr double kC4 = SampleBank::rootHz;

    template <typename Generator>
    struct Rig
    {
        SlotMemory memory;
        Generator generator;
    };

    template <typename Generator>
    std::unique_ptr<Rig<Generator>> makeRig()
    {
        auto rig = std::make_unique<Rig<Generator>>();
        rig->memory.allocate(Generator::memoryFloats(kRate, kSubBlock));
        rig->generator.prepare(kRate, kSubBlock, rig->memory);
        rig->generator.noteOn(NoteContext {});
        return rig;
    }

    SlotParams sampleParams(SampleBank::Sample which, double hz, bool loop = true)
    {
        SlotParams p;
        p.p[SampleGenerator::sample] = static_cast<float>(which);
        p.p[SampleGenerator::start] = 0.0f;
        p.p[SampleGenerator::loop] = loop ? 1.0f : 0.0f;
        p.p[SampleGenerator::loopStart] = 0.0f;
        p.p[SampleGenerator::loopEnd] = 1.0f;
        p.p[SampleGenerator::fade] = 0.2f;
        p.p[SampleGenerator::reverse] = 0.0f;
        p.frequencyHz = static_cast<float>(hz);
        return p;
    }

    SlotParams grainParams(SampleBank::Sample which, double hz, float sizeMs, float density)
    {
        SlotParams p;
        p.p[GrainGenerator::sample] = static_cast<float>(which);
        p.p[GrainGenerator::position] = 0.3f;
        p.p[GrainGenerator::scan] = 0.0f;
        p.p[GrainGenerator::size] = sizeMs;
        p.p[GrainGenerator::density] = density;
        p.p[GrainGenerator::spray] = 0.3f;
        p.p[GrainGenerator::pitchSpray] = 0.0f;
        p.p[GrainGenerator::spread] = 0.0f;
        p.frequencyHz = static_cast<float>(hz);
        return p;
    }

    struct Stereo
    {
        std::vector<float> left, right;
    };

    template <typename Generator>
    Stereo render(Generator& g, const SlotParams& p, int samples)
    {
        Stereo out;
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            out.left.insert(out.left.end(), l.begin(), l.begin() + n);
            out.right.insert(out.right.end(), r.begin(), r.begin() + n);
        }
        return out;
    }

    /** A centred mono generator's signal: left over cos(45 degrees). */
    std::vector<float> mono(const Stereo& s)
    {
        std::vector<float> m(s.left.size());
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = (s.left[i] + s.right[i]) * 0.70710678f;
        return m;
    }

    double rms(const float* x, std::size_t count)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, count)));
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to) { return rms(x.data() + from, to - from); }

    double zeroCrossingHz(const float* x, int count, double rate)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (int i = 1; i < count; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double at = (i - 1) + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
                if (first < 0.0)
                    first = at;
                last = at;
                ++crossings;
            }
        return crossings > 1 ? rate * (crossings - 1) / (last - first) : 0.0;
    }

    /** The level of the component at f over a Hann window, as a sine's amplitude. */
    double levelAt(const float* x, int n, double f, double rate)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (i + 0.5) / n);
            const double t = 2.0 * juce::MathConstants<double>::pi * f * i / rate;
            re += w * x[i] * std::cos(t);
            im += w * x[i] * std::sin(t);
        }
        return 4.0 * std::sqrt(re * re + im * im) / n;
    }

    float largestStep(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float step = 0.0f;
        for (std::size_t i = from + 1; i < to; ++i)
            step = std::max(step, std::abs(x[i] - x[i - 1]));
        return step;
    }

    /** The lag near `expected` at which a signal best repeats, to a fraction of a sample. */
    double periodOf(const float* x, int count, double expected)
    {
        const int centre = static_cast<int>(std::lround(expected));
        std::vector<double> r;
        const int span = 8;
        for (int lag = centre - span; lag <= centre + span; ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i + lag < count; ++i)
                sum += static_cast<double>(x[i]) * x[i + lag];
            r.push_back(sum);
        }
        const auto best = static_cast<int>(std::max_element(r.begin() + 1, r.end() - 1) - r.begin());
        const double a = r[static_cast<std::size_t>(best - 1)], b = r[static_cast<std::size_t>(best)], c = r[static_cast<std::size_t>(best + 1)];
        return centre - span + best + 0.5 * (a - c) / (a - 2.0 * b + c);
    }
}

class V3SampleTests final : public juce::UnitTest
{
public:
    V3SampleTests() : juce::UnitTest("V3 samples and grains", "V3") {}

    void runTest() override
    {
        const auto& bank = SampleBank::instance();

        beginTest("every_factory_sample_is_levelled_bounded_and_loops_without_a_seam");
        {
            const char* names[] { "Glass", "Choir", "Strings", "Air", "Harp" };
            for (int s = 0; s < SampleBank::sampleCount; ++s)
            {
                for (int level = 0; level < SampleBank::levels; ++level)
                {
                    const float* d = bank.data(s, level);
                    float peak = 0.0f, step = 0.0f;
                    bool finite = true;
                    for (int i = 0; i < kLength; ++i)
                    {
                        finite = finite && std::isfinite(d[i]);
                        peak = std::max(peak, std::abs(d[i]));
                        if (i > 0)
                            step = std::max(step, std::abs(d[i] - d[i - 1]));
                    }
                    expect(finite);
                    expectLessOrEqual(peak, 1.0f);
                    // The guard frames are the loop's own neighbours, and the seam is no bigger a step than the rest.
                    expectEquals(d[-1], d[kLength - 1]);
                    expectEquals(d[kLength], d[0]);
                    expectEquals(d[kLength + 2], d[2]);
                    expectLessOrEqual(std::abs(d[0] - d[kLength - 1]), step);
                }
                const double level = rms(bank.data(s, 0), static_cast<std::size_t>(kLength));
                logMessage(juce::String(names[s]) + ": rms " + juce::String(level, 3));
                expectGreaterThan(level, 0.08);
                expectLessThan(level, 0.23);
            }
        }

        beginTest("the_pitched_samples_are_tuned_to_c4");
        {
            const double glass = zeroCrossingHz(bank.data(static_cast<int>(SampleBank::Sample::glass), 0), kLength, SampleBank::sourceRate);
            expectWithinAbsoluteError(glass, kC4, 0.6);
            const double period = SampleBank::sourceRate / kC4;
            for (const auto which : { SampleBank::Sample::choir, SampleBank::Sample::strings })
            {
                const double measured = periodOf(bank.data(static_cast<int>(which), 0) + 48000, 48000, period);
                expectWithinAbsoluteError(1200.0 * std::log2(period / measured), 0.0, 10.0, "cents off C4");
            }
        }

        beginTest("each_band_level_is_darker_than_the_one_before_and_the_same_below");
        {
            const int air = static_cast<int>(SampleBank::Sample::air);
            const float* full = bank.data(air, 0);
            const float* ten = bank.data(air, 1);
            const float* five = bank.data(air, 2);
            const int n = 48000;
            expectLessThan(levelAt(ten, n, 15000.0, 48000.0), 0.02 * levelAt(full, n, 15000.0, 48000.0));
            expectLessThan(levelAt(five, n, 7500.0, 48000.0), 0.06 * levelAt(full, n, 7500.0, 48000.0));
            expectWithinAbsoluteError(levelAt(ten, n, 1000.0, 48000.0) / levelAt(full, n, 1000.0, 48000.0), 1.0, 0.02);
            expectWithinAbsoluteError(levelAt(five, n, 1000.0, 48000.0) / levelAt(full, n, 1000.0, 48000.0), 1.0, 0.02);
        }

        beginTest("c4_plays_a_sample_at_its_own_speed_and_an_octave_up_twice_as_fast");
        {
            const int glass = static_cast<int>(SampleBank::Sample::glass);
            auto rig = makeRig<SampleGenerator>();
            const auto own = mono(render(rig->generator, sampleParams(SampleBank::Sample::glass, kC4), 96000));
            // Every second output sample is a frame of the sample, two samples late.
            const float* d = bank.data(glass, 0);
            float worst = 0.0f;
            for (int i = 0; i < 2000; ++i)
                worst = std::max(worst, std::abs(own[static_cast<std::size_t>(2 + 2 * i)] - d[i]));
            expectLessThan(worst, 1.0e-5f);
            expectWithinAbsoluteError(zeroCrossingHz(own.data(), 96000, kRate), kC4, 1.0);

            auto high = makeRig<SampleGenerator>();
            const auto up = mono(render(high->generator, sampleParams(SampleBank::Sample::glass, 2.0 * kC4), 96000));
            expectWithinAbsoluteError(zeroCrossingHz(up.data(), 96000, kRate), 2.0 * kC4, 2.0);

            // The far ends of the range stay finite and bounded.
            for (double hz : { kC4 / 64.0, 16.0 * kC4 })
            {
                auto far = makeRig<SampleGenerator>();
                const auto out = mono(render(far->generator, sampleParams(SampleBank::Sample::harp, hz), 9600));
                for (float v : out)
                    expect(std::isfinite(v) && std::abs(v) < 2.0f);
            }
        }

        beginTest("start_is_where_a_note_begins_and_reverse_runs_from_the_end");
        {
            const float* d = bank.data(static_cast<int>(SampleBank::Sample::harp), 0);
            auto rig = makeRig<SampleGenerator>();
            auto p = sampleParams(SampleBank::Sample::harp, kC4);
            p.p[SampleGenerator::start] = 0.5f;
            const auto half = mono(render(rig->generator, p, 4096));
            auto back = makeRig<SampleGenerator>();
            p.p[SampleGenerator::start] = 0.0f;
            p.p[SampleGenerator::reverse] = 1.0f;
            const auto reversed = mono(render(back->generator, p, 4096));
            float worstHalf = 0.0f, worstReversed = 0.0f;
            for (int i = 0; i < 2000; ++i)
            {
                worstHalf = std::max(worstHalf, std::abs(half[static_cast<std::size_t>(2 + 2 * i)] - d[kLength / 2 + i]));
                worstReversed = std::max(worstReversed, std::abs(reversed[static_cast<std::size_t>(2 + 2 * i)] - d[(kLength - i) % kLength]));
            }
            // The note's frequency is a float: over 2000 frames the read drifts a ten-thousandth of a frame.
            expectLessThan(worstHalf, 1.0e-4f);
            expectLessThan(worstReversed, 1.0e-4f);
        }

        beginTest("a_one_shot_stops_at_the_end_and_a_loop_carries_on");
        {
            // Two octaves up the four seconds last one: 96000 output samples.
            auto once = makeRig<SampleGenerator>();
            const auto shot = mono(render(once->generator, sampleParams(SampleBank::Sample::strings, 4.0 * kC4, false), 144000));
            expectGreaterThan(rms(shot, 48000, 90000), 0.02);
            expect(std::all_of(shot.begin() + 96016, shot.end(), [](float v) { return v == 0.0f; }), "silent after the end");
            expectLessOrEqual(largestStep(shot, 95000, 96100), largestStep(shot, 48000, 90000), "the end fades instead of stopping");

            auto round = makeRig<SampleGenerator>();
            const auto looped = mono(render(round->generator, sampleParams(SampleBank::Sample::strings, 4.0 * kC4, true), 144000));
            expectGreaterThan(rms(looped, 100000, 144000), 0.02);

            // A retriggered voice starts the sample again.
            round->generator.strike();
            render(round->generator, sampleParams(SampleBank::Sample::strings, kC4, true), kSubBlock);
            expectWithinAbsoluteError(round->generator.positionForTesting(), 0.5 * kSubBlock, 1.0e-6);
        }

        beginTest("a_short_loop_joins_without_a_click_when_it_fades");
        {
            auto loopOf = [](float fade)
            {
                auto rig = makeRig<SampleGenerator>();
                auto p = sampleParams(SampleBank::Sample::glass, kC4);
                p.p[SampleGenerator::loopStart] = 0.25f;
                p.p[SampleGenerator::loopEnd] = 0.30f; // 9600 frames: the loop comes round every 19200 samples
                p.p[SampleGenerator::fade] = fade;
                return mono(render(rig->generator, p, 240000));
            };
            // From 50 ms on: the note's own onset is not what is being judged.
            auto plain = makeRig<SampleGenerator>();
            const float ordinary = largestStep(mono(render(plain->generator, sampleParams(SampleBank::Sample::glass, kC4), 240000)), 4800, 240000);
            const float faded = largestStep(loopOf(1.0f), 4800, 240000);
            const float cut = largestStep(loopOf(0.0f), 4800, 240000);
            logMessage("largest step: sample " + juce::String(ordinary, 4) + ", faded loop " + juce::String(faded, 4) + ", cut loop " + juce::String(cut, 4));
            expectLessThan(faded, 1.6f * ordinary);
            expectGreaterThan(cut, 3.0f * ordinary, "without a fade this loop clicks, so the fade is what removes it");
        }

        beginTest("a_cloud_is_about_as_loud_thin_or_thick_and_stays_bounded");
        {
            auto cloud = [](float sizeMs, float density)
            {
                auto rig = makeRig<GrainGenerator>();
                const auto out = render(rig->generator, grainParams(SampleBank::Sample::strings, kC4, sizeMs, density), 288000);
                float peak = 0.0f;
                for (std::size_t i = 0; i < out.left.size(); ++i)
                {
                    peak = std::max({ peak, std::abs(out.left[i]), std::abs(out.right[i]) });
                }
                return std::pair<double, float> { rms(out.left, 96000, 288000), peak };
            };
            const auto thin = cloud(120.0f, 8.0f), medium = cloud(200.0f, 25.0f), thick = cloud(400.0f, 60.0f);
            logMessage("cloud rms: thin " + juce::String(thin.first, 3) + ", medium " + juce::String(medium.first, 3) + ", thick " + juce::String(thick.first, 3));
            expectGreaterThan(thin.first, 0.02);
            expectWithinAbsoluteError(20.0 * std::log10(thick.first / thin.first), 0.0, 6.0);
            expectWithinAbsoluteError(20.0 * std::log10(medium.first / thin.first), 0.0, 6.0);
            for (float peak : { thin.second, medium.second, thick.second })
                expectLessThan(peak, 1.5f);

            // Never more than twelve grains, however many are asked for.
            auto rig = makeRig<GrainGenerator>();
            int most = 0;
            for (int block = 0; block < 3000; ++block)
            {
                render(rig->generator, grainParams(SampleBank::Sample::air, kC4, 500.0f, 80.0f), kSubBlock);
                most = std::max(most, rig->generator.activeGrainsForTesting());
            }
            expectEquals(most, GrainGenerator::maxGrains);
        }

        beginTest("spread_places_the_grains_across_the_stereo_field");
        {
            auto width = [](float spread)
            {
                auto rig = makeRig<GrainGenerator>();
                auto p = grainParams(SampleBank::Sample::choir, kC4, 150.0f, 30.0f);
                p.p[GrainGenerator::spread] = spread;
                const auto out = render(rig->generator, p, 192000);
                double side = 0.0, mid = 0.0;
                for (std::size_t i = 0; i < out.left.size(); ++i)
                {
                    side += std::pow(static_cast<double>(out.left[i]) - out.right[i], 2.0);
                    mid += std::pow(static_cast<double>(out.left[i]) + out.right[i], 2.0);
                }
                return std::sqrt(side / std::max(1.0e-12, mid));
            };
            expectLessThan(width(0.0f), 1.0e-4);
            expectGreaterThan(width(1.0f), 0.3);
        }

        beginTest("grains_follow_the_note_and_pitch_spray_scatters_them");
        {
            auto cloud = [](double hz, float pitchSpray)
            {
                auto rig = makeRig<GrainGenerator>();
                auto p = grainParams(SampleBank::Sample::glass, hz, 200.0f, 30.0f);
                p.p[GrainGenerator::pitchSpray] = pitchSpray;
                return mono(render(rig->generator, p, 192000));
            };
            const auto low = cloud(kC4, 0.0f), high = cloud(2.0 * kC4, 0.0f), scattered = cloud(kC4, 1.0f);
            const int n = 96000;
            auto at = [&](const std::vector<float>& x, double f) { return levelAt(x.data() + 48000, n, f, kRate); };
            expectGreaterThan(at(low, kC4), 4.0 * at(low, 2.0 * kC4));
            expectGreaterThan(at(high, 2.0 * kC4), 4.0 * at(high, kC4));
            // An octave either way: little is left at the note itself.
            expectLessThan(at(scattered, kC4) / rms(scattered, 48000, 144000), 0.5 * at(low, kC4) / rms(low, 48000, 144000));
        }

        beginTest("scan_moves_the_spot_through_the_sample_and_zero_freezes_it");
        {
            auto after = [](float scan)
            {
                auto rig = makeRig<GrainGenerator>();
                auto p = grainParams(SampleBank::Sample::harp, kC4, 100.0f, 20.0f);
                p.p[GrainGenerator::scan] = scan;
                render(rig->generator, p, 96000); // one second
                return rig->generator.scanFramesForTesting();
            };
            expectEquals(after(0.0f), 0.0);
            expectWithinAbsoluteError(after(1.0f), 48000.0, 1.0);
            expectWithinAbsoluteError(after(-0.5f), kLength - 24000.0, 1.0);
        }

        beginTest("a_grain_patch_plays_in_a_part_in_stereo");
        {
            auto part = std::make_unique<Part>();
            part->prepare(48000.0, 512, 11);
            PartSettings settings;
            auto& g = settings.voice.generators[0];
            g.type = generatorType::grain;
            const auto& d = GrainGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            g.level = 1.0f;
            Envelope::Parameters held;
            held.attack = 0.001f;
            held.sustain = 1.0f;
            held.release = 0.1f;
            settings.voice.envelopes = { held, held, held };

            coolsynth::synth::EngineMidiEvent key;
            key.type = Type::noteOn;
            key.noteNumber = 60;
            key.value = 1.0f;
            const std::vector<coolsynth::synth::EngineMidiEvent> events { key };
            std::vector<float> left, right, l(256), r(256);
            for (int block = 0; block < 190; ++block) // about a second
            {
                part->render(l.data(), r.data(), 256, block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(events)
                                                                 : std::span<const coolsynth::synth::EngineMidiEvent>(), settings, {});
                left.insert(left.end(), l.begin(), l.end());
                right.insert(right.end(), r.begin(), r.end());
            }
            expectGreaterThan(rms(left, 9600, left.size()), 0.005);
            expectGreaterThan(rms(right, 9600, right.size()), 0.005);
            double difference = 0.0;
            for (std::size_t i = 9600; i < left.size(); ++i)
                difference += std::abs(left[i] - right[i]);
            expectGreaterThan(difference / static_cast<double>(left.size()), 1.0e-4);
        }
    }
};

static V3SampleTests v3SampleTests;
