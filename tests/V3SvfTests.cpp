#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/StateMigration.h"
#include "synth/v3/SvfProcessor.h"
#include "synth/v3/Voice.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Mode = SvfProcessor::Mode;

    constexpr double kRate = 96000.0;
    constexpr int kSubBlock = 32;

    struct Svf
    {
        SlotMemory memory;
        SvfProcessor processor;
    };

    std::unique_ptr<Svf> makeSvf()
    {
        auto s = std::make_unique<Svf>();
        s->processor.prepare(kRate, kSubBlock, s->memory);
        return s;
    }

    SlotParams svfParams(Mode mode, float cutoffHz, float resonance, float drive = 0.0f)
    {
        SlotParams p;
        p.p[SvfProcessor::cutoff] = cutoffHz;
        p.p[SvfProcessor::resonance] = resonance;
        p.p[SvfProcessor::drive] = drive;
        p.p[SvfProcessor::keyTrack] = 0.0f;
        p.p[SvfProcessor::mode] = static_cast<float>(mode);
        p.frequencyHz = SvfProcessor::keyReferenceHz;
        return p;
    }

    /** Steady-state gain in dB of a quiet sine through the filter. */
    double gainDb(Mode mode, float cutoffHz, float resonance, double hz)
    {
        auto svf = makeSvf();
        const auto p = svfParams(mode, cutoffHz, resonance);
        const int total = static_cast<int>(kRate);
        std::array<float, kSubBlock> l {}, r {};
        double in = 0.0, out = 0.0;
        for (int s = 0; s < total; s += kSubBlock)
        {
            for (int i = 0; i < kSubBlock; ++i)
                l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] =
                    0.01f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * (s + i) / kRate));
            std::array<float, kSubBlock> dry = l;
            svf->processor.process(l.data(), r.data(), kSubBlock, p);
            if (s > total / 2)
                for (int i = 0; i < kSubBlock; ++i)
                {
                    in += static_cast<double>(dry[static_cast<std::size_t>(i)]) * dry[static_cast<std::size_t>(i)];
                    out += static_cast<double>(l[static_cast<std::size_t>(i)]) * l[static_cast<std::size_t>(i)];
                }
        }
        return 10.0 * std::log10(out / in);
    }
}

class V3SvfTests final : public juce::UnitTest
{
public:
    V3SvfTests() : juce::UnitTest("V3 state-variable filter", "V3") {}

