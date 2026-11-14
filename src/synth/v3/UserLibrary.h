#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "SampleLibrary.h"
#include "Wavetables.h"

/*
    The user's own sounds (decision D137): audio files in the User folder of the sample library
    (<library root>/User, see SampleLibrary), named in a preset by their path inside that folder,
    so a preset shared with someone who has the same files plays the same.

    A generator slot that names a file gets a handle here. The file loads on a background thread,
    once however many slots use it, into both forms a slot can play:

        a sample    the whole file as one recording over every key: its root note and loop from
                    the file's own sampler chunk ("smpl"), else C4 and no loop; stereo as it is
        a wavetable the common WAV kind (Serum, Vital and most packs): frames of 2048 samples,
                    or the frame size its "clm " chunk names, up to 256 frames, the first channel;
                    a shorter file is one single cycle, stretched to 2048. Every frame exists at
                    the factory bank's 11 band limits, its offset removed; the whole table is
                    scaled so its loudest frame peaks at 1, so frames keep their levels against
                    each other. Only files of at most 256 frames get a wavetable form.
        grains      the first minute in mono, at three band limits, for the grain generator

    The audio thread asks with the handle and gets what is in memory or nothing: it never waits.
    A sound no slot has held for five seconds is dropped, and its memory is freed two seconds
    later, when no block can still be reading it.
*/
namespace coolsynth::synth::v3
{
    /** A wavetable of the user's: frames laid out as the factory bank's, each at every band limit. */
    struct UserWavetable
    {
        int frameCount = 0;
        std::vector<float> data; // frameCount * levels * stride

        /** Points at sample 0 of a frame; samples -1 .. tableSize + 1 are readable. */
        [[nodiscard]] const float* frame(int frameIndex, int level) const noexcept
        {
            const int f = std::clamp(frameIndex, 0, std::max(0, frameCount - 1));
            const int l = std::clamp(level, 0, WavetableBank::levels - 1);
            return data.data() + static_cast<std::size_t>((f * WavetableBank::levels + l) * WavetableBank::stride) + 1;
        }
        [[nodiscard]] std::size_t bytes() const noexcept { return data.size() * sizeof(float); }
    };

    /** The user's sample as the grain generator reads it: mono, the first minute, at three band limits like the
        factory samples (the full band, then about a fifth and a tenth of the file's rate). */
    struct UserGrainSource
    {
        static constexpr int levels = 3;
        static constexpr int guardBefore = 1, guardAfter = 3;
        static constexpr double maxSeconds = 60.0;

        int length = 0;          // frames
        double rate = 48000.0;   // the file's sample rate
        double rootHz = 261.6255653005986; // the pitch the recording sounds at its own speed
        std::vector<float> storage;
        std::array<const float*, levels> level {}; // frame 0 of each; frames -1 .. length + 2 readable, wrapping

        [[nodiscard]] const float* data(int levelIndex) const noexcept { return level[static_cast<std::size_t>(std::clamp(levelIndex, 0, levels - 1))]; }
        [[nodiscard]] std::size_t bytes() const noexcept { return storage.size() * sizeof(float); }
    };

    class UserLibrary final : private juce::Thread
    {
    public:
        static constexpr int maxSounds = 32;
        static constexpr int maxWavetableFrames = 256;
        static constexpr double secondsHeldAfterRelease = 5.0;
        static constexpr double secondsBeforeFree = 2.0;

        static UserLibrary& instance();

        /** The User folder: "User" inside the sample library's root, unless setFolder named another. */
        [[nodiscard]] juce::File folder() const;
        /** Points somewhere else (tests and tools). Only while nothing is playing. */
        void setFolder(const juce::File& otherFolder);

        /** Message thread. A handle for the file at this path inside the User folder (forward slashes), loading
            it in the background the first time; -1 for an empty name or when every handle is taken. */
        int acquire(const juce::String& name);
        /** Message thread. The slot no longer uses the handle. */
        void release(int handle);

        /** Audio thread: the handle's sound in memory, or null while it loads or when the file could not be read. */
        [[nodiscard]] const UserWavetable* wavetable(int handle) const noexcept;
        [[nodiscard]] const LoadedStyle* sample(int handle) const noexcept;
        [[nodiscard]] const UserGrainSource* grains(int handle) const noexcept;

        /** Message thread: waits for the handle's file; true when it was read. */
        bool waitUntilLoaded(int handle, int timeoutMilliseconds);
        /** Why the handle holds nothing, or empty. */
        [[nodiscard]] juce::String problem(int handle) const;

        /** The loader's steps, usable on their own. */
        struct Loaded
        {
            std::unique_ptr<LoadedStyle> sample;
            std::unique_ptr<UserWavetable> wavetable;
            std::unique_ptr<UserGrainSource> grains;
            juce::String problem;
        };
        /** Mono audio at a rate, sounding rootHz at its own speed, as the grain generator reads it. */
        [[nodiscard]] static std::unique_ptr<UserGrainSource> buildGrainSource(const float* mono, int length, double rate, double rootHz);
        [[nodiscard]] static Loaded load(const juce::File& file);
        /** A table from mono audio cut into frames of frameSize samples (each resampled to 2048), at most
            maxWavetableFrames; a file shorter than one frame is one cycle. Null for no audio. */
        [[nodiscard]] static std::unique_ptr<UserWavetable> buildWavetable(const float* mono, int length, int frameSize);
        /** The samples per frame a WAV file's "clm " chunk names, or 0 when it has none. */
        [[nodiscard]] static int clmFrameSize(const juce::File& file);

    private:
        UserLibrary();
        ~UserLibrary() override;
        void run() override;
        [[nodiscard]] bool valid(int handle) const noexcept { return handle >= 0 && handle < maxSounds; }

        struct Entry
        {
            // Under `lock`:
            juce::String name;
            int refs = 0;
            double releasedAt = 0.0;
            bool loadWanted = false;
            juce::String problem;
            std::unique_ptr<LoadedStyle> sampleOwner;
            std::unique_ptr<UserWavetable> wavetableOwner;
            std::unique_ptr<UserGrainSource> grainsOwner;
            // For the audio thread:
            std::atomic<int> state { 0 }; // 0 free, 1 loading, 2 loaded, 3 failed
            std::atomic<const LoadedStyle*> sample { nullptr };
            std::atomic<const UserWavetable*> wavetable { nullptr };
            std::atomic<const UserGrainSource*> grains { nullptr };
        };
        struct Dropped
        {
            std::unique_ptr<LoadedStyle> sample;
            std::unique_ptr<UserWavetable> wavetable;
            std::unique_ptr<UserGrainSource> grains;
            double at = 0.0;
        };
        void retireLocked(Entry& entry, double now); // the entry's sounds leave the audio thread's reach

        void dropLocked(Entry& entry, double now);

        std::array<Entry, maxSounds> entries;
        std::vector<Dropped> dropped;
        juce::CriticalSection lock;
        juce::File folderOverride;
    };
}
