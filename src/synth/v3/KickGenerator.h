#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "DspMath.h"
#include "Module.h"

/*
    The Kick generator (docs/research/kick-generator.md, D229, D232): a kick drum in one slot, tuned to the key, with
    three models behind one set of knobs, as the Chip generator's chips share theirs.

    808: a struck resonator, as the TR-808's bridged-T is (Werner, Abel and Smith, DAFx 2014): the trigger pulse rings
    it, and the ring is the drum. Its pitch does what the paper measured: for the first few milliseconds it sits more
    than an octave up (Punch over Punch Time, the attack shift), then it "sighs" down a few semitones over about a
    quarter second, further the harder the hit (Sigh, Accent). A new hit lands on the drum still ringing, so no two
    hits are quite alike. The resonator is a decaying phasor turned by the pitch each sample, so it stays a sine
    however the pitch moves; Shape clips it as the 808's op-amps do; the trigger pulse leaks through as its click.

    909 and Modern: an oscillator thrown up in pitch and falling to the note (Punch, Punch Time), the 909's shaped
    towards a rounded square (Shape) with its attack pulse and noise burst, Modern a pure sine with a short bright
    transient. Each has its own decay; a new hit restarts the pitch and the level from where the tail is, without a
    step.

    All three: Decay (to -60 dB), Click, Noise, Tone (a low-pass on the whole drum), Drive (a cubic soft clip with
    first-order antiderivative anti-aliasing) and Trigger: One Shot plays the tail out after the key is let go, Gate
    leaves the end to the voice's envelope. The tail sits at the slot's pitch: octave, semitone, glide and bend tune
    it. The pitch moves at a control rate of every fourth sample, the bodies turn as phasors each sample. Mono, two
    samples late like every generator.
*/
namespace coolsynth::synth::v3
{
    class KickGenerator final
    {
    public:
        enum Param : int { model = 0, decay, punch, punchTime, sigh, click, noise, shape, accent, drive, tone, trigger, paramCount };
        enum class Model : int { tr808 = 0, tr909, modern };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr float level = 0.8f;          // a full hit's body peaks near here
        static constexpr double silentLevel = 1.0e-4; // -80 dB: the tail has ended
        static constexpr double sighSeconds = 0.08;   // the 808's sigh settles within about a quarter second

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Kick", 1,
                {{ { "model",  "Model",      0.0f,   2.0f,     0.0f,   Skew::linear,      Unit::choice,       false, "808|909|Modern" },
                   { "decay",  "Decay",      50.0f,  10000.0f, 600.0f, Skew::logarithmic, Unit::milliseconds, true },
                   { "punch",  "Punch",      0.0f,   48.0f,    12.0f,  Skew::linear,      Unit::semitones,    true },
                   { "ptime",  "Punch Time", 1.0f,   500.0f,   6.0f,   Skew::logarithmic, Unit::milliseconds, true },
                   { "sigh",   "Sigh",       0.0f,   12.0f,    2.4f,   Skew::linear,      Unit::semitones,    true },
                   { "click",  "Click",      0.0f,   1.0f,     0.3f,   Skew::linear,      Unit::percent,      true },
                   { "noise",  "Noise",      0.0f,   1.0f,     0.0f,   Skew::linear,      Unit::percent,      true },
                   { "shape",  "Shape",      0.0f,   1.0f,     0.0f,   Skew::linear,      Unit::percent,      true },
                   { "accent", "Accent",     0.0f,   1.0f,     0.5f,   Skew::linear,      Unit::percent,      true },
                   { "drive",  "Drive",      0.0f,   24.0f,    0.0f,   Skew::linear,      Unit::decibels,     true },
                   { "tone",   "Tone",       0.0f,   1.0f,     0.5f,   Skew::linear,      Unit::percent,      true },
                   { "trig",   "Trigger",    0.0f,   1.0f,     0.0f,   Skew::linear,      Unit::choice,       false, "One Shot|Gate" } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            rngStart = ((memory.seed() + 1u) * 2246822519u) ^ 0x6B1Cu;
            if (rngStart == 0)
                rngStart = 1;
            rng = rngStart;
            reset();
        }

        /** Back to the random numbers it was prepared with: a defined start (D222). */
        void reseed() noexcept { rng = rngStart; }

        void reset() noexcept
        {
            body = { 0.0, 0.0 };
            oscillator = { 1.0, 0.0 };
            turn = { 1.0, 0.0 };
            controlCountdown = 0;
            bodyLevel = 0.0;
            sinceHit = 1.0e9;
            hitPending = false;
            hitLevel = 0.0f;
            riseRemaining = 0;
            ring.fill(0.0f);
            cursor = 0;
            toneState = 0.0f;
            clickLow = 0.0f;
            driveLast = 0.0f;
            driveLastF = 0.0;
            rampsPrimed = false;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            input = {};
        }

        void noteOn(const NoteContext& note) noexcept { hit(note.velocity); }

        /** The same voice again: the drum is struck while it rings. */
        void strike(float velocity) noexcept { hit(velocity); }
        void strike() noexcept { hit(lastVelocity); }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        /** One Shot: the voice waits for the tail before it releases. */
        [[nodiscard]] bool holdsRelease() const noexcept { return oneShot && (hitPending || bodyLevel > silentLevel); }
        [[nodiscard]] bool silentUntilNextNote() const noexcept { return ! hitPending && bodyLevel <= silentLevel && sinceHit > 0.05 * sampleRate; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const auto kind = static_cast<Model>(juce::jlimit(0, 2, static_cast<int>(std::lround(params.p[model]))));
            const double decaySeconds = juce::jlimit(0.05, 10.0, 0.001 * static_cast<double>(params.p[decay]));
            const double punchSemis = juce::jlimit(0.0, 48.0, static_cast<double>(params.p[punch]));
            const double punchSeconds = juce::jlimit(0.001, 0.5, 0.001 * static_cast<double>(params.p[punchTime]));
            const double sighSemis = juce::jlimit(0.0, 12.0, static_cast<double>(params.p[sigh]));
            const float clickAmount = juce::jlimit(0.0f, 1.0f, params.p[click]);
            const float noiseAmount = juce::jlimit(0.0f, 1.0f, params.p[noise]);
            const float shapeAmount = juce::jlimit(0.0f, 1.0f, params.p[shape]);
            const float accentAmount = juce::jlimit(0.0f, 1.0f, params.p[accent]);
            const float driveDb = juce::jlimit(0.0f, 24.0f, params.p[drive]);
            const float toneAmount = juce::jlimit(0.0f, 1.0f, params.p[tone]);
            oneShot = std::lround(params.p[trigger]) == 0;

            const double fs = sampleRate;
            const double note = juce::jlimit(5.0, 0.2 * fs, static_cast<double>(params.frequencyHz));
            // Each sample the body's amplitude falls by this much: -60 dB over Decay.
            const double fall = std::exp(-6.907755 / (decaySeconds * fs));
            const double punchFall = std::exp(-1.0 / (punchSeconds * fs));
            const double sighFall = std::exp(-1.0 / ((kind == Model::tr808 ? sighSeconds : decaySeconds / 3.0) * fs));
            const double clickFall = std::exp(-1.0 / (0.004 * fs)); // the attack's own short decay
            const double noiseFall = std::exp(-1.0 / ((kind == Model::tr909 ? 0.03 : 0.015) * fs));
            const double toneHz = std::min(0.45 * fs, 80.0 * std::pow(250.0, static_cast<double>(toneAmount)));
            const float toneG = static_cast<float>(std::tan(juce::MathConstants<double>::pi * toneHz / fs));
            const float toneCoefficient = toneG / (1.0f + toneG);
            const bool toneOn = toneAmount < 0.999f;
            // Drive and Shape glide across the block, so a macro that throws them does not click.
            const float driveTarget = std::pow(10.0f, driveDb / 20.0f);
            const float driveMixTarget = juce::jlimit(0.0f, 1.0f, driveDb / 3.0f); // eased in over the first 3 dB
            if (! rampsPrimed)
            {
                driveNow = driveTarget;
                driveMixNow = driveMixTarget;
                shapeNow = shapeAmount;
                rampsPrimed = true;
            }
            const float rampScale = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float driveStep = (driveTarget - driveNow) * rampScale;
            const float driveMixStep = (driveMixTarget - driveMixNow) * rampScale;
            const float shapeStep = (shapeAmount - shapeNow) * rampScale;
            const bool driven = driveMixTarget > 0.0f || driveMixNow > 0.0f;
            const bool shaped = shapeAmount > 0.0f || shapeNow > 0.0f;
            const float clickLowCoefficient = coefficient(1500.0);

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                if (hitPending)
                {
                    hitPending = false;
                    // Accent: how far the velocity moves the hit, in its level, its punch and its sigh.
                    hitLevel = 1.0f - accentAmount * (1.0f - lastVelocity);
                    sinceHit = 0.0;
                    punchEnvelope = 1.0;
                    sighEnvelope = 1.0;
                    clickEnvelope = 1.0;
                    noiseEnvelope = 1.0;
                    if (kind == Model::tr808)
                        body += std::complex<double>(static_cast<double>(hitLevel), 0.0); // a real kick: the sine starts at 0
                    else
                    {
                        if (bodyLevel <= silentLevel)
                            oscillator = { 1.0, 0.0 }; // a fresh drum starts its sine at 0
                        riseRemaining = std::max(1, static_cast<int>(0.00025 * sampleRate)); // from where the tail is, no step
                    }
                    controlCountdown = 0;
                }

                // The pitch at a control rate of a quarter of the voice's: the turn of each sample's phasor.
                if (controlCountdown <= 0)
                {
                    controlCountdown = controlInterval;
                    const double hitPower = static_cast<double>(hitLevel);
                    const double semitones = punchSemis * std::sqrt(hitPower) * punchEnvelope + sighSemis * hitPower * sighEnvelope;
                    const double hz = std::min(0.4 * fs, note * std::exp2(semitones / 12.0));
                    const double w = juce::MathConstants<double>::twoPi * hz / fs;
                    turn = std::polar(kind == Model::tr808 ? fall : 1.0, w);
                    if (kind == Model::tr808)
                        bodyLevel = std::abs(body);
                    else
                        oscillator /= std::abs(oscillator); // keeps the turning phasor on the unit circle
                }
                --controlCountdown;

                driveNow += driveStep;
                driveMixNow += driveMixStep;
                shapeNow += shapeStep;

                float drum = 0.0f;
                if (kind == Model::tr808)
                {
                    body *= turn;
                    const float v = static_cast<float>(body.imag());
                    // The op-amps clip a hard-driven drum (Shape).
                    drum = shaped ? shapeOf(v) : v;
                }
                else
                {
                    oscillator *= turn;
                    if (riseRemaining > 0)
                    {
                        bodyLevel += (static_cast<double>(hitLevel) - bodyLevel) / static_cast<double>(riseRemaining);
                        --riseRemaining;
                    }
                    else
                        bodyLevel *= fall;
                    const float sine = static_cast<float>(oscillator.imag());
                    drum = (kind == Model::tr909 && shaped ? shapeOf(sine) : sine) * static_cast<float>(bodyLevel);
                }

                // The attack: the 808's trigger pulse leaking through, the 909's attack pulse, Modern's bright transient.
                float attack = 0.0f;
                if (clickAmount > 0.0f && clickEnvelope > 1.0e-4)
                {
                    const double t = sinceHit / fs;
                    float pulse = 0.0f;
                    if (kind == Model::modern)
                        pulse = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 3200.0 * t) * clickEnvelope);
                    else
                    {
                        // The 1 ms trigger pulse with soft edges, through a high-pass: its edges are the click. The
                        // 808's pulse shaper clips the falling edge to about a diode drop.
                        const double width = 0.001, edge = 0.00012;
                        double p = 0.0;
                        if (t < edge)
                            p = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * t / edge);
                        else if (t < width)
                            p = 1.0;
                        else if (t < width + edge)
                            p = 0.5 + 0.5 * std::cos(juce::MathConstants<double>::pi * (t - width) / edge);
                        const float x = static_cast<float>(p);
                        pulse = x - onePole(clickLow, x, clickLowCoefficient);
                        if (kind == Model::tr808)
                            pulse = std::max(pulse, -0.25f);
                    }
                    attack = 0.6f * clickAmount * hitLevel * pulse;
                }
                float hiss = 0.0f;
                if (noiseAmount > 0.0f && noiseEnvelope > 1.0e-4)
                    hiss = 0.5f * noiseAmount * hitLevel * static_cast<float>(noiseEnvelope) * nextNoise();

