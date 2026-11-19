#pragma once

#include <array>
#include <atomic>
#include <cstddef>

/*
    The allocation alarm (D161): an audio block must never take memory from the heap. processBlock
    marks its thread while it runs; the test program and the render tool replace operator new and
    count every allocation made on a marked thread (tests/AudioThreadAlarm.cpp,
    tools/AudioThreadAlarm.cpp). The plugin and the app replace nothing and pay one thread-local
    store per block. The test program also watches the C heap, locks, waits, sleeps and files (D222).
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

    /** The other calls that can wait on an audio block (D222): the C heap (malloc, realloc and free), a lock, a wait
        for an event, a sleep, a file. The test program counts them (tests/AudioThreadAlarm.cpp, watchBlockingCalls)
        as Clang's RealtimeSanitizer does where it runs, which is not Windows. */
    enum class BlockingCall : int { none = 0, heap, lock, wait, sleep, file };

    [[nodiscard]] inline const char* blockingCallName(int kind) noexcept
    {
        switch (static_cast<BlockingCall>(kind))
        {
            case BlockingCall::heap:  return "heap";
            case BlockingCall::lock:  return "lock";
            case BlockingCall::wait:  return "wait";
            case BlockingCall::sleep: return "sleep";
            case BlockingCall::file:  return "file";
            case BlockingCall::none:
            default:                  return "none";
        }
    }

    struct AudioBlockingCalls
    {
        std::atomic<int> count { 0 };
        std::array<std::atomic<int>, 8> firstKinds {};
    };
    inline AudioBlockingCalls audioBlockingCalls;

    /** Called by the test program's watch: counts the call if the thread is in an audio block, and gives its place in
        the count (or -1 outside a block). Never allocates. */
    inline int noteBlockingCall(BlockingCall kind) noexcept
    {
        if (! inAudioBlock)
            return -1;
        const int index = audioBlockingCalls.count.fetch_add(1, std::memory_order_relaxed);
        if (index >= 0 && index < static_cast<int>(audioBlockingCalls.firstKinds.size()))
            audioBlockingCalls.firstKinds[static_cast<std::size_t>(index)].store(static_cast<int>(kind), std::memory_order_relaxed);
        return index;
    }

    /** Test program only: starts watching the calls above, once; true if it could. */
    bool watchBlockingCalls();
    /** Test program only, outside audio blocks: the functions that made the first few counted calls, innermost first. */
    const char* firstBlockingCallers();

    inline void resetAudioAllocations() noexcept
    {
        audioAllocations.count.store(0, std::memory_order_relaxed);
        audioAllocations.bytes.store(0, std::memory_order_relaxed);
        for (auto& s : audioAllocations.firstSizes)
            s.store(0, std::memory_order_relaxed);
        audioBlockingCalls.count.store(0, std::memory_order_relaxed);
        for (auto& k : audioBlockingCalls.firstKinds)
            k.store(0, std::memory_order_relaxed);
    }
}
