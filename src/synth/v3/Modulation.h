#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 sub-block modulation context (DESIGN_V3.md sections 4.2 and 5).

    Once per sub-block a voice: steps every slot's SlotBase toward the targets the host set,
    clears every slot's SlotOffsets, lets the modulators and the matrix add into the offsets,
    then builds the SlotParams each module sees with buildSlotParams(). Generic parameters live
    in normalised 0..1 space until that last step, so smoothing and modulation behave the same
    for linear, logarithmic and squared parameters.
*/
namespace coolsynth::synth::v3
{
    enum class ModSource : int
    {
        none = 0,
        velocity,
        releaseVelocity,
        key,
        noteRandom,
        aftertouch,
        mpePressure,
        mpeSlide,
        drift,
        env1,
        env2,
        env3,
        lfo1,
        lfo2,
        glfo1,
        glfo2,
        genv,
        step,
        macro1,
        macro2,
        macro3,
        macro4,
        wheel,
        bend,
        expression,
        chaos,
        count
    };

    /** Current value of every modulation source for one sub-block. `none` is always 0. */
    struct ModSources
    {
        static constexpr int count = static_cast<int>(ModSource::count);

        [[nodiscard]] float get(ModSource source) const noexcept
        {
            return values[static_cast<std::size_t>(source)];
        }

        void set(ModSource source, float value) noexcept
        {
            if (source != ModSource::none)
                values[static_cast<std::size_t>(source)] = value;
        }

        void clear() noexcept { values.fill(0.0f); }

        std::array<float, static_cast<std::size_t>(count)> values {};
    };

    /** Modulation summed into one slot for one sub-block. Generic offsets are in normalised units. */
    struct SlotOffsets
    {
        std::array<float, maxSlotParameters> p {};
        float level = 0.0f;          // normalised, added to the level base
        float pan = 0.0f;            // -1..1 units
        float pitchSemitones = 0.0f; // semitones
        float amount = 0.0f;         // normalised, added to the combine amount

        void clear() noexcept
        {
            p.fill(0.0f);
            level = 0.0f;
            pan = 0.0f;
            pitchSemitones = 0.0f;
            amount = 0.0f;
        }
    };

    /** Maps a normalised 0..1 value to the descriptor's units, honouring its skew. */
    [[nodiscard]] inline float descriptorToUnits(const ParameterDescriptor& d, float normalised) noexcept
    {
        const float t = std::clamp(normalised, 0.0f, 1.0f);
        if (d.skew == Skew::logarithmic && d.min > 0.0f && d.max > d.min)
            return d.min * std::pow(d.max / d.min, t);
        if (d.skew == Skew::squared)
            return d.min + t * t * (d.max - d.min);
        return d.min + t * (d.max - d.min);
    }

    /** Inverse of descriptorToUnits(). */
    [[nodiscard]] inline float unitsToNormalised(const ParameterDescriptor& d, float units) noexcept
    {
        if (d.max <= d.min)
            return 0.0f;
        const float u = std::clamp(units, std::min(d.min, d.max), std::max(d.min, d.max));
        if (d.skew == Skew::logarithmic && d.min > 0.0f)
            return std::log(u / d.min) / std::log(d.max / d.min);
        if (d.skew == Skew::squared)
            return std::sqrt((u - d.min) / (d.max - d.min));
        return (u - d.min) / (d.max - d.min);
    }

    /**
        The host-set values of one slot, each moving linearly toward its target by at most a fixed
        rate. Generic values, level and combine amount are normalised 0..1; pan is -1..1; pitch is
        in semitones. step(n) advances by n samples, so sub-blocks of any length smooth alike.
    */
    class SlotBase final
    {
    public:
        static constexpr double defaultSmoothingSeconds = 0.010;
        static constexpr float pitchSpanSemitones = 96.0f;

        void prepare(double sampleRate, int subBlockSamples, double smoothingSeconds = defaultSmoothingSeconds)
        {
            subBlock = std::max(1, subBlockSamples);
            perSample = static_cast<float>(1.0 / (std::max(1.0, sampleRate) * std::max(1.0e-4, smoothingSeconds)));
            snapToTargets();
        }

