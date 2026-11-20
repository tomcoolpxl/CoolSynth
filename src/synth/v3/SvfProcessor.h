#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    V3 state-variable filter processor (docs/design/DESIGN_V3.md section 3.2; docs/design/REQUIREMENTS_V3.md: LP2, HP,
    BP, notch, cutoff, resonance, drive).

    Andy Simper's trapezoidal (zero-delay-feedback) state-variable filter, 12 dB per octave, with
    four taps: low-pass, high-pass, band-pass at a peak gain of 1, and notch. Resonance runs from
    Q 0.5 (no peak) to Q 40 (a long, singing ring that still decays). A soft limit on the band
    state (4 tanh(v / 4)) keeps a driven, fully resonant filter bounded without touching normal
    levels. Drive feeds a peak-normalised tanh stage before the filter, as on the ladder. Key
    tracking scales the cutoff with the note around C4. Cutoff and resonance ramp per sample.

    Added in D233 (docs/research/svf-sem-sid.md), all at the end of their lists:
    - LP>HP, the Oberheim SEM's mode knob: Morph crossfades the low-pass into the high-pass, through
      the notch at the middle, divided by the larger of the two shares so both ends stay at 0 dB.
    - LP+BP and BP+HP, the SID's other sums.
    - Character: Clean (as before, bit for bit), SID 6581, SID 8580. The 8580: the cutoff held
      between 30 Hz and 12.5 kHz, resonance in the chip's 16 steps (1/Q = 2^((4 - step) / 8), after
      libresidfp). The 6581: the knob read as the register an 8580 would need, then through the
      6581's measured curve (reSID's points: 220 Hz at the bottom, the drop at the middle, 18 kHz at
      the top), resonance 1/Q = (15 - step) / 8 with its last step held at Q 16 so it still decays,
      each integrator's cutoff moving with what it integrates (the transistors' law: one-sided
      distortion and a cutoff that climbs with the level), an uneven clip on its stages and a
      little of the dry signal leaking round it. Curve moves either chip's cutoff darker or brighter,
      as chips differ (reSIDfp's filter curve).
*/
namespace coolsynth::synth::v3
{
    class SvfProcessor final
    {
    public:
        enum Param : int { cutoff = 0, resonance, drive, keyTrack, mode, morph, character, curve, paramCount };
        enum class Mode : int { lowpass = 0, highpass, bandpass, notch, morph, lowBand, bandHigh };
        enum class Character : int { clean = 0, sid6581, sid8580 };
        static constexpr int modeCount = 7;
        static constexpr float keyReferenceHz = 261.6256f;
        static constexpr float minQ = 0.5f;
        static constexpr float maxQ = 40.0f;
        static constexpr float sid6581Bend = 0.3f;  // how far a full-scale signal moves the 6581's integrators
        static constexpr float sid6581Leak = 0.003f; // the dry signal that passes round its filter, about -50 dB

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "SVF", 1,
                {{ { "cutoff", "Cutoff",    20.0f, 20000.0f, 1000.0f, Skew::logarithmic, Unit::hertz,    true },
                   { "res",    "Resonance", 0.0f,  1.0f,     0.1f,    Skew::linear,      Unit::percent,  true },
                   { "drive",  "Drive",     0.0f,  24.0f,    0.0f,    Skew::linear,      Unit::decibels, true },
                   { "key",    "Key Track", 0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false },
                   { "mode",   "Mode",      0.0f,  6.0f,     0.0f,    Skew::linear,      Unit::choice,   false, "LP|HP|BP|Notch|LP>HP|LP+BP|BP+HP" },
                   { "morph",  "Morph",     0.0f,  1.0f,     0.0f,    Skew::linear,      Unit::percent,  true },
                   { "char",   "Character", 0.0f,  2.0f,     0.0f,    Skew::linear,      Unit::choice,   false, "Clean|SID 6581|SID 8580" },
                   { "curve",  "Curve",     0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false } }},
                paramCount };
            return d;
        }

        /** The 8580's cutoff for an 11-bit register value, and the 6581's (reSID's measured points, filter.cc). */
        static double sid8580Hz(double reg) noexcept { return interpolate(sid8580Points, reg); }
        static double sid6581Hz(double reg) noexcept { return interpolate(sid6581Points, reg); }

        /** The register an 8580 needs for a cutoff. */
        static double sid8580Register(double hz) noexcept
        {
            const auto& p = sid8580Points;
            if (hz <= p.front()[1])
                return 0.0;
            for (std::size_t i = 1; i < p.size(); ++i)
                if (hz <= p[i][1])
                    return p[i - 1][0] + (p[i][0] - p[i - 1][0]) * (hz - p[i - 1][1]) / (p[i][1] - p[i - 1][1]);
            return 2047.0;
        }

        /** The damping of a resonance in the chip's 16 steps. */
        static float sidDamping(Character c, float resonanceNormalised) noexcept
        {
            const int step = juce::jlimit(0, 15, static_cast<int>(std::lround(resonanceNormalised * 15.0f)));
            if (c == Character::sid6581)
                return std::max(1.0f / 16.0f, static_cast<float>(15 - step) / 8.0f); // step 15 held at Q 16 (D38)
            return std::exp2(static_cast<float>(4 - step) / 8.0f);
        }

        /** Resonance 0..1 to the filter's damping k = 1 / Q, Q growing exponentially from 0.5 to 40. */
        static float dampingFor(float resonanceNormalised) noexcept
        {
            const float r = juce::jlimit(0.0f, 1.0f, resonanceNormalised);
            return 1.0f / (minQ * std::pow(maxQ / minQ, r));
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            maxCutoffHz = std::min(24000.0f, 0.45f * sampleRate);
            reset();
        }

        void reset() noexcept
        {
            left = {};
            right = {};
            primed = false;
            morphPrimed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* leftSamples, float* rightSamples, int numSamples, const SlotParams& params) noexcept
        {
            const float key = juce::jlimit(0.0f, 1.0f, params.p[keyTrack]);
            float hz = params.p[cutoff] * std::pow(std::max(1.0f, params.frequencyHz) / keyReferenceHz, key);
            const auto chip = static_cast<Character>(juce::jlimit(0, 2, static_cast<int>(std::lround(params.p[character]))));
            const float bright = juce::jlimit(0.0f, 1.0f, params.p[curve]) - 0.5f;
            if (chip == Character::sid8580)
                hz = juce::jlimit(30.0f, 12500.0f, hz * std::exp2(0.5f * bright));
            else if (chip == Character::sid6581)
                hz = static_cast<float>(sid6581Hz(sid8580Register(static_cast<double>(hz)))) * std::exp2(bright);
            hz = juce::jlimit(10.0f, maxCutoffHz, hz);
            const float gTarget = std::tan(juce::MathConstants<float>::pi * hz / sampleRate);
            const float kTarget = chip == Character::clean ? dampingFor(params.p[resonance]) : sidDamping(chip, params.p[resonance]);
            const auto tap = static_cast<Mode>(juce::jlimit(0, modeCount - 1, static_cast<int>(std::lround(params.p[mode]))));
            const float morphTarget = juce::jlimit(0.0f, 1.0f, params.p[morph]);
            if (! morphPrimed)
            {
                morphValue = morphTarget;
                morphPrimed = true;
            }
            const float driveDb = juce::jlimit(0.0f, 24.0f, params.p[drive]);
            const bool driven = driveDb > 0.0f;
            const float driveGain = kDriveHeadroom * std::pow(10.0f, driveDb * 0.05f);
            const float driveNormalise = 1.0f / dsp::fastTanh(driveGain);

            if (! primed)
            {
                g = gTarget;
                k = kTarget;
                primed = true;
            }
            const float inverse = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float gStep = (gTarget - g) * inverse;
            const float kStep = (kTarget - k) * inverse;
            const float morphStep = (morphTarget - morphValue) * inverse;

            // Today's four taps, clean: the code as it was, so every sound made with it plays the same to the bit.
            if (chip == Character::clean && static_cast<int>(tap) <= static_cast<int>(Mode::notch))
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    g += gStep;
                    k += kStep;
                    const float a1 = 1.0f / (1.0f + g * (g + k));
                    const float a2 = g * a1;
                    float inL = leftSamples[i], inR = rightSamples[i];
                    if (driven)
                    {
                        inL = dsp::fastTanh(inL * driveGain) * driveNormalise;
                        inR = dsp::fastTanh(inR * driveGain) * driveNormalise;
                    }
                    leftSamples[i] = tick(left, inL, g, a1, a2, k, tap);
                    rightSamples[i] = tick(right, inR, g, a1, a2, k, tap);
                }
                g = gTarget;
                k = kTarget;
                morphValue = morphTarget;
                return;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                g += gStep;
                k += kStep;
                morphValue += morphStep;
                float inL = leftSamples[i], inR = rightSamples[i];
                if (driven)
                {
                    inL = dsp::fastTanh(inL * driveGain) * driveNormalise;
                    inR = dsp::fastTanh(inR * driveGain) * driveNormalise;
                }
                const Parts l = chip == Character::sid6581 ? tick6581(left, inL, g, k) : tickParts(left, inL, g, k);
                const Parts r = chip == Character::sid6581 ? tick6581(right, inR, g, k) : tickParts(right, inR, g, k);
                leftSamples[i] = mix(l, inL, tap, morphValue);
                rightSamples[i] = mix(r, inR, tap, morphValue);
                if (chip == Character::sid6581)
                {
                    leftSamples[i] += sid6581Leak * inL;
                    rightSamples[i] += sid6581Leak * inR;
                }
            }
            g = gTarget;
            k = kTarget;
            morphValue = morphTarget;
        }

    private:
        static constexpr float kDriveHeadroom = 0.25f;

        struct Channel
        {
            float ic1 = 0.0f, ic2 = 0.0f;
            float lastHigh = 0.0f, lastBand = 0.0f; // the 6581's integrators bend with what they integrated last
        };

        /** One sample's taps: low-pass, band-pass at a peak gain of 1, high-pass. */
        struct Parts
        {
            float low = 0.0f, band = 0.0f, high = 0.0f;
        };

        /** The same filter as tick(), all its taps at once. */
        static Parts tickParts(Channel& c, float x, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float v3 = x - c.ic2;
            float v1 = a1 * c.ic1 + a2 * v3;
            v1 = 4.0f * dsp::fastTanh(0.25f * v1);
            const float v2 = c.ic2 + g * v1;
            c.ic1 = 2.0f * v1 - c.ic1;
            c.ic2 = 2.0f * v2 - c.ic2;
            return { v2, k * v1, x - k * v1 - v2 };
        }

        /** The 6581: each integrator's cutoff moves with what it integrated last (one-sided, rising with the level),
            and its stages clip unevenly. Simper's solve with two g's: v1 = (ic1 + g1 (x - ic2)) / (1 + g1 (k + g2)). */
        static Parts tick6581(Channel& c, float x, float g, float k) noexcept
        {
            const float g1 = g * juce::jlimit(0.25f, 2.0f, 1.0f - sid6581Bend * c.lastHigh);
            const float g2 = g * juce::jlimit(0.25f, 2.0f, 1.0f - sid6581Bend * c.lastBand);
            float v1 = (c.ic1 + g1 * (x - c.ic2)) / (1.0f + g1 * (k + g2));
            v1 = uneven(v1);
            const float v2 = c.ic2 + g2 * v1;
            const float high = uneven(x - k * v1 - v2);
            c.ic1 = 2.0f * v1 - c.ic1;
            c.ic2 = 2.0f * v2 - c.ic2;
            c.lastHigh = high;
            c.lastBand = v1;
            return { v2, k * v1, high };
        }

        /** The NMOS stages' soft limit, lower on the negative side. */
        static float uneven(float v) noexcept
        {
            return v >= 0.0f ? 3.0f * dsp::fastTanh(v * (1.0f / 3.0f)) : 2.0f * dsp::fastTanh(v * 0.5f);
        }

        static float mix(const Parts& p, float x, Mode tap, float m) noexcept
        {
            switch (tap)
            {
                case Mode::lowpass:  return p.low;
                case Mode::highpass: return p.high;
                case Mode::bandpass: return p.band;
                case Mode::notch:    return x - p.band;
                case Mode::morph:    return ((1.0f - m) * p.low + m * p.high) / std::max(m, 1.0f - m); // the SEM's knob
                case Mode::lowBand:  return p.low + p.band;
                case Mode::bandHigh: return p.band + p.high;
            }
            return p.low;
        }

        template <std::size_t N>
        static double interpolate(const std::array<std::array<double, 2>, N>& p, double reg) noexcept
        {
            if (reg <= p.front()[0])
                return p.front()[1];
            for (std::size_t i = 1; i < N; ++i)
                if (reg <= p[i][0])
                {
                    const double span = p[i][0] - p[i - 1][0];
                    return span <= 0.0 ? p[i][1] : p[i - 1][1] + (p[i][1] - p[i - 1][1]) * (reg - p[i - 1][0]) / span;
                }
            return p.back()[1];
        }

        /** reSID's measured cutoffs (Dag Lem, filter.cc): register, Hz. */
        static constexpr std::array<std::array<double, 2>, 27> sid6581Points { {
            { 0, 220 }, { 128, 230 }, { 256, 250 }, { 384, 300 }, { 512, 420 }, { 640, 780 }, { 768, 1600 }, { 832, 2300 },
            { 896, 3200 }, { 960, 4300 }, { 992, 5000 }, { 1008, 5400 }, { 1016, 5700 }, { 1023, 6000 }, { 1024, 4600 },
            { 1032, 4800 }, { 1056, 5300 }, { 1088, 6000 }, { 1120, 6600 }, { 1152, 7200 }, { 1280, 9500 }, { 1408, 12000 },
            { 1536, 14500 }, { 1664, 16000 }, { 1792, 17100 }, { 1920, 17700 }, { 2047, 18000 } } };
        static constexpr std::array<std::array<double, 2>, 17> sid8580Points { {
            { 0, 0 }, { 128, 800 }, { 256, 1600 }, { 384, 2500 }, { 512, 3300 }, { 640, 4100 }, { 768, 4800 }, { 896, 5600 },
            { 1024, 6500 }, { 1152, 7500 }, { 1280, 8400 }, { 1408, 9200 }, { 1536, 9800 }, { 1664, 10500 }, { 1792, 11000 },
            { 1920, 11700 }, { 2047, 12500 } } };

        static float tick(Channel& c, float x, float g, float a1, float a2, float k, Mode tap) noexcept
        {
            const float v3 = x - c.ic2;
            float v1 = a1 * c.ic1 + a2 * v3;
            v1 = 4.0f * dsp::fastTanh(0.25f * v1); // the band state's soft limit
            const float v2 = c.ic2 + g * v1;       // the low-pass integrates the limited band
            c.ic1 = 2.0f * v1 - c.ic1;
            c.ic2 = 2.0f * v2 - c.ic2;
            switch (tap)
            {
                case Mode::lowpass:  return v2;
                case Mode::highpass: return x - k * v1 - v2;
                case Mode::bandpass: return k * v1;
                case Mode::notch:    return x - k * v1;
            }
            return v2;
        }

        float sampleRate = 96000.0f;
        float maxCutoffHz = 24000.0f;
        float g = 0.0f, k = 1.0f, morphValue = 0.0f;
        bool primed = false, morphPrimed = false;
        Channel left, right;
    };

    static_assert(Module<SvfProcessor>);
}
