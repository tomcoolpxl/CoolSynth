#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "parameters/v3/PartParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/EngineV3.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    EngineMidiEvent event(Type type, int note = 0, float value = 1.0f, int offset = 0)
    {
        EngineMidiEvent e;
        e.type = type;
        e.sampleOffset = offset;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }

    EngineMidiEvent on(int note, float velocity = 1.0f) { return event(Type::noteOn, note, velocity); }
    EngineMidiEvent off(int note) { return event(Type::noteOff, note, 0.0f); }

    PartSettings sinePatch()
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        const auto& d = VaGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        g.p[VaGenerator::shape] = unitsToNormalised(d.params[VaGenerator::shape], static_cast<float>(VaGenerator::Wave::sine));
        g.level = 1.0f;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    EngineSettings twoSines(PartMode mode)
    {
        EngineSettings s;
        s.parts = { sinePatch(), sinePatch() };
        s.partMode = mode;
        return s;
    }

    std::unique_ptr<EngineV3> makeEngine()
    {
        auto engine = std::make_unique<EngineV3>();
        engine->prepare(kRate, kBlock, 2);
        return engine;
    }

    struct Rendered
    {
        std::vector<float> left, right;
    };

    /** Renders `samples`, with `events` in the first block. */
    Rendered render(EngineV3& engine, const EngineSettings& settings, int samples, std::vector<EngineMidiEvent> events = {})
    {
        Rendered out;
        juce::AudioBuffer<float> buffer(2, kBlock);
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, n);
            engine.render(block, start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>(), settings);
            out.left.insert(out.left.end(), block.getReadPointer(0), block.getReadPointer(0) + n);
            out.right.insert(out.right.end(), block.getReadPointer(1), block.getReadPointer(1) + n);
        }
        return out;
    }

    /** The notes of the voices a part holds with a key down. */
    std::set<int> heldNotes(EngineV3& engine, int part)
    {
        std::set<int> notes;
        const auto& p = engine.partForTesting(part);
        for (int i = 0; i < Part::maxVoices; ++i)
            if (p.voiceForTesting(i).isActive() && p.voiceKeyDownForTesting(i))
                notes.insert(p.voiceForTesting(i).currentNote());
        return notes;
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double frequencyOf(const std::vector<float>& x, std::size_t from)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 3)
            return 0.0;
        return kRate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
    }

    juce::String entryText(const coolsynth::presets::BankPreset& preset, const juce::String& id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return entry.text;
        return "<none>";
    }
}

class V3PartsTests final : public juce::UnitTest
{
public:
    V3PartsTests() : juce::UnitTest("V3 parts", "V3") {}

    void runTest() override
    {
        beginTest("the_part_mode_says_which_parts_the_keys_reach");
        {
            for (const auto mode : { PartMode::aOnly, PartMode::layer, PartMode::bOnly })
            {
                auto engine = makeEngine();
                const auto settings = twoSines(mode);
                render(*engine, settings, 2400, { on(60), on(64) });
                const std::set<int> both { 60, 64 }, none;
                expect(heldNotes(*engine, 0) == (mode == PartMode::bOnly ? none : both), "part A");
                expect(heldNotes(*engine, 1) == (mode == PartMode::aOnly ? none : both), "part B");
                render(*engine, settings, 2400, { off(60), off(64) });
                expect(heldNotes(*engine, 0).empty() && heldNotes(*engine, 1).empty());
            }
        }

        beginTest("split_sends_each_key_to_the_parts_whose_range_holds_it");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::split);
            settings.mix[0].keyLow = 0;
            settings.mix[0].keyHigh = 64;
            settings.mix[1].keyLow = 60;
            settings.mix[1].keyHigh = 127;
            render(*engine, settings, 2400, { on(48), on(62), on(72) });
            expect(heldNotes(*engine, 0) == std::set<int> { 48, 62 });
            expect(heldNotes(*engine, 1) == std::set<int> { 62, 72 });

