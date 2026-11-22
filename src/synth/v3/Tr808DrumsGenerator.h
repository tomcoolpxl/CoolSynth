#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "Tr808BassDrum.h"
#include "Tr808RimShot.h"
#include "Tr808Tom.h"

/*
    The 808 Drums generator (D252): the TR-808's bass drum, its low, mid and high tom or conga and its rim shot or
    claves, each its own circuit (Tr808BassDrum.h, Tr808Tom.h, Tr808RimShot.h), in one slot that a kit voice plays
    (the part's Kit play mode, Part.h): each note strikes its instrument, the instruments ring together.

    The keys are General MIDI's with Roland's TR-8S alternates: bass drum 36 and 35, rim shot 37, low tom 43 and 41,
    mid tom 47 and 45, high tom 50 and 48, low conga 64, mid conga 63, high conga 62, claves 75. A tom and its conga
    share their circuit, as on the machine: the key sets its switch (SW8, SW9, SW10) before the hit, as the rim shot's
    and the claves' sets SW11; a struck conga takes over its tom's ringing circuit.

    The knobs are the 808's own ten (the bass drum's Level, Tone and Decay; each pair's Level and Tuning; the rim
    shot's Level) with its pots' tapers (levels audio, tunings and Decay linear, Tone the C taper fitted to two units),
    then Accent, which moves each hit's trigger between 4.7 and 11.5 V with its velocity (D258), and Punch Time, the bass drum's
    glide past its stock 6 ms (the Kick's knob). The toms share the machine's pink noise bus: white noise at the 130 mV
    rms the service notes set at TP4, over the later boards' band (C202, 22 pF across R130), through IC24b's Sallen-Key
    low-pass (273 Hz). Each circuit has its own copy of the common trigger stage, so hits at once do not load each
    other. The key does not tune the drums, nor do the slot's pitch and the bend. Mono, two samples late like every
    generator; a circuit runs from its hit until it has rung down, then rests and costs nothing.
*/
namespace coolsynth::synth::v3
{
    class Tr808DrumsGenerator final
    {
    public:
        enum Param : int { bdLevel = 0, bdTone, bdDecay, lowLevel, lowTuning, midLevel, midTuning, highLevel, highTuning, rimLevel,
                           accent, punchTime, paramCount };
        enum Instrument : int { bassDrum = 0, lowPair, midPair, highPair, rimPair, instrumentCount };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double circuitVoltsToLevel = 0.18; // as the Kick's 808: the circuits' volts to the slot's level
        static constexpr double silentLevel = 1.0e-4;       // -80 dB: an instrument has rung down
        /** The white noise's density, volts per root hertz: 130 mV rms over C202's band, a pole at 7.2 kHz. */
        static constexpr double noiseDensity = 0.13 / 106.60; // the band: pi / 2 times 1 / (2 pi 1 MOhm 22 pF), 11.36 kHz

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "808 Drums", 1,
                {{ { "bdLevel", "BD Level",        0.0f, 1.0f,   0.8f, Skew::linear,      Unit::percent,      true },
                   { "bdTone",  "BD Tone",         0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "bdDecay", "BD Decay",        0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "ltLevel", "LT/LC Level",     0.0f, 1.0f,   0.8f, Skew::linear,      Unit::percent,      true },
                   { "ltTune",  "LT/LC Tuning",    0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "mtLevel", "MT/MC Level",     0.0f, 1.0f,   0.8f, Skew::linear,      Unit::percent,      true },
                   { "mtTune",  "MT/MC Tuning",    0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "htLevel", "HT/HC Level",     0.0f, 1.0f,   0.8f, Skew::linear,      Unit::percent,      true },
                   { "htTune",  "HT/HC Tuning",    0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "rsLevel", "RS/CL Level",     0.0f, 1.0f,   0.8f, Skew::linear,      Unit::percent,      true },
                   { "accent",  "Accent",          0.0f, 1.0f,   0.5f, Skew::linear,      Unit::percent,      true },
                   { "ptime",   "Punch Time",      1.0f, 500.0f, 6.0f, Skew::logarithmic, Unit::milliseconds, true } }},
                paramCount };
            return d;
        }

        /** The instrument a key strikes, and whether it is the pair's other one (conga, claves); -1 for no instrument. */
        static constexpr int instrumentFor(int note, bool& other) noexcept
        {
            other = false;
            switch (note)
            {
                case 35: case 36: return bassDrum;
                case 37:          return rimPair;
                case 75:          other = true; return rimPair;
                case 41: case 43: return lowPair;
                case 64:          other = true; return lowPair;
                case 45: case 47: return midPair;
                case 63:          other = true; return midPair;
                case 48: case 50: return highPair;
                case 62:          other = true; return highPair;
                default:          return -1;
            }
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        /** Every instrument starts each hit from rest, silent by itself: the voice may open its amplifier at once (D251). */
        static bool startsFromRest(const float*) noexcept { return true; }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            rngStart = ((memory.seed() + 1u) * 2654435761u) ^ 0x808Du;
            if (rngStart == 0)
                rngStart = 1;
            rng = rngStart;
            bass.prepare(sampleRate);
            bass.setTuning(1.0);
            for (auto& t : toms)
                t.prepare(sampleRate);
            rim.prepare(sampleRate);
            noiseSigma = noiseDensity * std::sqrt(0.5 * sampleRate);
            designNoiseFilter();
            reset();
        }

        /** Back to the random numbers it was prepared with: a defined start (D222). */
        void reseed() noexcept { rng = rngStart; }

        void reset() noexcept
        {
            // Each pair back on its first instrument (tom, rim shot), as a fresh kit: a switch only when it was moved.
            bass.rest();
            for (auto& t : toms)
            {
                if (t.conga())
                    t.setConga(false);
                t.rest();
            }
            if (rim.claves())
                rim.setClaves(false);
            rim.rest();
            pending.fill(false);
            pendingOther.fill(false);
            pendingVelocity.fill(1.0f);
            running.fill(false);
            level.fill(0.0);
            sinceHit.fill(1.0e9);
            noiseState = {};
            ring.fill(0.0f);
            cursor = 0;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            input = {};
            applied.fill(-1.0f);
            currentNote = 60;
        }

        void noteOn(const NoteContext& note) noexcept
        {
            currentNote = note.noteNumber;
            hit(note.noteNumber, note.velocity);
        }

        /** The kit voice plays another key: the next strike is that key's instrument. */
        void noteChanged(int noteNumber) noexcept { currentNote = noteNumber; }

        /** The kit voice struck again: the key's instrument is hit while the others ring. */
        void strike(float velocity) noexcept { hit(currentNote, velocity); }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        /** The voice waits for every instrument to ring down before it releases. */
        [[nodiscard]] bool holdsRelease() const noexcept { return anyPending() || anyRunning(); }
        [[nodiscard]] bool silentUntilNextNote() const noexcept { return ! anyPending() && ! anyRunning(); }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            configure(params);
            const float accentAmount = juce::jlimit(0.0f, 1.0f, params.p[accent]);
            const double follow = std::exp(-1.0 / (0.02 * sampleRate)); // each instrument's level follower, for its end
            const double settleSamples = 0.05 * sampleRate;             // a hit runs at least this long
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                for (int k = 0; k < instrumentCount; ++k)
                {
                    const auto i = static_cast<std::size_t>(k);
                    if (! pending[i])
                        continue;
                    pending[i] = false;
                    const double volts = Tr808BassDrum::triggerFor(static_cast<double>(accentAmount * pendingVelocity[i]));
                    if (k == bassDrum)
                        bass.trigger(volts);
                    else if (k == rimPair)
                    {
                        if (rim.claves() != pendingOther[i])
                            rim.setClaves(pendingOther[i]);
                        rim.trigger(volts);
                    }
                    else
                    {
                        auto& t = toms[i - 1];
                        if (t.conga() != pendingOther[i])
                            t.setConga(pendingOther[i]);
                        t.trigger(volts);
                    }
                    running[i] = true;
                    sinceHit[i] = 0.0;
                }

                // The pink noise bus, while a tom's gate may pass it.
                double pink = 0.0;
                if (running[lowPair] || running[midPair] || running[highPair])
                    pink = nextPink();

                double sum = 0.0;
                for (int k = 0; k < instrumentCount; ++k)
                {
                    const auto i = static_cast<std::size_t>(k);
                    if (! running[i])
                        continue;
                    double y = 0.0;
                    if (k == bassDrum)
                        y = bass.process();
                    else if (k == rimPair)
                        y = rim.process();
                    else
                        y = toms[i - 1].process(pink);
                    y *= circuitVoltsToLevel;
                    level[i] = std::max(std::abs(y), level[i] * follow);
                    sinceHit[i] += 1.0;
                    if (level[i] <= silentLevel && sinceHit[i] > settleSamples)
                        running[i] = false; // rung down: it rests until its next hit
                    sum += y;
                }

                // Two samples late, as every generator.
                const unsigned int now = cursor & 3u;
                ring[now] = static_cast<float>(sum);
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

        // For tests.
        [[nodiscard]] bool isRunningForTesting(int instrument) const noexcept { return running[static_cast<std::size_t>(instrument)]; }
        [[nodiscard]] const Tr808Tom& tomForTesting(int pair) const noexcept { return toms[static_cast<std::size_t>(pair)]; }
        [[nodiscard]] const Tr808RimShot& rimForTesting() const noexcept { return rim; }

    private:
        /** The knobs to the circuits, once a block; a circuit's part is set again only when its knob has moved. */
        void configure(const SlotParams& params) noexcept
        {
            const auto moved = [this, &params](int index)
            {
                const auto i = static_cast<std::size_t>(index);
                const float value = params.p[i];
                if (std::abs(value - applied[i]) <= 1.0e-5f)
                    return false;
                applied[i] = value;
                return true;
            };
            const auto knob = [&params](int index) { return juce::jlimit(0.0, 1.0, static_cast<double>(params.p[static_cast<std::size_t>(index)])); };
            if (moved(bdLevel))
                bass.setLevel(audioTaper(knob(bdLevel)));
            if (moved(bdTone))
                bass.setTone(Tr808BassDrum::toneForKnob(knob(bdTone)));
            if (moved(bdDecay))
                bass.setDecay(knob(bdDecay));
            if (moved(lowLevel))
                toms[0].setLevel(knob(lowLevel));
            if (moved(lowTuning))
                toms[0].setTuning(knob(lowTuning));
            if (moved(midLevel))
                toms[1].setLevel(knob(midLevel));
            if (moved(midTuning))
                toms[1].setTuning(knob(midTuning));
            if (moved(highLevel))
                toms[2].setLevel(knob(highLevel));
            if (moved(highTuning))
                toms[2].setTuning(knob(highTuning));
            if (moved(rimLevel))
                rim.setLevel(knob(rimLevel));
            if (moved(punchTime))
            {
                // Punch Time, as the Kick's: up to the stock 6 ms, C38 shorter; past it, the glide bend (D251).
                const double seconds = juce::jlimit(0.001, 0.5, 0.001 * static_cast<double>(params.p[punchTime]));
                bass.setEnvelopeScale(std::min(1.0, seconds / 0.006));
                bass.setGlide(Tr808BassDrum::glideScaleFor(seconds));
            }
        }

        /** An audio-taper (A) pot's share below its wiper at a turn of 0..1: the bass drum's LEVEL, VR4. */
        static double audioTaper(double turn) noexcept { return (std::pow(10.0, 2.0 * std::clamp(turn, 0.0, 1.0)) - 1.0) / 99.0; }

        void hit(int note, float velocity) noexcept
        {
            bool other = false;
            const int k = instrumentFor(note, other);
            if (k < 0)
                return;
            const auto i = static_cast<std::size_t>(k);
            pending[i] = true;
            pendingOther[i] = other;
            pendingVelocity[i] = juce::jlimit(0.0f, 1.0f, velocity);
        }

        [[nodiscard]] bool anyPending() const noexcept { return std::any_of(pending.begin(), pending.end(), [](bool p) { return p; }); }
        [[nodiscard]] bool anyRunning() const noexcept { return std::any_of(running.begin(), running.end(), [](bool r) { return r; }); }

        /** IC24b: a unity-gain Sallen-Key low-pass (R133, R132 22 kOhm; C32 39 nF, C33 18 nF), by the bilinear transform. */
        void designNoiseFilter() noexcept
        {
            const double r = 22.0e3, c1 = 39.0e-9, c2 = 18.0e-9;
            const double w0 = 1.0 / (r * std::sqrt(c1 * c2));
            const double q = std::sqrt(c1 * c2) / (2.0 * c2);
            const double k = 2.0 * sampleRate;
            const double a0 = k * k + k * w0 / q + w0 * w0;
            noiseB = w0 * w0 / a0;
            noiseA1 = 2.0 * (w0 * w0 - k * k) / a0;
            noiseA2 = (k * k - k * w0 / q + w0 * w0) / a0;
        }

        /** One sample of the pink noise bus, volts: Gaussian white noise (four uniforms summed) through IC24b. */
        double nextPink() noexcept
        {
            double u = 0.0;
            for (int j = 0; j < 4; ++j)
            {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                u += static_cast<double>(rng >> 8) * (1.0 / 16777216.0) - 0.5;
            }
            const double white = u * std::sqrt(3.0) * noiseSigma; // four uniforms on +-0.5 have a variance of 1/3
            // Direct form II transposed, (1 + 2 z^-1 + z^-2) b.
            const double y = noiseB * white + noiseState[0];
            noiseState[0] = 2.0 * noiseB * white - noiseA1 * y + noiseState[1];
            noiseState[1] = noiseB * white - noiseA2 * y;
            return y;
        }

        double sampleRate = 96000.0;
        Tr808BassDrum bass;
        std::array<Tr808Tom, 3> toms { Tr808Tom(Tr808Tom::Pair::low), Tr808Tom(Tr808Tom::Pair::mid), Tr808Tom(Tr808Tom::Pair::high) };
        Tr808RimShot rim;
        std::array<bool, instrumentCount> pending {}, pendingOther {}, running {};
        std::array<float, instrumentCount> pendingVelocity {};
        std::array<double, instrumentCount> level {}, sinceHit {};
        std::array<float, paramCount> applied {};
        int currentNote = 60;
        double noiseSigma = 0.0, noiseB = 0.0, noiseA1 = 0.0, noiseA2 = 0.0;
        std::array<double, 2> noiseState {};
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1, rngStart = 1;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<Tr808DrumsGenerator>);
}
