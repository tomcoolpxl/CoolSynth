#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "InstrumentCatalogue.h"
#include "SampleMemory.h"
#include "SamplePack.h"

/*
    The sampled instruments' audio (decisions D96 to D98, D152, D155, D162, D163): files beside the
    plugin, read into the sample memory when a sound asks for them.

    On disk a style is a folder with a manifest and its recordings (FLAC or WAV), and, once the
    library is packed (tools/PackLibrary.cpp), one style.cspack holding its table and its recordings
    as FLAC streams, with library.index at the root giving every style's size in memory
    (SamplePack.h). A packed style is read from its pack, otherwise from its manifest. The folders sit
    under the library root: the folder named in COOLSYNTH_LIBRARY, else the one written in
    "CoolSynth/library.path" in the user's application data, else "CoolSynth/Library" in the common
    application data.

    In memory a recording keeps its own depth and channels, losslessly: 16-bit recordings as 16-bit
    numbers, 24-bit ones packed in three bytes, mono as mono, with silent guard frames around it so
    an interpolating read never leaves it; its level corrections are applied as it is read. All of
    it lives in one sample memory (SampleMemory.h), like a hardware sampler's: taken once, at most half
    the free memory and never more than the whole library, locked in RAM, never given back while
    CoolSynth runs (D155; the memory check kept, D162). A computer too small for the largest
    instrument gets a warning. A style is read by all cores but two at once, one recording each.

    The audio thread asks for a style with request(): it gets the style if it is in memory and
    nothing otherwise, never waits, and the asking alone makes a background thread read it in. A
    style stays in memory once read. When a new one does not fit, the one asked for longest ago gives
    its pages up, but never one a loaded sound still names (every sound asks for its instruments
    each block, D162), never one a note is still playing (its `users`), and only after every audio
    block that might have picked it up has ended (ReadScope). A sound that needs more than the memory
    holds is told which instrument does not fit (memoryWarning), never left silent.
*/
namespace coolsynth::synth::v3
{
    /** How a recording's numbers are held. */
    enum class SampleFormat : std::uint8_t { float32 = 0, int16, int24 };

    struct SampleZone
    {
        int root = 60;                           // the MIDI note the recording sounds
        int lowKey = 0, highKey = 127;           // the keys that play it
        int lowVelocity = 0, highVelocity = 127; // the MIDI velocities that play it
        int take = 0;                            // which of several recordings of these keys and velocities
        int frames = 0;
        double rate = 44100.0;
        int loopStart = 0, loopEnd = 0, loopFade = 0; // in frames; no loop when loopEnd is 0
        float tuneCents = 0.0f;                       // how far above its note the recording sounds; taken out as it plays
        const void* data = nullptr;                   // the first frame, `channels` interleaved; guardFrames readable before and after
        SampleFormat format = SampleFormat::float32;
        int channels = 2;                             // 1: mono, both sides the same
        float scale = 1.0f; // what one stored number is worth: the level corrections over the format's full scale

        /** One sample as the generator hears it (frame from -guardFrames, channel 0 or 1): for tests and tools. */
        [[nodiscard]] float at(int frame, int channel) const noexcept;
    };

    struct LoadedStyle
    {
        static constexpr int guardFrames = samplePack::guardFrames;

        std::vector<SampleZone> zones; // sorted by root, keys, velocities, then take: the takes of one zone lie together
        std::vector<std::unique_ptr<std::byte[]>> ownBlocks; // memory of its own: the user's recordings, a style read alone
        std::vector<SampleMemory::Run> runs;                 // or its pages in the sample memory
        std::size_t storedBytes = 0;
        /** Notes playing it now: its memory is reused only at 0. Audio thread. */
        mutable std::atomic<int> users { 0 };

        /** Memory of its own for one float stereo recording of `frames` frames plus silent guard frames on both sides;
            returns where the frames start. (The user's own sounds, UserLibrary.h.) */
        float* addRecording(int frames);
        float velocityTrack = 1.0f;  // 0: every note at full level (an organ); 1: level follows velocity squared
        float releaseSeconds = 0.5f; // the release the author gave the instrument: for presets, not used in playing

        /** The user's own sound (UserLibrary.h, D207): where its attacks are, for slices cut at them. */
        struct Attack
        {
            int frame = 0;         // where the sound rises, in the recording's frames
            float strength = 0.0f; // 1 for the file's strongest attack
        };
        std::vector<Attack> attacks; // in time order
        /** A folder of the user's files played as a kit: one zone per file, in name order, one per key (D207). */
        bool kit = false;

        /** The recording for a note (fractional MIDI) at a velocity 0..1; `turn` picks among takes. Null when empty.
            A zone whose keys and velocities hold the note wins; outside every zone the nearest recorded note plays. */
        [[nodiscard]] const SampleZone* pick(float midiNote, float velocity, unsigned turn) const noexcept;
        /** The level a velocity 0..1 plays at: the square law the mapping files are built on, as far as the style follows it. */
        [[nodiscard]] float velocityGain(float velocity) const noexcept
        {
            const float v = velocity < 0.0f ? 0.0f : velocity > 1.0f ? 1.0f : velocity;
            return 1.0f - velocityTrack + velocityTrack * v * v;
        }
        [[nodiscard]] std::size_t bytes() const noexcept { return storedBytes; }
        /** Puts the zones in pick()'s order. */
        void sortZones();
    };

