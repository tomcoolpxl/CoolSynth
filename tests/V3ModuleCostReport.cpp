#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "synth/v3/Inserts.h"
#include "synth/v3/Part.h"

/*
    Opt-in (set COOLSYNTH_CPU_REPORT): the cost of each module on its own. A part plays an
    eight-note chord for two seconds at 48 kHz (its voices run at 96 kHz) with one generator and
    no processor, or a plain saw and one processor; the report prints the share of real time.
    A processor's own cost is its line minus the plain saw's.
*/
namespace
{
    using namespace coolsynth::synth::v3;

    GeneratorSettings generatorOf(int type, std::initializer_list<std::pair<int, float>> units = {})
    {
        GeneratorSettings g;
        g.type = type;
        const auto& d = GeneratorSlot::typeTable().descriptor(type);
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, value] : units)
            g.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], value);
        g.level = 0.5f;
        return g;
    }

    ProcessorSettings processorOf(int type)
    {
        ProcessorSettings p;
        p.type = type;
        const auto& d = ProcessorSlot::typeTable().descriptor(type);
        for (int i = 0; i < d.usedParams; ++i)
            p.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        return p;
    }

    double shareOfRealTime(const PartSettings& settings)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        constexpr int blocks = static_cast<int>(2.0 * rate) / block;
        auto part = std::make_unique<Part>();
        part->prepare(rate, block, 7);
        std::vector<float> left(block), right(block);
        std::vector<coolsynth::synth::EngineMidiEvent> chord;
        for (int note : { 48, 52, 55, 59, 60, 64, 67, 72 })
        {
            coolsynth::synth::EngineMidiEvent e;
            e.type = coolsynth::synth::EngineMidiEventType::noteOn;
            e.noteNumber = static_cast<uint8_t>(note);
            e.value = 0.8f;
            chord.push_back(e);
        }
        const auto start = juce::Time::getHighResolutionTicks();
        for (int b = 0; b < blocks; ++b)
        {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            const auto events = b == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(chord) : std::span<const coolsynth::synth::EngineMidiEvent>();
            part->render(left.data(), right.data(), block, events, settings, {});
        }
        return 100.0 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) / 2.0;
    }

    /** A block's cost as a share of its own time: the average, the 99th percentile and the slowest. */
    struct BlockCost
    {
        double average = 0.0, p99 = 0.0, worst = 0.0;
    };

    BlockCost costOf(std::vector<double> shares)
    {
        BlockCost c;
        if (shares.empty())
            return c;
        for (const double s : shares)
            c.average += s;
        c.average /= static_cast<double>(shares.size());
        std::sort(shares.begin(), shares.end());
        c.p99 = shares[std::min(shares.size() - 1, static_cast<std::size_t>(0.99 * static_cast<double>(shares.size())))];
        c.worst = shares.back();
        return c;
    }

    /** Sixteen voices, each its own key, held three seconds, then all struck again: the steal fades and the clearing
        of used slots fall in the measured blocks too (D222). */
    BlockCost worstCaseOf(const PartSettings& settings)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        constexpr int blocks = static_cast<int>(3.0 * rate) / block;
        auto part = std::make_unique<Part>();
        part->prepare(rate, block, 7);
        std::vector<float> left(block), right(block);
        std::vector<coolsynth::synth::EngineMidiEvent> chord, again;
        for (int i = 0; i < Part::maxVoices; ++i)
        {
            coolsynth::synth::EngineMidiEvent e;
            e.type = coolsynth::synth::EngineMidiEventType::noteOn;
            e.noteNumber = static_cast<uint8_t>(36 + 3 * i);
            e.value = 1.0f;
            chord.push_back(e);
            e.noteNumber = static_cast<uint8_t>(37 + 3 * i);
            again.push_back(e);
        }
        std::vector<double> shares;
        shares.reserve(blocks);
        const double blockSeconds = block / rate;
        for (int b = 0; b < blocks; ++b)
        {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            const auto events = b == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(chord)
                              : b == blocks / 2 ? std::span<const coolsynth::synth::EngineMidiEvent>(again)
                                                : std::span<const coolsynth::synth::EngineMidiEvent>();
            const auto start = juce::Time::getHighResolutionTicks();
            part->render(left.data(), right.data(), block, events, settings, {});
            shares.push_back(100.0 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) / blockSeconds);
        }
        return costOf(std::move(shares));
    }
}

