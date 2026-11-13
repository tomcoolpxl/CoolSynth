#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "parameters/v3/PartParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "synth/v3/ChordMemory.h"
#include "synth/v3/Part.h"

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

    EngineMidiEvent on(int note, int offset = 0, float velocity = 1.0f) { return event(Type::noteOn, note, velocity, offset); }
    EngineMidiEvent off(int note, int offset = 0) { return event(Type::noteOff, note, 0.0f, offset); }

    struct Played
    {
        std::vector<EngineMidiEvent> events;
        std::vector<std::uint16_t> inputsBefore;
    };

    /** One chunk through chord memory. */
    Played run(ChordMemory& chord, const ChordSettings& settings, int samples, std::vector<EngineMidiEvent> in = {})
    {
        Played out;
        out.events.resize(in.size() * ChordMemory::maxEventsPerInput + ChordMemory::maxPending);
        out.inputsBefore.resize(out.events.size());
        const int count = chord.process(in, 0, samples, settings, out.events, out.inputsBefore);
        out.events.resize(static_cast<std::size_t>(count));
        out.inputsBefore.resize(static_cast<std::size_t>(count));
        return out;
    }

    std::vector<int> notesOf(const Played& played, Type type)
    {
        std::vector<int> notes;
        for (const auto& e : played.events)
            if (e.type == type)
                notes.push_back(static_cast<int>(e.noteNumber));
        return notes;
    }

    ChordSettings chordOf(int type, float strumMs = 0.0f)
    {
        ChordSettings s;
        s.type = type;
        s.strumMs = strumMs;
        return s;
    }

    std::unique_ptr<ChordMemory> makeChord()
    {
        auto chord = std::make_unique<ChordMemory>();
        chord->prepare(kRate);
        return chord;
    }

    /** Runs chunks of kBlock and returns every note-on with its time in samples from the start. */
    std::vector<std::pair<int, int>> noteOnTimes(ChordMemory& chord, const ChordSettings& settings, int chunks, std::vector<EngineMidiEvent> first)
    {
        std::vector<std::pair<int, int>> times;
        for (int c = 0; c < chunks; ++c)
        {
            const auto played = run(chord, settings, kBlock, c == 0 ? first : std::vector<EngineMidiEvent> {});
            for (const auto& e : played.events)
                if (e.type == Type::noteOn)
                    times.emplace_back(c * kBlock + e.sampleOffset, static_cast<int>(e.noteNumber));
        }
        return times;
    }

    PartSettings sinePart()
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        const auto& d = VaGenerator::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        g.level = 1.0f;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    void renderPart(Part& part, const PartSettings& settings, int samples, std::vector<EngineMidiEvent> events = {})
    {
        std::vector<float> left(kBlock), right(kBlock);
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            const auto span = start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>();
            part.render(left.data(), right.data(), n, span, settings, {});
        }
    }

    std::set<int> heldNotes(const Part& part)
    {
        std::set<int> notes;
        for (int i = 0; i < Part::maxVoices; ++i)
            if (part.voiceForTesting(i).isActive() && part.voiceKeyDownForTesting(i))
                notes.insert(part.voiceForTesting(i).currentNote());
        return notes;
    }
}

class V3ChordTests final : public juce::UnitTest
{
public:
    V3ChordTests() : juce::UnitTest("V3 chord memory", "V3") {}

