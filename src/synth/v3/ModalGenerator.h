#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 modal generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: partials with a material
    morph, mallet hardness, decay, strike position).

    Twelve partials, each a rotating complex resonator (z = (z + x) r e^{j theta}): its ring is a
    decaying sine of exactly the set frequency and decay, and it stays stable and click-free
    while frequency or decay move. A strike feeds every partial the same raised-cosine pulse;
    Hardness narrows the pulse from half the note's period (a felt mallet: little but the
    fundamental answers) to a single sample (a hard one: every partial does). The fundamental's
    level does not depend on the pulse's width.

    Material morphs the partial ratios:

        Wood    a tuned (undercut) bar, the marimba's 1 : 4 : 10.1 : 19.1
        Metal   a uniform free bar, the glockenspiel's 1 : 2.76 : 5.40 : 8.93
        Glass   a wine glass, 1 : 2.32 : 4.25 : 6.63 : 9.38
        Tine    a bar clamped at one end, the kalimba's 1 : 6.27 : 17.5 : 34.4

    The uniform and the clamped bar's ratios are the theory's. The tuned bar's are measured on a
    recorded marimba (4.00 to 4.02, 10.09 to 10.15, 18.8 to 19.4, 30.7), the glass's first five
    are the Csound manual's; both are extended along their trend.

    Decay is the fundamental's 60 dB time; a partial at ratio q lasts q^-e times as long. Measured
    on recorded bars e is close to 1 (a material rings for about as many cycles whatever the
    partial): 1.0 for wood, glass and tine, 1.2 for metal. Damping adds up to 1.2 to it. Strike Position weights partial k by
    sin(k pi position), as a bar struck there answers: at 0.5 every second partial is silent.
    Release Damp shortens every decay when the key goes up (0 leaves the bar ringing). Partials
    above 20 kHz are left out, and a partial that has rung out costs nothing. A retriggered voice
    strikes the ringing bar again. Output is mono, panned, two samples late like every generator.
