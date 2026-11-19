#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#if defined(_M_X64) || defined(__SSE2__)
 #include <emmintrin.h>
 #define COOLSYNTH_VA_SSE 1
#endif

#include "DspMath.h"
#include "Module.h"

/*
    V3 virtual-analogue generator (docs/design/DESIGN_V3.md section 3.1).

    Shape morphs continuously through sine, triangle, saw and pulse; pulse width shapes the pulse.
    Every discontinuity is located exactly in time on the rendered phase trajectory and corrected
    with a 4-point band-limited step (BLEP) or ramp (BLAMP) built from the integrated cubic
    B-spline (Valimaki, Pekonen and Nam, JASA 2012). Its kernel falls as sinc^4 instead of the
    sinc^2 of the common 2-point polyBLEP, so aliases sit far lower. The correction reaches two
    samples back, so the output runs two samples late.

    The same event machinery handles hard sync, with the exact reset time taken from the previous
    generator's wrap track and the step and slope change corrected on both sides, and
    phase-modulation FM, where crossings are found on the modulated trajectory.

    Unison: 1 to 7 copies on a JP-8000-style detune curve (crowded towards the centre, the upper
    side 2 % narrower so the beating never repeats), a centre-versus-sides mix, power-normalised,
    spread across the stereo field. Start phases are random per note by an adjustable amount.
    A sine sub one octave down. The voice applies the slot level; the generator renders at unit
    level and hands its mono signal and the wrap track of an undetuned reference phase onward.
*/
namespace coolsynth::synth::v3
{
    class VaGenerator final
    {
    public:
        static constexpr int maxUnison = 7;
        static constexpr int latencySamples = generatorLatencySamples;

        enum Param : int { shape = 0, pulseWidth, unison, detune, spread, mix, randomPhase, sub, paramCount };

        /** Integer positions of the Shape parameter. */
        enum class Wave : int { sine = 0, triangle, saw, pulse };

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "VA", 1,
                {{ { "shape",  "Shape",       0.0f,  3.0f,   2.0f,  Skew::linear,  Unit::generic, true, "Sine|Triangle|Saw|Pulse" },
                   { "pw",     "Pulse Width", 0.05f, 0.95f,  0.5f,  Skew::linear,  Unit::percent, true },
                   { "unison", "Unison",      1.0f,  7.0f,   1.0f,  Skew::linear,  Unit::choice,  false },
                   { "detune", "Detune",      0.0f,  200.0f, 20.0f, Skew::squared, Unit::cents,   true },
                   { "spread", "Spread",      0.0f,  1.0f,   0.6f,  Skew::linear,  Unit::percent, true },
                   { "mix",    "Mix",         0.0f,  1.0f,   0.67f, Skew::linear,  Unit::percent, true },
                   { "rand",   "Phase Rand",  0.0f,  1.0f,   1.0f,  Skew::linear,  Unit::percent, false },
                   { "sub",    "Sub",         0.0f,  1.0f,   0.0f,  Skew::linear,  Unit::percent, true } }},
                paramCount };
            return d;
        }

        /** The mono signal and the wrap track it hands the next generator, one float per sample each. */
        static std::size_t memoryFloats(double, int maxSubBlock)
        {
            return 2 * static_cast<std::size_t>(std::max(1, maxSubBlock));
        }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            wrapTrack = memory.take(n);
            reset();
        }

        void reset() noexcept
        {
            phases.fill(0.0f);
            referencePhase = 0.0f;
            subPhase = 0.0f;
            fmPrevious = 0.0f;
            accLeft.fill(0.0f);
            accRight.fill(0.0f);
            accMono.fill(0.0f);
            wrapRing.fill(-1.0f);
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            input = {};
            phasesPending = false;
        }

        /** Start phases are drawn on the first process() after the note, with that sub-block's Phase Rand. */
        void noteOn(const NoteContext&) noexcept { phasesPending = true; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), wrapTrack.data() }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const Shape waveform = makeShape(params.p[shape], params.p[pulseWidth]);
            const int count = juce::jlimit(1, maxUnison, static_cast<int>(std::lround(params.p[unison])));

            if (phasesPending)
            {
                drawPhases(juce::jlimit(0.0f, 1.0f, params.p[randomPhase]));
                phasesPending = false;
            }

            std::array<float, maxUnison> increments {};
            std::array<CopyGains, maxUnison> gains {};
            configureCopies(count, params, increments, gains);

            const float referenceIncrement = juce::jlimit(0.0f, 0.45f, params.frequencyHz / sampleRate);
            const float subIncrement = 0.5f * referenceIncrement;
            const float subLevel = juce::jlimit(0.0f, 1.0f, params.p[sub]);
            const CopyGains subGains = panGains(params.pan, subLevel);

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useSync = params.combine == Combine::sync && input.wraps != nullptr;
            const bool useFm = params.combine == Combine::fm && input.signal != nullptr && amount > 0.0f;
            const bool useRing = params.combine == Combine::ring;
            const bool useAm = params.combine == Combine::am;
            const float fmDepthCycles = kMaxFmCycles * amount * amount;

