#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "FmGenerator.h"
#include "Module.h"

/*
    V3 four-operator FM generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: four operators
    with fixed algorithms).

    Four sine operators. Operator 1 always sounds at the note; operators 2, 3 and 4 run at their
    Ratio times the note. Six algorithms (the arrow is "modulates"):

        1  4 > 3 > 2 > 1             one stack: the brightest, most metallic
        2  (3 + 4) > 2 > 1           two modulators into one
        3  (4 > 3) + 2 > 1           a stack and a single into one carrier
        4  4 > 3,  2 > 1             two pairs, carriers 1 and 3: the electric piano
        5  4 > 1, 2, 3               one modulator into three carriers
        6  1, 2, 3, 4                four sines, an organ

    Each operator's Level is its modulation index (Level 1 is 8 radians) where it modulates and
    its output level where it is a carrier; operator 1 is a carrier at level 1, and the carriers
    are summed and normalised so the output never exceeds one. Feedback goes to operator 4
    through the average of its last two outputs, as on the two-operator generator.

    Each modulation is limited with FmGenerator::indexLimit() for the operator it drives, so the
    sidebands that would fold into the audible band stay below 1e-4. Levels, ratios and feedback
    ramp across each sub-block. Phases in double precision restart at a fresh note. Output two
    samples late like every generator, with the signal and operator 1's wrap track for the next
    generator; combine FM drives operator 1, ring and AM work as on the VA.
*/
namespace coolsynth::synth::v3
{
    class Fm4Generator final
    {
    public:
        enum Param : int { algorithm = 0, ratio2, ratio3, ratio4, level2, level3, level4, feedback, paramCount };
        static constexpr int algorithmCount = 6;
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double levelRadians = 8.0;
        static constexpr double maxFeedbackRadians = 1.5;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "FM4", 2,
                {{ { "algo",   "Algorithm",  0.0f,  5.0f,  3.0f, Skew::linear,      Unit::choice,  false, "Stack|Two Into One|Stack Plus One|Two Pairs|One Into Three|Organ" },
                   { "ratio2", "Op 2 Ratio", 0.25f, 16.0f, 1.0f, Skew::logarithmic, Unit::generic, true },
                   { "ratio3", "Op 3 Ratio", 0.25f, 16.0f, 1.0f, Skew::logarithmic, Unit::generic, true },
                   { "ratio4", "Op 4 Ratio", 0.25f, 16.0f, 1.0f, Skew::logarithmic, Unit::generic, true },
                   { "level2", "Op 2 Level", 0.0f,  1.0f,  0.2f, Skew::squared,     Unit::percent, true },
                   { "level3", "Op 3 Level", 0.0f,  1.0f,  0.0f, Skew::squared,     Unit::percent, true },
                   { "level4", "Op 4 Level", 0.0f,  1.0f,  0.0f, Skew::squared,     Unit::percent, true },
                   { "fb",     "Feedback",   0.0f,  1.0f,  0.0f, Skew::linear,      Unit::percent, true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return 2 * static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            wrapTrack = memory.take(n);
            reset();
        }

        void reset() noexcept
        {
            phases.fill(0.0);
            feedback1 = feedback2 = 0.0;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            ringMono.fill(0.0f);
            ringWrap.fill(-1.0f);
            cursor = 0;
            primed = false;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            input = {};
        }

        void noteOn(const NoteContext&) noexcept {}
        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), wrapTrack.data() }; }

        /** Which operators each operator (0 = op 1) is modulated by, for an algorithm; and which ones sound. */
        struct Routing
        {
            std::array<std::array<bool, 4>, 4> modulatedBy {};
            std::array<bool, 4> carrier {};
        };

        [[nodiscard]] static Routing routing(int algo) noexcept
        {
            Routing r;
            auto link = [&r](int from, int to) { r.modulatedBy[static_cast<std::size_t>(to)][static_cast<std::size_t>(from)] = true; };
            switch (juce::jlimit(0, algorithmCount - 1, algo))
            {
                case 0: link(3, 2); link(2, 1); link(1, 0); r.carrier = { true, false, false, false }; break;
                case 1: link(2, 1); link(3, 1); link(1, 0); r.carrier = { true, false, false, false }; break;
                case 2: link(3, 2); link(2, 0); link(1, 0); r.carrier = { true, false, false, false }; break;
                case 3: link(1, 0); link(3, 2); r.carrier = { true, false, true, false }; break;
                case 4: link(3, 0); link(3, 1); link(3, 2); r.carrier = { true, true, true, false }; break;
                default: r.carrier = { true, true, true, true }; break;
            }
            return r;
        }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const int algo = juce::jlimit(0, algorithmCount - 1, static_cast<int>(std::lround(params.p[algorithm])));
            const Routing route = routing(algo);
            const double note = std::max(0.0, static_cast<double>(params.frequencyHz));

            std::array<double, 4> hz {};
            hz[0] = std::min(0.45 * sampleRate, note);
            for (int i = 1; i < 4; ++i)
                hz[static_cast<std::size_t>(i)] = std::min(0.45 * sampleRate,
                    note * juce::jlimit(0.25, 16.0, static_cast<double>(params.p[ratio2 + i - 1])));

