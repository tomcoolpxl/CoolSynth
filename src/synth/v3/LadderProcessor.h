#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <juce_core/juce_core.h>

#if defined(_M_X64) || defined(__SSE2__)
 #include <emmintrin.h>
 #define COOLSYNTH_LADDER_SSE 1
#endif

#include "DspMath.h"
#include "Module.h"

/*
    V3 ladder processor (DESIGN_V3.md section 3.2).

    A transistor ladder with a nonlinearity in every stage, not only at the input: each of the
    four one-pole stages integrates tanh(previous) - tanh(own), as the differential pairs of the
    Moog ladder do (Huovilainen 2004), and the input pair sees tanh(input - k * output). The loop
    is solved without a unit delay by linearising every tanh around its last operating point
    (tanh(v) ~ v * tanh(v0) / v0, the "cheap nonlinear zero-delay filter" method) and solving the
    resulting linear ladder exactly: one rational tanh per stage per sample, no iteration.
    Self-oscillation settles at the level the transistors allow, and a -110 dB noise floor lets it
    start from silence as the analogue circuit does.

    FM is the Minimoog's filter-modulation switch with oscillator 3 as the source (decision D114):
    generator 3's signal, whatever its level in the mix, moves the cutoff up and down by that
    many octaves, sample by sample. At the note's own pitch or an octave under it this is not a
    wobble but a growl: side tones around every harmonic the filter passes, and brightness the
    low-pass alone would have taken away. The cutoff's coefficient is scaled by 2^(FM * signal)
    per sample and held under the filter's highest cutoff.

    Before the ladder, Drive feeds a peak-normalised tanh stage that is nearly clean at 0 dB.
    Bass restores the low end the feedback takes away, at the input, so self-oscillation is not
    boosted with it. Mode taps the stages: 24 and 12 dB low-pass, band-pass and 12 dB high-pass,
    each with the resonance of the full loop. Cutoff and resonance ramp per sample.

    Left and right run through the same arithmetic side by side in two lanes of one SSE register
    (decision D138): the same operations in the same order as the one-channel code, so the output
    is the same to the last bit, at about half the cost.
*/
namespace coolsynth::synth::v3
{
    class LadderProcessor final
    {
    public:
        enum Param : int { cutoff = 0, resonance, drive, keyTrack, mode, bass, fm, leak, paramCount };
        enum class Mode : int { lowpass24 = 0, lowpass12, bandpass, highpass12 };

        static constexpr float keyReferenceHz = 261.6256f; // C4: key tracking is neutral here
        static constexpr float oscillationResonance = 0.9f; // feedback reaches 4, the oscillation threshold, here
        static constexpr float maxFeedback = 5.0f;          // at full resonance: a strong, stable sine

