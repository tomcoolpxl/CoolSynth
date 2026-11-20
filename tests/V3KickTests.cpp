#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/KickGenerator.h"
#include "synth/v3/Voice.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Kick = KickGenerator;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;

    struct Rig
    {
        SlotMemory memory;
        KickGenerator generator;
    };

    std::unique_ptr<Rig> makeKick(float velocity = 1.0f, std::uint32_t seed = 3)
    {
        auto r = std::make_unique<Rig>();
        r->memory.allocate(KickGenerator::memoryFloats(kVoiceRate, kSubBlock));
        r->memory.setSeed(seed);
        r->generator.prepare(kVoiceRate, kSubBlock, r->memory);
        NoteContext note;
        note.velocity = velocity;
        r->generator.noteOn(note);
        return r;
    }

    SlotParams kickParams(Kick::Model model, float hz)
    {
        SlotParams p;
        const auto& d = KickGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            p.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
        p.p[Kick::model] = static_cast<float>(model);
        p.frequencyHz = hz;
        return p;
    }

    std::vector<float> render(KickGenerator& g, const SlotParams& p, int samples)
    {
        std::vector<float> out(static_cast<std::size_t>(samples));
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
                out[static_cast<std::size_t>(s + i)] = g.link().signal[i];
        }
        return out;
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double frequencyIn(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        std::vector<double> crossings;
        for (std::size_t i = from + 1; i < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - x[i - 1] / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kVoiceRate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
    }

    double cents(double heard, double wanted) { return 1200.0 * std::log2(heard / wanted); }
}

class V3KickTests final : public juce::UnitTest
{
public:
    V3KickTests() : juce::UnitTest("V3 kick generator", "V3") {}

    void runTest() override
    {
        beginTest("kick_is_the_last_generator_type_with_twelve_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 12);
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::kick).name), juce::String("Kick"));
            expectEquals(KickGenerator::descriptor().usedParams, maxSlotParameters);
        }

        beginTest("the_808_starts_high_sighs_and_settles_on_the_note");
        {
            auto rig = makeKick();
            auto p = kickParams(Kick::Model::tr808, 55.0f);
            p.p[Kick::decay] = 2000.0f;
            p.p[Kick::click] = 0.0f;
            const auto out = render(rig->generator, p, static_cast<int>(1.2 * kVoiceRate));
            const double early = frequencyIn(out, 3000, 9000);        // 30 to 95 ms: in the sigh
            const double tail = frequencyIn(out, 57600, 105000);      // 0.6 to 1.1 s
            logMessage("808 at 55 Hz: " + juce::String(early, 1) + " Hz in the sigh, " + juce::String(tail, 2) + " Hz in the tail");
            expectWithinAbsoluteError(cents(tail, 55.0), 0.0, 10.0, "the tail on the note");
            expectGreaterThan(cents(early, 55.0), 50.0, "the sigh above it");
            expectLessThan(cents(early, 55.0), 300.0, "by less than its 2.4 semitones");
        }

        beginTest("decay_is_the_time_to_minus_60_dB");
        {
            for (const auto model : { Kick::Model::tr808, Kick::Model::tr909, Kick::Model::modern })
            {
                auto rig = makeKick();
                auto p = kickParams(model, 60.0f);
                p.p[Kick::decay] = 500.0f;
                p.p[Kick::click] = 0.0f;
                p.p[Kick::punch] = 0.0f;
                p.p[Kick::sigh] = 0.0f;
                const auto out = render(rig->generator, p, static_cast<int>(0.6 * kVoiceRate));
                const double start = rms(out, 4800, 9600), half = rms(out, 4800 + 24000, 9600 + 24000);
                const double dbPerHalf = 20.0 * std::log10(half / start); // 250 ms on: -30 dB
                expectWithinAbsoluteError(dbPerHalf, -30.0, 2.0, "model " + juce::String(static_cast<int>(model)));
            }
        }

        beginTest("one_shot_plays_the_tail_out_and_gate_leaves_it_to_the_voice");
        {
            auto rig = makeKick();
            auto p = kickParams(Kick::Model::tr808, 50.0f);
            p.p[Kick::decay] = 300.0f;
            render(rig->generator, p, 4800);
            expect(rig->generator.holdsRelease(), "one shot holds the voice while it rings");
            expect(! rig->generator.silentUntilNextNote());
            render(rig->generator, p, static_cast<int>(0.5 * kVoiceRate));
            expect(! rig->generator.holdsRelease(), "and lets it go when the tail is under -80 dB");
            expect(rig->generator.silentUntilNextNote());

            auto gate = makeKick();
            p.p[Kick::trigger] = 1.0f;
            render(gate->generator, p, 4800);
            expect(! gate->generator.holdsRelease(), "gate never holds");
        }

        beginTest("a_hit_on_a_ringing_drum_makes_no_step");
        {
            for (const auto model : { Kick::Model::tr808, Kick::Model::tr909, Kick::Model::modern })
            {
                auto rig = makeKick();
                auto p = kickParams(model, 50.0f);
                p.p[Kick::decay] = 2000.0f;
                p.p[Kick::click] = 0.0f;
                p.p[Kick::punch] = 0.0f;
                auto out = render(rig->generator, p, 9611); // somewhere mid-cycle
                rig->generator.strike(1.0f);
                const auto more = render(rig->generator, p, 960);
                out.insert(out.end(), more.begin(), more.end());
                float largest = 0.0f;
                for (std::size_t i = 9000; i < out.size(); ++i)
                    largest = std::max(largest, std::abs(out[i] - out[i - 1]));
                // A 50 Hz sine of peak 1.6 moves at most 0.0053 a sample; a step would be far more.
                expectLessThan(largest, 0.02f, "model " + juce::String(static_cast<int>(model)));
            }
        }

        beginTest("accent_makes_a_soft_hit_quieter_and_its_sigh_smaller");
        {
            auto loud = makeKick(1.0f), soft = makeKick(0.3f);
            auto p = kickParams(Kick::Model::tr808, 55.0f);
            p.p[Kick::accent] = 1.0f;
            p.p[Kick::click] = 0.0f;
            p.p[Kick::decay] = 2000.0f;
            const auto a = render(loud->generator, p, 12000), b = render(soft->generator, p, 12000);
            expectLessThan(rms(b, 1000, 12000), 0.5 * rms(a, 1000, 12000), "quieter");
            expectLessThan(frequencyIn(b, 3000, 9000), frequencyIn(a, 3000, 9000), "a smaller sigh");
        }

        beginTest("every_model_stays_bounded_driven_and_plays_the_same_twice");
        {
            for (const auto model : { Kick::Model::tr808, Kick::Model::tr909, Kick::Model::modern })
            {
                auto one = makeKick(1.0f, 9), two = makeKick(1.0f, 9);
                auto p = kickParams(model, 45.0f);
                p.p[Kick::drive] = 24.0f;
                p.p[Kick::noise] = 1.0f;
                p.p[Kick::click] = 1.0f;
                p.p[Kick::shape] = 1.0f;
                p.p[Kick::punch] = 48.0f;
                p.p[Kick::tone] = 1.0f;
                const auto a = render(one->generator, p, 48000), b = render(two->generator, p, 48000);
                float peak = 0.0f;
                bool finite = true;
                for (const float v : a)
                {
                    peak = std::max(peak, std::abs(v));
                    finite = finite && std::isfinite(v);
                }
                expect(finite);
                expectLessThan(peak, 1.5f, "model " + juce::String(static_cast<int>(model)));
                expect(a == b, "the same hit twice");
            }
        }
    }
};

static V3KickTests v3KickTests;
