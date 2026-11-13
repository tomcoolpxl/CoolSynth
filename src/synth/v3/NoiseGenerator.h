#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    V3 noise generator (DESIGN_V3.md section 3.1, REQUIREMENTS_V3.md: white, pink, band-passed
    with pitch tracking, LFSR periodic with clock divider).

    White: uniform noise. Pink: white through seven first-order shelves with poles every two
    octaves from 10 Hz and zeros an octave above each, designed at the running sample rate (so
    the slope stays -3 dB per octave at 96 kHz, where fixed coefficient sets drift). Band: white
    through a band-pass state-variable filter centred on the note (pitch tracking, the slot's
    octave, semitone and fine move it), Resonance from wide to a whistle, its gain set so the
    level stays even across notes and resonance. Chip: the NES noise channel's 15-bit shift
    register, feedback from bit 1 (Long, 32767 steps) or bit 6 (Short, 93 steps, a pitched buzz),
    clocked at 93 times the note frequency so a Short register plays the note; Clock moves the
    clock in semitones, like the NES period divider. Every step of the register is band-limited
    with the same 4-point BLEP as the VA generator, so even a fast clock does not alias.

    Tone is a gentle low-pass from fully open down to 100 Hz. Width decorrelates left and right
    (white, pink and band each run a second, independent noise for the right side). Levels are
    matched to a saw at the same slot level within a few dB after the voice's decimation. The
    output runs generatorLatencySamples late like every generator. Ring and AM combine with the
    previous generator; the generator hands its mono signal on and has no wrap track to sync to.
*/
namespace coolsynth::synth::v3
{
    class NoiseGenerator final
    {
    public:
        enum Param : int { type = 0, tone, resonance, chipMode, clock, width, paramCount };
        enum class Kind : int { white = 0, pink, band, chip };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr int pinkSections = 7;
        static constexpr int shortPeriodSteps = 93;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Noise", 1,
                {{ { "type",  "Type",      0.0f,  3.0f,  0.0f, Skew::linear, Unit::choice,    false, "White|Pink|Band|Chip" },
                   { "tone",  "Tone",      0.0f,  1.0f,  1.0f, Skew::linear, Unit::percent,   true },
                   { "res",   "Resonance", 0.0f,  1.0f,  0.3f, Skew::linear, Unit::percent,   true },
                   { "chip",  "Chip Mode", 0.0f,  1.0f,  0.0f, Skew::linear, Unit::choice,    false, "Long|Short" },
                   { "clock", "Clock",     -48.0f, 48.0f, 0.0f, Skew::linear, Unit::semitones, true },
                   { "width", "Width",     0.0f,  1.0f,  0.5f, Skew::linear, Unit::percent,   true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            designPink();

            // Each instance its own noise: voices that share a sequence would sum coherently.
            static std::atomic<std::uint32_t> instances { 0 };
            const std::uint32_t n = instances.fetch_add(1, std::memory_order_relaxed) + 1u;
            rngLeft = n * 2654435761u ^ 0xA341316Cu;
            rngRight = n * 2246822519u ^ 0xC8013EA4u;
            if (rngLeft == 0)
                rngLeft = 1;
            if (rngRight == 0)
                rngRight = 1;
            reset();
        }

        /** Clears filters and the output delay; the noise itself runs on, so every note differs. */
        void reset() noexcept
        {
            pinkLeft = {};
            pinkRight = {};
            bandLeft = {};
            bandRight = {};
            toneLeft = toneRight = toneMono = 0.0f;
            accLeft.fill(0.0f);
            accRight.fill(0.0f);
            accMono.fill(0.0f);
            cursor = 0;
            clockPhase = 0.0;
            if (shift == 0)
                shift = 1;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            input = {};
        }

        void noteOn(const NoteContext&) noexcept {}
        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const auto kind = static_cast<Kind>(juce::jlimit(0, 3, static_cast<int>(std::lround(params.p[type]))));
            const float toneAmount = juce::jlimit(0.0f, 1.0f, params.p[tone]);
            const float widthAmount = juce::jlimit(0.0f, 1.0f, params.p[width]);
            const bool stereo = widthAmount > 0.0f && kind != Kind::chip;
            const float angle = widthAmount * 0.5f * juce::MathConstants<float>::pi;
            const float sameSide = std::cos(angle), otherSide = std::sin(angle);

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);

            // Tone: a one-pole low-pass, open at the top of the knob.
            const bool toneOn = toneAmount < 0.999f;
            const double toneHz = std::min(0.45 * sampleRate, 100.0 * std::pow(200.0, static_cast<double>(toneAmount)));
            const float toneG = static_cast<float>(std::tan(juce::MathConstants<double>::pi * toneHz / sampleRate));
            const float toneCoefficient = toneG / (1.0f + toneG);

            // Band: a state-variable band-pass on the note.
            BandCoefficients band {};
            if (kind == Kind::band)
                band = bandCoefficients(params.frequencyHz, juce::jlimit(0.0f, 1.0f, params.p[resonance]));

            // Chip: the register's clock.
            const bool shortMode = params.p[chipMode] >= 0.5f;
            const double clockHz = static_cast<double>(std::max(1.0f, params.frequencyHz)) * shortPeriodSteps
                                   * std::exp2(static_cast<double>(juce::jlimit(-48.0f, 48.0f, params.p[clock])) / 12.0);
            const double clockStep = std::min(kMaxTicksPerSample, clockHz / sampleRate);

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                const unsigned int now = cursor & 3u;
                float a = 0.0f, b = 0.0f;
                switch (kind)
                {
                    case Kind::white:
                        a = kWhiteLevel * nextUniform(rngLeft);
                        b = stereo ? kWhiteLevel * nextUniform(rngRight) : a;
                        break;
                    case Kind::pink:
                        a = pinkGain * runPink(pinkLeft, nextUniform(rngLeft));
                        b = stereo ? pinkGain * runPink(pinkRight, nextUniform(rngRight)) : a;
                        break;
                    case Kind::band:
                        a = runBand(bandLeft, band, nextUniform(rngLeft));
                        b = stereo ? runBand(bandRight, band, nextUniform(rngRight)) : a;
                        break;
                    case Kind::chip:
                        a = b = runChip(clockStep, shortMode, gainLeft, gainRight);
                        break;
                }

                const float rightMix = stereo ? sameSide * a + otherSide * b : a;
                accLeft[now] += a * gainLeft;
                accRight[now] += rightMix * gainRight;
                accMono[now] += a;

                // The sample two steps back can no longer change: hand it out.
                const unsigned int out = (cursor + 2u) & 3u;
                float outLeft = accLeft[out], outRight = accRight[out], outMono = accMono[out];
                accLeft[out] = accRight[out] = accMono[out] = 0.0f;
                ++cursor;

                if (toneOn)
                {
                    outLeft = onePole(toneLeft, outLeft, toneCoefficient);
                    outRight = onePole(toneRight, outRight, toneCoefficient);
                    outMono = onePole(toneMono, outMono, toneCoefficient);
                }

                if (useRing || useAm)
                {
                    const float modulator = input.signal[s];
                    const float k = useRing ? 1.0f - amount + amount * modulator : 1.0f - 0.5f * amount * (1.0f - modulator);
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }

                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[static_cast<std::size_t>(s)] = outMono;
            }
        }