            // Layer plays every key on both, whatever the key ranges say.
            auto layered = makeEngine();
            settings.partMode = PartMode::layer;
            render(*layered, settings, 2400, { on(48), on(72) });
            expect(heldNotes(*layered, 0) == std::set<int> { 48, 72 });
            expect(heldNotes(*layered, 1) == std::set<int> { 48, 72 });
        }

        beginTest("velocity_ranges_choose_the_part_when_both_play");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::layer);
            settings.mix[0].velocityHigh = 80;
            settings.mix[1].velocityLow = 81;
            render(*engine, settings, 2400, { on(60, 0.5f), on(72, 1.0f) });
            expect(heldNotes(*engine, 0) == std::set<int> { 60 });
            expect(heldNotes(*engine, 1) == std::set<int> { 72 });

            // One part alone plays everything.
            auto alone = makeEngine();
            settings.partMode = PartMode::aOnly;
            render(*alone, settings, 2400, { on(60, 0.5f), on(72, 1.0f) });
            expect(heldNotes(*alone, 0) == std::set<int> { 60, 72 });
        }

        beginTest("transpose_moves_the_keys_a_part_is_sent_and_a_release_still_finds_its_note");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::layer);
            settings.mix[1].transpose = -12;
            render(*engine, settings, 2400, { on(60), on(5) });
            expect(heldNotes(*engine, 0) == std::set<int> { 60, 5 });
            expect(heldNotes(*engine, 1) == std::set<int> { 48 }, "key 5 an octave down is below the keyboard");

            settings.mix[1].transpose = 7; // changed while the key is down
            render(*engine, settings, 2400, { off(60), off(5) });
            expect(heldNotes(*engine, 0).empty());
            expect(heldNotes(*engine, 1).empty());
        }

        beginTest("fine_detunes_the_whole_part");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::bOnly);
            settings.mix[1].fineCents = 50.0f;
            const auto out = render(*engine, settings, 48000, { on(69) });
            expectWithinAbsoluteError(frequencyOf(out.left, 4800), 440.0 * std::exp2(50.0 / 1200.0), 0.2);
        }

        beginTest("a_part_the_mode_switches_off_lets_go_and_does_not_come_back_on_held_keys");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::layer);
            render(*engine, settings, 2400, { on(60) });
            expect(heldNotes(*engine, 1) == std::set<int> { 60 });

            settings.partMode = PartMode::aOnly;
            render(*engine, settings, 4800);
            expect(heldNotes(*engine, 0) == std::set<int> { 60 });
            expect(heldNotes(*engine, 1).empty());
            expect(! engine->partForTesting(1).isSounding(), "part B rang out");

            settings.partMode = PartMode::layer;
            render(*engine, settings, 2400);
            expect(heldNotes(*engine, 1).empty(), "a key held across the change does not start part B");
            render(*engine, settings, 2400, { on(67) });
            expect(heldNotes(*engine, 1) == std::set<int> { 67 });
            render(*engine, settings, 2400, { off(60), off(67) });
            expect(heldNotes(*engine, 0).empty() && heldNotes(*engine, 1).empty());
        }

        beginTest("both_parts_share_sixteen_voices_and_each_is_sure_of_eight");
        {
            using Limits = std::array<int, partCount>;
            expect(EngineV3::voiceLimits({ 16, 16 }) == Limits { 8, 8 });
            expect(EngineV3::voiceLimits({ 16, 1 }) == Limits { 15, 1 });
            expect(EngineV3::voiceLimits({ 4, 16 }) == Limits { 4, 12 });
            expect(EngineV3::voiceLimits({ 12, 10 }) == Limits { 8, 8 });
            expect(EngineV3::voiceLimits({ 3, 5 }) == Limits { 3, 5 });

            PartSettings patch;
            patch.polyphony = 12;
            expectEquals(EngineV3::voicesWanted(patch), 12);
            patch.playMode = PlayMode::mono;
            expectEquals(EngineV3::voicesWanted(patch), 1);
            patch.playMode = PlayMode::legato;
            expectEquals(EngineV3::voicesWanted(patch), 1);
            patch.playMode = PlayMode::unison;
            patch.unisonVoices = 6;
            expectEquals(EngineV3::voicesWanted(patch), 6);

            auto count = [](EngineV3& engine, int part)
            {
                int active = 0;
                for (int i = 0; i < Part::maxVoices; ++i)
                    active += engine.partForTesting(part).voiceForTesting(i).isActive() ? 1 : 0;
                return active;
            };
            std::vector<EngineMidiEvent> twelve;
            for (int i = 0; i < 12; ++i)
                twelve.push_back(on(48 + i));

            auto layered = makeEngine();
            render(*layered, twoSines(PartMode::layer), 9600, twelve);
            expectEquals(count(*layered, 0), 8);
            expectEquals(count(*layered, 1), 8);

            // A mono part under a poly one leaves the poly part fifteen voices.
            auto under = makeEngine();
            auto settings = twoSines(PartMode::layer);
            settings.parts[1].playMode = PlayMode::mono;
            render(*under, settings, 9600, twelve);
            expectEquals(count(*under, 0), 12);
            expectEquals(count(*under, 1), 1);

            auto alone = makeEngine();
            render(*alone, twoSines(PartMode::aOnly), 9600, twelve);
            expectEquals(count(*alone, 0), 12);
            expectEquals(count(*alone, 1), 0);
        }

        beginTest("level_and_pan_place_a_part_in_the_mix");
        {
            auto level = [](float db, float pan)
            {
                auto engine = makeEngine();
                auto settings = twoSines(PartMode::bOnly);
                settings.mix[1].levelDb = db;
                settings.mix[1].pan = pan;
                const auto out = render(*engine, settings, 9600, { on(69, 0.5f) });
                return std::pair<double, double> { rms(out.left, 4800, 9600), rms(out.right, 4800, 9600) };
            };
            const auto unity = level(0.0f, 0.0f);
            expectGreaterThan(unity.first, 0.01);
            expectWithinAbsoluteError(level(-6.0f, 0.0f).first / unity.first, 0.501, 0.005);
            expectEquals(level(-60.0f, 0.0f).first, 0.0);
            const auto right = level(0.0f, 1.0f);
            expectEquals(right.first, 0.0);
            expectWithinAbsoluteError(right.second / unity.second, 1.0, 0.001);
            const auto halfLeft = level(0.0f, -0.5f);
            expectWithinAbsoluteError(halfLeft.first / unity.first, 1.0, 0.001);
            expectWithinAbsoluteError(halfLeft.second / unity.second, 0.5, 0.005);

            // A level change moves across the block instead of stepping.
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::aOnly);
            render(*engine, settings, 9600, { on(69, 0.5f) });
            settings.mix[0].levelDb = -60.0f;
            const auto fading = render(*engine, settings, kBlock);
            expectGreaterThan(rms(fading.left, 0, 64), 4.0 * rms(fading.left, 192, 256));
            expectGreaterThan(rms(fading.left, 192, 256), 0.0);
        }

        beginTest("the_effects_share_keeps_a_part_out_of_the_master_effects");
        {
            auto echo = [](float fx, bool delayOn)
            {
                auto engine = makeEngine();
                auto settings = twoSines(PartMode::aOnly);
                settings.mix[0].fx = fx;
                if (delayOn)
                    settings.master[2] = coolsynth::synth::v3::InsertSettings::of(coolsynth::synth::v3::insertType::delay,
                                                                                    { { 0, 100.0f }, { 1, 0.0f }, { 2, 0.5f } });
                auto out = render(*engine, settings, 4800, { on(69, 0.5f) });
                const auto tail = render(*engine, settings, 9600, { off(69) });
                out.left.insert(out.left.end(), tail.left.begin(), tail.left.end());
                return out.left;
            };
            const auto plain = echo(1.0f, false), through = echo(1.0f, true), past = echo(0.0f, true), half = echo(0.5f, true);
            // 80 to 180 ms after the key went up the note is over; only an echo can sound.
            const double echoOfThrough = rms(through, 4800 + 3840, 4800 + 8640);
            expectGreaterThan(echoOfThrough, 0.01);
            expectLessThan(rms(plain, 4800 + 3840, 4800 + 8640), 1.0e-4);
            expectLessThan(rms(past, 4800 + 3840, 4800 + 8640), 1.0e-4, "a part at 0 does not reach the delay");
            expectWithinAbsoluteError(rms(half, 4800 + 3840, 4800 + 8640) / echoOfThrough, 0.5, 0.02);

            // Past the master effects the part is what it is without effects.
            double difference = 0.0;
            for (std::size_t i = 0; i < plain.size(); ++i)
                difference = std::max(difference, static_cast<double>(std::abs(plain[i] - past[i])));
            expectLessThan(difference, 1.0e-5);
        }

        beginTest("the_arp_drives_only_the_parts_that_follow_it");
        {
            auto engine = makeEngine();
            auto settings = twoSines(PartMode::layer);
            settings.parts[0].arp.enabled = true;
            settings.parts[1].arp = settings.parts[0].arp;
            settings.mix[1].arp = false;
            render(*engine, settings, 9600, { on(60), on(64) });
            expectEquals(engine->partForTesting(0).arpHeldNoteCountForTesting(), 2);
            expectEquals(engine->partForTesting(1).arpHeldNoteCountForTesting(), 0);
            expect(heldNotes(*engine, 1) == std::set<int> { 60, 64 }, "part B holds the keys as played");
        }

        beginTest("a_bank_preset_writes_part_b_with_a_prefix");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"(