            // Targets: an index where the operator modulates (limited for every operator it drives),
            // an output level where it sounds.
            std::array<double, 4> targetIndex {}, targetLevel {};
            targetLevel[0] = 1.0;
            for (int i = 1; i < 4; ++i)
            {
                const double setting = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[level2 + i - 1]));
                const auto ui = static_cast<std::size_t>(i);
                if (route.carrier[ui])
                    targetLevel[ui] = setting;
                double limit = FmGenerator::maxIndex;
                bool modulates = false;
                for (int to = 0; to < 4; ++to)
                    if (route.modulatedBy[static_cast<std::size_t>(to)][ui])
                    {
                        modulates = true;
                        limit = std::min(limit, FmGenerator::indexLimit(hz[static_cast<std::size_t>(to)], hz[ui], sampleRate));
                    }
                if (modulates)
                    targetIndex[ui] = std::min(levelRadians * setting, limit);
            }
            const double harmonicRoom = juce::jlimit(0.0, 1.0, (0.45 * sampleRate / std::max(1.0, hz[3]) - 1.0) / 20.0);
            const double targetFeedback = maxFeedbackRadians * juce::jlimit(0.0, 1.0, static_cast<double>(params.p[feedback])) * harmonicRoom;
            std::array<double, 4> targetStep {};
            for (std::size_t i = 0; i < 4; ++i)
                targetStep[i] = hz[i] / sampleRate;

            if (! primed)
            {
                index = targetIndex;
                level = targetLevel;
                step = targetStep;
                currentFeedback = targetFeedback;
                primed = true;
            }

            const double inverse = 1.0 / static_cast<double>(std::max(1, numSamples));
            std::array<double, 4> indexDelta {}, levelDelta {}, stepDelta {};
            for (std::size_t i = 0; i < 4; ++i)
            {
                indexDelta[i] = (targetIndex[i] - index[i]) * inverse;
                levelDelta[i] = (targetLevel[i] - level[i]) * inverse;
                stepDelta[i] = (targetStep[i] - step[i]) * inverse;
            }
            const double feedbackDelta = (targetFeedback - currentFeedback) * inverse;

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useFm = params.combine == Combine::fm && input.signal != nullptr && amount > 0.0f;
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            const double externalDepth = FmGenerator::externalFmRadians * static_cast<double>(amount * amount);

            for (int s = 0; s < numSamples; ++s)
            {
                for (std::size_t i = 0; i < 4; ++i)
                {
                    index[i] += indexDelta[i];
                    level[i] += levelDelta[i];
                    step[i] += stepDelta[i];
                }
                currentFeedback += feedbackDelta;

                // Operators from 4 down to 1: every modulator is computed before what it drives.
                std::array<double, 4> out {};
                for (int i = 3; i >= 0; --i)
                {
                    const auto ui = static_cast<std::size_t>(i);
                    double phase = kTwoPi * phases[ui];
                    for (std::size_t from = 0; from < 4; ++from)
                        if (route.modulatedBy[ui][from])
                            phase += index[from] * out[from];
                    if (i == 3)
                        phase += currentFeedback * 0.5 * (feedback1 + feedback2);
                    if (i == 0 && useFm)
                        phase += externalDepth * static_cast<double>(input.signal[s]);
                    out[ui] = std::sin(phase);
                }
                feedback2 = feedback1;
                feedback1 = out[3];

                double sum = 0.0, weight = 0.0;
                for (std::size_t i = 0; i < 4; ++i)
                    if (route.carrier[i])
                    {
                        sum += level[i] * out[i];
                        weight += level[i];
                    }
                const float sample = static_cast<float>(sum / std::max(1.0, weight));

                float wrapTau = -1.0f;
                const double next = phases[0] + step[0];
                if (next >= 1.0 && step[0] > 0.0)
                    wrapTau = static_cast<float>(juce::jlimit(1.0e-6, 1.0, (1.0 - phases[0]) / step[0]));
                for (std::size_t i = 0; i < 4; ++i)
                {
                    phases[i] += step[i];
                    phases[i] -= std::floor(phases[i]);
                }

                const unsigned int now = cursor & 3u;
                ringLeft[now] = sample * gainLeft;
                ringRight[now] = sample * gainRight;
                ringMono[now] = sample;
                ringWrap[now] = wrapTau;
                const unsigned int delayed = (cursor + 2u) & 3u;
                float outLeft = ringLeft[delayed], outRight = ringRight[delayed], outMono = ringMono[delayed];
                const float outWrap = ringWrap[delayed];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    const float k = useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }
                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[static_cast<std::size_t>(s)] = outMono;
                wrapTrack[static_cast<std::size_t>(s)] = outWrap;
            }
        }

        [[nodiscard]] double indexForTesting(int oneBasedOperator) const noexcept { return index[static_cast<std::size_t>(oneBasedOperator - 1)]; }

    private:
        static constexpr double kTwoPi = 6.283185307179586476925;

        double sampleRate = 96000.0;
        std::array<double, 4> phases {}, index {}, level {}, step {};
        double feedback1 = 0.0, feedback2 = 0.0, currentFeedback = 0.0;
        bool primed = false;
        std::array<float, 4> ringLeft {}, ringRight {}, ringMono {}, ringWrap {};
        unsigned int cursor = 0;
        std::span<float> signalTrack, wrapTrack;
        GeneratorLink input {};
    };

    static_assert(Module<Fm4Generator>);
}
