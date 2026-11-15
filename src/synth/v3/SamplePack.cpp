#include "SamplePack.h"

#include <algorithm>
#include <cstring>

#include <juce_audio_formats/juce_audio_formats.h>

namespace coolsynth::synth::v3::samplePack
{
    std::optional<ManifestStyle> readManifest(const juce::File& styleFolder)
    {
        const auto manifest = styleFolder.getChildFile("manifest.txt");
        if (! manifest.existsAsFile())
            return std::nullopt;

        ManifestStyle style;
        float gain = 1.0f; // one factor for the whole style: quietly recorded instruments are not quiet in the synth
        std::vector<float> rowGains;
        for (const auto& line : juce::StringArray::fromLines(manifest.loadFileAsString()))
        {
            const auto text = line.upToFirstOccurrenceOf("#", false, false).trim();
            if (text.isEmpty())
                continue;
            auto parts = juce::StringArray::fromTokens(text, "|", "");
            parts.trim();
            if (parts.size() == 2)
            {
                if (parts[0] == "gain")
                    gain *= juce::jlimit(1.0e-4f, 1.0e4f, parts[1].getFloatValue());
                else if (parts[0] == "trim") // decibels: makes an instrument's styles need the same preset level
                    gain *= juce::Decibels::decibelsToGain(juce::jlimit(-40.0f, 40.0f, parts[1].getFloatValue()));
                else if (parts[0] == "veltrack")
                    style.velocityTrack = juce::jlimit(0.0f, 1.0f, parts[1].getFloatValue());
                else if (parts[0] == "release")
                    style.releaseSeconds = juce::jlimit(0.0f, 60.0f, parts[1].getFloatValue());
                continue;
            }
            // file | root | low key | high key | low velocity | high velocity | take | frames | rate | channels
            //      | loop start | loop end | loop fade | cents | gain dB
            if (parts.size() < 15)
                continue;
            PackedRecording r;
            r.root = juce::jlimit(0, 127, parts[1].getIntValue());
            r.lowKey = juce::jlimit(0, 127, parts[2].getIntValue());
            r.highKey = juce::jlimit(r.lowKey, 127, parts[3].getIntValue());
            r.lowVelocity = juce::jlimit(0, 127, parts[4].getIntValue());
            r.highVelocity = juce::jlimit(r.lowVelocity, 127, parts[5].getIntValue());
            r.take = std::max(0, parts[6].getIntValue());
            r.frames = std::max(0, parts[7].getIntValue());
            r.rate = std::max(1000.0, parts[8].getDoubleValue());
            r.channels = static_cast<std::uint8_t>(juce::jlimit(1, 2, parts[9].getIntValue()));
            r.loopStart = std::max(0, parts[10].getIntValue());
            r.loopEnd = std::min(std::max(0, parts[11].getIntValue()), r.frames);
            r.loopFade = std::max(0, parts[12].getIntValue());
            if (r.loopEnd - r.loopStart < 64 || r.loopFade > r.loopStart)
                r.loopStart = r.loopEnd = r.loopFade = 0;
            r.tuneCents = juce::jlimit(-100.0f, 100.0f, parts[13].getFloatValue());
            rowGains.push_back(juce::Decibels::decibelsToGain(juce::jlimit(-80.0f, 80.0f, parts[14].getFloatValue())));
            style.recordings.push_back(r);
            style.files.add(parts[0]);
        }
        for (std::size_t i = 0; i < style.recordings.size(); ++i)
            style.recordings[i].level = gain * rowGains[i];
        return style;
    }

    std::optional<PackedStyle> readPackHeader(const juce::File& packFile)
    {
        juce::FileInputStream in(packFile);
        if (! in.openedOk())
            return std::nullopt;
        char magic[4] {};
        if (in.read(magic, 4) != 4 || std::memcmp(magic, "CSPK", 4) != 0)
            return std::nullopt;
        if (static_cast<std::uint32_t>(in.readInt()) != version)
            return std::nullopt;
        PackedStyle style;
        style.velocityTrack = in.readFloat();
        style.releaseSeconds = in.readFloat();
        const auto count = static_cast<std::uint32_t>(in.readInt());
        if (count > 100000)
            return std::nullopt;
        style.recordings.resize(count);
        const auto bytes = static_cast<int>(count * sizeof(PackedRecording));
        if (count > 0 && in.read(style.recordings.data(), bytes) != bytes)
            return std::nullopt;
        for (const auto& r : style.recordings)
            if (r.channels < 1 || r.channels > 2 || (r.bits != 16 && r.bits != 24) || r.frames < 0)
                return std::nullopt;
        return style;
    }