        void setTarget(int index, float normalised) noexcept
        {
            p[static_cast<std::size_t>(index)].target = std::clamp(normalised, 0.0f, 1.0f);
        }
        void setLevelTarget(float normalised) noexcept { level.target = std::clamp(normalised, 0.0f, 1.0f); }
        void setPanTarget(float pan_) noexcept { pan.target = std::clamp(pan_, -1.0f, 1.0f); }
        void setPitchTarget(float semitones) noexcept
        {
            pitch.target = std::clamp(semitones, -pitchSpanSemitones, pitchSpanSemitones);
        }
        void setAmountTarget(float normalised) noexcept { amount.target = std::clamp(normalised, 0.0f, 1.0f); }

        /** Jumps every value to its target: for reset and note start. */
        void snapToTargets() noexcept
        {
            for (auto& s : p)
                s.value = s.target;
            level.value = level.target;
            pan.value = pan.target;
            pitch.value = pitch.target;
            amount.value = amount.target;
        }

        /** One sub-block of the prepared length. */
        void step() noexcept { step(subBlock); }

        void step(int numSamples) noexcept
        {
            const float f = std::min(1.0f, perSample * static_cast<float>(std::max(0, numSamples)));
            for (auto& s : p)
                approach(s, f);
            approach(level, f);
            approach(amount, f);
            approach(pan, 2.0f * f);
            approach(pitch, pitchSpanSemitones * f);
        }

        [[nodiscard]] bool isSmoothing() const noexcept
        {
            for (const auto& s : p)
                if (s.value != s.target)
                    return true;
            return level.value != level.target || pan.value != pan.target || pitch.value != pitch.target
                   || amount.value != amount.target;
        }

        [[nodiscard]] float value(int index) const noexcept { return p[static_cast<std::size_t>(index)].value; }
        [[nodiscard]] float levelValue() const noexcept { return level.value; }
        [[nodiscard]] float panValue() const noexcept { return pan.value; }
        [[nodiscard]] float pitchValue() const noexcept { return pitch.value; }
        [[nodiscard]] float amountValue() const noexcept { return amount.value; }

    private:
        struct Smoothed
        {
            float value = 0.0f;
            float target = 0.0f;
        };

        static void approach(Smoothed& s, float maxStep) noexcept
        {
            const float remaining = s.target - s.value;
            if (std::abs(remaining) <= maxStep)
                s.value = s.target;
            else
                s.value += remaining > 0.0f ? maxStep : -maxStep;
        }

        std::array<Smoothed, maxSlotParameters> p {};
        Smoothed level { 1.0f, 1.0f };
        Smoothed pan {};
        Smoothed pitch {};
        Smoothed amount {};
        int subBlock = 32;
        float perSample = 1.0f;
    };

    /**
        The values a module sees for one sub-block: smoothed base plus offsets, clamped, and the
        generic ones mapped to the descriptor's units. Offsets on a parameter the descriptor marks
        as not modulatable are ignored.
    */
    [[nodiscard]] inline SlotParams buildSlotParams(const ModuleDescriptor& descriptor,
                                                    const SlotBase& base,
                                                    const SlotOffsets& offsets) noexcept
    {
        SlotParams params;
        for (int i = 0; i < maxSlotParameters; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            if (i >= descriptor.usedParams)
            {
                params.p[index] = 0.0f;
                continue;
            }

            const auto& d = descriptor.params[index];
            const float normalised = d.modulatable ? base.value(i) + offsets.p[index] : base.value(i);
            params.p[index] = descriptorToUnits(d, normalised);
        }

        params.level = std::clamp(base.levelValue() + offsets.level, 0.0f, 1.0f);
        params.pan = std::clamp(base.panValue() + offsets.pan, -1.0f, 1.0f);
        params.pitchSemitones = std::clamp(base.pitchValue() + offsets.pitchSemitones,
                                           -SlotBase::pitchSpanSemitones,
                                           SlotBase::pitchSpanSemitones);
        params.combineAmount = std::clamp(base.amountValue() + offsets.amount, 0.0f, 1.0f);
        return params;
    }
}
