// Scenario tests for the audio thread (D161): what a player does, played through the whole processor, each
// block watched by the allocation alarm (tests/AudioThreadAlarm.cpp), checked for broken values, and timed.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "diagnostics/AudioThreadAlarm.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/EffectRoutes.h"

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

    /** The kinds of the first blocking calls the watch saw in audio blocks (D222). */
    juce::String firstBlockingKinds()
    {
        juce::StringArray kinds;
        for (const auto& kind : diag::audioBlockingCalls.firstKinds)
            if (const auto k = kind.load(); k != 0)
                kinds.add(diag::blockingCallName(k));
        return kinds.joinIntoString(" ");
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

        beginTest("The watch counts the heap, a lock, a sleep and a file inside an audio block, and none outside (D222)");
        {
            expect(diag::watchBlockingCalls(), "the import tables could not be patched");
            juce::CriticalSection lock;
            const auto here = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            diag::resetAudioAllocations();
            {
                const juce::ScopedLock held(lock);
                juce::Thread::sleep(0);
                juce::ignoreUnused(here.existsAsFile());
            }
            expectEquals(diag::audioBlockingCalls.count.load(), 0, "outside a block: " + firstBlockingKinds());
            const auto kindSeen = [](diag::BlockingCall kind)
            {
                for (const auto& k : diag::audioBlockingCalls.firstKinds)
                    if (k.load() == static_cast<int>(kind))
                        return true;
                return false;
            };
            for (const auto kind : { diag::BlockingCall::heap, diag::BlockingCall::lock, diag::BlockingCall::sleep, diag::BlockingCall::file })
            {
                diag::resetAudioAllocations();
                {
                    const diag::AudioBlockScope block;
                    switch (kind)
                    {
                        case diag::BlockingCall::heap:
                        {
                            // Through volatiles: an optimising build may remove a malloc whose memory nobody uses, and
                            // then the release build's watch has nothing to see (the 0.4.0 release run, D228).
                            volatile std::size_t bytes = 64;
                            void* volatile kept = std::malloc(bytes);
                            std::free(kept);
                            break;
                        }
                        case diag::BlockingCall::lock: { const juce::ScopedLock held(lock); break; }
                        case diag::BlockingCall::sleep: juce::Thread::sleep(20); break; // shorter ones wait on an event
                        case diag::BlockingCall::file: juce::ignoreUnused(here.existsAsFile()); break;
                        case diag::BlockingCall::none:
                        case diag::BlockingCall::wait:
                        default: break;
                    }
                }
                expect(kindSeen(kind), juce::String("not seen: ") + diag::blockingCallName(static_cast<int>(kind)));
            }
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
            expectEquals(diag::audioBlockingCalls.count.load(), 0, "blocking calls in audio blocks, first kinds: " + firstBlockingKinds() + juce::String(diag::firstBlockingCallers()));
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
            expectEquals(diag::audioBlockingCalls.count.load(), 0, "blocking calls in audio blocks, first kinds: " + firstBlockingKinds() + juce::String(diag::firstBlockingCallers()));
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
            expectEquals(diag::audioBlockingCalls.count.load(), 0, "blocking calls in audio blocks, first kinds: " + firstBlockingKinds() + juce::String(diag::firstBlockingCallers()));
            expectEquals(rig->brokenSamples, 0);
            logMessage("worst block with 32 note-ons " + juce::String(rig->worstMs, 2) + " ms");
        }

        beginTest("A flood of controller moves in one block still lets the keys go (D170)");
        {
            // 1100 wheel moves fill the block's 1024 event slots; the releases after them must still arrive.
            const auto flood = [](bool release)
            {
                auto rig = std::make_unique<Rig>();
                rig->loadPreset(0);
                rig->play(0.1);
                rig->chord(60, true);
                rig->play(0.2);
                for (int i = 0; i < 1100; ++i)
                    rig->midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, i % 128), i % preparedBlock);
                if (release) // at the block's end, behind all 1100 moves
                    for (const int interval : { 0, 3, 7, 12 })
                        rig->midi.addEvent(juce::MidiMessage::noteOff(1, 60 + interval), preparedBlock - 1);
                rig->block(preparedBlock);
                rig->play(12.0);
                return rig->synth->engineForTesting().activeVoiceCount();
            };
            expectGreaterThan(flood(false), 0, "held keys still sound: the test can tell");
            expectEquals(flood(true), 0, "every released key ended");
        }

        beginTest("Every preset with all four macros full up stays sound, and not much louder (D177)");
        {
            // A chord, then all macros of both parts to full; the level against the same chord with them at 0.
            // COOLSYNTH_RENDER_HASHES=file writes a hash of each preset's render at rest, to compare two builds.
            const auto hashFile = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_RENDER_HASHES", {});
            juce::StringArray hashes;
            float worstRise = -100.0f;
            juce::String worstName;
            juce::StringArray tooLoud;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            {
                const auto play = [&](float macro, std::uint64_t* hash, int only = 0)
                {
                    auto rig = std::make_unique<Rig>();
                    rig->loadPreset(i);
                    auto& state = rig->synth->getValueTreeState();
                    for (const char* part : { "pA", "pB" })
                        for (int m = 1; m <= 4; ++m)
                            if (auto* p = state.getParameter(juce::String(part) + ".macro" + juce::String(m)); p != nullptr && (only == 0 || only == m))
                                p->setValueNotifyingHost(macro);
                    rig->play(0.05);
                    rig->chord(57, true);
                    // How loud: the loudest 53 ms (ten blocks) of the second, as an ear hears "louder"; a longer
                    // decay (the Time macro) raises the average but not this.
                    std::uint64_t h = 1469598103934665603ull;
                    std::vector<double> energy;
                    for (int b = 0; b < 188; ++b) // a second
                    {
                        rig->block(preparedBlock);
                        double e = 0.0;
                        for (int s = 0; s < preparedBlock; ++s)
                        {
                            const float v = rig->buffer.getSample(0, s);
                            std::uint32_t bits = 0;
                            std::memcpy(&bits, &v, sizeof(bits));
                            h = (h ^ bits) * 1099511628211ull;
                            e += static_cast<double>(v) * v;
                        }
                        energy.push_back(e);
                    }
                    if (hash != nullptr)
                        *hash = h;
                    expectEquals(rig->brokenSamples, 0, coolsynth::presets::getFactoryPreset(i).name);
                    double loudest = 0.0;
                    for (std::size_t b = 0; b + 10 <= energy.size(); ++b)
                    {
                        double window = 0.0;
                        for (std::size_t k = b; k < b + 10; ++k)
                            window += energy[k];
                        loudest = std::max(loudest, window);
                    }
                    return std::sqrt(loudest / (10.0 * preparedBlock));
                };
                std::uint64_t restHash = 0;
                const double rest = play(0.0f, &restHash);
                const double full = play(1.0f, nullptr);
                const auto& title = coolsynth::presets::getFactoryPreset(i).name;
                hashes.add(title + " " + juce::String::toHexString(static_cast<juce::int64>(restHash)));
                const float rise = static_cast<float>(juce::Decibels::gainToDecibels(full, -120.0) - juce::Decibels::gainToDecibels(rest, -120.0));
                if (rise > 4.0f)
                {
                    // An arpeggio with random patterns plays differently each time (D168): no level to compare.
                    std::uint64_t again = 0;
                    play(0.0f, &again);
                    if (again != restHash)
                    {
                        logMessage(title + " varies from run to run (a random arpeggio): its level is not compared");
                        continue;
                    }
                }
                if (rise > worstRise)
                {
                    worstRise = rise;
                    worstName = title;
                }
                if (rise > 4.0f)
                {
                    // which macro does it
                    juce::String each;
                    for (int m = 1; m <= 4; ++m)
                        each << " m" << m << " "
                             << juce::String(juce::Decibels::gainToDecibels(play(1.0f, nullptr, m), -120.0) - juce::Decibels::gainToDecibels(rest, -120.0), 1);
                    tooLoud.add(title + " +" + juce::String(rise, 1) + " dB (" + each.trim() + ")");
                }
            }
            if (hashFile.isNotEmpty())
                juce::File(hashFile).replaceWithText(hashes.joinIntoString("\n"));
            logMessage("loudest rise with all macros up: " + juce::String(worstRise, 1) + " dB, " + worstName);
            expect(tooLoud.isEmpty(), "more than 4 dB louder: " + tooLoud.joinIntoString(", "));
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
            expectEquals(diag::audioBlockingCalls.count.load(), 0, "blocking calls in audio blocks, first kinds: " + firstBlockingKinds() + juce::String(diag::firstBlockingCallers()));
            expectEquals(rig->brokenSamples, 0);
            expectGreaterThan(rig->peak, 0.001f, "it played");
        }
    }
};

