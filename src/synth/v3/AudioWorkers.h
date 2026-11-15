#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

/*
    The audio thread's helpers (decision D164): a few worker threads that render sound units
    alongside it, inside the same block. One pool for the whole program, whatever the number of
    CoolSynths in it (a host's 32 real-time threads are shared), with one worker fewer than the
    tasks it is meant for and never more than the physical cores less two.

    Each worker registers with Windows' Multimedia Class Scheduler as "Pro Audio", like the audio
    thread, sets flush-to-zero, and sleeps on an address until a block wakes it (WaitOnAddress).
    The audio thread fills a fixed table of tasks, wakes the workers, takes tasks itself, and waits
    by spinning only for a task a worker has already begun: a worker that wakes late costs no more
    than one core would. Nothing here allocates or locks while the audio runs.

    The CPU it found is logged at start (cores, threads, cache groups, efficiency classes).
*/
namespace coolsynth::synth::v3
{
    /** What the processor is made of (D164: "aware"). */
    struct CpuLayout
    {
        int logical = 1;
        int physical = 1;
        int cacheGroups = 1;      // last-level caches
        int efficiencyClasses = 1; // more than one: performance and efficiency cores
        [[nodiscard]] juce::String describe() const;
        [[nodiscard]] static CpuLayout detect();
    };

    class AudioWorkers final
    {
    public:
        static constexpr int maxTasks = 8;
        using Task = void (*)(void* context, int index) noexcept;

        /** The program's pool, started on first use (from prepare, never from the audio thread). */
        static AudioWorkers& instance();
        ~AudioWorkers();

        /** Message thread: makes sure the pool exists and has its workers. */
        void prepare();
        [[nodiscard]] int workerCount() const noexcept { return static_cast<int>(workers.size()); }
        [[nodiscard]] const CpuLayout& cpu() const noexcept { return layout; }

        /**
            Audio thread: runs task(context, i) for i in 0..count-1, on the workers and on the calling thread, and
            returns when all are done. With no workers it runs them all here, in order. Tasks must not allocate.
        */
        void run(Task task, void* context, int count) noexcept;

    private:
        class Worker;
        AudioWorkers();
        void work() noexcept; // claims tasks until none is left

        CpuLayout layout;
        std::vector<std::unique_ptr<Worker>> workers;
        std::atomic<std::uint32_t> generation { 0 }; // each block's work: the workers wake on its change
        std::atomic<std::uint64_t> claim { 0 };      // the block (high half) and its next task (low half)
        std::uint32_t blockNumber = 0;               // audio thread
        std::atomic<int> pending { 0 };
        std::atomic<Task> currentTask { nullptr };
        std::atomic<void*> currentContext { nullptr };
        std::atomic<int> taskCount { 0 };
        std::atomic<bool> stopping { false };
        std::atomic<bool> busy { false }; // one engine at a time uses the helpers
        bool prepared = false;
        juce::CriticalSection prepareLock;
        friend class Worker;
    };
}