        /**
            Resonance to loop feedback. Up to the threshold, k = 4 sqrt(r / 0.9), so the audible peak
            grows evenly across the travel; the last tenth drives the loop past it, so the ladder
            sings from the noise floor and the transistors set its level.
        */
        static float feedbackFor(float resonanceNormalised) noexcept
        {
            const float r = juce::jlimit(0.0f, 1.0f, resonanceNormalised);
            if (r < oscillationResonance)
                return 4.0f * std::sqrt(r / oscillationResonance);
            return 4.0f + (maxFeedback - 4.0f) * (r - oscillationResonance) / (1.0f - oscillationResonance);
        }

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Ladder", 3,
                {{ { "cutoff", "Cutoff",    20.0f, 20000.0f, 3200.0f, Skew::logarithmic, Unit::hertz,    true },
                   { "res",    "Resonance", 0.0f,  1.0f,     0.1f,    Skew::linear,      Unit::percent,  true },
                   { "drive",  "Drive",     0.0f,  24.0f,    0.0f,    Skew::linear,      Unit::decibels, true },
                   { "key",    "Key Track", 0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  false },
                   { "mode",   "Mode",      0.0f,  3.0f,     0.0f,    Skew::linear,      Unit::choice,   false, "LP24|LP12|BP|HP12" },
                   { "bass",   "Bass",      0.0f,  1.0f,     0.5f,    Skew::linear,      Unit::percent,  true },
                   { "fm",     "G3 To Cutoff", 0.0f, 3.0f,   0.0f,    Skew::squared,     Unit::generic,  true },
                   { "leak",   "Leak",      0.0f,  0.25f,    0.0f,    Skew::squared,     Unit::generic,  true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double newSampleRate, int, SlotMemory&) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            // Above 24 kHz the filter is inaudible; stopping there keeps the solve well away from Nyquist.
            maxCutoffHz = std::min(24000.0f, 0.45f * sampleRate);
            maxG = std::tan(juce::MathConstants<float>::pi * maxCutoffHz / sampleRate);
            reset();
        }

        void reset() noexcept
        {
            left = {};
            right = {};
            primed = false;
        }

        void noteOn(const NoteContext&) noexcept {}

        void process(float* leftSamples, float* rightSamples, int numSamples, const SlotParams& params) noexcept
        {
            const float key = juce::jlimit(0.0f, 1.0f, params.p[keyTrack]);
            const float kTarget = feedbackFor(params.p[resonance]);

            // Past the threshold the saturating stages slow the loop down and the oscillation sinks
            // flat, about 4 % per unit of excess feedback; raise the cutoff by as much to keep it in tune.
            const float tuning = 1.0f + kOscillationTuning * std::max(0.0f, kTarget - 4.0f);
            float hz = params.p[cutoff] * tuning * std::pow(std::max(1.0f, params.frequencyHz) / keyReferenceHz, key);
            hz = juce::jlimit(10.0f, maxCutoffHz, hz);

            const float gTarget = std::tan(juce::MathConstants<float>::pi * hz / sampleRate);
            const auto tap = static_cast<Mode>(juce::jlimit(0, 3, static_cast<int>(std::lround(params.p[mode]))));
            // Only the low-pass taps lose low end to the feedback; band- and high-pass have none to restore.
            const bool lowpass = tap == Mode::lowpass24 || tap == Mode::lowpass12;
            const float bassAmount = lowpass ? juce::jlimit(0.0f, 1.0f, params.p[bass]) : 0.0f;
            const float driveGain = kDriveHeadroom * std::pow(10.0f, juce::jlimit(0.0f, 24.0f, params.p[drive]) * 0.05f);
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

            // Generator 3 shaking the cutoff: octaves of swing for a full-level signal.
            const float shake = juce::jlimit(0.0f, 3.0f, params.p[fm]);
            const float* modulator = shake > 0.0f ? params.modulator : nullptr;
            float gRamp = g;

            // Leak: that share of the input goes round the filter. An analogue signal path lets a little of its
            // oscillators through whatever the cutoff (a Minimoog's faint sizzle under a closed filter), so the
            // top of a note never falls as far as the filter alone would take it.
            const float leakGain = juce::jlimit(0.0f, 0.25f, params.p[leak]);

#if COOLSYNTH_LADDER_SSE
            // Both channels at once: lane 0 left, lane 1 right.
            Lanes lanes = load();
            const __m128 driveGainV = _mm_set1_ps(driveGain);
            const __m128 leakV = _mm_set1_ps(leakGain);
            alignas(16) float out[4] {};
            for (int i = 0; i < numSamples; ++i)
            {
                gRamp += gStep;
                g = modulator != nullptr ? std::min(maxG, gRamp * std::exp2(shake * modulator[i])) : gRamp;
                k += kStep;
                const float inputGain = (1.0f + bassAmount * k) * driveNormalise;
                const float floor = kNoiseFloor * nextNoise();
                const __m128 in = _mm_set_ps(0.0f, 0.0f, rightSamples[i], leftSamples[i]);
                const __m128 x = _mm_add_ps(_mm_mul_ps(fastTanh4(_mm_mul_ps(in, driveGainV)), _mm_set1_ps(inputGain)), _mm_set1_ps(floor));
                _mm_store_ps(out, _mm_add_ps(tick4(lanes, x, tap), _mm_mul_ps(leakV, in)));
                leftSamples[i] = out[0];
                rightSamples[i] = out[1];
            }
            store(lanes);
#else
            for (int i = 0; i < numSamples; ++i)
            {
                gRamp += gStep;
                g = modulator != nullptr ? std::min(maxG, gRamp * std::exp2(shake * modulator[i])) : gRamp;
                k += kStep;
                const float inputGain = (1.0f + bassAmount * k) * driveNormalise;
                const float floor = kNoiseFloor * nextNoise();
                const float inLeft = leftSamples[i], inRight = rightSamples[i];
                leftSamples[i] = tick(left, dsp::fastTanh(inLeft * driveGain) * inputGain + floor, tap) + leakGain * inLeft;
                rightSamples[i] = tick(right, dsp::fastTanh(inRight * driveGain) * inputGain + floor, tap) + leakGain * inRight;
            }
#endif

            g = gTarget;
            k = kTarget;
        }

    private:
        static constexpr float kDriveHeadroom = 0.25f; // drive 0 dB: a full-scale input bends by under 2 %
        static constexpr float kNoiseFloor = 3.0e-6f;  // about -110 dB
        static constexpr float kOscillationTuning = 0.041f;
        float maxG = 1.0f;

        struct Channel
        {
            std::array<float, 4> s {}; // trapezoidal integrator states
            std::array<float, 4> y {}; // last stage outputs: the operating points of the linearisation
        };

        struct Solution
        {
            float u = 0.0f, y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        };

        /** The linear ladder with every pair's transconductance fixed at its value for the given operating point. */
        Solution solve(const Channel& c, float x, float input, float o1, float o2, float o3, float o4) const noexcept
        {
            using dsp::tanhOverX;
            const float t0 = tanhOverX(input);
            const float t1 = tanhOverX(o1);
            const float t2 = tanhOverX(o2);
            const float t3 = tanhOverX(o3);
            const float t4 = tanhOverX(o4);

            // Stage i: y_i = g (t_(i-1) y_(i-1) - t_i y_i) + s_i, so y_i = a_i y_(i-1) + b_i.
            const float d1 = 1.0f / (1.0f + g * t1);
            const float d2 = 1.0f / (1.0f + g * t2);
            const float d3 = 1.0f / (1.0f + g * t3);
            const float d4 = 1.0f / (1.0f + g * t4);
            const float a1 = g * t0 * d1;
            const float a2 = g * t1 * d2;
            const float a3 = g * t2 * d3;
            const float a4 = g * t3 * d4;
            const float b1 = c.s[0] * d1;
            const float b2 = c.s[1] * d2;
            const float b3 = c.s[2] * d3;
            const float b4 = c.s[3] * d4;

            // The four stages in series give y4 = A u + B with u = x - k y4: solve the loop at once.
            const float a = a1 * a2 * a3 * a4;
            const float b = a4 * (a3 * (a2 * b1 + b2) + b3) + b4;
            Solution s;
            s.y4 = (a * x + b) / (1.0f + k * a);
            s.u = x - k * s.y4;
            s.y1 = a1 * s.u + b1;
            s.y2 = a2 * s.y1 + b2;
            s.y3 = a3 * s.y2 + b3;
            return s;
        }

        float tick(Channel& c, float x, Mode tap) const noexcept
        {
            // First pass around the last sample's operating point, second pass around the first
            // pass's answer: close to the fully implicit solution even when the cutoff sits near
            // Nyquist and the state moves a long way in one sample.
            const Solution first = solve(c, x, x - k * c.y[3], c.y[0], c.y[1], c.y[2], c.y[3]);
            const Solution s = solve(c, x, first.u, first.y1, first.y2, first.y3, first.y4);

            c.s[0] = 2.0f * s.y1 - c.s[0];
            c.s[1] = 2.0f * s.y2 - c.s[1];
            c.s[2] = 2.0f * s.y3 - c.s[2];
            c.s[3] = 2.0f * s.y4 - c.s[3];
            c.y = { s.y1, s.y2, s.y3, s.y4 };

            // Band- and high-pass peak 4.3 and 2.4 times higher than the 24 dB tap at resonance;
            // scaling with the feedback keeps every mode at about the same level when it sings.
            switch (tap)
            {
                case Mode::lowpass24:  return s.y4;
                case Mode::lowpass12:  return s.y2;
                case Mode::bandpass:   return 2.0f * (s.y1 - s.y2) / (1.0f + 0.66f * k);
                case Mode::highpass12: return (s.u - 2.0f * s.y1 + s.y2) / (1.0f + 0.28f * k);
            }
            return s.y4;
        }

#if COOLSYNTH_LADDER_SSE
        // ---- the two channels side by side: every function below is its one-channel namesake, operation for operation
        struct Lanes
        {
            std::array<__m128, 4> s, y;
        };

        Lanes load() const noexcept
        {
            Lanes l;
            for (std::size_t j = 0; j < 4; ++j)
            {
                l.s[j] = _mm_set_ps(0.0f, 0.0f, right.s[j], left.s[j]);
                l.y[j] = _mm_set_ps(0.0f, 0.0f, right.y[j], left.y[j]);
            }
            return l;
        }

        void store(const Lanes& l) noexcept
        {
            alignas(16) float v[4] {};
            for (std::size_t j = 0; j < 4; ++j)
            {
                _mm_store_ps(v, l.s[j]);
                left.s[j] = v[0];
                right.s[j] = v[1];
                _mm_store_ps(v, l.y[j]);
                left.y[j] = v[0];
                right.y[j] = v[1];
            }
        }

        static __m128 tanhOverX4(__m128 x) noexcept
        {
            const __m128 x2 = _mm_mul_ps(x, x);
            const __m128 numerator = _mm_add_ps(_mm_set1_ps(135135.0f),
                _mm_mul_ps(x2, _mm_add_ps(_mm_set1_ps(17325.0f), _mm_mul_ps(x2, _mm_add_ps(_mm_set1_ps(378.0f), x2)))));
            const __m128 denominator = _mm_add_ps(_mm_set1_ps(135135.0f),
                _mm_mul_ps(x2, _mm_add_ps(_mm_set1_ps(62370.0f), _mm_mul_ps(x2, _mm_add_ps(_mm_set1_ps(3150.0f), _mm_mul_ps(_mm_set1_ps(28.0f), x2))))));
            const __m128 rational = _mm_div_ps(numerator, denominator);
            const __m128 large = _mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(x2));
            const __m128 useLarge = _mm_cmpge_ps(x2, _mm_set1_ps(24.0f));
            return _mm_or_ps(_mm_and_ps(useLarge, large), _mm_andnot_ps(useLarge, rational));
        }

