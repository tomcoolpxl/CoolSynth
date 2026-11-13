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
    using Event = coolsynth::synth::EngineMidiEvent;

    constexpr double kVoiceRate = 96000.0;
    constexpr double kHostRate = 48000.0;
    constexpr int kBlock = 256;

    PartSettings quietPart()
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
        Envelope::Parameters genv;
        genv.attack = 0.1f;
        genv.attackCurve = 0.0f;
        genv.decay = 0.05f;
        genv.sustain = 0.5f;
        genv.release = 0.1f;
        s.globalEnvelope = genv;
        return s;
    }

    Event event(Type type, int note = 0, float value = 1.0f)
    {
        Event e;
        e.type = type;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }

    std::unique_ptr<Part> makePart()
    {
        auto part = std::make_unique<Part>();
        part->prepare(kHostRate, 512, 5);
        return part;
    }

    /** Renders whole blocks for about `seconds`, the events in the first block. */
    void run(Part& part, const PartSettings& settings, double seconds, std::vector<Event> events = {})
    {
        std::vector<float> l(kBlock), r(kBlock);
        const int blocks = std::max(1, static_cast<int>(std::lround(seconds * kHostRate / kBlock)));
        for (int b = 0; b < blocks; ++b)
        {
            const auto span = b == 0 ? std::span<const Event>(events) : std::span<const Event>();
            part.render(l.data(), r.data(), kBlock, span, settings, {});
        }
    }

    int voiceFor(const Part& part, int note)
    {
        for (int i = 0; i < Part::maxVoices; ++i)
            if (part.voiceForTesting(i).isActive() && part.voiceForTesting(i).currentNote() == note)
                return i;
        return -1;
    }
}

class V3StepAndChaosTests final : public juce::UnitTest
{
public:
    V3StepAndChaosTests() : juce::UnitTest("V3 step modulator and chaos", "V3") {}