static AudioScenarioTests audioScenarioTests;

// ---- Effect routes (D179): the part's global sources move its effects ---------------------------------

class EffectRouteTests final : public juce::UnitTest
{
public:
    EffectRouteTests() : juce::UnitTest("Effect routes: macros and global sources move the effects", "Audio") {}

    void runTest() override
    {
        // Basic Saw with a master reverb at 20 % mix; a key held half a second, then the tail. Returns the left channel.
        const auto render = [](bool routed, float macro, bool sweep)
        {
            auto rig = std::make_unique<Rig>();
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == "Basic Saw")
                    rig->loadPreset(i);
            auto& state = rig->synth->getValueTreeState();
            const auto set = [&](const juce::String& id, float plain)
            {
                auto* p = state.getParameter(id);
                p->setValueNotifyingHost(p->convertTo0to1(plain));
            };
            set("f1.type", static_cast<float>(coolsynth::synth::v3::insertType::reverb));
            state.getParameter("f1.p1")->setValueNotifyingHost(0.6f); // size
            state.getParameter("f1.p3")->setValueNotifyingHost(0.2f); // mix
            if (routed)
            {
                set("pA.fx1.src", static_cast<float>(coolsynth::synth::v3::ModSource::macro1));
                set("pA.fx1.dst", static_cast<float>(coolsynth::synth::v3::effectDestination::master(0, 2))); // F1 mix
                set("pA.fx1.amount", 0.6f);
            }
            set("pA.macro1", sweep ? 0.0f : macro);
            std::vector<float> out;
            rig->chord(57, true);
            for (int b = 0; b < 375; ++b) // 2 s
            {
                if (b == 94)
                    rig->chord(57, false);
                if (sweep && b == 60)
                    set("pA.macro1", 1.0f);
                rig->block(preparedBlock);
                out.insert(out.end(), rig->buffer.getReadPointer(0), rig->buffer.getReadPointer(0) + preparedBlock);
            }
            return out;
        };
        const auto rms = [](const std::vector<float>& x, std::size_t from, std::size_t to)
        {
            double sum = 0.0;
            for (auto i = from; i < to; ++i)
                sum += static_cast<double>(x[i]) * x[i];
            return std::sqrt(sum / static_cast<double>(to - from));
        };