        static __m128 fastTanh4(__m128 x) noexcept { return _mm_mul_ps(x, tanhOverX4(x)); }

        struct Solution4
        {
            __m128 u, y1, y2, y3, y4;
        };

        Solution4 solve4(const Lanes& c, __m128 x, __m128 input, __m128 o1, __m128 o2, __m128 o3, __m128 o4) const noexcept
        {
            const __m128 gv = _mm_set1_ps(g);
            const __m128 kv = _mm_set1_ps(k);
            const __m128 one = _mm_set1_ps(1.0f);
            const __m128 t0 = tanhOverX4(input);
            const __m128 t1 = tanhOverX4(o1);
            const __m128 t2 = tanhOverX4(o2);
            const __m128 t3 = tanhOverX4(o3);
            const __m128 t4 = tanhOverX4(o4);

            const __m128 d1 = _mm_div_ps(one, _mm_add_ps(one, _mm_mul_ps(gv, t1)));
            const __m128 d2 = _mm_div_ps(one, _mm_add_ps(one, _mm_mul_ps(gv, t2)));
            const __m128 d3 = _mm_div_ps(one, _mm_add_ps(one, _mm_mul_ps(gv, t3)));
            const __m128 d4 = _mm_div_ps(one, _mm_add_ps(one, _mm_mul_ps(gv, t4)));
            const __m128 a1 = _mm_mul_ps(_mm_mul_ps(gv, t0), d1);
            const __m128 a2 = _mm_mul_ps(_mm_mul_ps(gv, t1), d2);
            const __m128 a3 = _mm_mul_ps(_mm_mul_ps(gv, t2), d3);
            const __m128 a4 = _mm_mul_ps(_mm_mul_ps(gv, t3), d4);
            const __m128 b1 = _mm_mul_ps(c.s[0], d1);
            const __m128 b2 = _mm_mul_ps(c.s[1], d2);
            const __m128 b3 = _mm_mul_ps(c.s[2], d3);
            const __m128 b4 = _mm_mul_ps(c.s[3], d4);

            const __m128 a = _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(a1, a2), a3), a4);
            const __m128 b = _mm_add_ps(_mm_mul_ps(a4, _mm_add_ps(_mm_mul_ps(a3, _mm_add_ps(_mm_mul_ps(a2, b1), b2)), b3)), b4);
            Solution4 s;
            s.y4 = _mm_div_ps(_mm_add_ps(_mm_mul_ps(a, x), b), _mm_add_ps(one, _mm_mul_ps(kv, a)));
            s.u = _mm_sub_ps(x, _mm_mul_ps(kv, s.y4));
            s.y1 = _mm_add_ps(_mm_mul_ps(a1, s.u), b1);
            s.y2 = _mm_add_ps(_mm_mul_ps(a2, s.y1), b2);
            s.y3 = _mm_add_ps(_mm_mul_ps(a3, s.y2), b3);
            return s;
        }

        __m128 tick4(Lanes& c, __m128 x, Mode tap) const noexcept
        {
            const __m128 kv = _mm_set1_ps(k);
            const __m128 two = _mm_set1_ps(2.0f);
            const Solution4 first = solve4(c, x, _mm_sub_ps(x, _mm_mul_ps(kv, c.y[3])), c.y[0], c.y[1], c.y[2], c.y[3]);
            const Solution4 s = solve4(c, x, first.u, first.y1, first.y2, first.y3, first.y4);

            c.s[0] = _mm_sub_ps(_mm_mul_ps(two, s.y1), c.s[0]);
            c.s[1] = _mm_sub_ps(_mm_mul_ps(two, s.y2), c.s[1]);
            c.s[2] = _mm_sub_ps(_mm_mul_ps(two, s.y3), c.s[2]);
            c.s[3] = _mm_sub_ps(_mm_mul_ps(two, s.y4), c.s[3]);
            c.y = { s.y1, s.y2, s.y3, s.y4 };

            switch (tap)
            {
                case Mode::lowpass24:  return s.y4;
                case Mode::lowpass12:  return s.y2;
                case Mode::bandpass:
                    return _mm_div_ps(_mm_mul_ps(two, _mm_sub_ps(s.y1, s.y2)), _mm_add_ps(_mm_set1_ps(1.0f), _mm_mul_ps(_mm_set1_ps(0.66f), kv)));
                case Mode::highpass12:
                    return _mm_div_ps(_mm_add_ps(_mm_sub_ps(s.u, _mm_mul_ps(two, s.y1)), s.y2),
                                      _mm_add_ps(_mm_set1_ps(1.0f), _mm_mul_ps(_mm_set1_ps(0.28f), kv)));
            }
            return s.y4;
        }
#endif

        float nextNoise() noexcept
        {
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            return static_cast<float>(noise >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        float sampleRate = 48000.0f;
        float maxCutoffHz = 21600.0f;
        float g = 0.1f;
        float k = 0.0f;
        bool primed = false;
        Channel left, right;
        std::uint32_t noise = 0x9E3779B9u;
    };

    static_assert(Module<LadderProcessor>);
}
