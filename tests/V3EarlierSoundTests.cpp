// Earlier sounds ring on (decision D158): a preset change while notes sound, played through the processor.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/AudioWorkers.h"

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    int presetNamed(const juce::String& name)
    {
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            if (coolsynth::presets::getFactoryPreset(i).name == name)
                return i;
        return -1;
    }

    struct Player
    {
        std::unique_ptr<SynthAudioProcessor> synth = std::make_unique<SynthAudioProcessor>();
        juce::AudioBuffer<float> buffer { 2, blockSize };
        juce::MidiBuffer midi;

        Player()
        {
            synth->setPlayConfigDetails(0, 2, rate, blockSize);
            synth->prepareToPlay(rate, blockSize);
        }
        coolsynth::synth::v3::EngineV3& engine() { return synth->engineForTesting(); }
        void play(double seconds)
        {
            for (int i = 0; i < static_cast<int>(std::ceil(seconds * rate / blockSize)); ++i)
            {
                buffer.clear();
                synth->processBlock(buffer, midi);
                midi.clear();
            }
        }
        void keys(std::initializer_list<int> notes, bool down)
        {
            for (const int note : notes)
                midi.addEvent(down ? juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)) : juce::MidiMessage::noteOff(1, note), 0);
        }
        void pedal(bool down) { midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, down ? 127 : 0), 0); }
        int voicesIn(int unit) { return engine().unitForTesting(unit).activeVoiceCount(); }
    };
}

class EarlierSoundTests final : public juce::UnitTest
{
public:
    EarlierSoundTests() : juce::UnitTest("Earlier sounds ring on after a preset change", "V3") {}

    void runTest() override
    {
        const int saw = presetNamed("Basic Saw"), fm = presetNamed("Basic FM"), sine = presetNamed("Basic Sine"),
                  pulse = presetNamed("Basic Pulse"), bass = presetNamed("Bedroom Bass");

        beginTest("A chord held through a preset change keeps its sound; a new note takes the new one; key-up ends it");
        {
            expect(saw >= 0 && fm >= 0 && sine >= 0 && pulse >= 0 && bass >= 0, "the presets these tests use");
            if (saw < 0 || fm < 0 || sine < 0 || pulse < 0 || bass < 0)
                return;
            auto p = std::make_unique<Player>();
            p->synth->setCurrentProgram(saw);
            p->keys({ 60, 64, 67 }, true);
            p->play(0.2);
            const int before = p->engine().currentUnitForTesting();
            expectEquals(p->voicesIn(before), 3);

            p->synth->setCurrentProgram(fm);
            p->play(0.05);
            expectEquals(p->engine().earlierSoundsRinging(), 1, "the saw chord rings on");
            const int now = p->engine().currentUnitForTesting();
            expect(now != before, "new notes go to another unit");
            expectEquals(p->voicesIn(before), 3, "the chord's voices are still the saw's");
            expectEquals(p->engine().unitForTesting(before).part(0).voiceForTesting(0).generatorTypeForTesting(0),
                         coolsynth::synth::v3::generatorType::va, "an old voice keeps its VA generator");

            p->keys({ 72 }, true);
            p->play(0.05);
            expectEquals(p->voicesIn(now), 1, "the new note plays the new sound");
            expectEquals(p->voicesIn(before), 3);

            p->keys({ 60, 64, 67 }, false); // the releases reach the saw
            p->play(1.0);
            expectEquals(p->engine().earlierSoundsRinging(), 0, "released and silent, the saw ends");
            expectEquals(p->voicesIn(now), 1, "the new note goes on");
        }

        beginTest("The same preset loaded again keeps no earlier sound");
        {
            auto p = std::make_unique<Player>();
            p->synth->setCurrentProgram(saw);
            p->keys({ 60 }, true);
            p->play(0.1);
            p->synth->setCurrentProgram(saw);
            p->play(0.05);
            expectEquals(p->engine().earlierSoundsRinging(), 0);
        }

        beginTest("The pedal holds an earlier sound; once it is up the sound ends");
        {
            auto p = std::make_unique<Player>();
            p->synth->setCurrentProgram(saw);
            p->keys({ 60, 64 }, true);
            p->pedal(true);
            p->play(0.1);
            p->keys({ 60, 64 }, false); // held by the pedal
            p->play(0.1);
            p->synth->setCurrentProgram(fm);
            p->play(1.0);
            expectEquals(p->engine().earlierSoundsRinging(), 1, "the pedal holds it");
            p->keys({ 67 }, true); // the new sound, with the pedal still down
            p->play(0.1);
            p->pedal(false);
            p->play(1.0);
            expectEquals(p->engine().earlierSoundsRinging(), 0, "pedal up: it releases and ends");
        }

        beginTest("A third earlier sound makes the oldest fade; All Sound Off ends them all");
        {
            auto p = std::make_unique<Player>();
            p->synth->setCurrentProgram(saw);
            p->keys({ 48 }, true);
            p->play(0.1);
            p->synth->setCurrentProgram(fm);
            p->keys({ 52 }, true);
            p->play(0.1);
            p->synth->setCurrentProgram(sine);
            p->keys({ 55 }, true);
            p->play(0.1);
            expectEquals(p->engine().earlierSoundsRinging(), 2);
            p->synth->setCurrentProgram(pulse);
            p->play(0.05);
            expectEquals(p->engine().earlierSoundsRinging(), 3, "two ring, the oldest fades");
            p->play(0.6);
            expectEquals(p->engine().earlierSoundsRinging(), 2, "the oldest has faded out");
            p->midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            p->play(0.05);
            expectEquals(p->engine().earlierSoundsRinging(), 0);
        }

        beginTest("A preset in another play mode leaves the held chord held");
        {
            auto p = std::make_unique<Player>();
            p->synth->setCurrentProgram(saw);
            p->keys({ 60, 64, 67 }, true);
            p->play(0.2);
            const int before = p->engine().currentUnitForTesting();
            p->synth->setCurrentProgram(bass); // mono
            p->play(0.5);
            expectEquals(p->voicesIn(before), 3, "the poly chord is not released by the mono preset");
        }
    }
};

