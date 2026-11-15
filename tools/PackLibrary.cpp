// Packs the sampled-instrument library (decision D163): every style folder's manifest and recordings become one
// style.cspack (its table, then each recording re-encoded as FLAC with 4,096-frame blocks, at its own depth), and
// library.index lists every style's exact size in memory. Every recording is decoded back and compared, number for
// number: the pack is lossless or it is not written. Recordings are encoded on all cores but two.
//
//   CoolSynthPackLibrary [library folder] [--threads N]
//
// The library folder defaults to the one CoolSynth uses (SampleLibrary::defaultRoot()).

#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "synth/v3/InstrumentCatalogue.h"
#include "synth/v3/SampleLibrary.h"
#include "synth/v3/SamplePack.h"

namespace
{
    namespace sv3 = coolsynth::synth::v3;
    namespace pack = coolsynth::synth::v3::samplePack;

    struct Job
    {
        juce::File file;
        sv3::PackedRecording* recording = nullptr;
        juce::MemoryBlock* stream = nullptr;
        juce::String problem;
    };

    /** Reads one recording, re-encodes it, decodes the result and compares: false (and why) when anything differs. */
    bool packOne(Job& job)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(job.file));
        if (reader == nullptr)
        {
            job.problem = "cannot be read";
            return false;
        }
        auto& r = *job.recording;
        r.channels = static_cast<std::uint8_t>(juce::jlimit(1, 2, static_cast<int>(reader->numChannels)));
        r.bits = static_cast<std::uint8_t>(reader->usesFloatingPointData || reader->bitsPerSample > 16 ? 24 : 16);
        r.rate = reader->sampleRate;

        // The recording as it will sit in memory: the reference every later decode is compared with.
        std::vector<std::byte> expected(pack::memoryBytes(r));
        pack::decodeReaderInto(reader.get(), r, expected.data());

        // Re-encoded from those very numbers (left-justified, as JUCE's writers take them).
        const int frames = r.frames;
        const int bytesPer = r.bits <= 16 ? 2 : 3;
        std::vector<int> channelData[2];
        for (int c = 0; c < r.channels; ++c)
            channelData[c].resize(static_cast<std::size_t>(std::max(1, frames)));
        const std::byte* start = expected.data() + static_cast<std::size_t>(pack::guardFrames) * static_cast<std::size_t>(r.channels * bytesPer);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < r.channels; ++c)
            {
                const std::byte* at = start + (static_cast<std::size_t>(i) * r.channels + static_cast<std::size_t>(c)) * static_cast<std::size_t>(bytesPer);
                std::uint32_t u = static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8);
                if (bytesPer == 3)
                    u |= static_cast<std::uint32_t>(at[2]) << 16;
                const int shift = 32 - (bytesPer * 8);
                channelData[c][static_cast<std::size_t>(i)] = static_cast<int>(u << shift); // left-justified, sign in the top bit
            }

        juce::MemoryBlock encoded;
        {
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream>(encoded, false);
            juce::FlacAudioFormat flac;
            auto writer = flac.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                        .withSampleRate(r.rate)
                                                        .withNumChannels(r.channels)
                                                        .withBitsPerSample(r.bits)
                                                        .withQualityOptionIndex(8));
            if (writer == nullptr)
            {
                job.problem = "the FLAC writer refused it";
                return false;
            }
            const int* pointers[3] { channelData[0].data(), r.channels > 1 ? channelData[1].data() : nullptr, nullptr };
            if (frames > 0 && ! writer->write(pointers, frames))
            {
                job.problem = "could not be encoded";
                return false;
            }
        }

        std::vector<std::byte> decoded(pack::memoryBytes(r));
        if (! pack::decodeInto(encoded.getData(), encoded.getSize(), r, decoded.data())
            || std::memcmp(decoded.data(), expected.data(), expected.size()) != 0)
        {
            job.problem = "the packed copy is not bit for bit the recording";
            return false;
        }
        *job.stream = std::move(encoded);
        return true;
    }
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = sv3::SampleLibrary::defaultRoot();
    int threads = std::max(1, juce::SystemStats::getNumCpus() - 2);
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg(argv[a]);
        if (arg == "--threads" && a + 1 < argc)
            threads = std::max(1, juce::String(argv[++a]).getIntValue());
        else
            root = juce::File::getCurrentWorkingDirectory().getChildFile(arg);
    }
    std::cout << "Packing " << root.getFullPathName() << " on " << threads << " threads\n";

    juce::ThreadPool pool(juce::ThreadPoolOptions {}.withNumberOfThreads(threads));
    std::vector<pack::IndexEntry> index;
    std::size_t memoryTotal = 0, packTotal = 0, sourceTotal = 0;
    int failures = 0;
    const auto& catalogue = sv3::InstrumentCatalogue::instance();
    const double started = juce::Time::getMillisecondCounterHiRes();
    for (int instrument = 0; instrument < catalogue.count(); ++instrument)
        for (const auto& styleInfo : catalogue.at(instrument).styles)
        {
            const auto folder = root.getChildFile(juce::String(styleInfo.folder));
            auto manifest = pack::readManifest(folder);
            if (! manifest.has_value())
            {
                std::cout << "  (not installed) " << styleInfo.folder << "\n";
                continue;
            }
            sv3::PackedStyle style;
            style.velocityTrack = manifest->velocityTrack;
            style.releaseSeconds = manifest->releaseSeconds;
            style.recordings = manifest->recordings;
            std::vector<juce::MemoryBlock> streams(style.recordings.size());
            std::vector<Job> jobs(style.recordings.size());
            std::atomic<int> failed { 0 };
            for (std::size_t i = 0; i < jobs.size(); ++i)
            {
                jobs[i] = { folder.getChildFile(manifest->files[static_cast<int>(i)]), &style.recordings[i], &streams[i], {} };
                sourceTotal += static_cast<std::size_t>(jobs[i].file.getSize());
                pool.addJob([&job = jobs[i], &failed] { if (! packOne(job)) ++failed; });
            }
            while (pool.getNumJobs() > 0)
                juce::Thread::sleep(5);
            if (failed.load() > 0)
            {
                for (const auto& job : jobs)
                    if (job.problem.isNotEmpty())
                        std::cout << "  FAILED " << job.file.getFullPathName() << ": " << job.problem << "\n";
                ++failures;
                continue;
            }
            const auto packFile = folder.getChildFile(pack::packFileName);
            if (! pack::writePack(packFile, style, streams))
            {
                std::cout << "  FAILED writing " << packFile.getFullPathName() << "\n";
                ++failures;
                continue;
            }
            std::size_t bytes = 0;
            for (const auto& r : style.recordings)
                bytes += pack::memoryBytes(r);
            const auto packBytes = static_cast<std::size_t>(packFile.getSize());
            memoryTotal += bytes;
            packTotal += packBytes;
            index.push_back({ juce::String(styleInfo.folder), bytes, static_cast<int>(style.recordings.size()), packBytes });
            std::cout << "  " << styleInfo.folder << ": " << style.recordings.size() << " recordings, "
                      << juce::String(static_cast<double>(bytes) / 1048576.0, 1) << " MB in memory, "
                      << juce::String(static_cast<double>(packBytes) / 1048576.0, 1) << " MB packed\n" << std::flush;
        }

    if (failures == 0 && ! pack::writeIndex(root, index))
    {
        std::cout << "Could not write " << pack::indexFileName << "\n";
        return 1;
    }
    std::cout << "\n" << index.size() << " styles packed in " << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1)
              << " s: " << juce::String(static_cast<double>(memoryTotal) / 1073741824.0, 2) << " GB in memory, "
              << juce::String(static_cast<double>(packTotal) / 1048576.0, 0) << " MB packed (the FLAC files: "
              << juce::String(static_cast<double>(sourceTotal) / 1048576.0, 0) << " MB). " << failures << " failed.\n";
    return failures == 0 ? 0 : 1;
}