#if COOLSYNTH_VA_SSE
            // Unison without sync, FM or sine: the copies four at a time (D219). Every copy's own numbers are as in
            // the loop below; only their sum is taken in another order, a difference near -140 dB. A sine, sync,
            // FM or a single copy keep the loop.
            const bool vectorCopies = ! useSync && ! useFm && waveform.sine == 0.0f && count > 1;
            CopyLanes lanes {};
            if (vectorCopies)
                lanes = prepareLanes(count, increments, gains, waveform);
#endif

            for (int s = 0; s < numSamples; ++s)
            {
                const auto sample = static_cast<std::size_t>(s);
                const float modulator = input.signal != nullptr ? input.signal[s] : 0.0f;
                const float syncTau = useSync ? input.wraps[s] : -1.0f;

                float fmDelta = 0.0f;
                if (useFm)
                {
                    const float fmNow = fmDepthCycles * modulator;
                    fmDelta = fmNow - fmPrevious;
                    fmPrevious = fmNow;
                }

                const unsigned int now = cursor & 3u;
                wrapRing[now] = advanceReference(referenceIncrement, fmDelta, syncTau);
                // The sample being built, in registers while every copy adds to it: the same additions in the same
                // order as into the ring itself, without a store and a load between one copy and the next (TODO 9).
                Sum here { accLeft[now], accRight[now], accMono[now] };

#if COOLSYNTH_VA_SSE
                if (vectorCopies)
                    addCopiesSideBySide(lanes, waveform, count, increments, gains, here);
                else
#endif
                for (int i = 0; i < count; ++i)
                {
                    const auto index = static_cast<std::size_t>(i);
                    const CopyGains& g = gains[index];
                    float& phase = phases[index];
                    const float dt = increments[index];

                    if (g.mono == 0.0f)
                    {
                        // Silent in this mix: keep it running so it is in place when the mix brings it in.
                        phase = syncTau > 0.0f ? dt * (1.0f - syncTau) : (useFm ? wrap(phase + dt + fmDelta) : wrapForward(phase + dt));
                        continue;
                    }

                    if (syncTau > 0.0f)
                    {
                        // Runs on up to the reset at tau, jumps to phase zero, runs on to the sample.
                        const float before = dt * syncTau;
                        addCrossings(waveform, phase, before, 0.0f, syncTau, g, here);
                        const float resetFrom = wrap(phase + before);
                        addEvent(syncTau,
                                 waveform.value(0.0f) - waveform.value(resetFrom),
                                 dt * (waveform.slope(0.0f) - waveform.slope(resetFrom)),
                                 g, here);
                        const float after = dt * (1.0f - syncTau);
                        addCrossings(waveform, 0.0f, after, syncTau, 1.0f - syncTau, g, here);
                        phase = after;
                    }
                    else
                    {
                        const float delta = dt + fmDelta;
                        if (std::abs(delta) < 1.0f)
                            addCrossings(waveform, phase, delta, 0.0f, 1.0f, g, here);
                        phase = useFm ? wrap(phase + delta) : wrapForward(phase + delta);
                    }

                    const float v = waveform.value(phase);
                    here.left += v * g.left;
                    here.right += v * g.right;
                    here.mono += v * g.mono;
                }

                subPhase = wrapForward(subPhase + subIncrement);
                if (subLevel > 0.0f)
                {
                    const float v = std::sin(subPhase * kTwoPi);
                    here.left += v * subGains.left;
                    here.right += v * subGains.right;
                    here.mono += v * subGains.mono;
                }
                accLeft[now] = here.left;
                accRight[now] = here.right;
                accMono[now] = here.mono;

                // The sample two steps back can no longer change: hand it out.
                const unsigned int out = (cursor + 2u) & 3u;
                float outLeft = accLeft[out];
                float outRight = accRight[out];
                float outMono = accMono[out];
                const float outWrap = wrapRing[out];
                accLeft[out] = 0.0f;
                accRight[out] = 0.0f;
                accMono[out] = 0.0f;
                wrapRing[out] = -1.0f;
                ++cursor;

                if (useRing)
                {
                    const float k = 1.0f - amount + amount * modulator;
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }
                else if (useAm)
                {
                    const float k = 1.0f - 0.5f * amount * (1.0f - modulator);
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }

                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[sample] = outMono;
                wrapTrack[sample] = outWrap;
            }

