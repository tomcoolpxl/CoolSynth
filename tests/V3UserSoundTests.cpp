#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/EngineV3.h"
#include "synth/v3/UserLibrary.h"

/*
    The user's own sounds (decision D137): files in the User folder, loaded in the background
    into a sample and, when short enough, a wavetable, and named by the patch.
*/
namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    /** Writes mono or stereo float audio as a 24-bit WAV, with sampler metadata if given. */
    void writeWav(const juce::File& file, const std::vector<float>& mono, double rate, const juce::StringPairArray& metadata = {})
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unordered_map<juce::String, juce::String> values;
        for (const auto& key : metadata.getAllKeys())
            values[key] = metadata[key];
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(1)
                                                                                    .withBitsPerSample(24).withMetadataValues(values));
        juce::AudioBuffer<float> buffer(1, static_cast<int>(mono.size()));
        std::copy(mono.begin(), mono.end(), buffer.getWritePointer(0));
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    /** Puts a Serum-style "clm " chunk right after a WAV file's header. */
    void addClmChunk(const juce::File& file, int frameSize)
    {
        juce::MemoryBlock original;
        file.loadFileAsData(original);
        const auto text = juce::String("<!>") + juce::String(frameSize) + " 10000000 wavetable (www.xferrecords.com)";
        juce::MemoryOutputStream chunk;
        chunk.write("clm ", 4);
        chunk.writeInt(static_cast<int>(text.getNumBytesAsUTF8()));
        chunk.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        if (text.getNumBytesAsUTF8() % 2 != 0)
            chunk.writeByte(0);

        juce::MemoryOutputStream out;
        out.write("RIFF", 4);
        out.writeInt(static_cast<int>(original.getSize() - 8 + chunk.getDataSize()));
        out.write(static_cast<const char*>(original.getData()) + 8, 4); // "WAVE"
        out.write(chunk.getData(), chunk.getDataSize());
        out.write(static_cast<const char*>(original.getData()) + 12, original.getSize() - 12);
        file.replaceWithData(out.getData(), out.getDataSize());
    }

    std::vector<float> sine(int length, double cyclesPerSample, float amplitude = 1.0f, int harmonic = 1)
    {
        std::vector<float> x(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
            x[static_cast<std::size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * kPi * cyclesPerSample * harmonic * i));
        return x;
    }

    float peak(const float* x, int n)
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = std::max(p, std::abs(x[i]));
        return p;
    }

    /** Upward zero crossings per second over a stretch. */
    double frequency(const std::vector<float>& x, double rate, std::size_t from)
    {
        int crossings = 0;
        for (std::size_t i = from + 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                ++crossings;
        return crossings * rate / static_cast<double>(x.size() - from);
    }

    std::vector<float> renderNote(int generatorType, int handle, int note, int samples)
    {
        auto part = std::make_unique<Part>();
        part->prepare(kRate, 512, 1);
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType;
        const auto& d = generatorType == generatorType::wavetable ? WavetableGenerator::descriptor()
                      : generatorType == generatorType::grain   ? GrainGenerator::descriptor()
                                                                 : SampleGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        g.level = 1.0f;
        g.userSound = handle;
        s.voice.processors[0].type = processorType::off;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };

        std::vector<float> left(static_cast<std::size_t>(samples)), right(static_cast<std::size_t>(samples));
        EngineMidiEvent on;
        on.type = Type::noteOn;
        on.noteNumber = static_cast<uint8_t>(note);
        on.value = 1.0f;
        const std::vector<EngineMidiEvent> events { on };
        for (int start = 0; start < samples; start += 256)
        {
            const int n = std::min(256, samples - start);
            const auto span = start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>();
            part->render(left.data() + start, right.data() + start, n, span, s, {});
        }
        return left;
    }
}

class V3UserSoundTests final : public juce::UnitTest
{
public:
    V3UserSoundTests() : juce::UnitTest("V3 user sounds", "CoolSynth") {}

