#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "Tr808Clap.h"
#include "Tr808Snare.h"

/*
    The 808 Snare & Clap generator (D256): the TR-808's snare drum and its hand clap or maracas, each its own circuit
    (Tr808Snare.h, Tr808Clap.h), in one slot that a kit voice plays (the part's Kit play mode, Part.h): each note
    strikes its instrument, the instruments ring together.

    The keys are General MIDI's with an alternate: snare 38 and 40, clap 39, maracas 70. The clap and the maracas share
    their circuit, as on the machine: the key sets its switch (SW12) before the hit; a struck maracas leaves a ringing
    clap's tail ringing, and the other way round.

    The knobs are the 808's own four (the snare's Level, Tone and Snappy; the clap's and maracas' Level) with its pots'
    tapers (levels audio, Tone and Snappy linear), then Accent, which moves each hit's trigger between 4 and 11.5 V
    with its velocity (D258), and six bends, each at the machine's own value at 12 o'clock: the snare's Tune (its shells'
    capacitors, an octave either way) and Decay (its shells' damping, a quarter to four times), Snappy Decay (its
    envelope's capacitors), the clap's Decay (its reverb's envelope), Spread (its claps' timing, half to twice) and the
    maracas' Decay. The circuits share the machine's white noise bus: 130 mV rms at TP4, its band as C202 (22 pF across
    R130) sets it. The key does not tune the drums, nor do the slot's pitch and the bend. Mono, two samples late like
    every generator; a circuit runs from its hit until it has rung down, then rests and costs nothing, and its next hit
    starts from rest.
*/
namespace coolsynth::synth::v3
{
    class Tr808SnareClapGenerator final
    {
    public:
        enum Param : int { sdLevel = 0, sdTone, sdSnappy, cpLevel, accent, sdTune, sdDecay, snappyDecay, cpDecay, cpSpread, maDecay,
                           paramCount };
        enum Instrument : int { snareDrum = 0, clapPair, instrumentCount };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double circuitVoltsToLevel = 0.18; // as the Kick's 808: the circuits' volts to the slot's level
        static constexpr double silentLevel = 1.0e-4;       // -80 dB: an instrument has rung down
        /** The white noise's density, volts per root hertz: 130 mV rms over C202's band, a pole at 7.2 kHz. */
        static constexpr double noiseDensity = 0.13 / 106.60; // the band: pi / 2 times 1 / (2 pi 1 MOhm 22 pF), 11.36 kHz
        static constexpr double noisePoleHz = 1.0 / (2.0 * 3.141592653589793 * 1.0e6 * 22.0e-12);

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "808 Snare & Clap", 1,
                {{ { "sdLevel",  "SD Level",      0.0f, 1.0f, 0.8f, Skew::linear, Unit::percent, true },
                   { "sdTone",   "SD Tone",       0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "sdSnappy", "SD Snappy",     0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "cpLevel",  "CP/MA Level",   0.0f, 1.0f, 0.8f, Skew::linear, Unit::percent, true },
                   { "accent",   "Accent",        0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "sdTune",   "SD Tune",       0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "sdDecay",  "SD Decay",      0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "snDecay",  "Snappy Decay",  0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "cpDecay",  "CP Decay",      0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "cpSpread", "CP Spread",     0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                   { "maDecay",  "MA Decay",      0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true } }},
                paramCount };
            return d;
        }

        /** The instrument a key strikes, and whether it is the pair's other one (maracas); -1 for no instrument. */
        static constexpr int instrumentFor(int note, bool& other) noexcept
        {
            other = false;
            switch (note)
            {
                case 38: case 40: return snareDrum;
                case 39:          return clapPair;
                case 70:          other = true; return clapPair;
                default:          return -1;
            }
        }

        /** A bend knob's factor: the machine's own value at 12 o'clock, `span` times either way at the ends. */
        static double bendScale(double turn, double span) noexcept { return std::pow(span, 2.0 * (std::clamp(turn, 0.0, 1.0) - 0.5)); }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        /** The circuits' rest states, solved once: call it off the audio thread. */
        static void warmUp() noexcept
        {
            Tr808Snare::warmUp();
            Tr808Clap::warmUp();
        }