*/
namespace coolsynth::synth::v3
{
    class ModalGenerator final
    {
    public:
        enum Param : int { material = 0, decay, hardness, position, damping, release, paramCount };
        static constexpr int partials = 12;
        static constexpr int latencySamples = generatorLatencySamples;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Modal", 2,
                {{ { "material", "Material",     0.0f,  3.0f,  0.0f, Skew::linear,      Unit::generic, true, "Wood|Metal|Glass|Tine" },
                   { "decay",    "Decay",        0.05f, 10.0f, 1.0f, Skew::logarithmic, Unit::generic, true },
                   { "hard",     "Hardness",     0.0f,  1.0f,  0.4f, Skew::linear,      Unit::percent, true },
                   { "pos",      "Strike Pos",   0.02f, 0.5f,  0.3f, Skew::linear,      Unit::percent, true },
                   { "damp",     "Damping",      0.0f,  1.0f,  0.3f, Skew::linear,      Unit::percent, true },
                   { "release",  "Release Damp", 0.0f,  1.0f,  0.0f, Skew::linear,      Unit::percent, true } }},
                paramCount };
            return d;
        }

        /** Partial ratios and the exponent of the material's upper-partial damping, at a position 0..3. */
        static void materialAt(float position, std::array<float, partials>& ratio, float& dampingExponent) noexcept
        {
            static constexpr float ratios[4][partials] = {
                { 1.0f, 4.0f, 10.1f, 19.1f, 30.7f, 44.7f, 61.0f, 79.5f, 100.0f, 123.0f, 148.0f, 175.0f },              // tuned bar
                { 1.0f, 2.757f, 5.404f, 8.933f, 13.344f, 18.638f, 24.814f, 31.872f, 39.812f, 48.635f, 58.340f, 68.928f }, // uniform bar
                { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.45f, 15.82f, 19.48f, 23.42f, 27.63f, 32.11f, 36.86f },           // wine glass
                { 1.0f, 6.267f, 17.548f, 34.386f, 56.843f, 84.91f, 118.6f, 157.9f, 202.8f, 253.3f, 309.5f, 371.2f } };  // clamped bar
            static constexpr float exponents[4] = { 1.0f, 1.2f, 1.0f, 1.0f };
            const float x = juce::jlimit(0.0f, 3.0f, position);
            const int lower = std::min(2, static_cast<int>(x));
            const float t = x - static_cast<float>(lower);
            for (std::size_t k = 0; k < ratio.size(); ++k)
                ratio[k] = t <= 0.0f ? ratios[lower][k]
                                     : ratios[lower][k] * std::pow(ratios[lower + 1][k] / ratios[lower][k], t);
            dampingExponent = exponents[lower] + (exponents[lower + 1] - exponents[lower]) * t;
        }

        /** The strike pulse's length in samples: half a period at Hardness 0, one sample at 1. */
        static int pulseLength(double periodSamples, float hardnessAmount) noexcept
        {
            const double h = juce::jlimit(0.0, 1.0, static_cast<double>(hardnessAmount));
            return std::max(1, static_cast<int>(std::lround(0.5 * periodSamples / (1.0 + 63.0 * h * h))));
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            reset();
        }

        void reset() noexcept
        {
            re.fill(0.0f);
            im.fill(0.0f);
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            pulseRemaining = pulseTotal = 0;
            strikePending = false;
            gateOpen = false;
            ringing = false;
            ring.fill(0.0f);
            cursor = 0;
            input = {};
            cachedMaterial = cachedDamping = cachedPosition = -1.0f;
            cachedT60 = cachedHz = -1.0;
        }

        void noteOn(const NoteContext&) noexcept
        {
            strikePending = true;
            gateOpen = true;
        }

        /** The same voice again: strike the ringing bar. */
        void strike() noexcept
        {
            strikePending = true;
            gateOpen = true;
        }

        void noteOff() noexcept { gateOpen = false; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const double hz = juce::jlimit(10.0, 0.45 * sampleRate, static_cast<double>(params.frequencyHz));

            double t60 = juce::jlimit(0.05, 10.0, static_cast<double>(params.p[decay]));
            if (! gateOpen)
            {
                const double releaseAmount = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[release]));
                if (releaseAmount > 0.0)
                    t60 = std::min(t60, 3.0 * std::pow(0.01, releaseAmount)); // 3 s down to 30 ms
            }
            updateCoefficients(hz, t60, params.p[material], juce::jlimit(0.0f, 1.0f, params.p[damping]),
                               juce::jlimit(0.02f, 0.5f, params.p[position]));

            if (strikePending)
            {
                pulseTotal = pulseLength(sampleRate / hz, params.p[hardness]);
                pulseRemaining = pulseTotal;
                strikePending = false;
                ringing = true;
            }
            // The pulse sums to one whatever its length, so a partial far below its cutoff rings at its weight.
            const float pulseGain = pulseTotal > 1 ? 2.0f / static_cast<float>(pulseTotal) : 1.0f;

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                float sum = 0.0f;
                if (ringing)
                {
                    float x = 0.0f;
                    if (pulseRemaining > 0)
                    {
                        const float phase = (static_cast<float>(pulseTotal - pulseRemaining) + 0.5f) / static_cast<float>(pulseTotal);
                        x = pulseTotal > 1 ? pulseGain * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase)) : 1.0f;
                        --pulseRemaining;
                    }

                    for (std::size_t k = 0; k < re.size(); ++k)
                    {
                        const float a = re[k] + x * weight[k];
                        const float b = im[k];
                        re[k] = a * cosine[k] - b * sine[k];
                        im[k] = a * sine[k] + b * cosine[k];
                        sum += im[k];
                    }

                    // Every 256 samples: clear what has rung out, so it never goes denormal, and rest when all has.
                    if ((cursor & 255u) == 0u && pulseRemaining == 0)
                    {
                        bool any = false;
                        for (std::size_t k = 0; k < re.size(); ++k)
                        {
                            if (std::abs(re[k]) + std::abs(im[k]) < 1.0e-9f)
                                re[k] = im[k] = 0.0f;
                            else
                                any = true;
                        }
                        ringing = any;
                    }
                }

                const unsigned int now = cursor & 3u;
                ring[now] = sum * norm;
                float out = ring[(cursor + 2u) & 3u];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    out *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = out * gainLeft;
                right[s] = out * gainRight;
                signalTrack[static_cast<std::size_t>(s)] = out;
            }
        }

    private:
        /** Recomputes only what a changed value touches: ratios, decays, strike weights, rotations. */
        void updateCoefficients(double hz, double t60, float materialNow, float dampingNow, float positionNow) noexcept
        {
            bool decaysChanged = false, weightsChanged = false, rotationsChanged = false;
            if (materialNow != cachedMaterial || dampingNow != cachedDamping)
            {
                float exponent = 1.0f;
                materialAt(materialNow, ratio, exponent);
                exponent += 1.2f * dampingNow;
                for (std::size_t k = 0; k < ratio.size(); ++k)
                {
                    lifeShare[k] = std::pow(ratio[k], -exponent);
                    tilt[k] = 1.0f / std::sqrt(ratio[k]);
                }
                cachedMaterial = materialNow;
                cachedDamping = dampingNow;
                decaysChanged = weightsChanged = rotationsChanged = true;
            }
            if (t60 != cachedT60 || decaysChanged)
            {
                for (std::size_t k = 0; k < ratio.size(); ++k)
                    radius[k] = std::pow(10.0, -3.0 / (std::max(0.002, t60 * static_cast<double>(lifeShare[k])) * sampleRate));
                cachedT60 = t60;
                rotationsChanged = true;
            }
            if (positionNow != cachedPosition)
            {
                cachedPosition = positionNow;
                weightsChanged = true;
            }
            if (hz != cachedHz)
            {
                cachedHz = hz;
                rotationsChanged = weightsChanged = true; // a partial may have crossed the top of the band
            }

            const double top = std::min(20000.0, 0.45 * sampleRate);
            if (weightsChanged)
            {
                // A strike excites partial k by where it lands on the bar, and a little less the higher the partial.
                float energy = 0.0f;
                for (std::size_t k = 0; k < ratio.size(); ++k)
                {
                    const bool audible = hz * static_cast<double>(ratio[k]) < top;
                    weight[k] = audible ? tilt[k] * std::abs(std::sin(static_cast<float>(k + 1) * juce::MathConstants<float>::pi * positionNow)) : 0.0f;
                    energy += weight[k] * weight[k];
                }
                norm = energy > 0.0f ? 0.9f / std::sqrt(energy) : 0.0f;
            }
            if (rotationsChanged)
            {
                for (std::size_t k = 0; k < ratio.size(); ++k)
                {
                    const double f = hz * static_cast<double>(ratio[k]);
                    if (f >= top)
                    {
                        // Out of the band: let what rings there die within a millisecond instead of aliasing.
                        cosine[k] = sine[k] = 0.0f;
                        continue;
                    }
                    const double theta = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
                    cosine[k] = static_cast<float>(radius[k] * std::cos(theta));
                    sine[k] = static_cast<float>(radius[k] * std::sin(theta));
                }
            }
        }

        double sampleRate = 96000.0;
        std::array<float, partials> re {}, im {};
        std::array<float, partials> ratio {}, lifeShare {}, tilt {}, weight {}, cosine {}, sine {};
        std::array<double, partials> radius {};
        float norm = 0.0f;
        float cachedMaterial = -1.0f, cachedDamping = -1.0f, cachedPosition = -1.0f;
        double cachedT60 = -1.0, cachedHz = -1.0;
        int pulseRemaining = 0, pulseTotal = 0;
        bool strikePending = false;
        bool gateOpen = false;
        bool ringing = false;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<ModalGenerator>);
}