class V3ModuleCostReport final : public juce::UnitTest
{
public:
    V3ModuleCostReport() : juce::UnitTest("V3 module cost report", "V3") {}

    void runTest() override
    {
        beginTest("cost_of_each_module_alone");
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CPU_REPORT", {}).isEmpty())
        {
            logMessage("skipped: set COOLSYNTH_CPU_REPORT=1 to measure");
            return;
        }

        auto base = []
        {
            PartSettings s;
            Envelope::Parameters held;
            held.attack = 0.005f;
            held.decay = 0.2f;
            held.sustain = 1.0f;
            held.release = 0.2f;
            s.voice.envelopes = { held, held, held };
            s.polyphony = 8;
            return s;
        };
        auto report = [&](const juce::String& label, const PartSettings& s)
        {
            double best = 1.0e9;
            for (int run = 0; run < 3; ++run) // the best of three: the least disturbed by the rest of the machine
                best = std::min(best, shareOfRealTime(s));
            logMessage(label.paddedRight(' ', 26) + juce::String(best, 2) + " % of real time");
        };

        {
            auto s = base();
            report("empty voices", s);
        }
        const std::vector<std::pair<juce::String, GeneratorSettings>> generators {
            { "VA saw", generatorOf(generatorType::va, { { 0, 2.0f } }) },
            { "VA saw, unison 7", generatorOf(generatorType::va, { { 0, 2.0f }, { 2, 7.0f } }) },
            { "VA pulse", generatorOf(generatorType::va, { { 0, 3.0f } }) },
            { "Noise", generatorOf(generatorType::noise) },
            { "FM2", generatorOf(generatorType::fm2) },
            { "FM4", generatorOf(generatorType::fm4) },
            { "Wavetable", generatorOf(generatorType::wavetable) },
            { "String", generatorOf(generatorType::string) },
            { "Modal", generatorOf(generatorType::modal) },
            { "Sample (generated)", generatorOf(generatorType::sample) },
            { "Grain", generatorOf(generatorType::grain) },
            { "Chip", generatorOf(generatorType::chip) },
            { "TB-303", generatorOf(generatorType::tb303) },
        };
        for (const auto& [label, generator] : generators)
        {
            auto s = base();
            s.voice.generators[0] = generator;
            report(label, s);
        }
        const std::vector<std::pair<juce::String, int>> processors {
            { "VA saw + Ladder", processorType::ladder }, { "VA saw + SVF", processorType::svf },
            { "VA saw + Crusher", processorType::crusher }, { "VA saw + Folder", processorType::folder },
            { "VA saw + Drive", processorType::drive }, { "VA saw + Formant", processorType::formant },
            { "VA saw + Comb", processorType::comb },
        };
        for (const auto& [label, type] : processors)
        {
            auto s = base();
            s.voice.generators[0] = generators.front().second;
            s.voice.processors[0] = processorOf(type);
            report(label, s);
        }
    }
};

/*
    Opt-in (set COOLSYNTH_CPU_REPORT): each module's worst case (D222, TODO 19), the numbers the voice cap is set from.
    Sixteen voices at the module's hardest settings, struck twice so steals fall in the blocks; the average, the 99th
    percentile and the slowest block as a share of a 256-sample block at 48 kHz, each the best of three runs. A
    voice's share is the line less the empty voices', over sixteen; a processor's, less the plain saw's too.
*/
class V3WorstCaseCost final : public juce::UnitTest
{
public:
    V3WorstCaseCost() : juce::UnitTest("V3 worst-case cost", "V3") {}