                float value = level * drum + attack + hiss;
                if (toneOn)
                    value = onePole(toneState, value, toneCoefficient);
                if (driven)
                {
                    const float wet = drivenValue(value * driveNow) / std::sqrt(driveNow);
                    value += driveMixNow * (wet - value);
                }
                else
                {
                    driveLast = 0.0f; // the next time the drive comes in, it starts from silence's state
                    driveLastF = 0.0;
                }

                punchEnvelope *= punchFall;
                sighEnvelope *= sighFall;
                clickEnvelope *= clickFall;
                noiseEnvelope *= noiseFall;
                sinceHit += 1.0;

                // Two samples late, as every generator.
                const unsigned int now = cursor & 3u;
                ring[now] = value;
                float out = ring[(cursor + 2u) & 3u];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    out *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = out;
                right[s] = out;
                signalTrack[static_cast<std::size_t>(s)] = out;
            }

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            for (int s = 0; s < numSamples; ++s)
            {
                left[s] *= gainLeft;
                right[s] *= gainRight;
            }
        }

    private:
        void hit(float velocity) noexcept
        {
            lastVelocity = juce::jlimit(0.0f, 1.0f, velocity);
            hitPending = true;
        }

        [[nodiscard]] float coefficient(double hz) const noexcept
        {
            const double g = std::tan(juce::MathConstants<double>::pi * std::min(hz, 0.45 * sampleRate) / sampleRate);
            return static_cast<float>(g / (1.0 + g));
        }

        /** Trapezoidal one-pole low-pass; coefficient is g / (1 + g). */
        static float onePole(float& z, float x, float c) noexcept
        {
            const float v = (x - z) * c;
            const float y = v + z;
            z = y + v;
            return y;
        }

        /** Shape: the drum through a tanh, mixed in by Shape's amount, so no Shape is the drum untouched. */
        [[nodiscard]] float shapeOf(float v) const noexcept
        {
            const float gain = 1.0f + 6.0f * shapeNow;
            const float clipped = dsp::fastTanh(v * gain) / dsp::fastTanh(gain);
            return v + shapeNow * (clipped - v);
        }

        /** A cubic soft clip of slope 1 at 0, flat at +-1 past 1.5 (1.5 clip(u / 1.5)), with first-order
            antiderivative anti-aliasing: its antiderivative is a polynomial, so the drive costs a few multiplies. */
        float drivenValue(float x) noexcept
        {
            const double u = static_cast<double>(x) / 1.5;
            const double f = 2.25 * clipIntegral(u);
            const double dx = static_cast<double>(x) - static_cast<double>(driveLast);
            double y;
            if (std::abs(dx) < 1.0e-5)
                y = 1.5 * clip(0.5 * (static_cast<double>(x) + static_cast<double>(driveLast)) / 1.5);
            else
                y = (f - driveLastF) / dx;
            driveLast = x;
            driveLastF = f;
            return static_cast<float>(y);
        }

        static double clip(double x) noexcept
        {
            if (x >= 1.0)
                return 2.0 / 3.0;
            if (x <= -1.0)
                return -2.0 / 3.0;
            return x - x * x * x / 3.0;
        }

        static double clipIntegral(double x) noexcept
        {
            const double a = std::abs(x);
            if (a >= 1.0)
                return 2.0 / 3.0 * a - 0.25;
            const double x2 = x * x;
            return 0.5 * x2 - x2 * x2 / 12.0;
        }

        float nextNoise() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        double sampleRate = 96000.0;
        static constexpr int controlInterval = 4;
        std::complex<double> body { 0.0, 0.0 }, oscillator { 1.0, 0.0 }, turn { 1.0, 0.0 };
        int controlCountdown = 0;
        double bodyLevel = 0.0, sinceHit = 1.0e9;
        double punchEnvelope = 0.0, sighEnvelope = 0.0, clickEnvelope = 0.0, noiseEnvelope = 0.0;
        float lastVelocity = 1.0f, hitLevel = 0.0f;
        int riseRemaining = 0;
        bool hitPending = false, oneShot = true;
        float toneState = 0.0f, clickLow = 0.0f, driveLast = 0.0f;
        double driveLastF = 0.0;
        float driveNow = 1.0f, driveMixNow = 0.0f, shapeNow = 0.0f;
        bool rampsPrimed = false;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1, rngStart = 1;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<KickGenerator>);
}
