#include "SampleMemory.h"

#include <algorithm>
#include <cstring>
#include <new>

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include "diagnostics/FlightRecorder.h"

namespace coolsynth::synth::v3
{
    namespace
    {
        // Touched and locked 16 MB at a time with a millisecond between: one large lock of the whole block made
        // the audio thread wait on the system's memory manager for as long as it took (D152).
        constexpr std::size_t sliceBytes = std::size_t { 16 } << 20;
    }

    SampleMemory::~SampleMemory() { release(); }

    std::size_t SampleMemory::freePhysicalBytes() noexcept
    {
#if JUCE_WINDOWS
        MEMORYSTATUSEX status {};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) != 0)
            return static_cast<std::size_t>(status.ullAvailPhys);
#endif
        return 0;
    }

    bool SampleMemory::reserve(std::size_t bytes, const std::function<bool()>& shouldStop)
    {
        if (base != nullptr || bytes == 0)
            return base != nullptr;
        const std::size_t pages = (bytes + pageBytes - 1) / pageBytes;
        const std::size_t size = pages * pageBytes;
        const double started = juce::Time::getMillisecondCounterHiRes();

#if JUCE_WINDOWS
        base = static_cast<std::byte*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        fromSystemPages = base != nullptr;
        if (base != nullptr)
        {
            // Room in the working set for the whole block, so it can be locked in RAM.
            SIZE_T minimum = 0, maximum = 0;
            const HANDLE process = GetCurrentProcess();
            locked = GetProcessWorkingSetSize(process, &minimum, &maximum) != 0
                     && SetProcessWorkingSetSize(process, minimum + size + (std::size_t { 64 } << 20), maximum + size + (std::size_t { 64 } << 20)) != 0;
            for (std::size_t at = 0; at < size; at += sliceBytes)
            {
                const std::size_t n = std::min(sliceBytes, size - at);
                if (locked && VirtualLock(base + at, n) == 0)
                    locked = false; // still usable, only not locked: the pages are touched below
                if (! locked)
                    for (std::size_t page = 0; page < n; page += 4096)
                        base[at + page] = std::byte { 0 };
                if (shouldStop && shouldStop()) // the program is closing: give it back and go
                {
                    if (locked)
                        VirtualUnlock(base, at + n);
                    VirtualFree(base, 0, MEM_RELEASE);
                    base = nullptr;
                    locked = false;
                    return false;
                }
                juce::Thread::sleep(1);
            }
        }
#else
        base = new (std::nothrow) std::byte[size];
        if (base != nullptr)
            for (std::size_t at = 0; at < size; at += sliceBytes)
            {
                std::memset(base + at, 0, std::min(sliceBytes, size - at));
                juce::Thread::sleep(1);
            }
#endif
        if (base == nullptr)
            return false;
        pageCount = pages;
        usedPages = 0;
        used.assign(pages, false);
        diag::record(diag::Event::message, static_cast<int>(size >> 20), locked ? 1 : 0,
                     static_cast<float>(juce::Time::getMillisecondCounterHiRes() - started), 0.0f,
                     "sample memory taken (MB, locked in RAM)");
        return true;
    }

    void SampleMemory::release() noexcept
    {
        if (base == nullptr)
            return;
#if JUCE_WINDOWS
        if (fromSystemPages)
        {
            if (locked)
                VirtualUnlock(base, pageCount * pageBytes);
            VirtualFree(base, 0, MEM_RELEASE);
        }
#else
        delete[] base;
#endif
        base = nullptr;
        pageCount = usedPages = 0;
        used.clear();
        locked = false;
    }

    std::size_t SampleMemory::largestFreeRunPages() const noexcept
    {
        std::size_t best = 0, run = 0;
        for (std::size_t i = 0; i < pageCount; ++i)
        {
            run = used[i] ? 0 : run + 1;
            best = std::max(best, run);
        }
        return best;
    }

    std::optional<SampleMemory::Run> SampleMemory::take(std::size_t pages)
    {
        if (pages == 0 || pages > pageCount - usedPages)
            return std::nullopt;
        std::size_t run = 0;
        for (std::size_t i = 0; i < pageCount; ++i)
        {
            run = used[i] ? 0 : run + 1;
            if (run == pages)
            {
                const std::size_t first = i + 1 - pages;
                for (std::size_t k = first; k <= i; ++k)
                    used[k] = true;
                usedPages += pages;
                return Run { static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(pages) };
            }
        }
        return std::nullopt;
    }

    void SampleMemory::give(const Run& run) noexcept
    {
        for (std::size_t k = run.first; k < static_cast<std::size_t>(run.first) + run.count && k < pageCount; ++k)
            if (used[k])
            {
                used[k] = false;
                --usedPages;
            }
    }
}
