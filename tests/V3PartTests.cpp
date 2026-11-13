#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "synth/v3/EngineV3.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Wave = VaGenerator::Wave;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    float vaNorm(int param, float units)
    {
        return unitsToNormalised(VaGenerator::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    PartSettings basicPart(Wave wave = Wave::sine)
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        g.p[VaGenerator::shape] = vaNorm(VaGenerator::shape, static_cast<float>(wave));
        g.p[VaGenerator::pulseWidth] = vaNorm(VaGenerator::pulseWidth, 0.5f);
        g.p[VaGenerator::unison] = 0.0f;
        g.p[VaGenerator::mix] = vaNorm(VaGenerator::mix, 0.67f);
        g.level = 1.0f;

        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    std::unique_ptr<Part> makePart()
    {
        auto part = std::make_unique<Part>();
        part->prepare(kRate, 512, 1);
        return part;
    }

    EngineMidiEvent event(Type type, int note = 0, float value = 1.0f, int offset = 0)
    {
        EngineMidiEvent e;
        e.type = type;
        e.sampleOffset = offset;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }

    EngineMidiEvent on(int note, float velocity = 1.0f, int offset = 0) { return event(Type::noteOn, note, velocity, offset); }
    EngineMidiEvent off(int note) { return event(Type::noteOff, note, 0.0f); }

    struct Rendered
    {
        std::vector<float> left, right;
    };

    /** Renders `samples`, with `events` in the first block. */
    Rendered render(Part& part, const PartSettings& settings, int samples, std::vector<EngineMidiEvent> events = {})
    {
        Rendered out { std::vector<float>(static_cast<std::size_t>(samples)), std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            const std::span<const EngineMidiEvent> span = start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>();
            part.render(out.left.data() + start, out.right.data() + start, n, span, settings, {});
        }
        return out;
    }

    std::set<int> soundingNotes(const Part& part)
    {
        std::set<int> notes;
        for (int i = 0; i < Part::maxVoices; ++i)
            if (part.voiceForTesting(i).isActive() && part.voiceKeyDownForTesting(i))
                notes.insert(part.voiceForTesting(i).currentNote());
        return notes;
    }

    double measureFrequency(const std::vector<float>& x, std::size_t from)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
    }

    float peakAbs(const std::vector<float>& x, std::size_t from = 0)
    {
        float peak = 0.0f;
        for (std::size_t i = from; i < x.size(); ++i)
            peak = std::max(peak, std::abs(x[i]));
        return peak;
    }
}

class V3PartTests final : public juce::UnitTest
{
public:
    V3PartTests() : juce::UnitTest("V3 part", "V3") {}

    void runTest() override
    {
        beginTest("poly_chord_takes_one_voice_per_note_and_releases");
        {
            auto part = makePart();
            const auto settings = basicPart();
            render(*part, settings, 4800, { on(60), on(64), on(67) });
            expectEquals(part->activeVoiceCountForTesting(), 3);
            expect(soundingNotes(*part) == std::set<int> { 60, 64, 67 });
            render(*part, settings, 4800, { off(60), off(64), off(67) });
            expectEquals(part->activeVoiceCountForTesting(), 0);
        }

        beginTest("a_repeated_key_reuses_its_own_voice");
        {
            auto part = makePart();
            const auto settings = basicPart();
            render(*part, settings, 2400, { on(60) });
            render(*part, settings, 96, { off(60) });
            render(*part, settings, 2400, { on(60) });
            expectEquals(part->activeVoiceCountForTesting(), 1);
        }

        beginTest("fixed_headroom_sets_one_voice_level");
        {
            auto part = makePart();
            const auto out = render(*part, basicPart(), 9600, { on(69) });
            const float peak = peakAbs(out.left, 4800);
            logMessage("one centred sine voice peaks at " + juce::String(peak, 4));
            expectWithinAbsoluteError(peak, 0.70711f * Part::voiceHeadroom, 0.01f);
        }

        beginTest("polyphony_limit_steals_the_oldest_held_note");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.polyphony = 2;
            render(*part, settings, 2400, { on(60) });
            render(*part, settings, 2400, { on(64) });
            render(*part, settings, 2400, { on(67) });
            expectEquals(part->activeVoiceCountForTesting(), 2);
            expect(soundingNotes(*part) == std::set<int> { 64, 67 });
        }

