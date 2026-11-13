#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
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

    struct Timed
    {
        int sample = 0;
        Type type = Type::noteOn;
        int note = 69;
    };

    PartSettings sinePatch()
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        const auto& d = VaGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        g.p[VaGenerator::shape] = unitsToNormalised(d.params[VaGenerator::shape], static_cast<float>(VaGenerator::Wave::saw));
        g.level = 1.0f;
        Envelope::Parameters held;
        held.attack = 0.001f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.005f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    EngineSettings onePart(PartMode mode = PartMode::aOnly)
    {
        EngineSettings s;
        s.parts = { sinePatch(), sinePatch() };
        s.partMode = mode;
        return s;
    }

    /** An insert of a type at that type's defaults; `values` overrides parameters by index, in units. */
    InsertSettings insertOf(int type, std::initializer_list<std::pair<int, float>> values = {})
    {
        InsertSettings s;
        s.type = type;
        const auto& d = insertDescriptor(type);
        for (int i = 0; i < d.usedParams; ++i)
            s.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, units] : values)
            s.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], units);
        return s;
    }

    std::unique_ptr<EngineV3> makeEngine()
    {
        auto engine = std::make_unique<EngineV3>();
        engine->prepare(kRate, kBlock, 2);
        return engine;
    }

    /** Renders `samples`; events carry their sample position from the start. Returns the left channel. */
    std::vector<float> render(EngineV3& engine, const EngineSettings& settings, int samples, const std::vector<Timed>& events)
    {
        std::vector<float> left;
        juce::AudioBuffer<float> buffer(2, kBlock);
        std::size_t next = 0;
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            std::vector<EngineMidiEvent> now;
            while (next < events.size() && events[next].sample < start + n)
            {
                EngineMidiEvent e;
                e.type = events[next].type;
                e.sampleOffset = std::max(0, events[next].sample - start);
                e.noteNumber = static_cast<uint8_t>(events[next].note);
                e.value = events[next].type == Type::noteOn ? 0.7f : 0.0f;
                now.push_back(e);
                ++next;
            }
            juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, n);
            engine.render(block, now, settings);
            left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + n);
        }
        return left;
    }

    /** A note of 20 ms at the start. */
    std::vector<Timed> blip(int note = 69) { return { { 0, Type::noteOn, note }, { 960, Type::noteOff, note } }; }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double largestDifference(const std::vector<float>& a, const std::vector<float>& b)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
        return worst;
    }

    /** Where the loudest 10 ms after `from` begins. */
    int loudestAfter(const std::vector<float>& x, int from)
    {
        int best = from;
        double bestLevel = 0.0;
        for (int start = from; start + 480 < static_cast<int>(x.size()); start += 48)
        {
            const double level = rms(x, static_cast<std::size_t>(start), static_cast<std::size_t>(start + 480));
            if (level > bestLevel)
            {
                bestLevel = level;
                best = start;
            }
        }
        return best;
    }

    /** The power of one frequency in a stretch of signal (Goertzel). */
    double powerAt(const std::vector<float>& x, std::size_t from, std::size_t to, double hz)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
        const double coefficient = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
        {
            const double s0 = static_cast<double>(x[i]) + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    }

    juce::String entryText(const coolsynth::presets::BankPreset& preset, const juce::String& id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return entry.text;
        return "<none>";
    }
}

class V3InsertTests final : public juce::UnitTest
{
public:
    V3InsertTests() : juce::UnitTest("V3 insert slots", "V3") {}

