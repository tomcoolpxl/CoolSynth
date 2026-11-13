#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    V3 formant and comb processors (DESIGN_V3.md section 3.2; REQUIREMENTS_V3.md: formant with a
    five-vowel morph, size and resonance; comb as a tuned delay with feedback, key tracking and
    polarity).

    Formant: three band-passes in parallel on the first three formants of a, e, i, o and u (male
    voice), Vowel morphing between neighbours (frequencies in the log domain, levels linearly).
    Size scales every formant (above 1 a bigger, darker throat, below 1 a smaller one), Resonance
    narrows the bands from Q 3 to Q 25, the level made up so the vowel's loudness holds across
    it. Mix blends with the dry signal. Frequencies ramp per sample, so a vowel sweep is smooth.

    Comb: a delay tuned to the note (Key Track 1) or to C4 (0), plus Tune in semitones, feeding
    back through a one-pole damping low-pass and a soft limit. Positive polarity reinforces every
    harmonic of the tuned pitch; negative cancels the even ones, so it sounds an octave lower and
    hollow. A Hermite read and a per-sample ramp of the delay keep a swept Tune clean. Mix blends
    with the dry signal.
*/
namespace coolsynth::synth::v3
{
    class FormantProcessor final
    {
    public:
        enum Param : int { vowel = 0, size, resonance, mix, paramCount };
        static constexpr int formants = 3;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Formant", 1,
                {{ { "vowel", "Vowel",     0.0f, 4.0f, 0.0f, Skew::linear,      Unit::generic, true, "A|E|I|O|U" },
                   { "size",  "Size",      0.5f, 2.0f, 1.0f, Skew::logarithmic, Unit::generic, true },
                   { "res",   "Resonance", 0.0f, 1.0f, 0.5f, Skew::linear,      Unit::percent, true },
                   { "mix",   "Mix",       0.0f, 1.0f, 1.0f, Skew::linear,      Unit::percent, true } }},
                paramCount };
            return d;
        }

        /** Formant frequencies and levels at a vowel position 0..4 (a e i o u). */
        static void vowelAt(float position, std::array<float, formants>& hz, std::array<float, formants>& level) noexcept
        {
            static constexpr float table[5][formants] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                          { 570, 840, 2410 },  { 300, 870, 2240 } };
            static constexpr float levels[5][formants] = { { 1.0f, 0.50f, 0.25f }, { 1.0f, 0.40f, 0.30f }, { 1.0f, 0.30f, 0.25f },
                                                           { 1.0f, 0.45f, 0.15f }, { 1.0f, 0.35f, 0.10f } };
            const float x = juce::jlimit(0.0f, 4.0f, position);
            const int lower = std::min(3, static_cast<int>(x));
            const float t = x - static_cast<float>(lower);
            for (int k = 0; k < formants; ++k)
            {
                const auto uk = static_cast<std::size_t>(k);
                hz[uk] = std::exp(std::log(table[lower][k]) + (std::log(table[lower + 1][k]) - std::log(table[lower][k])) * t);
                level[uk] = levels[lower][k] + (levels[lower + 1][k] - levels[lower][k]) * t;
            }
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            reset();
        }

        void reset() noexcept
        {
            for (auto& c : channels)
                c = {};
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            std::array<float, formants> hz {}, level {};
            vowelAt(params.p[vowel], hz, level);
            const float scale = juce::jlimit(0.5f, 2.0f, params.p[size]);
            const float q = 3.0f * std::pow(25.0f / 3.0f, juce::jlimit(0.0f, 1.0f, params.p[resonance]));
            const float k = 1.0f / q;
            // Narrower bands pass less of a broadband source: make it up, about sqrt(Q) for noise-like
            // input, so the vowel's loudness holds while Resonance turns.
            const float makeup = 1.2f * std::sqrt(q / 3.0f);
            const float wet = juce::jlimit(0.0f, 1.0f, params.p[mix]);

            std::array<float, formants> gTarget {};
            for (std::size_t i = 0; i < gTarget.size(); ++i)
                gTarget[i] = std::tan(juce::MathConstants<float>::pi * juce::jlimit(50.0f, 0.45f * sampleRate, hz[i] / scale) / sampleRate);
            if (! primed)
            {
                g = gTarget;
                gains = level;
                primed = true;
            }
            const float inverse = 1.0f / static_cast<float>(std::max(1, numSamples));
            std::array<float, formants> gStep {}, levelStep {};
            for (std::size_t i = 0; i < gStep.size(); ++i)
            {
                gStep[i] = (gTarget[i] - g[i]) * inverse;
                levelStep[i] = (level[i] - gains[i]) * inverse;
            }

            for (int s = 0; s < numSamples; ++s)
            {
                for (std::size_t i = 0; i < g.size(); ++i)
                {
                    g[i] += gStep[i];
                    gains[i] += levelStep[i];
                }
                float* io[2] = { left + s, right + s };
                for (std::size_t ch = 0; ch < 2; ++ch)
                {
                    const float x = *io[ch];
                    float y = 0.0f;
                    for (std::size_t i = 0; i < g.size(); ++i)
                    {
                        auto& band = channels[ch][i];
                        const float a1 = 1.0f / (1.0f + g[i] * (g[i] + k));
                        const float a2 = g[i] * a1;
                        const float v3 = x - band.ic2;
                        const float v1 = a1 * band.ic1 + a2 * v3;
                        const float v2 = band.ic2 + g[i] * v1;
                        band.ic1 = 2.0f * v1 - band.ic1;
                        band.ic2 = 2.0f * v2 - band.ic2;
                        y += gains[i] * k * v1;
                    }
                    *io[ch] = x + (y * makeup - x) * wet;
                }
            }
        }

    private:
        struct Band
        {
            float ic1 = 0.0f, ic2 = 0.0f;
        };

        float sampleRate = 96000.0f;
        std::array<std::array<Band, formants>, 2> channels {};
        std::array<float, formants> g {}, gains {};
        bool primed = false;
    };

    class CombProcessor final
    {
    public:
        enum Param : int { tune = 0, feedback, damping, keyTrack, polarity, mix, paramCount };
        static constexpr float keyReferenceHz = 261.6256f;
        static constexpr float lowestHz = 20.0f;
        static constexpr float maxFeedback = 0.98f;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Comb", 1,
                {{ { "tune",     "Tune",      -24.0f, 24.0f, 0.0f, Skew::linear, Unit::semitones, true },
                   { "fb",       "Feedback",  0.0f,   0.98f, 0.8f, Skew::linear, Unit::percent,   true },
                   { "damp",     "Damping",   0.0f,   1.0f,  0.2f, Skew::linear, Unit::percent,   true },
                   { "key",      "Key Track", 0.0f,   1.0f,  1.0f, Skew::linear, Unit::percent,   false },
                   { "polarity", "Polarity",  0.0f,   1.0f,  0.0f, Skew::linear, Unit::choice,    false, "Positive|Negative" },
                   { "mix",      "Mix",       0.0f,   1.0f,  0.5f, Skew::linear, Unit::percent,   true } }},
                paramCount };
            return d;
        }

        static int lineLength(double sampleRate) noexcept { return static_cast<int>(std::ceil(sampleRate / lowestHz)) + 8; }

        static std::size_t memoryFloats(double sampleRate, int) { return 2 * static_cast<std::size_t>(lineLength(sampleRate)); }

        void prepare(double newSampleRate, int, SlotMemory& memory) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            length = lineLength(newSampleRate);
            lineLeft = memory.take(static_cast<std::size_t>(length));
            lineRight = memory.take(static_cast<std::size_t>(length));
            reset();
        }

        void reset() noexcept
        {
            std::fill(lineLeft.begin(), lineLeft.end(), 0.0f);
            std::fill(lineRight.begin(), lineRight.end(), 0.0f);
            write = 0;
            dampLeft = dampRight = 0.0f;
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            if (lineLeft.size() < static_cast<std::size_t>(length) || length < 8)
                return;
            const float key = juce::jlimit(0.0f, 1.0f, params.p[keyTrack]);
            const float note = std::max(1.0f, params.frequencyHz);
            const float hz = juce::jlimit(lowestHz, 0.25f * sampleRate,
                                          keyReferenceHz * std::pow(note / keyReferenceHz, key) * std::exp2(juce::jlimit(-24.0f, 24.0f, params.p[tune]) / 12.0f));
            const float targetDelay = juce::jlimit(2.0f, static_cast<float>(length - 4), sampleRate / hz);
            const float sign = params.p[polarity] >= 0.5f ? -1.0f : 1.0f;
            const float fb = sign * juce::jlimit(0.0f, maxFeedback, params.p[feedback]);
            const float dampAmount = juce::jlimit(0.0f, 1.0f, params.p[damping]);
            const float dampCoefficient = 1.0f - 0.95f * dampAmount; // one-pole low-pass in the loop: 1 is open
            const float wet = juce::jlimit(0.0f, 1.0f, params.p[mix]);

            if (! primed)
            {
                delay = targetDelay;
                primed = true;
            }
            const float delayStep = (targetDelay - delay) / static_cast<float>(std::max(1, numSamples));

            for (int s = 0; s < numSamples; ++s)
            {
                delay += delayStep;
                left[s] = tick(lineLeft, dampLeft, left[s], fb, dampCoefficient, wet);
                right[s] = tick(lineRight, dampRight, right[s], fb, dampCoefficient, wet);
                write = (write + 1) % length;
            }
        }

    private:
        float read(std::span<float> line, float delaySamples) const noexcept
        {
            const float position = static_cast<float>(write) - delaySamples;
            int base = static_cast<int>(std::floor(position));
            const float t = position - static_cast<float>(base);
            auto at = [&](int i)
            {
                int index = i % length;
                if (index < 0)
                    index += length;
                return line[static_cast<std::size_t>(index)];
            };
            const float xm1 = at(base - 1), x0 = at(base), x1 = at(base + 1), x2 = at(base + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        float tick(std::span<float> line, float& dampState, float x, float fb, float dampCoefficient, float wet) noexcept
        {
            const float delayed = read(line, delay);
            dampState += (delayed - dampState) * dampCoefficient;
            float in = x + fb * dampState;
            in = 2.0f * dsp::fastTanh(0.5f * in); // soft limit: high feedback rings without running away
            line[static_cast<std::size_t>(write)] = std::abs(in) < 1.0e-20f ? 0.0f : in;
            return x + (in - x) * wet;
        }

        float sampleRate = 96000.0f;
        int length = 0;
        int write = 0;
        float delay = 100.0f;
        bool primed = false;
        float dampLeft = 0.0f, dampRight = 0.0f;
        std::span<float> lineLeft, lineRight;
    };

    static_assert(Module<FormantProcessor>);
    static_assert(Module<CombProcessor>);
}
