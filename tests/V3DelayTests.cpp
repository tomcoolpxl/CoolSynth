#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/GlobalDelay.h"

namespace
{
    using coolsynth::synth::DelayParametersV2;
    using coolsynth::synth::GlobalDelay;

    constexpr double kRate = 48000.0;

    struct Taps
    {
        std::vector<int> left, right; // sample positions of echoes, loudest first per window
        std::vector<float> leftLevels, rightLevels;
    };

    /** Feeds one impulse on both channels and returns where the echoes land on each side. */
    Taps impulseTaps(const DelayParametersV2& parameters, double seconds)
    {
        auto delay = std::make_unique<GlobalDelay>();
        delay->prepare(kRate, 512, 2);
        const int total = static_cast<int>(seconds * kRate);
        std::vector<float> left(static_cast<std::size_t>(total)), right(static_cast<std::size_t>(total));
        juce::AudioBuffer<float> block(2, 512);
        for (int start = 0; start < total; start += 512)
        {
            const int n = std::min(512, total - start);
            block.setSize(2, n, false, false, true);
            block.clear();
            if (start == 0)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, 1.0f);
            }
            delay->process(block, parameters);
            for (int i = 0; i < n; ++i)
            {
                left[static_cast<std::size_t>(start + i)] = block.getSample(0, i);
                right[static_cast<std::size_t>(start + i)] = block.getSample(1, i);
            }
        }

        // Echoes: local peaks above a floor, at least 10 ms apart.
        Taps taps;
        auto find = [](const std::vector<float>& x, std::vector<int>& at, std::vector<float>& level)
        {
            const int gap = static_cast<int>(0.01 * kRate);
            for (int i = 1; i + 1 < static_cast<int>(x.size()); ++i)
            {
                const float v = std::abs(x[static_cast<std::size_t>(i)]);
                if (v < 0.005f || v < std::abs(x[static_cast<std::size_t>(i - 1)]) || v < std::abs(x[static_cast<std::size_t>(i + 1)]))
                    continue;
                if (! at.empty() && i - at.back() < gap)
                {
                    if (v > level.back())
                    {
                        at.back() = i;
                        level.back() = v;
                    }
                    continue;
                }
                at.push_back(i);
                level.push_back(v);
            }
        };
        find(left, taps.left, taps.leftLevels);
        find(right, taps.right, taps.rightLevels);
        return taps;
    }
}

class V3DelayTests final : public juce::UnitTest
{
public:
    V3DelayTests() : juce::UnitTest("V3 delay sync and ping-pong", "V3") {}

    void runTest() override
    {
        beginTest("division_lengths_follow_the_tempo");
        {
            using coolsynth::synth::delayDivisionMs;
            expectWithinAbsoluteError(delayDivisionMs(5, 120.0), 250.0f, 1.0e-3f);  // 1/8
            expectWithinAbsoluteError(delayDivisionMs(6, 120.0), 375.0f, 1.0e-3f);  // 1/8.
            expectWithinAbsoluteError(delayDivisionMs(4, 120.0), 166.667f, 1.0e-2f); // 1/8T
            expectWithinAbsoluteError(delayDivisionMs(13, 60.0), 4000.0f, 1.0e-2f);  // a bar at 60 BPM
            expectWithinAbsoluteError(delayDivisionMs(6, 90.0), 500.0f, 1.0e-3f);
        }

        beginTest("taps_are_spaced_by_the_delay_time_at_an_eighth_and_a_dotted_eighth");
        {
            for (int division : { 5, 6 })
            {
                DelayParametersV2 p;
                p.enabled = true;
                p.timeMs = coolsynth::synth::delayDivisionMs(division, 120.0);
                p.feedback = 0.5f;
                p.mix = 0.5f;
                const auto taps = impulseTaps(p, 1.6);
                const double expected = p.timeMs * 0.001 * kRate;
                expect(taps.left.size() >= 4, "echoes found: " + juce::String(static_cast<int>(taps.left.size())));
                for (std::size_t k = 1; k < std::min<std::size_t>(taps.left.size(), 4); ++k)
                    expectWithinAbsoluteError(static_cast<double>(taps.left[k] - taps.left[k - 1]), expected, 3.0,
                                              "tap spacing at division " + juce::String(division));
                // Tape: both channels echo in place.
                expectEquals(static_cast<int>(taps.right.size()), static_cast<int>(taps.left.size()));
            }
        }

        beginTest("ping_pong_alternates_left_and_right_each_bounce_scaled_by_the_feedback");
        {
            DelayParametersV2 p;
            p.enabled = true;
            p.timeMs = 250.0f;
            p.feedback = 0.6f;
            p.mix = 1.0f;
            p.pingPong = true;
            const auto taps = impulseTaps(p, 1.2);
            const double t = 0.25 * kRate;
            // Mix 1: the dry impulse is gone; echoes at T (left), 2T (right), 3T (left), 4T (right).
            expect(taps.left.size() >= 2 && taps.right.size() >= 2);
            if (taps.left.size() >= 2 && taps.right.size() >= 2)
            {
                expectWithinAbsoluteError(static_cast<double>(taps.left[0]), t, 3.0);
                expectWithinAbsoluteError(static_cast<double>(taps.right[0]), 2.0 * t, 3.0);
                expectWithinAbsoluteError(static_cast<double>(taps.left[1]), 3.0 * t, 3.0);
                expectWithinAbsoluteError(static_cast<double>(taps.right[1]), 4.0 * t, 3.0);
                expectLessThan(taps.rightLevels[0], taps.leftLevels[0], "each bounce is quieter");
                expectLessThan(taps.leftLevels[1], taps.rightLevels[0]);
            }

            p.feedback = 0.0f;
            const auto single = impulseTaps(p, 1.2);
            expectEquals(static_cast<int>(single.left.size()), 1, "no feedback: one echo, on the left");
            expectEquals(static_cast<int>(single.right.size()), 0);
        }

        beginTest("bank_delay_slot_takes_sync_division_and_mode");
        {
            const auto result = coolsynth::presets::parseBankFile("[D]\nf1 = Delay sync=on division=1/8. mode=pingpong feedback=0.4 mix=0.2\n", "d.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "f1.type=Delay", "f1.p4=on", "f1.p5=1/8.", "f1.p6=pingpong", "f1.p2=0.4" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted);
        }
    }
};

static V3DelayTests v3DelayTests;