    void runTest() override
    {
        beginTest("every_insert_type_has_a_descriptor_with_defaults_inside_its_range");
        {
            expectEquals(insertTypeTable().count, static_cast<int>(insertType::count));
            for (int type = 0; type < insertType::count; ++type)
            {
                const auto& d = insertDescriptor(type);
                expect(juce::String(d.name).isNotEmpty());
                expect(d.usedParams >= 0 && d.usedParams <= maxSlotParameters);
                for (int i = 0; i < d.usedParams; ++i)
                {
                    const auto& p = d.params[static_cast<std::size_t>(i)];
                    expect(p.max > p.min, juce::String(d.name) + " " + p.id);
                    expect(p.def >= p.min && p.def <= p.max, juce::String(d.name) + " " + p.id);
                }
            }
            expectEquals(juce::String(insertDescriptor(insertType::delay).name), juce::String("Delay"));
            expectEquals(juce::String(insertDescriptor(insertType::pump).name), juce::String("Pump"));
            expectEquals(juce::String(insertDescriptor(insertType::grain).name), juce::String("Grain"));
            expectEquals(static_cast<int>(insertType::grain), 11); // saved in patches: it stays the twelfth
        }

        beginTest("an_insert_sounds_exactly_like_the_same_effect_in_the_master_section");
        {
            const std::vector<Timed> notes { { 0, Type::noteOn, 57 }, { 0, Type::noteOn, 64 }, { 14400, Type::noteOff, 57 }, { 14400, Type::noteOff, 64 } };
            struct Case
            {
                const char* name;
                InsertSettings insert;
                void (*master)(EngineSettings&);
            };
            const std::vector<Case> cases {
                { "drive", insertOf(insertType::drive), [](EngineSettings& s) { s.drive.enabled = true; s.drive.amount = 0.3f; s.drive.mix = 1.0f; } },
                { "phaser", insertOf(insertType::phaser), [](EngineSettings& s) { s.phaser.enabled = true; s.phaser.rateHz = 0.5f; s.phaser.depth = 0.6f; } },
                { "flanger", insertOf(insertType::flanger),
                  [](EngineSettings& s) { s.flanger.enabled = true; s.flanger.rateHz = 0.3f; s.flanger.depth = 0.7f; s.flanger.feedback = 0.5f; s.flanger.mix = 0.5f; } },
                { "chorus", insertOf(insertType::chorus), [](EngineSettings& s) { s.chorus.enabled = true; s.chorus.rateHz = 0.6f; s.chorus.depth = 0.5f; s.chorus.mix = 0.5f; } },
                // The insert's time goes through its normalised value and comes back a millionth of a sample off:
                // the master gets exactly that time, so the two lines read at the same place.
                { "delay", insertOf(insertType::delay),
                  [](EngineSettings& s)
                  {
                      const auto& d = insertDescriptor(insertType::delay);
                      s.delay.enabled = true;
                      s.delay.timeMs = descriptorToUnits(d.params[0], unitsToNormalised(d.params[0], 250.0f));
                      s.delay.feedback = descriptorToUnits(d.params[1], unitsToNormalised(d.params[1], 0.3f));
                      s.delay.mix = descriptorToUnits(d.params[2], unitsToNormalised(d.params[2], 0.3f));
                  } },
                { "reverb", insertOf(insertType::reverb),
                  [](EngineSettings& s) { s.reverb.enabled = true; s.reverb.size = 0.4f; s.reverb.damping = 0.5f; s.reverb.mix = 0.3f; } },
                { "compressor", insertOf(insertType::compressor), [](EngineSettings& s) { s.compressor.enabled = true; s.compressor.amount = 0.3f; s.compressor.mix = 1.0f; } },
                { "eq", insertOf(insertType::eq, { { 0, 6.0f }, { 2, -6.0f } }),
                  [](EngineSettings& s) { s.eq.enabled = true; s.eq.lowDb = 6.0f; s.eq.tiltDb = 0.0f; s.eq.highDb = -6.0f; } },
                { "pump", insertOf(insertType::pump), [](EngineSettings& s) { s.pump.enabled = true; s.pump.depth = 0.6f; s.pump.division = 8; s.pump.release = 0.5f; } },
            };
            const auto plain = [&] { auto e = makeEngine(); return render(*e, onePart(), 28800, notes); }();
            for (const auto& c : cases)
            {
                auto asInsert = makeEngine();
                auto withInsert = onePart();
                withInsert.inserts[0][1] = c.insert; // the middle slot: its place among empty slots does not matter
                const auto a = render(*asInsert, withInsert, 28800, notes);

                auto asMaster = makeEngine();
                auto withMaster = onePart();
                c.master(withMaster);
                const auto b = render(*asMaster, withMaster, 28800, notes);

                expectLessThan(largestDifference(a, b), 1.0e-5, c.name);
                expectGreaterThan(largestDifference(a, plain), 1.0e-3, juce::String(c.name) + " does something");
            }
        }

        beginTest("inserts_belong_to_their_part");
        {
            auto settings = onePart();
            settings.inserts[0][0] = insertOf(insertType::delay, { { 0, 100.0f }, { 1, 0.0f }, { 2, 0.5f } }); // one echo, 100 ms on
            auto a = makeEngine();
            const auto throughA = render(*a, settings, 14400, blip());
            expectGreaterThan(rms(throughA, 4800 + 240, 4800 + 960), 0.01, "part A has its echo");

            settings.partMode = PartMode::bOnly;
            auto b = makeEngine();
            const auto throughB = render(*b, settings, 14400, blip());
            expectGreaterThan(rms(throughB, 0, 960), 0.01);
            expectLessThan(rms(throughB, 4800 + 240, 4800 + 960), 1.0e-5, "part B has none");
        }

        beginTest("a_slot_that_is_emptied_drops_its_tail");
        {
            auto engine = makeEngine();
            auto settings = onePart();
            settings.inserts[0][2] = insertOf(insertType::delay, { { 0, 200.0f }, { 1, 0.8f }, { 2, 0.5f } });
            render(*engine, settings, 4800, blip());
            expectEquals(engine->insertForTesting(0, 2).currentType(), static_cast<int>(insertType::delay));
            settings.inserts[0][2] = {};
            render(*engine, settings, kBlock, {}); // the block in which the effect goes
            const auto after = render(*engine, settings, 48000, {});
            expectLessThan(rms(after, 0, after.size()), 1.0e-9);
            expectEquals(engine->insertForTesting(0, 2).currentType(), static_cast<int>(insertType::off));
        }

        beginTest("a_synced_insert_delay_follows_the_tempo");
        {
            auto echoAt = [](float bpm)
            {
                auto engine = makeEngine();
                auto settings = onePart();
                settings.parts[0].arp.internalTempoBpm = bpm;
                settings.inserts[0][0] = insertOf(insertType::delay, { { 1, 0.0f }, { 2, 0.5f }, { 3, 1.0f }, { 4, 5.0f } }); // synced, 1/8
                const auto out = render(*engine, settings, 48000, blip());
                return loudestAfter(out, 2400);
            };
            expectWithinAbsoluteError(echoAt(120.0f), 12000, 300); // an eighth at 120 BPM: 250 ms
            expectWithinAbsoluteError(echoAt(60.0f), 24000, 300);  // and 500 ms at 60
        }

        beginTest("the_grain_insert_plays_the_parts_past_as_a_cloud");
        {
            const std::vector<Timed> held { { 0, Type::noteOn, 69 } };
            const std::vector<Timed> halfSecond { { 0, Type::noteOn, 69 }, { 24000, Type::noteOff, 69 } };

            // At Mix 0 the part passes untouched.
            auto settings = onePart();
            auto plainEngine = makeEngine();
            const auto plain = render(*plainEngine, settings, 24000, held);
            settings.inserts[0][0] = insertOf(insertType::grain, { { 7, 0.0f } });
            auto dryEngine = makeEngine();
            expectLessThan(largestDifference(plain, render(*dryEngine, settings, 24000, held)), 1.0e-6);

            // The cloud alone, taken a quarter second back with no Spray, grains of 120 ms: it goes on for as far
            // back as it reaches after the note has stopped, and then it has run out of past.
            settings.inserts[0][0] = insertOf(insertType::grain, { { 2, 250.0f }, { 3, 0.0f }, { 7, 1.0f } });
            auto engine = makeEngine();
            const auto cloud = render(*engine, settings, 96000, halfSecond);
            expectGreaterThan(rms(cloud, 14400, 24000), 0.02, "the cloud sounds while the note is held");
            expectGreaterThan(rms(cloud, 26400, 33600), 0.02, "and for a quarter second after it");
            expectLessThan(rms(cloud, 45600, 96000), 1.0e-4, "then there is no more past to play");
            expect(engine->insertForTesting(0, 0).grainForTesting().activeGrainsForTesting() <= GrainInsert::maxGrains);

            // With nothing scattered the cloud is the part a quarter second late, about as loud as it was;
            // so is a thick, scattered cloud.
            const double direct = rms(plain, 14400, 24000);
            expectWithinAbsoluteError(rms(cloud, 14400, 24000) / direct, 1.0, 0.35);
            settings.inserts[0][0] = insertOf(insertType::grain, { { 0, 400.0f }, { 1, 80.0f }, { 3, 0.5f }, { 7, 1.0f } });
            auto thickEngine = makeEngine();
            const auto thick = render(*thickEngine, settings, 96000, { { 0, Type::noteOn, 69 } });
            expectWithinAbsoluteError(rms(thick, 72000, 96000) / direct, 1.0, 0.5);
            float peak = 0.0f;
            for (const float v : thick)
                peak = std::max(peak, std::abs(v));
            expectLessThan(peak, 1.0f);
        }

        beginTest("grain_pitch_reads_the_past_faster");
        {
            // The part plays a saw at 440 Hz. Unshifted the cloud's strongest tone is 440 Hz; an octave up it is
            // 880 Hz and nothing is left at 440.
            auto ratioFor = [&](float semitones)
            {
                auto settings = onePart();
                settings.inserts[0][0] = insertOf(insertType::grain, { { 3, 0.3f }, { 4, semitones }, { 7, 1.0f } });
                auto engine = makeEngine();
                const auto out = render(*engine, settings, 96000, { { 0, Type::noteOn, 69 } });
                // A cloud smears every tone a few hertz to either side: the power in a band round it counts.
                auto band = [&](double centre)
                {
                    double sum = 0.0;
                    for (double hz = centre - 15.0; hz <= centre + 15.0; hz += 1.0)
                        sum += powerAt(out, 48000, 96000, hz);
                    return sum;
                };
                return band(880.0) / band(440.0);
            };
            expectLessThan(ratioFor(0.0f), 0.6);
            expectGreaterThan(ratioFor(12.0f), 10.0);
        }

        beginTest("freeze_keeps_the_cloud_playing_what_it_caught");
        {
            // A note of half a second. Frozen while it sounds, the cloud is still there three seconds later;
            // left recording, it has long gone.
            auto lastSecond = [&](bool freeze)
            {
                auto settings = onePart();
                settings.inserts[0][0] = insertOf(insertType::grain, { { 3, 0.3f }, { 7, 1.0f } });
                auto engine = makeEngine();
                render(*engine, settings, 19200, { { 0, Type::noteOn, 69 } });
                if (freeze)
                    settings.inserts[0][0] = insertOf(insertType::grain, { { 3, 0.3f }, { 6, 1.0f }, { 7, 1.0f } });
                const auto out = render(*engine, settings, 172800, { { 4800, Type::noteOff, 69 } });
                return rms(out, 124800, 172800);
            };
            expectGreaterThan(lastSecond(true), 0.02);
            expectLessThan(lastSecond(false), 1.0e-4);
        }

        beginTest("the_grain_insert_stays_in_bounds_whatever_is_asked_of_it");
        {
            // Noise in, every setting thrown about block by block, Freeze flipping: the output stays finite and
            // bounded and never more than 32 grains play.
            GrainInsert grain;
            grain.prepare(kRate);
            juce::Random random(20261113);
            juce::AudioBuffer<float> block(2, kBlock);
            bool finite = true;
            float peak = 0.0f;
            int most = 0;
            for (int b = 0; b < 1500; ++b) // eight seconds: the recording wraps round twice
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < kBlock; ++s)
                        block.setSample(ch, s, random.nextFloat() * 2.0f - 1.0f);
                GrainInsert::Settings g;
                g.sizeMs = 10.0f + 490.0f * random.nextFloat();
                g.density = 1.0f + 79.0f * random.nextFloat();
                g.positionMs = 3000.0f * random.nextFloat();
                g.spray = random.nextFloat();
                g.pitchSemitones = -24.0f + 48.0f * random.nextFloat();
                g.spread = random.nextFloat();
                g.freeze = random.nextFloat() < 0.3f;
                g.mix = 1.0f;
                grain.process(block, g);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < kBlock; ++s)
                    {
                        const float v = block.getSample(ch, s);
                        finite = finite && std::isfinite(v);
                        peak = std::max(peak, std::abs(v));
                    }
                most = std::max(most, grain.activeGrainsForTesting());
            }
            expect(finite);
            expectLessThan(peak, 4.0f);
            expect(most <= GrainInsert::maxGrains && most > 4);
        }

        beginTest("a_bank_preset_writes_inserts_as_slot_lines");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"(
