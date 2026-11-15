// The test program's operator new (D161): every allocation on a thread inside an audio block is counted
// (diagnostics/AudioThreadAlarm.h). Also built into the render tool.

#include <cstdlib>
#include <new>

#include "diagnostics/AudioThreadAlarm.h"

namespace
{
    void* allocate(std::size_t size)
    {
        coolsynth::diag::noteAllocation(size);
        if (void* p = std::malloc(size == 0 ? 1 : size))
            return p;
        throw std::bad_alloc();
    }

    void* allocateNoThrow(std::size_t size) noexcept
    {
        coolsynth::diag::noteAllocation(size);
        return std::malloc(size == 0 ? 1 : size);
    }
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocateNoThrow(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocateNoThrow(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
