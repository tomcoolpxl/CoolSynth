#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "TestSampleLibrary.h"
#include "parameters/v3/PartParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/InstrumentCatalogue.h"
#include "synth/v3/SampleGenerator.h"
#include "synth/v3/SampleLibrary.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 96000.0; // a voice's rate
    constexpr int kSubBlock = 32;

    struct Rig
    {
        SlotMemory memory;
        SampleGenerator generator;
    };

    std::unique_ptr<Rig> makeRig()
    {
        auto rig = std::make_unique<Rig>();
        rig->memory.allocate(SampleGenerator::memoryFloats(kRate, kSubBlock));
        rig->generator.prepare(kRate, kSubBlock, rig->memory);
        return rig;
    }

    SlotParams instrumentParams(int instrument, int style, int midiNote, bool loop = true)
    {
        SlotParams p;
        p.p[SampleGenerator::sample] = static_cast<float>(SampleGenerator::firstInstrument + instrument);
        p.p[SampleGenerator::style] = static_cast<float>(style);
        p.p[SampleGenerator::loop] = loop ? 1.0f : 0.0f;
        p.p[SampleGenerator::start] = 0.0f;
        p.frequencyHz = static_cast<float>(440.0 * std::exp2((midiNote - 69) / 12.0));
        return p;
    }

    struct Stereo
    {
        std::vector<float> left, right;
    };

    Stereo render(SampleGenerator& g, const SlotParams& p, int samples)
    {
        Stereo out;
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            out.left.insert(out.left.end(), l.begin(), l.begin() + n);
            out.right.insert(out.right.end(), r.begin(), r.begin() + n);
        }
        return out;
    }

    Stereo play(int instrument, int style, int midiNote, float velocity, int samples, bool loop = true)
    {
        auto rig = makeRig();
        NoteContext note;
        note.noteNumber = midiNote;
        note.velocity = velocity;
        rig->generator.noteOn(note);
        return render(rig->generator, instrumentParams(instrument, style, midiNote, loop), samples);
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double zeroCrossingHz(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t i = from + 1; i < std::min(to, x.size()); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double at = static_cast<double>(i - 1) + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
                if (first < 0.0)
                    first = at;
                last = at;
                ++crossings;
            }
        return crossings > 1 ? kRate * (crossings - 1) / (last - first) : 0.0;
    }

    float largestStep(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float step = 0.0f;
        for (std::size_t i = from + 1; i < std::min(to, x.size()); ++i)
            step = std::max(step, std::abs(x[i] - x[i - 1]));
        return step;
    }

    juce::String entryText(const coolsynth::presets::BankPreset& preset, const juce::String& id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return entry.text;
        return "<none>";
    }
}

class V3InstrumentTests final : public juce::UnitTest
{
public:
    V3InstrumentTests() : juce::UnitTest("V3 sampled instruments", "V3") {}

