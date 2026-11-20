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

// The wider watch (D222): the C heap through the debug runtime's allocation hook; locks, waits, sleeps and files by
// pointing the import tables of the program and of Microsoft's C and C++ runtimes at counting stand-ins, each of which
// calls the real function. Clang's RealtimeSanitizer and Tracktion's rtcheck do this by interposition on Linux and
// macOS only. Calls through GetProcAddress, or from Windows' own libraries, are not seen.
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <crtdbg.h>
 #include <dbghelp.h>
 #include <algorithm>
 #include <cstring>
 #include <string>
 #pragma comment(lib, "dbghelp.lib")

namespace
{
    namespace diag = coolsynth::diag;

    template <typename Function>
    struct Original
    {
        Function call = nullptr;
    };

    // Where the first few counted calls came from, named by firstBlockingCallers once the blocks are done.
    constexpr int keptStacks = 4, keptFrames = 12;
    void* stacks[keptStacks][keptFrames] {};

    void note(diag::BlockingCall kind) noexcept
    {
        const int index = diag::noteBlockingCall(kind);
        if (index >= 0 && index < keptStacks)
            RtlCaptureStackBackTrace(1, keptFrames, stacks[index], nullptr);
    }

    Original<decltype(&EnterCriticalSection)> enterCriticalSection;
    Original<decltype(&AcquireSRWLockExclusive)> acquireSrwExclusive;
    Original<decltype(&AcquireSRWLockShared)> acquireSrwShared;
    Original<decltype(&SleepConditionVariableSRW)> sleepConditionSrw;
    Original<decltype(&SleepConditionVariableCS)> sleepConditionCs;
    Original<decltype(&WaitForSingleObject)> waitSingle;
    Original<decltype(&WaitForSingleObjectEx)> waitSingleEx;
    Original<decltype(&WaitForMultipleObjects)> waitMultiple;
    Original<decltype(&WaitForMultipleObjectsEx)> waitMultipleEx;
    Original<decltype(&Sleep)> sleepPlain;
    Original<decltype(&SleepEx)> sleepEx;
    Original<decltype(&CreateFileW)> createFileW;
    Original<decltype(&CreateFileA)> createFileA;
    Original<decltype(&ReadFile)> readFile;
    Original<decltype(&WriteFile)> writeFile;
    Original<decltype(&GetFileAttributesW)> fileAttributes;
    Original<decltype(&GetFileAttributesExW)> fileAttributesEx;
    Original<decltype(&FindFirstFileW)> findFirstFile;
    Original<decltype(&::free)> heapFree;
    Original<decltype(&::malloc)> heapMalloc;
    Original<decltype(&::calloc)> heapCalloc;
    Original<decltype(&::realloc)> heapRealloc;

    // free(nullptr) does nothing, as the C standard says; the debug runtime takes its heap lock for it all the same,
    // which the release runtime does not, so every buffer view's empty heap block looked like a lock (D222).
    void __cdecl countedFree(void* block)
    {
        if (block == nullptr)
            return;
        note(diag::BlockingCall::heap);
        heapFree.call(block);
    }
    void* __cdecl countedMalloc(size_t size) { note(diag::BlockingCall::heap); return heapMalloc.call(size); }
    void* __cdecl countedCalloc(size_t count, size_t size) { note(diag::BlockingCall::heap); return heapCalloc.call(count, size); }
    void* __cdecl countedRealloc(void* block, size_t size) { note(diag::BlockingCall::heap); return heapRealloc.call(block, size); }

