#include "SampleLibrary.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace coolsynth::synth::v3
{
    const SampleZone* LoadedStyle::pick(float midiNote, float velocity, unsigned turn) const noexcept
    {
        if (zones.empty())
            return nullptr;

        const int key = static_cast<int>(std::lround(midiNote));
        const int struck = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 0, 127);

        // The zones whose keys hold the note. Outside all of them (above or below what the instrument was mapped
        // for) the nearest recorded note plays; of two equally near, the lower: a recording sounds better raised.
        bool held = false;
        for (const auto& z : zones)
            held = held || (key >= z.lowKey && key <= z.highKey);
        int nearestRoot = zones.front().root;
        if (! held)
        {
            float nearest = 1.0e9f;
            for (const auto& z : zones)
                if (const float distance = std::abs(static_cast<float>(z.root) - midiNote); distance < nearest)
                {
                    nearest = distance;
                    nearestRoot = z.root;
                }
        }

        // Of those, the zone whose velocities hold the note's, else the nearest in velocity; of several, the nearest note.
        const SampleZone* best = nullptr;
        int bestMiss = 0;
        float bestDistance = 0.0f;
        for (const auto& z : zones)
        {
            if (held ? (key < z.lowKey || key > z.highKey) : z.root != nearestRoot)
                continue;
            const int miss = struck < z.lowVelocity ? z.lowVelocity - struck : struck > z.highVelocity ? struck - z.highVelocity : 0;
            const float distance = std::abs(static_cast<float>(z.root) - midiNote);
            if (best == nullptr || miss < bestMiss || (miss == bestMiss && distance < bestDistance))
            {
                best = &z;
                bestMiss = miss;
                bestDistance = distance;
            }
        }
        if (best == nullptr)
            return nullptr;

        const SampleZone* first = nullptr;
        unsigned takes = 0;
        for (const auto& z : zones)
            if (z.root == best->root && z.lowKey == best->lowKey && z.highKey == best->highKey
                && z.lowVelocity == best->lowVelocity && z.highVelocity == best->highVelocity)
            {
                if (first == nullptr)
                    first = &z;
                ++takes;
            }
        return first + (turn % takes); // the takes of one zone lie together
    }

    std::unique_ptr<LoadedStyle> SampleLibrary::readStyle(const juce::File& folder)
    {
        const auto manifest = folder.getChildFile("manifest.txt");
        if (! manifest.existsAsFile())
            return nullptr;

        struct Row
        {
            juce::String file;
            SampleZone zone;
            float gain = 1.0f; // this recording's own level correction
        };
        std::vector<Row> rows;
        float gain = 1.0f; // one factor for the whole style: quietly recorded instruments are not quiet in the synth
        auto style = std::make_unique<LoadedStyle>();
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
                    style->velocityTrack = juce::jlimit(0.0f, 1.0f, parts[1].getFloatValue());
                else if (parts[0] == "release")
                    style->releaseSeconds = juce::jlimit(0.0f, 60.0f, parts[1].getFloatValue());
                continue;
            }
            // file | root | low key | high key | low velocity | high velocity | take | frames | rate | channels
            //      | loop start | loop end | loop fade | cents | gain dB
            if (parts.size() < 15)
                continue;
            Row row;
            row.file = parts[0];
            row.zone.root = juce::jlimit(0, 127, parts[1].getIntValue());
            row.zone.lowKey = juce::jlimit(0, 127, parts[2].getIntValue());
            row.zone.highKey = juce::jlimit(row.zone.lowKey, 127, parts[3].getIntValue());
            row.zone.lowVelocity = juce::jlimit(0, 127, parts[4].getIntValue());
            row.zone.highVelocity = juce::jlimit(row.zone.lowVelocity, 127, parts[5].getIntValue());
            row.zone.take = std::max(0, parts[6].getIntValue());
            row.zone.frames = std::max(0, parts[7].getIntValue());
            row.zone.rate = std::max(1000.0, parts[8].getDoubleValue());
            row.zone.loopStart = std::max(0, parts[10].getIntValue());
            row.zone.loopEnd = std::max(0, parts[11].getIntValue());
            row.zone.loopFade = std::max(0, parts[12].getIntValue());
            row.zone.tuneCents = juce::jlimit(-100.0f, 100.0f, parts[13].getFloatValue());
            row.gain = juce::Decibels::decibelsToGain(juce::jlimit(-80.0f, 80.0f, parts[14].getFloatValue()));
            rows.push_back(row);
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
        {
            return std::tie(a.zone.root, a.zone.lowKey, a.zone.highKey, a.zone.lowVelocity, a.zone.highVelocity, a.zone.take)
                 < std::tie(b.zone.root, b.zone.lowKey, b.zone.highKey, b.zone.lowVelocity, b.zone.highVelocity, b.zone.take);
        });

        constexpr int guard = LoadedStyle::guardFrames;
        std::size_t floats = 0;
        for (const auto& row : rows)
            floats += 2u * static_cast<std::size_t>(row.zone.frames + 2 * guard);
        style->storage.assign(floats, 0.0f);

        juce::FlacAudioFormat flac;
        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> scratch;
        std::size_t offset = 0;
        std::vector<std::size_t> offsets;
        for (auto& row : rows)
        {
            const auto file = folder.getChildFile(row.file);
            std::unique_ptr<juce::AudioFormatReader> reader;
            if (auto stream = file.createInputStream())
            {
                juce::AudioFormat& format = file.hasFileExtension("wav") ? static_cast<juce::AudioFormat&>(wav) : flac;
                reader.reset(format.createReaderFor(stream.release(), true));
            }
            // What the manifest promises is what gets a place; a short or missing file leaves silence there.
            const int frames = reader != nullptr ? std::min(row.zone.frames, static_cast<int>(reader->lengthInSamples)) : 0;
            if (frames > 0)
            {
                scratch.setSize(2, frames, false, false, true);
                reader->read(&scratch, 0, frames, 0, true, true);
                const float* left = scratch.getReadPointer(0);
                const float* right = scratch.getReadPointer(reader->numChannels > 1 ? 1 : 0);
                float* out = style->storage.data() + offset + 2u * static_cast<std::size_t>(guard);
                const float level = gain * row.gain;
                for (int i = 0; i < frames; ++i)
                {
                    out[2 * i] = left[i] * level;
                    out[2 * i + 1] = right[i] * level;
                }
                row.zone.rate = reader->sampleRate;
            }
            row.zone.loopEnd = std::min(row.zone.loopEnd, row.zone.frames);
            if (row.zone.loopEnd - row.zone.loopStart < 64 || row.zone.loopFade > row.zone.loopStart)
                row.zone.loopStart = row.zone.loopEnd = row.zone.loopFade = 0;
            offsets.push_back(offset + 2u * static_cast<std::size_t>(guard));
            offset += 2u * static_cast<std::size_t>(row.zone.frames + 2 * guard);
        }

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            rows[i].zone.data = style->storage.data() + offsets[i];
            style->zones.push_back(rows[i].zone);
        }
        return style;
    }

    juce::File SampleLibrary::defaultRoot()
    {
        if (const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_LIBRARY", {}); fromEnvironment.isNotEmpty())
            return juce::File(fromEnvironment);

        const auto pointer = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CoolSynth").getChildFile("library.path");
        if (pointer.existsAsFile())
            if (const auto written = pointer.loadFileAsString().trim(); written.isNotEmpty() && juce::File::isAbsolutePath(written))
                return juce::File(written);

        return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("CoolSynth").getChildFile("Library");
    }

    SampleLibrary& SampleLibrary::instance()
    {
        static SampleLibrary library;
        return library;
    }

    SampleLibrary::SampleLibrary() : juce::Thread("CoolSynth sample loader")
    {
        rootFolder = defaultRoot();
        startThread(juce::Thread::Priority::low);
    }

    SampleLibrary::~SampleLibrary()
    {
        stopThread(4000);
    }

    juce::File SampleLibrary::root() const
    {
        const juce::ScopedLock lock(rootLock);
        return rootFolder;
    }

    void SampleLibrary::setRoot(const juce::File& folder)
    {
        stopThread(4000);
        {
            const juce::ScopedLock lock(rootLock);
            rootFolder = folder;
        }
        dropAll();
        startThread(juce::Thread::Priority::low);
    }

    SampleLibrary::Slot* SampleLibrary::slotFor(int instrument, int style) noexcept
    {
        if (instrument < 0 || instrument >= maxInstruments || style < 0 || style >= maxInstrumentStyles)
            return nullptr;
        return &slots[static_cast<std::size_t>(instrument * maxInstrumentStyles + style)];
    }

    const SampleLibrary::Slot* SampleLibrary::slotFor(int instrument, int style) const noexcept
    {
        if (instrument < 0 || instrument >= maxInstruments || style < 0 || style >= maxInstrumentStyles)
            return nullptr;
        return &slots[static_cast<std::size_t>(instrument * maxInstrumentStyles + style)];
    }

    const LoadedStyle* SampleLibrary::request(int instrument, int style) noexcept
    {
        auto* slot = slotFor(instrument, style);
        if (slot == nullptr)
            return nullptr;
        slot->lastAsked.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        slot->wanted.store(true, std::memory_order_relaxed);
        return slot->loaded.load(std::memory_order_acquire);
    }

    const LoadedStyle* SampleLibrary::loadNow(int instrument, int style, int timeoutMilliseconds)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(std::max(0, timeoutMilliseconds));
        for (;;)
        {
            if (const auto* loaded = request(instrument, style))
                return loaded;
            if (missing(instrument, style) || juce::Time::getMillisecondCounter() > deadline)
                return nullptr;
            juce::Thread::sleep(2);
        }
    }

    bool SampleLibrary::installed(int instrument, int style) const
    {
        const auto& styles = InstrumentCatalogue::instance().at(instrument).styles;
        if (style < 0 || style >= static_cast<int>(styles.size()))
            return false;
        return root().getChildFile(styles[static_cast<std::size_t>(style)].folder).getChildFile("manifest.txt").existsAsFile();
    }

    bool SampleLibrary::missing(int instrument, int style) const noexcept
    {
        const auto* slot = slotFor(instrument, style);
        return slot == nullptr || slot->notThere.load(std::memory_order_relaxed);
    }

    void SampleLibrary::run()
    {
        const auto& catalogue = InstrumentCatalogue::instance();
        while (! threadShouldExit())
        {
            bool worked = false;
            for (int instrument = 0; instrument < std::min(maxInstruments, catalogue.count()) && ! threadShouldExit(); ++instrument)
            {
                const auto& styles = catalogue.at(instrument).styles;
                for (int style = 0; style < static_cast<int>(styles.size()) && ! threadShouldExit(); ++style)
                {
                    auto& slot = *slotFor(instrument, style);
                    if (! slot.wanted.load(std::memory_order_relaxed) || slot.owner != nullptr || slot.notThere.load(std::memory_order_relaxed))
                        continue;

                    auto loaded = readStyle(root().getChildFile(styles[static_cast<std::size_t>(style)].folder));
                    if (loaded == nullptr || loaded->zones.empty())
                    {
                        slot.notThere.store(true, std::memory_order_relaxed);
                        continue;
                    }
                    loadedBytes.fetch_add(loaded->bytes(), std::memory_order_relaxed);
                    slot.owner = std::move(loaded);
                    slot.loaded.store(slot.owner.get(), std::memory_order_release);
                    worked = true;
                }
            }
            dropIdle();
            if (! worked)
                wait(25);
        }
    }

    void SampleLibrary::dropIdle()
    {
        if (loadedBytes.load(std::memory_order_relaxed) <= memoryBudgetBytes)
            return;
        const auto now = juce::Time::getMillisecondCounter();
        for (auto& slot : slots)
        {
            if (slot.owner == nullptr || now - slot.lastAsked.load(std::memory_order_relaxed) < idleMillisecondsBeforeDrop)
                continue;
            // Nothing has asked for it in a minute and a half: no voice is reading it.
            slot.wanted.store(false, std::memory_order_relaxed);
            slot.loaded.store(nullptr, std::memory_order_release);
            loadedBytes.fetch_sub(slot.owner->bytes(), std::memory_order_relaxed);
            slot.owner.reset();
            if (loadedBytes.load(std::memory_order_relaxed) <= memoryBudgetBytes)
                return;
        }
    }

    void SampleLibrary::dropAll()
    {
        for (auto& slot : slots)
        {
            slot.loaded.store(nullptr, std::memory_order_release);
            slot.owner.reset();
            slot.wanted.store(false, std::memory_order_relaxed);
            slot.notThere.store(false, std::memory_order_relaxed);
        }
        loadedBytes.store(0, std::memory_order_relaxed);
    }
}