    void runTest() override
    {
        const auto libraryFolder = coolsynth::testing::installTestSampleLibrary();
        const auto& catalogue = InstrumentCatalogue::instance();
        auto& library = SampleLibrary::instance();
        const int cellos = catalogue.find("Cellos");
        const int marimba = catalogue.find("Marimba");

        beginTest("the_catalogue_has_forty_instruments_in_seven_families_and_stable_numbers");
        {
            expectEquals(catalogue.count(), 40);
            expectEquals(static_cast<int>(catalogue.families().size()), 7);
            int styles = 0;
            for (int i = 0; i < catalogue.count(); ++i)
            {
                const auto& instrument = catalogue.at(i);
                expect(! instrument.name.empty() && ! instrument.family.empty());
                expect(! instrument.styles.empty() && static_cast<int>(instrument.styles.size()) <= maxInstrumentStyles, instrument.name);
                styles += static_cast<int>(instrument.styles.size());
            }
            expectEquals(styles, 74);
            // The numbers are saved in patches: these must never move.
            expectEquals(catalogue.find("Steinway Grand"), 0);
            expectEquals(catalogue.find("steinway-grand"), 0);
            expectEquals(catalogue.find("Timpani"), 39);
            expectEquals(catalogue.find("nothing of the kind"), -1);
            expect(cellos > 0);
            expectEquals(catalogue.findStyle(cellos, "Sustain"), 0);
            expectEquals(catalogue.findStyle(cellos, "tremolo"), 1);
            expectEquals(catalogue.findStyle(cellos, "Pizzicato"), 2);
            expectEquals(catalogue.findStyle(cellos, "spiccato"), -1);
            expectEquals(juce::String(catalogue.at(cellos).styles[1].folder), juce::String("strings/cellos/tremolo"));
            expect(catalogue.at(cellos).styles[0].sustains && ! catalogue.at(cellos).styles[2].sustains);
            expectEquals(juce::String(InstrumentCatalogue::slug("Mallets and Bells")), juce::String("mallets-and-bells"));
            expectEquals(juce::String(InstrumentCatalogue::slug("8'")), juce::String("8"));

            const InstrumentCatalogue small("# a comment\nKeys | Piano | Grand | no | x\nKeys | Piano | Soft | no | y\nStrings | Viol | Bowed | yes | z\n");
            expectEquals(small.count(), 2);
            expectEquals(static_cast<int>(small.at(0).styles.size()), 2);
            expectEquals(juce::String(small.joinedNames()), juce::String("Piano|Viol"));

            // The Sample generator offers the five generated samples and then every instrument.
            const auto& d = SampleGenerator::descriptor();
            expectEquals(juce::StringArray::fromTokens(juce::String(d.params[SampleGenerator::sample].options), "|", "").size(), 5 + 40);
            expectEquals(static_cast<int>(d.params[SampleGenerator::sample].max), 44);
        }

        beginTest("a_style_is_read_from_its_folder_and_a_note_finds_its_recording");
        {
            const auto style = SampleLibrary::readStyle(libraryFolder.getChildFile(juce::String(catalogue.at(cellos).styles[0].folder)));
            expect(style != nullptr);
            if (style == nullptr)
                return;
            expectEquals(static_cast<int>(style->zones.size()), 5);
            expectEquals(style->layers, 2);
            expectEquals(style->zones.front().root, 48);
            expectEquals(style->zones.back().root, 72);
            expectEquals(style->zones.front().frames, coolsynth::testing::testSampleFrames);
            expectEquals(style->zones.front().loopEnd, coolsynth::testing::testLoopEnd);

            // The nearest recorded note; of two equally near, the lower.
            expectEquals(style->pick(50.0f, 1.0f, 0)->root, 48);
            expectEquals(style->pick(54.0f, 1.0f, 0)->root, 48);
            expectEquals(style->pick(55.0f, 1.0f, 0)->root, 60);
            expectEquals(style->pick(100.0f, 1.0f, 0)->root, 72);
            // Velocity picks the layer where the note has more than one.
            expectEquals(style->pick(60.0f, 0.2f, 0)->layer, 0);
            expectEquals(style->pick(60.0f, 0.9f, 0)->layer, 1);
            expectEquals(style->pick(48.0f, 0.9f, 0)->layer, 0);
            // Takes come round in turn.
            expectEquals(style->pick(72.0f, 0.5f, 0)->take, 0);
            expectEquals(style->pick(72.0f, 0.5f, 1)->take, 1);
            expectEquals(style->pick(72.0f, 0.5f, 2)->take, 0);
            // A mono file fills both channels.
            const auto* zone = style->pick(60.0f, 0.9f, 0);
            expectEquals(zone->data[2 * 100], zone->data[2 * 100 + 1]);
            expectWithinAbsoluteError(zone->data[2 * 42], 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 261.6256f * 42.0f / 44100.0f), 1.0e-3f);

            expect(SampleLibrary::readStyle(libraryFolder.getChildFile("no/such/style")) == nullptr);
        }