    // A release build carries the C runtime inside it (/MT): its malloc is no import to point elsewhere, and there is
    // no debug allocation hook. Its heap is watched where that runtime asks Windows for memory (the 0.4.0 release
    // run, D228).
    Original<decltype(&HeapAlloc)> heapAlloc;
    Original<decltype(&HeapReAlloc)> heapReAlloc;
    Original<decltype(&HeapFree)> heapFreeBlock;
    LPVOID WINAPI countedHeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) { note(diag::BlockingCall::heap); return heapAlloc.call(heap, flags, size); }
    LPVOID WINAPI countedHeapReAlloc(HANDLE heap, DWORD flags, LPVOID block, SIZE_T size)
    {
        note(diag::BlockingCall::heap);
        return heapReAlloc.call(heap, flags, block, size);
    }
    BOOL WINAPI countedHeapFree(HANDLE heap, DWORD flags, LPVOID block)
    {
        if (block != nullptr)
            note(diag::BlockingCall::heap);
        return heapFreeBlock.call(heap, flags, block);
    }

    void WINAPI countedEnterCriticalSection(LPCRITICAL_SECTION cs) { note(diag::BlockingCall::lock); enterCriticalSection.call(cs); }
    void WINAPI countedAcquireSrwExclusive(PSRWLOCK lock) { note(diag::BlockingCall::lock); acquireSrwExclusive.call(lock); }
    void WINAPI countedAcquireSrwShared(PSRWLOCK lock) { note(diag::BlockingCall::lock); acquireSrwShared.call(lock); }
    BOOL WINAPI countedSleepConditionSrw(PCONDITION_VARIABLE c, PSRWLOCK l, DWORD ms, ULONG flags)
    {
        note(diag::BlockingCall::wait);
        return sleepConditionSrw.call(c, l, ms, flags);
    }
    BOOL WINAPI countedSleepConditionCs(PCONDITION_VARIABLE c, PCRITICAL_SECTION cs, DWORD ms)
    {
        note(diag::BlockingCall::wait);
        return sleepConditionCs.call(c, cs, ms);
    }
    DWORD WINAPI countedWaitSingle(HANDLE h, DWORD ms) { note(diag::BlockingCall::wait); return waitSingle.call(h, ms); }
    DWORD WINAPI countedWaitSingleEx(HANDLE h, DWORD ms, BOOL alertable)
    {
        note(diag::BlockingCall::wait);
        return waitSingleEx.call(h, ms, alertable);
    }
    DWORD WINAPI countedWaitMultiple(DWORD n, const HANDLE* h, BOOL all, DWORD ms)
    {
        note(diag::BlockingCall::wait);
        return waitMultiple.call(n, h, all, ms);
    }
    DWORD WINAPI countedWaitMultipleEx(DWORD n, const HANDLE* h, BOOL all, DWORD ms, BOOL alertable)
    {
        note(diag::BlockingCall::wait);
        return waitMultipleEx.call(n, h, all, ms, alertable);
    }
    void WINAPI countedSleep(DWORD ms) { note(diag::BlockingCall::sleep); sleepPlain.call(ms); }
    DWORD WINAPI countedSleepEx(DWORD ms, BOOL alertable) { note(diag::BlockingCall::sleep); return sleepEx.call(ms, alertable); }
    HANDLE WINAPI countedCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE templ)
    {
        note(diag::BlockingCall::file);
        return createFileW.call(name, access, share, sa, disposition, flags, templ);
    }
    HANDLE WINAPI countedCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE templ)
    {
        note(diag::BlockingCall::file);
        return createFileA.call(name, access, share, sa, disposition, flags, templ);
    }
    BOOL WINAPI countedReadFile(HANDLE h, LPVOID data, DWORD size, LPDWORD read, LPOVERLAPPED o)
    {
        note(diag::BlockingCall::file);
        return readFile.call(h, data, size, read, o);
    }
    BOOL WINAPI countedWriteFile(HANDLE h, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED o)
    {
        note(diag::BlockingCall::file);
        return writeFile.call(h, data, size, written, o);
    }
    DWORD WINAPI countedFileAttributes(LPCWSTR name) { note(diag::BlockingCall::file); return fileAttributes.call(name); }
    BOOL WINAPI countedFileAttributesEx(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
    {
        note(diag::BlockingCall::file);
        return fileAttributesEx.call(name, level, info);
    }
    HANDLE WINAPI countedFindFirstFile(LPCWSTR name, LPWIN32_FIND_DATAW data)
    {
        note(diag::BlockingCall::file);
        return findFirstFile.call(name, data);
    }

    struct Hook
    {
        const char* name;
        void** original;
        void* counted;
    };

    template <typename Function>
    Hook hook(const char* name, Original<Function>& original, Function counted)
    {
        return { name, reinterpret_cast<void**>(&original.call), reinterpret_cast<void*>(counted) };
    }

    /** Points every import of a watched function in this module at its counting stand-in. */
    void patchImports(HMODULE module, Hook* hooks, std::size_t hookCount)
    {
        if (module == nullptr)
            return;
        auto* base = reinterpret_cast<BYTE*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0)
            return;
        for (auto* library = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); library->Name != 0; ++library)
        {
            if (library->OriginalFirstThunk == 0)
                continue;
            auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + library->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + library->FirstThunk);
            for (; names->u1.AddressOfData != 0; ++names, ++slots)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    continue;
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                const auto* name = reinterpret_cast<const char*>(byName->Name);
                for (std::size_t h = 0; h < hookCount; ++h)
                {
                    if (std::strcmp(name, hooks[h].name) != 0)
                        continue;
                    auto* slot = reinterpret_cast<void**>(&slots->u1.Function);
                    if (*slot == hooks[h].counted)
                        break;
                    if (*hooks[h].original == nullptr)
                        *hooks[h].original = *slot;
                    DWORD protection = 0;
                    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
                    {
                        *slot = hooks[h].counted;
                        VirtualProtect(slot, sizeof(void*), protection, &protection);
                    }
                    break;
                }
            }
        }
    }

    int countHeap(int, void*, size_t, int, long, const unsigned char*, int)
    {
        note(diag::BlockingCall::heap);
        return TRUE;
    }
}

