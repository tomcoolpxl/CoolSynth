#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

/*
    The sampled instruments' memory (decision D155), like a hardware sampler's sample RAM: one block
    taken from the system once, locked in RAM where the system allows (so Windows cannot page it
    out under a playing note), and never given back while CoolSynth runs. It is handed out in
    pages of 1 MB, as runs of pages next to each other; an instrument that is dropped gives its
    pages back here, not to the system. Loader thread only, except address(), which is constant.
*/
namespace coolsynth::synth::v3
{
    class SampleMemory final
    {
    public:
        static constexpr std::size_t pageBytes = std::size_t { 1 } << 20;

        struct Run
        {
            std::uint32_t first = 0, count = 0; // pages
        };

        SampleMemory() = default;
        ~SampleMemory();
        SampleMemory(const SampleMemory&) = delete;
        SampleMemory& operator=(const SampleMemory&) = delete;

        /** Takes `bytes` (rounded up to whole pages) from the system, touching and locking it a slice at a
            time with a short pause between slices, so the audio thread never waits long on the system's
            memory manager. False if the system has not got it. Once only, until release(). */
        bool reserve(std::size_t bytes, const std::function<bool()>& shouldStop = {});
        /** Gives everything back to the system. Only when nothing is held (a new library folder, the end). */
        void release() noexcept;

        [[nodiscard]] bool reserved() const noexcept { return base != nullptr; }
        [[nodiscard]] bool lockedInRam() const noexcept { return locked; }
        [[nodiscard]] std::size_t capacityBytes() const noexcept { return pageCount * pageBytes; }
        [[nodiscard]] std::size_t usedBytes() const noexcept { return usedPages * pageBytes; }
        [[nodiscard]] std::size_t largestFreeRunPages() const noexcept;

        /** The first free run of `pages` pages, or none. */
        [[nodiscard]] std::optional<Run> take(std::size_t pages);
        void give(const Run& run) noexcept;
        [[nodiscard]] std::byte* address(const Run& run) const noexcept { return base + static_cast<std::size_t>(run.first) * pageBytes; }

        /** The physical memory free right now, in bytes (0 if the system does not say). */
        [[nodiscard]] static std::size_t freePhysicalBytes() noexcept;

    private:
        std::byte* base = nullptr;
        std::size_t pageCount = 0, usedPages = 0;
        std::vector<bool> used;
        bool locked = false;
        bool fromSystemPages = false; // VirtualAlloc, not the heap
    };
}
