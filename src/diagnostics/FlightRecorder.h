#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>

/*
    The flight recorder: tripwires and events from every thread, kept in a ring buffer in memory and written to a
    log file by a background thread (%LOCALAPPDATA%\CoolSynth\logs, one file per session, the last 20 kept).

    record() is wait-free and allocation-free, so the audio thread may call it: it claims a slot with one atomic
    add, fills a fixed-size record and publishes it. When the writer falls behind, the oldest records are
    overwritten and counted as lost. The pattern: a lock-free ring from the real-time thread to a logging thread,
    as JACK, PortAudio and CoreAudio hand data off (decision D150).

    It runs in the standalone app and the plugin, and in tools and tests when COOLSYNTH_FLIGHT_LOG is set.
*/
namespace coolsynth::diag
{
    enum class Event : std::uint16_t
    {
        sessionStart,     // text: build
        blockSlow,        // x: ms, y: budget ms, a: stage code of the slowest stage, b: that stage's ms * 100
        blockOverrun,     // as blockSlow, over the budget: an audible dropout on a live device
        secondSummary,    // x: worst block ms, y: budget ms, a: peak voices, b: clipped samples, text: peak dB
        nonFinite,        // a: samples that were NaN or infinite in the output
        presetLoaded,     // a: program index, text: name
        slotType,         // a: kind (0 generator, 1 processor), b: (old << 8) | new type
        styleLoaded,      // a: instrument, b: style, x: MB, y: ms to read
        styleDropped,     // a: instrument, b: style, x: MB
        styleMissing,     // a: instrument, b: style
        noteOn,           // a: note, b: velocity
        noteOff,          // a: note
        velocityFixed,    // a: note, b: the velocity used instead of 1
        miniLab,          // text: what happened
        message           // text
    };

    /** Engine stages, for blockSlow's stage code. */
    enum Stage : int { stageNone = 0, stagePartA = 1, stagePartB = 2, stageInsertsA = 3, stageInsertsB = 4, stageMaster1 = 10 };

    struct Record
    {
        std::atomic<std::uint64_t> sequence { 0 }; // index + 1 once written; 0 while being written
        double milliseconds = 0.0;                 // Time::getMillisecondCounterHiRes()
        std::uint64_t thread = 0;
        Event event = Event::message;
        std::int32_t a = 0, b = 0;
        float x = 0.0f, y = 0.0f;
        char text[48] {};
    };

    class FlightRecorder final : private juce::Thread
    {
    public:
        static FlightRecorder& instance();

        /** Starts writing to a new log file, once; later calls do nothing. Message thread or any thread. */
        void start();
        [[nodiscard]] bool running() const noexcept { return started.load(std::memory_order_acquire); }
        [[nodiscard]] juce::File logFile() const { return file; }

        /** Any thread, wait-free. Records nothing until start(). */
        void record(Event event, std::int32_t a = 0, std::int32_t b = 0, float x = 0.0f, float y = 0.0f,
                    const char* text = nullptr) noexcept;

        /** Writes what is waiting now (tests, shutdown). */
        void flush();

        ~FlightRecorder() override;

    private:
        FlightRecorder();
        void run() override;
        void drain();
        [[nodiscard]] juce::String describe(const Record& r) const;

        static constexpr std::size_t capacity = 1u << 14; // 16384 records
        std::unique_ptr<std::array<Record, capacity>> ring;
        std::atomic<std::uint64_t> head { 0 };
        std::uint64_t readIndex = 0;
        std::uint64_t lost = 0;
        std::atomic<bool> started { false };
        juce::File file;
        std::unique_ptr<juce::FileOutputStream> out;
        juce::CriticalSection writeLock;
        double startMs = 0.0;
    };

    /** Shorthand: FlightRecorder::instance().record(...). */
    inline void record(Event event, std::int32_t a = 0, std::int32_t b = 0, float x = 0.0f, float y = 0.0f, const char* text = nullptr) noexcept
    {
        FlightRecorder::instance().record(event, a, b, x, y, text);
    }
}
