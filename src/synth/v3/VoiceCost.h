#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "SoundUnit.h"

/*
    The voice cap (D222, DESIGN_V3 section 3.5). A sound plays no more voices than fit a share of a block, decided by
    the patch alone: the same on every computer and in every bounce, as on hardware whose polyphony depends on the
    patch (the Access Virus, Korg's opsix). The costs are measured by the worst-case cost test
    (tests/V3ModuleCostReport.cpp, COOLSYNTH_CPU_REPORT=1) on the development machine, the optimised Debug build, in
    percent of a 256-sample block at 48 kHz: each module at its hardest settings in sixteen voices, struck twice,
    the best of three runs. A voice's share is the average over the voices, as the 99th percentile divided by
    sixteen is lost in the timer's noise; an effect's is its 99th percentile, as some spike (spectral Pitch's frames).
    At other rates the costs scale with the rate, as the voices run at twice the host's. The effects come off the
    budget first, once per sound; the layers then share what is left in proportion to what they ask for, never
    below one voice. A slower computer is the live emergency's (EngineV3::emergencyShare), never a bounce's.
*/
namespace coolsynth::synth::v3::voiceCost
{
    /** The share of a block a whole sound's voices and effects may take on the development machine (D222). */
    inline constexpr float budgetPercent = 50.0f;

    /** The measured costs, percent of a 256-sample block at 48 kHz per voice (or per sound, for the effects). */
    struct Costs
    {
        float voice = 0.0f;                                    // an empty voice: envelopes, matrix, the decimator
        std::array<float, 12> generator {};                    // by generatorType, one copy
        float vaCopy = 0.0f, wavetableCopy = 0.0f;             // each unison copy past the first
        std::array<float, 8> processor {};                     // by processorType
        std::array<float, insertType::count> effect {};        // by insertType, once for the sound
        float spectralPitch = 0.0f;                            // Pitch in its spectral mode, instead of its heads
        float svf6581 = 0.0f;                                  // the SVF in its SID 6581 character, instead of Clean's
    };

    // Measured on 2026-11-19 (D224). Wavetable's five copies cost alike, so one copy and each more are a fifth of
    // them; the VA's copies include the sub. Grain's effect is priced by its grains at once (grainEffect).
    inline constexpr Costs measured {
        0.35f, // the voice itself
        //  Off   VA    Noise FM2   FM4   Wave  String Modal Sample Grain Chip  Kick
        { 0.0f, 0.19f, 0.05f, 0.30f, 0.86f, 0.12f, 0.38f, 0.15f, 0.13f, 0.85f, 0.33f, 0.33f }, // Chip, Kick: 2026-11-20
        0.04f, 0.12f,
        //  Off   Ladder SVF  Crush Folder Drive Formant Comb
        { 0.0f, 0.56f, 0.17f, 0.04f, 0.23f, 0.48f, 0.23f, 0.28f },
        //  Off   Drive LoFi  Phaser Flanger Chorus Delay Reverb Comp  EQ    Pump  Grain Room  Pitch Stutter
        { 0.0f, 0.80f, 0.65f, 0.53f, 0.45f, 0.28f, 0.39f, 0.77f, 0.23f, 0.10f, 0.10f, 0.40f, 0.86f, 3.79f, 0.36f },
        5.54f,
        0.40f, // the SVF's 6581: measured 2026-11-20 (D233), 0.42 against the clean SVF's 0.20 in the same run
    };

    /** The Grain effect's grains at once, density times size, each this much (its densest, 40 at once, took 7.1). */
    inline constexpr float grainEffectPerGrain = 0.17f;

    namespace detail
    {
        inline int unisonOf(const GeneratorSettings& g, const ModuleDescriptor& d, int index) noexcept
        {
            return std::max(1, static_cast<int>(std::lround(descriptorToUnits(d.params[static_cast<std::size_t>(index)],
                                                                               g.p[static_cast<std::size_t>(index)]))));
        }
    }