const char* coolsynth::diag::firstBlockingCallers()
{
    static std::string text;
    text.clear();
    const HANDLE process = GetCurrentProcess();
    static const bool symbols = [process]
    {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        return SymInitialize(process, nullptr, TRUE) != FALSE;
    }();
    const int count = std::min(keptStacks, audioBlockingCalls.count.load());
    for (int i = 0; i < count; ++i)
    {
        text += std::string("\n  [") + blockingCallName(audioBlockingCalls.firstKinds[static_cast<std::size_t>(i)].load()) + "] ";
        for (int f = 0; f < keptFrames && stacks[i][f] != nullptr; ++f)
        {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] {};
            auto* info = reinterpret_cast<SYMBOL_INFO*>(buffer);
            info->SizeOfStruct = sizeof(SYMBOL_INFO);
            info->MaxNameLen = 255;
            DWORD64 displacement = 0;
            text += symbols && SymFromAddr(process, reinterpret_cast<DWORD64>(stacks[i][f]), &displacement, info) ? info->Name : "?";
            text += " < ";
        }
    }
    return text.c_str();
}

bool coolsynth::diag::watchBlockingCalls()
{
    static const bool watching = []
    {
        Hook hooks[] {
            hook("EnterCriticalSection", enterCriticalSection, &countedEnterCriticalSection),
            hook("AcquireSRWLockExclusive", acquireSrwExclusive, &countedAcquireSrwExclusive),
            hook("AcquireSRWLockShared", acquireSrwShared, &countedAcquireSrwShared),
            hook("SleepConditionVariableSRW", sleepConditionSrw, &countedSleepConditionSrw),
            hook("SleepConditionVariableCS", sleepConditionCs, &countedSleepConditionCs),
            hook("WaitForSingleObject", waitSingle, &countedWaitSingle),
            hook("WaitForSingleObjectEx", waitSingleEx, &countedWaitSingleEx),
            hook("WaitForMultipleObjects", waitMultiple, &countedWaitMultiple),
            hook("WaitForMultipleObjectsEx", waitMultipleEx, &countedWaitMultipleEx),
            hook("Sleep", sleepPlain, &countedSleep),
            hook("SleepEx", sleepEx, &countedSleepEx),
            hook("CreateFileW", createFileW, &countedCreateFileW),
            hook("CreateFileA", createFileA, &countedCreateFileA),
            hook("ReadFile", readFile, &countedReadFile),
            hook("WriteFile", writeFile, &countedWriteFile),
            hook("GetFileAttributesW", fileAttributes, &countedFileAttributes),
            hook("GetFileAttributesExW", fileAttributesEx, &countedFileAttributesEx),
            hook("FindFirstFileW", findFirstFile, &countedFindFirstFile),
            hook("free", heapFree, &countedFree),
            hook("malloc", heapMalloc, &countedMalloc),
            hook("calloc", heapCalloc, &countedCalloc),
            hook("realloc", heapRealloc, &countedRealloc),
        };
        // The program itself (JUCE is linked into it) and the runtimes it calls through, whose own locks (std::mutex,
        // the C library's file and heap locks) would otherwise be out of sight.
        for (const auto* module : { static_cast<const wchar_t*>(nullptr), L"msvcp140d_atomic_wait.dll",
                                    L"ucrtbased.dll", L"vcruntime140d.dll", L"vcruntime140_1d.dll",
                                    L"msvcp140_atomic_wait.dll", L"ucrtbase.dll", L"vcruntime140.dll" })
            patchImports(GetModuleHandleW(module), hooks, std::size(hooks));
        // The C++ library all but its critical sections: those are the debug build's iterator checks (std::_Lockit,
        // every vector iterator), gone from a release build; its mutexes are slim reader/writer locks, still watched.
        for (const auto* module : { L"msvcp140d.dll", L"msvcp140.dll" })
            patchImports(GetModuleHandleW(module), hooks + 1, std::size(hooks) - 1);
       #if defined(_DEBUG)
        _CrtSetAllocHook(&countHeap);
       #else
        Hook windowsHeap[] {
            hook("HeapAlloc", heapAlloc, &countedHeapAlloc),
            hook("HeapReAlloc", heapReAlloc, &countedHeapReAlloc),
            hook("HeapFree", heapFreeBlock, &countedHeapFree),
        };
        patchImports(GetModuleHandleW(nullptr), windowsHeap, std::size(windowsHeap));
       #endif
        return enterCriticalSection.call != nullptr && sleepPlain.call != nullptr;
    }();
    return watching;
}
#else
bool coolsynth::diag::watchBlockingCalls() { return false; }
const char* coolsynth::diag::firstBlockingCallers() { return ""; }
#endif