        beginTest("a_released_voice_is_taken_before_a_held_one");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.polyphony = 2;
            settings.voice.envelopes[0].release = 1.0f; // still ringing when the third note comes
            render(*part, settings, 2400, { on(60), on(64) });
            render(*part, settings, 2400, { off(60) });
            render(*part, settings, 2400, { on(67) });
            expect(soundingNotes(*part) == std::set<int> { 64, 67 });
        }

        beginTest("sustain_pedal_holds_released_keys_until_lifted");
        {
            auto part = makePart();
            const auto settings = basicPart();
            render(*part, settings, 2400, { event(Type::sustainPedal, 0, 1.0f), on(60) });
            render(*part, settings, 9600, { off(60) });
            expectEquals(part->activeVoiceCountForTesting(), 1);
            render(*part, settings, 4800, { event(Type::sustainPedal, 0, 0.0f) });
            expectEquals(part->activeVoiceCountForTesting(), 0);
        }

        beginTest("mono_last_priority_returns_to_the_held_key");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.playMode = PlayMode::mono;
            render(*part, settings, 2400, { on(60) });
            render(*part, settings, 2400, { on(64) });
            expectEquals(part->activeVoiceCountForTesting(), 1);
            expectEquals(part->voiceForTesting(0).currentNote(), 64);
            render(*part, settings, 2400, { off(64) });
            expectEquals(part->voiceForTesting(0).currentNote(), 60);
            render(*part, settings, 4800, { off(60) });
            expectEquals(part->activeVoiceCountForTesting(), 0);
        }

        beginTest("mono_low_priority_keeps_the_lowest_key");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.playMode = PlayMode::mono;
            settings.keyPriority = KeyPriority::low;
            render(*part, settings, 2400, { on(64) });
            render(*part, settings, 2400, { on(60) });
            expectEquals(part->voiceForTesting(0).currentNote(), 60);
            render(*part, settings, 2400, { on(67) });
            expectEquals(part->voiceForTesting(0).currentNote(), 60);
            render(*part, settings, 2400, { off(60) });
            expectEquals(part->voiceForTesting(0).currentNote(), 64);
        }

        beginTest("legato_keeps_the_envelope_and_mono_restarts_it");
        {
            for (const auto mode : { PlayMode::legato, PlayMode::mono })
            {
                auto part = makePart();
                auto settings = basicPart();
                settings.playMode = mode;
                settings.voice.envelopes[0].attack = 0.010f;
                settings.voice.envelopes[0].decay = 0.050f;
                settings.voice.envelopes[0].sustain = 0.3f;
                render(*part, settings, 4800, { on(60) });
                const float before = part->voiceForTesting(0).ampLevel();
                render(*part, settings, 240, { on(64) });
                const float after = part->voiceForTesting(0).ampLevel();
                logMessage(juce::String(mode == PlayMode::legato ? "legato" : "mono") + ": amp " + juce::String(before, 3) + " then " + juce::String(after, 3));
                if (mode == PlayMode::legato)
                    expectWithinAbsoluteError(after, before, 0.01f);
                else
                    expect(after > before + 0.2f);
            }
        }

        beginTest("unison_stacks_detuned_voices_on_one_note");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.playMode = PlayMode::unison;
            settings.unisonVoices = 4;
            settings.unisonDetuneCents = 20.0f;
            render(*part, settings, 2400, { on(60) });
            expectEquals(part->activeVoiceCountForTesting(), 4);
            std::vector<float> pitches;
            for (int i = 0; i < 4; ++i)
                pitches.push_back(part->voiceForTesting(i).pitchForTesting());
            std::sort(pitches.begin(), pitches.end());
            expectWithinAbsoluteError(pitches.front(), 59.8f, 1.0e-4f);
            expectWithinAbsoluteError(pitches.back(), 60.2f, 1.0e-4f);
            expect(pitches[1] > 59.8f && pitches[2] < 60.2f && pitches[1] < pitches[2]);
        }

        beginTest("legato_only_glide_jumps_on_detached_keys");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.playMode = PlayMode::mono;
            settings.voice.glideSeconds = 0.1f;
            settings.glideLegatoOnly = true;
            render(*part, settings, 2400, { on(60) });
            render(*part, settings, 96, { off(60) });
            render(*part, settings, 64, { on(67) });
            expectEquals(part->voiceForTesting(0).pitchForTesting(), 67.0f);
            render(*part, settings, 64, { on(72) });
            const float moving = part->voiceForTesting(0).pitchForTesting();
            expect(moving > 67.0f && moving < 72.0f);
        }

        beginTest("pitch_bend_follows_the_range");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.bendRangeSemitones = 7.0f;
            const auto out = render(*part, settings, 24000, { event(Type::pitchBend, 0, 1.0f), on(69) });
            expectEquals(part->bendSemitonesForTesting(), 7.0f);
            const double cents = 1200.0 * std::log2(measureFrequency(out.left, 4800) / (440.0 * std::pow(2.0, 7.0 / 12.0)));
            expect(std::abs(cents) < 2.0, "bent pitch off by " + juce::String(cents, 2) + " cents");
        }

        beginTest("all_notes_off_releases_and_all_sound_off_silences");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.voice.envelopes[0].release = 0.05f;
            render(*part, settings, 2400, { on(60), on(64) });
            render(*part, settings, 240, { event(Type::allNotesOff) });
            expectEquals(part->activeVoiceCountForTesting(), 2);
            render(*part, settings, 4800);
            expectEquals(part->activeVoiceCountForTesting(), 0);

            render(*part, settings, 2400, { on(60), on(64) });
            const auto out = render(*part, settings, 2400, { event(Type::allSoundOff) });
            expectEquals(part->activeVoiceCountForTesting(), 0);
            expect(peakAbs(out.left, 256) < 1.0e-6f);
        }

        beginTest("a_note_starts_at_its_sample_offset");
        {
            auto part = makePart();
            const auto out = render(*part, basicPart(Wave::saw), 512, { on(69, 1.0f, 200) });
            const auto onsetLimit = static_cast<std::size_t>(200 + part->latencySamples());
            expect(peakAbs(std::vector<float>(out.left.begin(), out.left.begin() + static_cast<std::ptrdiff_t>(onsetLimit - 2))) < 1.0e-4f);
            expect(peakAbs(std::vector<float>(out.left.begin() + static_cast<std::ptrdiff_t>(onsetLimit), out.left.begin() + 400)) > 0.01f);
        }

        beginTest("the_arp_plays_held_keys_and_a_tap_inside_one_block_still_fires");
        {
            auto part = makePart();
            auto settings = basicPart();
            settings.arp.enabled = true;
            settings.voice.envelopes[0].release = 0.005f;

            std::set<int> heard;
            render(*part, settings, kBlock, { on(60), on(64) });
            for (int block = 0; block < 100; ++block) // about half a second: four steps of 1/16 at 120 BPM
            {
                render(*part, settings, kBlock);
                for (int note : soundingNotes(*part))
                    heard.insert(note);
            }
            expect(heard.count(60) == 1 && heard.count(64) == 1);
            render(*part, settings, 24000, { off(60), off(64) });

            auto tap = makePart();
            render(*tap, settings, kBlock, { on(67), event(Type::noteOff, 67, 0.0f, 100) });
            bool fired = false;
            for (int block = 0; block < 20 && ! fired; ++block)
            {
                fired = tap->activeVoiceCountForTesting() > 0;
                render(*tap, settings, kBlock);
            }
            expect(fired);
        }

        beginTest("a_block_longer_than_prepared_is_split");
        {
            auto part = makePart();
            std::vector<float> left(2000, 0.0f), right(2000, 0.0f);
            const std::vector<EngineMidiEvent> events { on(69, 1.0f, 0), on(72, 1.0f, 1500) };
            part->render(left.data(), right.data(), 2000, events, basicPart(), {});
            expectEquals(part->activeVoiceCountForTesting(), 2);
            expect(peakAbs(left, 1600) > 0.1f);
        }
    }
};

