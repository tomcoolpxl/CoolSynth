#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "parameters/v3/SlotParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"

namespace
{
    using coolsynth::presets::BankPreset;
    using coolsynth::presets::parseBankFile;

    juce::String entryText(const BankPreset& preset, const juce::String& id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return entry.text;
        return "<missing>";
    }

    float rawValue(SynthAudioProcessor& processor, const juce::String& id)
    {
        return processor.getValueTreeState().getRawParameterValue(id)->load();
    }
}

class V3BankTests final : public juce::UnitTest
{
public:
    V3BankTests() : juce::UnitTest("V3 bank files", "V3") {}

    void runTest() override
    {
        beginTest("parse_maps_slots_envelopes_and_effects_to_parameter_ids");
        {
            const auto parsed = parseBankFile(R"(
# a comment line
[Reese]
room = Main Stage
g1 = VA shape=saw oct=-1 fine=+6 level=0.8   # trailing comment
p1 = Ladder cutoff=800 res=0.2 mode=lp12
env1 = attack=5 decay=200 sustain=1 release=120 loop=off
voice = env2ToP1=0.1 glide=15
play = mode=legato priority=low
arp = on rate=1/16 pattern=up/down
drive = on amount=0.15 mix=0.8
master = -6
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            expectEquals(static_cast<int>(parsed.presets.size()), 1);
            const auto& preset = parsed.presets.front();
            expect(preset.name == "Reese");
            expect(preset.category == "Main Stage");
            expect(preset.sourceFile == "test.csbank");
            expect(entryText(preset, "pA.g1.type") == "VA");
            expect(entryText(preset, "pA.g1.p1") == "saw");     // shape is the first VA parameter
            expect(entryText(preset, "pA.g1.oct") == "-1");
            expect(entryText(preset, "pA.g1.fine") == "+6");
            expect(entryText(preset, "pA.p1.type") == "Ladder");
            expect(entryText(preset, "pA.p1.p1") == "800");
            expect(entryText(preset, "pA.p1.p5") == "lp12");    // mode is the fifth ladder parameter
            expect(entryText(preset, "pA.env1.attack") == "5");
            expect(entryText(preset, "pA.env1.loop") == "off");
            expect(entryText(preset, "pA.voice.glide") == "15");
            expect(entryText(preset, "pA.play.priority") == "low");
            expect(entryText(preset, "arpEnabled") == "on");
            expect(entryText(preset, "arpPattern") == "up/down");
            expect(entryText(preset, "driveAmount") == "0.15");
            expect(entryText(preset, "masterGainDb") == "-6");
            expect(preset.entries.front().parameterId == "pA.g1.type"); // the type comes before its parameters
        }

        beginTest("parse_reports_unknown_settings_with_file_and_line");
        {
            const auto parsed = parseBankFile(R"(room = Nowhere
[Broken]
g1 = Wavetable position=0.5
g2 = VA wobble=3
p1 = Ladder cutoff
envelope = attack=5
delay = maybe time=300
)", "broken.csbank");
            for (const auto& e : parsed.errors)
                logMessage(e);
            expectEquals(parsed.errors.size(), 6);
            expect(parsed.errors[0].startsWith("broken.csbank:1:"));
            expect(parsed.errors[1].contains(":3:") && parsed.errors[1].contains("Wavetable"));
            expect(parsed.errors[2].contains(":4:") && parsed.errors[2].contains("wobble"));
            expect(parsed.errors[3].contains(":5:"));
            expect(parsed.errors[4].contains(":6:") && parsed.errors[4].contains("envelope"));
            expect(parsed.errors[5].contains(":7:") && parsed.errors[5].contains("maybe"));
        }

        beginTest("apply_loads_type_defaults_and_reads_values_through_the_parameters");
        {
            const auto parsed = parseBankFile(R"([Layered]
room = Workshop
g2 = VA level=0.5
p1 = Ladder mode=hp12 cutoff=140 res=0.25
play = mode=legato voices=6
arp = on rate=1/8T
master = -7.5
)", "layered.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));

            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            expect(problems.isEmpty(), problems.joinIntoString("\n"));

            expectEquals(static_cast<int>(rawValue(*processor, "pA.g2.type")), 1);              // VA
            expectWithinAbsoluteError(rawValue(*processor, "pA.g2.p1"), 2.0f / 3.0f, 1.0e-5f);  // VA's default shape: saw, not the empty slot's 0
            expectWithinAbsoluteError(rawValue(*processor, "pA.g2.level"), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p2"), 0.25f, 1.0e-6f);        // res=0.25: a bare fraction, not a percent
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p5"), 1.0f, 1.0e-6f);          // HP12 is the last mode
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p1"), std::log(140.0f / 20.0f) / std::log(1000.0f), 1.0e-4f);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.mode")), 2);            // Legato
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.voices")), 6);
            expectEquals(static_cast<int>(rawValue(*processor, "arpEnabled")), 1);
            expectEquals(static_cast<int>(rawValue(*processor, "arpRateDivision")), 2);         // 1/8T
            expectWithinAbsoluteError(rawValue(*processor, "masterGainDb"), -7.5f, 1.0e-3f);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.g1.type")), 1);              // untouched: back to its default
        }

        beginTest("apply_reports_values_it_cannot_read");
        {
            const auto parsed = parseBankFile(R"([Wrong]
g1 = VA shape=wobble
p1 = Ladder cutoff=50000
play = mode=sideways
env1 = loop=maybe attack=abc
)", "wrong.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            for (const auto& p : problems)
                logMessage(p);
            expectEquals(problems.size(), 5);
        }

        beginTest("the_bank_is_the_processors_program_list");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            expectEquals(processor->getNumPrograms(), coolsynth::presets::getFactoryPresetCount());
            int moogBass = -1;
            for (int i = 0; i < processor->getNumPrograms(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == "Moog Bass")
                    moogBass = i;
            expect(moogBass >= 0);
            expect(processor->getProgramName(moogBass) == "Studio 1982: Moog Bass");
            processor->setCurrentProgram(moogBass);
            expectEquals(processor->getCurrentProgram(), moogBass);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.mode")), 2); // legato
            expect(processor->getPatchName() == "Moog Bass");
        }

        beginTest("audition_editor_builds_and_lays_out");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                expect(editor != nullptr);
                expect(editor->getWidth() >= 560 && editor->getHeight() > 100);
                editor->setSize(900, 700);
                expect(editor->getNumChildComponents() >= 2); // the preset menu, the sliders, and the resize corner
            }
        }

        beginTest("every_factory_preset_sounds_and_stays_bounded");
        {
            for (int index = 0; index < coolsynth::presets::getFactoryPresetCount(); ++index)
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(index);
                auto processor = std::make_unique<SynthAudioProcessor>();
                processor->prepareToPlay(48000.0, 256);
                coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), preset);

                juce::AudioBuffer<float> buffer(2, 256);
                double sum = 0.0;
                float peak = 0.0f;
                bool finite = true;
                for (int block = 0; block < 188; ++block) // about one second
                {
                    juce::MidiBuffer midi;
                    if (block == 0)
                        for (int note : { 48, 55, 60 })
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
                    buffer.clear();
                    processor->processBlock(buffer, midi);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        for (int s = 0; s < 256; ++s)
                        {
                            const float v = buffer.getSample(ch, s);
                            finite = finite && std::isfinite(v);
                            peak = std::max(peak, std::abs(v));
                            sum += static_cast<double>(v) * static_cast<double>(v);
                        }
                    }
                }
                const float rms = static_cast<float>(std::sqrt(sum / (188.0 * 256.0 * 2.0)));
                expect(finite, preset.name + " produced a non-finite sample");
                expect(rms > 1.0e-3f, preset.name + " is silent: rms " + juce::String(rms, 6));
                expect(peak <= 1.0f, preset.name + " peaks at " + juce::String(peak, 3));
            }
        }
    }
};

