#pragma once

#include <array>
#include <atomic>
#include <cstddef>

/*
    The allocation alarm (D161): an audio block must never take memory from the heap. processBlock
    marks its thread while it runs; the test program and the render tool replace operator new and
    count every allocation made on a marked thread (tests/AudioThreadAlarm.cpp,
    tools/AudioThreadAlarm.cpp). The plugin and the app replace nothing and pay one thread-local
    store per block.
*/
namespace coolsynth::diag
{
    /** True on a thread while it renders an audio block. */
    inline thread_local bool inAudioBlock = false;

    /** Marks the current thread for one audio block (nests). */
    class AudioBlockScope final
    {
    public:
        AudioBlockScope() noexcept : previous(inAudioBlock) { inAudioBlock = true; }
        ~AudioBlockScope() { inAudioBlock = previous; }
        AudioBlockScope(const AudioBlockScope&) = delete;
        AudioBlockScope& operator=(const AudioBlockScope&) = delete;

    private:
        bool previous;
    };

    /** What the replaced operator new saw; read by tests and the render tool. */
    struct AudioAllocations
    {
        std::atomic<int> count { 0 };
        std::atomic<std::size_t> bytes { 0 };
        std::array<std::atomic<std::size_t>, 8> firstSizes {};
    };
    inline AudioAllocations audioAllocations;

    /** Called by a replaced operator new: counts an allocation if the thread is in an audio block. Never allocates. */
    inline void noteAllocation(std::size_t size) noexcept
    {
        if (! inAudioBlock)
            return;
        const int index = audioAllocations.count.fetch_add(1, std::memory_order_relaxed);
        audioAllocations.bytes.fetch_add(size, std::memory_order_relaxed);
        if (index >= 0 && index < static_cast<int>(audioAllocations.firstSizes.size()))
            audioAllocations.firstSizes[static_cast<std::size_t>(index)].store(size, std::memory_order_relaxed);
    }

    inline void resetAudioAllocations() noexcept
    {
        audioAllocations.count.store(0, std::memory_order_relaxed);
        audioAllocations.bytes.store(0, std::memory_order_relaxed);
        for (auto& s : audioAllocations.firstSizes)
            s.store(0, std::memory_order_relaxed);
    }
}