    void runTest() override
    {
        beginTest("worst_case_of_each_module");
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_CPU_REPORT", {}).isEmpty())
        {
            logMessage("skipped: set COOLSYNTH_CPU_REPORT=1 to measure");
            return;
        }

        auto base = []
        {
            PartSettings s;
            Envelope::Parameters held;
            held.attack = 0.002f;
            held.decay = 0.2f;
            held.sustain = 1.0f;
            held.release = 0.5f;
            s.voice.envelopes = { held, held, held };
            s.polyphony = Part::maxVoices;
            return s;
        };
        auto measure = [&](const PartSettings& s)
        {
            BlockCost best { 1.0e9, 1.0e9, 1.0e9 };
            for (int run = 0; run < 3; ++run) // the least disturbed of three, each figure on its own
            {
                const auto c = worstCaseOf(s);
                best.average = std::min(best.average, c.average);
                best.p99 = std::min(best.p99, c.p99);
                best.worst = std::min(best.worst, c.worst);
            }
            return best;
        };
        const auto empty = measure(base());
        auto line = [&](const juce::String& label, const BlockCost& c, const BlockCost& under)
        {
            logMessage(label.paddedRight(' ', 30) + "average " + juce::String(c.average, 2) + " %, p99 " + juce::String(c.p99, 2)
                       + " %, slowest " + juce::String(c.worst, 2) + " %; a voice at p99 "
                       + juce::String((c.p99 - under.p99) / Part::maxVoices, 3) + " %");
        };
        line("empty voices", empty, BlockCost {});

        const std::vector<std::pair<juce::String, GeneratorSettings>> generators {
            { "VA saw", generatorOf(generatorType::va, { { VaGenerator::shape, 2.0f } }) },
            { "VA saw, unison 7, sub", generatorOf(generatorType::va, { { VaGenerator::shape, 2.0f }, { VaGenerator::unison, 7.0f },
                                                                          { VaGenerator::detune, 200.0f }, { VaGenerator::sub, 1.0f } }) },
            { "Noise", generatorOf(generatorType::noise, { { NoiseGenerator::resonance, 1.0f } }) },
            { "FM2, index 10, feedback", generatorOf(generatorType::fm2, { { FmGenerator::index, 10.0f }, { FmGenerator::feedback, 1.0f } }) },
            { "FM4, feedback", generatorOf(generatorType::fm4, { { Fm4Generator::feedback, 1.0f }, { Fm4Generator::level2, 1.0f },
                                                                 { Fm4Generator::level3, 1.0f }, { Fm4Generator::level4, 1.0f } }) },
            { "Wavetable, unison 5", generatorOf(generatorType::wavetable, { { WavetableGenerator::unison, 5.0f }, { WavetableGenerator::detune, 100.0f } }) },
            { "String, long decay", generatorOf(generatorType::string, { { StringGenerator::decay, 10.0f } }) },
            { "Modal, long decay", generatorOf(generatorType::modal, { { ModalGenerator::decay, 10.0f }, { ModalGenerator::hardness, 1.0f } }) },
            { "Sample (generated)", generatorOf(generatorType::sample) },
            { "Grain, densest, longest", generatorOf(generatorType::grain, { { GrainGenerator::density, 80.0f }, { GrainGenerator::size, 500.0f },
                                                                             { GrainGenerator::pitchSpray, 1.0f } }) },
            // The Chip's hardest: a combined wave read between two widths with an arpeggio; and its noise register.
            { "Chip, Tri+Saw+Pulse, arp", generatorOf(generatorType::chip, { { ChipGenerator::chip, 3.0f }, { ChipGenerator::wave, 8.0f },
                                                                              { ChipGenerator::duty, 0.4f }, { ChipGenerator::sequence, 5.0f } }) },
            { "Chip, NES noise", generatorOf(generatorType::chip, { { ChipGenerator::wave, 3.0f }, { ChipGenerator::output, 0.0f } }) },
            // The Kick's hardest: the 808 with every part on, driven, its glide bend at its longest (D251).
            { "Kick, 808, all on, driven", generatorOf(generatorType::kick, { { KickGenerator::click, 1.0f }, { KickGenerator::noise, 1.0f },
                                                                               { KickGenerator::shape, 1.0f }, { KickGenerator::drive, 24.0f },
                                                                               { KickGenerator::tone, 0.5f }, { KickGenerator::decay, 10000.0f },
                                                                               { KickGenerator::punchTime, 500.0f } }) },
            { "Kick, 909, all on, driven", generatorOf(generatorType::kick, { { KickGenerator::model, 1.0f }, { KickGenerator::click, 1.0f },
                                                                               { KickGenerator::noise, 1.0f }, { KickGenerator::shape, 1.0f },
                                                                               { KickGenerator::drive, 24.0f }, { KickGenerator::decay, 10000.0f } }) },
            // The TB-303's hardest: full resonance, square, accent, the longest decay.
            { "TB-303, square, resonant, accented", generatorOf(generatorType::tb303, { { Tb303Generator::waveform, 1.0f }, { Tb303Generator::resonance, 1.0f },
                                                                                         { Tb303Generator::envMod, 1.0f }, { Tb303Generator::accent, 1.0f },
                                                                                         { Tb303Generator::decay, 2000.0f } }) },
        };
        BlockCost saw;
        for (const auto& [label, generator] : generators)
        {
            auto s = base();
            s.voice.generators[0] = generator;
            const auto c = measure(s);
            if (label == "VA saw")
                saw = c;
            line(label, c, empty);
        }
        const std::vector<std::pair<juce::String, int>> processors {
            { "VA saw + Ladder", processorType::ladder }, { "VA saw + SVF", processorType::svf },
            { "VA saw + Crusher", processorType::crusher }, { "VA saw + Folder", processorType::folder },
            { "VA saw + Drive", processorType::drive }, { "VA saw + Formant", processorType::formant },
            { "VA saw + Comb", processorType::comb },
        };
        for (const auto& [label, type] : processors)
        {
            auto s = base();
            s.voice.generators[0] = generators.front().second;
            s.voice.processors[0] = processorOf(type);
            line(label, measure(s), saw);
        }
        {
            // The SVF's SID 6581 character, priced on its own (D233).
            auto s = base();
            s.voice.generators[0] = generators.front().second;
            s.voice.processors[0] = processorOf(processorType::svf);
            const auto& d = SvfProcessor::descriptor();
            s.voice.processors[0].p[SvfProcessor::character] = unitsToNormalised(d.params[SvfProcessor::character], 1.0f);
            s.voice.processors[0].p[SvfProcessor::resonance] = unitsToNormalised(d.params[SvfProcessor::resonance], 1.0f);
            line("VA saw + SVF 6581", measure(s), saw);
        }
        {
            // The 808 Drums in the Kit play mode (D252): one voice holds the kit, so its price is that voice's whole
            // cost, at its 99th percentile, as the Kick's circuits. Its hardest: every instrument struck at once at
            // full accent with the bass drum's glide at its longest, then the other of each pair (congas, claves)
            // while the first ring, then the first again. In the other play modes a voice holds the one instrument
            // its key strikes: each alone, the dearest its price there.
            auto kitCost = [&](const GeneratorSettings& generator, const std::vector<int>& firstNotes, const std::vector<int>& secondNotes)
            {
                BlockCost best { 1.0e9, 1.0e9, 1.0e9 };
                for (int run = 0; run < 3; ++run)
                {
                    constexpr double rate = 48000.0;
                    constexpr int block = 256;
                    constexpr int blocks = static_cast<int>(3.0 * rate) / block;
                    auto s = base();
                    s.playMode = PlayMode::kit;
                    s.voice.generators[0] = generator;
                    auto part = std::make_unique<Part>();
                    part->prepare(rate, block, 7);
                    std::vector<float> left(block), right(block);
                    auto hits = [](const std::vector<int>& notes)
                    {
                        std::vector<coolsynth::synth::EngineMidiEvent> events;
                        for (const int note : notes)
                        {
                            coolsynth::synth::EngineMidiEvent e;
                            e.type = coolsynth::synth::EngineMidiEventType::noteOn;
                            e.noteNumber = static_cast<uint8_t>(note);
                            e.value = 1.0f;
                            events.push_back(e);
                        }
                        return events;
                    };
                    const auto first = hits(firstNotes), second = hits(secondNotes);
                    std::vector<double> shares;
                    for (int b = 0; b < blocks; ++b)
                    {
                        std::fill(left.begin(), left.end(), 0.0f);
                        std::fill(right.begin(), right.end(), 0.0f);
                        const auto& events = b == blocks / 3 ? second : first;
                        const bool strike = b == 0 || b == blocks / 3 || b == 2 * blocks / 3;
                        const auto start = juce::Time::getHighResolutionTicks();
                        part->render(left.data(), right.data(), block,
                                     strike ? std::span<const coolsynth::synth::EngineMidiEvent>(events) : std::span<const coolsynth::synth::EngineMidiEvent>(),
                                     s, {});
                        shares.push_back(100.0 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) / (block / rate));
                    }
                    const auto c = costOf(std::move(shares));
                    best.average = std::min(best.average, c.average);
                    best.p99 = std::min(best.p99, c.p99);
                    best.worst = std::min(best.worst, c.worst);
                }
                return best;
            };
            const auto drums = generatorOf(generatorType::drums808, { { Tr808DrumsGenerator::accent, 1.0f },
                                                                      { Tr808DrumsGenerator::punchTime, 500.0f },
                                                                      { Tr808DrumsGenerator::bdDecay, 1.0f } });
            // The 808 Metal (D255) likewise: every instrument at once at full accent, the decays at their longest.
            const auto metal = generatorOf(generatorType::metal808, { { Tr808MetalGenerator::accent, 1.0f },
                                                                      { Tr808MetalGenerator::cyDecay, 1.0f },
                                                                      { Tr808MetalGenerator::ohDecay, 1.0f },
                                                                      { Tr808MetalGenerator::cbDecay, 4.0f },
                                                                      { Tr808MetalGenerator::chDecay, 4.0f } });
            const struct { const char* label; const GeneratorSettings* generator; std::vector<int> first, second; } sets[] = {
                { "808 Drums, kit, all struck", &drums, { 36, 37, 43, 47, 50 }, { 35, 75, 64, 63, 62 } },
                { "808 Drums, bass drum alone", &drums, { 36 }, { 35 } },
                { "808 Drums, low tom, conga", &drums, { 43 }, { 64 } },
                { "808 Drums, mid tom, conga", &drums, { 47 }, { 63 } },
                { "808 Drums, high tom, conga", &drums, { 50 }, { 62 } },
                { "808 Drums, rim shot, claves", &drums, { 37 }, { 75 } },
                { "808 Metal, kit, all struck", &metal, { 49, 46, 56, 42 }, { 51, 44, 56 } },
                { "808 Metal, cowbell alone", &metal, { 56 }, { 56 } },
                { "808 Metal, cymbal alone", &metal, { 49 }, { 51 } },
                { "808 Metal, open hat alone", &metal, { 46 }, { 46 } },
                { "808 Metal, closed hat alone", &metal, { 42 }, { 44 } },
            };
            for (const auto& set : sets)
            {
                const auto best = kitCost(*set.generator, set.first, set.second);
                logMessage(juce::String(set.label).paddedRight(' ', 30) + "average " + juce::String(best.average, 2) + " %, p99 "
                           + juce::String(best.p99, 2) + " %, slowest " + juce::String(best.worst, 2) + " % (one voice)");
            }
        }

