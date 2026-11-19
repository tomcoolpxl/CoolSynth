#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "presets/BankFile.h"
#include "synth/v3/EngineV3.h"
#include "synth/v3/Part.h"
#include "synth/v3/SampleGenerator.h"
#include "synth/v3/UserLibrary.h"

/*
    Slices and kits of the user's own sounds in the Sample generator (D207).
*/
namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kBlock = 256;

    void writeWav(const juce::File& file, const std::vector<float>& mono, double rate)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));
        juce::AudioBuffer<float> buffer(1, static_cast<int>(mono.size()));
        std::copy(mono.begin(), mono.end(), buffer.getWritePointer(0));
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    /** Four quarters of a second, each a sine of its own: 200, 300, 400 and 500 Hz. */
    std::vector<float> quarters(bool rising = false)
    {
        const int quarter = static_cast<int>(kRate / 4);
        std::vector<float> x(static_cast<std::size_t>(4 * quarter));
        for (int q = 0; q < 4; ++q)
            for (int i = 0; i < quarter; ++i)
            {
                const double level = rising ? static_cast<double>(i) / quarter : 1.0;
                x[static_cast<std::size_t>(q * quarter + i)] = static_cast<float>(0.5 * level * std::sin(2.0 * kPi * (200.0 + 100.0 * q) * i / kRate));
            }
        return x;
    }

    /** Struck tones of 1 kHz, each rising in 2 ms and dying over 80 ms to the end, at the given times and levels. */
    std::vector<float> hits(const std::vector<double>& seconds, const std::vector<float>& levels, double length)
    {
        std::vector<float> x(static_cast<std::size_t>(length * kRate), 0.0f);
        for (std::size_t h = 0; h < seconds.size(); ++h)
        {
            const int at = static_cast<int>(seconds[h] * kRate);
            for (int i = 0; at + i < static_cast<int>(x.size()); ++i)
            {
                const double t = i / kRate;
                const double envelope = std::min(1.0, t / 0.002) * std::exp(-t / 0.08);
                x[static_cast<std::size_t>(at + i)] += static_cast<float>(levels[h] * envelope * std::sin(2.0 * kPi * 1000.0 * t));
            }
        }
        return x;
    }

    std::vector<float> sine(double hz, double seconds)
    {
        std::vector<float> x(static_cast<std::size_t>(seconds * kRate));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
        return x;
    }

    struct Play
    {
        int handle = -1;
        int slices = 1;        // 0 Off, 1 Equal, 2 Attacks
        int count = 4;
        float sense = 1.0f;
        int firstKey = 60;
        bool reverse = false;
        float pitchSemitones = 0.0f;
        PlayMode mode = PlayMode::poly;
    };

    /** A part playing the Sample slot: notes as (key, start sample, release sample or -1). */
    std::vector<float> render(const Play& play, const std::vector<std::array<int, 3>>& notes, int samples)
    {
        auto part = std::make_unique<Part>();
        part->prepare(kRate, 512, 1);
        PartSettings s;
        s.playMode = play.mode;
        auto& g = s.voice.generators[0];
        g.type = generatorType::sample;
        const auto& d = SampleGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        auto set = [&](int index, float units) { g.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], units); };
        set(SampleGenerator::slices, static_cast<float>(play.slices));
        set(SampleGenerator::count, static_cast<float>(play.count - SampleGenerator::fewestSlices));
        set(SampleGenerator::sense, play.sense);
        set(SampleGenerator::firstKey, static_cast<float>(play.firstKey));
        set(SampleGenerator::reverse, play.reverse ? 1.0f : 0.0f);
        g.level = 1.0f;
        g.pitchSemitones = play.pitchSemitones;
        g.userSound = play.handle;
        s.voice.processors[0].type = processorType::off;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };

        std::vector<float> left(static_cast<std::size_t>(samples)), right(static_cast<std::size_t>(samples));
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            std::vector<EngineMidiEvent> events;
            for (const auto& [key, on, off] : notes)
            {
                if (on >= start && on < start + n)
                {
                    EngineMidiEvent e;
                    e.type = Type::noteOn;
                    e.noteNumber = static_cast<uint8_t>(key);
                    e.value = 1.0f;
                    e.sampleOffset = on - start;
                    events.push_back(e);
                }
                if (off >= start && off < start + n)
                {
                    EngineMidiEvent e;
                    e.type = Type::noteOff;
                    e.noteNumber = static_cast<uint8_t>(key);
                    e.sampleOffset = off - start;
                    events.push_back(e);
                }
            }
            part->render(left.data() + start, right.data() + start, n, std::span<const EngineMidiEvent>(events), s, {});
        }
        return left;
    }

    double rms(const std::vector<float>& x, int from, int to)
    {
        double sum = 0.0;
        for (int i = std::max(0, from); i < std::min(to, static_cast<int>(x.size())); ++i)
            sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
        return std::sqrt(sum / std::max(1, to - from));
    }

    double frequency(const std::vector<float>& x, int from, int to)
    {
        double first = -1.0, last = -1.0;
        int periods = -1;
        for (int i = std::max(1, from); i < std::min(to, static_cast<int>(x.size())); ++i)
            if (x[static_cast<std::size_t>(i - 1)] < 0.0f && x[static_cast<std::size_t>(i)] >= 0.0f)
            {
                const double t = (i - 1) - x[static_cast<std::size_t>(i - 1)] / (x[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i - 1)]);
                if (first < 0.0)
                    first = t;
                last = t;
                ++periods;
            }
        return periods > 0 ? periods * kRate / (last - first) : 0.0;
    }

    /** The largest step of the slope in a stretch: what a click is made of. */
    double roughest(const std::vector<float>& x, int from, int to)
    {
        double worst = 0.0;
        for (int i = std::max(2, from); i < std::min(to, static_cast<int>(x.size())); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(x[static_cast<std::size_t>(i)]) - 2.0 * x[static_cast<std::size_t>(i - 1)] + x[static_cast<std::size_t>(i - 2)]));
        return worst;
    }
}

