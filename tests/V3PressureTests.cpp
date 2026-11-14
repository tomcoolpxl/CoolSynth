#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "plugin/SynthAudioProcessor.h"
#include "synth/v3/EngineV3.h"

/*
    Key pressure and the expression pedal as modulation sources (decision D136): the whole keyboard's
    pressure (channel aftertouch) and each key's own (polyphonic aftertouch) become the Aftertouch
    source of every voice, the larger of the two, smoothed over about 10 ms; CC 11 becomes the
    Expression source. Heap parts and processors: the test runner's stack frame is small in Debug.
*/
namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    PartSettings sinePart()
    {
        PartSettings s;
        auto& g = s.voice.generators[0];
        g.type = generatorType::va;
        g.p[VaGenerator::shape] = unitsToNormalised(VaGenerator::descriptor().params[VaGenerator::shape], 0.0f);
        g.level = 1.0f;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    EngineMidiEvent event(Type type, int note = 0, float value = 1.0f)
    {
        EngineMidiEvent e;
        e.type = type;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }

    std::vector<float> render(Part& part, const PartSettings& settings, int samples, std::vector<EngineMidiEvent> events = {})
    {
        std::vector<float> left(static_cast<std::size_t>(samples)), right(static_cast<std::size_t>(samples));
        for (int start = 0; start < samples; start += kBlock)
        {
            const int n = std::min(kBlock, samples - start);
            const auto span = start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>();
            part.render(left.data() + start, right.data() + start, n, span, settings, {});
        }
        return left;
    }

    /** A source's value on the voice playing `note`, or -1 when no voice plays it. */
    float sourceOn(const Part& part, int note, ModSource source)
    {
        for (int i = 0; i < Part::maxVoices; ++i)
        {
            const auto& v = part.voiceForTesting(i);
            if (v.isActive() && v.currentNote() == note)
                return v.sourcesForTesting().get(source);
        }
        return -1.0f;
    }

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }
}

class V3PressureTests final : public juce::UnitTest
{
public:
    V3PressureTests() : juce::UnitTest("V3 key pressure and expression", "CoolSynth") {}

    void runTest() override
    {
        beginTest("the_keyboards_pressure_reaches_every_voice_smoothed");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            const auto settings = sinePart();
            render(*part, settings, 2400, { event(Type::noteOn, 60), event(Type::noteOn, 64) });
            expectEquals(sourceOn(*part, 60, ModSource::aftertouch), 0.0f);

            render(*part, settings, 48, { event(Type::channelPressure, 0, 1.0f) });
            const float early = sourceOn(*part, 64, ModSource::aftertouch);
            expectGreaterThan(early, 0.02f, "it moves at once");
            expectLessThan(early, 0.3f, "but does not jump: 1 ms into a 10 ms smoothing");

            render(*part, settings, 4800);
            expectGreaterThan(sourceOn(*part, 60, ModSource::aftertouch), 0.99f);
            expectGreaterThan(sourceOn(*part, 64, ModSource::aftertouch), 0.99f);
        }

        beginTest("a_keys_own_pressure_reaches_only_that_key_and_the_larger_pressure_counts");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            const auto settings = sinePart();
            render(*part, settings, 2400, { event(Type::noteOn, 60), event(Type::noteOn, 64) });
            render(*part, settings, 4800, { event(Type::polyPressure, 64, 0.8f) });
            expectWithinAbsoluteError(sourceOn(*part, 64, ModSource::aftertouch), 0.8f, 0.005f);
            expectWithinAbsoluteError(sourceOn(*part, 60, ModSource::aftertouch), 0.0f, 1.0e-6f, "the other key has none");