    bool writePack(const juce::File& packFile, const PackedStyle& style, const std::vector<juce::MemoryBlock>& streams)
    {
        if (streams.size() != style.recordings.size())
            return false;
        auto table = style.recordings;
        std::uint64_t offset = 4 + 4 + 4 + 4 + 4 + table.size() * sizeof(PackedRecording);
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            table[i].dataOffset = offset;
            table[i].dataBytes = static_cast<std::uint32_t>(streams[i].getSize());
            offset += streams[i].getSize();
        }
        const auto temporary = packFile.getSiblingFile(packFile.getFileName() + ".part");
        {
            juce::FileOutputStream out(temporary);
            if (! out.openedOk())
                return false;
            out.setPosition(0);
            out.truncate();
            out.write("CSPK", 4);
            out.writeInt(static_cast<int>(version));
            out.writeFloat(style.velocityTrack);
            out.writeFloat(style.releaseSeconds);
            out.writeInt(static_cast<int>(table.size()));
            out.write(table.data(), table.size() * sizeof(PackedRecording));
            for (const auto& stream : streams)
                out.write(stream.getData(), stream.getSize());
            out.flush();
            if (out.getStatus().failed())
                return false;
        }
        return temporary.moveFileTo(packFile);
    }

    std::vector<IndexEntry> readIndex(const juce::File& root)
    {
        std::vector<IndexEntry> entries;
        const auto file = root.getChildFile(indexFileName);
        if (! file.existsAsFile())
            return entries;
        for (const auto& line : juce::StringArray::fromLines(file.loadFileAsString()))
        {
            const auto text = line.upToFirstOccurrenceOf("#", false, false).trim();
            auto parts = juce::StringArray::fromTokens(text, "|", "");
            parts.trim();
            if (parts.size() < 4)
                continue;
            entries.push_back({ parts[0], static_cast<std::size_t>(parts[1].getLargeIntValue()), parts[2].getIntValue(),
                                static_cast<std::size_t>(parts[3].getLargeIntValue()) });
        }
        return entries;
    }

    bool writeIndex(const juce::File& root, const std::vector<IndexEntry>& entries)
    {
        juce::String text = "# CoolSynth sample library index (decision D163): folder | bytes in memory | recordings | pack bytes\n";
        for (const auto& e : entries)
            text << e.folder << " | " << static_cast<juce::int64>(e.memoryBytes) << " | " << e.recordings << " | "
                 << static_cast<juce::int64>(e.packBytes) << "\n";
        return root.getChildFile(indexFileName).replaceWithText(text, false, false, "\n");
    }

    void decodeReaderInto(juce::AudioFormatReader* reader, const PackedRecording& r, std::byte* out)
    {
        const int bytesPer = r.bits <= 16 ? 2 : 3;
        const int channels = r.channels;
        const std::size_t total = memoryBytes(r);
        std::memset(out, 0, total); // guards and anything the file does not have: silence
        if (reader == nullptr)
            return;
        const int available = static_cast<int>(std::min<juce::int64>(r.frames, reader->lengthInSamples));
        const bool floating = reader->usesFloatingPointData;
        constexpr int chunk = 16384;
        juce::HeapBlock<int> left(chunk), right(chunk);
        int* destinations[2] { left.get(), right.get() };
        std::byte* frameStart = out + static_cast<std::size_t>(guardFrames) * static_cast<std::size_t>(channels * bytesPer);
        for (int done = 0; done < available; done += chunk)
        {
            const int n = std::min(chunk, available - done);
            reader->read(destinations, channels, done, n, true);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < channels; ++c)
                {
                    const int raw = destinations[c][i];
                    std::int32_t v;
                    if (floating) // a float file: rounded to the depth it is stored at
                    {
                        float f;
                        std::memcpy(&f, &raw, sizeof(f));
                        const double full = r.bits <= 16 ? 32767.0 : 8388607.0;
                        v = static_cast<std::int32_t>(std::lround(juce::jlimit(-1.0, 1.0, static_cast<double>(f)) * full));
                    }
                    else // integers come left-justified in 32 bits: the top bits are the recording's own
                        v = raw >> (32 - (r.bits <= 16 ? 16 : 24));
                    std::byte* at = frameStart + (static_cast<std::size_t>(done + i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)) * static_cast<std::size_t>(bytesPer);
                    const auto u = static_cast<std::uint32_t>(v);
                    at[0] = static_cast<std::byte>(u & 0xFFu);
                    at[1] = static_cast<std::byte>((u >> 8) & 0xFFu);
                    if (bytesPer == 3)
                        at[2] = static_cast<std::byte>((u >> 16) & 0xFFu);
                }
        }
    }

    bool decodeInto(const void* flacStream, std::size_t streamBytes, const PackedRecording& r, std::byte* out)
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader(
            flac.createReaderFor(new juce::MemoryInputStream(flacStream, streamBytes, false), true));
        if (reader == nullptr)
        {
            std::memset(out, 0, memoryBytes(r));
            return false;
        }
        decodeReaderInto(reader.get(), r, out);
        return true;
    }
}
