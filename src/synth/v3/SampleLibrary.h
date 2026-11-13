#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "InstrumentCatalogue.h"

/*
    The sampled instruments' audio (decisions D96 to D98): files beside the plugin, read into
    memory when a patch asks for them.

    On disk a style is a folder of FLAC files and a manifest. One line per recording: the note
    it sounds, the keys and the velocities that play it, its take, its length, for sustaining
    styles a loop, how many cents it is off its note, and a level correction. These come from
    the mapping files the recordings' author publishes, so an instrument plays as its author
    built it. Three lines for the whole style: a gain that evens out how loudly instruments
    were recorded, how far loudness follows velocity, and the release time the author gave it.
    The folders sit under the library root: the folder named in COOLSYNTH_LIBRARY, else the one
    written in "CoolSynth/library.path" in the user's application data, else "CoolSynth/Library"
    in the common application data.

    In memory a style is every one of its recordings as 32-bit floats, two channels, with silent
    guard frames around each so an interpolating read never leaves it. The audio thread asks for
    a style with request(): it gets the style if it is in memory and nothing otherwise, never
    waits, and the asking alone makes a background thread load it. A style nothing has asked for
    in a minute and a half is dropped once memory runs over the budget; a voice checks every
    block that the style it plays is still the one in memory.
*/
namespace coolsynth::synth::v3
{
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
        const float* data = nullptr;                  // interleaved left, right; guardFrames readable before and after
    };

    struct LoadedStyle
    {
        static constexpr int guardFrames = 16;

        std::vector<SampleZone> zones; // sorted by root, keys, velocities, then take: the takes of one zone lie together
        std::vector<float> storage;
        float velocityTrack = 1.0f;  // 0: every note at full level (an organ); 1: level follows velocity squared
        float releaseSeconds = 0.5f; // the release the author gave the instrument: for presets, not used in playing

        /** The recording for a note (fractional MIDI) at a velocity 0..1; `turn` picks among takes. Null when empty.
            A zone whose keys and velocities hold the note wins; outside every zone the nearest recorded note plays. */
        [[nodiscard]] const SampleZone* pick(float midiNote, float velocity, unsigned turn) const noexcept;
        /** The level a velocity 0..1 plays at: the square law the mapping files are built on, as far as the style follows it. */
        [[nodiscard]] float velocityGain(float velocity) const noexcept
        {
            const float v = velocity < 0.0f ? 0.0f : velocity > 1.0f ? 1.0f : velocity;
            return 1.0f - velocityTrack + velocityTrack * v * v;
        }
        [[nodiscard]] std::size_t bytes() const noexcept { return storage.size() * sizeof(float); }
    };

    class SampleLibrary final : private juce::Thread
    {
    public:
        static constexpr int maxInstruments = 128;
        static constexpr std::uint32_t idleMillisecondsBeforeDrop = 90000;
        static constexpr std::size_t memoryBudgetBytes = std::size_t { 3 } << 30; // 3 GB

        static SampleLibrary& instance();
        ~SampleLibrary() override;

        /** Where the library is looked for by default (see above). */
        [[nodiscard]] static juce::File defaultRoot();
        [[nodiscard]] juce::File root() const;
        /** Points the library somewhere else and forgets what was loaded. Only while nothing is playing. */
        void setRoot(const juce::File& folder);

        /** Audio thread, never waits: the style if it is in memory, else null; either way it is wanted from now on. */
        [[nodiscard]] const LoadedStyle* request(int instrument, int style) noexcept;
        /** Waits until the style is in memory or known to be missing: for tests and offline rendering. */
        const LoadedStyle* loadNow(int instrument, int style, int timeoutMilliseconds = 60000);
        /** True if the style's manifest is on disk. */
        [[nodiscard]] bool installed(int instrument, int style) const;
        /** True once a load was tried and the style was not there. */
        [[nodiscard]] bool missing(int instrument, int style) const noexcept;
        [[nodiscard]] std::size_t bytesInMemory() const noexcept { return loadedBytes.load(std::memory_order_relaxed); }

        /** Reads one style from a folder: what the loader thread does, usable on its own. Null if there is no manifest. */
        [[nodiscard]] static std::unique_ptr<LoadedStyle> readStyle(const juce::File& folder);

    private:
        struct Slot
        {
            std::atomic<const LoadedStyle*> loaded { nullptr };
            std::atomic<bool> wanted { false };
            std::atomic<bool> notThere { false };
            std::atomic<std::uint32_t> lastAsked { 0 };
            std::unique_ptr<LoadedStyle> owner; // loader thread only
        };

        SampleLibrary();
        void run() override;
        [[nodiscard]] Slot* slotFor(int instrument, int style) noexcept;
        [[nodiscard]] const Slot* slotFor(int instrument, int style) const noexcept;
        void dropIdle();
        void dropAll();

        std::array<Slot, maxInstruments * maxInstrumentStyles> slots;
        mutable juce::CriticalSection rootLock;
        juce::File rootFolder;
        std::atomic<std::size_t> loadedBytes { 0 };
    };
}