static EarlierSoundTests earlierSoundTests;

// ---- Several cores, the same sound (D164) ------------------------------------------------------------------

class MultiCoreTests final : public juce::UnitTest
{
public:
    MultiCoreTests() : juce::UnitTest("Overlapping sounds on several cores", "V3") {}

    void runTest() override
    {
        beginTest("Three sounds ringing together render the same, bit for bit, on one core and on several");
        {
            auto render = [](bool multiCore)
            {
                auto p = std::make_unique<Player>();
                p->synth->setMultiCore(multiCore);
                juce::AudioBuffer<float> all(2, static_cast<int>(rate * 2.0));
                all.clear();
                int at = 0;
                auto playInto = [&](double seconds)
                {
                    for (int i = 0; i < static_cast<int>(std::ceil(seconds * rate / blockSize)); ++i)
                    {
                        p->buffer.clear();
                        p->synth->processBlock(p->buffer, p->midi);
                        p->midi.clear();
                        if (at + blockSize <= all.getNumSamples())
                            for (int ch = 0; ch < 2; ++ch)
                                all.copyFrom(ch, at, p->buffer, ch, 0, blockSize);
                        at += blockSize;
                    }
                };
                p->synth->setCurrentProgram(presetNamed("Basic Saw"));
                p->keys({ 48, 52 }, true);
                playInto(0.3);
                p->synth->setCurrentProgram(presetNamed("Basic FM"));
                p->keys({ 55, 59 }, true);
                playInto(0.3);
                p->synth->setCurrentProgram(presetNamed("Juno Pad"));
                p->keys({ 62, 65 }, true);
                playInto(1.0);
                return all;
            };
            const auto serial = render(false);
            const auto parallel = render(true);
            int different = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < serial.getNumSamples(); ++i)
                    different += serial.getSample(ch, i) != parallel.getSample(ch, i) ? 1 : 0;
            expectEquals(different, 0, "samples that differ");
            expectGreaterThan(serial.getMagnitude(0, 0, serial.getNumSamples()), 0.01f, "it played");
            logMessage("audio workers: " + juce::String(coolsynth::synth::v3::AudioWorkers::instance().workerCount()) + "; "
                       + coolsynth::synth::v3::AudioWorkers::instance().cpu().describe());
        }

        beginTest("Each second, every thread's work is counted: the audio thread's blocks and each helper's tasks (D172)");
        {
            auto& pool = coolsynth::synth::v3::AudioWorkers::instance();
            auto p = std::make_unique<Player>();
            p->synth->setMultiCore(true);
            juce::ignoreUnused(pool.takeActivity()); // from here on
            const auto play = [&](double seconds)
            {
                for (int i = 0; i < static_cast<int>(std::ceil(seconds * rate / blockSize)); ++i)
                {
                    p->buffer.clear();
                    p->synth->processBlock(p->buffer, p->midi);
                    p->midi.clear();
                }
            };
            // Two sounds ringing together for over a second: their units are shared out.
            p->synth->setCurrentProgram(presetNamed("Basic Saw"));
            p->keys({ 48, 52 }, true);
            play(0.2);
            p->synth->setCurrentProgram(presetNamed("Juno Pad"));
            p->keys({ 62, 65 }, true);
            play(1.2);
            const auto load = p->synth->cpuLoad();
            expectEquals(load.threads, 1 + pool.workerCount());
            expectGreaterThan(load.percent[0], 0.0f, "the audio thread worked");
            expectLessThan(load.percent[0], 1000.0f);
            expectGreaterThan(load.worstBlock, 0.0f);
            juce::String shares;
            for (int t = 0; t < load.threads; ++t)
                shares << juce::String(load.percent[static_cast<std::size_t>(t)], 2) << "% ";
            logMessage("last second, per thread (audio thread first): " + shares + "; worst block " + juce::String(100.0f * load.worstBlock, 1) + "%");
            if (pool.workerCount() > 0)
            {
                float helpers = 0.0f;
                for (int t = 1; t < load.threads; ++t)
                    helpers += load.percent[static_cast<std::size_t>(t)];
                expectGreaterThan(helpers, 0.0f, "a helper rendered a sound");
            }
        }
    }
};

static MultiCoreTests multiCoreTests;
