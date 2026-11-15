#pragma once

#include <algorithm>
#include <array>

#include "Inserts.h"
#include "ModMatrix.h"
#include "Modulation.h"

/*
    Effect modulation (decision D179): a part's global sources move the settings of its own insert
    effects (I1-I6 since D194) and of the master effects (F1-F6). Only what is the same for every voice of the
    part can move an effect all its voices share: the macros, both global LFOs, the global
    envelope, the step modulator, chaos, the mod wheel, pitch bend, key pressure and the expression
    pedal. A route reads like a matrix slot: source, destination, amount, an optional via source
    that scales it, and a curve. Its value is added to the setting's normalised value, as the
    voice matrix does, and the setting is held inside 0..1.

    The sources are read at the start and the end of each block and the effects are run in steps of
    32 samples between them, so a route moves as smoothly as a voice's modulation; a block where no
    route adds anything runs the effects as before, in one piece.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int effectRouteCount = 8;
    inline constexpr int effectRouteStep = 32; // samples per step while a route moves something

    namespace effectDestination
    {
        inline constexpr int off = 0;
        inline constexpr int insertSlots = insertSlotCount; // six since D194: old states' F destinations move up
        inline constexpr int masterSlots = 6;
        inline constexpr int perSlot = maxSlotParameters;
        inline constexpr int insertBase = 1;
        inline constexpr int masterBase = insertBase + insertSlots * perSlot;
        inline constexpr int count = masterBase + masterSlots * perSlot;

        [[nodiscard]] inline constexpr int insert(int slot, int parameter) noexcept { return insertBase + slot * perSlot + parameter; }
        [[nodiscard]] inline constexpr int master(int slot, int parameter) noexcept { return masterBase + slot * perSlot + parameter; }
        [[nodiscard]] inline constexpr bool isInsert(int d) noexcept { return d >= insertBase && d < masterBase; }
        [[nodiscard]] inline constexpr bool isMaster(int d) noexcept { return d >= masterBase && d < count; }
        [[nodiscard]] inline constexpr int slotOf(int d) noexcept { return (d - (isInsert(d) ? insertBase : masterBase)) / perSlot; }
        [[nodiscard]] inline constexpr int parameterOf(int d) noexcept { return (d - (isInsert(d) ? insertBase : masterBase)) % perSlot; }
    }

    /** True for a source that is the same for every voice of a part, which an effect may follow. */
    [[nodiscard]] inline constexpr bool isPartWideSource(ModSource s) noexcept
    {
        switch (s)
        {
            case ModSource::glfo1: case ModSource::glfo2: case ModSource::genv: case ModSource::step: case ModSource::chaos:
            case ModSource::macro1: case ModSource::macro2: case ModSource::macro3: case ModSource::macro4:
            case ModSource::wheel: case ModSource::bend: case ModSource::aftertouch: case ModSource::expression:
                return true;
            default:
                return false;
        }
    }

    struct EffectRoute
    {
        ModSource source = ModSource::none;
        int destination = effectDestination::off;
        float amount = 0.0f;
        ModSource via = ModSource::none;
        ModCurve curve = ModCurve::linear;

        [[nodiscard]] bool used() const noexcept
        {
            return source != ModSource::none && destination != effectDestination::off && amount != 0.0f;
        }

        /** What the route adds now, in the destination's normalised units. */
        [[nodiscard]] float value(const ModSources& sources) const noexcept
        {
            if (! isPartWideSource(source))
                return 0.0f;
            float v = applyCurve(curve, sources.get(source)) * amount;
            if (via != ModSource::none)
                v *= isPartWideSource(via) ? sources.get(via) : 0.0f;
            return v;
        }
    };

    using EffectRoutes = std::array<EffectRoute, effectRouteCount>;

    /** Adds every route that reaches this slot (an insert of the part, or a master slot) to its settings, with
        the sources at `sources`; settings the slot's type does not let move are left alone. */
    inline void applyEffectRoutes(InsertSettings& slot, bool master, int slotIndex, const EffectRoutes& routes,
                                  const ModSources& sources) noexcept
    {
        const auto& descriptor = insertDescriptor(std::clamp(slot.type, 0, insertType::count - 1));
        for (const auto& route : routes)
        {
            if (! route.used())
                continue;
            const bool here = master ? effectDestination::isMaster(route.destination) : effectDestination::isInsert(route.destination);
            if (! here || effectDestination::slotOf(route.destination) != slotIndex)
                continue;
            const int parameter = effectDestination::parameterOf(route.destination);
            if (parameter >= descriptor.usedParams || ! descriptor.params[static_cast<std::size_t>(parameter)].modulatable)
                continue;
            auto& p = slot.p[static_cast<std::size_t>(parameter)];
            p = std::clamp(p + route.value(sources), 0.0f, 1.0f);
        }
    }

    /** True when any route adds something with the sources at either end of a block. */
    [[nodiscard]] inline bool effectRoutesMove(const EffectRoutes& routes, const ModSources& before, const ModSources& after) noexcept
    {
        for (const auto& route : routes)
            if (route.used() && (route.value(before) != 0.0f || route.value(after) != 0.0f))
                return true;
        return false;
    }
}