[Two Rooms]
room = Workshop
parts = layer
g1 = VA shape=saw level=0.7
i1 = Delay time=375 feedback=0.3 mix=0.25 sync=on division=1/8. mode=pingpong
i3 = EQ low=-3 high=4
b.g1 = VA shape=sine level=0.8
b.i2 = Chorus rate=0.5 depth=0.4 mix=0.3
b.i3 = Grain size=200 density=30 pitch=12 freeze=on mix=0.6
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            if (parsed.presets.size() == 1)
            {
                const auto& preset = parsed.presets.front();
                expect(entryText(preset, "pA.i1.type") == "Delay");
                expect(entryText(preset, "pA.i1.p1") == "375");
                expect(entryText(preset, "pA.i1.p4") == "on");
                expect(entryText(preset, "pA.i1.p5") == "1/8.");
                expect(entryText(preset, "pA.i1.p6") == "pingpong");
                expect(entryText(preset, "pA.i3.type") == "EQ");
                expect(entryText(preset, "pA.i3.p3") == "4");
                expect(entryText(preset, "pB.i2.type") == "Chorus");
                expect(entryText(preset, "pB.i2.p2") == "0.4");
                expect(entryText(preset, "pB.i3.type") == "Grain");
                expect(entryText(preset, "pB.i3.p1") == "200");
                expect(entryText(preset, "pB.i3.p5") == "12");
                expect(entryText(preset, "pB.i3.p7") == "on");
                expect(entryText(preset, "pA.i2.type") == "<none>");
            }
            const auto wrong = coolsynth::presets::parseBankFile("[X]\ni1 = Wah depth=1\ni2 = Delay speed=3\ni4 = Delay\n", "bad.csbank");
            expectEquals(wrong.errors.size(), 3);
        }
    }
};

