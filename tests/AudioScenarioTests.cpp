// Scenario tests for the audio thread (D161): what a player does, played through the whole processor, each
// block watched by the allocation alarm (tests/AudioThreadAlarm.cpp), checked for broken values, and timed.

#include <algorithm>
#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "diagnostics/AudioThreadAlarm.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

namespace
{
    namespace diag = coolsynth::diag;

    constexpr double sampleRate = 48000.0;
    constexpr int preparedBlock = 256;

    /** A processor, its buffers, and what its blocks did. */
    struct Rig
    {
        std::unique_ptr<SynthAudioProcessor> synth = std::make_unique<SynthAudioProcessor>();
        juce::AudioBuffer<float> buffer { 2, 4096 };
        juce::MidiBuffer midi;
        double worstMs = 0.0;
        int brokenSamples = 0;
        float peak = 0.0f;

        Rig()
        {
            synth->setPlayConfigDetails(0, 2, sampleRate, preparedBlock);
            synth->prepareToPlay(sampleRate, preparedBlock);
        }

        void block(int length)
        {
            buffer.setSize(2, length, false, false, true);
            buffer.clear();
            const double start = juce::Time::getMillisecondCounterHiRes();
            synth->processBlock(buffer, midi);
            worstMs = std::max(worstMs, juce::Time::getMillisecondCounterHiRes() - start);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                {
                    const float v = buffer.getSample(ch, i);
                    if (! std::isfinite(v))
                        ++brokenSamples;
                    else
                        peak = std::max(peak, std::abs(v));
                }
        }

        void play(double seconds, int length = preparedBlock)
        {
            const int blocks = static_cast<int>(std::ceil(seconds * sampleRate / length));
            for (int i = 0; i < blocks; ++i)
                block(length);
        }

        void chord(int root, bool on, int velocity = 100)
        {
            for (const int interval : { 0, 3, 7, 12 })
                midi.addEvent(on ? juce::MidiMessage::noteOn(1, root + interval, static_cast<juce::uint8>(velocity))
                                 : juce::MidiMessage::noteOff(1, root + interval), 0);
        }

        void loadPreset(int index)
        {
            synth->setCurrentProgram(index);
            synth->loadSampledInstrumentsNow(); // the stand-in library: a moment
        }
    };

    juce::String firstSizes()
    {
        juce::String s;
        for (const auto& size : diag::audioAllocations.firstSizes)
            if (const auto v = size.load(); v > 0)
                s << static_cast<juce::int64>(v) << " ";
        return s.trim();
    }
}

class AudioScenarioTests final : public juce::UnitTest
{
public:
    AudioScenarioTests() : juce::UnitTest("Audio scenarios: no allocation, no broken values", "Audio") {}

    void runTest() override
    {
        beginTest("The alarm counts an allocation made inside an audio block, and none outside");
        {
            diag::resetAudioAllocations();
            {
                auto outside = std::make_unique<int>(1);
                juce::ignoreUnused(outside);
            }
            expectEquals(diag::audioAllocations.count.load(), 0);
            {
                const diag::AudioBlockScope block;
                auto inside = std::make_unique<std::array<char, 100>>();
                juce::ignoreUnused(inside);
            }
            expectEquals(diag::audioAllocations.count.load(), 1, "the replaced operator new is the one in use");
            expectEquals(static_cast<int>(diag::audioAllocations.firstSizes[0].load()), 100);
            diag::resetAudioAllocations();
        }

        beginTest("Every preset, a chord, and a switch to the next preset while it rings");
        {
            auto rig = std::make_unique<Rig>();
            const int presets = coolsynth::presets::getFactoryPresetCount();
            rig->loadPreset(0);
            rig->play(0.1); // first blocks after prepare
            diag::resetAudioAllocations();
            for (int i = 0; i < presets; ++i)
            {
                rig->loadPreset(i);
                rig->chord(57, true);
                rig->play(0.25);
                rig->loadPreset((i + 1) % presets); // the chord still held
                rig->play(0.15);
                rig->chord(57, false);
                rig->play(0.15);
            }
            expectEquals(diag::audioAllocations.count.load(), 0, "allocations in audio blocks, first sizes: " + firstSizes());
            expectEquals(rig->brokenSamples, 0, "NaN or infinity in the output");
            logMessage("worst block " + juce::String(rig->worstMs, 2) + " ms of " + juce::String(1000.0 * preparedBlock / sampleRate, 2)
                       + " ms over " + juce::String(presets) + " switches; peak " + juce::String(juce::Decibels::gainToDecibels(rig->peak), 1) + " dB");
        }

        beginTest("All Sound Off and panic while chords and effects ring");
        {
            auto rig = std::make_unique<Rig>();
            rig->loadPreset(0);
            rig->play(0.1);
            diag::resetAudioAllocations();
            rig->chord(60, true);
            rig->play(0.3);
            rig->worstMs = 0.0;
            rig->midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            rig->block(preparedBlock);
            const double soundOffMs = rig->worstMs;
            rig->chord(60, true);
            rig->play(0.2);
            rig->worstMs = 0.0;
            rig->synth->requestPanic();
            rig->block(preparedBlock);
            expectEquals(diag::audioAllocations.count.load(), 0, "allocations, first sizes: " + firstSizes());
            expectEquals(rig->brokenSamples, 0);
            logMessage("All Sound Off block " + juce::String(soundOffMs, 3) + " ms, panic block " + juce::String(rig->worstMs, 3) + " ms");
        }

        beginTest("Thirty-two notes in one block, on every preset");
        {
            auto rig = std::make_unique<Rig>();
            rig->loadPreset(0);
            rig->play(0.1);
            diag::resetAudioAllocations();
            rig->worstMs = 0.0;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            {
                rig->loadPreset(i);
                for (int note = 40; note < 72; ++note)
                    rig->midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(110)), 0);
                rig->play(0.1);
                rig->midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                rig->play(0.05);
            }
            expectEquals(diag::audioAllocations.count.load(), 0, "allocations, first sizes: " + firstSizes());
            expectEquals(rig->brokenSamples, 0);
            logMessage("worst block with 32 note-ons " + juce::String(rig->worstMs, 2) + " ms");
        }

        beginTest("Blocks of 1, 37 and 129 samples, and blocks longer than prepared");
        {
            auto rig = std::make_unique<Rig>();
            rig->loadPreset(0);
            rig->play(0.1);
            diag::resetAudioAllocations();
            for (const int length : { 1, 37, 129, preparedBlock, 1024, 4096 })
            {
                rig->chord(60, true);
                rig->play(0.1, length);
                rig->chord(60, false);
                rig->play(0.05, length);
            }
            expectEquals(diag::audioAllocations.count.load(), 0, "allocations, first sizes: " + firstSizes());
            expectEquals(rig->brokenSamples, 0);
            expectGreaterThan(rig->peak, 0.001f, "it played");
        }
    }
};

static AudioScenarioTests audioScenarioTests;