[Stack]
room = Main Stage
parts = layer
g1 = VA shape=saw level=0.7
mix = level=-2 fx=1
b.g1 = Wavetable table=classic pos=0.3 level=0.9
b.p1 = SVF mode=lp cutoff=900
b.env1 = attack=3 release=200
b.lfo1 = shape=sine rate=2
b.mod = src=lfo1 dst=g1.pos amount=0.2
b.play = mode=mono priority=low
b.chord = type=fifth
b.mix = level=-3 transpose=-12 fx=0 keyLow=0 keyHigh=59 arp=off
master = -3
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            expectEquals(static_cast<int>(parsed.presets.size()), 1);
            if (parsed.presets.size() == 1)
            {
                const auto& preset = parsed.presets.front();
                expect(entryText(preset, "part.mode") == "Layer");
                expect(entryText(preset, "pA.g1.type") == "VA");
                expect(entryText(preset, "pA.mix.level") == "-2");
                expect(entryText(preset, "pB.g1.type") == "Wavetable");
                expect(entryText(preset, "pB.p1.type") == "SVF");
                expect(entryText(preset, "pB.env1.attack") == "3");
                expect(entryText(preset, "pB.lfo1.rate") == "2");
                expect(entryText(preset, "pB.mod1.src") == "LFO 1");
                // The destination is named by part B's own generator: a wavetable's position.
                expect(entryText(preset, "pB.mod1.dst") != "<none>");
                expect(entryText(preset, "pA.mod1.dst") == "<none>");
                expect(entryText(preset, "pB.play.mode") == "mono");
                expect(entryText(preset, "pB.chord.type") == "fifth");
                expect(entryText(preset, "pB.mix.transpose") == "-12");
                expect(entryText(preset, "pB.mix.fx") == "0");
                expect(entryText(preset, "pB.mix.arp") == "off");
            }

            const auto wrong = coolsynth::presets::parseBankFile("[X]\nb.master = -3\nb.f1 = Reverb\nparts = both\nb.mix = wide=1\n", "bad.csbank");
            expectEquals(wrong.errors.size(), 4);
        }
    }
};