        beginTest("A route that adds nothing leaves the sound bit for bit as without it");
        {
            const auto without = render(false, 0.0f, false);
            const auto idle = render(true, 0.0f, false);
            int different = 0;
            for (std::size_t i = 0; i < without.size(); ++i)
                different += without[i] != idle[i] ? 1 : 0;
            expectEquals(different, 0, "samples that differ with the macro at 0");
        }

        beginTest("A macro on the master reverb's mix makes the tail louder");
        {
            const auto rest = render(true, 0.0f, false);
            const auto full = render(true, 1.0f, false);
            const std::size_t tailFrom = 94 * preparedBlock + 24000, tailTo = tailFrom + 24000; // 0.5 to 1 s after the keys
            const double gain = juce::Decibels::gainToDecibels(rms(full, tailFrom, tailTo) / std::max(1.0e-9, rms(rest, tailFrom, tailTo)));
            logMessage("tail with the macro full up: " + juce::String(gain, 1) + " dB");
            expectGreaterThan(gain, 6.0);
        }

        beginTest("Turning that macro at once while the chord sounds does not step");
        {
            // The macro's own 10 ms glide, the 32-sample steps and the effect's mix smoothing together.
            const auto x = render(true, 0.0f, true);
            const std::size_t at = 60 * preparedBlock;
            float steady = 0.0f, change = 0.0f;
            for (std::size_t i = at - 9600; i < at; ++i)
                steady = std::max(steady, std::abs(x[i] - 2.0f * x[i - 1] + x[i - 2]));
            for (std::size_t i = at; i < at + 1440; ++i)
                change = std::max(change, std::abs(x[i] - 2.0f * x[i - 1] + x[i - 2]));
            logMessage("roughness before " + juce::String(steady, 5) + ", at the turn " + juce::String(change, 5));
            expectLessThan(change, 1.5f * steady + 1.0e-3f);
        }
    }
};

static EffectRouteTests effectRouteTests;