    void runTest() override
    {
        beginTest("off_passes_every_event_through_in_place");
        {
            auto chord = makeChord();
            const std::vector<EngineMidiEvent> in { on(60, 10, 0.7f), event(Type::pitchBend, 0, 0.5f, 20), off(60, 30),
                                                    event(Type::sustainPedal, 0, 1.0f, 30) };
            const auto played = run(*chord, chordOf(chordType::off), kBlock, in);
            expectEquals(static_cast<int>(played.events.size()), 4);
            for (std::size_t i = 0; i < std::min<std::size_t>(4, played.events.size()); ++i)
            {
                expect(played.events[i].type == in[i].type);
                expectEquals(played.events[i].sampleOffset, in[i].sampleOffset);
                expectEquals(static_cast<int>(played.events[i].noteNumber), static_cast<int>(in[i].noteNumber));
                expectEquals(played.events[i].value, in[i].value);
                expectEquals(static_cast<int>(played.inputsBefore[i]), static_cast<int>(i) + 1);
            }
        }

        beginTest("every_type_is_a_chord_on_the_key_and_a_key_plays_and_ends_it");
        {
            const std::map<int, std::vector<int>> expected {
                { chordType::octave, { 0, 12 } },         { chordType::fifth, { 0, 7, 12 } },        { chordType::major, { 0, 4, 7 } },
                { chordType::minor, { 0, 3, 7 } },        { chordType::sus2, { 0, 2, 7 } },          { chordType::sus4, { 0, 5, 7 } },
                { chordType::major7, { 0, 4, 7, 11 } },   { chordType::minor7, { 0, 3, 7, 10 } },    { chordType::dominant7, { 0, 4, 7, 10 } },
                { chordType::major9, { 0, 4, 7, 11, 14 } }, { chordType::minor9, { 0, 3, 7, 10, 14 } },
                { chordType::minor11, { 0, 3, 7, 10, 14, 17 } } };
            expectEquals(static_cast<int>(expected.size()), 12);
            for (const auto& [type, semitones] : expected)
            {
                auto chord = makeChord();
                const auto settings = chordOf(type);
                const auto down = run(*chord, settings, kBlock, { on(60, 17, 0.6f) });
                std::vector<int> notes;
                for (int s : semitones)
                    notes.push_back(60 + s);
                expect(notesOf(down, Type::noteOn) == notes, "type " + juce::String(type));
                for (const auto& e : down.events)
                {
                    expectEquals(e.sampleOffset, 17);
                    expectEquals(e.value, 0.6f);
                }
                const auto up = run(*chord, settings, kBlock, { off(60, 40) });
                expect(notesOf(up, Type::noteOff) == notes, "release of type " + juce::String(type));
            }

            // Notes above the top of the keyboard are left out.
            auto chord = makeChord();
            expect(notesOf(run(*chord, chordOf(chordType::major), kBlock, { on(122) }), Type::noteOn) == std::vector<int> { 122, 126 });
        }

        beginTest("a_note_two_keys_share_is_struck_again_and_ends_with_the_last_key");
        {
            auto chord = makeChord();
            const auto settings = chordOf(chordType::major);
            run(*chord, settings, kBlock, { on(60) });
            expect(notesOf(run(*chord, settings, kBlock, { on(64) }), Type::noteOn) == std::vector<int> { 64, 68, 71 });
            expectEquals(chord->soundingCountForTesting(64), 2);
            expect(notesOf(run(*chord, settings, kBlock, { off(60) }), Type::noteOff) == std::vector<int> { 60, 67 });
            expect(notesOf(run(*chord, settings, kBlock, { off(64) }), Type::noteOff) == std::vector<int> { 64, 68, 71 });
            for (int note = 0; note < 128; ++note)
                expectEquals(chord->soundingCountForTesting(note), 0);
        }

        beginTest("strum_spaces_the_notes_across_blocks_and_a_negative_time_strums_downwards");
        {
            auto up = makeChord();
            const auto rising = noteOnTimes(*up, chordOf(chordType::major, 10.0f), 8, { on(60, 100) });
            expect(rising == std::vector<std::pair<int, int>> { { 100, 60 }, { 580, 64 }, { 1060, 67 } });

            auto down = makeChord();
            const auto falling = noteOnTimes(*down, chordOf(chordType::major, -10.0f), 8, { on(60, 100) });
            expect(falling == std::vector<std::pair<int, int>> { { 100, 67 }, { 580, 64 }, { 1060, 60 } });

            // A strummed note reports the played events read by the time it sounds.
            auto late = makeChord();
            const auto settings = chordOf(chordType::major, 1.0f); // 48 samples apart
            const auto played = run(*late, settings, kBlock, { on(60, 0), event(Type::modWheel, 0, 0.5f, 60) });
            expectEquals(static_cast<int>(played.events.size()), 4);
            if (played.events.size() == 4)
            {
                expect(played.events[1].type == Type::noteOn && played.events[1].sampleOffset == 48);
                expectEquals(static_cast<int>(played.inputsBefore[1]), 1);
                expect(played.events[2].type == Type::modWheel);
                expectEquals(static_cast<int>(played.inputsBefore[2]), 2);
                expect(played.events[3].type == Type::noteOn && played.events[3].sampleOffset == 96);
                expectEquals(static_cast<int>(played.inputsBefore[3]), 2);
            }
        }

        beginTest("a_key_released_mid_strum_drops_the_notes_still_waiting");
        {
            auto chord = makeChord();
            const auto settings = chordOf(chordType::major, 10.0f);
            run(*chord, settings, kBlock, { on(60) });
            expectEquals(chord->pendingCountForTesting(), 2);
            const auto up = run(*chord, settings, kBlock, { off(60, 100) }); // at 356, before the second note at 480
            expect(notesOf(up, Type::noteOff) == std::vector<int> { 60 });
            expectEquals(chord->pendingCountForTesting(), 0);
            for (int c = 0; c < 8; ++c)
                expectEquals(static_cast<int>(run(*chord, settings, kBlock).events.size()), 0);

            // Released after the second note and before the third at 960: both that sounded end.
            auto second = makeChord();
            run(*second, settings, 512, { on(60) });
            expect(notesOf(run(*second, settings, kBlock, { off(60, 100) }), Type::noteOff) == std::vector<int> { 60, 64 });
            expectEquals(second->pendingCountForTesting(), 0);
        }

        beginTest("a_release_ends_what_the_key_started_even_if_the_type_changed");
        {
            auto chord = makeChord();
            run(*chord, chordOf(chordType::major), kBlock, { on(60) });
            expect(notesOf(run(*chord, chordOf(chordType::minor9), kBlock, { off(60) }), Type::noteOff) == std::vector<int> { 60, 64, 67 });
            run(*chord, chordOf(chordType::major), kBlock, { on(60) });
            expect(notesOf(run(*chord, chordOf(chordType::off), kBlock, { off(60) }), Type::noteOff) == std::vector<int> { 60, 64, 67 });
        }

        beginTest("a_key_struck_again_keeps_the_notes_both_chords_share");
        {
            auto chord = makeChord();
            run(*chord, chordOf(chordType::major), kBlock, { on(60) });
            const auto again = run(*chord, chordOf(chordType::minor), kBlock, { on(60) });
            expect(notesOf(again, Type::noteOff) == std::vector<int> { 64 });
            expect(notesOf(again, Type::noteOn) == std::vector<int> { 60, 63, 67 });
            expectEquals(chord->soundingCountForTesting(60), 1);
            expectEquals(chord->soundingCountForTesting(64), 0);
            expect(notesOf(run(*chord, chordOf(chordType::minor), kBlock, { off(60) }), Type::noteOff) == std::vector<int> { 60, 63, 67 });

            // With chord memory off a repeated note-on is one note-on, as it always was.
            auto plain = makeChord();
            run(*plain, chordOf(chordType::off), kBlock, { on(60) });
            const auto repeated = run(*plain, chordOf(chordType::off), kBlock, { on(60) });
            expectEquals(static_cast<int>(repeated.events.size()), 1);
            expect(notesOf(repeated, Type::noteOn) == std::vector<int> { 60 });
        }

        beginTest("all_notes_off_forgets_the_keys_and_the_notes_still_waiting");
        {
            auto chord = makeChord();
            const auto settings = chordOf(chordType::minor7, 20.0f);
            run(*chord, settings, kBlock, { on(60) });
            const auto cleared = run(*chord, settings, kBlock, { event(Type::allNotesOff, 0, 0.0f, 5) });
            expectEquals(static_cast<int>(cleared.events.size()), 1);
            expect(! cleared.events.empty() && cleared.events[0].type == Type::allNotesOff);
            expectEquals(chord->pendingCountForTesting(), 0);
            for (int c = 0; c < 20; ++c)
                expectEquals(static_cast<int>(run(*chord, settings, kBlock).events.size()), 0);
            // The key's own release, arriving afterwards, is passed on as the plain note-off it is.
            expect(notesOf(run(*chord, settings, kBlock, { off(60) }), Type::noteOff) == std::vector<int> { 60 });
        }

        beginTest("learning_collects_a_chord_and_hands_it_over_once");
        {
            auto chord = makeChord();
            auto settings = chordOf(chordType::major);
            settings.learn = true;
            // While learning, keys play plain notes.
            const auto down = run(*chord, settings, kBlock, { on(55), on(48), on(64, 30), on(52, 60) });
            expect(notesOf(down, Type::noteOn) == std::vector<int> { 55, 48, 64, 52 });
            expectEquals(static_cast<int>(chord->consumeLearned()), 0);
            run(*chord, settings, kBlock, { off(55), off(64), off(52) });
            expectEquals(static_cast<int>(chord->consumeLearned()), 0);
            run(*chord, settings, kBlock, { off(48) });
            const auto packed = chord->consumeLearned();
            expect(packed != 0);
            expect(unpackLearnedChord(packed) == std::array<int, learnedIntervalCount> { 4, 7, 16, 0, 0 });
            expectEquals(static_cast<int>(chord->consumeLearned()), 0);

            // The learned chord plays on any key; unused and repeated intervals do not count.
            ChordSettings learned;
            learned.type = chordType::learned;
            learned.learned = { 16, 4, 7, 0, 7 };
            expect(notesOf(run(*chord, learned, kBlock, { on(60) }), Type::noteOn) == std::vector<int> { 60, 64, 67, 76 });
            run(*chord, learned, kBlock, { off(60) });

            // More than six notes keep the lowest six; notes over three octaves above the root are left out.
            auto wide = makeChord();
            run(*wide, settings, kBlock, { on(40), on(43), on(47), on(50), on(54), on(57), on(60), on(90) });
            run(*wide, settings, kBlock, { off(40), off(43), off(47), off(50), off(54), off(57), off(60), off(90) });
            expect(unpackLearnedChord(wide->consumeLearned()) == std::array<int, learnedIntervalCount> { 3, 7, 10, 14, 17 });

            // A single key learns a chord of that key alone.
            auto single = makeChord();
            run(*single, settings, kBlock, { on(60) });
            run(*single, settings, kBlock, { off(60) });
            const auto one = single->consumeLearned();
            expect(one != 0);
            expect(unpackLearnedChord(one) == std::array<int, learnedIntervalCount> {});
        }

        beginTest("one_key_sounds_the_whole_chord_in_a_part_and_its_release_ends_it");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 3);
            auto settings = sinePart();
            settings.chord = chordOf(chordType::minor7, 20.0f); // a note every 960 samples
            settings.globalEnvelopeTrigger = GenvTrigger::everyNote;
            settings.globalEnvelope.attack = 0.5f;
            settings.globalEnvelope.sustain = 1.0f;

