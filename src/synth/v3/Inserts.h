#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "synth/GlobalFxRack.h"
#include "GrainInsert.h"
#include "Modulation.h"
#include "Module.h"

/*
    V3 insert slots (DESIGN_V3.md section 8; decision D81): three per part, between the part's
    voices and its place in the mix. A slot holds one effect at a time, any of the ten the master
    rack has. It runs that effect's own code (a rack of its own with only that stage switched
    on), so an insert sounds exactly as the same effect does in the master section, with the
    same smoothing when a value moves or the effect is switched. An eleventh type, Grain, exists
    only as an insert: the granular effect on a part (GrainInsert.h, decision D82).

    A slot has a type and eight generic values, named and ranged by the type's descriptor, like a
    generator or processor slot. Values arrive normalised 0..1. A synced delay and the pump take
    their time from the tempo and the beat the engine passes in. An empty slot costs nothing.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int insertSlotCount = 3;

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
        inline constexpr int count = 12; // new types only at the end: the index is saved in patches
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
        }};
        return table[static_cast<std::size_t>(std::clamp(typeIndex, 0, insertType::count - 1))];
    }

    [[nodiscard]] inline SlotTypeTable insertTypeTable() noexcept { return { insertType::count, &insertDescriptor }; }

    /** One insert slot as the patch sets it. Values are normalised 0..1. */
    struct InsertSettings
    {
        int type = insertType::off;
        std::array<float, maxSlotParameters> p {};
    };

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
        void prepare(double sampleRate, int maxBlockSize)
        {
            rack.prepare(sampleRate, std::max(1, maxBlockSize), 2);
            grain.prepare(sampleRate);
            lastType = insertType::off;
        }

        /** Silence: every tail gone. */
        void clear() noexcept
        {
            rack.clear();
            grain.clear();
        }

        [[nodiscard]] const GrainInsert& grainForTesting() const noexcept { return grain; }

        [[nodiscard]] int currentType() const noexcept { return lastType; }

        void process(juce::AudioBuffer<float>& stereo, const InsertSettings& settings, const InsertContext& context) noexcept
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
                lastType = type;
                return;
            }
            if (lastType == insertType::grain)
                grain.clear();

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
            if (type == insertType::off)
                rack.clear(); // the effect that was here has had its last block
            lastType = type;
        }

    private:
        coolsynth::synth::GlobalFxRack rack;
        GrainInsert grain;
        int lastType = insertType::off;
    };
}
