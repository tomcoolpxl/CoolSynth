#include "UserLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace coolsynth::synth::v3
{
    namespace
    {
        constexpr double maxSampleSeconds = 300.0; // five minutes: about 115 MB of stereo at 48 kHz
        constexpr double kPiDouble = 3.14159265358979323846;

        double nowSeconds() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

        /** Periodic cubic read of one cycle of `size` samples at a phase 0..1. */
        float cyclic(const float* cycle, int size, double phase) noexcept
        {
            const double at = phase * size;
            const int i = static_cast<int>(std::floor(at));
            const float t = static_cast<float>(at - i);
            auto x = [&](int k) { return cycle[((i + k) % size + size) % size]; };
            const float xm1 = x(-1), x0 = x(0), x1 = x(1), x2 = x(2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }
    }

    UserLibrary& UserLibrary::instance()
    {
        static UserLibrary library;
        return library;
    }

    UserLibrary::UserLibrary() : juce::Thread("CoolSynth user sound loader")
    {
        startThread(juce::Thread::Priority::low);
    }

    UserLibrary::~UserLibrary()
    {
        stopThread(4000);
    }

    juce::File UserLibrary::folder() const
    {
        const juce::ScopedLock scoped(lock);
        if (folderOverride != juce::File())
            return folderOverride;
        return SampleLibrary::instance().root().getChildFile("User");
    }

    void UserLibrary::setFolder(const juce::File& otherFolder)
    {
        const juce::ScopedLock scoped(lock);
        folderOverride = otherFolder;
        // What is held is read again from the new folder.
        for (auto& entry : entries)
            if (entry.state.load() != 0 && entry.refs > 0)
            {
                entry.loadWanted = true;
                entry.state.store(1);
            }
        notify();
    }

    int UserLibrary::acquire(const juce::String& rawName)
    {
        const auto name = rawName.trim().replaceCharacter('\\', '/');
        if (name.isEmpty())
            return -1;

        const juce::ScopedLock scoped(lock);
        for (int i = 0; i < maxSounds; ++i)
        {
            auto& entry = entries[static_cast<std::size_t>(i)];
            if (entry.state.load() != 0 && entry.name == name)
            {
                ++entry.refs;
                if (entry.state.load() == 3) // it failed before: the file may be there now
                {
                    entry.loadWanted = true;
                    entry.state.store(1);
                    notify();
                }
                return i;
            }
        }

        int chosen = -1;
        for (int i = 0; i < maxSounds && chosen < 0; ++i)
            if (entries[static_cast<std::size_t>(i)].state.load() == 0)
                chosen = i;
        for (int i = 0; i < maxSounds && chosen < 0; ++i)
            if (entries[static_cast<std::size_t>(i)].refs == 0)
            {
                dropLocked(entries[static_cast<std::size_t>(i)], nowSeconds()); // a sound no slot holds makes room
                chosen = i;
            }
        if (chosen < 0)
            return -1;

        auto& entry = entries[static_cast<std::size_t>(chosen)];
        entry.name = name;
        entry.refs = 1;
        entry.problem.clear();
        entry.loadWanted = true;
        entry.state.store(1);
        notify();
        return chosen;
    }

    void UserLibrary::release(int handle)
    {
        if (! valid(handle))
            return;
        const juce::ScopedLock scoped(lock);
        auto& entry = entries[static_cast<std::size_t>(handle)];
        if (entry.refs > 0 && --entry.refs == 0)
            entry.releasedAt = nowSeconds();
    }

    const UserWavetable* UserLibrary::wavetable(int handle) const noexcept
    {
        return valid(handle) ? entries[static_cast<std::size_t>(handle)].wavetable.load(std::memory_order_acquire) : nullptr;
    }

    const LoadedStyle* UserLibrary::sample(int handle) const noexcept
    {
        return valid(handle) ? entries[static_cast<std::size_t>(handle)].sample.load(std::memory_order_acquire) : nullptr;
    }

    const UserGrainSource* UserLibrary::grains(int handle) const noexcept
    {
        return valid(handle) ? entries[static_cast<std::size_t>(handle)].grains.load(std::memory_order_acquire) : nullptr;
    }

    bool UserLibrary::waitUntilLoaded(int handle, int timeoutMilliseconds)
    {
        if (! valid(handle))
            return false;
        const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(std::max(0, timeoutMilliseconds));
        for (;;)
        {
            const int state = entries[static_cast<std::size_t>(handle)].state.load();
            if (state == 2)
                return true;
            if (state == 3 || state == 0 || juce::Time::getMillisecondCounter() >= until)
                return false;
            juce::Thread::sleep(2);
        }
    }

    juce::String UserLibrary::problem(int handle) const
    {
        if (! valid(handle))
            return "no such sound";
        const juce::ScopedLock scoped(lock);
        const auto& entry = entries[static_cast<std::size_t>(handle)];
        return entry.state.load() == 1 ? juce::String("still loading") : entry.problem;
    }

    void UserLibrary::retireLocked(Entry& entry, double now)
    {
        entry.sample.store(nullptr, std::memory_order_release);
        entry.wavetable.store(nullptr, std::memory_order_release);
        entry.grains.store(nullptr, std::memory_order_release);
        if (entry.sampleOwner != nullptr || entry.wavetableOwner != nullptr || entry.grainsOwner != nullptr)
            dropped.push_back({ std::move(entry.sampleOwner), std::move(entry.wavetableOwner), std::move(entry.grainsOwner), now });
    }

    void UserLibrary::dropLocked(Entry& entry, double now)
    {
        retireLocked(entry, now);
        entry.state.store(0);
        entry.name.clear();
        entry.problem.clear();
        entry.loadWanted = false;
        entry.refs = 0;
    }

    void UserLibrary::run()
    {
        while (! threadShouldExit())
        {
            int index = -1;
            juce::String name;
            juce::File file;
            {
                const juce::ScopedLock scoped(lock);
                for (int i = 0; i < maxSounds && index < 0; ++i)
                {
                    auto& entry = entries[static_cast<std::size_t>(i)];
                    if (entry.loadWanted)
                    {
                        entry.loadWanted = false;
                        index = i;
                        name = entry.name;
                    }
                }
                if (index >= 0)
                    file = folder().getChildFile(name);
            }

            if (index >= 0)
            {
                auto loaded = load(file);
                const juce::ScopedLock scoped(lock);
                auto& entry = entries[static_cast<std::size_t>(index)];
                if (entry.name == name && entry.refs > 0 && ! entry.loadWanted)
                {
                    retireLocked(entry, nowSeconds()); // what it held before (a file read again from another folder)
                    entry.sampleOwner = std::move(loaded.sample);
                    entry.wavetableOwner = std::move(loaded.wavetable);
                    entry.grainsOwner = std::move(loaded.grains);
                    entry.problem = loaded.problem;
                    entry.sample.store(entry.sampleOwner.get(), std::memory_order_release);
                    entry.wavetable.store(entry.wavetableOwner.get(), std::memory_order_release);
                    entry.grains.store(entry.grainsOwner.get(), std::memory_order_release);
                    entry.state.store(entry.sampleOwner != nullptr ? 2 : 3);
                }
                continue; // a load first, the housekeeping after
            }

            {
                const juce::ScopedLock scoped(lock);
                const double now = nowSeconds();
                for (auto& entry : entries)
                    if (entry.refs == 0 && entry.state.load() != 0 && now - entry.releasedAt >= secondsHeldAfterRelease)
                        dropLocked(entry, now);
                dropped.erase(std::remove_if(dropped.begin(), dropped.end(),
                                             [now](const Dropped& d) { return now - d.at >= secondsBeforeFree; }),
                              dropped.end());
            }
            wait(250);
        }
    }

    UserLibrary::Loaded UserLibrary::load(const juce::File& file)
    {
        Loaded result;
        if (! file.existsAsFile())
        {
            result.problem = "not found: " + file.getFullPathName();
            return result;
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            result.problem = "not an audio file CoolSynth can read: " + file.getFileName();
            return result;
        }
        if (reader->lengthInSamples <= 0)
        {
            result.problem = "the file holds no audio: " + file.getFileName();
            return result;
        }
        if (static_cast<double>(reader->lengthInSamples) > maxSampleSeconds * reader->sampleRate)
        {
            result.problem = "longer than five minutes: " + file.getFileName();
            return result;
        }

        const int frames = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> audio(2, frames);
        reader->read(&audio, 0, frames, 0, true, true);
        const float* left = audio.getReadPointer(0);
        const float* right = audio.getReadPointer(reader->numChannels > 1 ? 1 : 0);

        // The sample: one recording over every key and velocity, as the file is.
        auto style = std::make_unique<LoadedStyle>();
        constexpr int guard = LoadedStyle::guardFrames;
        style->storage.assign(2u * static_cast<std::size_t>(frames + 2 * guard), 0.0f);
        float* out = style->storage.data() + 2 * guard;
        for (int i = 0; i < frames; ++i)
        {
            out[2 * i] = left[i];
            out[2 * i + 1] = right[i];
        }
        SampleZone zone;
        zone.frames = frames;
        zone.rate = reader->sampleRate;
        zone.data = out;
        const auto& meta = reader->metadataValues;
        if (meta.containsKey("MidiUnityNote"))
            zone.root = juce::jlimit(0, 127, meta.getValue("MidiUnityNote", "60").getIntValue());
        if (meta.containsKey("MidiPitchFraction"))
        {
            // The sampler chunk's fraction of a semitone the recording sounds above its note.
            const double fraction = meta.getValue("MidiPitchFraction", "0").getLargeIntValue() / 4294967296.0;
            zone.tuneCents = static_cast<float>(juce::jlimit(0.0, 100.0, 100.0 * fraction));
        }
        if (meta.getValue("NumSampleLoops", "0").getIntValue() > 0)
        {
            const int loopStart = meta.getValue("Loop0Start", "0").getIntValue();
            const int loopEnd = meta.getValue("Loop0End", "0").getIntValue() + 1; // the chunk names the last frame of the loop
            if (loopStart >= 0 && loopEnd <= frames && loopEnd - loopStart >= 64)
            {
                zone.loopStart = loopStart;
                zone.loopEnd = loopEnd;
            }
        }
        style->zones.push_back(zone);
        result.sample = std::move(style);

        // The grains' source: mono, at the pitch the sampler chunk gives.
        {
            std::vector<float> mono(static_cast<std::size_t>(frames));
            for (int i = 0; i < frames; ++i)
                mono[static_cast<std::size_t>(i)] = 0.5f * (left[i] + right[i]);
            const double rootHz = 440.0 * std::exp2((zone.root - 69.0 + 0.01 * zone.tuneCents) / 12.0);
            result.grains = buildGrainSource(mono.data(), frames, reader->sampleRate, rootHz);
        }

        // The wavetable, when the file is short enough to be one.
        const int clm = clmFrameSize(file);
        const int frameSize = clm > 0 ? clm : WavetableBank::tableSize;
        if (frames <= frameSize * maxWavetableFrames)
            result.wavetable = buildWavetable(left, frames, frameSize);
        return result;
    }

    std::unique_ptr<UserWavetable> UserLibrary::buildWavetable(const float* mono, int length, int frameSize)
    {
        if (mono == nullptr || length <= 0 || frameSize <= 0)
            return nullptr;

        constexpr int size = WavetableBank::tableSize;
        constexpr int levels = WavetableBank::levels;
        constexpr int stride = WavetableBank::stride;
        const bool singleCycle = length < frameSize;
        const int cycle = singleCycle ? length : frameSize;
        const int count = singleCycle ? 1 : std::min(maxWavetableFrames, length / frameSize);

        auto table = std::make_unique<UserWavetable>();
        table->frameCount = count;
        table->data.assign(static_cast<std::size_t>(count * levels * stride), 0.0f);

        constexpr int order = 11;
        static_assert((1 << order) == size);
        juce::dsp::FFT fft(order);
        std::vector<float> resampled(static_cast<std::size_t>(size));
        std::vector<float> spectrum(2 * size), buffer(2 * size);
        float peak = 0.0f;
        for (int f = 0; f < count; ++f)
        {
            const float* source = mono + static_cast<std::size_t>(f) * static_cast<std::size_t>(cycle);
            for (int i = 0; i < size; ++i)
                resampled[static_cast<std::size_t>(i)] = cycle == size ? source[i] : cyclic(source, cycle, static_cast<double>(i) / size);

            std::fill(spectrum.begin(), spectrum.end(), 0.0f);
            std::copy(resampled.begin(), resampled.end(), spectrum.begin());
            fft.performRealOnlyForwardTransform(spectrum.data(), true);

            for (int level = 0; level < levels; ++level)
            {
                // The factory bank's limits: harmonic 1 to 1023 at the full band, one harmonic fewer per octave up.
                const int limit = std::min(size / 2 - 1, (size / 2) >> level);
                std::fill(buffer.begin(), buffer.end(), 0.0f); // no offset: bin 0 stays empty
                for (int h = 1; h <= limit; ++h)
                {
                    buffer[static_cast<std::size_t>(2 * h)] = spectrum[static_cast<std::size_t>(2 * h)];
                    buffer[static_cast<std::size_t>(2 * h + 1)] = spectrum[static_cast<std::size_t>(2 * h + 1)];
                }
                fft.performRealOnlyInverseTransform(buffer.data());
                float* out = table->data.data() + static_cast<std::size_t>((f * levels + level) * stride);
                for (int i = 0; i < size; ++i)
                    out[i + 1] = buffer[static_cast<std::size_t>(i)];
                if (level == 0)
                    for (int i = 0; i < size; ++i)
                        peak = std::max(peak, std::abs(out[i + 1]));
            }
        }

        // One scale for the whole table: its loudest frame peaks at 1, the others keep their level against it.
        const float scale = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
        for (int f = 0; f < count; ++f)
            for (int level = 0; level < levels; ++level)
            {
                float* out = table->data.data() + static_cast<std::size_t>((f * levels + level) * stride);
                for (int i = 1; i <= size; ++i)
                    out[i] *= scale;
                out[0] = out[size];
                out[size + 1] = out[1];
                out[size + 2] = out[2];
            }
        return table;
    }

    std::unique_ptr<UserGrainSource> UserLibrary::buildGrainSource(const float* mono, int length, double rate, double rootHz)
    {
        if (mono == nullptr || length < 4 || rate <= 0.0)
            return nullptr;
        auto source = std::make_unique<UserGrainSource>();
        source->length = std::min(length, static_cast<int>(std::ceil(UserGrainSource::maxSeconds * rate)));
        source->rate = rate;
        source->rootHz = rootHz > 0.0 ? rootHz : 261.6255653005986;
        const int n = source->length;
        const int stride = n + UserGrainSource::guardBefore + UserGrainSource::guardAfter;
        source->storage.assign(static_cast<std::size_t>(UserGrainSource::levels * stride), 0.0f);

        // Levels 1 and 2: a Kaiser-windowed sinc low-pass (63 taps, beta 8: about 80 dB down) at a fifth and a tenth
        // of the rate, as the factory samples' 10 and 5 kHz at 48 kHz. The source wraps, so the filter does too.
        constexpr int half = 31;
        auto besselI0 = [](double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        for (int level = 0; level < UserGrainSource::levels; ++level)
        {
            float* out = source->storage.data() + static_cast<std::size_t>(level * stride + UserGrainSource::guardBefore);
            if (level == 0)
                std::copy(mono, mono + n, out);
            else
            {
                const double cutoff = level == 1 ? 0.21 : 0.105; // cycles per sample
                std::array<float, 2 * half + 1> kernel {};
                double sum = 0.0;
                for (int k = -half; k <= half; ++k)
                {
                    const double x = 2.0 * cutoff * k;
                    const double sinc = k == 0 ? 1.0 : std::sin(kPiDouble * x) / (kPiDouble * x);
                    const double r = static_cast<double>(k) / half;
                    const double w = besselI0(8.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(8.0);
                    kernel[static_cast<std::size_t>(k + half)] = static_cast<float>(2.0 * cutoff * sinc * w);
                    sum += 2.0 * cutoff * sinc * w;
                }
                for (auto& k : kernel)
                    k = static_cast<float>(k / sum); // unity gain at 0 Hz
                for (int i = 0; i < n; ++i)
                {
                    float acc = 0.0f;
                    if (i >= half && i < n - half)
                        for (int k = -half; k <= half; ++k)
                            acc += kernel[static_cast<std::size_t>(k + half)] * mono[i - k];
                    else
                        for (int k = -half; k <= half; ++k)
                            acc += kernel[static_cast<std::size_t>(k + half)] * mono[((i - k) % n + n) % n];
                    out[i] = acc;
                }
            }
            out[-1] = out[n - 1];
            out[n] = out[0];
            out[n + 1] = out[1 % n];
            out[n + 2] = out[2 % n];
            source->level[static_cast<std::size_t>(level)] = out;
        }
        return source;
    }

    int UserLibrary::clmFrameSize(const juce::File& file)
    {
        // A RIFF WAVE file's chunks in turn; Serum's "clm " chunk reads "<!>2048 ...".
        juce::FileInputStream in(file);
        if (! in.openedOk())
            return 0;
        char riff[4] {}, wave[4] {};
        in.read(riff, 4);
        in.readInt();
        in.read(wave, 4);
        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
            return 0;
        while (! in.isExhausted())
        {
            char id[4] {};
            if (in.read(id, 4) != 4)
                return 0;
            const auto chunkSize = static_cast<juce::int64>(static_cast<juce::uint32>(in.readInt()));
            const auto next = in.getPosition() + chunkSize + (chunkSize & 1);
            if (std::memcmp(id, "clm ", 4) == 0)
            {
                juce::MemoryBlock text;
                in.readIntoMemoryBlock(text, static_cast<juce::int64>(std::min<juce::int64>(chunkSize, 256)));
                const auto s = text.toString();
                const int at = s.indexOf("<!>");
                const int value = (at >= 0 ? s.substring(at + 3) : s).getIntValue();
                return value >= 16 && value <= 65536 ? value : 0;
            }
            if (! in.setPosition(next))
                return 0;
        }
        return 0;
    }
}
