// The click detector (D171): what a player does that must never click, on the factory presets, through the whole
// processor. A click is a burst of roughness (the largest second difference, |x[n] - 2x[n-1] + x[n-2]|) right
// after the event that is higher than the sound's own, both before the event and once it has settled. Where the
// sound may have its own sharp moments (an arpeggio, a new note's attack), the same render without the event, or
// the new note started fresh, says what the sound's own roughness is there.

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/SampleLibrary.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    std::size_t samplesFor(double seconds) { return static_cast<std::size_t>(std::lround(seconds * sampleRate)); }

    /** A processor whose output is kept, block after block. */
    struct Recorder
    {
        std::unique_ptr<SynthAudioProcessor> synth = std::make_unique<SynthAudioProcessor>();
        juce::AudioBuffer<float> buffer { 2, blockSize };
        juce::MidiBuffer midi;
        std::vector<float> left, right;

        Recorder()
        {
            synth->setPlayConfigDetails(0, 2, sampleRate, blockSize);
            synth->prepareToPlay(sampleRate, blockSize);
            left.reserve(samplesFor(4.0));
            right.reserve(samplesFor(4.0));
        }

        void play(double seconds)
        {
            const auto blocks = (samplesFor(seconds) + blockSize - 1) / blockSize;
            for (std::size_t b = 0; b < blocks; ++b)
            {
                buffer.clear();
                synth->processBlock(buffer, midi);
                midi.clear();
                left.insert(left.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize);
                right.insert(right.end(), buffer.getReadPointer(1), buffer.getReadPointer(1) + blockSize);
            }
        }

        /** Where the next block starts. */
        [[nodiscard]] std::size_t now() const noexcept { return left.size(); }

        void loadPreset(int index)
        {
            synth->setCurrentProgram(index);
            synth->loadSampledInstrumentsNow();
        }

        void set(const char* id, float plainValue)
        {
            if (auto* parameter = synth->getValueTreeState().getParameter(id))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
        }

        void chord(bool on)
        {
            for (const int note : { 57, 60, 64, 69 })
                midi.addEvent(on ? juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)) : juce::MidiMessage::noteOff(1, note), 0);
        }

        void note(int number, bool on)
        {
            midi.addEvent(on ? juce::MidiMessage::noteOn(1, number, static_cast<juce::uint8>(100)) : juce::MidiMessage::noteOff(1, number), 0);
        }

        /** The largest second difference in [from, to), both channels. */
        [[nodiscard]] float roughness(std::size_t from, std::size_t to) const noexcept
        {
            from = std::max<std::size_t>(from, 2);
            to = std::min(to, left.size());
            float worst = 0.0f;
            for (const auto* channel : { &left, &right })
                for (std::size_t i = from; i < to; ++i)
                    worst = std::max(worst, std::abs((*channel)[i] - 2.0f * (*channel)[i - 1] + (*channel)[i - 2]));
            return worst;
        }
    };

    /** What the detector found at one event. */
    struct Finding
    {
        float during = 0.0f; // roughness right after the event
        float own = 0.0f;    // the sound's own: before, settled after, and the reference render's at the same place
        [[nodiscard]] float ratio() const noexcept { return during / std::max(own, 1.0e-4f); }
    };

    constexpr float clickRatio = 1.5f;  // half again as rough as the sound itself
    constexpr float audibleFloor = 1.0e-3f; // below -60 dB of second difference, nothing to hear

    [[nodiscard]] bool clicks(const Finding& f) noexcept { return f.during > audibleFloor && f.ratio() > clickRatio; }

    /** The sound's own roughness around an event at `at`, whose effect lasts `length`. */
    float ownRoughness(const Recorder& r, std::size_t at, std::size_t length)
    {
        return std::max(r.roughness(at - samplesFor(0.2), at), r.roughness(at + length + samplesFor(0.05), at + length + samplesFor(0.25)));
    }

    juce::String presetName(int index) { return coolsynth::presets::getFactoryPreset(index).name; }

    /** COOLSYNTH_CLICK_ONLY=part of a preset's name: only those presets (to study one finding). */
    bool wanted(int index)
    {
        const auto only = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CLICK_ONLY", {});
        return only.isEmpty() || presetName(index).containsIgnoreCase(only);
    }

    /** With COOLSYNTH_CLICK_PROFILE set: the roughness and the peak level in 2 ms frames from 10 ms before an event
        to 80 ms after, for the event's render and the reference, to tell a click from the sound's own change. */
    juce::String profile(const Recorder& event, std::size_t at, const Recorder& reference, std::size_t referenceAt)
    {
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CLICK_PROFILE", {}).isEmpty())
            return {};
        const auto frame = samplesFor(0.002);
        const auto level = [&](const Recorder& r, std::size_t from)
        {
            float peak = 0.0f;
            for (std::size_t i = from; i < std::min(from + frame, r.left.size()); ++i)
                peak = std::max({ peak, std::abs(r.left[i]), std::abs(r.right[i]) });
            return peak;
        };
        juce::String s = "\n  ms   rough(event) peak(event)  rough(ref) peak(ref)";
        for (int k = -5; k < 40; ++k)
        {
            const auto e = at + static_cast<std::size_t>(k) * frame, f = referenceAt + static_cast<std::size_t>(k) * frame;
            s << "\n  " << juce::String(2 * k).paddedLeft(' ', 3) << "  " << juce::String(event.roughness(e, e + frame), 5).paddedLeft(' ', 10)
              << " " << juce::String(level(event, e), 4).paddedLeft(' ', 10) << "  " << juce::String(reference.roughness(f, f + frame), 5).paddedLeft(' ', 10)
              << " " << juce::String(level(reference, f), 4).paddedLeft(' ', 9);
        }
        return s;
    }

    /** Collects the presets that clicked and the worst ratio. */
    struct Tally
    {
        juce::StringArray clicked;
        float worst = 0.0f;
        juce::String worstName;

        void add(const Finding& f, const juce::String& name)
        {
            if (f.ratio() > worst && f.during > audibleFloor)
            {
                worst = f.ratio();
                worstName = name;
            }
            if (clicks(f))
                clicked.add(name + " (" + juce::String(f.ratio(), 2) + "x)");
        }

        /** As add, if the reference is the same render up to the event; a random arpeggio plays differently each
            time (D168), so its reference cannot tell its own notes from a click: it is named, not judged. */
        void add(const Finding& f, const juce::String& name, const Recorder& event, const Recorder& reference, std::size_t at)
        {
            for (std::size_t i = 0; i < std::min({ at, event.left.size(), reference.left.size() }); ++i)
                if (event.left[i] != reference.left[i])
                {
                    varies.add(name.upToFirstOccurrenceOf("\n", false, false));
                    return;
                }
            add(f, name);
        }

        [[nodiscard]] juce::String note() const
        {
            return "worst " + juce::String(worst, 2) + "x at " + worstName
                   + (varies.isEmpty() ? juce::String() : "; varies from run to run, not judged: " + varies.joinIntoString(", "));
        }

        juce::StringArray varies;
    };
}