static V3BankTests v3BankTests;

/**
    Plays random phrases on every non-arp factory preset, switching presets between phrases the
    way someone auditioning does, and after every block checks what must hold: each pressed key
    in poly mode has a voice playing it, the mono, legato and unison stacks play the key with
    priority, and once every key is up and the releases are over, no voice is left running.
*/
class V3PerformanceFuzz final : public juce::UnitTest
{
public:
    V3PerformanceFuzz() : juce::UnitTest("V3 performance fuzz", "V3") {}

    void runTest() override
    {
        beginTest("random_playing_across_presets_always_sounds_the_right_notes");

        auto processor = std::make_unique<SynthAudioProcessor>();
        processor->prepareToPlay(48000.0, 256);
        auto& part = processor->getPartForTesting();
        juce::Random random(20261113);
        juce::AudioBuffer<float> buffer(2, 256);
        int failures = 0;
        int checkedNotes = 0;

        std::vector<int> candidates;
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
        {
            bool arp = false;
            for (const auto& e : coolsynth::presets::getFactoryPreset(i).entries)
                arp = arp || (e.parameterId == "arpEnabled" && e.text == "on");
            if (! arp)
                candidates.push_back(i);
        }

        auto render = [&](juce::MidiBuffer& midi)
        {
            buffer.clear();
            processor->processBlock(buffer, midi);
        };

        for (int phrase = 0; phrase < 16 && failures < 10; ++phrase)
        {
            const int presetIndex = candidates[static_cast<std::size_t>(random.nextInt(static_cast<int>(candidates.size())))];
            processor->setCurrentProgram(presetIndex);
            const auto& preset = coolsynth::presets::getFactoryPreset(presetIndex);
            const int mode = static_cast<int>(processor->getValueTreeState().getRawParameterValue("pA.play.mode")->load());
            const int priority = static_cast<int>(processor->getValueTreeState().getRawParameterValue("pA.play.priority")->load());
            const int polyphony = static_cast<int>(processor->getValueTreeState().getRawParameterValue("pA.play.voices")->load());

            std::vector<std::pair<int, std::uint64_t>> held; // note, order pressed
            std::uint64_t order = 0;

            for (int step = 0; step < 30 && failures < 10; ++step)
            {
                juce::MidiBuffer midi;
                const bool press = held.empty() || (held.size() < 6 && random.nextFloat() < 0.55f);
                if (press)
                {
                    int note = 0;
                    do { note = 36 + random.nextInt(48); }
                    while (std::any_of(held.begin(), held.end(), [note](const auto& h) { return h.first == note; }));
                    held.emplace_back(note, ++order);
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(40 + random.nextInt(87))), random.nextInt(256));
                }
                else
                {
                    const auto victim = static_cast<std::size_t>(random.nextInt(static_cast<int>(held.size())));
                    midi.addEvent(juce::MidiMessage::noteOff(1, held[victim].first), random.nextInt(256));
                    held.erase(held.begin() + static_cast<std::ptrdiff_t>(victim));
                }
                render(midi);
                juce::MidiBuffer none;
                for (int b = 0, gap = 1 + random.nextInt(16); b < gap; ++b)
                    render(none); // up to 85 ms between key events

                if (held.empty())
                    continue;

                std::vector<int> soundingKeys;
                for (int v = 0; v < coolsynth::synth::v3::Part::maxVoices; ++v)
                    if (part.voiceForTesting(v).isActive() && part.voiceKeyDownForTesting(v))
                        soundingKeys.push_back(part.voiceForTesting(v).currentNote());

                auto describe = [&]
                {
                    juce::String keys, sounding;
                    for (const auto& h : held) keys << h.first << " ";
                    for (int n : soundingKeys) sounding << n << " ";
                    return preset.name + ", step " + juce::String(step) + ": keys down " + keys + "| voices playing " + sounding;
                };

                if (mode == 0)
                {
                    // Poly: the newest keys up to the polyphony limit must all be playing.
                    std::vector<std::pair<int, std::uint64_t>> newest(held);
                    std::sort(newest.begin(), newest.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                    for (std::size_t k = 0; k < newest.size() && static_cast<int>(k) < polyphony; ++k)
                    {
                        ++checkedNotes;
                        if (std::find(soundingKeys.begin(), soundingKeys.end(), newest[k].first) == soundingKeys.end())
                        {
                            ++failures;
                            expect(false, "key " + juce::String(newest[k].first) + " is down but silent. " + describe());
                        }
                    }
                }
                else
                {
                    int expected = held.front().first;
                    for (const auto& h : held)
                    {
                        if (priority == 0 && h.second > std::find_if(held.begin(), held.end(), [expected](const auto& x) { return x.first == expected; })->second)
                            expected = h.first;
                        if (priority == 1 && h.first < expected)
                            expected = h.first;
                        if (priority == 2 && h.first > expected)
                            expected = h.first;
                    }
                    ++checkedNotes;
                    const bool playing = ! soundingKeys.empty()
                        && std::all_of(soundingKeys.begin(), soundingKeys.end(), [expected](int n) { return n == expected; });
                    if (! playing)
                    {
                        ++failures;
                        expect(false, "expected the stack to play " + juce::String(expected) + ". " + describe());
                    }
                }
            }

            // Every key up: after the releases nothing may still be running.
            juce::MidiBuffer releaseAll;
            for (const auto& h : held)
                releaseAll.addEvent(juce::MidiMessage::noteOff(1, h.first), 0);
            render(releaseAll);
            juce::MidiBuffer none;
            for (int b = 0; b < 200; ++b) // about a second, longer than any release in the bank
                render(none);
            if (part.activeVoiceCountForTesting() != 0)
            {
                ++failures;
                expect(false, preset.name + ": " + juce::String(part.activeVoiceCountForTesting()) + " voices still running a second after the last key went up");
            }
        }

