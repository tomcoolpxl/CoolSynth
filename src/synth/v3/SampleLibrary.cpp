#include "SampleLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "diagnostics/FlightRecorder.h"

namespace coolsynth::synth::v3
{
    std::atomic<std::uint64_t> SampleLibrary::readEpoch { 0 };
    std::array<std::atomic<int>, 2> SampleLibrary::readersIn {};

    namespace
    {
        constexpr std::size_t slackPerStyle = SampleMemory::pageBytes; // a style's last pages are partly empty
        constexpr std::size_t extentBytes = std::size_t { 64 } << 20;   // a style is placed in runs of up to 64 MB

        [[nodiscard]] SampleFormat formatOf(int bits) noexcept { return bits <= 16 ? SampleFormat::int16 : SampleFormat::int24; }
        [[nodiscard]] float scaleOf(int bits, float level) noexcept { return level / (bits <= 16 ? 32768.0f : 8388608.0f); }

        /** A zone for a recording placed at `memory` (its first guard frame). */
        SampleZone zoneFor(const PackedRecording& r, const std::byte* memory) noexcept
        {
            SampleZone z;
            z.root = r.root;
            z.lowKey = r.lowKey;
            z.highKey = r.highKey;
            z.lowVelocity = r.lowVelocity;
            z.highVelocity = r.highVelocity;
            z.take = r.take;
            z.frames = r.frames;
            z.rate = r.rate;
            z.loopStart = r.loopStart;
            z.loopEnd = r.loopEnd;
            z.loopFade = r.loopFade;
            z.tuneCents = r.tuneCents;
            z.format = formatOf(r.bits);
            z.channels = r.channels;
            z.scale = scaleOf(r.bits, r.level);
            z.data = memory + static_cast<std::size_t>(samplePack::guardFrames) * static_cast<std::size_t>(r.channels * (r.bits <= 16 ? 2 : 3));
            return z;
        }

        /** What a style holds before it is read: its table, from its pack or, failing that, its manifest and the
            headers of its files (their depth and channels). */
        struct StylePlan
        {
            PackedStyle style;
            juce::File pack;              // when packed
            juce::StringArray files;      // otherwise, the recordings' files
        };

