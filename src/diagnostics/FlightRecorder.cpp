#include "FlightRecorder.h"

#include <cstring>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
#endif

namespace coolsynth::diag
{
    namespace
    {
        /** The program's memory, read by the writer thread (D172). False where it cannot be read. */
        bool readMemory(std::uint64_t& pageFaults, double& workingSetMb, double& committedMb, double& peakMb) noexcept
        {
#if JUCE_WINDOWS
            PROCESS_MEMORY_COUNTERS_EX counters {};
            counters.cb = sizeof(counters);
            if (! K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
                return false;
            constexpr double mb = 1024.0 * 1024.0;
            pageFaults = counters.PageFaultCount;
            workingSetMb = static_cast<double>(counters.WorkingSetSize) / mb;
            committedMb = static_cast<double>(counters.PrivateUsage) / mb;
            peakMb = static_cast<double>(counters.PeakWorkingSetSize) / mb;
            return true;
#else
            juce::ignoreUnused(pageFaults, workingSetMb, committedMb, peakMb);
            return false;
#endif
        }
    }

    FlightRecorder& FlightRecorder::instance()
    {
        static FlightRecorder recorder;
        return recorder;
    }

    FlightRecorder::FlightRecorder() : juce::Thread("CoolSynth flight recorder"), ring(std::make_unique<std::array<Record, capacity>>()) {}

    FlightRecorder::~FlightRecorder()
    {
        if (running())
        {
            stopThread(2000);
            flush();
        }
    }