            render(*part, settings, 4800, { event(Type::channelPressure, 0, 0.3f) });
            expectWithinAbsoluteError(sourceOn(*part, 64, ModSource::aftertouch), 0.8f, 0.005f, "its own pressure is larger");
            expectWithinAbsoluteError(sourceOn(*part, 60, ModSource::aftertouch), 0.3f, 0.005f, "the keyboard's is larger");
        }

        beginTest("a_note_struck_while_pressing_starts_at_that_pressure");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            const auto settings = sinePart();
            render(*part, settings, 2400, { event(Type::channelPressure, 0, 0.9f), event(Type::expression, 0, 0.4f) });
            render(*part, settings, 64, { event(Type::noteOn, 67) });
            expectWithinAbsoluteError(sourceOn(*part, 67, ModSource::aftertouch), 0.9f, 1.0e-4f, "no ramp from nothing");
            expectWithinAbsoluteError(sourceOn(*part, 67, ModSource::expression), 0.4f, 1.0e-4f);
        }

        beginTest("the_expression_pedal_reaches_every_voice_and_reset_controllers_clears_both");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            const auto settings = sinePart();
            render(*part, settings, 2400, { event(Type::noteOn, 60), event(Type::noteOn, 64) });
            expectEquals(sourceOn(*part, 60, ModSource::expression), 0.0f, "nothing until the pedal moves");
            render(*part, settings, 4800, { event(Type::expression, 0, 0.6f), event(Type::polyPressure, 60, 0.7f) });
            expectWithinAbsoluteError(sourceOn(*part, 64, ModSource::expression), 0.6f, 0.005f);
            render(*part, settings, 4800, { event(Type::resetControllers) });
            expectWithinAbsoluteError(sourceOn(*part, 60, ModSource::expression), 0.0f, 0.005f);
            expectWithinAbsoluteError(sourceOn(*part, 60, ModSource::aftertouch), 0.0f, 0.005f);
        }

        beginTest("a_chord_from_one_key_takes_that_keys_pressure_on_every_note");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            auto settings = sinePart();
            settings.chord.type = chordType::major;
            render(*part, settings, 2400, { event(Type::noteOn, 60) });
            render(*part, settings, 4800, { event(Type::polyPressure, 60, 0.5f) });
            for (const int note : { 60, 64, 67 })
                expectWithinAbsoluteError(sourceOn(*part, note, ModSource::aftertouch), 0.5f, 0.005f, juce::String(note));
        }

        beginTest("pressure_shapes_the_sound_through_the_matrix_without_a_click");
        {
            auto part = std::make_unique<Part>();
            part->prepare(kRate, 512, 1);
            auto settings = sinePart();
            settings.voice.matrix.add({ ModSource::aftertouch, modDestination::vca, -1.0f }); // full pressure: silent
            const auto before = render(*part, settings, 4800, { event(Type::noteOn, 69) });
            const auto after = render(*part, settings, 9600, { event(Type::channelPressure, 0, 1.0f) });
            const double held = rms(before, 2400, 4800);
            expectGreaterThan(held, 0.05);
            expectLessThan(rms(after, 7200, 9600), 0.01 * held, "pressed all the way: silent");
            expectGreaterThan(rms(after, 0, 48), 0.5 * held, "the first millisecond fades rather than drops");
        }

        beginTest("a_keys_pressure_follows_the_parts_transpose");
        {
            auto engine = std::make_unique<EngineV3>();
            engine->prepare(kRate, kBlock, 2);
            EngineSettings settings;
            settings.parts[0] = sinePart();
            settings.mix[0].transpose = 12;
            juce::AudioBuffer<float> buffer(2, kBlock);
            const std::vector<EngineMidiEvent> down { event(Type::noteOn, 60) };
            engine->render(buffer, down, settings);
            const std::vector<EngineMidiEvent> press { event(Type::polyPressure, 60, 0.6f) };
            engine->render(buffer, press, settings);
            for (int i = 0; i < 30; ++i)
                engine->render(buffer, {}, settings);
            expectWithinAbsoluteError(sourceOn(engine->partForTesting(0), 72, ModSource::aftertouch), 0.6f, 0.005f,
                                      "key 60 plays note 72 on this part");
        }
    }
};

static V3PressureTests v3PressureTests;

/** The plugin reads channel pressure, polyphonic aftertouch and CC 11 from MIDI. */
class V3PressureMidiTests final : public juce::UnitTest
{
public:
    V3PressureMidiTests() : juce::UnitTest("V3 key pressure from MIDI", "CoolSynth") {}

    void runTest() override
    {
        beginTest("the_plugin_turns_pressure_and_cc11_into_the_sources");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            processor->prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(2, kBlock);
            auto play = [&](std::initializer_list<juce::MidiMessage> messages, int blocks)
            {
                for (int b = 0; b < blocks; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b == 0)
                        for (const auto& m : messages)
                            midi.addEvent(m, 0);
                    buffer.clear();
                    processor->processBlock(buffer, midi);
                }
            };
            play({ juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)) }, 4);
            play({ juce::MidiMessage::aftertouchChange(1, 60, 64) }, 20);
            expectWithinAbsoluteError(sourceOn(processor->getPartForTesting(), 60, ModSource::aftertouch), 64.0f / 127.0f, 0.005f,
                                      "polyphonic aftertouch");
            play({ juce::MidiMessage::channelPressureChange(1, 127), juce::MidiMessage::controllerEvent(1, 11, 32) }, 20);
            expectWithinAbsoluteError(sourceOn(processor->getPartForTesting(), 60, ModSource::aftertouch), 1.0f, 0.005f,
                                      "channel pressure");
            expectWithinAbsoluteError(sourceOn(processor->getPartForTesting(), 60, ModSource::expression), 32.0f / 127.0f, 0.005f,
                                      "CC 11");
        }
    }
};

static V3PressureMidiTests v3PressureMidiTests;