    void runTest() override
    {
        beginTest("the_four_modes_have_their_shapes");
        {
            // Resonance 0 is Q 0.5: two coincident poles, -6 dB at the cutoff, 12 dB per octave beyond.
            expectWithinAbsoluteError(gainDb(Mode::lowpass, 1000.0f, 0.0f, 100.0), 0.0, 0.2);
            expectWithinAbsoluteError(gainDb(Mode::lowpass, 1000.0f, 0.0f, 1000.0), -6.02, 0.2);
            expectWithinAbsoluteError(gainDb(Mode::lowpass, 1000.0f, 0.0f, 10000.0), -40.1, 1.0);
            expectWithinAbsoluteError(gainDb(Mode::highpass, 1000.0f, 0.0f, 10000.0), 0.0, 0.3);
            expectWithinAbsoluteError(gainDb(Mode::highpass, 1000.0f, 0.0f, 100.0), -40.1, 1.0);
            expectWithinAbsoluteError(gainDb(Mode::bandpass, 1000.0f, 0.5f, 1000.0), 0.0, 0.2);
            expectLessThan(gainDb(Mode::bandpass, 1000.0f, 0.5f, 100.0), -15.0);
            expectLessThan(gainDb(Mode::notch, 1000.0f, 0.3f, 1000.0), -40.0);
            expectWithinAbsoluteError(gainDb(Mode::notch, 1000.0f, 0.3f, 100.0), 0.0, 0.3);
        }

        beginTest("resonance_raises_a_peak_of_q_at_the_cutoff");
        {
            const double q = 1.0 / SvfProcessor::dampingFor(0.6f);
            expectWithinAbsoluteError(gainDb(Mode::lowpass, 2000.0f, 0.6f, 2000.0), 20.0 * std::log10(q), 0.3);
            expectWithinAbsoluteError(1.0 / SvfProcessor::dampingFor(1.0f), static_cast<double>(SvfProcessor::maxQ), 1.0e-3);
        }

        beginTest("full_resonance_and_drive_stay_stable_and_bounded");
        {
            for (auto mode : { Mode::lowpass, Mode::highpass, Mode::bandpass, Mode::notch })
            {
                auto svf = makeSvf();
                auto p = svfParams(mode, 1000.0f, 1.0f, 24.0f);
                juce::Random random(7);
                std::array<float, kSubBlock> l {}, r {};
                float largest = 0.0f;
                bool finite = true;
                for (int s = 0; s < static_cast<int>(kRate); s += kSubBlock)
                {
                    p.p[SvfProcessor::cutoff] = 50.0f + 15000.0f * random.nextFloat(); // jumps every sub-block
                    for (auto& v : l)
                        v = random.nextFloat() * 2.0f - 1.0f;
                    r = l;
                    svf->processor.process(l.data(), r.data(), kSubBlock, p);
                    for (float v : l)
                    {
                        finite = finite && std::isfinite(v);
                        largest = std::max(largest, std::abs(v));
                    }
                }
                expect(finite);
                expectLessThan(largest, 12.0f, "mode " + juce::String(static_cast<int>(mode)));
            }

            // An impulse at full resonance rings, then dies away.
            auto svf = makeSvf();
            const auto p = svfParams(Mode::lowpass, 1000.0f, 1.0f);
            std::array<float, kSubBlock> l {}, r {};
            float late = 0.0f;
            for (int s = 0; s < static_cast<int>(kRate); s += kSubBlock)
            {
                l.fill(0.0f);
                if (s == 0)
                    l[0] = 1.0f;
                r = l;
                svf->processor.process(l.data(), r.data(), kSubBlock, p);
                if (s > static_cast<int>(kRate / 2))
                    for (float v : l)
                        late = std::max(late, std::abs(v));
            }
            expectLessThan(late, 1.0e-4f, "the ring decays");
        }

        beginTest("key_tracking_moves_the_cutoff_with_the_note");
        {
            auto svf = makeSvf();
            auto p = svfParams(Mode::bandpass, 500.0f, 0.8f);
            p.p[SvfProcessor::keyTrack] = 1.0f;
            p.frequencyHz = 2.0f * SvfProcessor::keyReferenceHz; // an octave up: the band moves to 1 kHz
            std::array<float, kSubBlock> l {}, r {};
            double at1k = 0.0, at500 = 0.0;
            for (double hz : { 1000.0, 500.0 })
            {
                auto fresh = makeSvf();
                double out = 0.0;
                for (int s = 0; s < static_cast<int>(kRate); s += kSubBlock)
                {
                    for (int i = 0; i < kSubBlock; ++i)
                        l[static_cast<std::size_t>(i)] = 0.01f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * (s + i) / kRate));
                    r = l;
                    fresh->processor.process(l.data(), r.data(), kSubBlock, p);
                    if (s > static_cast<int>(kRate / 2))
                        for (float v : l)
                            out += static_cast<double>(v) * v;
                }
                (hz == 1000.0 ? at1k : at500) = out;
            }
            expectGreaterThan(10.0 * std::log10(at1k / at500), 10.0, "the peak sits at 1 kHz, not 500 Hz");
        }

        beginTest("clean_lp_hp_bp_and_notch_play_as_before_to_the_bit");
        {
            // The filter as it was before D233, line for line: every saved sound and preset relies on it.
            struct Old
            {
                float g = 0.0f, k = 1.0f;
                bool primed = false;
                float ic1[2] {}, ic2[2] {};
                void run(float* l, float* r, int n, const SlotParams& p, float rate)
                {
                    const float key = juce::jlimit(0.0f, 1.0f, p.p[SvfProcessor::keyTrack]);
                    float hz = p.p[SvfProcessor::cutoff] * std::pow(std::max(1.0f, p.frequencyHz) / SvfProcessor::keyReferenceHz, key);
                    hz = juce::jlimit(10.0f, std::min(24000.0f, 0.45f * rate), hz);
                    const float gT = std::tan(juce::MathConstants<float>::pi * hz / rate);
                    const float kT = SvfProcessor::dampingFor(p.p[SvfProcessor::resonance]);
                    const int tap = juce::jlimit(0, 3, static_cast<int>(std::lround(p.p[SvfProcessor::mode])));
                    if (! primed)
                    {
                        g = gT;
                        k = kT;
                        primed = true;
                    }
                    const float inv = 1.0f / static_cast<float>(std::max(1, n));
                    const float gS = (gT - g) * inv, kS = (kT - k) * inv;
                    for (int i = 0; i < n; ++i)
                    {
                        g += gS;
                        k += kS;
                        const float a1 = 1.0f / (1.0f + g * (g + k));
                        const float a2 = g * a1;
                        float* io[2] { l + i, r + i };
                        for (int c = 0; c < 2; ++c)
                        {
                            const float x = *io[c];
                            const float v3 = x - ic2[c];
                            float v1 = a1 * ic1[c] + a2 * v3;
                            v1 = 4.0f * coolsynth::synth::v3::dsp::fastTanh(0.25f * v1);
                            const float v2 = ic2[c] + g * v1;
                            ic1[c] = 2.0f * v1 - ic1[c];
                            ic2[c] = 2.0f * v2 - ic2[c];
                            *io[c] = tap == 0 ? v2 : tap == 1 ? x - k * v1 - v2 : tap == 2 ? k * v1 : x - k * v1;
                        }
                    }
                    g = gT;
                    k = kT;
                }
            };
            for (auto mode : { Mode::lowpass, Mode::highpass, Mode::bandpass, Mode::notch })
            {
                auto now = makeSvf();
                Old before;
                auto p = svfParams(mode, 800.0f, 0.7f);
                juce::Random random(11);
                bool same = true;
                for (int s = 0; s < 48000; s += kSubBlock)
                {
                    p.p[SvfProcessor::cutoff] = 100.0f + 8000.0f * random.nextFloat();
                    std::array<float, kSubBlock> l {}, r {};
                    for (auto& v : l)
                        v = random.nextFloat() * 2.0f - 1.0f;
                    r = l;
                    auto l2 = l, r2 = r;
                    now->processor.process(l.data(), r.data(), kSubBlock, p);
                    before.run(l2.data(), r2.data(), kSubBlock, p, static_cast<float>(kRate));
                    same = same && l == l2 && r == r2;
                }
                expect(same, "mode " + juce::String(static_cast<int>(mode)));
            }
        }

        beginTest("the_sem_morph_runs_from_low_pass_through_the_notch_to_high_pass");
        {
            auto morphGain = [](float m, double hz)
            {
                auto p = svfParams(Mode::morph, 1000.0f, 0.0f);
                p.p[SvfProcessor::morph] = m;
                return gainOf(p, hz);
            };
            expectWithinAbsoluteError(morphGain(0.0f, 100.0), gainDb(Mode::lowpass, 1000.0f, 0.0f, 100.0), 0.05, "0: the low-pass");
            expectWithinAbsoluteError(morphGain(0.0f, 10000.0), gainDb(Mode::lowpass, 1000.0f, 0.0f, 10000.0), 0.3);
            expectWithinAbsoluteError(morphGain(1.0f, 10000.0), gainDb(Mode::highpass, 1000.0f, 0.0f, 10000.0), 0.05, "1: the high-pass");
            expectLessThan(morphGain(0.5f, 1000.0), -40.0, "the middle notches at the cutoff");
            expectWithinAbsoluteError(morphGain(0.5f, 100.0), 0.0, 0.3, "and passes below at 0 dB");
            expectWithinAbsoluteError(morphGain(0.5f, 10000.0), 0.0, 0.3, "and above");
            expectLessThan(morphGain(0.25f, 1000.0 * std::sqrt(3.0)), -30.0, "at a quarter the notch sits at the cutoff times root 3");
        }

        beginTest("the_sid_sums_add_their_taps");
        {
            expectWithinAbsoluteError(gainDb(Mode::lowBand, 1000.0f, 0.0f, 100.0), 0.0, 0.5, "LP+BP passes the lows");
            expectWithinAbsoluteError(gainDb(Mode::bandHigh, 1000.0f, 0.0f, 10000.0), 0.0, 0.5, "BP+HP passes the highs");
            expectLessThan(gainDb(Mode::bandHigh, 1000.0f, 0.0f, 50.0), -15.0, "and not the lows");
        }

        beginTest("the_8580_keeps_its_range_and_steps_its_resonance");
        {
            auto p = svfParams(Mode::lowpass, 20000.0f, 0.0f);
            p.p[SvfProcessor::character] = static_cast<float>(SvfProcessor::Character::sid8580);
            p.p[SvfProcessor::curve] = 0.5f;
            expectLessThan(gainOf(p, 18000.0), -6.0, "no cutoff above 12.5 kHz");
            p.p[SvfProcessor::cutoff] = 2000.0f;
            p.p[SvfProcessor::resonance] = 1.0f; // step 15: Q = 2^(11/8)
            expectWithinAbsoluteError(gainOf(p, 2000.0), 20.0 * std::log10(std::exp2(11.0 / 8.0)), 0.5, "the top step's peak");
            p.p[SvfProcessor::resonance] = 0.52f; // still step 8
            const double step8 = gainOf(p, 2000.0);
            p.p[SvfProcessor::resonance] = 0.55f;
            expectWithinAbsoluteError(gainOf(p, 2000.0), step8, 0.01, "between steps nothing moves");
        }

        beginTest("the_6581_reads_its_curve_distorts_loud_and_stays_clean_quiet");
        {
            expectGreaterThan(SvfProcessor::sid6581Hz(SvfProcessor::sid8580Register(20.0)), 200.0, "its floor");
            expectWithinAbsoluteError(SvfProcessor::sid6581Hz(1023.0), 6000.0, 1.0);
            expectWithinAbsoluteError(SvfProcessor::sid6581Hz(1024.0), 4600.0, 1.0, "the drop at the middle");
            expectWithinAbsoluteError(SvfProcessor::sid6581Hz(2047.0), 18000.0, 1.0);

            auto p = svfParams(Mode::lowpass, 1000.0f, 0.3f);
            p.p[SvfProcessor::character] = static_cast<float>(SvfProcessor::Character::sid6581);
            p.p[SvfProcessor::curve] = 0.0f;
            const double dark = gainOf(p, 3000.0);
            p.p[SvfProcessor::curve] = 1.0f;
            expectGreaterThan(gainOf(p, 3000.0), dark + 3.0, "Curve 1 is brighter than 0");

            p.p[SvfProcessor::curve] = 0.5f;
            p.p[SvfProcessor::cutoff] = 4000.0f; // about 735 Hz on the 6581: the 200 Hz sine and its harmonic in the passband
            const double quiet = secondHarmonicDb(p, 0.01f), loud = secondHarmonicDb(p, 1.0f);
            logMessage("6581 second harmonic: " + juce::String(quiet, 1) + " dB quiet, " + juce::String(loud, 1) + " dB loud");
            expectLessThan(quiet, -50.0, "a quiet sine stays clean");
            // How much is for the ears (sid6581Bend); here only that it grows with the level, as the chip's does.
            expectGreaterThan(loud, quiet + 25.0, "a loud one gains a second harmonic");
        }

        beginTest("every_character_and_new_mode_stays_bounded_and_rings_down");
        {
            for (const auto chip : { SvfProcessor::Character::clean, SvfProcessor::Character::sid6581, SvfProcessor::Character::sid8580 })
                for (const auto mode : { Mode::lowpass, Mode::bandpass, Mode::morph, Mode::lowBand, Mode::bandHigh })
                {
                    auto svf = makeSvf();
                    auto p = svfParams(mode, 1000.0f, 1.0f, 24.0f);
                    p.p[SvfProcessor::character] = static_cast<float>(chip);
                    juce::Random random(5);
                    std::array<float, kSubBlock> l {}, r {};
                    float largest = 0.0f;
                    bool finite = true;
                    for (int s = 0; s < static_cast<int>(kRate / 2); s += kSubBlock)
                    {
                        p.p[SvfProcessor::cutoff] = 50.0f + 15000.0f * random.nextFloat();
                        p.p[SvfProcessor::morph] = random.nextFloat();
                        for (auto& v : l)
                            v = random.nextFloat() * 2.0f - 1.0f;
                        r = l;
                        svf->processor.process(l.data(), r.data(), kSubBlock, p);
                        for (float v : l)
                        {
                            finite = finite && std::isfinite(v);
                            largest = std::max(largest, std::abs(v));
                        }
                    }
                    expect(finite);
                    expectLessThan(largest, 12.0f, juce::String(static_cast<int>(chip)) + "/" + juce::String(static_cast<int>(mode)));
                }

            auto svf = makeSvf();
            auto p = svfParams(Mode::lowpass, 1000.0f, 1.0f);
            p.p[SvfProcessor::character] = static_cast<float>(SvfProcessor::Character::sid6581);
            std::array<float, kSubBlock> l {}, r {};
            float late = 0.0f;
            for (int s = 0; s < static_cast<int>(kRate); s += kSubBlock)
            {
                l.fill(0.0f);
                if (s == 0)
                    l[0] = 1.0f;
                r = l;
                svf->processor.process(l.data(), r.data(), kSubBlock, p);
                if (s > static_cast<int>(kRate / 2))
                    for (float v : l)
                        late = std::max(late, std::abs(v));
            }
            expectLessThan(late, 1.0e-4f, "the 6581's top step rings, then dies away");
        }

        beginTest("an_older_sound_keeps_its_svf_mode");
        {
            juce::XmlElement state("STATE");
            auto add = [&](const juce::String& id, double value)
            {
                auto* e = state.createNewChildElement("PARAM");
                e->setAttribute("id", id);
                e->setAttribute("value", value);
            };
            add("pA.p1.type", processorType::svf);
            add("pA.p1.p5", 2.0 / 3.0); // BP over four modes
            add("pA.p1.p7", 0.8);       // left over from another type
            add("pB.p2.type", processorType::ladder);
            add("pB.p2.p5", 2.0 / 3.0); // not an SVF: untouched
            coolsynth::presets::migrateSvfModes(state);
            auto valueOf = [&](const juce::String& id)
            {
                for (auto* e : state.getChildWithTagNameIterator("PARAM"))
                    if (e->getStringAttribute("id") == id)
                        return e->getDoubleAttribute("value");
                return -1.0;
            };
            expectWithinAbsoluteError(valueOf("pA.p1.p5") * (SvfProcessor::modeCount - 1), 2.0, 1.0e-9, "BP stays BP");
            expectEquals(valueOf("pA.p1.p7"), 0.0, "Clean");
            expectWithinAbsoluteError(valueOf("pB.p2.p5"), 2.0 / 3.0, 1.0e-12);
        }
    }

