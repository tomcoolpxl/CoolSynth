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
#include "Tr808BassDrum.h"
#include "Tr909BassDrum.h"

/*
    The Kick generator (docs/research/kick-generator.md, D229, D232, D235, D238): a kick drum in one slot, tuned to the key,
    with three models behind one set of knobs, as the Chip generator's chips share theirs.

    808: the TR-808 bass drum's circuit (Tr808BassDrum.h, Werner's thesis in wave digital filters): the trigger
    pulse through the pulse shaper rings the bridged-T, the envelope generator's transistor shifts it up for the
    attack, the leakage makes its sigh, the feedback buffer its decay, the output filter its tone. The knobs reach
    the circuit's own controls and bends: Decay is VR6 (read through a table measured on the model, so it still
    means seconds to -60 dB), Tone is VR5, Accent moves the trigger between 4 and 14 V with the velocity, Click is
    the trigger's width, Punch the attack shift (R166), Punch Time the envelope (C38), Sigh the leakage (R161), Shape
    the op-amps' rails. The key tunes it by the bridged-T's capacitors, as the 808's tuning mod does.

    909: the TR-909 bass drum's circuit (Tr909BassDrum.h, from the service notes): a triangle oscillator swept by its
    pitch envelope, rounded by a diode pair, through a transistor VCA held for some 60 ms and then let fall, with an
    attack of a ringing click and filtered noise. The knobs reach its parts: Punch Time is TUNE (the sweep's fall, as
    a time constant), Punch the sweep's depth (R26), Decay is DECAY (VR5, read through a table measured on the model),
    Click is ATTACK (VR3), Accent moves the accent DAC between 2 and 4 V with the velocity, Shape the drive mod
    (R52), Noise the noise branch (R10). The key tunes it by the oscillator's capacitor.

    Modern: an oscillator thrown up in pitch and falling to the note (Punch, Punch Time), a pure sine with a short
    bright transient; a new hit restarts the pitch and the level from where the tail is, without a step.

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
        static constexpr double circuitVoltsToLevel = 0.18; // the circuit's volts to the slot's level: an accent of half at full velocity peaks near 0.8
        static constexpr double circuit909VoltsToLevel = 0.15; // likewise the 909's, its LEVEL full

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
            circuit.prepare(sampleRate);
            circuit.setLevel(0.99);
            drum909.prepare(sampleRate);
            drum909.setLevelOhms(100.0e3);
            drum909.setNoiseSeed(rngStart);
            reset();
        }

        /** Back to the random numbers it was prepared with: a defined start (D222). */
        void reseed() noexcept { rng = rngStart; }

        void reset() noexcept
        {
            circuit.rest();
            drum909.rest();
            blockIn909 = blockOut909 = 0.0;
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
            const double sighFall = std::exp(-1.0 / ((decaySeconds / 3.0) * fs));
            const double clickFall = std::exp(-1.0 / (0.004 * fs)); // the attack's own short decay
            const double noiseFall = std::exp(-1.0 / (0.015 * fs));
            const double toneHz = std::min(0.45 * fs, 80.0 * std::pow(250.0, static_cast<double>(toneAmount)));
            const float toneG = static_cast<float>(std::tan(juce::MathConstants<double>::pi * toneHz / fs));
            const float toneCoefficient = toneG / (1.0f + toneG);
            const bool toneOn = toneAmount < 0.999f;
            // Drive glides across the block, so a macro that throws it does not click.
            const float driveTarget = std::pow(10.0f, driveDb / 20.0f);
            const float driveMixTarget = juce::jlimit(0.0f, 1.0f, driveDb / 3.0f); // eased in over the first 3 dB
            if (! rampsPrimed)
            {
                driveNow = driveTarget;
                driveMixNow = driveMixTarget;
                rampsPrimed = true;
            }
            const float rampScale = 1.0f / static_cast<float>(std::max(1, numSamples));
            const float driveStep = (driveTarget - driveNow) * rampScale;
            const float driveMixStep = (driveMixTarget - driveMixNow) * rampScale;
            const bool driven = driveMixTarget > 0.0f || driveMixNow > 0.0f;

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            const bool circuit808 = kind == Model::tr808, circuit909 = kind == Model::tr909;
            const bool circuitModel = circuit808 || circuit909;
            if (circuit808)
                configureCircuit(decaySeconds, punchSemis, punchSeconds, sighSemis, shapeAmount, toneAmount, note);
            if (circuit909)
                configure909(decaySeconds, punchSemis, punchSeconds, clickAmount, noiseAmount, shapeAmount, note);
            const double follow = std::exp(-1.0 / (0.02 * fs)); // the circuits' level follower, for their end
            const double block909 = std::exp(-juce::MathConstants<double>::twoPi * 10.0 / fs); // the 909's DC block

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
                    if (circuit808)
                        circuit.trigger(4.0 + 10.0 * static_cast<double>(accentAmount * lastVelocity), triggerWidth(clickAmount));
                    else if (circuit909)
                        drum909.trigger(2.0 + 2.0 * static_cast<double>(accentAmount * lastVelocity)); // the accent DAC, volts
                    else
                    {
                        if (bodyLevel <= silentLevel)
                            oscillator = { 1.0, 0.0 }; // a fresh drum starts its sine at 0
                        riseRemaining = std::max(1, static_cast<int>(0.00025 * sampleRate)); // from where the tail is, no step
                    }
                    controlCountdown = 0;
                }

                // The pitch at a control rate of a quarter of the voice's: the turn of each sample's phasor.
                if (! circuitModel && controlCountdown <= 0)
                {
                    controlCountdown = controlInterval;
                    const double hitPower = static_cast<double>(hitLevel);
                    const double semitones = punchSemis * std::sqrt(hitPower) * punchEnvelope + sighSemis * hitPower * sighEnvelope;
                    const double hz = std::min(0.4 * fs, note * std::exp2(semitones / 12.0));
                    const double w = juce::MathConstants<double>::twoPi * hz / fs;
                    turn = std::polar(1.0, w);
                    oscillator /= std::abs(oscillator); // keeps the turning phasor on the unit circle
                }
                --controlCountdown;

                driveNow += driveStep;
                driveMixNow += driveMixStep;

                float drum = 0.0f;
                if (circuitModel)
                {
                    // The circuit runs from a hit until it has rung down; then it rests (and costs nothing).
                    if (sinceHit < 0.05 * fs || bodyLevel > silentLevel)
                    {
                        double y = 0.0;
                        if (circuit808)
                            y = circuit.process() * circuitVoltsToLevel;
                        else
                        {
                            // The mixer's output carries Q12's base current as a slow offset: blocked at 10 Hz, as the
                            // drum leaves the machine through a capacitor.
                            const double raw = drum909.process();
                            blockOut909 = raw - blockIn909 + block909 * blockOut909;
                            blockIn909 = raw;
                            y = blockOut909 * circuit909VoltsToLevel;
                        }
                        bodyLevel = std::max(std::abs(y), bodyLevel * follow);
                        drum = static_cast<float>(y) / level;
                    }
                    else
                        bodyLevel = 0.0;
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
                    drum = sine * static_cast<float>(bodyLevel);
                }

                // Modern's bright transient (the 808's and the 909's attacks are their circuits' own).
                float attack = 0.0f;
                if (! circuitModel && clickAmount > 0.0f && clickEnvelope > 1.0e-4)
                {
                    const double t = sinceHit / fs;
                    const float pulse = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 3200.0 * t) * clickEnvelope);
                    attack = 0.6f * clickAmount * hitLevel * pulse;
                }
                float hiss = 0.0f;
                if (! circuit909 && noiseAmount > 0.0f && noiseEnvelope > 1.0e-4) // the 909's noise is its circuit's
                    hiss = 0.5f * noiseAmount * hitLevel * static_cast<float>(noiseEnvelope) * nextNoise();

                float value = level * drum + attack + hiss;
                if (toneOn && ! circuit808) // the 808's Tone is its circuit's VR5
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
        /**
            The 808 circuit's controls and bends from the knobs, once a block (a change makes the circuit analyse
            again, a few microseconds): each knob's default is the stock circuit.
        */
        void configureCircuit(double decaySeconds, double punchSemis, double punchSeconds, double sighSemis, float shapeAmount,
                              float toneAmount, double note) noexcept
        {
            circuit.setAttackResistor(6.8e3 * std::pow(4.0, (12.0 - punchSemis) / 12.0)); // Punch 12: the stock R166
            circuit.setEnvelopeScale(punchSeconds / 0.006);                                 // Punch Time 6 ms: the stock C38
            circuit.setLeakResistor(1.0e6 * 2.4 / std::max(0.05, sighSemis));             // Sigh 2.4: the stock R161
            circuit.setRails(15.0 - 11.0 * static_cast<double>(shapeAmount));             // Shape 0: the stock 15 V rails
            circuit.setTone(Tr808BassDrum::toneForKnob(static_cast<double>(toneAmount))); // the knob through VR5's taper
            // The tail on the note: the bridged-T's capacitors scaled, as the 808's tuning mod does. Only a move of
            // more than about a fifth of a cent re-tunes: each new tuning makes the resonator analyse again, and a
            // voice's slow drift would otherwise do that every block.
            const double scale = circuit.tailHz(1.0) / std::max(5.0, note);
            if (std::abs(scale / circuit.currentTuning() - 1.0) > 1.0e-4)
                circuit.setTuning(scale);
            // The decay in seconds whatever the tuning: it scales with the capacitors.
            circuit.setDecay(decayForSeconds(decaySeconds / circuit.currentTuning()));
        }

        /**
            The 909 circuit's parts from the knobs, once a block; each knob's default is the stock drum. A change of a
            resistor makes that circuit analyse again; the key only scales the oscillator's capacitor, outside them.
        */
        void configure909(double decaySeconds, double punchSemis, double punchSeconds, float clickAmount, float noiseAmount,
                          float shapeAmount, double note) noexcept
        {
            // Punch: the sweep's depth by R26 (C9's pull on the control voltage), 330 kOhm at the default 12.
            const double r26 = 330.0e3 * std::pow(2.0, (12.0 - punchSemis) / 12.0);
            drum909.setSweepOhms(r26);
            // Punch Time: the sweep's time constant, C9 ((R57 + TUNE) || R26): TUNE fully down (6.8 ms) at the default.
            const double c9 = 0.33e-6, r57 = 22.0e3;
            const double conductance = c9 / punchSeconds - 1.0 / r26;
            drum909.setTuneOhms(conductance > 1.0e-9 ? juce::jlimit(0.0, 10.0e6, 1.0 / conductance - r57) : 10.0e6);
            drum909.setDecayOhms(decay909ForSeconds(decaySeconds));
            drum909.setAttackOhms(500.0 * (1.0 - static_cast<double>(clickAmount))); // Click: ATTACK turned up
            drum909.setNoiseOhms(12.0e3 / (1.0 + 9.0 * static_cast<double>(noiseAmount))); // Noise: R10, stock at 0
            drum909.setShaperOhms(10.0e3 * (1.0 - 0.9 * static_cast<double>(shapeAmount))); // Shape: the drive mod, stock at 0
            // The settled pitch on the note: C14 scaled (the sweep scales with it).
            const double scale = drum909.restHz(1.0) / std::max(5.0, note);
            if (std::abs(scale / drum909.currentTuning() - 1.0) > 1.0e-4)
                drum909.setTuning(scale);
        }

        /**
            DECAY (VR5) for a time to -60 dB: measured on the circuit model (tests/V3Tr909Tests.cpp, COOLSYNTH_909_TABLES),
            read between points on a log scale. The circuit's shortest is 0.1 s, its stock longest 0.51 s (1 MOhm); past
            that VR5 is a bend up to 10 MOhm, 1.13 s, where C8's other path (R54 into Q12) takes over: a longer Decay
            stays there.
        */
        static double decay909ForSeconds(double seconds) noexcept
        {
            static constexpr std::array<double, 17> ohms { 1.0, 10.0e3, 30.0e3, 60.0e3, 100.0e3, 150.0e3, 220.0e3, 330.0e3, 470.0e3,
                                                           680.0e3, 1.0e6, 1.5e6, 2.2e6, 3.3e6, 4.7e6, 6.8e6, 10.0e6 };
            static constexpr std::array<double, 17> t60 { 0.100, 0.110, 0.120, 0.150, 0.170, 0.200, 0.240, 0.300, 0.350,
                                                          0.420, 0.510, 0.610, 0.710, 0.810, 0.910, 1.030, 1.130 };
            if (seconds <= t60.front())
                return ohms.front();
            for (std::size_t i = 1; i < t60.size(); ++i)
                if (seconds <= t60[i])
                {
                    const double w = std::log(seconds / t60[i - 1]) / std::log(t60[i] / t60[i - 1]);
                    return ohms[i - 1] + w * (ohms[i] - ohms[i - 1]);
                }
            return ohms.back();
        }

        /** Click: the trigger's width, 0.1 ms at 0, the stock 1 ms at the default 0.3, 10 ms at 1 (a bend). */
        static double triggerWidth(float click) noexcept
        {
            const double c = juce::jlimit(0.0, 1.0, static_cast<double>(click));
            return c <= 0.3 ? 0.0001 * std::pow(10.0, c / 0.3) : 0.001 * std::pow(10.0, (c - 0.3) / 0.7);
        }

        /**
            The Decay setting (VR6 over its stock 500 kOhm) for a time to -60 dB, at the stock tuning: measured on the
            circuit model (tests/V3Tr808Tests.cpp, COOLSYNTH_808_DECAY_TABLE), read between points on a log scale.
            The circuit's shortest is 0.17 s; past 2.7 s it is the extended-decay bend.
        */
        static double decayForSeconds(double seconds) noexcept
        {
            static constexpr std::array<double, 29> t60 { 0.167, 0.237, 0.298, 0.363, 0.450, 0.523, 0.610, 0.702, 0.801, 0.907,
                                                           1.021, 1.146, 1.281, 1.429, 1.592, 1.771, 1.969, 2.190, 2.453, 2.733,
                                                           3.052, 3.418, 3.843, 4.357, 4.951, 5.670, 6.567, 7.710, 9.213 };
            if (seconds <= t60.front())
                return 0.05;
            for (std::size_t i = 1; i < t60.size(); ++i)
                if (seconds <= t60[i])
                    return 0.05 * static_cast<double>(i) + 0.05 * std::log(seconds / t60[i - 1]) / std::log(t60[i] / t60[i - 1]);
            return std::min(1.48, 1.45 + 0.05 * std::log(seconds / t60.back()) / std::log(t60.back() / t60[t60.size() - 2]));
        }

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
        std::complex<double> oscillator { 1.0, 0.0 }, turn { 1.0, 0.0 };
        Tr808BassDrum circuit;
        Tr909BassDrum drum909;
        double blockIn909 = 0.0, blockOut909 = 0.0;
        int controlCountdown = 0;
        double bodyLevel = 0.0, sinceHit = 1.0e9;
        double punchEnvelope = 0.0, sighEnvelope = 0.0, clickEnvelope = 0.0, noiseEnvelope = 0.0;
        float lastVelocity = 1.0f, hitLevel = 0.0f;
        int riseRemaining = 0;
        bool hitPending = false, oneShot = true;
        float toneState = 0.0f, driveLast = 0.0f;
        double driveLastF = 0.0;
        float driveNow = 1.0f, driveMixNow = 0.0f;
        bool rampsPrimed = false;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1, rngStart = 1;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<KickGenerator>);
}