        logMessage("checked " + juce::String(checkedNotes) + " key states, " + juce::String(failures) + " failures");
        expect(checkedNotes > 500);
    }
};

static V3PerformanceFuzz v3PerformanceFuzz;

/**
    Opt-in (set COOLSYNTH_CPU_REPORT): renders every factory preset for two seconds with an
    eight-note chord and prints how long that took against real time. Above 100 % the audio
    device misses deadlines and the output drops out.
*/
class V3CpuReport final : public juce::UnitTest
{
public:
    V3CpuReport() : juce::UnitTest("V3 CPU report", "V3") {}

    void runTest() override
    {
        beginTest("cpu_load_per_preset");
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CPU_REPORT", {}).isEmpty())
        {
            logMessage("skipped: set COOLSYNTH_CPU_REPORT=1 to measure");
            return;
        }

        constexpr double sampleRate = 48000.0;
        constexpr int block = 256;
        constexpr int blocks = static_cast<int>(2.0 * sampleRate) / block;
        for (int index = 0; index < coolsynth::presets::getFactoryPresetCount(); ++index)
        {
            const auto& preset = coolsynth::presets::getFactoryPreset(index);
            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(sampleRate, block);
            coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), preset);

            juce::AudioBuffer<float> buffer(2, block);
            double worstBlock = 0.0;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int note : { 48, 52, 55, 59, 60, 64, 67, 72 })
                        midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
                buffer.clear();
                const auto blockStart = juce::Time::getHighResolutionTicks();
                processor->processBlock(buffer, midi);
                worstBlock = std::max(worstBlock, juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - blockStart));
            }
            const double seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            const double blockSeconds = block / sampleRate;
            logMessage(preset.name.paddedRight(' ', 16) + " average " + juce::String(100.0 * seconds / 2.0, 1)
                       + " % of real time, worst block " + juce::String(100.0 * worstBlock / blockSeconds, 1) + " %");
        }
    }
};

static V3CpuReport v3CpuReport;