        // Each effect once for the sound, at its defaults, on a loud stereo signal; Pitch in both modes.
        beginTest("worst_case_of_each_effect");
        struct Effect { juce::String label; InsertSettings settings; };
        std::vector<Effect> effects;
        for (int type = 1; type < insertType::count; ++type)
            effects.push_back({ insertDescriptor(type).name, InsertSettings::of(type) });
        effects.push_back({ "Pitch, spectral", InsertSettings::of(insertType::pitch, { { 8, 1.0f } }) });
        effects.push_back({ "Grain, densest", InsertSettings::of(insertType::grain, { { 0, 500.0f }, { 1, 80.0f } }) });
        for (const auto& effect : effects)
        {
            BlockCost best { 1.0e9, 1.0e9, 1.0e9 };
            for (int run = 0; run < 3; ++run)
            {
                constexpr double rate = 48000.0;
                constexpr int block = 256;
                constexpr int blocks = static_cast<int>(3.0 * rate) / block;
                auto slot = std::make_unique<InsertSlot>();
                slot->prepare(rate, block, 3);
                juce::AudioBuffer<float> stereo(2, block);
                std::uint32_t noise = 1u;
                double phase = 0.0;
                std::vector<double> shares;
                InsertContext context;
                context.beatsPerSample = 2.0 / rate;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int s = 0; s < block; ++s)
                    {
                        noise = noise * 1664525u + 1013904223u;
                        phase += 110.0 / rate;
                        phase -= std::floor(phase);
                        const float saw = static_cast<float>(2.0 * phase - 1.0) * 0.4f;
                        const float n = static_cast<float>(noise >> 8) * (2.0f / 16777216.0f) - 1.0f;
                        stereo.setSample(0, s, saw + 0.1f * n);
                        stereo.setSample(1, s, saw - 0.1f * n);
                    }
                    context.beatsAtStart = b * block * context.beatsPerSample;
                    const auto start = juce::Time::getHighResolutionTicks();
                    slot->process(stereo, effect.settings, context);
                    shares.push_back(100.0 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) / (block / rate));
                }
                const auto c = costOf(std::move(shares));
                best.average = std::min(best.average, c.average);
                best.p99 = std::min(best.p99, c.p99);
                best.worst = std::min(best.worst, c.worst);
            }
            logMessage(effect.label.paddedRight(' ', 30) + "average " + juce::String(best.average, 2) + " %, p99 "
                       + juce::String(best.p99, 2) + " %, slowest " + juce::String(best.worst, 2) + " %");
        }
    }
};

