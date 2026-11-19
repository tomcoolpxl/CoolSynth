#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

#include <juce_audio_basics/juce_audio_basics.h>

#include "synth/GlobalFxRack.h"
#include "GrainInsert.h"
#include "Modulation.h"
#include "Module.h"
#include "PitchInsert.h"
#include "RoomInsert.h"
#include "StutterInsert.h"

/*
    V3 insert slots (docs/design/DESIGN_V3.md section 8; decision D81): six per part since D194, between the part's
    voices and its place in the mix. A slot holds one effect at a time, any of the ten the master
    rack has. It runs that effect's own code (a rack of its own with only that stage switched
    on), so an insert sounds exactly as the same effect does in the master section, with the
    same smoothing when a value moves or the effect is switched. Three more types have code of
    their own: Grain, the granular effect on a part (GrainInsert.h, decision D82), Room, a small
    room around an instrument (RoomInsert.h, decision D108), Pitch, a pitch shifter with a
    delay (PitchInsert.h, decision D205), and Stutter, slices caught and repeated on the beat
    (StutterInsert.h, decision D220).

    A slot has a type and twelve generic values, named and ranged by the type's descriptor, like a
    generator or processor slot. Values arrive normalised 0..1. A synced delay and the pump take
    their time from the tempo and the beat the engine passes in. An empty slot costs nothing.

    The master section is six of the same slots, F1..F6, in a chain after the parts are mixed
    (decision D135): any type in any slot, in any order. The fixed rack of ten that was the master
    section before is now only the code each slot runs.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int insertSlotCount = 6; // a layer's own effects: six since D194 (were three), a whole sound's

    /** The master effect slots F1..F6 (docs/design/DESIGN_V3.md section 8, decision D135): the same slot and the same types as an
        insert, in a chain after the parts are mixed, before the master gain. */
    inline constexpr int masterSlotCount = 6;

    namespace insertType
    {
        inline constexpr int off = 0;
        inline constexpr int drive = 1;
        inline constexpr int lofi = 2;
        inline constexpr int phaser = 3;
        inline constexpr int flanger = 4;
        inline constexpr int chorus = 5;
        inline constexpr int delay = 6;
        inline constexpr int reverb = 7;
        inline constexpr int compressor = 8;
        inline constexpr int eq = 9;
        inline constexpr int pump = 10;
        inline constexpr int grain = 11;
        inline constexpr int room = 12;
        inline constexpr int pitch = 13;
        inline constexpr int stutter = 14;
        inline constexpr int count = 15; // new types only at the end: the index is saved in patches
    }

    /** Name and parameters of an insert type. The ranges are the master rack's. */
    [[nodiscard]] inline const ModuleDescriptor& insertDescriptor(int typeIndex)
    {
        static constexpr const char* divisions = "1/32|1/16T|1/16|1/16.|1/8T|1/8|1/8.|1/4T|1/4|1/4.|1/2T|1/2|1/2.|1 Bar";
        static const std::array<ModuleDescriptor, insertType::count> table {{
            { "Off", 0, {}, 0 },
            { "Drive", 1,
              {{ { "amount", "Amount", 0.0f, 1.0f, 0.3f, Skew::linear, Unit::percent, true },
                 { "mix",    "Mix",    0.0f, 1.0f, 1.0f, Skew::linear, Unit::percent, true } }}, 2 },
            { "LoFi", 1,
              {{ { "wow",       "Wow",       0.0f,    1.0f,     0.2f,     Skew::linear,      Unit::percent, true },
                 { "flutter",   "Flutter",   0.0f,    1.0f,     0.1f,     Skew::linear,      Unit::percent, true },
                 { "hiss",      "Hiss",      0.0f,    1.0f,     0.0f,     Skew::linear,      Unit::percent, true },
                 { "crackle",   "Crackle",   0.0f,    1.0f,     0.0f,     Skew::linear,      Unit::percent, true },
                 { "bandwidth", "Bandwidth", 0.0f,    1.0f,     0.8f,     Skew::linear,      Unit::percent, true },
                 { "bits",      "Bits",      1.0f,    16.0f,    16.0f,    Skew::linear,      Unit::generic, true },
                 { "rate",      "Rate",      1000.0f, 48000.0f, 48000.0f, Skew::logarithmic, Unit::hertz,   true },
                 { "mix",       "Mix",       0.0f,    1.0f,     1.0f,     Skew::linear,      Unit::percent, true } }}, 8 },
            { "Phaser", 1,
              {{ { "rate",  "Rate",  0.05f, 8.0f, 0.5f, Skew::logarithmic, Unit::hertz,   true },
                 { "depth", "Depth", 0.0f,  1.0f, 0.6f, Skew::linear,      Unit::percent, true } }}, 2 },
            { "Flanger", 1,
              {{ { "rate",     "Rate",     0.05f,  5.0f,  0.3f, Skew::logarithmic, Unit::hertz,   true },
                 { "depth",    "Depth",    0.0f,   1.0f,  0.7f, Skew::linear,      Unit::percent, true },
                 { "feedback", "Feedback", -0.95f, 0.95f, 0.5f, Skew::linear,      Unit::generic, true },
                 { "mix",      "Mix",      0.0f,   1.0f,  0.5f, Skew::linear,      Unit::percent, true } }}, 4 },
            { "Chorus", 1,
              {{ { "rate",  "Rate",  0.05f, 10.0f, 0.6f, Skew::logarithmic, Unit::hertz,   true },
                 { "depth", "Depth", 0.0f,  1.0f,  0.5f, Skew::linear,      Unit::percent, true },
                 { "mix",   "Mix",   0.0f,  1.0f,  0.5f, Skew::linear,      Unit::percent, true } }}, 3 },
            { "Delay", 2,
              {{ { "time",     "Time",     1.0f, 1000.0f, 250.0f, Skew::logarithmic, Unit::milliseconds, true },
                 { "feedback", "Feedback", 0.0f, 0.85f,   0.3f,   Skew::linear,      Unit::generic,      true },
                 { "mix",      "Mix",      0.0f, 1.0f,    0.3f,   Skew::linear,      Unit::percent,      true },
                 { "sync",     "Sync",     0.0f, 1.0f,    0.0f,   Skew::linear,      Unit::choice,       false, "Off|On" },
                 { "division", "Division", 0.0f, 13.0f,   5.0f,   Skew::linear,      Unit::choice,       false, divisions },
                 { "mode",     "Mode",     0.0f, 1.0f,    0.0f,   Skew::linear,      Unit::choice,       false, "Tape|Ping-Pong" } }}, 6 },
            { "Reverb", 3,
              {{ { "size",       "Size",       0.0f,  1.0f,    0.4f,  Skew::linear,      Unit::percent,      true },
                 { "damping",    "Damping",    0.0f,  1.0f,    0.5f,  Skew::linear,      Unit::percent,      true },
                 { "mix",        "Mix",        0.0f,  1.0f,    0.3f,  Skew::linear,      Unit::percent,      true },
                 { "predelay",   "Pre-Delay",  0.0f,  250.0f,  0.0f,  Skew::linear,      Unit::milliseconds, true },
                 { "lowcut",     "Low Cut",    20.0f, 1000.0f, 20.0f, Skew::logarithmic, Unit::hertz,        true },
                 { "modulation", "Modulation", 0.0f,  1.0f,    0.5f,  Skew::linear,      Unit::percent,      true },
                 { "shimmer",    "Shimmer",    0.0f,  1.0f,    0.0f,  Skew::linear,      Unit::percent,      true } }}, 7 },
            { "Compressor", 1,
              {{ { "amount", "Amount", 0.0f, 1.0f, 0.3f, Skew::linear, Unit::percent, true },
                 { "mix",    "Mix",    0.0f, 1.0f, 1.0f, Skew::linear, Unit::percent, true } }}, 2 },
            { "EQ", 1,
              {{ { "low",  "Low",  -12.0f, 12.0f, 0.0f, Skew::linear, Unit::decibels, true },
                 { "tilt", "Tilt", -6.0f,  6.0f,  0.0f, Skew::linear, Unit::decibels, true },
                 { "high", "High", -12.0f, 12.0f, 0.0f, Skew::linear, Unit::decibels, true } }}, 3 },
            { "Pump", 1,
              {{ { "depth",    "Depth",    0.0f,  1.0f,  0.6f, Skew::linear, Unit::percent, true },
                 { "division", "Division", 0.0f,  13.0f, 8.0f, Skew::linear, Unit::choice,  false, divisions },
                 { "release",  "Release",  0.05f, 1.0f,  0.5f, Skew::linear, Unit::percent, true } }}, 3 },
            { "Grain", 2,
              {{ { "size",     "Size",     10.0f,  500.0f,  120.0f, Skew::logarithmic, Unit::milliseconds, true },
                 { "density",  "Density",  1.0f,   80.0f,   20.0f,  Skew::logarithmic, Unit::hertz,        true },
                 { "position", "Position", 0.0f,   3000.0f, 250.0f, Skew::linear,      Unit::milliseconds, true },
                 { "spray",    "Spray",    0.0f,   1.0f,    0.2f,   Skew::linear,      Unit::percent,      true },
                 { "pitch",    "Pitch",    -24.0f, 24.0f,   0.0f,   Skew::linear,      Unit::semitones,    true },
                 { "spread",   "Spread",   0.0f,   1.0f,    0.5f,   Skew::linear,      Unit::percent,      true },
                 { "freeze",   "Freeze",   0.0f,   1.0f,    0.0f,   Skew::linear,      Unit::choice,       false, "Off|On" },
                 { "mix",      "Mix",      0.0f,   1.0f,    0.5f,   Skew::linear,      Unit::percent,      true } }}, 8 },
            { "Room", 2,
              {{ { "size",    "Size",    0.0f, 1.0f, 0.4f, Skew::linear, Unit::percent, true },
                 { "damping", "Damping", 0.0f, 1.0f, 0.5f, Skew::linear, Unit::percent, true },
                 { "width",   "Width",   0.0f, 1.0f, 1.0f, Skew::linear, Unit::percent, true },
                 { "mix",     "Mix",     0.0f, 1.0f, 0.3f, Skew::linear, Unit::percent, true } }}, 4 },
            // Pages of four: Pitch, Delay, Mode (D205).
            { "Pitch", 2,
              {{ { "pitch",     "Pitch",     -24.0f,  24.0f,   12.0f, Skew::linear,      Unit::semitones,    true },
                 { "fine",      "Fine",      -100.0f, 100.0f,  0.0f,  Skew::linear,      Unit::cents,        true },
                 { "spread",    "Spread",    0.0f,    50.0f,   0.0f,  Skew::linear,      Unit::cents,        true },
                 { "mix",       "Mix",       0.0f,    1.0f,    0.5f,  Skew::linear,      Unit::percent,      true },
                 { "delay",     "Delay",     0.0f,    1000.0f, 0.0f,  Skew::squared,     Unit::milliseconds, true },
                 { "sync",      "Sync",      0.0f,    1.0f,    0.0f,  Skew::linear,      Unit::choice,       false, "Off|On" },
                 { "division",  "Division",  0.0f,    13.0f,   6.0f,  Skew::linear,      Unit::choice,       false, divisions },
                 { "feedback",  "Feedback",  0.0f,    0.9f,    0.0f,  Skew::linear,      Unit::percent,      true },
                 { "mode",      "Mode",      0.0f,    1.0f,    0.0f,  Skew::linear,      Unit::choice,       false, "Heads|Spectral" },
                 { "window",    "Window",    10.0f,   200.0f,  50.0f, Skew::logarithmic, Unit::milliseconds, true },
                 { "character", "Character", 0.0f,    1.0f,    0.25f, Skew::linear,      Unit::percent,      true } }}, 11 },
            // Pages of four: when it catches, what it catches, what each repeat does (D220). Its defaults are Beat
            // Repeat's: every bar on beat three, sixteenths, for a quarter note.
            { "Stutter", 2,
              {{ { "interval",  "Interval",  0.0f, 5.0f,  3.0f,  Skew::linear, Unit::choice,    false, StutterInsert::intervalNames },
                 { "offset",    "Offset",    0.0f, 15.0f, 8.0f,  Skew::linear, Unit::choice,    false,
                   "0|1/16|2/16|3/16|4/16|5/16|6/16|7/16|8/16|9/16|10/16|11/16|12/16|13/16|14/16|15/16" },
                 { "chance",    "Chance",    0.0f, 1.0f,  1.0f,  Skew::linear, Unit::percent,   true },
                 { "hold",      "Hold",      0.0f, 1.0f,  0.0f,  Skew::linear, Unit::choice,    true,  "Off|On" },
                 { "grid",      "Grid",      0.0f, 8.0f,  4.0f,  Skew::linear, Unit::choice,    false, StutterInsert::gridNames },
                 { "gate",      "Gate",      0.0f, 8.0f,  3.0f,  Skew::linear, Unit::choice,    false, StutterInsert::gateNames },
                 { "variation", "Variation", 0.0f, 1.0f,  0.0f,  Skew::linear, Unit::percent,   true },
                 { "roll",      "Roll",      0.0f, 1.0f,  0.0f,  Skew::linear, Unit::percent,   true },
                 { "pitch",     "Pitch",     0.0f, 12.0f, 0.0f,  Skew::linear, Unit::semitones, true },
                 { "decay",     "Decay",     0.0f, 1.0f,  0.0f,  Skew::linear, Unit::percent,   true },
                 { "reverse",   "Reverse",   0.0f, 2.0f,  0.0f,  Skew::linear, Unit::choice,    false, "Off|All|Alternate" },
                 { "mode",      "Mode",      0.0f, 2.0f,  0.0f,  Skew::linear, Unit::choice,    false, "Insert|Mix|Gate" } }}, 12 },
        }};
        return table[static_cast<std::size_t>(std::clamp(typeIndex, 0, insertType::count - 1))];
    }

    [[nodiscard]] inline SlotTypeTable insertTypeTable() noexcept { return { insertType::count, &insertDescriptor }; }

    /** One insert slot as the patch sets it. Values are normalised 0..1. */
    struct InsertSettings
    {
        int type = insertType::off;
        std::array<float, maxSlotParameters> p {};

        /** A slot of this type with every value at the type's default, then the given values (in the type's units). */
        [[nodiscard]] static InsertSettings of(int typeIndex, std::initializer_list<std::pair<int, float>> unitsByIndex = {}) noexcept
        {
            InsertSettings s;
            s.type = std::clamp(typeIndex, 0, insertType::count - 1);
            const auto& d = insertDescriptor(s.type);
            for (int i = 0; i < d.usedParams; ++i)
                s.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            for (const auto& [index, units] : unitsByIndex)
                if (index >= 0 && index < d.usedParams)
                    s.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], units);
            return s;
        }
    };

    /** A Pitch slot's values in the effect's own terms; a synced delay takes its time from the tempo. */
    [[nodiscard]] inline PitchInsert::Settings pitchSettings(const InsertSettings& settings, double bpm) noexcept
    {
        const auto& d = insertDescriptor(insertType::pitch);
        auto units = [&](int index)
        {
            return descriptorToUnits(d.params[static_cast<std::size_t>(index)], settings.p[static_cast<std::size_t>(index)]);
        };
        auto choice = [&](int index) { return static_cast<int>(std::lround(units(index))); };
        PitchInsert::Settings p;
        p.semitones = units(0);
        p.cents = units(1);
        p.spreadCents = units(2);
        p.mix = units(3);
        p.delayMs = choice(5) == 1 ? static_cast<float>(coolsynth::synth::delayDivisionMs(choice(6), bpm)) : units(4);
        p.feedback = units(7);
        p.mode = choice(8);
        p.windowMs = units(9);
        p.character = units(10);
        return p;
    }

    /** A Stutter slot's values in the effect's own terms (D220). */
    [[nodiscard]] inline StutterInsert::Settings stutterSettings(const InsertSettings& settings) noexcept
    {
        const auto& d = insertDescriptor(insertType::stutter);
        auto units = [&](int index)
        {
            return descriptorToUnits(d.params[static_cast<std::size_t>(index)], settings.p[static_cast<std::size_t>(index)]);
        };
        auto choice = [&](int index, int last) { return std::clamp(static_cast<int>(std::lround(units(index))), 0, last); };
        StutterInsert::Settings s;
        s.intervalBeats = StutterInsert::intervalBeats[static_cast<std::size_t>(choice(0, 5))];
        s.offsetBeats = 0.25 * choice(1, 15);
        s.chance = units(2);
        s.hold = choice(3, 1) == 1;
        s.gridIndex = choice(4, 8);
        s.gateBeats = StutterInsert::gateBeats[static_cast<std::size_t>(choice(5, 8))];
        s.variation = units(6);
        s.roll = units(7);
        s.pitchSemitones = units(8);
        s.decay = units(9);
        s.reverse = static_cast<StutterInsert::Reverse>(choice(10, 2));
        s.mode = static_cast<StutterInsert::Mode>(choice(11, 2));
        return s;
    }

    /** How long a slot keeps sounding once its input is silent: what the plugin reports to a host as its tail. */
    [[nodiscard]] inline double insertTailSeconds(const InsertSettings& settings, double bpm = 120.0) noexcept
    {
        const int type = std::clamp(settings.type, 0, insertType::count - 1);
        const auto& d = insertDescriptor(type);
        auto units = [&](int index)
        {
            return descriptorToUnits(d.params[static_cast<std::size_t>(index)], settings.p[static_cast<std::size_t>(index)]);
        };
        coolsynth::synth::DelayParametersV2 delay;
        coolsynth::synth::ReverbParametersV2 reverb;
        delay.enabled = false;
        reverb.enabled = false;
        switch (type)
        {
            case insertType::delay:
                delay.enabled = true;
                delay.timeMs = units(0);
                delay.feedback = units(1);
                delay.mix = units(2);
                if (std::lround(units(3)) == 1)
                    delay.timeMs = coolsynth::synth::delayDivisionMs(static_cast<int>(std::lround(units(4))), bpm);
                return coolsynth::synth::GlobalFxRack::estimateTailLengthSeconds(delay, reverb);
            case insertType::reverb:
                reverb.enabled = true;
                reverb.mix = units(2);
                return coolsynth::synth::GlobalFxRack::estimateTailLengthSeconds(delay, reverb);
            case insertType::room:
                return units(3) > 0.0f ? RoomInsert::reverbSeconds(units(0)) + 0.25 : 0.0;
            case insertType::grain:
                return units(7) > 0.0f ? GrainInsert::bufferSeconds + 0.5 : 0.0;
            case insertType::pitch:
                return PitchInsert::tailSeconds(pitchSettings(settings, bpm));
            case insertType::stutter:
                return StutterInsert::tailSeconds(stutterSettings(settings), bpm);
            default:
                return 0.0;
        }
    }

    /** What the effects that follow the tempo need from the engine. */
    struct InsertContext
    {
        double bpm = 120.0;
        double beatsAtStart = 0.0;   // the beat position at the block's first sample
        double beatsPerSample = 0.0;
    };

    class InsertSlot final
    {
    public:
        static constexpr double typeFadeSeconds = 0.01; // the old effect out, then the new one in (D165)

        void prepare(double sampleRate, int maxBlockSize)
        {
            rack.prepare(sampleRate, std::max(1, maxBlockSize), 2);
            grain.prepare(sampleRate);
            room.prepare(sampleRate);
            pitch.prepare(sampleRate);
            stutter.prepare(sampleRate);
            dry.setSize(2, std::max(1, maxBlockSize), false, true, true);
            fadeSamples = std::max(1, static_cast<int>(std::lround(typeFadeSeconds * sampleRate)));
            lastType = insertType::off;
            change = Change::none;
            quiet = true;
            rackUsed = grainUsed = roomUsed = pitchUsed = stutterUsed = false; // prepared silent
        }

        /**
            A type change fades instead of switching at once (D165): the old effect, with the settings it had, gives way
            to the dry signal over 10 ms, then the new one comes in over 10 ms. Its tail is gone; nothing clicks.
        */
        void process(juce::AudioBuffer<float>& stereo, const InsertSettings& settings, const InsertContext& context) noexcept
        {
            const int requested = std::clamp(settings.type, 0, insertType::count - 1);
            if (change == Change::none)
            {
                if (requested == lastType || quiet)
                {
                    // Steady, or nothing sounds through the slot: a new type starts at once (a fresh sound's first
                    // block, a slot silent since its last use), as there is nothing to click.
                    if (requested != lastType)
                        clear();
                    processType(stereo, settings, context);
                    heldSettings = settings;
                    quiet = stereo.getMagnitude(0, 0, stereo.getNumSamples()) < quietLevel
                            && stereo.getMagnitude(1, 0, stereo.getNumSamples()) < quietLevel;
                    return;
                }
                if (lastType == insertType::off)
                    beginFadeIn(requested); // nothing to fade out
                else
                {
                    change = Change::fadingOut;
                    fadeDone = 0;
                }
            }
            else if (change == Change::fadingIn && requested != fadingInType)
            {
                // Changed again while the new one came in: it goes out from where it got to.
                change = Change::fadingOut;
                fadeDone = fadeSamples - fadeDone;
            }

            const int n = stereo.getNumSamples();
            quiet = false;
            const auto& playing = change == Change::fadingOut ? heldSettings : settings;
            const float from = static_cast<float>(fadeDone) / static_cast<float>(fadeSamples);
            fadeDone = std::min(fadeSamples, fadeDone + n);
            const float to = static_cast<float>(fadeDone) / static_cast<float>(fadeSamples);
            // How much of the effect is heard: falling while the old goes out, rising while the new comes in.
            const float gainFrom = change == Change::fadingOut ? 1.0f - from : from;
            const float gainTo = change == Change::fadingOut ? 1.0f - to : to;

            // The effect hears the signal faded with it, so its delay lines fill (or empty) gradually and nothing it
            // delays starts or stops mid-waveform. Its output fades too, both ways: going out, so its memory is silent
            // when it is cleared; coming in, so what it makes of its own (LoFi's hiss and crackle) rises instead of
            // starting at full level. The dry signal takes the rest, 1 - s * s, so what the effect does to the signal
            // crossfades without a dip. The gain s is a raised cosine (D171): it starts and ends flat, so the fade has
            // no corner where it begins, ends, or where the old effect's fade meets the new one's.
            const float step = (gainTo - gainFrom) / static_cast<float>(n);
            const auto smooth = [](float g) noexcept
            {
                return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * std::clamp(g, 0.0f, 1.0f));
            };
            for (int ch = 0; ch < 2; ++ch)
            {
                dry.copyFrom(ch, 0, stereo, ch, 0, n);
                float* in = stereo.getWritePointer(ch);
                float g = gainFrom;
                for (int i = 0; i < n; ++i)
                {
                    g += step;
                    in[i] *= smooth(g);
                }
            }
            processType(stereo, playing, context);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* wet = stereo.getWritePointer(ch);
                const float* clean = dry.getReadPointer(ch);
                float g = gainFrom;
                for (int i = 0; i < n; ++i)
                {
                    g += step;
                    const float s = smooth(g);
                    wet[i] = clean[i] * (1.0f - s * s) + wet[i] * s;
                }
            }

            if (fadeDone < fadeSamples)
                return;
            if (change == Change::fadingOut)
            {
                clear(); // the old effect is out: its memory silent for the next
                lastType = insertType::off;
                if (requested == insertType::off)
                    change = Change::none;
                else
                    beginFadeIn(requested);
            }
            else
            {
                change = Change::none;
                heldSettings = settings;
            }
        }

        /** Silence: every tail gone. Only what played since the last clear is wiped: All Sound Off cleared every
            effect of all 18 slots, about 38 MB at 48 kHz, inside one audio block (D160). */
        void clear() noexcept
        {
            if (rackUsed)
                rack.clear();
            if (grainUsed)
                grain.clear();
            if (roomUsed)
                room.clear();
            if (pitchUsed)
                pitch.clear();
            if (stutterUsed)
                stutter.clear();
            rackUsed = grainUsed = roomUsed = pitchUsed = stutterUsed = false;
            quiet = true;
        }

        [[nodiscard]] const GrainInsert& grainForTesting() const noexcept { return grain; }
        [[nodiscard]] const StutterInsert& stutterForTesting() const noexcept { return stutter; }

        [[nodiscard]] int currentType() const noexcept { return lastType; }

    private:
        enum class Change : std::uint8_t { none, fadingOut, fadingIn };

        void beginFadeIn(int type) noexcept
        {
            change = Change::fadingIn;
            fadingInType = type;
            fadeDone = 0;
        }

        /** The slot as one type, at once: what every block did before type changes faded. */
        void processType(juce::AudioBuffer<float>& stereo, const InsertSettings& settings, const InsertContext& context) noexcept
        {
            const int type = std::clamp(settings.type, 0, insertType::count - 1);
            if (type == insertType::off && lastType == insertType::off)
                return;

            const auto& d = insertDescriptor(type);
            auto units = [&](int index)
            {
                return descriptorToUnits(d.params[static_cast<std::size_t>(index)], settings.p[static_cast<std::size_t>(index)]);
            };
            auto choice = [&](int index) { return static_cast<int>(std::lround(units(index))); };

            // The granular effect is its own code; the rack has no such stage.
            if (type == insertType::grain)
            {
                if (lastType != insertType::grain)
                    clear(); // whatever was here before is cut, and the recording starts empty
                GrainInsert::Settings g;
                g.sizeMs = units(0);
                g.density = units(1);
                g.positionMs = units(2);
                g.spray = units(3);
                g.pitchSemitones = units(4);
                g.spread = units(5);
                g.freeze = choice(6) == 1;
                g.mix = units(7);
                grain.process(stereo, g);
                grainUsed = true;
                lastType = type;
                return;
            }
            if (lastType == insertType::grain)
            {
                grain.clear();
                grainUsed = false;
            }

            // So is the room.
            if (type == insertType::room)
            {
                if (lastType != insertType::room)
                    clear();
                RoomInsert::Settings r;
                r.size = units(0);
                r.damping = units(1);
                r.width = units(2);
                r.mix = units(3);
                room.process(stereo, r);
                roomUsed = true;
                lastType = type;
                return;
            }
            if (lastType == insertType::room)
            {
                room.clear();
                roomUsed = false;
            }

            // And the pitch shifter.
            if (type == insertType::pitch)
            {
                if (lastType != insertType::pitch)
                    clear();
                pitch.process(stereo, pitchSettings(settings, context.bpm));
                pitchUsed = true;
                lastType = type;
                return;
            }
            if (lastType == insertType::pitch)
            {
                pitch.clear();
                pitchUsed = false;
            }

            // And the stutter, on the beat the engine passes in.
            if (type == insertType::stutter)
            {
                if (lastType != insertType::stutter)
                    clear();
                stutter.process(stereo, stutterSettings(settings), context.beatsAtStart, context.beatsPerSample);
                stutterUsed = true;
                lastType = type;
                return;
            }
            if (lastType == insertType::stutter)
            {
                stutter.clear();
                stutterUsed = false;
            }

            coolsynth::synth::DriveParametersV2 drive;
            coolsynth::synth::PhaserParametersV2 phaser;
            coolsynth::synth::ChorusParametersV2 chorus;
            coolsynth::synth::DelayParametersV2 delay;
            coolsynth::synth::ReverbParametersV2 reverb;
            coolsynth::synth::CompressorParametersV2 compressor;
            coolsynth::synth::ExtraFxParameters extra;

            switch (type)
            {
                case insertType::drive:
                    drive.enabled = true;
                    drive.amount = units(0);
                    drive.mix = units(1);
                    break;
                case insertType::lofi:
                    extra.lofi.enabled = true;
                    extra.lofi.wow = units(0);
                    extra.lofi.flutter = units(1);
                    extra.lofi.hiss = units(2);
                    extra.lofi.crackle = units(3);
                    extra.lofi.bandwidth = units(4);
                    extra.lofi.bits = units(5);
                    extra.lofi.rateHz = units(6);
                    extra.lofi.mix = units(7);
                    break;
                case insertType::phaser:
                    phaser.enabled = true;
                    phaser.rateHz = units(0);
                    phaser.depth = units(1);
                    break;
                case insertType::flanger:
                    extra.flanger.enabled = true;
                    extra.flanger.rateHz = units(0);
                    extra.flanger.depth = units(1);
                    extra.flanger.feedback = units(2);
                    extra.flanger.mix = units(3);
                    break;
                case insertType::chorus:
                    chorus.enabled = true;
                    chorus.rateHz = units(0);
                    chorus.depth = units(1);
                    chorus.mix = units(2);
                    break;
                case insertType::delay:
                    delay.enabled = true;
                    delay.timeMs = units(0);
                    delay.feedback = units(1);
                    delay.mix = units(2);
                    delay.sync = choice(3) == 1;
                    delay.division = choice(4);
                    delay.pingPong = choice(5) == 1;
                    if (delay.sync)
                        delay.timeMs = coolsynth::synth::delayDivisionMs(delay.division, context.bpm);
                    break;
                case insertType::reverb:
                    reverb.enabled = true;
                    reverb.size = units(0);
                    reverb.damping = units(1);
                    reverb.mix = units(2);
                    reverb.preDelayMs = units(3);
                    reverb.lowCutHz = units(4);
                    reverb.modulation = units(5);
                    reverb.shimmer = units(6);
                    break;
                case insertType::compressor:
                    compressor.enabled = true;
                    compressor.amount = units(0);
                    compressor.mix = units(1);
                    break;
                case insertType::eq:
                    extra.eq.enabled = true;
                    extra.eq.lowDb = units(0);
                    extra.eq.tiltDb = units(1);
                    extra.eq.highDb = units(2);
                    break;
                case insertType::pump:
                {
                    extra.pump.enabled = true;
                    extra.pump.depth = units(0);
                    extra.pump.division = choice(1);
                    extra.pump.release = units(2);
                    const double beats = coolsynth::synth::delayDivisions[static_cast<std::size_t>(
                        std::clamp(extra.pump.division, 0, static_cast<int>(coolsynth::synth::delayDivisions.size()) - 1))].beats;
                    const double phase = context.beatsAtStart / beats;
                    extra.pump.phaseAtStart = phase - std::floor(phase);
                    extra.pump.phasePerSample = context.beatsPerSample / beats;
                    break;
                }
                default:
                    break;
            }

            rack.process(stereo, drive, phaser, chorus, delay, reverb, compressor, extra);
            rackUsed = true;
            if (type == insertType::off)
            {
                rack.clear(); // the effect that was here has had its last block
                rackUsed = false;
            }
            lastType = type;
        }

        coolsynth::synth::GlobalFxRack rack;
        GrainInsert grain;
        RoomInsert room;
        PitchInsert pitch;
        StutterInsert stutter;
        int lastType = insertType::off;
        Change change = Change::none;
        static constexpr float quietLevel = 1.0e-6f; // -120 dB
        bool quiet = true;                 // the slot's last block was silent: a type may switch at once
        int fadingInType = insertType::off;
        int fadeSamples = 480, fadeDone = 0;
        InsertSettings heldSettings {};     // what the type playing was last given: it fades out with these
        juce::AudioBuffer<float> dry;       // the block as it came in, during a change
        bool rackUsed = false, grainUsed = false, roomUsed = false, pitchUsed = false, stutterUsed = false; // played since its last clear (D160)
    };
}
