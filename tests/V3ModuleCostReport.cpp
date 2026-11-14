#include <juce_core/juce_core.h>

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

static V3ModuleCostReport v3ModuleCostReport;