        /** The register's state, for tests. */
        [[nodiscard]] std::uint32_t shiftForTesting() const noexcept { return shift; }

        /** Steps the register once as the generator does; for tests. */
        static std::uint32_t stepShift(std::uint32_t value, bool shortMode) noexcept
        {
            const std::uint32_t feedback = (value ^ (value >> (shortMode ? 6u : 1u))) & 1u;
            return (value >> 1u) | (feedback << 14u);
        }

    private:
        static constexpr float kWhiteLevel = 1.0f;     // uniform -1..1: rms 0.58, a saw's
        static constexpr float kChipLevel = 0.5f;      // the register's output is +-0.5
        static constexpr double kMaxTicksPerSample = 8.0;
        static constexpr float kPinkLevel = 1.68f;     // matches white after the voice's decimation

        struct PinkState
        {
            std::array<float, pinkSections> x {}, y {};
        };

        struct BandState
        {
            float ic1 = 0.0f, ic2 = 0.0f;
        };

        struct BandCoefficients
        {
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f, gain = 1.0f;
        };

        /** Trapezoidal one-pole low-pass; coefficient is g / (1 + g). */
        static float onePole(float& state, float x, float coefficient) noexcept
        {
            const float v = (x - state) * coefficient;
            const float y = v + state;
            state = y + v;
            return y;
        }