        beginTest("a_flac_file_is_read_bit_for_bit_and_takes_the_styles_gain");
        {
            // tests/data/flac-style: 0.1 s of a 440 Hz sine at 1/8 of full scale, as FLAC; the manifest's gain is 2.
            const auto folder = juce::File(COOLSYNTH_SOURCE_DIR).getChildFile("tests/data/flac-style");
            const auto style = SampleLibrary::readStyle(folder);
            expect(style != nullptr, "tests/data/flac-style is part of the repository");
            if (style != nullptr && ! style->zones.empty())
            {
                const auto& zone = style->zones.front();
                expectEquals(zone.frames, 4410);
                expectWithinAbsoluteError(zone.rate, 44100.0, 0.01);
                float peak = 0.0f;
                int crossings = 0;
                for (int i = 1; i < zone.frames; ++i)
                {
                    peak = std::max(peak, std::abs(zone.data[2 * i]));
                    crossings += zone.data[2 * (i - 1)] < 0.0f && zone.data[2 * i] >= 0.0f ? 1 : 0;
                }
                expectWithinAbsoluteError(peak, 0.25f, 0.005f);
                expectWithinAbsoluteError(crossings, 44, 1);
            }
        }

        beginTest("the_library_loads_in_the_background_and_knows_what_is_missing");
        {
            const int harp = catalogue.find("Folk Harp");
            expect(library.installed(harp, 0));
            expect(! library.installed(harp, 3), "the folk harp has one style");
            (void) library.request(harp, 0); // the asking starts the load; it may or may not be there yet
            expect(library.loadNow(harp, 0) != nullptr);
            expect(library.request(harp, 0) != nullptr, "once loaded it is there at once");
            expect(library.loadNow(harp, 3, 2000) == nullptr);
            expect(library.request(-1, 0) == nullptr && library.request(0, 9) == nullptr);
            expectGreaterThan(static_cast<int>(library.bytesInMemory()), 0);
        }

        beginTest("a_note_plays_its_recording_at_the_speed_that_gives_its_pitch");
        {
            expect(library.loadNow(marimba, 0) != nullptr);
            for (const auto& [note, hz] : { std::pair<int, double> { 60, 261.626 }, { 62, 293.665 }, { 58, 233.082 }, { 84, 1046.502 }, { 36, 65.406 } })
            {
                const auto out = play(marimba, 0, note, 0.9f, 19200);
                // The recording is a quarter second long at its own pitch; measure inside what a note raised an octave still has.
                expectWithinAbsoluteError(zeroCrossingHz(out.left, 960, note >= 84 ? 9600 : 17000), hz, hz * 0.002, "note " + juce::String(note));
            }
            // Velocity picks the louder recording: twice the level at middle C.
            const auto soft = play(marimba, 0, 60, 0.2f, 9600), loud = play(marimba, 0, 60, 0.9f, 9600);
            expectWithinAbsoluteError(rms(loud.left, 960, 9600) / rms(soft.left, 960, 9600), 2.0, 0.05);
            // Centre is the recording as it is on both sides; the balance takes one side away.
            expectWithinAbsoluteError(rms(loud.left, 960, 9600), 0.5 / std::sqrt(2.0), 0.01);
            expectWithinAbsoluteError(rms(loud.right, 960, 9600), rms(loud.left, 960, 9600), 1.0e-6);
            auto rig = makeRig();
            rig->generator.noteOn(NoteContext {});
            auto p = instrumentParams(marimba, 0, 60);
            p.pan = -1.0f;
            const auto leftOnly = render(rig->generator, p, 9600);
            expectGreaterThan(rms(leftOnly.left, 960, 9600), 0.1);
            expectEquals(rms(leftOnly.right, 960, 9600), 0.0);
        }

        beginTest("a_sustaining_style_goes_on_past_its_recording_and_a_struck_one_ends");
        {
            expect(library.loadNow(cellos, 0) != nullptr);
            // The stand-in recordings last 0.25 s: 24000 output samples at their own pitch.
            const auto held = play(cellos, 0, 60, 0.9f, 96000);
            expectGreaterThan(rms(held.left, 72000, 96000), 0.2, "still sounding after four times its length");
            // The loop's join is no bigger a step than the tone's own.
            expectLessThan(largestStep(held.left, 9600, 96000), 1.25f * largestStep(held.left, 960, 9600));

            const auto unlooped = play(cellos, 0, 60, 0.9f, 96000, false);
            expectEquals(rms(unlooped.left, 30000, 96000), 0.0);
            const auto struck = play(marimba, 0, 60, 0.9f, 96000); // Loop is on, but a struck style has no loop
            expectEquals(rms(struck.left, 30000, 96000), 0.0);
            expectGreaterThan(rms(struck.left, 960, 20000), 0.2);
        }