private:
    /** Steady-state gain in dB of a quiet sine through the filter, any settings. */
    static double gainOf(const SlotParams& p, double hz)
    {
        auto svf = makeSvf();
        const int total = static_cast<int>(kRate);
        std::array<float, kSubBlock> l {}, r {};
        double in = 0.0, out = 0.0;
        for (int s = 0; s < total; s += kSubBlock)
        {
            for (int i = 0; i < kSubBlock; ++i)
                l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] =
                    0.01f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * (s + i) / kRate));
            std::array<float, kSubBlock> dry = l;
            svf->processor.process(l.data(), r.data(), kSubBlock, p);
            if (s > total / 2)
                for (int i = 0; i < kSubBlock; ++i)
                {
                    in += static_cast<double>(dry[static_cast<std::size_t>(i)]) * dry[static_cast<std::size_t>(i)];
                    out += static_cast<double>(l[static_cast<std::size_t>(i)]) * l[static_cast<std::size_t>(i)];
                }
        }
        return 10.0 * std::log10(out / in);
    }

    /** The second harmonic of a 200 Hz sine through the filter, in dB under the first. */
    static double secondHarmonicDb(const SlotParams& p, float amplitude)
    {
        auto svf = makeSvf();
        const int total = static_cast<int>(kRate);
        std::array<float, kSubBlock> l {}, r {};
        double re1 = 0, im1 = 0, re2 = 0, im2 = 0;
        for (int s = 0; s < total; s += kSubBlock)
        {
            for (int i = 0; i < kSubBlock; ++i)
                l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] =
                    amplitude * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * (s + i) / kRate));
            svf->processor.process(l.data(), r.data(), kSubBlock, p);
            if (s >= total / 2)
                for (int i = 0; i < kSubBlock; ++i)
                {
                    const double t = 2.0 * juce::MathConstants<double>::pi * 200.0 * (s + i) / kRate;
                    const double v = l[static_cast<std::size_t>(i)];
                    re1 += v * std::cos(t);
                    im1 += v * std::sin(t);
                    re2 += v * std::cos(2.0 * t);
                    im2 += v * std::sin(2.0 * t);
                }
        }
        return 10.0 * std::log10((re2 * re2 + im2 * im2) / (re1 * re1 + im1 * im1));
    }
};

static V3SvfTests v3SvfTests;
