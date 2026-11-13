#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/Part.h"
#include "synth/v3/StringGenerator.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 96000.0;
    constexpr int kSubBlock = 32;

    struct String
    {
        SlotMemory memory;
        StringGenerator generator;
    };

    std::unique_ptr<String> makeString()
    {
        auto s = std::make_unique<String>();
        s->memory.allocate(StringGenerator::memoryFloats(kRate, kSubBlock));
        s->generator.prepare(kRate, kSubBlock, s->memory);
        s->generator.noteOn(NoteContext {});
        return s;
    }

    SlotParams stringParams(double hz, float decaySeconds = 6.0f, float damping = 0.3f)
    {
        SlotParams p;
        p.p[StringGenerator::decay] = decaySeconds;
        p.p[StringGenerator::damping] = damping;
        p.p[StringGenerator::pick] = 0.7f;
        p.p[StringGenerator::position] = 0.2f;
        p.p[StringGenerator::release] = 0.6f;
        p.p[StringGenerator::excitation] = 0.0f;
        p.frequencyHz = static_cast<float>(hz);
        return p;
    }

    void render(StringGenerator& g, const SlotParams& p, int samples, std::vector<float>& out)
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

    /** The period in samples from the autocorrelation peak near ten periods. */
    double measuredPeriod(const std::vector<float>& x, double expectedPeriod)
    {
        // Early and short: a heavily damped high string is gone within a tenth of a second.
        const std::size_t from = 960, window = 9600;
        const int centre = static_cast<int>(std::lround(10.0 * expectedPeriod));
        std::vector<double> r;
        const int span = 5;
        for (int lag = centre - span; lag <= centre + span; ++lag)
        {
            double sum = 0.0;
            for (std::size_t i = from; i < from + window; ++i)
                sum += static_cast<double>(x[i]) * x[i + static_cast<std::size_t>(lag)];
            r.push_back(sum);
        }
        const auto best = static_cast<int>(std::max_element(r.begin() + 1, r.end() - 1) - r.begin());
        const double a = r[static_cast<std::size_t>(best - 1)], b = r[static_cast<std::size_t>(best)], c = r[static_cast<std::size_t>(best + 1)];
        const double offset = 0.5 * (a - c) / (a - 2.0 * b + c);
        return (centre - span + best + offset) / 10.0;
    }

    double harmonicDb(const std::vector<float>& x, double f, int k)
    {
        const std::size_t from = 4800;
        const int n = static_cast<int>(std::lround(std::floor(40000.0 * f / kRate) * kRate / f));
        double re1 = 0.0, im1 = 0.0, re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = x[from + static_cast<std::size_t>(i)];
            const double t = 2.0 * juce::MathConstants<double>::pi * f * i / kRate;
            re1 += v * std::cos(t);
            im1 += v * std::sin(t);
            re += v * std::cos(k * t);
            im += v * std::sin(k * t);
        }
        return 10.0 * std::log10((re * re + im * im) / (re1 * re1 + im1 * im1));
    }
}

class V3StringTests final : public juce::UnitTest
{
public:
    V3StringTests() : juce::UnitTest("V3 plucked string", "V3") {}

    void runTest() override
    {
        beginTest("the_string_is_in_tune_across_five_octaves_whatever_the_damping");
        {
            double worst = 0.0;
            for (float damping : { 0.1f, 0.8f })
            {
                for (int note = 36; note <= 96; note += 12)
                {
                    const double hz = 440.0 * std::exp2((note - 69) / 12.0);
                    auto s = makeString();
                    std::vector<float> out;
                    render(s->generator, stringParams(hz, 10.0f, damping), 96000, out);
                    const double cents = 1200.0 * std::log2((kRate / hz) / measuredPeriod(out, kRate / hz));
                    worst = std::max(worst, std::abs(cents));
                    expectLessThan(std::abs(cents), 3.0, "note " + juce::String(note) + " damping " + juce::String(damping) + ": " + juce::String(cents, 2) + " cents");
                }
            }
            logMessage("worst tuning error " + juce::String(worst, 2) + " cents");
        }

        beginTest("decay_is_the_time_to_fall_sixty_db");
        {
            auto s = makeString();
            std::vector<float> out;
            render(s->generator, stringParams(220.0, 2.0f, 0.0f), 96000 * 2, out);
            const double early = rms(out, 24000, 33600);   // 0.25 to 0.35 s
            const double late = rms(out, 120000, 129600);  // one second later
            expectWithinAbsoluteError(20.0 * std::log10(early / late), 30.0, 3.0, "30 dB a second at Decay 2 s");
        }

        beginTest("the_key_going_up_damps_the_string");
        {
            auto s = makeString();
            auto p = stringParams(220.0, 8.0f, 0.2f);
            p.p[StringGenerator::release] = 1.0f;
            std::vector<float> out;
            render(s->generator, p, 28800, out);
            const double ringing = rms(out, 19200, 28800);
            s->generator.noteOff();
            render(s->generator, p, 19200, out);
            const double damped = rms(out, 38400, 48000); // 100 to 200 ms after the key went up
            expectLessThan(damped, 0.05 * ringing);

            // Release at 0 lets it ring on.
            auto free = makeString();
            p.p[StringGenerator::release] = 0.0f;
            std::vector<float> ringOn;
            render(free->generator, p, 28800, ringOn);
            free->generator.noteOff();
            render(free->generator, p, 19200, ringOn);
            expectGreaterThan(rms(ringOn, 38400, 48000), 0.7 * rms(ringOn, 19200, 28800));
        }

        beginTest("plucking_in_the_middle_removes_the_even_harmonics");
        {
            auto middle = makeString();
            auto p = stringParams(220.0, 8.0f, 0.1f);
            p.p[StringGenerator::position] = 0.5f;
            std::vector<float> a;
            render(middle->generator, p, 96000, a);
            auto fifth = makeString();
            p.p[StringGenerator::position] = 0.2f;
            std::vector<float> b;
            render(fifth->generator, p, 96000, b);
            expectLessThan(harmonicDb(a, 220.0, 2), -25.0, "position 0.5");
            expectGreaterThan(harmonicDb(b, 220.0, 2), -15.0, "position 0.2");
        }

        beginTest("a_retriggered_voice_plucks_the_ringing_string_again");
        {
            auto part = std::make_unique<Part>();
            part->prepare(48000.0, 512, 9);
            PartSettings settings;
            auto& g = settings.voice.generators[0];
            g.type = generatorType::string;
            const auto& d = StringGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            g.p[StringGenerator::decay] = unitsToNormalised(d.params[StringGenerator::decay], 0.3f); // dies quickly
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
            run(150, { event(Type::noteOn, 57) });               // 0.8 s: the short string has died away
            const double faded = rms(out, out.size() - 4800, out.size());
            run(20, { event(Type::noteOn, 57) });                // the same key again: the same voice, struck
            const double struck = rms(out, out.size() - 4800, out.size());
            expectGreaterThan(struck, 20.0 * faded + 1.0e-4, "the second note sounds");
        }
    }
};

static V3StringTests v3StringTests;