        beginTest("a_note_asked_before_its_instrument_is_in_memory_is_silent_and_the_next_one_sounds");
        {
            const int flute = catalogue.find("Flute");
            library.setRoot(libraryFolder); // forgets everything that was loaded
            auto rig = makeRig();
            rig->generator.noteOn(NoteContext {});
            // The first block may or may not find it; either way the note it started on stays whole or silent, never half.
            const auto first = render(rig->generator, instrumentParams(flute, 0, 60), 4800);
            expect(library.loadNow(flute, 0) != nullptr);
            rig->generator.strike();
            const auto second = render(rig->generator, instrumentParams(flute, 0, 60), 4800);
            expectGreaterThan(rms(second.left, 480, 4800), 0.2);
            const bool wasThere = rms(first.left, 480, 4800) > 0.2;
            expect(wasThere || rms(first.left, 0, 4800) == 0.0);
        }

        beginTest("a_bank_preset_names_its_instrument_its_style_and_its_family");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"(
[Low Strings]
room = Conservatory
family = Strings
g1 = Sample sample=cellos style=tremolo loop=on level=0.9
g2 = Sample sample=renaissance-organ style=4 level=0.3
g3 = Sample sample=glass
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            if (parsed.presets.size() == 1)
            {
                const auto& preset = parsed.presets.front();
                expect(preset.category == "Conservatory" && preset.section == "Strings");
                expect(entryText(preset, "pA.g1.p1") == "cellos");
                expect(entryText(preset, "pA.g1.p8") == "2", entryText(preset, "pA.g1.p8"));
                expect(entryText(preset, "pA.g2.p8") == "2", "the organ's 4' stop is its second style");
                expect(entryText(preset, "pA.g3.p1") == "glass");
            }
            const auto wrong = coolsynth::presets::parseBankFile("[X]\ng1 = Sample sample=cellos style=yodel\nb.family = Strings\n", "bad.csbank");
            expectEquals(wrong.errors.size(), 2);
        }

        beginTest("the_conservatory_has_a_preset_for_every_instrument_in_its_family");
        {
            std::set<juce::String> found;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(i);
                if (preset.category != "Conservatory")
                    continue;
                const int instrument = catalogue.find(preset.name.toStdString());
                expect(instrument >= 0, preset.name);
                if (instrument >= 0)
                    expectEquals(preset.section, juce::String(catalogue.at(instrument).family), preset.name);
                found.insert(preset.name);
            }
            expectEquals(static_cast<int>(found.size()), catalogue.count());
        }
    }
};

static V3InstrumentTests v3InstrumentTests;

/** Its own class and a heap processor: the test runner's stack frame is small in Debug. */
class V3InstrumentProcessorTests final : public juce::UnitTest
{
public:
    V3InstrumentProcessorTests() : juce::UnitTest("V3 sampled instruments in the processor", "V3") {}

    void runTest() override
    {
        beginTest("a_conservatory_preset_loads_its_instrument_and_plays");
        {
            coolsynth::testing::installTestSampleLibrary();
            int index = -1;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).category == "Conservatory" && coolsynth::presets::getFactoryPreset(i).name == "Cellos")
                    index = i;
            expect(index >= 0);
            if (index < 0)
                return;

            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 256);
            processor->setCurrentProgram(index);
            expect(processor->getProgramName(index).contains("Strings"), processor->getProgramName(index));
            expect(processor->loadSampledInstrumentsNow());

            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            float peak = 0.0f;
            for (int block = 0; block < 200; ++block) // a second: four times the stand-in recording, held by its loop
            {
                processor->processBlock(buffer, midi);
                midi.clear();
                if (block > 150)
                    peak = std::max(peak, buffer.getMagnitude(0, 256));
            }
            expectGreaterThan(peak, 0.01f);

            // The style is a switch inside the preset: the second style of the cellos is there too.
            auto* style = processor->getValueTreeState().getParameter("pA.g1.p8");
            expect(style != nullptr);
            if (style != nullptr)
            {
                style->setValueNotifyingHost(style->getValueForText("2"));
                expect(processor->loadSampledInstrumentsNow());
                expectEquals(style->getCurrentValueAsText(), juce::String("2"));
            }
        }
    }
};

static V3InstrumentProcessorTests v3InstrumentProcessorTests;
