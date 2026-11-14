#include "FlightRecorder.h"

#include <cstring>

namespace coolsynth::diag
{
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

    void FlightRecorder::record(Event event, std::int32_t a, std::int32_t b, float x, float y, const char* text) noexcept
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
                return "second: worst block " + ms(r.x) + " of " + ms(r.y) + ", voices " + juce::String(r.a) + ", peak " + juce::String(r.text) + " dB"
                       + (r.b > 0 ? ", CLIPPED " + juce::String(r.b) + " samples" : juce::String());
            case Event::nonFinite: return "NON-FINITE OUTPUT: " + juce::String(r.a) + " samples (NaN or infinity), silenced";
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
        while (! threadShouldExit())
        {
            wait(250);
            drain();
        }
    }
}