#if COOLSYNTH_VA_SSE
            if (vectorCopies)
            {
                alignas(16) float phase[8];
                _mm_store_ps(phase, lanes.phase[0]);
                _mm_store_ps(phase + 4, lanes.phase[1]);
                for (int i = 0; i < count; ++i) // the copies not playing keep their phases, as in the loop
                    phases[static_cast<std::size_t>(i)] = phase[i];
            }
#endif
        }

    private:
        static constexpr float kTwoPi = 6.28318530717958647692f;
        static constexpr float kMaxFmCycles = 2.0f; // phase deviation at full combine amount

        struct CopyGains
        {
            float left = 0.0f;
            float right = 0.0f;
            float mono = 0.0f;
        };


        struct Sum
        {
            float left = 0.0f;
            float right = 0.0f;
            float mono = 0.0f;
        };

        struct Point
        {
            float phase = 0.0f;
            float jump = 0.0f;      // value step crossing the point in the direction of rising phase
            float slopeJump = 0.0f; // slope step (per cycle) crossing it in that direction
        };

        /** A weighted blend of two adjacent waves, its values and the points where it breaks. */
        struct Shape
        {
            float sine = 0.0f, triangle = 0.0f, saw = 0.0f, pulse = 0.0f;
            float width = 0.5f;
            std::array<Point, 5> points {};
            int pointCount = 0;

            [[nodiscard]] float value(float p) const noexcept
            {
                float v = 0.0f;
                if (sine != 0.0f)
                    v += sine * std::sin(p * kTwoPi);
                if (triangle != 0.0f)
                    v += triangle * (p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f));
                if (saw != 0.0f)
                    v += saw * (p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f);
                if (pulse != 0.0f)
                    v += pulse * (p < width ? 2.0f * (1.0f - width) : -2.0f * width);
                return v;
            }

            [[nodiscard]] float slope(float p) const noexcept
            {
                float d = 0.0f;
                if (sine != 0.0f)
                    d += sine * kTwoPi * std::cos(p * kTwoPi);
                if (triangle != 0.0f)
                    d += triangle * ((p < 0.25f || p >= 0.75f) ? 4.0f : -4.0f);
                if (saw != 0.0f)
                    d += saw * 2.0f;
                return d;
            }
        };

        // All four waves start at zero, rising, with the fundamental in phase, so a morph never cancels it.
        static Shape makeShape(float position, float pulseWidth) noexcept
        {
            Shape s;
            const float x = juce::jlimit(0.0f, 3.0f, position);
            const int lower = std::min(2, static_cast<int>(x));
            const float upper = x - static_cast<float>(lower);
            std::array<float, 4> w {};
            w[static_cast<std::size_t>(lower)] = 1.0f - upper;
            w[static_cast<std::size_t>(lower + 1)] = upper;
            s.sine = w[0];
            s.triangle = w[1];
            s.saw = w[2];
            s.pulse = w[3];
            s.width = juce::jlimit(0.05f, 0.95f, pulseWidth);

            auto add = [&s](float phase, float jump, float slopeJump)
            {
                s.points[static_cast<std::size_t>(s.pointCount)] = { phase, jump, slopeJump };
                ++s.pointCount;
            };
            if (s.pulse != 0.0f)
            {
                add(0.0f, 2.0f * s.pulse, 0.0f);
                add(s.width, -2.0f * s.pulse, 0.0f);
            }
            if (s.triangle != 0.0f)
            {
                add(0.25f, 0.0f, -8.0f * s.triangle);
                add(0.75f, 0.0f, 8.0f * s.triangle);
            }
            if (s.saw != 0.0f)
                add(0.5f, -2.0f * s.saw, 0.0f);
            return s;
        }