        static float nextUniform(std::uint32_t& state) noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        /** Seven shelves at the running rate; the gain is set so the shelf stack passes 1 kHz at unity. */
        void designPink() noexcept
        {
            const double fs = sampleRate;
            std::complex<double> at1k { 1.0, 0.0 };
            const std::complex<double> z = std::polar(1.0, 2.0 * juce::MathConstants<double>::pi * 1000.0 / fs);
            for (int i = 0; i < pinkSections; ++i)
            {
                const double pole = std::min(0.45 * fs, 10.0 * std::pow(4.0, i));
                const double zero = std::min(0.47 * fs, 2.0 * pole);
                const double wp = 2.0 * fs * std::tan(juce::MathConstants<double>::pi * pole / fs);
                const double wz = 2.0 * fs * std::tan(juce::MathConstants<double>::pi * zero / fs);
                const double a0 = 2.0 * fs + wp;
                auto& c = pinkCoefficients[static_cast<std::size_t>(i)];
                c.b0 = static_cast<float>((2.0 * fs + wz) / a0);
                c.b1 = static_cast<float>((wz - 2.0 * fs) / a0);
                c.a1 = static_cast<float>((wp - 2.0 * fs) / a0);
                at1k *= (static_cast<double>(c.b0) + static_cast<double>(c.b1) / z) / (1.0 + static_cast<double>(c.a1) / z);
            }
            pinkGain = kPinkLevel / static_cast<float>(std::abs(at1k));
        }

        float runPink(PinkState& state, float white) const noexcept
        {
            float x = white;
            for (std::size_t i = 0; i < pinkCoefficients.size(); ++i)
            {
                const auto& c = pinkCoefficients[i];
                const float y = c.b0 * x + c.b1 * state.x[i] - c.a1 * state.y[i];
                state.x[i] = x;
                state.y[i] = y;
                x = y;
            }
            return x;
        }

        BandCoefficients bandCoefficients(float frequencyHz, float resonanceAmount) const noexcept
        {
            const double f = juce::jlimit(20.0, 0.45 * sampleRate, static_cast<double>(frequencyHz));
            const double q = 0.5 * std::pow(60.0, static_cast<double>(resonanceAmount));
            const double g = std::tan(juce::MathConstants<double>::pi * f / sampleRate);
            const double k = 1.0 / q;
            BandCoefficients c;
            c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
            c.a2 = static_cast<float>(g * c.a1);
            c.a3 = static_cast<float>(g * c.a2);
            c.k = static_cast<float>(k);
            // White noise through a band-pass of peak gain 1 keeps pi f / (q fs) of its power;
            // this brings it back to half (what white keeps below the decimator's band edge).
            c.gain = static_cast<float>(std::min(64.0, std::sqrt(q * sampleRate / (2.0 * juce::MathConstants<double>::pi * f))));
            return c;
        }

        static float runBand(BandState& state, const BandCoefficients& c, float x) noexcept
        {
            // Simper's trapezoidal state-variable filter; k * v1 is the band-pass of peak gain 1.
            const float v3 = x - state.ic2;
            const float v1 = c.a1 * state.ic1 + c.a2 * v3;
            const float v2 = state.ic2 + c.a2 * state.ic1 + c.a3 * v3;
            state.ic1 = 2.0f * v1 - state.ic1;
            state.ic2 = 2.0f * v2 - state.ic2;
            return c.gain * c.k * v1;
        }

        /** The register's output for this sample; each step in it is band-limited around its exact time. */
        float runChip(double step, bool shortMode, float gainLeft, float gainRight) noexcept
        {
            const float before = (shift & 1u) ? -kChipLevel : kChipLevel;
            float level = before;
            clockPhase += step;
            while (clockPhase >= 1.0)
            {
                clockPhase -= 1.0;
                shift = stepShift(shift, shortMode);
                const float next = (shift & 1u) ? -kChipLevel : kChipLevel;
                if (next != level)
                {
                    // The tick happened clockPhase / step of a sample before now.
                    const float tau = static_cast<float>(1.0 - clockPhase / step);
                    addStep(juce::jlimit(1.0e-6f, 1.0f, tau), next - level, gainLeft, gainRight);
                    level = next;
                }
            }
            return level;
        }

        void addStep(float tau, float jump, float gainLeft, float gainRight) noexcept
        {
            for (unsigned int j = 0; j < 4u; ++j)
            {
                const float x = static_cast<float>(static_cast<int>(j) - 1) - tau;
                const float c = jump * dsp::blepResidual(x);
                const unsigned int slot = (cursor + 2u + j) & 3u;
                accLeft[slot] += c * gainLeft;
                accRight[slot] += c * gainRight;
                accMono[slot] += c;
            }
        }

        struct ShelfCoefficients
        {
            float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
        };

        double sampleRate = 96000.0;
        std::array<ShelfCoefficients, pinkSections> pinkCoefficients {};
        float pinkGain = 1.0f;
        PinkState pinkLeft, pinkRight;
        BandState bandLeft, bandRight;
        float toneLeft = 0.0f, toneRight = 0.0f, toneMono = 0.0f;
        std::array<float, 4> accLeft {}, accRight {}, accMono {};
        unsigned int cursor = 0;
        double clockPhase = 0.0;
        std::uint32_t shift = 1;
        std::uint32_t rngLeft = 1, rngRight = 2;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<NoiseGenerator>);
}