        std::optional<StylePlan> planStyle(const juce::File& folder)
        {
            StylePlan plan;
            plan.pack = folder.getChildFile(samplePack::packFileName);
            if (auto packed = samplePack::readPackHeader(plan.pack))
            {
                plan.style = std::move(*packed);
                return plan;
            }
            plan.pack = juce::File();
            auto manifest = samplePack::readManifest(folder);
            if (! manifest.has_value())
                return std::nullopt;
            plan.style.velocityTrack = manifest->velocityTrack;
            plan.style.releaseSeconds = manifest->releaseSeconds;
            plan.style.recordings = manifest->recordings;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            for (int i = 0; i < manifest->files.size(); ++i)
            {
                const auto file = folder.getChildFile(manifest->files[i]);
                plan.files.add(file.getFullPathName());
                auto& r = plan.style.recordings[static_cast<std::size_t>(i)];
                if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor(file) })
                {
                    r.channels = static_cast<std::uint8_t>(juce::jlimit(1, 2, static_cast<int>(reader->numChannels)));
                    r.bits = static_cast<std::uint8_t>(reader->usesFloatingPointData || reader->bitsPerSample > 16 ? 24 : 16);
                    r.rate = reader->sampleRate;
                }
            }
            return plan;
        }

        /** Reads one recording into the memory placed for it, from the pack or from its own file. */
        bool readRecording(const StylePlan& plan, std::size_t index, std::byte* out)
        {
            const auto& r = plan.style.recordings[index];
            if (plan.pack != juce::File())
            {
                juce::FileInputStream in(plan.pack);
                if (! in.openedOk() || ! in.setPosition(static_cast<juce::int64>(r.dataOffset)))
                    return false;
                juce::HeapBlock<char> stream(r.dataBytes);
                if (in.read(stream.get(), static_cast<int>(r.dataBytes)) != static_cast<int>(r.dataBytes))
                    return false;
                return samplePack::decodeInto(stream.get(), r.dataBytes, r, out);
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(plan.files[static_cast<int>(index)])));
            samplePack::decodeReaderInto(reader.get(), r, out); // a missing file leaves silence where it was promised
            return true;
        }
    }

    // ---- Zones and styles -------------------------------------------------------------------------------------

    float SampleZone::at(int frame, int channel) const noexcept
    {
        if (data == nullptr)
            return 0.0f;
        const int c = channels > 1 ? std::clamp(channel, 0, 1) : 0;
        const auto index = static_cast<std::ptrdiff_t>(frame) * channels + c;
        switch (format)
        {
            case SampleFormat::int16: return static_cast<float>(static_cast<const std::int16_t*>(data)[index]) * scale;
            case SampleFormat::int24:
            {
                const auto* p = static_cast<const std::uint8_t*>(data) + index * 3;
                const auto v = static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) | (static_cast<std::uint32_t>(p[1]) << 16)
                                                         | (static_cast<std::uint32_t>(p[2]) << 24)) >> 8;
                return static_cast<float>(v) * scale;
            }
            case SampleFormat::float32:
            default: return static_cast<const float*>(data)[index] * scale;
        }
    }

    float* LoadedStyle::addRecording(int frames)
    {
        const auto floats = 2u * static_cast<std::size_t>(std::max(0, frames) + 2 * guardFrames);
        auto block = std::make_unique<std::byte[]>(floats * sizeof(float)); // value-initialised: silence
        auto* start = reinterpret_cast<float*>(block.get()) + 2 * guardFrames;
        ownBlocks.push_back(std::move(block));
        storedBytes += floats * sizeof(float);
        return start;
    }

    void LoadedStyle::sortZones()
    {
        std::stable_sort(zones.begin(), zones.end(), [](const SampleZone& a, const SampleZone& b)
        {
            return std::tie(a.root, a.lowKey, a.highKey, a.lowVelocity, a.highVelocity, a.take)
                 < std::tie(b.root, b.lowKey, b.highKey, b.lowVelocity, b.highVelocity, b.take);
        });
    }

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
        auto plan = planStyle(folder);
        if (! plan.has_value())
            return nullptr;
        auto style = std::make_unique<LoadedStyle>();
        style->velocityTrack = plan->style.velocityTrack;
        style->releaseSeconds = plan->style.releaseSeconds;
        for (std::size_t i = 0; i < plan->style.recordings.size(); ++i)
        {
            const auto& r = plan->style.recordings[i];
            const auto bytes = samplePack::memoryBytes(r);
            auto block = std::make_unique<std::byte[]>(bytes);
            readRecording(*plan, i, block.get());
            style->zones.push_back(zoneFor(r, block.get()));
            style->storedBytes += bytes;
            style->ownBlocks.push_back(std::move(block));
        }
        style->sortZones();
        return style;
    }

    // ---- The library ------------------------------------------------------------------------------------------

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
        decoders.reset();
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
        memory.release();
        planned = false;
        startThread(juce::Thread::Priority::low);
    }

    void SampleLibrary::prepareMemory() noexcept
    {
        memoryWanted.store(true, std::memory_order_relaxed);
        notify();
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
        if (! slot->everAsked.exchange(true, std::memory_order_relaxed))
            diag::record(diag::Event::message, instrument, style, 0.0f, 0.0f, "instrument style asked for (instrument, style)");
        return slot->loaded.load(std::memory_order_acquire);
    }

    const LoadedStyle* SampleLibrary::loadNow(int instrument, int style, int timeoutMilliseconds)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(std::max(0, timeoutMilliseconds));
        for (;;)
        {
            if (const auto* loaded = request(instrument, style))
                return loaded;
            notify();
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
        const auto folder = root().getChildFile(styles[static_cast<std::size_t>(style)].folder);
        return folder.getChildFile("manifest.txt").existsAsFile() || folder.getChildFile(samplePack::packFileName).existsAsFile();
    }

    bool SampleLibrary::missing(int instrument, int style) const noexcept
    {
        const auto* slot = slotFor(instrument, style);
        return slot == nullptr || slot->notThere.load(std::memory_order_relaxed);
    }

    // ---- Readers and the epoch ----------------------------------------------------------------------------------

    SampleLibrary::ReadScope::ReadScope() noexcept
    {
        // Enter the current epoch; if it moved on meanwhile, enter the new one (the loader waits for the old to empty).
        for (;;)
        {
            epoch = readEpoch.load(std::memory_order_seq_cst);
            readersIn[static_cast<std::size_t>(epoch & 1u)].fetch_add(1, std::memory_order_seq_cst);
            if (readEpoch.load(std::memory_order_seq_cst) == epoch)
                break;
            readersIn[static_cast<std::size_t>(epoch & 1u)].fetch_sub(1, std::memory_order_seq_cst);
        }
    }

    SampleLibrary::ReadScope::~ReadScope()
    {
        readersIn[static_cast<std::size_t>(epoch & 1u)].fetch_sub(1, std::memory_order_seq_cst);
    }

    bool SampleLibrary::waitForReaders()
    {
        const auto old = readEpoch.fetch_add(1, std::memory_order_seq_cst);
        const auto deadline = juce::Time::getMillisecondCounter() + 1000u;
        while (readersIn[static_cast<std::size_t>(old & 1u)].load(std::memory_order_seq_cst) != 0)
        {
            if (juce::Time::getMillisecondCounter() > deadline)
                return false; // a block that long is broken anyway: keep the memory
            juce::Thread::sleep(1);
        }
        return true;
    }

    // ---- The loader thread ------------------------------------------------------------------------------------

    void SampleLibrary::planMemory()
    {
        const auto& catalogue = InstrumentCatalogue::instance();
        const auto libraryRoot = root();
        std::map<juce::String, std::size_t> indexed;
        for (const auto& entry : samplePack::readIndex(libraryRoot))
            indexed[entry.folder] = entry.memoryBytes;

        std::size_t library = 0, largest = 0;
        juce::String largestName;
        int styles = 0;
        for (int i = 0; i < std::min(maxInstruments, catalogue.count()); ++i)
            for (int s = 0; s < static_cast<int>(catalogue.at(i).styles.size()); ++s)
            {
                const auto& info = catalogue.at(i).styles[static_cast<std::size_t>(s)];
                const juce::String folder(info.folder);
                std::size_t bytes = 0;
                if (const auto found = indexed.find(folder); found != indexed.end())
                    bytes = found->second;
                else if (auto manifest = samplePack::readManifest(libraryRoot.getChildFile(folder)))
                    for (auto r : manifest->recordings)
                    {
                        r.bits = 24; // not packed: counted at the most a recording can take
                        bytes += samplePack::memoryBytes(r);
                    }
                slotFor(i, s)->plannedBytes = bytes;
                if (bytes == 0)
                    continue;
                ++styles;
                library += bytes;
                if (bytes > largest)
                {
                    largest = bytes;
                    largestName = juce::String(catalogue.at(i).name) + " (" + juce::String(info.name) + ")";
                }
            }

        // Half the free memory, at most the whole library (D155, D162); COOLSYNTH_SAMPLE_MEMORY_MB lowers it.
        const std::size_t wanted = library + static_cast<std::size_t>(styles) * slackPerStyle;
        const std::size_t freeBytes = SampleMemory::freePhysicalBytes();
        std::size_t capacity = freeBytes > 0 ? std::min(wanted, freeBytes / 2) : wanted;
        if (const auto cap = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_SAMPLE_MEMORY_MB", {}); cap.isNotEmpty())
            capacity = std::min(capacity, static_cast<std::size_t>(std::max<juce::int64>(1, cap.getLargeIntValue())) << 20);
        capacity = std::max(capacity, SampleMemory::pageBytes);
        const auto closing = [this] { return threadShouldExit(); };
        if (! memory.reserve(capacity, closing) && ! threadShouldExit())
            memory.reserve(capacity / 2, closing);
        if (threadShouldExit())
            return; // the program is closing: no decoders started

        const int threads = std::max(1, juce::SystemStats::getNumCpus() - 2);
        decoders = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions {}
                                                          .withThreadName("CoolSynth sample decoder")
                                                          .withNumberOfThreads(threads)
                                                          .withDesiredThreadPriority(juce::Thread::Priority::low));
        planned = true;
        {
            const juce::ScopedLock lock(reportLock);
            report.planned = true;
            report.capacity = memory.capacityBytes();
            report.library = library;
            report.largest = largest;
            report.largestName = largestName;
            report.locked = memory.lockedInRam();
            report.tooSmall = report.capacity < std::min(library, largest + (std::size_t { 256 } << 20));
            report.loadThreads = threads;
        }
        diag::record(diag::Event::message, static_cast<int>(memory.capacityBytes() >> 20), static_cast<int>(library >> 20),
                     static_cast<float>(threads), 0.0f, "sample memory planned (MB taken, MB library)");
        updateReport();
    }

    std::optional<SampleMemory::Run> SampleLibrary::takeMaking(std::size_t pages, const Slot* loading)
    {
        for (;;)
        {
            if (auto run = memory.take(pages))
                return run;
            if (! dropOldest(loading))
                return std::nullopt;
        }
    }

    bool SampleLibrary::dropOldest(const Slot* loading)
    {
        // The style asked for longest ago that no sound still names (none asked in the last two seconds) and no note
        // plays: its pages are reused, never given back to the system (D155, D162).
        const auto now = juce::Time::getMillisecondCounter();
        Slot* oldest = nullptr;
        for (auto& slot : slots)
        {
            if (&slot == loading || slot.owner == nullptr || slot.owner->users.load(std::memory_order_acquire) != 0)
                continue;
            const auto asked = slot.lastAsked.load(std::memory_order_relaxed);
            if (now - asked < wantedMilliseconds)
                continue;
            if (oldest == nullptr || now - asked > now - oldest->lastAsked.load(std::memory_order_relaxed))
                oldest = &slot;
        }
        if (oldest == nullptr)
            return false;

        const auto* owner = oldest->owner.get();
        oldest->loaded.store(nullptr, std::memory_order_seq_cst);
        // Every block that might have picked it up has ended; then no note may have started on it meanwhile.
        if (! waitForReaders() || owner->users.load(std::memory_order_acquire) != 0)
        {
            oldest->loaded.store(owner, std::memory_order_release);
            return false;
        }
        for (const auto& run : owner->runs)
            memory.give(run);
        loadedBytes.fetch_sub(owner->bytes(), std::memory_order_relaxed);
        const auto index = static_cast<int>(oldest - slots.data());
        diag::record(diag::Event::styleDropped, index / maxInstrumentStyles, index % maxInstrumentStyles,
                     static_cast<float>(owner->bytes()) / 1048576.0f);
        oldest->owner.reset();
        updateReport();
        return true;
    }

    void SampleLibrary::load(int instrument, int styleIndex, Slot& slot)
    {
        const auto& info = InstrumentCatalogue::instance().at(instrument).styles[static_cast<std::size_t>(styleIndex)];
        const double started = juce::Time::getMillisecondCounterHiRes();
        auto plan = planStyle(root().getChildFile(info.folder));
        if (! plan.has_value() || plan->style.recordings.empty())
        {
            slot.notThere.store(true, std::memory_order_relaxed);
            diag::record(diag::Event::styleMissing, instrument, styleIndex);
            return;
        }

        std::size_t total = 0;
        for (const auto& r : plan->style.recordings)
            total += samplePack::memoryBytes(r);
        const auto name = juce::String(InstrumentCatalogue::instance().at(instrument).name) + " (" + juce::String(info.name) + ")";
        if (total > memory.capacityBytes())
        {
            // It can never fit: said, not left silent (D162).
            slot.notThere.store(true, std::memory_order_relaxed);
            {
                const juce::ScopedLock lock(reportLock);
                report.problem = name + " needs " + juce::String(static_cast<double>(total) / 1073741824.0, 2) + " GB; the sample memory holds "
                                 + juce::String(static_cast<double>(memory.capacityBytes()) / 1073741824.0, 2) + " GB";
            }
            diag::record(diag::Event::message, instrument, styleIndex, static_cast<float>(total >> 20), 0.0f, "instrument too big for the sample memory");
            return;
        }

        // Its place first: runs of pages, each recording whole inside one (D155).
        auto style = std::make_unique<LoadedStyle>();
        style->velocityTrack = plan->style.velocityTrack;
        style->releaseSeconds = plan->style.releaseSeconds;
        std::vector<std::byte*> places(plan->style.recordings.size(), nullptr);
        std::byte* extent = nullptr;
        std::size_t extentLeft = 0, styleLeft = total;
        bool placed = true;
        for (std::size_t i = 0; i < places.size() && placed; ++i)
        {
            const auto bytes = (samplePack::memoryBytes(plan->style.recordings[i]) + 15u) & ~std::size_t { 15 };
            if (bytes > extentLeft)
            {
                const auto want = std::max(bytes, std::min(styleLeft, extentBytes));
                auto run = takeMaking((want + SampleMemory::pageBytes - 1) / SampleMemory::pageBytes, &slot);
                if (! run.has_value())
                    run = takeMaking((bytes + SampleMemory::pageBytes - 1) / SampleMemory::pageBytes, &slot);
                if (! run.has_value())
                {
                    placed = false;
                    break;
                }
                style->runs.push_back(*run);
                extent = memory.address(*run);
                extentLeft = static_cast<std::size_t>(run->count) * SampleMemory::pageBytes;
            }
            places[i] = extent;
            extent += bytes;
            extentLeft -= bytes;
            styleLeft -= std::min(styleLeft, bytes);
            style->storedBytes += bytes;
        }
        if (! placed)
        {
            // No room while the sounds loaded need what is there: tried again later, and said (D162).
            for (const auto& run : style->runs)
                memory.give(run);
            slot.waitedForRoomAt = juce::Time::getMillisecondCounter();
            {
                const juce::ScopedLock lock(reportLock);
                report.problem = name + " does not fit next to the instruments the loaded sounds use";
            }
            diag::record(diag::Event::message, instrument, styleIndex, static_cast<float>(total >> 20), 0.0f, "no room for the instrument yet");
            return;
        }

        // Then every recording at once, on all cores but two.
        std::atomic<int> remaining { static_cast<int>(places.size()) };
        juce::WaitableEvent finished;
        const auto* readPlan = &*plan;
        for (std::size_t i = 0; i < places.size(); ++i)
            decoders->addJob([readPlan, i, out = places[i], &remaining, &finished]
            {
                readRecording(*readPlan, i, out);
                if (remaining.fetch_sub(1) == 1)
                    finished.signal();
            });
        while (! finished.wait(100))
            if (threadShouldExit())
            {
                finished.wait(-1); // the jobs write into the memory: they finish before it goes
                break;
            }

        for (std::size_t i = 0; i < places.size(); ++i)
            style->zones.push_back(zoneFor(plan->style.recordings[i], places[i]));
        style->sortZones();
        diag::record(diag::Event::styleLoaded, instrument, styleIndex, static_cast<float>(style->bytes()) / 1048576.0f,
                     static_cast<float>(juce::Time::getMillisecondCounterHiRes() - started));
        loadedBytes.fetch_add(style->bytes(), std::memory_order_relaxed);
        slot.owner = std::move(style);
        slot.loaded.store(slot.owner.get(), std::memory_order_release);
        {
            const juce::ScopedLock lock(reportLock);
            if (report.problem.startsWith(name))
                report.problem = {};
        }
        updateReport();
    }

    void SampleLibrary::run()
    {
        const auto& catalogue = InstrumentCatalogue::instance();
        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();
            // The most recently asked style that is wanted and not yet in memory.
            Slot* next = nullptr;
            int nextInstrument = 0, nextStyle = 0;
            for (int instrument = 0; instrument < std::min(maxInstruments, catalogue.count()); ++instrument)
                for (int style = 0; style < static_cast<int>(catalogue.at(instrument).styles.size()); ++style)
                {
                    auto& slot = *slotFor(instrument, style);
                    if (slot.owner != nullptr || slot.notThere.load(std::memory_order_relaxed) || ! slot.everAsked.load(std::memory_order_relaxed))
                        continue;
                    const auto asked = slot.lastAsked.load(std::memory_order_relaxed);
                    if (now - asked >= wantedMilliseconds)
                        continue;
                    if (slot.waitedForRoomAt != 0 && now - slot.waitedForRoomAt < 500u)
                        continue; // no room a moment ago: not again at once
                    if (next == nullptr || asked - next->lastAsked.load(std::memory_order_relaxed) < 0x80000000u)
                    {
                        next = &slot;
                        nextInstrument = instrument;
                        nextStyle = style;
                    }
                }

            if ((next != nullptr || memoryWanted.load(std::memory_order_relaxed)) && ! planned)
                planMemory();
            if (threadShouldExit())
                break;
            if (next != nullptr && planned)
                load(nextInstrument, nextStyle, *next);
            else
                wait(20);
        }
    }

    void SampleLibrary::dropAll()
    {
        for (auto& slot : slots)
        {
            slot.loaded.store(nullptr, std::memory_order_release);
            if (slot.owner != nullptr)
                for (const auto& run : slot.owner->runs)
                    memory.give(run);
            slot.owner.reset();
            slot.everAsked.store(false, std::memory_order_relaxed);
            slot.notThere.store(false, std::memory_order_relaxed);
            slot.waitedForRoomAt = 0;
        }
        loadedBytes.store(0, std::memory_order_relaxed);
        const juce::ScopedLock lock(reportLock);
        report = {};
    }

    void SampleLibrary::updateReport()
    {
        int count = 0;
        for (const auto& slot : slots)
            count += slot.owner != nullptr ? 1 : 0;
        const juce::ScopedLock lock(reportLock);
        report.used = memory.usedBytes();
        report.stylesInMemory = count;
    }

    SampleLibrary::MemoryReport SampleLibrary::memoryReport() const
    {
        const juce::ScopedLock lock(reportLock);
        return report;
    }

    juce::String SampleLibrary::memoryWarning() const
    {
        const auto r = memoryReport();
        if (r.problem.isNotEmpty())
            return r.problem;
        if (r.planned && r.tooSmall)
            return "Little memory for instruments: " + juce::String(static_cast<double>(r.capacity) / 1073741824.0, 1) + " GB ("
                   + r.largestName + " alone needs " + juce::String(static_cast<double>(r.largest) / 1073741824.0, 1)
                   + " GB). Close other programs and restart CoolSynth.";
        return {};
    }
}