class V3SliceTests final : public juce::UnitTest
{
public:
    V3SliceTests() : juce::UnitTest("V3 slices and kits", "V3") {}

    void runTest() override
    {
        // Opt-in (COOLSYNTH_ATTACKS_OF=a WAV): the attacks the detector finds in a file, to study one, with the
        // Sensitivity at which each comes in ((1 - sensitivity) squared of the strongest).
        if (const auto path = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_ATTACKS_OF", {}); path.isNotEmpty())
        {
            beginTest("the_attacks_of_a_file");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor(juce::File(path)) })
            {
                juce::AudioBuffer<float> audio(1, static_cast<int>(reader->lengthInSamples));
                reader->read(&audio, 0, audio.getNumSamples(), 0, true, false);
                const auto attacks = UserLibrary::findAttacks(audio.getReadPointer(0), audio.getNumSamples(), reader->sampleRate);
                for (const auto& a : attacks)
                    logMessage(juce::String(a.frame / reader->sampleRate, 3) + " s, strength " + juce::String(a.strength, 3)
                               + ", in from Sensitivity " + juce::String(100.0f * (1.0f - std::sqrt(a.strength)), 0) + " %");
            }
            return;
        }
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("CoolSynthSliceTests_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
        folder.createDirectory();
        auto& library = UserLibrary::instance();
        library.setFolder(folder);
        const int quarter = static_cast<int>(kRate / 4);

        writeWav(folder.getChildFile("quarters.wav"), quarters(), kRate);
        writeWav(folder.getChildFile("rising.wav"), quarters(true), kRate);
        const std::vector<double> times { 0.1, 0.3, 0.5, 0.7, 0.95 };
        writeWav(folder.getChildFile("hits.wav"), hits(times, { 1.0f, 0.3f, 0.8f, 0.15f, 0.6f }, 1.3), kRate);
        folder.getChildFile("Kit").createDirectory();
        writeWav(folder.getChildFile("Kit/02 middle.wav"), sine(330.0, 0.3), kRate);
        writeWav(folder.getChildFile("Kit/1 low.wav"), sine(220.0, 0.3), kRate);
        writeWav(folder.getChildFile("Kit/10 high.wav"), sine(440.0, 0.3), kRate);

        beginTest("equal_slices_play_their_piece_at_its_own_speed_one_per_key");
        {
            const int handle = library.acquire("quarters.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            Play play;
            play.handle = handle;
            for (int k = 0; k < 4; ++k)
            {
                const auto out = render(play, { { 60 + k, 0, -1 } }, quarter * 2);
                expectWithinAbsoluteError(frequency(out, 2000, quarter - 1000), 200.0 + 100.0 * k, 1.0, "slice " + juce::String(k));
                expectLessThan(rms(out, quarter + 1000, 2 * quarter), 1.0e-4, "the slice ends with its piece, the key still held");
            }
            expectLessThan(rms(render(play, { { 64, 0, -1 } }, quarter), 0, quarter), 1.0e-6, "a key past the slices is silent");
            expectLessThan(rms(render(play, { { 59, 0, -1 } }, quarter), 0, quarter), 1.0e-6, "and so is one below the first key");

            play.pitchSemitones = 12.0f;
            const auto up = render(play, { { 62, 0, -1 } }, quarter);
            expectWithinAbsoluteError(frequency(up, 1000, quarter / 2 - 500), 800.0, 2.0, "an octave up moves a slice");

            play.pitchSemitones = 0.0f;
            const auto out = render(play, { { 61, 0, -1 } }, quarter * 2);
            expectLessThan(roughest(out, 0, quarter + 2000), 0.02, "no click where the slice starts or ends");
            library.release(handle);
        }

        beginTest("a_slice_plays_to_its_end_after_the_key_is_let_go");
        {
            const int handle = library.acquire("quarters.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            Play play;
            play.handle = handle;
            const auto out = render(play, { { 60, 0, 960 } }, quarter * 2);
            expectGreaterThan(rms(out, quarter / 2, quarter - 2000), 0.1, "still sounding long after a 20 ms press");
            expectLessThan(rms(out, quarter + 2400, 2 * quarter), 1.0e-4, "and done once its piece is");

            play.slices = 0; // the whole file, as before: the key's release ends it
            const auto whole = render(play, { { 60, 0, 960 } }, quarter);
            expectLessThan(rms(whole, quarter / 2, quarter), 1.0e-4, "without slices a short press stops it");
            library.release(handle);
        }

        beginTest("reverse_plays_a_slice_from_its_end");
        {
            const int handle = library.acquire("rising.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            Play play;
            play.handle = handle;
            const auto forward = render(play, { { 60, 0, -1 } }, quarter);
            play.reverse = true;
            const auto backward = render(play, { { 60, 0, -1 } }, quarter);
            expectGreaterThan(rms(forward, quarter / 2, quarter), 2.0 * rms(forward, 0, quarter / 2), "forward it swells");
            expectGreaterThan(rms(backward, 0, quarter / 2), 2.0 * rms(backward, quarter / 2, quarter), "backward it fades");
            library.release(handle);
        }

        beginTest("in_mono_a_new_key_cuts_the_slice_and_plays_its_own");
        {
            const int handle = library.acquire("quarters.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            Play play;
            play.handle = handle;
            play.mode = PlayMode::mono;
            const auto out = render(play, { { 60, 0, -1 }, { 63, quarter / 2, -1 } }, quarter * 2);
            expectWithinAbsoluteError(frequency(out, 1000, quarter / 2 - 500), 200.0, 1.0);
            expectWithinAbsoluteError(frequency(out, quarter / 2 + 2000, quarter), 500.0, 1.0, "the new key's slice");
            library.release(handle);
        }

        beginTest("attacks_are_found_where_the_sound_rises");
        {
            const auto audio = hits(times, { 1.0f, 0.3f, 0.8f, 0.15f, 0.6f }, 1.3);
            const auto attacks = UserLibrary::findAttacks(audio.data(), static_cast<int>(audio.size()), kRate);
            expectEquals(static_cast<int>(attacks.size()), 5);
            for (std::size_t a = 0; a < std::min(attacks.size(), times.size()); ++a)
                expectWithinAbsoluteError(attacks[a].frame / kRate, times[a], 0.003, "attack " + juce::String(static_cast<int>(a)));
            float strongest = 0.0f;
            for (const auto& a : attacks)
                strongest = std::max(strongest, a.strength);
            expectWithinAbsoluteError(strongest, 1.0f, 1.0e-6f);
            expect(attacks.size() < 2 || attacks[1].strength < attacks[0].strength, "the quieter hit is the weaker attack");
        }

        beginTest("sensitivity_keeps_more_or_fewer_attack_slices");
        {
            const int handle = library.acquire("hits.wav");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            const auto* loaded = library.sample(handle);
            expect(loaded != nullptr && ! loaded->attacks.empty(), "the loader found the attacks");
            if (loaded != nullptr)
            {
                const auto& zone = loaded->zones.front();
                auto sliceCount = [&](float sensitivity)
                {
                    int n = 0, from = 0, to = 0;
                    while (SampleGenerator::sliceRange(*loaded, zone, 2, 0, sensitivity, n, from, to))
                        ++n;
                    return n;
                };
                expectEquals(sliceCount(1.0f), 6, "every attack, and the start");
                expectEquals(sliceCount(0.0f), 2, "only the strongest");
                expect(sliceCount(0.5f) >= 2 && sliceCount(0.5f) <= 6);
                int from = 0, to = 0;
                for (int k = 1; k <= 5; ++k)
                {
                    expect(SampleGenerator::sliceRange(*loaded, zone, 2, 0, 1.0f, k, from, to));
                    expectWithinAbsoluteError(from / kRate, times[static_cast<std::size_t>(k - 1)] - 0.002, 0.003, "slice " + juce::String(k) + " starts just before its hit");
                }

                Play play;
                play.handle = handle;
                play.slices = 2;
                const auto out = render(play, { { 63, 0, 960 } }, static_cast<int>(0.3 * kRate));
                expectGreaterThan(rms(out, 200, 4000), 0.1, "the fourth slice starts on its hit at once");
                expectLessThan(rms(out, static_cast<int>(0.21 * kRate), static_cast<int>(0.3 * kRate)), 1.0e-3, "and ends before the next");
            }
            library.release(handle);
        }

        beginTest("a_folder_is_a_kit_one_file_per_key_in_name_order");
        {
            const auto files = UserLibrary::kitFiles(folder.getChildFile("Kit"));
            expectEquals(files.size(), 3);
            if (files.size() == 3)
            {
                expectEquals(files[0].getFileName(), juce::String("1 low.wav"));
                expectEquals(files[1].getFileName(), juce::String("02 middle.wav"));
                expectEquals(files[2].getFileName(), juce::String("10 high.wav"));
            }
            const int handle = library.acquire("Kit");
            expect(library.waitUntilLoaded(handle, 10000), library.problem(handle));
            const auto* loaded = library.sample(handle);
            expect(loaded != nullptr && loaded->kit && loaded->zones.size() == 3, "three zones");
            Play play;
            play.handle = handle;
            play.slices = 0; // a kit plays one file per key whatever Slices says
            play.firstKey = 48;
            const double expected[] = { 220.0, 330.0, 440.0 };
            for (int k = 0; k < 3; ++k)
            {
                const auto out = render(play, { { 48 + k, 0, 480 } }, static_cast<int>(0.4 * kRate));
                expectWithinAbsoluteError(frequency(out, 1000, static_cast<int>(0.25 * kRate)), expected[k], 1.0, "file " + juce::String(k));
            }
            expectLessThan(rms(render(play, { { 51, 0, -1 } }, quarter), 0, quarter), 1.0e-6, "past the last file: silent");
            library.release(handle);
        }

        beginTest("a_bank_line_sets_slices_and_names_a_kit");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"([Chops]
room = Workshop
g1 = Sample file="Breaks/amen.wav" slices=attacks sense=0.7 firstkey=C2
[Found Kit]
room = Workshop
g1 = Sample file="Kits/Found" slices=equal count=8 firstkey=48
)", "slices.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            bool attacks = false, key = false, count = false, kit = false;
            for (const auto& preset : parsed.presets)
                for (const auto& entry : preset.entries)
                {
                    attacks = attacks || (entry.parameterId == "pA.g1.p9" && entry.text == "attacks");
                    key = key || (entry.parameterId == "pA.g1.p12" && entry.text == "C2");
                    count = count || (entry.parameterId == "pA.g1.p10" && entry.text == "8");
                    kit = kit || (entry.text == "Kits/Found");
                }
            expect(attacks && key && count && kit);
            expectEquals(SampleGenerator::descriptor().usedParams, 12);
            const auto& firstKey = SampleGenerator::descriptor().params[SampleGenerator::firstKey];
            expectEquals(juce::StringArray::fromTokens(firstKey.options, "|", "")[48], juce::String("C3"), "the MiniLab's lowest key");
        }

        library.setFolder(juce::File());
        folder.deleteRecursively();
    }
};

static V3SliceTests v3SliceTests;
