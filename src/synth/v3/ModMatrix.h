#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "Modulation.h"

/*
    V3 modulation matrix (DESIGN_V3.md section 5).

    24 slots per part. Each slot takes a source, bends it with a curve, scales it by its amount
    and, when a via source is set, by that source too, and adds the result into a destination's
    offset. Offsets are in the destination's normalised range (an amount of 1 sweeps a generic
    parameter over its whole range), except pitch, where 1 is 48 semitones, and the LFO rates,
    where 1 is four octaves faster, and the envelope times, where 1 is four octaves (16 times)
    longer. Envelope times are read once per note, when the note starts or retriggers, from the
    note's own sources and the part's current ones. The VCA destination scales the voice: its gain is 1 plus the
    offset, held between 0 and 1, so negative amounts duck and a unipolar LFO gates.

    Destination numbers are part of saved patches: add new ones only at the end.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int matrixSlotCount = 24;
    inline constexpr float matrixPitchSemitones = 48.0f;
    inline constexpr float matrixRateOctaves = 4.0f;

    enum class ModCurve : int { linear = 0, unipolar, exponential, logarithmic, sCurve, quantized };
    inline constexpr int modCurveCount = 6;

    /**
        A source value bent by a curve. Unipolar turns -1..1 into 0..1; the others keep the sign.
        Quantized passes the source unchanged here: the slot's whole contribution is rounded at its
        destination instead, to whole semitones on pitch and to sixteenths of the range elsewhere.
    */
    [[nodiscard]] inline float applyCurve(ModCurve curve, float x) noexcept
    {
        const float a = std::min(1.0f, std::abs(x));
        switch (curve)
        {
            case ModCurve::linear:      return x;
            case ModCurve::unipolar:    return 0.5f * (x + 1.0f);
            case ModCurve::exponential: return std::copysign(a * a, x);
            case ModCurve::logarithmic: return std::copysign(std::sqrt(a), x);
            case ModCurve::sCurve:      return std::copysign(a * a * (3.0f - 2.0f * a), x);
            case ModCurve::quantized:   return x;
        }
        return x;
    }

    namespace modDestination
    {
        inline constexpr int none = 0;
        inline constexpr int pitch = 1;    // every key-tracked generator, semitones
        inline constexpr int vca = 2;
        inline constexpr int pan = 3;
        inline constexpr int lfo1Rate = 4;
        inline constexpr int lfo2Rate = 5;
        inline constexpr int generatorBase = 6;
        inline constexpr int generatorFields = 12; // pitch, level, pan, amount, then P1..P8
        inline constexpr int generatorSlots = 3;
        inline constexpr int processorBase = generatorBase + generatorSlots * generatorFields;
        inline constexpr int processorSlots = 3;
        inline constexpr int envelopeTimeBase = processorBase + processorSlots * maxSlotParameters;
        inline constexpr int envelopeTimes = 3;
        inline constexpr int count = envelopeTimeBase + envelopeTimes;

        [[nodiscard]] constexpr bool isEnvelopeTime(int destination) noexcept
        {
            return destination >= envelopeTimeBase && destination < envelopeTimeBase + envelopeTimes;
        }

        namespace generatorField
        {
            inline constexpr int pitch = 0;
            inline constexpr int level = 1;
            inline constexpr int pan = 2;
            inline constexpr int amount = 3;
            inline constexpr int firstParameter = 4;
        }

        [[nodiscard]] constexpr int generator(int slot, int field) noexcept { return generatorBase + slot * generatorFields + field; }
        [[nodiscard]] constexpr int generatorParameter(int slot, int index) noexcept
        {
            return generator(slot, generatorField::firstParameter + index);
        }
        [[nodiscard]] constexpr int processorParameter(int slot, int index) noexcept
        {
            return processorBase + slot * maxSlotParameters + index;
        }
    }

    struct MatrixSlot
    {
        ModSource source = ModSource::none;
        int destination = modDestination::none;
        float amount = 0.0f;
        ModSource via = ModSource::none;
        ModCurve curve = ModCurve::linear;
    };

    /** The slots that do something, packed at the front. */
    struct MatrixSettings
    {
        std::array<MatrixSlot, matrixSlotCount> slots {};
        int count = 0;

        /** Keeps a slot only when it has a source, a destination and an amount. */
        void add(const MatrixSlot& slot) noexcept
        {
            if (count < matrixSlotCount && slot.source != ModSource::none && slot.destination > modDestination::none
                && slot.destination < modDestination::count && slot.amount != 0.0f)
                slots[static_cast<std::size_t>(count++)] = slot;
        }
    };

    /** Everything one evaluation adds, per destination. */
    struct MatrixOffsets
    {
        std::array<SlotOffsets, modDestination::generatorSlots> generators {};
        std::array<SlotOffsets, modDestination::processorSlots> processors {};
        float pitchSemitones = 0.0f;
        float vca = 0.0f;
        float pan = 0.0f;
        std::array<float, 2> lfoRateOctaves {};
        std::array<float, modDestination::envelopeTimes> envelopeTimeOctaves {};

        void clear() noexcept
        {
            for (auto& g : generators)
                g.clear();
            for (auto& p : processors)
                p.clear();
            pitchSemitones = vca = pan = 0.0f;
            lfoRateOctaves = {};
            envelopeTimeOctaves = {};
        }
    };

    inline void addToDestination(int destination, float value, MatrixOffsets& out) noexcept
    {
        namespace d = modDestination;
        if (d::isEnvelopeTime(destination))
        {
            out.envelopeTimeOctaves[static_cast<std::size_t>(destination - d::envelopeTimeBase)] += value * matrixRateOctaves;
            return;
        }
        if (destination >= d::processorBase)
        {
            const int index = destination - d::processorBase;
            const int slot = index / maxSlotParameters;
            if (slot < d::processorSlots)
                out.processors[static_cast<std::size_t>(slot)].p[static_cast<std::size_t>(index % maxSlotParameters)] += value;
            return;
        }
        if (destination >= d::generatorBase)
        {
            const int index = destination - d::generatorBase;
            auto& g = out.generators[static_cast<std::size_t>(index / d::generatorFields)];
            const int field = index % d::generatorFields;
            switch (field)
            {
                case d::generatorField::pitch:  g.pitchSemitones += value * matrixPitchSemitones; break;
                case d::generatorField::level:  g.level += value; break;
                case d::generatorField::pan:    g.pan += value; break;
                case d::generatorField::amount: g.amount += value; break;
                default: g.p[static_cast<std::size_t>(field - d::generatorField::firstParameter)] += value; break;
            }
            return;
        }
        switch (destination)
        {
            case d::pitch:    out.pitchSemitones += value * matrixPitchSemitones; break;
            case d::vca:      out.vca += value; break;
            case d::pan:      out.pan += value; break;
            case d::lfo1Rate: out.lfoRateOctaves[0] += value * matrixRateOctaves; break;
            case d::lfo2Rate: out.lfoRateOctaves[1] += value * matrixRateOctaves; break;
            default: break;
        }
    }

    /** Whole semitones on a pitch destination, sixteenths of the range elsewhere. */
    [[nodiscard]] inline float quantizeContribution(int destination, float value) noexcept
    {
        namespace d = modDestination;
        const bool pitch = destination == d::pitch
                           || (destination >= d::generatorBase && destination < d::processorBase
                               && (destination - d::generatorBase) % d::generatorFields == d::generatorField::pitch);
        const float steps = pitch ? matrixPitchSemitones : 16.0f;
        return std::round(value * steps) / steps;
    }

    /** Clears `out` and adds every slot of `matrix` for the current source values. */
    inline void evaluateMatrix(const MatrixSettings& matrix, const ModSources& sources, MatrixOffsets& out) noexcept
    {
        out.clear();
        for (int i = 0; i < matrix.count; ++i)
        {
            const auto& slot = matrix.slots[static_cast<std::size_t>(i)];
            float value = applyCurve(slot.curve, sources.get(slot.source)) * slot.amount;
            if (slot.via != ModSource::none)
                value *= sources.get(slot.via);
            if (slot.curve == ModCurve::quantized)
                value = quantizeContribution(slot.destination, value);
            addToDestination(slot.destination, value, out);
        }
    }
}
