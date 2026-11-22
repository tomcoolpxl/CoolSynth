#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "Tr808Cowbell.h"
#include "Tr808Cymbal.h"
#include "Tr808Hats.h"
#include "Tr808MetalBank.h"

/*
    The 808 Metal generator (D254, D255): the TR-808's cowbell, cymbal, open and closed hi-hat, each its own circuit
    (Tr808Cowbell.h, Tr808Cymbal.h, Tr808Hats.h) on the voicing board's shared parts (Tr808MetalBank.h: the six
    oscillators, IC3's two bands, IC2a's narrowed trigger), in one slot that a kit voice plays (the part's Kit play
    mode, Part.h): each note strikes its instrument, the instruments ring together, the closed hat cuts the open one.

    The keys are General MIDI's with Roland's TR-8S alternates: closed hat 42 and 44, open hat 46, cymbal 49, 51 and
    57, cowbell 56. The knobs are the 808's own seven (CB Level; CY Level, Tone and Decay; OH Level and Decay; CH
    Level) with its pots' tapers (levels audio, the rest linear), then Accent, which moves each hit's trigger between
    4.7 and 11.5 V with its velocity (D258) before IC2a narrows it; Tune, the six oscillators' capacitors together, in semitones;
    CB Pitch, the cowbell's two trimmers together, in semitones from 554 and 825 Hz (both units' tuning; the service
    notes' 540 and 800 Hz lie half a semitone down) as far as both reach (-12 to +2);
    CH Decay and CB Decay, bends that scale C63 and C9 with C34 (a quarter to four times as drawn).

    The oscillators run freely across hits; a start from silence takes their places from the voice's seeded random
    numbers (D224), runs IC3 for 2 ms, and starts each VCA's coupling capacitor where the running bank holds it. The
    bank and the five VCAs on IC3's bands run while any instrument sounds; an instrument's envelope and output from
    its hit until it has rung down. The key does not tune the instruments, nor do the slot's pitch and the bend.
    Mono, two samples late like every generator.
*/
namespace coolsynth::synth::v3
{
    class Tr808MetalGenerator final
    {
    public:
        enum Param : int { cbLevel = 0, cyLevel, cyTone, cyDecay, ohLevel, ohDecay, chLevel, accent, tune, cbPitch, chDecay, cbDecay,
                           paramCount };
        enum Instrument : int { cowbell = 0, cymbal, openHat, closedHat, instrumentCount };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double circuitVoltsToLevel = 0.18; // as the 808 Drums: the circuits' volts to the slot's level
        static constexpr double silentLevel = 1.0e-4;       // -80 dB: an instrument has rung down
        static constexpr double shedFadeSeconds = 0.005;    // a shed instrument's fade: the voice's steal fade (D170)
        static constexpr double prerollSeconds = 0.002;     // IC3 settling before a start from silence

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "808 Metal", 1,
                {{ { "cbLevel", "CB Level",  0.0f,   1.0f,  0.8f, Skew::linear,      Unit::percent,   true },
                   { "cyLevel", "CY Level",  0.0f,   1.0f,  0.8f, Skew::linear,      Unit::percent,   true },
                   { "cyTone",  "CY Tone",   0.0f,   1.0f,  0.5f, Skew::linear,      Unit::percent,   true },
                   { "cyDecay", "CY Decay",  0.0f,   1.0f,  0.5f, Skew::linear,      Unit::percent,   true },
                   { "ohLevel", "OH Level",  0.0f,   1.0f,  0.8f, Skew::linear,      Unit::percent,   true },
                   { "ohDecay", "OH Decay",  0.0f,   1.0f,  0.5f, Skew::linear,      Unit::percent,   true },
                   { "chLevel", "CH Level",  0.0f,   1.0f,  0.8f, Skew::linear,      Unit::percent,   true },
                   { "accent",  "Accent",    0.0f,   1.0f,  0.5f, Skew::linear,      Unit::percent,   true },
                   { "tune",    "Tune",    -12.0f,  12.0f,  0.0f, Skew::linear,      Unit::semitones, true },
                   { "cbPitch", "CB Pitch", -12.0f,  2.0f,  0.0f, Skew::linear,      Unit::semitones, true },
                   { "chDecay", "CH Decay",  0.25f,  4.0f,  1.0f, Skew::logarithmic, Unit::percent,   true },
                   { "cbDecay", "CB Decay",  0.25f,  4.0f,  1.0f, Skew::logarithmic, Unit::percent,   true } }},
                paramCount };
            return d;
        }

        /** The instrument a key strikes; -1 for no instrument. */
        static constexpr int instrumentFor(int note) noexcept
        {
            switch (note)
            {
                case 42: case 44:          return closedHat;
                case 46:                   return openHat;
                case 49: case 51: case 57: return cymbal;
                case 56:                   return cowbell;
                default:                   return -1;
            }
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        /** Every instrument's envelope starts each hit from rest, silent by itself: the voice may open its amplifier at once. */
        static bool startsFromRest(const float*) noexcept { return true; }

        /** The circuits' rest states, solved once for the process: call it off the audio thread. */
        static void warmUp() noexcept
        {
            Tr808Cowbell::warmUp();
            Tr808Cymbal::warmUp();
            Tr808Hats::warmUp();
        }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            rngStart = ((memory.seed() + 1u) * 2654435761u) ^ 0x808Eu;
            if (rngStart == 0)
                rngStart = 1;
            rng = rngStart;
            bank.prepare(sampleRate);
            bell.prepare(sampleRate);
            cymbalCircuit.prepare(sampleRate);
            hats.prepare(sampleRate);
            reset();
        }

        /** Back to the random numbers it was prepared with: a defined start (D222). */
        void reseed() noexcept { rng = rngStart; }

        void reset() noexcept
        {
            pending.fill(false);
            pendingVelocity.fill(1.0f);
            running.fill(false);
            level.fill(0.0);
            shedding.fill(false);
            shedGain.fill(1.0);
            sinceHit.fill(1.0e9);
            bankRunning = false;
            startPending = false;
            ring.fill(0.0f);
            cursor = 0;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            input = {};
            applied.fill(-1.0e9f);
            currentNote = 60;
            buffer.reset();
        }

        void noteOn(const NoteContext& note) noexcept
        {
            if (! bankRunning && ! startPending)
            {
                // A start from silence: the oscillators' places from the seeded random numbers (D254).
                for (auto& p : startPhases)
                {
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    p = static_cast<double>(rng >> 8) * (1.0 / 16777216.0);
                }
                startPending = true;
            }
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
        /** The overload's emergency (D222, D258): the quietest ringing instrument's level, -1 with none ringing. */
        [[nodiscard]] float quietestInstrumentLevel() const noexcept
        {
            const int k = quietestRinging();
            return k < 0 ? -1.0f : static_cast<float>(level[static_cast<std::size_t>(k)]);
        }
        /** ... and it fades out over the steal fade, the other instruments ringing on. */
        bool shedQuietestInstrument() noexcept
        {
            const int k = quietestRinging();
            if (k < 0)
                return false;
            shedding[static_cast<std::size_t>(k)] = true;
            return true;
        }
        [[nodiscard]] bool silentUntilNextNote() const noexcept { return ! anyPending() && ! anyRunning(); }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            configure(params);
            if (startPending)
                startBank();
            const float accentAmount = juce::jlimit(0.0f, 1.0f, params.p[accent]);
            const double follow = std::exp(-1.0 / (0.02 * sampleRate)); // each instrument's level follower, for its end
            const double settleSamples = 0.05 * sampleRate;             // a hit runs at least this long
            const double shedStep = 1.0 / (shedFadeSeconds * sampleRate); // a shed instrument's fade a sample
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
                    const double trig = Tr808BassDrum::triggerFor(static_cast<double>(accentAmount * pendingVelocity[i]));
                    const double volts = buffer.volts(trig);
                    if (! running[i])
                    {
                        // From rest: its envelope and output at rest (its VCAs have run with the bank).
                        if (k == cowbell)
                            bell.rest();
                        else if (k == cymbal)
                            cymbalCircuit.restEnvelope();
                        else
                            hats.restEnvelope(k == openHat ? Tr808Hats::open : Tr808Hats::closed);
                    }
                    if (k == cowbell)
                        bell.trigger(volts);
                    else if (k == cymbal)
                        cymbalCircuit.trigger(volts);
                    else
                        hats.trigger(k == openHat ? Tr808Hats::open : Tr808Hats::closed, volts);
                    running[i] = true;
                    shedding[i] = false;
                    shedGain[i] = 1.0;
                    bankRunning = true;
                    sinceHit[i] = 0.0;
                }

                double sum = 0.0;
                if (bankRunning)
                {
                    bank.process();
                    if (running[cymbal])
                        cymbalCircuit.processEnvelope();
                    if (running[openHat])
                        hats.processEnvelope(Tr808Hats::open);
                    if (running[closedHat])
                        hats.processEnvelope(Tr808Hats::closed);
                    cymbalCircuit.processVcas(bank.high(), bank.low());
                    hats.processVcas(bank.high());
                    for (int k = 0; k < instrumentCount; ++k)
                    {
                        const auto i = static_cast<std::size_t>(k);
                        if (! running[i])
                            continue;
                        double y = 0.0;
                        if (k == cowbell)
                            y = bell.process(bank.oscillator(0), bank.oscillator(1));
                        else if (k == cymbal)
                            y = cymbalCircuit.processMix();
                        else
                            y = hats.processOutput(k == openHat ? Tr808Hats::open : Tr808Hats::closed);
                        y *= circuitVoltsToLevel;
                        if (shedding[i])
                        {
                            // Shed by the overload's emergency (D258): out over the steal fade; its next hit starts
                            // from rest, as after it rings down.
                            shedGain[i] = std::max(0.0, shedGain[i] - shedStep);
                            y *= shedGain[i];
                            if (shedGain[i] <= 0.0)
                            {
                                shedding[i] = false;
                                running[i] = false;
                                continue;
                            }
                        }
                        level[i] = std::max(std::abs(y), level[i] * follow);
                        sinceHit[i] += 1.0;
                        if (level[i] <= silentLevel && sinceHit[i] > settleSamples)
                            running[i] = false; // rung down: it rests until its next hit
                        sum += y;
                    }
                    bankRunning = anyRunning() || anyPending();
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
        [[nodiscard]] bool bankRunningForTesting() const noexcept { return bankRunning; }
        [[nodiscard]] const Tr808MetalBank& bankForTesting() const noexcept { return bank; }

    private:
        /** The knobs to the circuits, once a block; a part is set again only when its knob has moved. */
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
            const auto raw = [&params](int index) { return static_cast<double>(params.p[static_cast<std::size_t>(index)]); };
            if (moved(cbLevel))
                bell.setLevel(knob(cbLevel));
            if (moved(cyLevel))
                cymbalCircuit.setLevel(knob(cyLevel));
            if (moved(cyTone))
                cymbalCircuit.setTone(knob(cyTone));
            if (moved(cyDecay))
                cymbalCircuit.setDecay(knob(cyDecay));
            if (moved(ohLevel))
                hats.setLevel(Tr808Hats::open, knob(ohLevel));
            if (moved(ohDecay))
                hats.setOpenDecay(knob(ohDecay));
            if (moved(chLevel))
                hats.setLevel(Tr808Hats::closed, knob(chLevel));
            const bool tuneMoved = moved(tune);
            if (moved(cbPitch) || tuneMoved)
                bank.setTiming(std::pow(2.0, -juce::jlimit(-12.0, 12.0, raw(tune)) / 12.0), raw(cbPitch));
            if (moved(chDecay))
                hats.setClosedDecayScale(juce::jlimit(0.25, 4.0, raw(chDecay)));
            if (moved(cbDecay))
                bell.setDecayScale(juce::jlimit(0.25, 4.0, raw(cbDecay)));
        }

        /** A start from silence: the bank at its drawn places, IC3 run in, every circuit at rest. */
        void startBank() noexcept
        {
            startPending = false;
            bank.start(startPhases);
            const int preroll = static_cast<int>(std::lround(prerollSeconds * sampleRate));
            for (int s = 0; s < preroll; ++s)
                bank.process();
            bell.rest();
            cymbalCircuit.rest();
            hats.rest();
            running.fill(false);
            level.fill(0.0);
            shedding.fill(false);
            shedGain.fill(1.0);
        }

        void hit(int note, float velocity) noexcept
        {
            const int k = instrumentFor(note);
            if (k < 0)
                return;
            const auto i = static_cast<std::size_t>(k);
            pending[i] = true;
            pendingVelocity[i] = juce::jlimit(0.0f, 1.0f, velocity);
        }

        [[nodiscard]] bool anyPending() const noexcept { return std::any_of(pending.begin(), pending.end(), [](bool p) { return p; }); }
        [[nodiscard]] bool anyRunning() const noexcept { return std::any_of(running.begin(), running.end(), [](bool r) { return r; }); }
        [[nodiscard]] int quietestRinging() const noexcept
        {
            int best = -1;
            for (int k = 0; k < instrumentCount; ++k)
            {
                const auto i = static_cast<std::size_t>(k);
                if (running[i] && ! shedding[i] && ! pending[i] && (best < 0 || level[i] < level[static_cast<std::size_t>(best)]))
                    best = k;
            }
            return best;
        }

        double sampleRate = 96000.0;
        Tr808MetalBank bank;
        Tr808MetalTriggerBuffer buffer;
        Tr808Cowbell bell;
        Tr808Cymbal cymbalCircuit;
        Tr808Hats hats;
        std::array<bool, instrumentCount> pending {}, running {};
        std::array<float, instrumentCount> pendingVelocity {};
        std::array<double, instrumentCount> level {}, sinceHit {};
        std::array<bool, instrumentCount> shedding {}; // the overload's emergency fades it out (D258)
        std::array<double, instrumentCount> shedGain {};
        std::array<double, Tr808MetalBank::oscillatorCount> startPhases {};
        std::array<float, paramCount> applied {};
        bool bankRunning = false, startPending = false;
        int currentNote = 60;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::uint32_t rng = 1, rngStart = 1;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<Tr808MetalGenerator>);
}
