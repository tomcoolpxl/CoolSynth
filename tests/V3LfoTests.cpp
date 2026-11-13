#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/v3/Part.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kVoiceRate = 96000.0;
    constexpr double kHostRate = 48000.0;
    constexpr int kStep = 32;

    /** Advances by `seconds` in sub-blocks; returns the value of the last span. */
    float run(Lfo& lfo, const LfoSettings& s, double seconds, LfoClock clock = {})
    {
        auto remaining = static_cast<long long>(std::llround(seconds * kVoiceRate));
        float value = lfo.value();
        while (remaining > 0)
        {
            const int n = static_cast<int>(std::min<long long>(kStep, remaining));
            value = lfo.advance(n, s, clock);
            remaining -= n;
        }
        return value;
    }

    std::unique_ptr<Lfo> makeLfo(std::uint32_t seed = 7)
    {
        auto lfo = std::make_unique<Lfo>();
        lfo->prepare(kVoiceRate, seed);
        return lfo;
    }

    PartSettings simplePart()
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        g.level = 1.0f;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    coolsynth::synth::EngineMidiEvent event(Type type, int note = 0, float value = 1.0f)
    {
        coolsynth::synth::EngineMidiEvent e;
        e.type = type;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }

    void renderPart(Part& part, const PartSettings& settings, int samples, std::vector<coolsynth::synth::EngineMidiEvent> events = {},
                    coolsynth::synth::EngineTransportInfo transport = {})
    {
        std::vector<float> l(256), r(256);
        for (int start = 0; start < samples; start += 256)
        {
            const int n = std::min(256, samples - start);
            const auto span = start == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(events)
                                         : std::span<const coolsynth::synth::EngineMidiEvent>();
            part.render(l.data(), r.data(), n, span, settings, transport);
            transport.hostPpqAtBlockStart += n * transport.hostBpm / 60.0 / kHostRate;
        }
    }

    int activeVoice(const Part& part, int note)
    {
        for (int i = 0; i < Part::maxVoices; ++i)
            if (part.voiceForTesting(i).isActive() && part.voiceForTesting(i).currentNote() == note)
                return i;
        return -1;
    }
}

class V3LfoTests final : public juce::UnitTest
{
public:
    V3LfoTests() : juce::UnitTest("V3 LFO", "V3") {}

