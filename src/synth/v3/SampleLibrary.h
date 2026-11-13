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

    On disk a style is a folder of FLAC files and a manifest (one line per recording: the note
    it plays, its dynamic layer, its take, its length and, for sustaining styles, a loop; and one
    gain for the whole style, applied as it is read, that evens out how loudly instruments were
    recorded). The
    folders sit under the library root: the folder named in COOLSYNTH_LIBRARY, else the one
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
        int root = 60;        // the MIDI note the recording plays
        int layer = 0;        // dynamic layer, 0 the softest
        int take = 0;         // which of several recordings of this note and layer
        int frames = 0;
        double rate = 44100.0;
        int loopStart = 0, loopEnd = 0, loopFade = 0; // in frames; no loop when loopEnd is 0
        const float* data = nullptr;                  // interleaved left, right; guardFrames readable before and after
    };

    struct LoadedStyle
    {
        static constexpr int guardFrames = 16;

        std::vector<SampleZone> zones; // sorted by root, then layer, then take
        std::vector<float> storage;
        int layers = 1;

        /** The recording for a note (fractional MIDI) at a velocity 0..1; `turn` picks among takes. Null when empty. */
        [[nodiscard]] const SampleZone* pick(float midiNote, float velocity, unsigned turn) const noexcept;
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