static V3WorstCaseCost v3WorstCaseCost;

/*
    Opt-in (set COOLSYNTH_VA_FINGERPRINT): a fingerprint of the VA generator's exact output in each of its modes,
    every bit of every sample, to show that a faster VA plays the very same numbers (TODO 9). Compare the lines
    printed before and after a change on the same machine and build; they are not kept as expected values, as
    another compiler or processor may round differently.
*/
class V3VaFingerprint final : public juce::UnitTest
{
public:
    V3VaFingerprint() : juce::UnitTest("V3 VA fingerprint", "V3") {}

    void runTest() override
    {
        beginTest("the_va_output_bit_for_bit");
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_VA_FINGERPRINT", {}).isEmpty())
        {
            logMessage("skipped: set COOLSYNTH_VA_FINGERPRINT=1 to print");
            return;
        }
        struct Case { const char* name; float shape; int unison; Combine combine; float amount; float detune; float sub; };
        const Case cases[] {
            { "sine", 0.0f, 1, Combine::off, 0.0f, 20.0f, 0.0f },     { "triangle", 1.0f, 1, Combine::off, 0.0f, 20.0f, 0.0f },
            { "saw", 2.0f, 1, Combine::off, 0.0f, 20.0f, 0.0f },      { "pulse", 3.0f, 1, Combine::off, 0.0f, 20.0f, 0.0f },
            { "morph 1.4", 1.4f, 1, Combine::off, 0.0f, 20.0f, 0.0f }, { "morph 2.6", 2.6f, 1, Combine::off, 0.0f, 20.0f, 0.0f },
            { "supersaw", 2.0f, 7, Combine::off, 0.0f, 60.0f, 0.0f },  { "unison 4", 2.0f, 4, Combine::off, 0.0f, 150.0f, 0.0f },
            { "saw + sub", 2.0f, 3, Combine::off, 0.0f, 20.0f, 0.7f }, { "sync", 2.0f, 1, Combine::sync, 0.5f, 20.0f, 0.0f },
            { "fm", 2.0f, 2, Combine::fm, 0.8f, 20.0f, 0.0f },         { "ring", 3.0f, 1, Combine::ring, 0.6f, 20.0f, 0.0f },
            { "am", 1.0f, 1, Combine::am, 0.6f, 20.0f, 0.0f },
            { "pulse unison 5", 3.0f, 5, Combine::off, 0.0f, 40.0f, 0.0f }, { "triangle unison 3", 1.0f, 3, Combine::off, 0.0f, 30.0f, 0.0f },
            { "morph 2.6 unison 7", 2.6f, 7, Combine::off, 0.0f, 80.0f, 0.3f }, { "sine unison 7", 0.0f, 7, Combine::off, 0.0f, 30.0f, 0.0f },
            { "supersaw ring", 2.0f, 7, Combine::ring, 0.5f, 50.0f, 0.0f },
        };
        constexpr int sub = 32;
        // A step that changes the order of the sums (D219) cannot keep every bit: record the output to a folder
        // before it (COOLSYNTH_VA_RECORD) and compare after (COOLSYNTH_VA_COMPARE), which prints the largest
        // difference against the signal's peak.
        const juce::File recordTo(juce::SystemStats::getEnvironmentVariable("COOLSYNTH_VA_RECORD", {}));
        const juce::File compareWith(juce::SystemStats::getEnvironmentVariable("COOLSYNTH_VA_COMPARE", {}));
        for (const auto& c : cases)
        {
            // The previous generator: a plain VA a fifth under, for sync, FM, ring and AM.
            SlotMemory masterMemory, memory;
            masterMemory.allocate(VaGenerator::memoryFloats(96000.0, sub));
            memory.allocate(VaGenerator::memoryFloats(96000.0, sub));
            auto master = std::make_unique<VaGenerator>();
            auto va = std::make_unique<VaGenerator>();
            master->prepare(96000.0, sub, masterMemory);
            va->prepare(96000.0, sub, memory);
            master->noteOn({});
            va->noteOn({});
            SlotParams mp, p;
            mp.p[VaGenerator::shape] = 2.0f;
            mp.p[VaGenerator::unison] = 1.0f;
            mp.p[VaGenerator::pulseWidth] = 0.5f;
            mp.frequencyHz = 293.66f;
            p.p[VaGenerator::shape] = c.shape;
            p.p[VaGenerator::pulseWidth] = 0.3f;
            p.p[VaGenerator::unison] = static_cast<float>(c.unison);
            p.p[VaGenerator::detune] = c.detune;
            p.p[VaGenerator::spread] = 0.6f;
            p.p[VaGenerator::mix] = 0.67f;
            p.p[VaGenerator::randomPhase] = 1.0f;
            p.p[VaGenerator::sub] = c.sub;
            p.combine = c.combine;
            p.combineAmount = c.amount;
            std::uint64_t hash = 1469598103934665603ull;
            std::vector<float> ml(sub), mr(sub), l(sub), r(sub), all;
            all.reserve(6000 * sub * 4);
            for (int block = 0; block < 6000; ++block) // two seconds at 96 kHz, a glide up an octave on the way
            {
                p.frequencyHz = 440.0f * std::exp2(static_cast<float>(block) / 6000.0f);
                master->process(ml.data(), mr.data(), sub, mp);
                va->setLink(master->link());
                va->process(l.data(), r.data(), sub, p);
                for (int s = 0; s < sub; ++s)
                    for (const float v : { l[static_cast<std::size_t>(s)], r[static_cast<std::size_t>(s)], va->link().signal[s], va->link().wraps[s] })
                    {
                        std::uint32_t bits;
                        std::memcpy(&bits, &v, sizeof(bits));
                        hash = (hash ^ bits) * 1099511628211ull;
                        all.push_back(v);
                    }
            }
            juce::String line = juce::String("VA ") + c.name + ": " + juce::String::toHexString(static_cast<juce::int64>(hash));
            const auto fileName = juce::String("va-") + juce::String(c.name).replaceCharacter(' ', '-').replaceCharacter('+', 'p') + ".raw";
            if (recordTo != juce::File())
            {
                recordTo.createDirectory();
                recordTo.getChildFile(fileName).replaceWithData(all.data(), all.size() * sizeof(float));
            }
            if (compareWith != juce::File())
            {
                juce::MemoryBlock before;
                if (compareWith.getChildFile(fileName).loadFileAsData(before) && before.getSize() == all.size() * sizeof(float))
                {
                    const auto* old = static_cast<const float*>(before.getData());
                    float largest = 0.0f, peak = 0.0f;
                    for (std::size_t i = 0; i < all.size(); ++i)
                    {
                        largest = std::max(largest, std::abs(all[i] - old[i]));
                        peak = std::max(peak, std::abs(old[i]));
                    }
                    line << ", largest difference " << (largest == 0.0f ? juce::String("none") : juce::String(20.0f * std::log10(largest / peak), 1) + " dB under the peak");
                    expect(largest <= peak * 1.0e-5f, juce::String(c.name) + ": a difference over -100 dB");
                }
                else
                    line << ", nothing recorded to compare";
            }
            logMessage(line);
        }

        // The ladder the same way (TODO 9, D219): a detuned saw pair with a little noise, through each mode, at gentle,
        // strong and singing resonance, with Drive, Bass, the shaking cutoff (FM) and Leak, the cutoff sweeping.
        struct LadderCase { const char* name; float mode, res, drive, bass, fm, leak, inputLevel; };
        const LadderCase ladders[] {
            { "lp24", 0.0f, 0.1f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f },       { "lp12", 1.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f },
            { "bp", 2.0f, 0.7f, 6.0f, 0.0f, 0.0f, 0.0f, 0.5f },         { "hp12", 3.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.05f, 0.5f },
            { "singing", 0.0f, 1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.2f },    { "drive 24", 0.0f, 0.85f, 24.0f, 1.0f, 0.0f, 0.0f, 1.0f },
            { "fm", 0.0f, 0.6f, 3.0f, 0.5f, 1.5f, 0.1f, 0.7f },         { "loud", 1.0f, 0.95f, 12.0f, 1.0f, 0.0f, 0.0f, 2.0f },
        };
        for (const auto& c : ladders)
        {
            SlotMemory memory;
            memory.allocate(0);
            auto ladder = std::make_unique<LadderProcessor>();
            ladder->prepare(96000.0, sub, memory);
            SlotParams p;
            p.p[LadderProcessor::resonance] = c.res;
            p.p[LadderProcessor::drive] = c.drive;
            p.p[LadderProcessor::keyTrack] = 0.5f;
            p.p[LadderProcessor::mode] = c.mode;
            p.p[LadderProcessor::bass] = c.bass;
            p.p[LadderProcessor::fm] = c.fm;
            p.p[LadderProcessor::leak] = c.leak;
            p.frequencyHz = 220.0f;
            std::uint64_t hash = 1469598103934665603ull;
            std::vector<float> l(sub), r(sub), shake(sub);
            std::uint32_t noise = 12345u;
            double phaseA = 0.0, phaseB = 0.0, phaseC = 0.0;
            for (int block = 0; block < 6000; ++block)
            {
                p.p[LadderProcessor::cutoff] = 40.0f * std::exp2(9.0f * static_cast<float>(block % 3000) / 3000.0f); // 40 Hz to 20 kHz
                for (int s = 0; s < sub; ++s)
                {
                    noise = noise * 1664525u + 1013904223u;
                    const float n = static_cast<float>(noise >> 8) * (2.0f / 16777216.0f) - 1.0f;
                    phaseA += 220.0 / 96000.0;
                    phaseB += 221.3 / 96000.0;
                    phaseC += 110.0 / 96000.0;
                    phaseA -= std::floor(phaseA);
                    phaseB -= std::floor(phaseB);
                    phaseC -= std::floor(phaseC);
                    l[static_cast<std::size_t>(s)] = c.inputLevel * (static_cast<float>(2.0 * phaseA - 1.0) * 0.5f + 0.02f * n);
                    r[static_cast<std::size_t>(s)] = c.inputLevel * (static_cast<float>(2.0 * phaseB - 1.0) * 0.5f - 0.02f * n);
                    shake[static_cast<std::size_t>(s)] = static_cast<float>(std::sin(6.283185307179586 * phaseC));
                }
                p.modulator = shake.data();
                ladder->process(l.data(), r.data(), sub, p);
                for (int s = 0; s < sub; ++s)
                    for (const float v : { l[static_cast<std::size_t>(s)], r[static_cast<std::size_t>(s)] })
                    {
                        std::uint32_t bits;
                        std::memcpy(&bits, &v, sizeof(bits));
                        hash = (hash ^ bits) * 1099511628211ull;
                    }
            }
            logMessage(juce::String("Ladder ") + c.name + ": " + juce::String::toHexString(static_cast<juce::int64>(hash)));
        }
    }
};

static V3VaFingerprint v3VaFingerprint;

static V3ModuleCostReport v3ModuleCostReport;