#if COOLSYNTH_VA_SSE
        // ---- Unison copies four at a time (D219): copies 0-3 in the first register, 4-6 in the second, an eighth
        // lane silent. Each copy's phase, crossing test, value and products are its loop's, operation for operation.
        struct CopyLanes
        {
            std::array<__m128, 2> phase, increment, gainLeft, gainRight, gainMono;
            __m128 triangle, saw, pulse, width, pulseHigh, pulseLow;
        };

        CopyLanes prepareLanes(int count, const std::array<float, maxUnison>& increments,
                               const std::array<CopyGains, maxUnison>& gains, const Shape& w) const noexcept
        {
            alignas(16) float p[8] {}, d[8] {}, l[8] {}, r[8] {}, m[8] {};
            for (int i = 0; i < count; ++i)
            {
                const auto index = static_cast<std::size_t>(i);
                p[i] = phases[index];
                d[i] = increments[index];
                l[i] = gains[index].left;
                r[i] = gains[index].right;
                m[i] = gains[index].mono;
            }
            CopyLanes lanes {};
            for (std::size_t h = 0; h < 2; ++h)
            {
                lanes.phase[h] = _mm_load_ps(p + 4 * h);
                lanes.increment[h] = _mm_load_ps(d + 4 * h);
                lanes.gainLeft[h] = _mm_load_ps(l + 4 * h);
                lanes.gainRight[h] = _mm_load_ps(r + 4 * h);
                lanes.gainMono[h] = _mm_load_ps(m + 4 * h);
            }
            lanes.triangle = _mm_set1_ps(w.triangle);
            lanes.saw = _mm_set1_ps(w.saw);
            lanes.pulse = _mm_set1_ps(w.pulse);
            lanes.width = _mm_set1_ps(w.width);
            lanes.pulseHigh = _mm_set1_ps(2.0f * (1.0f - w.width));
            lanes.pulseLow = _mm_set1_ps(-2.0f * w.width);
            return lanes;
        }

        static __m128 select(__m128 mask, __m128 whenTrue, __m128 whenFalse) noexcept
        {
            return _mm_or_ps(_mm_and_ps(mask, whenTrue), _mm_andnot_ps(mask, whenFalse));
        }

        /** Shape::value for four phases, without the sine: the same expressions, chosen per lane. */
        static __m128 value4(const CopyLanes& lanes, const Shape& w, __m128 p) noexcept
        {
            __m128 v = _mm_setzero_ps();
            if (w.triangle != 0.0f)
            {
                const __m128 fourP = _mm_mul_ps(_mm_set1_ps(4.0f), p);
                const __m128 rising = fourP;
                const __m128 falling = _mm_sub_ps(_mm_set1_ps(2.0f), fourP);
                const __m128 risingAgain = _mm_sub_ps(fourP, _mm_set1_ps(4.0f));
                const __m128 term = select(_mm_cmplt_ps(p, _mm_set1_ps(0.25f)), rising,
                                           select(_mm_cmplt_ps(p, _mm_set1_ps(0.75f)), falling, risingAgain));
                v = _mm_add_ps(v, _mm_mul_ps(lanes.triangle, term));
            }
            if (w.saw != 0.0f)
            {
                const __m128 twoP = _mm_mul_ps(_mm_set1_ps(2.0f), p);
                const __m128 term = select(_mm_cmplt_ps(p, _mm_set1_ps(0.5f)), twoP, _mm_sub_ps(twoP, _mm_set1_ps(2.0f)));
                v = _mm_add_ps(v, _mm_mul_ps(lanes.saw, term));
            }
            if (w.pulse != 0.0f)
                v = _mm_add_ps(v, _mm_mul_ps(lanes.pulse, select(_mm_cmplt_ps(p, lanes.width), lanes.pulseHigh, lanes.pulseLow)));
            return v;
        }

        static float sumOf(__m128 v) noexcept
        {
            const __m128 pairs = _mm_add_ps(v, _mm_movehl_ps(v, v));
            return _mm_cvtss_f32(_mm_add_ss(pairs, _mm_shuffle_ps(pairs, pairs, 1)));
        }

        void addCopiesSideBySide(CopyLanes& lanes, const Shape& w, int count, const std::array<float, maxUnison>& increments,
                                 const std::array<CopyGains, maxUnison>& gains, Sum& here) noexcept
        {
            const __m128 one = _mm_set1_ps(1.0f);
            std::array<__m128, 2> end {};
            int crossing = 0;
            for (std::size_t h = 0; h < 2; ++h)
            {
                end[h] = _mm_add_ps(lanes.phase[h], lanes.increment[h]);
                // crosses(): the point, a cycle on if it lies at or behind the phase, reached by the end. A silent lane
                // (no increment) never reaches it.
                for (int k = 0; k < w.pointCount; ++k)
                {
                    const __m128 point = _mm_set1_ps(w.points[static_cast<std::size_t>(k)].phase);
                    const __m128 q = _mm_add_ps(point, _mm_and_ps(_mm_cmple_ps(point, lanes.phase[h]), one));
                    crossing |= _mm_movemask_ps(_mm_cmple_ps(q, end[h])) << (4 * static_cast<int>(h));
                }
            }
            if (crossing != 0)
            {
                // About once a cycle per copy: those copies' crossings by the loop's own code, in copy order.
                alignas(16) float start[8];
                _mm_store_ps(start, lanes.phase[0]);
                _mm_store_ps(start + 4, lanes.phase[1]);
                for (int i = 0; i < count; ++i)
                {
                    const auto index = static_cast<std::size_t>(i);
                    if (((crossing >> i) & 1) != 0 && gains[index].mono != 0.0f)
                        addCrossings(w, start[i], increments[index], 0.0f, 1.0f, gains[index], here);
                }
            }
            __m128 sumLeft = _mm_setzero_ps(), sumRight = _mm_setzero_ps(), sumMono = _mm_setzero_ps();
            for (std::size_t h = 0; h < 2; ++h)
            {
                // wrapForward(): one cycle off once the phase reaches 1.
                lanes.phase[h] = _mm_sub_ps(end[h], _mm_and_ps(_mm_cmpge_ps(end[h], one), one));
                const __m128 v = value4(lanes, w, lanes.phase[h]);
                sumLeft = _mm_add_ps(sumLeft, _mm_mul_ps(v, lanes.gainLeft[h]));
                sumRight = _mm_add_ps(sumRight, _mm_mul_ps(v, lanes.gainRight[h]));
                sumMono = _mm_add_ps(sumMono, _mm_mul_ps(v, lanes.gainMono[h]));
            }
            here.left += sumOf(sumLeft);
            here.right += sumOf(sumRight);
            here.mono += sumOf(sumMono);
        }
