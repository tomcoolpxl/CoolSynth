#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/ModalGenerator.h"
#include "synth/v3/Part.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 96000.0;
    constexpr int kSubBlock = 32;
    constexpr float kWood = 0.0f, kMetal = 1.0f, kGlass = 2.0f, kTine = 3.0f;

    struct Modal
    {
        SlotMemory memory;
        ModalGenerator generator;
    };

    std::unique_ptr<Modal> makeModal()
    {
        auto m = std::make_unique<Modal>();
        m->memory.allocate(ModalGenerator::memoryFloats(kRate, kSubBlock));
        m->generator.prepare(kRate, kSubBlock, m->memory);
        m->generator.noteOn(NoteContext {});
        return m;
    }

    SlotParams modalParams(double hz, float material, float decaySeconds, float hardness, float position = 0.3f)
    {
        SlotParams p;
        p.p[ModalGenerator::material] = material;
        p.p[ModalGenerator::decay] = decaySeconds;
        p.p[ModalGenerator::hardness] = hardness;
        p.p[ModalGenerator::position] = position;
        p.p[ModalGenerator::damping] = 0.0f;
        p.p[ModalGenerator::release] = 0.0f;
        p.frequencyHz = static_cast<float>(hz);
        return p;
    }

    void render(ModalGenerator& g, const SlotParams& p, int samples, std::vector<float>& out)
    {
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
                out.push_back(g.link().signal[i]);
        }
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(to - from));
    }

    /** The level of the component at f over a Hann window, as a sine's amplitude. */
    double levelAt(const std::vector<float>& x, std::size_t from, int n, double f)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (i + 0.5) / n);
            const double v = w * x[from + static_cast<std::size_t>(i)];
            const double t = 2.0 * juce::MathConstants<double>::pi * f * i / kRate;
            re += v * std::cos(t);
            im += v * std::sin(t);
        }
        return 4.0 * std::sqrt(re * re + im * im) / n;
    }

    double db(double ratio) { return 20.0 * std::log10(std::max(1.0e-12, ratio)); }

    /** How far, in cents, the spectral peak near `expected` sits from it. */
    double centsOff(const std::vector<float>& x, double expected)
    {
        const std::size_t from = 960;
        const int n = 24000;
        std::vector<double> levels;
        const int span = 24; // half-cent steps, 12 cents either way
        for (int step = -span; step <= span; ++step)
            levels.push_back(levelAt(x, from, n, expected * std::exp2(0.5 * step / 1200.0)));
        const auto best = static_cast<int>(std::max_element(levels.begin(), levels.end()) - levels.begin());
        if (best == 0 || best == 2 * span)
            return 99.0;
        const double a = levels[static_cast<std::size_t>(best - 1)], b = levels[static_cast<std::size_t>(best)], c = levels[static_cast<std::size_t>(best + 1)];
        return 0.5 * ((best - span) + 0.5 * (a - c) / (a - 2.0 * b + c));
    }
}

class V3ModalTests final : public juce::UnitTest
{
public:
    V3ModalTests() : juce::UnitTest("V3 modal generator", "V3") {}

    void runTest() override
    {
        beginTest("each_material_rings_at_its_own_partial_ratios");
        {
            double worst = 0.0;
            for (float material : { kWood, kMetal, kGlass, kTine })
            {
                std::array<float, ModalGenerator::partials> ratio {};
                float exponent = 0.0f;
                ModalGenerator::materialAt(material, ratio, exponent);
                auto m = makeModal();
                std::vector<float> out;
                render(m->generator, modalParams(220.0, material, 6.0f, 1.0f, 0.13f), 28800, out);
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const double cents = centsOff(out, 220.0 * ratio[k]);
                    worst = std::max(worst, std::abs(cents));
                    expectLessThan(std::abs(cents), 2.0, "material " + juce::String(material) + " partial " + juce::String(static_cast<int>(k) + 1)
                                                             + ": " + juce::String(cents, 2) + " cents");
                }
            }
            logMessage("worst partial tuning error " + juce::String(worst, 2) + " cents");

            // The tables themselves: a tuned bar, a uniform bar, a glass, a clamped bar.
            std::array<float, ModalGenerator::partials> ratio {};
            float exponent = 0.0f;
            ModalGenerator::materialAt(kWood, ratio, exponent);
            expectWithinAbsoluteError(ratio[1], 4.0f, 0.001f);
            expectWithinAbsoluteError(ratio[2], 10.1f, 0.001f);
            ModalGenerator::materialAt(kTine, ratio, exponent);
            expectWithinAbsoluteError(ratio[1], 6.267f, 0.001f);
            expectWithinAbsoluteError(ratio[2], 17.548f, 0.001f);
            ModalGenerator::materialAt(kMetal, ratio, exponent);
            expectWithinAbsoluteError(ratio[1], 2.757f, 0.001f);
            expectWithinAbsoluteError(ratio[3], 8.933f, 0.001f);
            ModalGenerator::materialAt(kGlass, ratio, exponent);
            expectWithinAbsoluteError(ratio[1], 2.32f, 0.001f);
            // Half way between two materials every ratio lies between theirs.
            ModalGenerator::materialAt(0.5f, ratio, exponent);
            expect(ratio[1] > 2.757f && ratio[1] < 4.0f);
            expectWithinAbsoluteError(ratio[0], 1.0f, 1.0e-6f);
        }

