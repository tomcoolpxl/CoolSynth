// Earlier sounds ring on (decision D158): a preset change while notes sound, played through the processor.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

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
        const int saw = presetNamed("Init Saw"), fm = presetNamed("Init FM"), sine = presetNamed("Init Sine"),
                  pulse = presetNamed("Init Pulse"), bass = presetNamed("Bedroom Bass");

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
