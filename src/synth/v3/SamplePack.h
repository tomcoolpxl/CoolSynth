#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

namespace juce { class AudioFormatReader; }

/*
    The packed sample library (decision D163): each style pre-processed once into one file,
    `style.cspack` in its folder, and the whole library described by `library.index` at its root,
    so CoolSynth plans its sample memory without opening a recording and loading is reading.

    style.cspack, little-endian:
        "CSPK", version (uint32), velocity track (float), release seconds (float), recordings (uint32),
        one PackedRecording per recording, then the recordings' FLAC streams one after another.
    A recording's stream is its own FLAC file (16 or 24 bit, mono or stereo, 4,096-frame blocks),
    so each is decoded on its own, all of them in parallel. Its level corrections (the style's
    gain and trim, the recording's gain) are one factor, applied as it plays; its numbers stay as
    recorded (lossless).

    library.index, text, one line per style: folder | bytes in memory | recordings | pack bytes.

    A style folder with a manifest and no pack is still read (the manifest path, SampleLibrary.h).
*/
namespace coolsynth::synth::v3
{
    /** One manifest row, or one packed recording's facts. */
    struct PackedRecording
    {
        std::int32_t root = 60, lowKey = 0, highKey = 127, lowVelocity = 0, highVelocity = 127, take = 0;
        std::int32_t frames = 0;
        std::int32_t reserved0 = 0;
        double rate = 44100.0;
        std::int32_t loopStart = 0, loopEnd = 0, loopFade = 0;
        float tuneCents = 0.0f;
        float level = 1.0f;          // every level correction as one factor
        std::uint8_t channels = 2;   // 1 or 2
        std::uint8_t bits = 16;      // 16 or 24
        std::uint8_t reserved[2] {};
        std::uint64_t dataOffset = 0; // the FLAC stream, from the start of the file
        std::uint32_t dataBytes = 0;
        std::uint32_t reserved2 = 0;
    };
    static_assert(sizeof(PackedRecording) == 80, "the packed layout is fixed");

    struct PackedStyle
    {
        float velocityTrack = 1.0f;
        float releaseSeconds = 0.5f;
        std::vector<PackedRecording> recordings;
    };

    /** A style's manifest (what the library builder writes): its recordings in manifest order, the file of each. */
    struct ManifestStyle
    {
        float velocityTrack = 1.0f;
        float releaseSeconds = 0.5f;
        std::vector<PackedRecording> recordings; // dataOffset and dataBytes unused; level holds every correction
        juce::StringArray files;
    };

    namespace samplePack
    {
        inline constexpr char packFileName[] = "style.cspack";
        inline constexpr char indexFileName[] = "library.index";
        inline constexpr std::uint32_t version = 1;
        inline constexpr int guardFrames = 16; // silent frames before and after each recording in memory

        /** The bytes a recording takes in the sample memory: its frames and the guard frames, at its own depth. */
        [[nodiscard]] inline std::size_t memoryBytes(const PackedRecording& r) noexcept
        {
            return static_cast<std::size_t>(std::max(0, r.frames) + 2 * guardFrames) * r.channels * (r.bits <= 16 ? 2u : 3u);
        }

        /** Reads a manifest; nothing if there is none. */
        [[nodiscard]] std::optional<ManifestStyle> readManifest(const juce::File& styleFolder);

        /** Reads a pack's header and table; nothing if it is missing or not a pack of this version. */
        [[nodiscard]] std::optional<PackedStyle> readPackHeader(const juce::File& packFile);

        /** Writes a pack: the table, then each recording's FLAC stream in `streams` (same order). */
        bool writePack(const juce::File& packFile, const PackedStyle& style, const std::vector<juce::MemoryBlock>& streams);

        /** One line per style folder (relative to the root) and its bytes in memory, from library.index. */
        struct IndexEntry
        {
            juce::String folder;
            std::size_t memoryBytes = 0;
            int recordings = 0;
            std::size_t packBytes = 0;
        };
        [[nodiscard]] std::vector<IndexEntry> readIndex(const juce::File& root);
        bool writeIndex(const juce::File& root, const std::vector<IndexEntry>& entries);

        /**
            Decodes one recording's FLAC stream into `out` at its own depth, interleaved, its guard frames silent:
            exactly memoryBytes(recording) bytes. A stream shorter than the recording's frames leaves silence. False
            if the stream cannot be read. Any thread but the audio thread.
        */
        bool decodeInto(const void* flacStream, std::size_t streamBytes, const PackedRecording& recording, std::byte* out);
        /** The same from any reader (a manifest's FLAC or WAV file); `recording.bits` and `channels` say how it is stored. */
        void decodeReaderInto(juce::AudioFormatReader* reader, const PackedRecording& recording, std::byte* out);
    }
}