        beginTest("decay_is_the_time_the_fundamental_takes_to_fall_sixty_db");
        {
            auto m = makeModal();
            std::vector<float> out;
            render(m->generator, modalParams(220.0, kWood, 2.0f, 0.0f), 96000 * 2, out);
            const double early = rms(out, 24000, 33600);   // 0.25 to 0.35 s
            const double late = rms(out, 120000, 129600);  // one second later
            expectWithinAbsoluteError(db(early / late), 30.0, 1.5, "30 dB a second at Decay 2 s");
        }

        beginTest("a_partial_rings_in_inverse_proportion_to_its_frequency");
        {
            // How far a component falls between 0.1-0.2 s and 0.3-0.4 s after the strike: 12 dB per 60 dB time fifth.
            auto drop = [](float material, double ratio, float damping)
            {
                auto m = makeModal();
                auto p = modalParams(220.0, material, 2.0f, 1.0f);
                p.p[ModalGenerator::damping] = damping;
                std::vector<float> out;
                render(m->generator, p, 40000, out);
                return db(levelAt(out, 9600, 9600, 220.0 * ratio) / levelAt(out, 28800, 9600, 220.0 * ratio));
            };
            // Decay 2 s: the fundamental falls 6 dB in 0.2 s.
            expectWithinAbsoluteError(drop(kWood, 1.0, 0.0f), 6.0, 0.5);
            // The wooden bar's second partial, four times as high, lasts a quarter as long: 24 dB.
            expectWithinAbsoluteError(drop(kWood, 4.0, 0.0f), 24.0, 1.5);
            // Metal's upper partials go a little faster than that: 2.757 to the power 1.2.
            expectWithinAbsoluteError(drop(kMetal, 2.757, 0.0f), 12.0 / (2.0 * std::pow(2.757, -1.2)), 1.5);
            // The tine's first overtone, 6.27 times as high: 37.6 dB.
            expectWithinAbsoluteError(drop(kTine, 6.267, 0.0f), 12.0 * 6.267 / 2.0, 2.5);
            // Damping takes the upper partials down faster and leaves the fundamental alone.
            expectWithinAbsoluteError(drop(kWood, 4.0, 0.25f), 12.0 / (2.0 * std::pow(4.0, -1.3)), 2.5);
            expectWithinAbsoluteError(drop(kWood, 1.0, 1.0f), 6.0, 0.5);
        }

        beginTest("a_hard_mallet_is_brighter_and_the_fundamental_as_loud");
        {
            auto strike = [](float hardness)
            {
                auto m = makeModal();
                std::vector<float> out;
                render(m->generator, modalParams(220.0, kMetal, 6.0f, hardness), 28800, out);
                return out;
            };
            const auto soft = strike(0.0f), hard = strike(1.0f);
            const double third = 220.0 * 5.404;
            expectWithinAbsoluteError(db(levelAt(hard, 4800, 19200, 220.0) / levelAt(soft, 4800, 19200, 220.0)), 0.0, 2.0, "fundamental");
            expectGreaterThan(db(levelAt(hard, 4800, 19200, third) / levelAt(soft, 4800, 19200, third)), 20.0, "third partial");
            expectEquals(ModalGenerator::pulseLength(400.0, 0.0f), 200);
            expectEquals(ModalGenerator::pulseLength(400.0, 1.0f), 3);
            expectEquals(ModalGenerator::pulseLength(40.0, 1.0f), 1);
        }

        beginTest("a_strike_in_the_middle_leaves_every_second_partial_silent");
        {
            auto second = [](float position)
            {
                auto m = makeModal();
                std::vector<float> out;
                render(m->generator, modalParams(220.0, kMetal, 6.0f, 1.0f, position), 28800, out);
                return db(levelAt(out, 4800, 19200, 220.0 * 2.757) / levelAt(out, 4800, 19200, 220.0));
            };
            expectLessThan(second(0.5f), -60.0, "position 0.5");
            expectGreaterThan(second(0.3f), -12.0, "position 0.3");
        }