class ClickDetectorTests final : public juce::UnitTest
{
public:
    ClickDetectorTests() : juce::UnitTest("Click detector: switches, effect changes, steals", "Audio") {}

    void runTest() override
    {
        const int presets = coolsynth::presets::getFactoryPresetCount();

        beginTest("The detector hears a step, and not a sine's own roughness");
        {
            Recorder r;
            r.left.resize(samplesFor(1.0));
            r.right.resize(samplesFor(1.0));
            for (std::size_t i = 0; i < r.left.size(); ++i)
                r.left[i] = r.right[i] = 0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * static_cast<double>(i) / sampleRate));
            const std::size_t at = samplesFor(0.5);
            const Finding smooth { r.roughness(at, at + samplesFor(0.02)), ownRoughness(r, at, samplesFor(0.02)) };
            expect(! clicks(smooth), "a steady sine: " + juce::String(smooth.ratio(), 2));
            for (std::size_t i = at; i < r.left.size(); ++i)
                r.left[i] += 0.02f; // a step of -34 dB
            const Finding step { r.roughness(at, at + samplesFor(0.02)), ownRoughness(r, at, samplesFor(0.02)) };
            expect(clicks(step), "a step: " + juce::String(step.ratio(), 2));
        }

        beginTest("A preset change while a chord rings does not click, on every preset");
        {
            Tally tally;
            for (int i = 0; i < presets; ++i)
            {
                if (! wanted(i))
                    continue;
                const auto render = [&](bool change)
                {
                    auto r = std::make_unique<Recorder>();
                    r->loadPreset(i);
                    r->play(0.05);
                    r->chord(true);
                    r->play(0.7);
                    const auto at = r->now();
                    if (change)
                        r->loadPreset((i + 1) % presets);
                    r->play(0.6);
                    return std::make_pair(std::move(r), at);
                };
                const auto [changed, at] = render(true);
                const auto [kept, keptAt] = render(false);
                juce::ignoreUnused(keptAt);
                const auto window = samplesFor(0.03);
                const Finding f { changed->roughness(at, at + window),
                                  std::max(ownRoughness(*changed, at, window), kept->roughness(at, at + window)) };
                tally.add(f, presetName(i) + " -> " + presetName((i + 1) % presets), *changed, *kept, at);
            }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }

        beginTest("An effect type change while a chord rings does not click (part insert and master slot)");
        {
            Tally tally;
            int step = 0;
            for (int i = 0; i < presets; i += 6)
                for (const char* slot : { "pA.i1.type", "f1.type" })
                {
                    // Each preset tries another type: over the presets, every type in both slots.
                    const int type = 1 + (step++ % (coolsynth::synth::v3::insertType::count - 1));
                    if (! wanted(i))
                        continue;
                    const auto render = [&](bool change)
                    {
                        auto r = std::make_unique<Recorder>();
                        r->loadPreset(i);
                        r->play(0.05);
                        r->chord(true);
                        r->play(0.7);
                        const auto at = r->now();
                        if (change)
                            r->set(slot, static_cast<float>(type));
                        r->play(0.6);
                        return std::make_pair(std::move(r), at);
                    };
                    const auto [changed, at] = render(true);
                    const auto [kept, keptAt] = render(false);
                    juce::ignoreUnused(keptAt);
                    const auto window = samplesFor(0.03);
                    const Finding f { changed->roughness(at, at + window),
                                      std::max(ownRoughness(*changed, at, window), kept->roughness(at, at + window)) };
                    tally.add(f, presetName(i) + " " + slot + " -> " + coolsynth::synth::v3::insertDescriptor(type).name
                                     + (clicks(f) ? profile(*changed, at, *kept, at) : juce::String()), *changed, *kept, at);
                }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }

        beginTest("Brightness and Space swept end to end at once while a chord rings do not click (D176)");
        {
            Tally tally;
            for (int i = 0; i < presets; i += 3)
                for (const char* knob : { coolsynth::parameters::ids::brightness, coolsynth::parameters::ids::space })
                {
                    if (! wanted(i))
                        continue;
                    const auto render = [&](bool sweep)
                    {
                        auto r = std::make_unique<Recorder>();
                        r->loadPreset(i);
                        r->set(knob, -1.0f);
                        r->play(0.05);
                        r->chord(true);
                        r->play(0.7);
                        const auto at = r->now();
                        if (sweep)
                            r->set(knob, 1.0f);
                        r->play(0.6);
                        return std::make_pair(std::move(r), at);
                    };
                    const auto [swept, at] = render(true);
                    const auto [kept, keptAt] = render(false);
                    juce::ignoreUnused(keptAt);
                    const auto window = samplesFor(0.03);
                    const Finding f { swept->roughness(at, at + window),
                                      std::max(ownRoughness(*swept, at, window), kept->roughness(at, at + window)) };
                    tally.add(f, presetName(i) + " " + knob + (clicks(f) ? profile(*swept, at, *kept, at) : juce::String()), *swept, *kept, at);
                }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }

        beginTest("All four macros turned full up at once while a chord rings do not click, on every preset (D177)");
        {
            Tally tally;
            for (int i = 0; i < presets; ++i)
            {
                if (! wanted(i))
                    continue;
                const auto render = [&](bool turn)
                {
                    auto r = std::make_unique<Recorder>();
                    r->loadPreset(i);
                    r->play(0.05);
                    r->chord(true);
                    r->play(0.7);
                    const auto at = r->now();
                    if (turn)
                        for (const char* part : { "pA", "pB" })
                            for (int m = 1; m <= 4; ++m)
                                r->set((juce::String(part) + ".macro" + juce::String(m)).toRawUTF8(), 1.0f);
                    r->play(0.6);
                    return std::make_pair(std::move(r), at);
                };
                const auto [turned, at] = render(true);
                const auto [kept, keptAt] = render(false);
                juce::ignoreUnused(keptAt);
                const auto window = samplesFor(0.03);
                const Finding f { turned->roughness(at, at + window),
                                  std::max(ownRoughness(*turned, at, window), kept->roughness(at, at + window)) };
                tally.add(f, presetName(i) + (clicks(f) ? profile(*turned, at, *kept, at) : juce::String()), *turned, *kept, at);
            }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }

        beginTest("A note's start with the Time macro full up is no rougher than the sound's own, on every preset (D177)");
        {
            // The Time macro scales an envelope's times, its attack too: a very short attack must not click.
            Tally tally;
            for (int i = 0; i < presets; ++i)
            {
                if (! wanted(i))
                    continue;
                const auto render = [&](float time)
                {
                    auto r = std::make_unique<Recorder>();
                    r->loadPreset(i);
                    for (const char* part : { "pA.macro3", "pB.macro3" })
                        r->set(part, time);
                    r->play(0.05);
                    const auto at = r->now();
                    r->note(60, true);
                    r->play(0.4);
                    return std::make_pair(std::move(r), at);
                };
                const auto [quick, at] = render(1.0f);
                const auto [asDesigned, restAt] = render(0.0f);
                juce::ignoreUnused(restAt);
                const auto window = samplesFor(0.01);
                const Finding f { quick->roughness(at, at + window),
                                  std::max(asDesigned->roughness(at, at + window),
                                           quick->roughness(at + samplesFor(0.06), at + samplesFor(0.26))) };
                tally.add(f, presetName(i) + (clicks(f) ? profile(*quick, at, *asDesigned, at) : juce::String()), *quick, *asDesigned, at);
            }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }

        beginTest("An instrument read in the background while an earlier sound rings does not click");
        {
            // Sampled presets in pairs: the first rings a chord, the second is chosen without waiting for its
            // instrument, and the blocks go on near real time while the loader reads it in. The reference is the
            // same render without the change.
            std::vector<int> sampled;
            for (int i = 0; i < presets && sampled.size() < 9; ++i)
            {
                if (! wanted(i))
                    continue;
                Recorder probe;
                probe.loadPreset(i);
                int low = 0, high = 0;
                if (probe.synth->sampledInstrumentRange(low, high))
                    sampled.push_back(i);
            }
            auto& library = coolsynth::synth::v3::SampleLibrary::instance();
            Tally tally;
            int loadsSeen = 0;
            for (std::size_t k = 0; k + 1 < sampled.size(); ++k)
            {
                const int first = sampled[k], second = sampled[k + 1];
                const auto render = [&](bool change, int blocksAfter)
                {
                    library.setRoot(library.root()); // nothing in memory: the second instrument must be read
                    auto r = std::make_unique<Recorder>();
                    r->loadPreset(first);
                    r->play(0.05);
                    r->chord(true);
                    r->play(0.1); // the stand-in recordings last 0.25 s: still sounding
                    const auto at = r->now();
                    if (change)
                        r->synth->setCurrentProgram(second);
                    int blocks = 0, loadingBlocks = 0;
                    for (; blocksAfter > 0 ? blocks < blocksAfter : blocks < 2000; ++blocks)
                    {
                        r->play(1.0 / sampleRate); // one block
                        juce::Thread::sleep(1);
                        loadingBlocks += r->synth->isLoadingInstrument() ? 1 : 0;
                        if (blocksAfter == 0 && blocks > 8 && ! r->synth->isLoadingInstrument())
                            break;
                    }
                    if (blocksAfter == 0)
                        r->play(0.3);
                    return std::make_tuple(std::move(r), at, blocks, loadingBlocks);
                };
                auto [changed, at, blocks, loadingBlocks] = render(true, 0);
                // Everything wanted from the change's render is taken before the reference empties the library: its
                // notes hold the instrument they read, and emptying the library under them freed it while they still
                // counted on it (D208: an intermittent crash in Release builds).
                const auto end = changed->now();
                const auto changedRoughness = changed->roughness(at, end);
                const auto ownRoughnessThere = ownRoughness(*changed, at, 0);
                // It counts only if the earlier sound was still sounding while the loader worked: over the load, and at
                // least 50 ms, since an instrument in Windows' file cache is read in two blocks, too short to measure.
                const auto minimumWindow = static_cast<std::size_t>(0.05 * sampleRate);
                const auto loadEnd = at + std::max(static_cast<std::size_t>(loadingBlocks) * blockSize, minimumWindow);
                loadsSeen += loadingBlocks > 0 && changed->roughness(at, loadEnd) > audibleFloor ? 1 : 0;
                changed.reset();

                auto [kept, keptAt, keptBlocks, keptLoading] = render(false, blocks);
                juce::ignoreUnused(keptAt, keptBlocks, keptLoading);
                kept->play(0.3);
                const Finding f { changedRoughness, std::max(kept->roughness(at, end), ownRoughnessThere) };
                kept.reset();
                tally.add(f, presetName(first) + " -> " + presetName(second) + " (read in over " + juce::String(loadingBlocks) + " blocks)");
            }
            library.setRoot(library.root());
            logMessage("worst " + juce::String(tally.worst, 2) + "x at " + tally.worstName + "; " + juce::String(loadsSeen) + " loads while it sounded");
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
            if (! juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CLICK_ONLY", {}).isNotEmpty())
                expectGreaterThan(loadsSeen, 0, "an instrument was read in while the earlier sound rang");
        }

        beginTest("A stolen voice does not click, on every preset (one voice, a second key)");
        {
            // The reference is the same render with a second voice free: the same effect tails, the same new note,
            // only the old note goes on instead of fading out in 5 ms.
            Tally tally;
            const auto render = [](int preset, int voices)
            {
                auto r = std::make_unique<Recorder>();
                r->loadPreset(preset);
                for (const char* part : { "pA", "pB" })
                {
                    r->set((juce::String(part) + ".play.mode").toRawUTF8(), 0.0f); // Poly
                    r->set((juce::String(part) + ".play.voices").toRawUTF8(), static_cast<float>(voices));
                    r->set((juce::String(part) + ".chord.type").toRawUTF8(), 0.0f);
                }
                r->set(coolsynth::parameters::ids::arpEnabled, 0.0f);
                r->play(0.05);
                r->note(57, true);
                r->play(0.7);
                const auto at = r->now();
                r->note(64, true);
                r->play(0.6);
                return std::make_pair(std::move(r), at);
            };
            for (int i = 0; i < presets; ++i)
            {
                if (! wanted(i))
                    continue;
                const auto [stolen, at] = render(i, 1);
                const auto [both, bothAt] = render(i, 2);
                juce::ignoreUnused(bothAt);
                const auto window = samplesFor(0.02); // the 5 ms fade, the new note's start, and some
                const Finding f { stolen->roughness(at, at + window),
                                  std::max(ownRoughness(*stolen, at, window), both->roughness(at, at + window)) };
                tally.add(f, presetName(i) + (clicks(f) ? profile(*stolen, at, *both, at) : juce::String()), *stolen, *both, at);
            }
            logMessage(tally.note());
            expect(tally.clicked.isEmpty(), "clicked: " + tally.clicked.joinIntoString(", "));
        }
    }
};

static ClickDetectorTests clickDetectorTests;
