#include <juce_core/juce_core.h>

#include <cstring>
#include <memory>
#include <vector>

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
        };
        constexpr int sub = 32;
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
            std::vector<float> ml(sub), mr(sub), l(sub), r(sub);
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
                    }
            }
            logMessage(juce::String("VA ") + c.name + ": " + juce::String::toHexString(static_cast<juce::int64>(hash)));
        }
    }
};

static V3VaFingerprint v3VaFingerprint;

static V3ModuleCostReport v3ModuleCostReport;
