// Determinism (D222, docs/research/determinism-and-budget.md): the same input gives the same output, every preset.
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/Arpeggiator.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<SynthAudioProcessor> makeSynth(bool offline)
    {
        auto synth = std::make_unique<SynthAudioProcessor>();
        synth->setNonRealtime(offline);
        synth->ignoreLoadForTesting(true); // the sound, not how busy the computer is (the emergency, D224)
        synth->setPlayConfigDetails(0, 2, sampleRate, block);
        synth->prepareToPlay(sampleRate, block);
        return synth;
    }

    /** A preset played the same way every time: a chord held 0.75 s, then let go for 0.5 s; both channels. */
    std::vector<float> render(SynthAudioProcessor& synth, int preset)
    {
        synth.setCurrentProgram(preset);
        synth.loadSampledInstrumentsNow();
        std::vector<float> out;
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        const int blocks = static_cast<int>(1.25 * sampleRate / block);
        const int release = static_cast<int>(0.75 * sampleRate / block);
        for (int b = 0; b < blocks; ++b)
        {
            midi.clear();
            for (const int note : { 48, 55, 60, 63 })
            {
                if (b == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 17);
                else if (b == release)
                    midi.addEvent(juce::MidiMessage::noteOff(1, note), 5);
            }
            buffer.clear();
            synth.processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.insert(out.end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + block);
        }
        return out;
    }

    bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }
}

class DeterminismTests final : public juce::UnitTest
{
public:
    DeterminismTests() : juce::UnitTest("Determinism", "Audio") {}

    void runTest() override
    {
        beginTest("a random arp plays the same notes after a defined start, unless New Patterns is on");
        {
            // The notes of 100 blocks, then the same after a reseed (a reset, the transport starting).
            const auto playTwice = [](bool newPatterns)
            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(sampleRate, 0x1234u);
                coolsynth::synth::ArpParametersV2 p;
                p.enabled = true;
                p.pattern = coolsynth::parameters::ArpPatternChoice::random;
                p.octaveRange = 3;
                p.newPatterns = newPatterns;
                arp.setParameters(p);
                for (const int note : { 60, 63, 67, 70 })
                    arp.onNoteOn(note, 0.8f);
                const auto notes = [&arp]
                {
                    std::vector<int> played;
                    std::array<coolsynth::synth::EngineMidiEvent, coolsynth::synth::maxArpEventsPerBlock> events {};
                    for (int b = 0; b < 1200 && played.size() < 30; ++b) // 1/16 at 120 BPM: a step every 6000 samples
                    {
                        const int count = arp.generateEventsForBlock(block, sampleRate, events.data(), static_cast<int>(events.size()));
                        for (int i = 0; i < count; ++i)
                            if (events[static_cast<std::size_t>(i)].type == coolsynth::synth::EngineMidiEventType::noteOn)
                                played.push_back(events[static_cast<std::size_t>(i)].noteNumber);
                    }
                    played.resize(std::min<std::size_t>(played.size(), 30));
                    return played;
                };
                const auto first = notes();
                arp.reseed();
                return std::make_pair(first, notes());
            };
            const auto repeated = playTwice(false);
            expect(repeated.first.size() == 30, "the arp played");
            expect(repeated.first == repeated.second, "the same notes again");
            const auto renewed = playTwice(true);
            expect(renewed.first != renewed.second, "New Patterns: other notes");
        }

        const int presets = coolsynth::presets::getFactoryPresetCount();
        for (const bool offline : { false, true })
        {
            beginTest(juce::String("every preset gives the same output twice") + (offline ? ", bouncing" : ""));
            juce::StringArray freshDiffer, resetDiffer, resetsDiffer;
            const auto only = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_PRESET_ONLY", {}); // one preset by name
            for (int preset = 0; preset < presets; ++preset)
            {
                if (only.isNotEmpty() && ! coolsynth::presets::getFactoryPreset(preset).name.containsIgnoreCase(only))
                    continue;
                // Two fresh processors, then the second again after a reset: a defined start.
                auto first = makeSynth(offline);
                auto second = makeSynth(offline);
                const auto a = render(*first, preset);
                const auto b = render(*second, preset);
                second->reset();
                const auto c = render(*second, preset);
                second->reset();
                const auto d = render(*second, preset);
                const auto& name = coolsynth::presets::getFactoryPreset(preset).name;
                // Known, set aside by the user for 0.7.0 (TODO): the Kick's 808 circuit after a reset differs from a
                // fresh one from sample 18, by 0.0047. Its fresh renders are still checked.
                const bool resetKnown = name == "Succession 808";
                if (resetKnown)
                    logMessage("set aside (known, TODO): Succession 808 after a reset");
                if (! sameBits(c, d) && ! resetKnown)
                    resetsDiffer.add(name);
                if (! sameBits(a, b))
                    freshDiffer.add(name);
                if (! sameBits(a, c) && ! resetKnown)
                {
                    // Where the two first part, and how far: a clue to what the reset left behind.
                    std::size_t first = 0;
                    float largest = 0.0f;
                    for (std::size_t i = 0; i < std::min(a.size(), c.size()); ++i)
                    {
                        if (a[i] != c[i] && first == 0)
                            first = i + 1;
                        largest = std::max(largest, std::abs(a[i] - c[i]));
                    }
                    // The renders hold each block's left then right half: back to the sample in the channel.
                    const auto index = first - 1;
                    const auto sample = (index / (2 * block)) * block + index % block;
                    resetDiffer.add(name + " (from sample " + juce::String(static_cast<juce::int64>(sample))
                                    + ", by " + juce::String(largest, 6) + ")");
                }
            }
            logMessage("fresh processors differ: " + juce::String(freshDiffer.size()) + " " + freshDiffer.joinIntoString(", "));
            logMessage("after a reset differ: " + juce::String(resetDiffer.size()) + " " + resetDiffer.joinIntoString(", "));
            logMessage("two resets differ: " + juce::String(resetsDiffer.size()) + " " + resetsDiffer.joinIntoString(", "));
            expect(freshDiffer.isEmpty(), "two fresh processors: " + freshDiffer.joinIntoString(", "));
            expect(resetDiffer.isEmpty(), "after a reset: " + resetDiffer.joinIntoString(", "));
            expect(resetsDiffer.isEmpty(), "one reset against two: " + resetsDiffer.joinIntoString(", "));
        }
    }
};

static DeterminismTests determinismTests;