        /** Every instrument starts each hit from rest, silent by itself: the voice may open its amplifier at once (D251). */
        static bool startsFromRest(const float*) noexcept { return true; }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            rngStart = ((memory.seed() + 1u) * 2654435761u) ^ 0x5DC9u;
            if (rngStart == 0)
                rngStart = 1;
            rng = rngStart;
            snare.prepare(sampleRate);
            clap.prepare(sampleRate);
            noiseSigma = noiseDensity * std::sqrt(0.5 * sampleRate);
            // C202's pole by the bilinear transform, prewarped to it.
            const double k = std::tan(3.141592653589793 * noisePoleHz / sampleRate);
            noiseB = k / (1.0 + k);
            noiseA = (k - 1.0) / (k + 1.0);
            reset();
        }

        /** Back to the random numbers it was prepared with: a defined start (D222). */
        void reseed() noexcept { rng = rngStart; }

        void reset() noexcept
        {
            // The clap's switch back on the clap, as a fresh kit.
            clap.setMaracas(false);
            snare.rest();
            clap.rest();
            pending.fill(false);
            pendingOther.fill(false);
            pendingVelocity.fill(1.0f);
            running.fill(false);
            level.fill(0.0);
            sinceHit.fill(1.0e9);
            noiseState = 0.0;
            noiseLast = 0.0;
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
                    if (k == snareDrum)
                    {
                        if (! running[i])
                            snare.rest(); // rung down: its next hit starts from rest
                        snare.trigger(volts);
                    }
                    else
                    {
                        if (! running[i])
                            clap.rest();
                        clap.select(pendingOther[i]);
                        clap.trigger(volts);
                    }
                    running[i] = true;
                    sinceHit[i] = 0.0;
                }

                // The white noise bus, while an instrument may pass it.
                double white = 0.0;
                if (running[snareDrum] || running[clapPair])
                    white = nextWhite();

                double sum = 0.0;
                for (int k = 0; k < instrumentCount; ++k)
                {
                    const auto i = static_cast<std::size_t>(k);
                    if (! running[i])
                        continue;
                    double y = k == snareDrum ? snare.process(white) : clap.process(white);
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
        /** The clap's circuit, for fitting its parts (D258). */
        Tr808Clap& clapForTesting() noexcept { return clap; }
        [[nodiscard]] const Tr808Snare& snareForTesting() const noexcept { return snare; }
        [[nodiscard]] const Tr808Clap& clapForTesting() const noexcept { return clap; }

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
            if (moved(sdLevel))
                snare.setLevel(knob(sdLevel));
            if (moved(sdTone))
                snare.setTone(knob(sdTone));
            if (moved(sdSnappy))
                snare.setSnappy(knob(sdSnappy));
            if (moved(cpLevel))
                clap.setLevel(knob(cpLevel));
            if (moved(sdTune))
                snare.setTuneScale(bendScale(knob(sdTune), 2.0));
            if (moved(sdDecay))
                snare.setDecayScale(bendScale(knob(sdDecay), 4.0));
            if (moved(snappyDecay))
                snare.setSnappyDecayScale(bendScale(knob(snappyDecay), 4.0));
            if (moved(cpDecay))
                clap.setDecayScale(bendScale(knob(cpDecay), 4.0));
            if (moved(cpSpread))
                clap.setSpreadScale(bendScale(knob(cpSpread), 2.0));
            if (moved(maDecay))
                clap.setMaracasDecayScale(bendScale(knob(maDecay), 4.0));
        }

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

        /** One sample of the white noise bus, volts: Gaussian white noise (four uniforms summed) through C202's pole. */
        double nextWhite() noexcept
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
            // A one-pole low-pass, (1 + z^-1) b / (1 + a z^-1).
            const double y = noiseB * (white + noiseLast) - noiseA * noiseState;
            noiseLast = white;
            noiseState = y;
            return y;
        }

        double sampleRate = 96000.0;
        Tr808Snare snare;
        Tr808Clap clap;
        std::array<bool, instrumentCount> pending {}, pendingOther {}, running {};
        std::array<float, instrumentCount> pendingVelocity {};
        std::array<double, instrumentCount> level {}, sinceHit {};
        std::array<float, paramCount> applied {};
        int currentNote = 60;
        double noiseSigma = 0.0, noiseB = 0.0, noiseA = 0.0, noiseState = 0.0, noiseLast = 0.0;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1, rngStart = 1;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<Tr808SnareClapGenerator>);
}