    void runTest() override
    {
        beginTest("every_shape_starts_where_documented_and_stays_in_range");
        {
            const float expectedStart[] = { 0.0f, 0.0f, -1.0f, 1.0f, 1.0f };
            for (int shape = 0; shape < lfoShapeCount; ++shape)
            {
                auto lfo = makeLfo();
                LfoSettings s;
                s.shape = static_cast<LfoShape>(shape);
                s.rateHz = 3.0f;
                lfo->restart(s);
                const float first = lfo->advance(kStep, s, {});
                if (shape < 5)
                    expectWithinAbsoluteError(first, expectedStart[shape], 1.0e-6f, "shape " + juce::String(shape));
                if (static_cast<LfoShape>(shape) == LfoShape::steps)
                    expectWithinAbsoluteError(first, -1.0f, 1.0e-6f);

                float low = 1.0f, high = -1.0f, largestStep = 0.0f, previous = first;
                int changes = 0;
                for (int i = 0; i < static_cast<int>(kVoiceRate) / kStep; ++i) // one second, three cycles
                {
                    const float v = lfo->advance(kStep, s, {});
                    low = std::min(low, v);
                    high = std::max(high, v);
                    largestStep = std::max(largestStep, std::abs(v - previous));
                    changes += v != previous ? 1 : 0;
                    previous = v;
                }
                expect(low >= -1.0f && high <= 1.0f, "shape " + juce::String(shape) + " leaves -1..1");
                if (static_cast<LfoShape>(shape) == LfoShape::sampleHold)
                    expectLessOrEqual(changes, 3, "sample and hold changes once per cycle");
                if (static_cast<LfoShape>(shape) == LfoShape::steps)
                    expectLessOrEqual(changes, 3 * Lfo::stepCount, "sixteen steps per cycle");
                if (static_cast<LfoShape>(shape) == LfoShape::smoothRandom || static_cast<LfoShape>(shape) == LfoShape::sine)
                    expectLessThan(largestStep, 0.01f, "shape " + juce::String(shape) + " moves without jumps");
            }
        }

        beginTest("a_synced_cycle_lasts_its_division_at_any_tempo");
        {
            for (double bpm : { 120.0, 97.0 })
            {
                for (int division = 0; division < static_cast<int>(lfoDivisions.size()); ++division)
                {
                    auto lfo = makeLfo();
                    LfoSettings s;
                    s.sync = true;
                    s.division = division;
                    s.shape = LfoShape::sawUp;
                    lfo->restart(s);
                    LfoClock clock;
                    clock.bpm = bpm;
                    const double cycleSeconds = lfoDivisions[static_cast<std::size_t>(division)].beats * 60.0 / bpm;
                    run(*lfo, s, cycleSeconds * 2.0, clock);
                    const double phase = lfo->phaseForTesting();
                    const double error = std::min(phase, 1.0 - phase);
                    // two cycles in whole voice samples: the rounding of the length is the only error
                    expectLessThan(error, 1.0 / (cycleSeconds * kVoiceRate) + 1.0e-9,
                                   juce::String(lfoDivisions[static_cast<std::size_t>(division)].name) + " at " + juce::String(bpm));
                }
            }
        }

        beginTest("a_free_synced_lfo_follows_the_song_position_while_the_host_plays");
        {
            LfoSettings s;
            s.sync = true;
            s.retrigger = false;
            s.division = 10; // 1/8: half a beat
            s.startPhase = 0.25f;
            s.shape = LfoShape::sawUp;

            auto a = makeLfo(1);
            auto b = makeLfo(2);
            run(*a, s, 0.37); // a different history while stopped
            LfoClock clock;
            clock.bpm = 128.0;
            clock.playing = true;
            clock.ppq = 17.3;
            const float va = a->advance(kStep, s, clock);
            const float vb = b->advance(kStep, s, clock);
            const double expectedPhase = std::fmod(17.3 / 0.5 + 0.25, 1.0);
            expectWithinAbsoluteError(a->phaseForTesting(), expectedPhase, 1.0e-9);
            expectWithinAbsoluteError(va, vb, 1.0e-6f);
            expectWithinAbsoluteError(va, static_cast<float>(2.0 * expectedPhase - 1.0), 1.0e-5f);

            // With retrigger on the note owns the phase, even while the host plays.
            s.retrigger = true;
            a->restart(s);
            const float restarted = a->advance(kStep, s, clock);
            expectWithinAbsoluteError(restarted, -0.5f, 1.0e-5f); // saw up at the start phase 0.25
        }

        beginTest("retrigger_restarts_at_the_start_phase_and_free_running_does_not");
        {
            auto lfo = makeLfo();
            LfoSettings s;
            s.rateHz = 1.3f;
            s.startPhase = 0.25f;
            lfo->restart(s);
            expectWithinAbsoluteError(lfo->advance(kStep, s, {}), 1.0f, 1.0e-6f); // sine at a quarter cycle
            run(*lfo, s, 0.31);
            lfo->restart(s);
            expectWithinAbsoluteError(lfo->advance(kStep, s, {}), 1.0f, 1.0e-6f);

            s.retrigger = false;
            run(*lfo, s, 0.31);
            const double before = lfo->phaseForTesting();
            lfo->restart(s);
            expectWithinAbsoluteError(lfo->phaseForTesting(), before, 1.0e-12);
        }

        beginTest("the_delay_holds_the_cycle_back_and_the_fade_rises_in_a_straight_line");
        {
            auto lfo = makeLfo();
            LfoSettings s;
            s.shape = LfoShape::square;
            s.rateHz = 0.1f; // the first half cycle (high) lasts five seconds: the value is the depth
            s.delaySeconds = 0.1f;
            s.fadeSeconds = 0.2f;
            lfo->restart(s);
            expectWithinAbsoluteError(run(*lfo, s, 0.05), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(lfo->phaseForTesting(), 0.0, 1.0e-12); // waits for the delay
            expectWithinAbsoluteError(run(*lfo, s, 0.10), 0.25f, 0.01f);      // 0.15 s: a quarter into the fade
            expectWithinAbsoluteError(run(*lfo, s, 0.10), 0.75f, 0.01f);      // 0.25 s
            expectWithinAbsoluteError(run(*lfo, s, 0.10), 1.0f, 1.0e-6f);
            const double phaseAtFullDepth = lfo->phaseForTesting();
            expectWithinAbsoluteError(phaseAtFullDepth, 0.1 * (0.35 - 0.1), 0.001); // ran only after the delay
        }

        beginTest("one_shot_runs_a_single_cycle_and_holds_the_end");
        {
            auto lfo = makeLfo();
            LfoSettings s;
            s.shape = LfoShape::sawUp;
            s.rateHz = 10.0f;
            s.oneShot = true;
            s.retrigger = false; // one-shot restarts the phase anyway
            lfo->restart(s);
            expectWithinAbsoluteError(run(*lfo, s, 0.05), 0.0f, 0.01f);
            run(*lfo, s, 0.2);
            expect(lfo->isFinished());
            expectWithinAbsoluteError(lfo->advance(kStep, s, {}), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError(run(*lfo, s, 1.0), 1.0f, 1.0e-4f);
            lfo->restart(s);
            expectWithinAbsoluteError(lfo->advance(kStep, s, {}), -1.0f, 1.0e-6f);
        }

        beginTest("rate_scale_multiplies_the_rate");
        {
            auto a = makeLfo();
            LfoSettings s;
            s.rateHz = 2.0f;
            a->restart(s);
            for (int i = 0; i < 300; ++i)
                a->advance(kStep, s, {}, 2.0f);
            expectWithinAbsoluteError(a->phaseForTesting(), std::fmod(300.0 * kStep / kVoiceRate * 4.0, 1.0), 1.0e-9);
        }
    }
};

static V3LfoTests v3LfoTests;

class V3LfoPartTests final : public juce::UnitTest
{
public:
    V3LfoPartTests() : juce::UnitTest("V3 LFO in the part", "V3") {}

    void runTest() override
    {
        beginTest("a_voice_carries_its_lfos_as_sources_and_the_wheel_scales_lfo1");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kHostRate, 512, 3);
            auto settings = simplePart();
            settings.voice.lfos[0].shape = LfoShape::square;
            settings.voice.lfos[0].rateHz = 0.05f;
            settings.voice.lfos[1].shape = LfoShape::sawDown;
            settings.voice.lfos[1].rateHz = 0.05f;
            renderPart(*part, settings, 512, { event(Type::noteOn, 60) });
            const int v = activeVoice(*part, 60);
            expect(v >= 0);
            const auto& sources = part->voiceForTesting(v).sourcesForTesting();
            expectWithinAbsoluteError(sources.get(ModSource::lfo1), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(sources.get(ModSource::lfo2), 1.0f, 0.01f);

            settings.voice.wheelToLfo1 = 1.0f;
            renderPart(*part, settings, 256);
            expectWithinAbsoluteError(part->voiceForTesting(v).sourcesForTesting().get(ModSource::lfo1), 0.0f, 1.0e-6f);
            renderPart(*part, settings, 256, { event(Type::modWheel, 0, 0.5f) });
            expectWithinAbsoluteError(part->voiceForTesting(v).sourcesForTesting().get(ModSource::lfo1), 0.5f, 1.0e-6f);
        }

        beginTest("a_retriggered_note_restarts_its_lfo_and_legato_keeps_it");
        {
            for (auto mode : { PlayMode::mono, PlayMode::legato })
            {
                auto part = std::make_unique<Part>();
                part->prepare(kHostRate, 512, 3);
                auto settings = simplePart();
                settings.playMode = mode;
                settings.voice.lfos[0].shape = LfoShape::sawUp;
                settings.voice.lfos[0].rateHz = 0.5f;
                renderPart(*part, settings, 24000, { event(Type::noteOn, 60) }); // half a second: a quarter cycle
                renderPart(*part, settings, 256, { event(Type::noteOn, 64) });
                const int v = activeVoice(*part, 64);
                expect(v >= 0);
                const double phase = part->voiceForTesting(v).lfoForTesting(0).phaseForTesting();
                if (mode == PlayMode::mono)
                    expectLessThan(phase, 0.01, "mono restarts the LFO with the note");
                else
                    expectGreaterThan(phase, 0.24, "legato carries the LFO on");
            }
        }

        beginTest("a_global_lfo_is_shared_and_restarts_only_after_silence");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kHostRate, 512, 3);
            auto settings = simplePart();
            auto& g = settings.globalLfos[0];
            g.sync = false;
            g.rateHz = 0.5f;
            g.shape = LfoShape::sawUp;
            g.retrigger = true;

            renderPart(*part, settings, 12000); // the LFO runs in silence
            expectGreaterThan(part->globalLfoForTesting(0).phaseForTesting(), 0.1);
            renderPart(*part, settings, 256, { event(Type::noteOn, 60) });
            expectLessThan(part->globalLfoForTesting(0).phaseForTesting(), 0.01, "the first note restarts it");
            renderPart(*part, settings, 24000);
            renderPart(*part, settings, 256, { event(Type::noteOn, 67) });
            expectGreaterThan(part->globalLfoForTesting(0).phaseForTesting(), 0.24, "a second note does not");

            const int a = activeVoice(*part, 60);
            const int b = activeVoice(*part, 67);
            expect(a >= 0 && b >= 0 && a != b);
            expectEquals(part->voiceForTesting(a).sourcesForTesting().get(ModSource::glfo1),
                         part->voiceForTesting(b).sourcesForTesting().get(ModSource::glfo1));
            expectGreaterThan(part->voiceForTesting(a).sourcesForTesting().get(ModSource::glfo1), -0.6f);
        }