    void runTest() override
    {
        beginTest("steps_play_at_their_division_and_wrap_at_the_length");
        {
            StepSettings s;
            for (int i = 0; i < stepCount; ++i)
                s.values[static_cast<std::size_t>(i)] = -1.0f + 2.0f * static_cast<float>(i) / 15.0f;
            s.division = 13; // 1/16: an eighth of a second at 120 BPM
            auto step = std::make_unique<StepModulator>();
            step->prepare(kVoiceRate);
            LfoClock clock;
            clock.bpm = 120.0;
            const int stepSamples = static_cast<int>(kVoiceRate / 8.0);
            for (int k = 0; k < 20; ++k)
            {
                float value = 0.0f;
                for (int n = 0; n < stepSamples; n += 32)
                {
                    const float v = step->advance(32, s, clock);
                    if (n == 64)
                        value = v; // well inside step k
                }
                expectWithinAbsoluteError(value, s.values[static_cast<std::size_t>(k % stepCount)], 1.0e-6f, "step " + juce::String(k));
            }

            s.length = 4;
            expectWithinAbsoluteError(StepModulator::valueAt(5.5, s), s.values[1], 1.0e-6f);
            expectWithinAbsoluteError(StepModulator::valueAt(4.0, s), s.values[0], 1.0e-6f);
        }

        beginTest("a_slide_glides_from_the_previous_step_over_its_share");
        {
            StepSettings s;
            s.values[0] = 0.0f;
            s.values[1] = 1.0f;
            s.slides[1] = true;
            s.glide = 0.5f;
            s.length = 2;
            expectWithinAbsoluteError(StepModulator::valueAt(1.0, s), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(StepModulator::valueAt(1.25, s), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(StepModulator::valueAt(1.6, s), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(StepModulator::valueAt(0.5, s), 0.0f, 1.0e-6f); // step 0 does not slide
        }

        beginTest("without_retrigger_the_steps_follow_the_song_position");
        {
            StepSettings s;
            for (int i = 0; i < stepCount; ++i)
                s.values[static_cast<std::size_t>(i)] = static_cast<float>(i) / 16.0f;
            s.retrigger = false;
            auto step = std::make_unique<StepModulator>();
            step->prepare(kVoiceRate);
            LfoClock clock;
            clock.playing = true;
            clock.ppq = 3.3; // 13.2 sixteenths
            expectWithinAbsoluteError(step->advance(32, s, clock), 13.0f / 16.0f, 1.0e-6f);
            clock.ppq = 4.01; // the next bar's first sixteenth
            expectWithinAbsoluteError(step->advance(32, s, clock), 0.0f, 1.0e-6f);
        }

        beginTest("chaos_wanders_inside_its_range_without_corners");
        {
            auto chaos = std::make_unique<ChaosSource>();
            chaos->prepare(kVoiceRate, 11);
            float low = 1.0f, high = -1.0f, previous = 0.0f, largestStep = 0.0f;
            double sum = 0.0, sumSquares = 0.0;
            const int spans = static_cast<int>(60.0 * kVoiceRate / 32.0);
            for (int i = 0; i < spans; ++i)
            {
                const float v = chaos->advance(32, 1.0f);
                low = std::min(low, v);
                high = std::max(high, v);
                largestStep = std::max(largestStep, std::abs(v - previous));
                previous = v;
                sum += v;
                sumSquares += static_cast<double>(v) * v;
            }
            const double mean = sum / spans;
            const double deviation = std::sqrt(std::max(0.0, sumSquares / spans - mean * mean));
            expect(low >= -1.0f && high <= 1.0f);
            expectGreaterThan(deviation, 0.15, "it moves");
            expectLessThan(largestStep, 0.005f, "it moves smoothly");
        }
    }
};

static V3StepAndChaosTests v3StepAndChaosTests;

class V3GlobalModulatorTests final : public juce::UnitTest
{
public:
    V3GlobalModulatorTests() : juce::UnitTest("V3 global modulators in the part", "V3") {}

    void runTest() override
    {
        beginTest("first_note_trigger_opens_once_per_phrase_and_every_note_restarts");
        {
            for (auto trigger : { GenvTrigger::firstNote, GenvTrigger::everyNote })
            {
                auto part = makePart();
                auto settings = quietPart();
                settings.globalEnvelopeTrigger = trigger;
                run(*part, settings, 0.05, { event(Type::noteOn, 60) });
                expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), 0.5f, 0.03f); // halfway up a 100 ms attack
                run(*part, settings, 0.25);
                expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), 0.5f, 0.01f); // at sustain
                run(*part, settings, 0.03, { event(Type::noteOn, 64) });
                if (trigger == GenvTrigger::firstNote)
                    expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), 0.5f, 0.01f, "a second key is not a new phrase");
                else
                    expectGreaterThan(part->globalEnvelopeForTesting().getValue(), 0.6f, "every key restarts the attack from where it is");