            renderPart(*part, settings, 4800, { on(57) });
            expect(heldNotes(*part) == std::set<int> { 57, 60, 64, 67 });
            expectEquals(part->heldNoteCountForTesting(), 4);

            // The global envelope follows the key as played: it started once, not with each strummed
            // note, so it stands where a plain note's envelope stands.
            auto plainPart = std::make_unique<Part>();
            plainPart->prepare(kRate, 512, 3);
            auto plain = settings;
            plain.chord = {};
            renderPart(*plainPart, plain, 4800, { on(57) });
            expectGreaterThan(plainPart->globalEnvelopeForTesting().getValue(), 0.05f);
            expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), plainPart->globalEnvelopeForTesting().getValue(), 1.0e-5f);

            renderPart(*part, settings, 2400, { off(57) });
            expect(heldNotes(*part).empty());
            expectEquals(part->heldNoteCountForTesting(), 0);
            expect(part->globalEnvelopeForTesting().getStage() == Envelope::Stage::release
                   || ! part->globalEnvelopeForTesting().isActive());
        }

        beginTest("the_arp_walks_the_chord_one_key_plays_in_order");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 3);
            auto settings = sinePart();
            settings.chord = chordOf(chordType::major);
            settings.arp.enabled = true; // up, 1/16 at 120 BPM: a step every 6000 samples

            std::vector<int> order;
            std::vector<float> left(64), right(64);
            const std::vector<EngineMidiEvent> key { on(60) };
            for (int block = 0; block < 600; ++block) // 0.8 s: six steps
            {
                part->render(left.data(), right.data(), 64, block == 0 ? std::span<const EngineMidiEvent>(key) : std::span<const EngineMidiEvent>(),
                             settings, {});
                const int last = part->lastPlayedNoteForTesting();
                if (last >= 0 && (order.empty() || order.back() != last))
                    order.push_back(last);
            }
            expectEquals(part->arpHeldNoteCountForTesting(), 3);
            expect(order.size() >= 6);
            if (order.size() >= 6)
                expect(std::vector<int>(order.begin(), order.begin() + 6) == std::vector<int> { 60, 64, 67, 60, 64, 67 });

            const std::vector<EngineMidiEvent> up { off(60) };
            part->render(left.data(), right.data(), 64, up, settings, {});
            expectEquals(part->arpHeldNoteCountForTesting(), 0);
        }
    }
};

