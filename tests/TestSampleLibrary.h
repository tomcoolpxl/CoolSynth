#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "synth/v3/InstrumentCatalogue.h"
#include "synth/v3/SampleLibrary.h"

/*
    A stand-in for the sampled-instrument library, so no test depends on the real one (1.4 GB,
    not in the repository). Every style of the catalogue gets the same small set of recordings,
    written as 16-bit mono WAV files, a quarter of a second each:

        C3 (48)   a sine at C3 for keys 0 to 56, every velocity
        C4 (60)   a sine at C4 for keys 57 to 66: at 0.25 for velocities up to 63, at 0.5 above
        C5 (72)   a sine at C5 for keys 67 to 96 in two takes: the second is the first upside down

    Keys above 96 belong to no recording: the nearest one plays. Sustaining styles carry a loop
    over the middle of each recording. The manifest's gain is 1 and loudness does not follow
    velocity (veltrack 0), so levels in tests are the recordings' own.
*/
namespace coolsynth::testing
{
    inline constexpr int testSampleRate = 44100;
    inline constexpr int testSampleFrames = 11025;
    inline constexpr int testLoopStart = 4410, testLoopEnd = 8820, testLoopFade = 1102;
    inline constexpr const char* testManifestColumns =
        "# file | root | low key | high key | low velocity | high velocity | take | frames | rate | channels | loop start | loop end | loop fade | cents | gain dB\n";

    inline void writeSineWav(const juce::File& file, double hz, double amplitude)
    {
        std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(testSampleFrames) * 2u;
        auto put32 = [&](std::uint32_t v) { const char b[4] { static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24) }; out.write(b, 4); };
        auto put16 = [&](std::uint32_t v) { const char b[2] { static_cast<char>(v), static_cast<char>(v >> 8) }; out.write(b, 2); };
        out.write("RIFF", 4);
        put32(36u + dataBytes);
        out.write("WAVEfmt ", 8);
        put32(16u);
        put16(1u);
        put16(1u);
        put32(static_cast<std::uint32_t>(testSampleRate));
        put32(static_cast<std::uint32_t>(testSampleRate) * 2u);
        put16(2u);
        put16(16u);
        out.write("data", 4);
        put32(dataBytes);
        for (int i = 0; i < testSampleFrames; ++i)
        {
            const double v = amplitude * std::sin(2.0 * 3.14159265358979323846 * hz * i / testSampleRate);
            put16(static_cast<std::uint32_t>(static_cast<std::int16_t>(std::lround(v * 32767.0))) & 0xFFFFu);
        }
    }

    /** Writes one style's stand-in recordings and manifest into a folder. */
    inline void writeTestStyle(const juce::File& folder, bool sustains)
    {
        folder.createDirectory();
        struct Entry
        {
            const char* file;
            int root, lowKey, highKey, lowVelocity, highVelocity, take;
            double amplitude;
        };
        const Entry entries[] { { "000.wav", 48, 0, 56, 0, 127, 0, 0.5 },    { "001.wav", 60, 57, 66, 0, 63, 0, 0.25 },
                                { "002.wav", 60, 57, 66, 64, 127, 0, 0.5 },  { "003.wav", 72, 67, 96, 0, 127, 0, 0.5 },
                                { "004.wav", 72, 67, 96, 0, 127, 1, -0.5 } };
        juce::String manifest = "gain | 1.0\nveltrack | 0.0\nrelease | 0.5\n";
        manifest << testManifestColumns;
        for (const auto& e : entries)
        {
            writeSineWav(folder.getChildFile(e.file), 440.0 * std::exp2((e.root - 69) / 12.0), e.amplitude);
            manifest << e.file << " | " << e.root << " | " << e.lowKey << " | " << e.highKey << " | " << e.lowVelocity << " | " << e.highVelocity
                     << " | " << e.take << " | " << testSampleFrames << " | " << testSampleRate << " | 1 | " << (sustains ? testLoopStart : 0)
                     << " | " << (sustains ? testLoopEnd : 0) << " | " << (sustains ? testLoopFade : 0) << " | 0 | 0\n";
        }
        folder.getChildFile("manifest.txt").replaceWithText(manifest, false, false, "\n");
    }

    /** Builds the stand-in library once per process and points the sample library at it. Returns its folder. */
    inline juce::File installTestSampleLibrary()
    {
        static const juce::File root = []
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("CoolSynthTestLibrary-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
            const auto& catalogue = coolsynth::synth::v3::InstrumentCatalogue::instance();
            for (int i = 0; i < catalogue.count(); ++i)
                for (const auto& style : catalogue.at(i).styles)
                    writeTestStyle(folder.getChildFile(juce::String(style.folder)), style.sustains);
            coolsynth::synth::v3::SampleLibrary::instance().setRoot(folder);
            return folder;
        }();
        return root;
    }

    inline void removeTestSampleLibrary()
    {
        const auto folder = coolsynth::synth::v3::SampleLibrary::instance().root();
        if (folder.getFileName().startsWith("CoolSynthTestLibrary-"))
        {
            // Point it at a folder that is not there, so nothing is held open, then clear up.
            coolsynth::synth::v3::SampleLibrary::instance().setRoot(folder.getSiblingFile("CoolSynthNoLibrary"));
            folder.deleteRecursively();
        }
    }
}