    class SampleLibrary final : private juce::Thread
    {
    public:
        static constexpr int maxInstruments = 128;
        /** A style counts as wanted while something asked for it in the last two seconds (every loaded sound asks
            every block); one not wanted is read in no more, and may give its memory up. */
        static constexpr std::uint32_t wantedMilliseconds = 2000;

        static SampleLibrary& instance();
        ~SampleLibrary() override;

        /** Where the library is looked for by default (see above). */
        [[nodiscard]] static juce::File defaultRoot();
        [[nodiscard]] juce::File root() const;
        /** Points the library somewhere else, forgets what was read and gives the sample memory back. Only while
            nothing is playing. */
        void setRoot(const juce::File& folder);

        /** Takes the sample memory now, in the background (the app calls it at start; else the first ask does). */
        void prepareMemory() noexcept;

        /** Audio thread, never waits: the style if it is in memory, else null; either way it is wanted from now on. */
        [[nodiscard]] const LoadedStyle* request(int instrument, int style) noexcept;
        /** Waits until the style is in memory or known to be missing: for tests and offline rendering. */
        const LoadedStyle* loadNow(int instrument, int style, int timeoutMilliseconds = 60000);
        /** True if the style is on disk, packed or with its manifest. */
        [[nodiscard]] bool installed(int instrument, int style) const;
        /** True once a load was tried and the style was not there, or cannot fit in the sample memory at all. */
        [[nodiscard]] bool missing(int instrument, int style) const noexcept;
        [[nodiscard]] std::size_t bytesInMemory() const noexcept { return loadedBytes.load(std::memory_order_relaxed); }

        struct MemoryReport
        {
            bool planned = false;         // the library was measured and the memory taken
            std::size_t capacity = 0;     // the sample memory
            std::size_t used = 0;
            std::size_t library = 0;      // everything installed, at its own depth
            std::size_t largest = 0;      // the largest style
            juce::String largestName;
            bool locked = false;          // locked in RAM
            bool tooSmall = false;        // the computer cannot hold the largest instrument and more
            int stylesInMemory = 0;
            int loadThreads = 0;
            juce::String problem;         // the last style that did not fit, if any
        };
        /** What the sample memory holds; any thread but the audio thread. */
        [[nodiscard]] MemoryReport memoryReport() const;
        /** A sentence for the screen when the computer is too small or a style did not fit; empty otherwise. */
        [[nodiscard]] juce::String memoryWarning() const;

        /** Held by the audio thread around every block that reads styles (EngineV3::render): memory a block may
            have picked up is reused only once every such block has ended. Never waits. */
        class ReadScope final
        {
        public:
            ReadScope() noexcept;
            ~ReadScope();
            ReadScope(const ReadScope&) = delete;
            ReadScope& operator=(const ReadScope&) = delete;

        private:
            std::uint64_t epoch = 0;
        };

        /** Reads one style from its folder (its pack, else its manifest) at its own depth, every recording in memory
            of its own: for tests and tools. Null if there is neither. */
        [[nodiscard]] static std::unique_ptr<LoadedStyle> readStyle(const juce::File& folder);

    private:
        struct Slot
        {
            std::atomic<const LoadedStyle*> loaded { nullptr };
            std::atomic<bool> notThere { false };
            std::atomic<bool> everAsked { false };
            std::atomic<std::uint32_t> lastAsked { 0 };
            std::unique_ptr<LoadedStyle> owner; // loader thread only
            std::size_t plannedBytes = 0;       // its size at its own depth, from the library's measure
            std::uint32_t waitedForRoomAt = 0;  // the last time it found no room (loader thread)
        };

        SampleLibrary();
        void run() override;
        [[nodiscard]] Slot* slotFor(int instrument, int style) noexcept;
        [[nodiscard]] const Slot* slotFor(int instrument, int style) const noexcept;
        void planMemory();
        void load(int instrument, int style, Slot& slot);
        [[nodiscard]] std::optional<SampleMemory::Run> takeMaking(std::size_t pages, const Slot* loading);
        bool dropOldest(const Slot* loading);
        bool waitForReaders();
        void dropAll();
        void updateReport();

        std::array<Slot, maxInstruments * maxInstrumentStyles> slots;
        mutable juce::CriticalSection rootLock, reportLock;
        juce::File rootFolder;
        std::atomic<std::size_t> loadedBytes { 0 };
        std::atomic<bool> memoryWanted { false };
        SampleMemory memory;  // loader thread
        bool planned = false; // loader thread
        std::unique_ptr<juce::ThreadPool> decoders; // loader thread: all cores but two, low priority
        MemoryReport report;  // under reportLock

        static std::atomic<std::uint64_t> readEpoch;
        static std::array<std::atomic<int>, 2> readersIn;
    };
}