static V3InsertTests v3InsertTests;

/** Its own class and a heap processor: the test runner's stack frame is small in Debug. */
class V3InsertProcessorTests final : public juce::UnitTest
{
public:
    V3InsertProcessorTests() : juce::UnitTest("V3 insert slots in the processor", "V3") {}

    void runTest() override
    {
        beginTest("a_preset_with_inserts_applies_and_names_its_values_by_the_type");
        {
            const auto parsed = coolsynth::presets::parseBankFile(R"(
[Echo Pad]
room = Workshop
g1 = VA shape=saw level=0.7
i1 = Delay time=375 feedback=0.3 mix=0.25 division=1/8. mode=pingpong
i2 = Pump depth=0.5 division=1/4
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            if (parsed.presets.empty())
                return;

            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 256);
            auto& state = processor->getValueTreeState();
            const auto problems = coolsynth::presets::applyFactoryPreset(state, parsed.presets.front());
            expect(problems.isEmpty(), problems.joinIntoString("\n"));

            expectEquals(static_cast<int>(state.getRawParameterValue("pA.i1.type")->load()), static_cast<int>(insertType::delay));
            expectEquals(static_cast<int>(state.getRawParameterValue("pA.i2.type")->load()), static_cast<int>(insertType::pump));
            expectEquals(static_cast<int>(state.getRawParameterValue("pA.i3.type")->load()), static_cast<int>(insertType::off));
            if (auto* time = state.getParameter("pA.i1.p1"))
            {
                expect(time->getName(32).contains("Time"), time->getName(32));
                expect(time->getCurrentValueAsText().contains("375"), time->getCurrentValueAsText());
            }
            if (auto* division = state.getParameter("pA.i1.p5"))
                expectEquals(division->getCurrentValueAsText(), juce::String("1/8."));
            if (auto* mode = state.getParameter("pA.i1.p6"))
                expectEquals(mode->getCurrentValueAsText(), juce::String("Ping-Pong"));

            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            float peak = 0.0f;
            for (int block = 0; block < 40; ++block)
            {
                processor->processBlock(buffer, midi);
                midi.clear();
                peak = std::max(peak, buffer.getMagnitude(0, 256));
                for (int i = 0; i < 256; ++i)
                    expect(std::isfinite(buffer.getSample(0, i)));
            }
            expectGreaterThan(peak, 0.01f);
        }
    }
};

static V3InsertProcessorTests v3InsertProcessorTests;
