#pragma once

#include <algorithm>

#include "Module.h"

/* The "Off" type of every slot kind: costs nothing, a generator renders silence, a processor passes through. */
namespace coolsynth::synth::v3
{
    struct OffGenerator final
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Off", 0, {}, 0 };
            return d;
        }
        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double, int, SlotMemory&) noexcept {}
        void reset() noexcept {}
        void noteOn(const NoteContext&) noexcept {}
        void setLink(const GeneratorLink&) noexcept {}
        [[nodiscard]] GeneratorLink link() const noexcept { return {}; }

        void process(float* left, float* right, int numSamples, const SlotParams&) noexcept
        {
            std::fill_n(left, numSamples, 0.0f);
            std::fill_n(right, numSamples, 0.0f);
        }
    };

    struct OffProcessor final
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Off", 0, {}, 0 };
            return d;
        }
        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double, int, SlotMemory&) noexcept {}
        void reset() noexcept {}
        void noteOn(const NoteContext&) noexcept {}
        void process(float*, float*, int, const SlotParams&) noexcept {}
    };

    static_assert(Module<OffGenerator>);
    static_assert(Module<OffProcessor>);
}
