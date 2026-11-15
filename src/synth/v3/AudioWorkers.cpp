#include "AudioWorkers.h"

#include <algorithm>
#include <set>

#include <juce_audio_basics/juce_audio_basics.h>

#include "diagnostics/AudioThreadAlarm.h"
#include "diagnostics/FlightRecorder.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <avrt.h>
 #include <intrin.h>
 #pragma comment(lib, "avrt.lib")
 #pragma comment(lib, "Synchronization.lib")
#endif

namespace coolsynth::synth::v3
{
    juce::String CpuLayout::describe() const
    {
        return juce::String(physical) + " cores, " + juce::String(logical) + " threads, " + juce::String(cacheGroups)
               + (cacheGroups == 1 ? " cache group" : " cache groups") + (efficiencyClasses > 1 ? ", performance and efficiency cores" : "");
    }

    CpuLayout CpuLayout::detect()
    {
        CpuLayout layout;
        layout.logical = std::max(1, juce::SystemStats::getNumCpus());
        layout.physical = std::max(1, juce::SystemStats::getNumPhysicalCpus());
#if JUCE_WINDOWS
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &bytes);
        if (bytes > 0)
        {
            juce::HeapBlock<std::uint8_t> buffer(bytes);
            if (GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get()), &bytes))
            {
                int cores = 0, caches = 0;
                std::set<int> classes;
                for (DWORD at = 0; at < bytes;)
                {
                    const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get() + at);
                    if (info->Relationship == RelationProcessorCore)
                    {
                        ++cores;
                        classes.insert(info->Processor.EfficiencyClass);
                    }
                    else if (info->Relationship == RelationCache && info->Cache.Level == 3)
                        ++caches;
                    at += info->Size;
                }
                layout.physical = std::max(1, cores);
                layout.cacheGroups = std::max(1, caches);
                layout.efficiencyClasses = std::max(1, static_cast<int>(classes.size()));
            }
        }
#endif
        return layout;
    }

    // ---- A worker -------------------------------------------------------------------------------------------

    class AudioWorkers::Worker final : public juce::Thread
    {
    public:
        Worker(AudioWorkers& o, int index) : juce::Thread("CoolSynth audio worker " + juce::String(index)), owner(o) {}

        void run() override
        {
#if JUCE_WINDOWS
            DWORD taskIndex = 0;
            const HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex); // like the audio thread itself
            if (mmcss == nullptr)
                diag::record(diag::Event::message, static_cast<int>(GetLastError()), 0, 0.0f, 0.0f, "audio worker not registered with MMCSS");
#endif
            const juce::ScopedNoDenormals noDenormals; // flush-to-zero belongs to each thread
            std::uint32_t seen = owner.generation.load(std::memory_order_acquire);
            while (! owner.stopping.load(std::memory_order_acquire) && ! threadShouldExit())
            {
                const auto now = owner.generation.load(std::memory_order_acquire);
                if (now == seen)
                {
#if JUCE_WINDOWS
                    WaitOnAddress(&owner.generation, &seen, sizeof(seen), 200);
#else
                    juce::Thread::sleep(1);
#endif
                    continue;
                }
                seen = now;
                owner.work();
            }
#if JUCE_WINDOWS
            if (mmcss != nullptr)
                AvRevertMmThreadCharacteristics(mmcss);
#endif
        }

    private:
        AudioWorkers& owner;
    };

    // ---- The pool ----------------------------------------------------------------------------------------------

    AudioWorkers& AudioWorkers::instance()
    {
        static AudioWorkers pool;
        return pool;
    }

    AudioWorkers::AudioWorkers() : layout(CpuLayout::detect()) {}

    AudioWorkers::~AudioWorkers()
    {
        stopping.store(true, std::memory_order_release);
        generation.fetch_add(1, std::memory_order_acq_rel);
#if JUCE_WINDOWS
        WakeByAddressAll(&generation);
#endif
        for (auto& worker : workers)
            worker->stopThread(2000);
    }

    void AudioWorkers::prepare()
    {
        const juce::ScopedLock lock(prepareLock);
        if (prepared)
            return;
        prepared = true;
#if JUCE_WINDOWS
        // Three helpers render the earlier sounds and the one fading next to the sound played (D158); never more
        // than the physical cores less two, so the system and the sample loaders keep theirs.
        const int count = std::clamp(layout.physical - 2, 0, 3);
        for (int i = 0; i < count; ++i)
        {
            workers.push_back(std::make_unique<Worker>(*this, i + 1));
            workers.back()->startThread(juce::Thread::Priority::highest);
        }
#endif
        diag::record(diag::Event::message, layout.physical, layout.logical, static_cast<float>(workerCount()), 0.0f,
                     ("CPU: " + layout.describe() + "; audio workers (cores, threads)").toRawUTF8());
    }

    void AudioWorkers::work() noexcept
    {
        for (;;)
        {
            // The claim holds the block and the next task together: a worker late from an earlier block cannot take
            // a task of a later one with that block's details.
            auto claimed = claim.load(std::memory_order_acquire);
            const int count = taskCount.load(std::memory_order_acquire);
            const auto index = static_cast<int>(claimed & 0xFFFFFFFFu);
            if (index >= count)
                return;
            if (! claim.compare_exchange_weak(claimed, claimed + 1, std::memory_order_acq_rel))
                continue;
            const Task task = currentTask.load(std::memory_order_acquire);
            void* context = currentContext.load(std::memory_order_acquire);
            {
                const diag::AudioBlockScope block; // the allocation alarm watches workers too (D161)
                task(context, index);
            }
            pending.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void AudioWorkers::run(Task task, void* context, int count) noexcept
    {
        if (count <= 0)
            return;
        // Another CoolSynth in the same program is using the helpers this block: this one renders on its own thread.
        if (workers.empty() || count == 1 || busy.exchange(true, std::memory_order_acq_rel))
        {
            for (int i = 0; i < count; ++i)
                task(context, i);
            return;
        }
        currentTask.store(task, std::memory_order_release);
        currentContext.store(context, std::memory_order_release);
        taskCount.store(std::min(count, maxTasks), std::memory_order_release);
        pending.store(std::min(count, maxTasks), std::memory_order_release);
        const auto block = ++blockNumber;
        claim.store(static_cast<std::uint64_t>(block) << 32, std::memory_order_release);
        generation.store(block, std::memory_order_release);
#if JUCE_WINDOWS
        WakeByAddressAll(&generation);
#endif
        work(); // the audio thread takes tasks too
        while (pending.load(std::memory_order_acquire) != 0)
        {
#if JUCE_WINDOWS
            _mm_pause(); // only for a task a worker has begun
#endif
        }
        busy.store(false, std::memory_order_release);
    }
}