                run(*part, settings, 0.01, { event(Type::noteOff, 60) });
                expect(part->globalEnvelopeForTesting().getStage() != Envelope::Stage::release, "a key is still down");
                run(*part, settings, 0.01, { event(Type::noteOff, 64) });
                expect(part->globalEnvelopeForTesting().getStage() == Envelope::Stage::release);
            }
        }

        beginTest("the_pedal_holds_the_global_envelope_open");
        {
            auto part = makePart();
            auto settings = quietPart();
            run(*part, settings, 0.2, { event(Type::noteOn, 60), event(Type::sustainPedal, 0, 1.0f) });
            run(*part, settings, 0.05, { event(Type::noteOff, 60) });
            expect(part->globalEnvelopeForTesting().getStage() != Envelope::Stage::release);
            run(*part, settings, 0.01, { event(Type::sustainPedal, 0, 0.0f) });
            expect(part->globalEnvelopeForTesting().getStage() == Envelope::Stage::release);
        }

        beginTest("synced_global_envelope_times_follow_the_tempo");
        {
            auto part = makePart();
            auto settings = quietPart();
            settings.globalEnvelope.attack = 1.0f; // half a bar at 120 BPM
            settings.globalEnvelope.sustain = 1.0f;
            settings.globalEnvelopeSync = true;
            settings.arp.internalTempoBpm = 60.0f; // so it lasts two seconds
            run(*part, settings, 1.0, { event(Type::noteOn, 60) });
            expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), 0.5f, 0.02f);
            run(*part, settings, 1.05);
            expectWithinAbsoluteError(part->globalEnvelopeForTesting().getValue(), 1.0f, 1.0e-4f);
        }

        beginTest("global_sources_reach_the_voices_and_macros_are_smoothed");
        {
            auto part = makePart();
            auto settings = quietPart();
            settings.macros = { 0.8f, 0.0f, 0.3f, 1.0f };
            settings.step.values[0] = 0.75f;
            run(*part, settings, 0.03, { event(Type::noteOn, 60) });
            const int v = voiceFor(*part, 60);
            expect(v >= 0);
            const auto& sources = part->voiceForTesting(v).sourcesForTesting();
            expectWithinAbsoluteError(sources.get(ModSource::macro1), 0.8f, 1.0e-4f, "the first block starts at the setting");
            expectWithinAbsoluteError(sources.get(ModSource::macro4), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError(sources.get(ModSource::step), 0.75f, 1.0e-6f);
            expectWithinAbsoluteError(sources.get(ModSource::genv), part->globalEnvelopeForTesting().getValue(), 0.02f);
            expect(sources.get(ModSource::chaos) >= -1.0f && sources.get(ModSource::chaos) <= 1.0f);

            settings.macros[0] = 0.2f;
            run(*part, settings, 0.005);
            const float moving = part->voiceForTesting(v).sourcesForTesting().get(ModSource::macro1);
            expect(moving > 0.25f && moving < 0.75f, "a macro jump is smoothed, got " + juce::String(moving));
            run(*part, settings, 0.1);
            expectWithinAbsoluteError(part->voiceForTesting(v).sourcesForTesting().get(ModSource::macro1), 0.2f, 1.0e-3f);
        }

        beginTest("an_envelope_time_route_stretches_the_note_it_starts");
        {
            for (float amount : { 0.0f, 0.25f })
            {
                auto part = makePart();
                auto settings = quietPart();
                settings.voice.envelopes[0].attack = 0.010f;
                settings.voice.envelopes[0].attackCurve = 0.0f;
                MatrixSlot slot;
                slot.source = ModSource::velocity;
                slot.destination = modDestination::envelopeTimeBase; // ENV1 time
                slot.amount = amount;                                 // velocity 1: one octave, twice as long
                settings.voice.matrix.add(slot);
                std::vector<float> l(480), r(480);
                const std::vector<Event> on { event(Type::noteOn, 60) };
                part->render(l.data(), r.data(), 480, on, settings, {}); // 10 ms
                const int v = voiceFor(*part, 60);
                const float level = part->voiceForTesting(v).ampLevel();
                if (amount == 0.0f)
                    expectGreaterThan(level, 0.97f, "a 10 ms attack is done after 10 ms");
                else
                    expectWithinAbsoluteError(level, 0.5f, 0.03f, "stretched to 20 ms it is halfway");
            }
        }

        beginTest("bank_global_modulator_sections");
        {
            const auto result = coolsynth::presets::parseBankFile(
                "[Global]\n"
                "genv = attack=8bars decay=2beats sustain=0.7 sync=on trigger=everyNote\n"
                "step = values=1,0.5,-0.25,0 slides=0,1 division=1/8 length=4 glide=0.3 s12=0.9 slide16=on\n"
                "macros = macro1=0.5 macro4=1\n"
                "chaos = rate=0.2\n"
                "mod = src=step dst=env2.time amount=0.1\n",
                "global.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "pA.genv.attack=16000", "pA.genv.decay=1000", "pA.genv.sync=on", "pA.genv.trigger=everyNote",
                                        "pA.step.s1=1", "pA.step.s3=-0.25", "pA.step.slide1=off", "pA.step.slide2=on",
                                        "pA.step.division=1/8", "pA.step.length=4", "pA.step.s12=0.9", "pA.step.slide16=on",
                                        "pA.macro1=0.5", "pA.macro4=1", "pA.chaos.rate=0.2", "pA.mod1.src=Step", "pA.mod1.dst=Env 2 Time" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted + " in " + entries.joinIntoString(", "));

            const auto bad = coolsynth::presets::parseBankFile(
                "[Bad]\ngenv = speed=1\nstep = s17=1\nmacros = macro5=1\nchaos = speed=1\ngenv = attack=xbars\nstep = values=\n",
                "bad.csbank");
            expectEquals(bad.errors.size(), 6, bad.errors.joinIntoString("; "));
        }
    }
};

static V3GlobalModulatorTests v3GlobalModulatorTests;