        beginTest("global_lfo_values_move_inside_a_block");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kHostRate, 512, 3);
            auto settings = simplePart();
            settings.globalLfos[0].sync = false;
            settings.globalLfos[0].rateHz = 40.0f;
            renderPart(*part, settings, 512, { event(Type::noteOn, 60) });
            const int v = activeVoice(*part, 60);
            // 512 samples at 48 kHz is 0.43 cycles at 40 Hz: the last sub-block is near the cycle's
            // value there, not the value at the start of the block.
            const float expected = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 40.0 * (512.0 - 16.0) / kHostRate));
            expectWithinAbsoluteError(part->voiceForTesting(v).sourcesForTesting().get(ModSource::glfo1), expected, 0.02f);
        }

        beginTest("tempo_is_the_hosts_or_the_arp_tempo_and_synced_lfos_lock_to_the_song");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kHostRate, 512, 3);
            auto settings = simplePart();
            settings.arp.internalTempoBpm = 90.0f;
            renderPart(*part, settings, 256);
            expectWithinAbsoluteError(part->tempoForTesting(), 90.0, 1.0e-9);

            coolsynth::synth::EngineTransportInfo transport;
            transport.hostHasTempo = true;
            transport.hostHasPpq = true;
            transport.hostIsPlaying = true;
            transport.hostBpm = 133.0;
            transport.hostPpqAtBlockStart = 8.0;
            settings.globalLfos[0].sync = true;
            settings.globalLfos[0].retrigger = false;
            settings.globalLfos[0].division = 7; // 1/4: one cycle per beat
            renderPart(*part, settings, 4096, {}, transport);
            expectWithinAbsoluteError(part->tempoForTesting(), 133.0, 1.0e-9);
            // The last frame starts one sub-block (16 host samples) before the end of the last block.
            const double ppqOfLastFrame = 8.0 + (4096.0 - 16.0) * 133.0 / 60.0 / kHostRate;
            expectWithinAbsoluteError(part->globalLfoForTesting(0).phaseForTesting(), std::fmod(ppqOfLastFrame, 1.0), 1.0e-6);
        }

        beginTest("bank_lfo_sections_name_lfo_parameters");
        {
            const auto result = coolsynth::presets::parseBankFile(
                "[Wobbly]\nlfo1 = shape=square rate=4.5 fade=300\nglfo2 = sync=on division=1/8T retrigger=off\nvoice = wheelToLfo1=1\n",
                "test.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            expectEquals(static_cast<int>(result.presets.size()), 1);
            juce::StringArray ids;
            for (const auto& e : result.presets.front().entries)
                ids.add(e.parameterId + "=" + e.text);
            expect(ids.contains("pA.lfo1.shape=square"));
            expect(ids.contains("pA.lfo1.fade=300"));
            expect(ids.contains("pA.glfo2.division=1/8T"));
            expect(ids.contains("pA.voice.wheelToLfo1=1"));

            const auto bad = coolsynth::presets::parseBankFile("[X]\nlfo3 = rate=1\nlfo1 = speed=2\n", "bad.csbank");
            expectEquals(bad.errors.size(), 2);
        }
    }
};

static V3LfoPartTests v3LfoPartTests;