    void runTest() override
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("CoolSynthUserSoundTests_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
        folder.createDirectory();
        auto& library = UserLibrary::instance();
        library.setFolder(folder);

        beginTest("a_single_cycle_becomes_one_frame_without_its_offset_at_full_level");
        {
            auto cycle = sine(600, 1.0 / 600.0, 0.5f);
            for (auto& v : cycle)
                v += 0.3f; // an offset the table must not keep
            const auto table = UserLibrary::buildWavetable(cycle.data(), static_cast<int>(cycle.size()), 2048);
            expect(table != nullptr);
            expectEquals(table->frameCount, 1);
            const float* f = table->frame(0, 0);
            expectWithinAbsoluteError(peak(f, WavetableBank::tableSize), 1.0f, 1.0e-3f, "scaled to peak at 1");
            double mean = 0.0;
            for (int i = 0; i < WavetableBank::tableSize; ++i)
                mean += f[i];
            expectWithinAbsoluteError(mean / WavetableBank::tableSize, 0.0, 1.0e-4, "no offset");
            expectWithinAbsoluteError(f[512], 1.0f, 2.0e-3f, "a quarter cycle in: the sine's top");
            expectWithinAbsoluteError(f[-1], f[WavetableBank::tableSize - 1], 1.0e-6f, "guard samples wrap the cycle");
        }

        beginTest("frames_keep_their_levels_and_lose_their_top_harmonics_at_the_band_limits");
        {
            std::vector<float> audio;
            for (int k = 0; k < 4; ++k) // frame k: harmonic k + 1 at level (k + 1) / 4
            {
                const auto frame = sine(2048, 1.0 / 2048.0, 0.25f * (k + 1), k + 1);
                audio.insert(audio.end(), frame.begin(), frame.end());
            }
            const auto table = UserLibrary::buildWavetable(audio.data(), static_cast<int>(audio.size()), 2048);
            expectEquals(table->frameCount, 4);
            expectWithinAbsoluteError(peak(table->frame(3, 0), 2048), 1.0f, 1.0e-3f, "the loudest frame peaks at 1");
            expectWithinAbsoluteError(peak(table->frame(0, 0), 2048), 0.25f, 1.0e-3f, "a quieter frame stays quieter");
            expectWithinAbsoluteError(peak(table->frame(3, 8), 2048), 1.0f, 1.0e-3f, "harmonic 4 is inside the limit of level 8");
            expectLessThan(peak(table->frame(3, 9), 2048), 1.0e-4f, "and outside the limit of level 9");

            std::vector<float> many(static_cast<std::size_t>(2048 * 300), 0.1f);
            for (std::size_t i = 0; i < many.size(); ++i)
                many[i] = static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(i) / 2048.0));
            expectEquals(UserLibrary::buildWavetable(many.data(), static_cast<int>(many.size()), 2048)->frameCount,
                         UserLibrary::maxWavetableFrames, "at most 256 frames");
        }

        beginTest("a_wav_file_is_read_as_a_sample_and_a_wavetable_with_its_clm_frame_size");
        {
            std::vector<float> audio;
            for (int k = 0; k < 8; ++k)
            {
                const auto frame = sine(1024, 1.0 / 1024.0, 0.5f, k % 3 + 1);
                audio.insert(audio.end(), frame.begin(), frame.end());
            }
            const auto file = folder.getChildFile("Tables").getChildFile("eight.wav");
            file.getParentDirectory().createDirectory();
            writeWav(file, audio, 44100.0);
            expectEquals(UserLibrary::clmFrameSize(file), 0, "no chunk yet");
            expectEquals(UserLibrary::load(file).wavetable->frameCount, 4, "2048 samples a frame without the chunk");
            addClmChunk(file, 1024);
            expectEquals(UserLibrary::clmFrameSize(file), 1024);
            const auto loaded = UserLibrary::load(file);
            expect(loaded.problem.isEmpty(), loaded.problem);
            expectEquals(loaded.wavetable->frameCount, 8, "the chunk's 1024 samples a frame");
            expect(loaded.sample != nullptr && loaded.sample->zones.size() == 1u);
            expectEquals(loaded.sample->zones.front().frames, 8 * 1024);
            expectEquals(loaded.sample->zones.front().rate, 44100.0);
        }

        beginTest("a_samples_root_note_and_loop_come_from_its_sampler_chunk");
        {
            juce::StringPairArray meta;
            meta.set("MidiUnityNote", "69");
            meta.set("NumSampleLoops", "1");
            meta.set("Loop0Type", "0");
            meta.set("Loop0Start", "1000");
            meta.set("Loop0End", "20000");
            const auto file = folder.getChildFile("a440.wav");
            writeWav(file, sine(48000, 440.0 / kRate, 0.5f), kRate, meta);
            const auto loaded = UserLibrary::load(file);
            expect(loaded.sample != nullptr, loaded.problem);
            const auto& zone = loaded.sample->zones.front();
            expectEquals(zone.root, 69);
            expectEquals(zone.loopStart, 1000);
            expectEquals(zone.loopEnd, 20001, "the chunk names the loop's last frame");
            expect(loaded.wavetable != nullptr && loaded.wavetable->frameCount == 23, "a second is short enough for 23 frames");
        }

        beginTest("a_missing_file_says_so_and_a_handle_is_shared_and_released");
        {
            const int missing = library.acquire("nowhere.wav");
            expect(missing >= 0);
            expect(! library.waitUntilLoaded(missing, 5000));
            expect(library.problem(missing).startsWith("not found"), library.problem(missing));
            library.release(missing);

            const int a = library.acquire("a440.wav");
            const int b = library.acquire("a440.wav");
            expectEquals(a, b, "one file, one sound in memory");
            expect(library.waitUntilLoaded(a, 10000), library.problem(a));
            expect(library.sample(a) != nullptr);
            library.release(a);
            library.release(b);
        }

        beginTest("a_wavetable_slot_plays_the_users_table_at_the_notes_pitch");
        {
            const auto file = folder.getChildFile("cycle.wav");
            writeWav(file, sine(2048, 1.0 / 2048.0, 0.8f), kRate);
            const int handle = library.acquire("cycle.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            const auto out = renderNote(generatorType::wavetable, handle, 69, 24000);
            expectGreaterThan(static_cast<double>(peak(out.data() + 4800, 19200)), 0.05);
            expectWithinAbsoluteError(frequency(out, kRate, 4800), 440.0, 3.0, "A4");
            library.release(handle);

            const int nothing = library.acquire("not-there.wav");
            library.waitUntilLoaded(nothing, 5000);
            const auto silent = renderNote(generatorType::wavetable, nothing, 69, 9600);
            expectLessThan(peak(silent.data(), 9600), 1.0e-6f, "a slot whose file is missing is silent");
            library.release(nothing);
        }

        beginTest("a_sample_slot_plays_the_users_sample_from_its_root_note");
        {
            const int handle = library.acquire("a440.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            const auto out = renderNote(generatorType::sample, handle, 81, 24000);
            expectWithinAbsoluteError(frequency(out, kRate, 4800), 880.0, 6.0, "an octave above its root note");
            library.release(handle);
        }

        beginTest("the_grain_source_keeps_the_low_band_and_removes_the_high_one_at_its_darker_levels");
        {
            auto level = [](const UserGrainSource& source, int index, int from, int to)
            {
                double sum = 0.0;
                for (int i = from; i < to; ++i)
                    sum += static_cast<double>(source.data(index)[i]) * source.data(index)[i];
                return std::sqrt(sum / (to - from));
            };
            const auto low = sine(48000, 0.05);
            const auto high = sine(48000, 0.4);
            const auto lowSource = UserLibrary::buildGrainSource(low.data(), 48000, kRate, 440.0);
            const auto highSource = UserLibrary::buildGrainSource(high.data(), 48000, kRate, 440.0);
            const double reference = level(*lowSource, 0, 1000, 47000);
            expectWithinAbsoluteError(level(*lowSource, 1, 1000, 47000) / reference, 1.0, 0.01, "a low tone passes");
            expectLessThan(level(*highSource, 1, 1000, 47000) / reference, 1.0e-3, "a tone at 0.4 of the rate is gone");
            expectLessThan(level(*highSource, 2, 1000, 47000) / reference, 1.0e-3);
            expectWithinAbsoluteError(lowSource->data(0)[-1], lowSource->data(0)[47999], 1.0e-7f, "the source wraps");

            std::vector<float> long_(static_cast<std::size_t>(kRate * 70.0), 0.1f);
            expectEquals(UserLibrary::buildGrainSource(long_.data(), static_cast<int>(long_.size()), kRate, 440.0)->length,
                         static_cast<int>(kRate * 60.0), "the first minute");
        }

        beginTest("a_grain_slot_plays_the_users_sample");
        {
            const int handle = library.acquire("a440.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            const auto out = renderNote(generatorType::grain, handle, 69, 24000);
            expectGreaterThan(static_cast<double>(peak(out.data() + 4800, 19200)), 0.05, "grains sound");
            expectWithinAbsoluteError(frequency(out, kRate, 4800), 440.0, 15.0, "at the sample's own pitch on its root note");
            library.release(handle);

            const int nothing = library.acquire("not-there-either.wav");
            library.waitUntilLoaded(nothing, 5000);
            expectLessThan(peak(renderNote(generatorType::grain, nothing, 69, 9600).data(), 9600), 1.0e-6f, "missing: silent");
            library.release(nothing);
        }

        library.setFolder(juce::File());
        folder.deleteRecursively();
    }
};

static V3UserSoundTests v3UserSoundTests;

/** The file names a patch gives its generator slots: in bank files, presets, saved states. */
class V3UserFileStateTests final : public juce::UnitTest
{
public:
    V3UserFileStateTests() : juce::UnitTest("V3 user files in patches", "CoolSynth") {}

    void runTest() override
    {
        beginTest("a_bank_line_names_a_file_with_spaces_and_a_preset_puts_it_into_the_state");
        {
            const auto parsed = coolsynth::presets::parseBankFile(
                "[Mine]\ng1 = Wavetable file=\"Pads/My Table.wav\" pos=0.5\nb.g2 = Sample file=voice.wav\n", "mine.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : parsed.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            expect(entries.contains("pA.g1.file=Pads/My Table.wav"), entries.joinIntoString(", "));
            expect(entries.contains("pB.g2.file=voice.wav"));
            expect(entries.contains("pA.g1.p2=0.5"));

            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto& preset = parsed.presets.front();
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), preset);
            expect(problems.isEmpty(), problems.joinIntoString("; "));
            expectEquals(processor->getUserFile(0, 1), juce::String("Pads/My Table.wav"));
            expectEquals(processor->getUserFile(1, 2), juce::String("voice.wav"));
            expectEquals(processor->getUserFile(0, 2), juce::String());
        }

        beginTest("a_saved_state_keeps_the_file_names_and_the_init_patch_clears_them");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            source->setUserFile(0, 3, "Pads\\Glass.wav");
            expectEquals(source->getUserFile(0, 3), juce::String("Pads/Glass.wav"), "forward slashes");
            juce::MemoryBlock saved;
            source->getStateInformation(saved);

            auto target = std::make_unique<SynthAudioProcessor>();
            target->setUserFile(1, 1, "other.wav");
            target->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            expectEquals(target->getUserFile(0, 3), juce::String("Pads/Glass.wav"));
            expectEquals(target->getUserFile(1, 1), juce::String(), "what the state does not name is gone");

            target->resetAutomatableParametersToDefaults();
            expectEquals(target->getUserFile(0, 3), juce::String());
        }
    }
};

static V3UserFileStateTests v3UserFileStateTests;