class V3EngineTests final : public juce::UnitTest
{
public:
    V3EngineTests() : juce::UnitTest("V3 engine", "V3") {}

    void runTest() override
    {
        beginTest("latency_master_gain_clipper_and_panic");
        {
            auto engine = std::make_unique<EngineV3>();
            engine->prepare(kRate, kBlock, 2);
            expectEquals(engine->getLatencySamples(), engine->partForTesting().latencySamples());
            logMessage("engine latency: " + juce::String(engine->getLatencySamples()) + " samples");

            EngineSettings settings;
            settings.part = basicPart();
            juce::AudioBuffer<float> buffer(2, kBlock);

            auto renderPeak = [&](EngineSettings s, std::vector<EngineMidiEvent> events, int blocks)
            {
                float peak = 0.0f;
                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    engine->render(buffer, b == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>(), s);
                    if (b > blocks / 2)
                        peak = std::max(peak, buffer.getMagnitude(0, 0, kBlock));
                }
                return peak;
            };

            const float full = renderPeak(settings, { on(69) }, 40);
            settings.masterGainLinear = 0.5f;
            const float half = renderPeak(settings, {}, 40);
            expectWithinAbsoluteError(20.0f * std::log10(half / full), -6.02f, 0.1f);

            settings.masterGainLinear = 4.0f;
            settings.part.voice.generators[0].p[VaGenerator::shape] = vaNorm(VaGenerator::shape, 2.0f);
            const float loud = renderPeak(settings, { on(48), on(52), on(55), on(59), on(62), on(65), on(69), on(72) }, 40);
            logMessage("eight saws at +12 dB master peak at " + juce::String(loud, 3));
            expect(loud <= 1.0f);

            engine->panic();
            expectEquals(engine->partForTesting().activeVoiceCountForTesting(), 0);
            buffer.clear();
            engine->render(buffer, {}, settings);
            expect(buffer.getMagnitude(0, 0, kBlock) < 1.0e-6f);
        }

        beginTest("mono_output_buffer_gets_the_sum");
        {
            auto engine = std::make_unique<EngineV3>();
            engine->prepare(kRate, kBlock, 1);
            EngineSettings settings;
            settings.part = basicPart();
            juce::AudioBuffer<float> buffer(1, kBlock);
            const std::vector<EngineMidiEvent> events { on(69) };
            float peak = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                engine->render(buffer, b == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>(), settings);
                peak = std::max(peak, buffer.getMagnitude(0, 0, kBlock));
            }
            expect(peak > 0.3f);
        }
    }
};

static V3PartTests v3PartTests;
static V3EngineTests v3EngineTests;