#endif

        static float wrap(float x) noexcept { return x - std::floor(x); }

        /** wrap() for a phase in [0, 1] moved forward by under a cycle, so x is in [0, 2): the same number
            (floor is 0 or 1 there), without the library call std::floor is on SSE2 (TODO 9). */
        static float wrapForward(float x) noexcept { return x >= 1.0f ? x - 1.0f : x; }

        /**
            Whether moving from start by delta crosses point; fraction is where, as a share of delta.
            The test compares against start + delta, the very expression the phase update wraps, so a
            phase that lands exactly on a point is counted as crossed exactly when value() already
            returns the far side of it. Without that, a sync reset that coincides with the saw's own
            jump could count the jump and then take the reset step from the near side as well.
        */
        static bool crosses(float start, float delta, float point, float& fraction) noexcept
        {
            const float end = start + delta;
            if (delta > 0.0f)
            {
                const float q = point <= start ? point + 1.0f : point;
                if (q > end)
                    return false;
                fraction = (q - start) / delta;
                return true;
            }
            if (delta < 0.0f)
            {
                // Sitting on a point is its upper side, so moving down crosses it at once.
                const float q = point > start ? point - 1.0f : point;
                if (q <= end)
                    return false;
                fraction = (start - q) / -delta;
                return true;
            }
            return false;
        }

        // Integral of the cubic B-spline from -2 to t, for t in [-2, 0].
        /** Band-limited unit step minus the ideal step, x samples from the step, zero outside (-2, 2). */
        static float stepResidual(float x) noexcept { return dsp::blepResidual(x); }

        /** Band-limited unit-slope corner minus the ideal corner: the integral of stepResidual. Even in x. */
        static float rampResidual(float x) noexcept
        {
            const float t = -std::abs(x);
            if (t <= -2.0f)
                return 0.0f;
            if (t <= -1.0f)
            {
                const float u = t + 2.0f;
                return u * u * u * u * u * (1.0f / 120.0f);
            }
            return 1.0f / 120.0f + (t + 1.0f) * (1.0f / 24.0f)
                   + (1.1f + t * (2.75f + t * (2.0f + t * t * (-0.5f - 0.15f * t)))) * (1.0f / 6.0f);
        }

        /** An event at tau in (0, 1] after the previous sample: corrects the two samples before and after it. */
        void addEvent(float tau, float jump, float slopeChange, const CopyGains& g, Sum& here) noexcept
        {
            for (unsigned int j = 0; j < 4u; ++j)
            {
                const float x = static_cast<float>(static_cast<int>(j) - 1) - tau;
                const float c = jump * stepResidual(x) + slopeChange * rampResidual(x);
                if (j == 2u) // (cursor + 4) & 3: the sample being built, held in `here`
                {
                    here.left += c * g.left;
                    here.right += c * g.right;
                    here.mono += c * g.mono;
                    continue;
                }
                const unsigned int slot = (cursor + 2u + j) & 3u;
                accLeft[slot] += c * g.left;
                accRight[slot] += c * g.right;
                accMono[slot] += c * g.mono;
            }
        }

        /** Events of the shape between start and start + delta, which spans [tauOffset, tauOffset + tauScale] of the sample. */
        void addCrossings(const Shape& w, float start, float delta, float tauOffset, float tauScale, const CopyGains& g, Sum& here) noexcept
        {
            if (delta == 0.0f || tauScale <= 0.0f)
                return;
            for (int k = 0; k < w.pointCount; ++k)
            {
                const auto& point = w.points[static_cast<std::size_t>(k)];
                float fraction = 0.0f;
                if (crosses(start, delta, point.phase, fraction))
                {
                    // Worked out only for a crossing, about once a cycle, not every sample (TODO 9).
                    const float speed = std::abs(delta) / tauScale;
                    const float direction = delta > 0.0f ? 1.0f : -1.0f;
                    addEvent(tauOffset + tauScale * fraction, direction * point.jump, speed * point.slopeJump, g, here);
                }
            }
        }

        /** Advances the undetuned reference phase; returns its wrap time in this sample, or -1. */
        float advanceReference(float increment, float fmDelta, float syncTau) noexcept
        {
            float fraction = 0.0f;
            if (syncTau > 0.0f)
            {
                const float tau = crosses(referencePhase, increment * syncTau, 0.0f, fraction) ? fraction * syncTau : syncTau;
                referencePhase = increment * (1.0f - syncTau);
                return tau;
            }

            const float delta = increment + fmDelta;
            float tau = -1.0f;
            if (delta > 0.0f && delta < 1.0f && crosses(referencePhase, delta, 0.0f, fraction))
                tau = fraction;
            referencePhase = fmDelta == 0.0f && delta >= 0.0f ? wrapForward(referencePhase + delta) : wrap(referencePhase + delta);
            return tau;
        }

        static CopyGains panGains(float pan, float level) noexcept
        {
            const float angle = (juce::jlimit(-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            return { std::cos(angle) * level, std::sin(angle) * level, level };
        }

        void configureCopies(int count,
                             const SlotParams& params,
                             std::array<float, maxUnison>& increments,
                             std::array<CopyGains, maxUnison>& gains) const noexcept
        {
            const float detuneCents = juce::jlimit(0.0f, 200.0f, params.p[detune]);
            const float spreadAmount = juce::jlimit(0.0f, 1.0f, params.p[spread]);
            const float mixAmount = juce::jlimit(0.0f, 1.0f, params.p[mix]);

            float sumSquares = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const auto index = static_cast<std::size_t>(i);
                const float u = count > 1 ? -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
                const float position = u >= 0.0f ? 0.98f * u * std::sqrt(u) : u * std::sqrt(-u);
                const float hz = params.frequencyHz * std::exp2(detuneCents * position / 1200.0f);
                increments[index] = juce::jlimit(0.0f, 0.45f, hz / sampleRate);

                const bool centre = (count % 2 == 1) ? (i == count / 2) : (i == count / 2 - 1 || i == count / 2);
                const float level = count == 1 ? 1.0f : (centre ? 1.0f - 0.5f * mixAmount : mixAmount);
                gains[index] = panGains(params.pan + spreadAmount * u, level);
                sumSquares += level * level;
            }

            const float norm = sumSquares > 0.0f ? 1.0f / std::sqrt(sumSquares) : 0.0f;
            for (int i = 0; i < count; ++i)
            {
                auto& g = gains[static_cast<std::size_t>(i)];
                g.left *= norm;
                g.right *= norm;
                g.mono *= norm;
            }
        }

        void drawPhases(float amount) noexcept
        {
            for (auto& phase : phases)
                phase = amount * nextUniform();
            referencePhase = 0.0f;
            subPhase = 0.0f;
            fmPrevious = 0.0f;
        }

        float nextUniform() noexcept
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            return static_cast<float>(random >> 8) * (1.0f / 16777216.0f);
        }

        float sampleRate = 48000.0f;
        std::array<float, maxUnison> phases {};
        float referencePhase = 0.0f;
        float subPhase = 0.0f;
        float fmPrevious = 0.0f;
        std::array<float, 4> accLeft {}, accRight {}, accMono {}, wrapRing {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        std::span<float> wrapTrack;
        GeneratorLink input {};
        std::uint32_t random = 0x2545F491u;
        bool phasesPending = false;
    };

    static_assert(Module<VaGenerator>);
}