    void FlightRecorder::start()
    {
        bool expected = false;
        if (! started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getParentDirectory().getChildFile("Local").getChildFile("CoolSynth").getChildFile("logs");
        const auto logs = folder.isDirectory() || folder.createDirectory().wasOk()
                              ? folder
                              : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth-logs");
        logs.createDirectory();

        // keep the newest 20 sessions
        auto old = logs.findChildFiles(juce::File::findFiles, false, "session-*.log");
        std::sort(old.begin(), old.end(), [](const juce::File& x, const juce::File& y) { return x.getFileName() > y.getFileName(); });
        for (int i = 19; i < old.size(); ++i)
            old.getReference(i).deleteFile();

        file = logs.getChildFile("session-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".log");
        out = std::make_unique<juce::FileOutputStream>(file);
        startMs = juce::Time::getMillisecondCounterHiRes();
        *out << "CoolSynth flight recorder, " << juce::Time::getCurrentTime().toString(true, true, true, true)
             << ", " << juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName() << "\n"
             << "time is seconds since this file started; thread is the OS thread id\n";
        out->flush();
        startThread(juce::Thread::Priority::low);
    }

    void FlightRecorder::record(Event event, std::int32_t a, std::int32_t b, float x, float y, const char* text, float z) noexcept
    {
        if (! started.load(std::memory_order_relaxed))
            return;
        const std::uint64_t index = head.fetch_add(1, std::memory_order_relaxed);
        auto& r = (*ring)[static_cast<std::size_t>(index & (capacity - 1))];
        r.sequence.store(0, std::memory_order_relaxed); // being written
        std::atomic_thread_fence(std::memory_order_release);
        r.milliseconds = juce::Time::getMillisecondCounterHiRes();
        r.thread = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(juce::Thread::getCurrentThreadId()));
        r.event = event;
        r.a = a;
        r.b = b;
        r.x = x;
        r.y = y;
        r.z = z;
        std::size_t length = 0;
        if (text != nullptr)
            for (; length < sizeof(r.text) - 1 && text[length] != 0; ++length)
                r.text[length] = text[length];
        r.text[length] = 0;
        r.sequence.store(index + 1, std::memory_order_release);
    }

    juce::String FlightRecorder::describe(const Record& r) const
    {
        const auto ms = [](float v) { return juce::String(v, 2) + " ms"; };
        const auto stageName = [](int code)
        {
            switch (code)
            {
                case stagePartA: return juce::String("part A voices");
                case stagePartB: return juce::String("part B voices");
                case stageInsertsA: return juce::String("part A inserts");
                case stageInsertsB: return juce::String("part B inserts");
                case stageNone: return juce::String("-");
                default: return "master F" + juce::String(code - stageMaster1 + 1);
            }
        };
        switch (r.event)
        {
            case Event::sessionStart: return juce::String("session start, build ") + r.text;
            case Event::blockSlow:
                return "SLOW BLOCK " + ms(r.x) + " of " + ms(r.y) + ", slowest stage " + stageName(r.a) + " " + ms(static_cast<float>(r.b) / 100.0f);
            case Event::blockOverrun:
                return "DROPOUT block took " + ms(r.x) + " of " + ms(r.y) + ", slowest stage " + stageName(r.a) + " " + ms(static_cast<float>(r.b) / 100.0f);
            case Event::secondSummary:
                return "second: worst block " + ms(r.x) + " of " + ms(r.y) + ", voices " + juce::String(r.a) + ", peak " + juce::String(r.z, 1) + " dB"
                       + (r.b > 0 ? ", CLIPPED " + juce::String(r.b) + " samples" : juce::String());
            case Event::nonFinite: return "NON-FINITE OUTPUT: " + juce::String(r.a) + " samples (NaN or infinity), silenced";
            case Event::stageBroken:
                return "BROKEN VALUES in " + stageName(r.a) + ": " + juce::String(r.b) + " samples (NaN or infinity), that stage was reset";
            case Event::threadLoad:
                if (r.a == 0)
                    return "audio thread " + juce::String(r.x, 1) + "% of the second"
                           + (r.z > 0.0f ? ", helpers shared " + juce::String(static_cast<int>(r.z)) + " blocks, worst wait for one " + ms(r.y)
                                         : juce::String(", no helpers needed"));
                return "helper " + juce::String(r.a) + " " + juce::String(r.x, 1) + "% of the second, worst wake " + ms(r.y)
                       + (r.z > 0.0f ? ", LATE " + juce::String(static_cast<int>(r.z)) + " times (over 1 ms)" : juce::String());
            case Event::memoryUse:
                return "memory: working set " + juce::String(juce::roundToInt(r.x)) + " MB (peak " + juce::String(juce::roundToInt(r.z)) + "), committed "
                       + juce::String(juce::roundToInt(r.y)) + " MB, " + juce::String(r.a) + " page faults in the last second";
            case Event::eventsDropped:
                return "MIDI FLOOD: " + juce::String(r.a) + " events found no room in a block (" + juce::String(r.b) + " kept, releases never dropped)";
            case Event::presetLoaded: return "preset " + juce::String(r.a) + " loaded: " + r.text;
            case Event::slotType:
                return juce::String(r.a == 0 ? "generator" : "processor") + " slot type " + juce::String((r.b >> 8) & 0xff) + " -> " + juce::String(r.b & 0xff);
            case Event::styleLoaded:
                return "instrument " + juce::String(r.a) + " style " + juce::String(r.b) + " read in: " + juce::String(r.x, 1) + " MB in " + ms(r.y);
            case Event::styleDropped:
                return "instrument " + juce::String(r.a) + " style " + juce::String(r.b) + " dropped: " + juce::String(r.x, 1) + " MB freed";
            case Event::styleMissing: return "instrument " + juce::String(r.a) + " style " + juce::String(r.b) + " not installed";
            case Event::noteOn: return "note on " + juce::String(r.a) + " velocity " + juce::String(r.b);
            case Event::noteOff: return "note off " + juce::String(r.a);
            case Event::velocityFixed: return "MiniLab velocity-1 fault on note " + juce::String(r.a) + ": played at " + juce::String(r.b);
            case Event::miniLab: return juce::String("MiniLab: ") + r.text;
            case Event::message:
                return juce::String(r.text) + ((r.a != 0 || r.b != 0) ? " (" + juce::String(r.a) + ", " + juce::String(r.b) + ")" : juce::String());
        }
        return "?";
    }

    void FlightRecorder::drain()
    {
        const juce::ScopedLock lock(writeLock);
        if (out == nullptr)
            return;
        const std::uint64_t end = head.load(std::memory_order_acquire);
        if (end - readIndex > capacity)
        {
            lost += end - readIndex - capacity;
            readIndex = end - capacity;
        }
        juce::String text;
        while (readIndex < end)
        {
            auto& r = (*ring)[static_cast<std::size_t>(readIndex & (capacity - 1))];
            const auto sequence = r.sequence.load(std::memory_order_acquire);
            if (sequence == 0)
                break; // still being written: next time
            if (sequence != readIndex + 1)
            {
                ++lost; // overwritten by a newer record
                ++readIndex;
                continue;
            }
            Record copy;
            copy.milliseconds = r.milliseconds;
            copy.thread = r.thread;
            copy.event = r.event;
            copy.a = r.a;
            copy.b = r.b;
            copy.x = r.x;
            copy.y = r.y;
            copy.z = r.z;
            std::memcpy(copy.text, r.text, sizeof(copy.text));
            copy.text[sizeof(copy.text) - 1] = 0;
            if (r.sequence.load(std::memory_order_acquire) != readIndex + 1)
            {
                ++lost; // overwritten while copying
                ++readIndex;
                continue;
            }
            text << juce::String((copy.milliseconds - startMs) / 1000.0, 3).paddedLeft(' ', 9) << "  t"
                 << juce::String(static_cast<juce::int64>(copy.thread % 100000)).paddedLeft(' ', 5) << "  " << describe(copy) << "\n";
            ++readIndex;
        }
        if (lost > 0)
        {
            text << "          (" << juce::String(static_cast<juce::int64>(lost)) << " records lost: the writer fell behind)\n";
            lost = 0;
        }
        if (text.isNotEmpty())
        {
            *out << text;
            out->flush();
        }
    }

    void FlightRecorder::flush() { drain(); }

    void FlightRecorder::run()
    {
        // Once a second, the program's memory: a line when pages were faulted in or the working set moved by a
        // megabyte, else every ten seconds (D172). Page faults count soft ones too (memory touched for the first
        // time); the sample memory is locked, so steady playing should fault little.
        std::uint64_t lastFaults = 0;
        double lastLoggedMb = -1.0;
        int quietSeconds = 0;
        for (int tick = 0; ! threadShouldExit(); ++tick)
        {
            wait(250);
            if (tick % 4 == 0)
            {
                std::uint64_t faults = 0;
                double workingSet = 0.0, committed = 0.0, peak = 0.0;
                if (readMemory(faults, workingSet, committed, peak))
                {
                    const auto newFaults = lastFaults == 0 ? 0 : faults - lastFaults;
                    lastFaults = faults;
                    if (newFaults > 0 || std::abs(workingSet - lastLoggedMb) >= 1.0 || ++quietSeconds >= 10)
                    {
                        record(Event::memoryUse, static_cast<std::int32_t>(std::min<std::uint64_t>(newFaults, 0x7fffffff)), 0,
                               static_cast<float>(workingSet), static_cast<float>(committed), nullptr, static_cast<float>(peak));
                        lastLoggedMb = workingSet;
                        quietSeconds = 0;
                    }
                }
            }
            drain();
        }
    }
}