    /** One voice of this layer, percent of a block at 48 kHz. */
    [[nodiscard]] inline float ofVoice(const PartSettings& part, const Costs& c = measured) noexcept
    {
        float cost = c.voice;
        for (const auto& g : part.voice.generators)
        {
            const int type = std::clamp(g.type, 0, static_cast<int>(c.generator.size()) - 1);
            cost += c.generator[static_cast<std::size_t>(type)];
            if (type == generatorType::va)
                cost += c.vaCopy * static_cast<float>(detail::unisonOf(g, VaGenerator::descriptor(), VaGenerator::unison) - 1);
            else if (type == generatorType::wavetable)
                cost += c.wavetableCopy * static_cast<float>(detail::unisonOf(g, WavetableGenerator::descriptor(), WavetableGenerator::unison) - 1);
        }
        for (const auto& p : part.voice.processors)
        {
            const int type = std::clamp(p.type, 0, static_cast<int>(c.processor.size()) - 1);
            const bool sid6581 = type == processorType::svf
                                 && std::lround(descriptorToUnits(SvfProcessor::descriptor().params[SvfProcessor::character], p.p[SvfProcessor::character]))
                                        == static_cast<long>(SvfProcessor::Character::sid6581);
            cost += sid6581 ? c.svf6581 : c.processor[static_cast<std::size_t>(type)];
        }
        return cost;
    }

    /** One effect slot, percent of a block at 48 kHz. */
    [[nodiscard]] inline float ofEffect(const InsertSettings& slot, const Costs& c = measured) noexcept
    {
        const int type = std::clamp(slot.type, 0, insertType::count - 1);
        if (type == insertType::grain)
        {
            const auto& d = insertDescriptor(insertType::grain);
            const float sizeSeconds = 0.001f * descriptorToUnits(d.params[0], slot.p[0]);
            const float density = descriptorToUnits(d.params[1], slot.p[1]);
            return c.effect[static_cast<std::size_t>(type)] + grainEffectPerGrain * density * sizeSeconds;
        }
        if (type == insertType::pitch)
        {
            const auto& d = insertDescriptor(insertType::pitch);
            if (std::lround(descriptorToUnits(d.params[8], slot.p[8])) >= 1) // Mode: Spectral
                return c.spectralPitch;
        }
        return c.effect[static_cast<std::size_t>(type)];
    }

    /** The layers' voices, as wanted (poly: the polyphony; unison: its voices), cut down to the budget. A layer not
        playing keeps what it wants: it costs nothing. */
    [[nodiscard]] inline std::array<int, partCount> cap(const EngineSettings& settings, const std::array<bool, partCount>& active,
                                                         const std::array<int, partCount>& wanted, double hostRate,
                                                         const Costs& c = measured, float budget = budgetPercent) noexcept
    {
        const float scale = static_cast<float>(std::max(1000.0, hostRate) / 48000.0);
        float effects = 0.0f;
        for (int p = 0; p < partCount; ++p)
            if (active[static_cast<std::size_t>(p)])
                for (const auto& slot : settings.inserts[static_cast<std::size_t>(p)])
                    effects += ofEffect(slot, c);
        for (const auto& slot : settings.master)
            effects += ofEffect(slot, c);
        // The voices keep at least a fifth of the budget, however many effects: a sound with no voice is no sound.
        const float forVoices = std::max(0.2f * budget, budget - effects * scale);

        std::array<float, partCount> perVoice {};
        float total = 0.0f;
        for (int p = 0; p < partCount; ++p)
        {
            const auto i = static_cast<std::size_t>(p);
            if (! active[i])
                continue;
            perVoice[i] = ofVoice(settings.parts[i], c) * scale;
            total += perVoice[i] * static_cast<float>(wanted[i]);
        }
        if (total <= forVoices || total <= 0.0f)
            return wanted;
        const float share = forVoices / total;
        auto voices = wanted;
        for (int p = 0; p < partCount; ++p)
        {
            const auto i = static_cast<std::size_t>(p);
            if (active[i])
                voices[i] = std::clamp(static_cast<int>(std::floor(static_cast<float>(wanted[i]) * share)), 1, wanted[i]);
        }
        return voices;
    }
}