        beginTest("the_key_going_up_damps_the_bar_only_when_asked");
        {
            auto m = makeModal();
            auto p = modalParams(220.0, kMetal, 8.0f, 0.5f);
            p.p[ModalGenerator::release] = 1.0f;
            std::vector<float> out;
            render(m->generator, p, 28800, out);
            const double ringing = rms(out, 19200, 28800);
            m->generator.noteOff();
            render(m->generator, p, 19200, out);
            expectLessThan(rms(out, 38400, 48000), 0.05 * ringing); // 100 to 200 ms after the key went up

            auto free = makeModal();
            p.p[ModalGenerator::release] = 0.0f;
            std::vector<float> ringOn;
            render(free->generator, p, 28800, ringOn);
            free->generator.noteOff();
            render(free->generator, p, 19200, ringOn);
            expectGreaterThan(rms(ringOn, 38400, 48000), 0.7 * rms(ringOn, 19200, 28800));
        }

        beginTest("the_strike_stays_in_bounds_and_a_bar_that_rang_out_is_silent_until_struck_again");
        {
            float highest = 0.0f, lowest = 10.0f;
            for (float material : { kWood, kMetal, kGlass, kTine })
            {
                for (int note = 36; note <= 108; note += 12)
                {
                    const double hz = 440.0 * std::exp2((note - 69) / 12.0);
                    auto m = makeModal();
                    std::vector<float> out;
                    render(m->generator, modalParams(hz, material, 4.0f, 1.0f, 0.23f), 9600, out);
                    float peak = 0.0f;
                    for (float v : out)
                    {
                        expect(std::isfinite(v));
                        peak = std::max(peak, std::abs(v));
                    }
                    highest = std::max(highest, peak);
                    lowest = std::min(lowest, peak);
                }
            }
            logMessage("hardest strike peaks between " + juce::String(lowest, 2) + " and " + juce::String(highest, 2));
            expectLessThan(highest, 2.5f);
            expectGreaterThan(lowest, 0.3f);

            auto m = makeModal();
            const auto p = modalParams(440.0, kMetal, 0.05f, 0.7f);
            std::vector<float> out;
            render(m->generator, p, 96000, out);
            expect(std::all_of(out.end() - 4800, out.end(), [](float v) { return v == 0.0f; }), "rung out to exact silence");
            m->generator.strike();
            render(m->generator, p, 960, out);
            expectGreaterThan(rms(out, 96000, 96960), 0.01, "struck again");
        }

        beginTest("partials_above_the_band_are_left_out");
        {
            // C8 on the tuned bar: the third partial would sit at 42.3 kHz. Nothing may ring above 20 kHz.
            auto m = makeModal();
            std::vector<float> out;
            render(m->generator, modalParams(4186.0, kWood, 4.0f, 1.0f), 9600, out);
            expectGreaterThan(levelAt(out, 960, 4800, 4186.0), 0.1);
            expectLessThan(db(levelAt(out, 960, 4800, 4186.0 * 10.1) / levelAt(out, 960, 4800, 4186.0)), -80.0);
        }

        beginTest("a_retriggered_voice_strikes_the_ringing_bar_again");
        {
            auto part = std::make_unique<Part>();
            part->prepare(48000.0, 512, 9);
            PartSettings settings;
            auto& g = settings.voice.generators[0];
            g.type = generatorType::modal;
            const auto& d = ModalGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            g.p[ModalGenerator::decay] = unitsToNormalised(d.params[ModalGenerator::decay], 0.2f); // dies quickly
            g.level = 1.0f;
            Envelope::Parameters held;
            held.attack = 0.001f;
            held.sustain = 1.0f;
            held.release = 0.5f;
            settings.voice.envelopes = { held, held, held };

            auto event = [](Type type, int note)
            {
                coolsynth::synth::EngineMidiEvent e;
                e.type = type;
                e.noteNumber = static_cast<uint8_t>(note);
                e.value = 1.0f;
                return e;
            };
            std::vector<float> out;
            auto run = [&](int blocks, std::vector<coolsynth::synth::EngineMidiEvent> events)
            {
                std::vector<float> l(256), r(256);
                for (int b = 0; b < blocks; ++b)
                {
                    const auto span = b == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(events)
                                             : std::span<const coolsynth::synth::EngineMidiEvent>();
                    part->render(l.data(), r.data(), 256, span, settings, {});
                    out.insert(out.end(), l.begin(), l.end());
                }
            };
            run(20, { event(Type::noteOn, 57) });
            expectGreaterThan(rms(out, 0, out.size()), 0.01, "the first note sounds");
            run(130, {});                                         // 0.7 s on: the short bar has died away
            const double faded = rms(out, out.size() - 4800, out.size());
            run(20, { event(Type::noteOn, 57) });                // the same key again: the same voice, struck
            const double struck = rms(out, out.size() - 4800, out.size());
            expectGreaterThan(struck, 20.0 * faded + 1.0e-4, "the second note sounds");
        }
    }
};

static V3ModalTests v3ModalTests;