static V3ChordTests v3ChordTests;

/** Its own class and a heap processor: the test runner's stack frame is small in Debug. */
class V3ChordLearnTests final : public juce::UnitTest
{
public:
    V3ChordLearnTests() : juce::UnitTest("V3 chord learning", "V3") {}

    void runTest() override
    {
        beginTest("a_chord_played_while_learn_is_on_becomes_the_learned_chord");
        {
            namespace pv3 = coolsynth::parameters::v3;
            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(48000.0, 256);
            auto& state = processor->getValueTreeState();
            auto plain = [&](const juce::String& field)
            {
                return state.getRawParameterValue(pv3::chordParameterId(pv3::Part::a, field))->load();
            };
            auto* learn = state.getParameter(pv3::chordParameterId(pv3::Part::a, "learn"));
            expect(learn != nullptr);
            if (learn == nullptr)
                return;
            learn->setValueNotifyingHost(1.0f);

            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer down;
            for (int note : { 60, 63, 67, 70 })
                down.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 10);
            processor->processBlock(buffer, down);
            processor->flushMappedControllerEventsSync();
            expectEquals(plain("learn"), 1.0f, "still learning while keys are down");

            juce::MidiBuffer up;
            for (int note : { 60, 63, 67, 70 })
                up.addEvent(juce::MidiMessage::noteOff(1, note), 20);
            processor->processBlock(buffer, up);
            processor->flushMappedControllerEventsSync();

            expectEquals(plain("learn"), 0.0f);
            expectEquals(static_cast<int>(plain("type")), static_cast<int>(coolsynth::synth::v3::chordType::learned));
            expectEquals(static_cast<int>(plain("n1")), 3);
            expectEquals(static_cast<int>(plain("n2")), 7);
            expectEquals(static_cast<int>(plain("n3")), 10);
            expectEquals(static_cast<int>(plain("n4")), 0);
            expectEquals(static_cast<int>(plain("n5")), 0);
        }

        beginTest("a_bank_preset_can_name_its_chord");
        {
            namespace pv3 = coolsynth::parameters::v3;
            expectEquals(pv3::chordTypeNames().size(), static_cast<int>(coolsynth::synth::v3::chordType::count));
            expectEquals(pv3::chordTypeNames()[coolsynth::synth::v3::chordType::minor7], juce::String("Minor 7"));
            expectEquals(pv3::chordParameterId(pv3::Part::a, "strum"), juce::String("pA.chord.strum"));
        }
    }
};

static V3ChordLearnTests v3ChordLearnTests;
