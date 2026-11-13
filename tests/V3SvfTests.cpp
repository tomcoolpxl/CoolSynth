#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/SvfProcessor.h"

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
    }
};

static V3SvfTests v3SvfTests;