static V3PartsTests v3PartsTests;

/** Its own class and heap processors: the test runner's stack frame is small in Debug. */
class V3PartsProcessorTests final : public juce::UnitTest
{
public:
    V3PartsProcessorTests() : juce::UnitTest("V3 parts in the processor", "V3") {}

    void runTest() override
    {
        namespace pv3 = coolsynth::parameters::v3;

        beginTest("a_layered_bank_preset_plays_both_parts_and_survives_a_state_round_trip");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"(
[Under]
room = Workshop
parts = layer
g1 = VA shape=saw level=0.7
b.g1 = VA shape=sine level=0.9
b.play = mode=mono priority=low
b.mix = transpose=-12 fx=0 level=-4
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            if (parsed.presets.empty())
                return;

            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 256);
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            expect(problems.isEmpty(), problems.joinIntoString("\n"));

            auto plain = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };
            expectEquals(static_cast<int>(plain(*processor, pv3::partModeParameterId)), 1);
            expectEquals(plain(*processor, "pB.mix.transpose"), -12.0f);
            expectEquals(plain(*processor, "pB.mix.fx"), 0.0f);
            expectEquals(plain(*processor, "pB.mix.level"), -4.0f);
            expectEquals(static_cast<int>(plain(*processor, "pB.play.mode")), 1);

            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            processor->processBlock(buffer, midi);
            float peak = 0.0f;
            for (int block = 0; block < 20; ++block)
            {
                juce::MidiBuffer none;
                processor->processBlock(buffer, none);
                peak = std::max(peak, buffer.getMagnitude(0, 256));
            }
            expectGreaterThan(peak, 0.01f);

            juce::MemoryBlock state;
            processor->getStateInformation(state);
            auto restored = std::make_unique<SynthAudioProcessor>();
            restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            expectEquals(static_cast<int>(plain(*restored, pv3::partModeParameterId)), 1);
            expectEquals(plain(*restored, "pB.mix.transpose"), -12.0f);
            expectEquals(plain(*restored, "pB.mix.fx"), 0.0f);
            expectEquals(static_cast<int>(plain(*restored, "pB.play.mode")), 1);
            expectEquals(plain(*restored, "pB.g1.level"), plain(*processor, "pB.g1.level"));
        }

        beginTest("a_processor_starts_and_loads_in_reasonable_time");
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            processor->prepareToPlay(48000.0, 256);
            const auto t2 = juce::Time::getMillisecondCounterHiRes();
            coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), coolsynth::presets::getFactoryPreset(0));
            const auto t3 = juce::Time::getMillisecondCounterHiRes();
            juce::MemoryBlock state;
            processor->getStateInformation(state);
            const auto t4 = juce::Time::getMillisecondCounterHiRes();
            processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            const auto t5 = juce::Time::getMillisecondCounterHiRes();
            logMessage("construct " + juce::String(t1 - t0, 1) + " ms, prepare " + juce::String(t2 - t1, 1) + " ms, preset "
                       + juce::String(t3 - t2, 1) + " ms, save " + juce::String(t4 - t3, 1) + " ms, load " + juce::String(t5 - t4, 1) + " ms");
            expectLessThan(t5 - t0, 5000.0);
        }
    }
};

static V3PartsProcessorTests v3PartsProcessorTests;
