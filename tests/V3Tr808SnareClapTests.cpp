#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <vector>

#include "synth/v3/Part.h"
#include "synth/v3/Tr808Clap.h"
#include "synth/v3/Tr808Snare.h"
#include "synth/v3/Tr808SnareClapGenerator.h"

/*
    The 808 Snare & Clap's circuits (D256) against ngspice renders of the same schematics (tools/tr808/snare.cir and
    clap.cir, rendered by tools/tr808/render.py; the references are in tests/data/tr808): each stage's node, sample by
    sample, over the first 40 ms of a hit (45 for the clap, whose fourth clap comes at 31 ms). The white noise is two
    sines on both sides, so the noise paths compare sample by sample too.
*/
namespace
{
    using coolsynth::synth::v3::Tr808Clap;
    using coolsynth::synth::v3::Tr808Snare;

    struct Reference
    {
        std::map<juce::String, std::vector<double>> columns;
        double rate = 96000.0;
        std::size_t size() const { return columns.empty() ? 0 : columns.begin()->second.size(); }
    };

    Reference loadReference(const juce::File& file)
    {
        Reference r;
        juce::StringArray lines;
        file.readLines(lines);
        if (lines.size() < 3)
            return r;
        const auto names = juce::StringArray::fromTokens(lines[0], ",", "");
        double t0 = 0.0, t1 = 0.0;
        for (int i = 1; i < lines.size(); ++i)
        {
            const auto f = juce::StringArray::fromTokens(lines[i], ",", "");
            if (f.size() < names.size())
                continue;
            if (i == 1)
                t0 = f[0].getDoubleValue();
            if (i == 2)
                t1 = f[0].getDoubleValue();
            for (int c = 1; c < names.size(); ++c)
                r.columns[names[c]].push_back(f[c].getDoubleValue());
        }
        if (t1 > t0)
            r.rate = 1.0 / (t1 - t0);
        return r;
    }

    juce::File referenceFolder()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_REFERENCE", {});
        if (env.isNotEmpty())
            return juce::File(env);
        return juce::File(__FILE__).getParentDirectory().getChildFile("data").getChildFile("tr808");
    }

    /** The rms difference over the rms of the reference (each less its first value when `signalOnly`). */
    double relativeError(std::vector<double> model, std::vector<double> spice, bool signalOnly)
    {
        if (signalOnly && ! model.empty() && ! spice.empty())
        {
            const double m0 = model.front(), s0 = spice.front();
            for (auto& v : model)
                v -= m0;
            for (auto& v : spice)
                v -= s0;
        }
        double e = 0.0, s = 0.0;
        const std::size_t n = std::min(model.size(), spice.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            e += (model[i] - spice[i]) * (model[i] - spice[i]);
            s += spice[i] * spice[i];
        }
        return std::sqrt(e / std::max(1.0e-30, s));
    }

    /** The test sines on the white noise bus, as the references' VWA and VWB. */
    double sines(double t, double amplitude, double fa, double fb)
    {
        constexpr double twoPi = 6.283185307179586;
        return amplitude * (std::sin(twoPi * fa * t) + std::sin(twoPi * fb * t));
    }

    /** How a node is held to its reference: sample by sample; by its envelope (1 ms rms windows of its signal); by
        its running integral. */
    enum class Check { sample, envelope, integral };

    struct NodeCheck
    {
        const char* name;
        bool signalOnly; // compared less its bias
        double limit;
        Check check = Check::sample;
    };

    /** The node's error by its kind of check. */
    double checkError(std::vector<double> model, std::vector<double> spice, const NodeCheck& node, double rate)
    {
        if (node.check == Check::sample)
            return relativeError(std::move(model), std::move(spice), node.signalOnly);
        const std::size_t n = std::min(model.size(), spice.size());
        const double m0 = model.empty() ? 0.0 : model.front(), s0 = spice.empty() ? 0.0 : spice.front();
        std::vector<double> m, s;
        if (node.check == Check::envelope)
        {
            const auto window = static_cast<std::size_t>(std::max(1.0, 0.001 * rate));
            for (std::size_t start = 0; start + window <= n; start += window)
            {
                double em = 0.0, es = 0.0;
                for (std::size_t i = start; i < start + window; ++i)
                {
                    em += (model[i] - m0) * (model[i] - m0);
                    es += (spice[i] - s0) * (spice[i] - s0);
                }
                m.push_back(std::sqrt(em / static_cast<double>(window)));
                s.push_back(std::sqrt(es / static_cast<double>(window)));
            }
        }
        else
        {
            double cm = 0.0, cs = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                cm += model[i] - m0;
                cs += spice[i] - s0;
                m.push_back(cm);
                s.push_back(cs);
            }
        }
        return relativeError(m, s, false);
    }

    using Columns = std::map<juce::String, std::vector<double>>;
}

class V3Tr808SnareClapTests final : public juce::UnitTest
{
public:
    V3Tr808SnareClapTests() : juce::UnitTest("V3 TR-808 snare and clap circuits", "V3") {}

    void runTest() override
    {
        beginTest("the_circuits_are_well_posed_and_rest_quietly");
        {
            auto snare = std::make_unique<Tr808Snare>();
            auto clap = std::make_unique<Tr808Clap>();
            expect(snare->isWellPosed(), "the snare's circuits");
            expect(clap->isWellPosed(), "the clap's circuits");
            // At rest, with the noise bus running, neither makes a sound: the snappy's VCA has no supply, the reverb's
            // Q70 sits off at the C.P. OFFSET's voltage (through D68 and D69), the bursts' pair has Q71 on.
            snare->prepare(96000.0);
            clap->prepare(96000.0);
            double snarePeak = 0.0, clapPeak = 0.0;
            for (int i = 0; i < 9600; ++i)
            {
                const double noise = sines(i / 96000.0, 0.1, 1100.0, 2300.0);
                snarePeak = std::max(snarePeak, std::abs(snare->process(noise)));
                clapPeak = std::max(clapPeak, std::abs(clap->process(noise)));
            }
            logMessage("at rest: snare " + juce::String(snarePeak, 9) + " V, clap " + juce::String(clapPeak, 9) + " V");
            expectLessThan(snarePeak, 1.0e-5); // -100 dB under a volt
            expectLessThan(clapPeak, 1.0e-5);
        }

        beginTest("the_snare_matches_spice_stage_by_stage");
        {
            // IC14a's input is a spike shorter than a sample (C57 into R190, 4.6 us): held to its integral, which is
            // what the shell takes (the shell itself sample by sample). At full accent the snappy's swing VCA Q48
            // switches inside a sample on every swing of the noise, the high-pass after it turning the edges into
            // spikes: its nodes are held to their envelopes there (some 3 % of charge, 0.3 dB, moves with the edges).
            const NodeCheck plain[] = { { "vt", false, 0.05 }, { "np1", false, 0.05, Check::integral }, { "np2", false, 0.05 },
                                        { "no1", false, 0.05 }, { "no2", false, 0.05 }, { "ne47", false, 0.05 }, { "n52", false, 0.05 },
                                        { "nsn", false, 0.05 }, { "nc48", false, 0.05 }, { "ne49", true, 0.07 }, { "vout", false, 0.05 } };
            const NodeCheck accented[] = { { "vt", false, 0.05 }, { "np1", false, 0.07, Check::integral }, { "np2", false, 0.05 },
                                           { "no1", false, 0.05 }, { "no2", false, 0.05 }, { "ne47", false, 0.05 }, { "n52", false, 0.05 },
                                           { "nsn", false, 0.08, Check::envelope }, { "nc48", false, 0.08, Check::envelope },
                                           { "ne49", true, 0.05, Check::envelope }, { "vout", false, 0.05, Check::envelope } };
            for (const auto& [file, volts] : { std::pair { "sd.csv", 4.0 }, std::pair { "sd14.csv", 14.0 } })
            {
                const auto ref = load(file);
                if (ref.size() == 0)
                    continue;
                auto snare = std::make_unique<Tr808Snare>();
                snare->prepare(ref.rate);
                snare->setTone(0.5);
                snare->setSnappy(0.5);
                snare->setLevel(0.8);
                snare->settle();
                snare->trigger(volts);
                Columns model;
                int worstNewton = 0;
                bool converged = true;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    model["vout"].push_back(snare->process(sines(static_cast<double>(i) / ref.rate, 0.1, 1700.0, 5300.0)));
                    model["vt"].push_back(snare->triggerNodeVolts());
                    model["np1"].push_back(snare->lowerPulseVolts());
                    model["np2"].push_back(snare->upperPulseVolts());
                    model["no1"].push_back(snare->lowerShellVolts());
                    model["no2"].push_back(snare->upperShellVolts());
                    model["ne47"].push_back(snare->c51Volts());
                    model["n52"].push_back(snare->c52Volts());
                    model["nsn"].push_back(snare->envelopeVolts());
                    model["nc48"].push_back(snare->collectorVolts());
                    model["ne49"].push_back(snare->followerVolts());
                    worstNewton = std::max(worstNewton, snare->newtonSteps());
                    converged = converged && snare->lastSolveConverged();
                }
                if (volts > 10.0)
                    compare(file, ref, model, accented, worstNewton, converged);
                else
                    compare(file, ref, model, plain, worstNewton, converged);
            }
        }

        beginTest("the_clap_matches_spice_stage_by_stage");
        {
            // IC23's pins 2 and 5 switch inside a sample (the reference's switch at its crossing, the model's at the
            // sample after it): their narrow pulses hold to 6 %.
            const NodeCheck nodes[] = { { "vt", false, 0.05 }, { "n8", false, 0.05 }, { "n139", false, 0.05 }, { "n143", false, 0.05 },
                                        { "no21", false, 0.05 }, { "n138", false, 0.05 }, { "nb70", true, 0.05 }, { "nc70", true, 0.05 },
                                        { "n14", false, 0.05 }, { "n1", false, 0.05 }, { "n13", false, 0.05 }, { "n144", false, 0.05 },
                                        { "n2", false, 0.06 }, { "n5", false, 0.06 }, { "nb72", false, 0.05 }, { "nh", false, 0.05 },
                                        { "iabc", false, 0.05 }, { "np6", false, 0.05 }, { "vout", false, 0.05 } };
            for (const auto& [file, volts] : { std::pair { "cp.csv", 4.0 }, std::pair { "cp14.csv", 14.0 } })
            {
                auto ref = load(file);
                if (ref.size() == 0)
                    continue;
                // The control current, as the reference's Q72 collector over R374 to pin 1.
                // (Its own column, filled from the reference's length: a new key sorts first, and size() reads the first.)
                const std::size_t length = ref.columns.at("nc72").size();
                std::vector<double> iabc;
                for (std::size_t i = 0; i < length; ++i)
                    iabc.push_back((ref.columns.at("nc72")[i] - ref.columns.at("np1")[i]) / 10.0e3);
                ref.columns["iabc"] = std::move(iabc);
                auto clap = std::make_unique<Tr808Clap>();
                clap->prepare(ref.rate);
                clap->setLevel(0.8);
                clap->settle();
                clap->trigger(volts);
                Columns model;
                int worstNewton = 0;
                bool converged = true;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    model["vout"].push_back(clap->process(sines(static_cast<double>(i) / ref.rate, 0.03, 1100.0, 2300.0)));
                    model["vt"].push_back(clap->triggerNodeVolts());
                    model["n8"].push_back(clap->pin8Volts());
                    model["n139"].push_back(clap->c139Volts());
                    model["n143"].push_back(clap->c143Volts());
                    model["no21"].push_back(clap->bandPassVolts());
                    model["n138"].push_back(clap->c138Volts());
                    model["nb70"].push_back(clap->reverbBaseVolts());
                    model["nc70"].push_back(clap->reverbCollectorVolts());
                    model["n14"].push_back(clap->c140Volts());
                    model["n1"].push_back(clap->pin1Volts());
                    model["n13"].push_back(clap->pin13Volts());
                    model["n144"].push_back(clap->c144Volts());
                    model["n2"].push_back(clap->pin2Volts());
                    model["n5"].push_back(clap->pin5Volts());
                    model["nb72"].push_back(clap->q72BaseVolts());
                    model["nh"].push_back(clap->q71BaseVolts());
                    model["iabc"].push_back(clap->abcAmps());
                    model["np6"].push_back(clap->otaVolts());
                    worstNewton = std::max(worstNewton, clap->newtonSteps());
                    converged = converged && clap->lastSolveConverged();
                }
                compare(file, ref, model, nodes, worstNewton, converged);
                logMessage(juce::String(file) + ": claps at " + onsets(model["n2"], ref.rate) + " ms against " + onsets(ref.columns.at("n2"), ref.rate));
            }
        }

        beginTest("the_maracas_match_spice_stage_by_stage");
        {
            // Q65 switches the noise inside a sample and Q68's high-pass rings at 10.6 kHz on each edge (prewarped to
            // it; the rest of its curve still warps): its output and the mix are held to their envelopes.
            const NodeCheck nodes[] = { { "vt", false, 0.05 }, { "n135", false, 0.05 }, { "nb67", false, 0.05 }, { "nb66", false, 0.05 },
                                        { "nm3", false, 0.05 }, { "ne68", true, 0.05, Check::envelope }, { "vout", false, 0.05, Check::envelope } };
            for (const auto& [file, volts] : { std::pair { "ma.csv", 4.0 }, std::pair { "ma14.csv", 14.0 } })
            {
                const auto ref = load(file);
                if (ref.size() == 0)
                    continue;
                auto clap = std::make_unique<Tr808Clap>();
                clap->prepare(ref.rate);
                clap->setMaracas(true);
                clap->setLevel(0.8);
                clap->settle();
                clap->trigger(volts);
                Columns model;
                int worstNewton = 0;
                bool converged = true;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    model["vout"].push_back(clap->process(sines(static_cast<double>(i) / ref.rate, 0.1, 1100.0, 2300.0)));
                    model["vt"].push_back(clap->triggerNodeVolts());
                    model["n135"].push_back(clap->c135Volts());
                    model["nb67"].push_back(clap->q67BaseVolts());
                    model["nb66"].push_back(clap->maracasEnvelopeVolts());
                    model["nm3"].push_back(clap->maracasNoiseVolts());
                    model["ne68"].push_back(clap->maracasFollowerVolts());
                    worstNewton = std::max(worstNewton, clap->newtonSteps());
                    converged = converged && clap->lastSolveConverged();
                }
                compare(file, ref, model, nodes, worstNewton, converged);
            }
        }

        if (const auto dump = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_SNARECLAP_DUMP", {}); dump.isNotEmpty())
            dumpRenders(juce::File(dump));
    }

private:
    Reference load(const char* name)
    {
        const auto file = referenceFolder().getChildFile(name);
        if (! file.existsAsFile())
        {
            expect(false, juce::String("the SPICE reference is missing: ") + name);
            return {};
        }
        return loadReference(file);
    }

    template <std::size_t N>
    void compare(const char* file, const Reference& ref, Columns& model, const NodeCheck (&nodes)[N], int worstNewton, bool converged)
    {
        juce::String line = juce::String(file) + ":";
        expectGreaterThan(static_cast<int>(model["vout"].size()), 1000, juce::String(file) + ": the hit was rendered and compared");
        for (const auto& node : nodes)
        {
            const auto it = ref.columns.find(node.name);
            if (it == ref.columns.end())
            {
                expect(false, juce::String(file) + " has no column " + node.name);
                continue;
            }
            const double err = checkError(model[node.name], it->second, node, ref.rate);
            line << " " << node.name << (node.check == Check::envelope ? " (envelope) " : node.check == Check::integral ? " (integral) " : " ")
                 << juce::String(err, 4);
            expectLessThan(err, node.limit, juce::String(file) + " " + node.name);
        }
        line << " | most Newton steps " << worstNewton;
        logMessage(line);
        expect(converged, juce::String(file) + ": every sample converged");
    }

    /** The times pin 2 rises past -7 V: the claps' pulses. */
    static juce::String onsets(const std::vector<double>& pin2, double rate)
    {
        juce::String s;
        for (std::size_t i = 1; i < pin2.size(); ++i)
            if (pin2[i - 1] <= -7.0 && pin2[i] > -7.0)
                s << juce::String(1000.0 * static_cast<double>(i) / rate, 2) << " ";
        return s.trim();
    }

    /** Long renders through the generator (its own noise bus), for the comparison with the units and page 14 (D256). */
    void dumpRenders(const juce::File& folder);
};

static V3Tr808SnareClapTests v3Tr808SnareClapTests;

namespace
{
    using namespace coolsynth::synth::v3;

    /** A generator at its defaults with these knobs (index, value 0..1), prepared at a rate with a seed. */
    struct Rig
    {
        SlotMemory memory;
        std::unique_ptr<Tr808SnareClapGenerator> generator = std::make_unique<Tr808SnareClapGenerator>();
        SlotParams params;

        Rig(double rate, std::uint32_t seed, std::initializer_list<std::pair<int, float>> knobs = {})
        {
            memory.allocate(Tr808SnareClapGenerator::memoryFloats(rate, 32));
            memory.setSeed(seed);
            generator->prepare(rate, 32, memory);
            const auto& d = Tr808SnareClapGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                params.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
            for (const auto& [index, value] : knobs)
                params.p[static_cast<std::size_t>(index)] = value;
        }

        /** Strikes a key at the start, then renders this long: the slot's output in circuit volts (centre pan undone). */
        std::vector<double> render(int note, float velocity, double seconds, double rate)
        {
            NoteContext n;
            n.noteNumber = note;
            n.velocity = velocity;
            generator->noteOn(n);
            std::vector<double> out;
            std::array<float, 32> l {}, r {};
            const int blocks = static_cast<int>(seconds * rate) / 32;
            const double scale = 1.0 / (Tr808SnareClapGenerator::circuitVoltsToLevel * std::cos(0.25 * 3.141592653589793));
            for (int b = 0; b < blocks; ++b)
            {
                generator->process(l.data(), r.data(), 32, params);
                for (const float v : l)
                    out.push_back(static_cast<double>(v) * scale);
            }
            return out;
        }
    };
}

void V3Tr808SnareClapTests::dumpRenders(const juce::File& folder)
{
    // vout only, one CSV each, at 96 kHz: the snare at both units' TONE and SNAPPY grids, the clap and the maracas
    // (several hits, each on its own noise), each also at full accent.
    beginTest("dump");
    const double rate = 96000.0;
    folder.createDirectory();
    const auto write = [&folder, rate](const juce::String& name, const std::vector<double>& v)
    {
        juce::String text = "time,vout\n";
        for (std::size_t i = 0; i < v.size(); ++i)
            text << juce::String(static_cast<double>(i) / rate, 9) << "," << juce::String(v[i], 7) << "\n";
        folder.getChildFile(name + ".csv").replaceWithText(text);
    };
    using G = Tr808SnareClapGenerator;
    const float marks[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (int t = 0; t < 5; ++t)
        for (int s = 0; s < 5; ++s)
        {
            Rig rig(rate, 7u, { { G::sdLevel, 1.0f }, { G::sdTone, marks[t] }, { G::sdSnappy, marks[s] }, { G::accent, 0.0f } });
            write("sd_t" + juce::String(t) + "_s" + juce::String(s), rig.render(38, 1.0f, 0.5, rate));
        }
    {
        Rig rig(rate, 7u, { { G::sdLevel, 1.0f }, { G::accent, 1.0f } });
        write("sd_acc", rig.render(38, 1.0f, 0.5, rate));
    }
    for (std::uint32_t seed = 1; seed <= 4; ++seed)
    {
        Rig clap(rate, seed, { { G::cpLevel, 1.0f }, { G::accent, 0.0f } });
        write("cp_" + juce::String(static_cast<int>(seed)), clap.render(39, 1.0f, 0.6, rate));
        Rig maracas(rate, seed, { { G::cpLevel, 1.0f }, { G::accent, 0.0f } });
        write("ma_" + juce::String(static_cast<int>(seed)), maracas.render(70, 1.0f, 0.2, rate));
        Rig clapAccent(rate, seed, { { G::cpLevel, 1.0f }, { G::accent, 1.0f } });
        write("cp_acc_" + juce::String(static_cast<int>(seed)), clapAccent.render(39, 1.0f, 0.6, rate));
        Rig maracasAccent(rate, seed, { { G::cpLevel, 1.0f }, { G::accent, 1.0f } });
        write("ma_acc_" + juce::String(static_cast<int>(seed)), maracasAccent.render(70, 1.0f, 0.2, rate));
    }
}

namespace
{
    GeneratorSettings snareClapOf(std::initializer_list<std::pair<int, float>> units = {})
    {
        GeneratorSettings g;
        g.type = generatorType::snareClap808;
        const auto& d = GeneratorSlot::typeTable().descriptor(g.type);
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, value] : units)
            g.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], value);
        g.level = 0.5f;
        return g;
    }

    PartSettings snareClapKit(std::initializer_list<std::pair<int, float>> units = {})
    {
        PartSettings s;
        s.playMode = PlayMode::kit;
        s.voice.generators[0] = snareClapOf(units);
        Envelope::Parameters amp;
        amp.attack = 0.0005f;
        amp.decay = 0.2f;
        amp.sustain = 1.0f;
        amp.release = 0.01f;
        s.voice.envelopes = { amp, amp, amp };
        return s;
    }

    struct KitHit
    {
        int sample, note;
        bool off = false;
    };

    /** A part playing these hits, host-rate mono out; the part's voices active at the end. */
    std::vector<float> playKit(const PartSettings& settings, const std::vector<KitHit>& hits, double seconds, int* activeAtEnd = nullptr)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        auto part = std::make_unique<Part>();
        part->prepare(rate, block, 11);
        const int blocks = static_cast<int>(seconds * rate) / block;
        std::vector<float> out, left(block), right(block);
        for (int b = 0; b < blocks; ++b)
        {
            std::vector<coolsynth::synth::EngineMidiEvent> events;
            for (const auto& h : hits)
                if (h.sample >= b * block && h.sample < (b + 1) * block)
                {
                    coolsynth::synth::EngineMidiEvent e;
                    e.type = h.off ? coolsynth::synth::EngineMidiEventType::noteOff : coolsynth::synth::EngineMidiEventType::noteOn;
                    e.sampleOffset = h.sample - b * block;
                    e.noteNumber = static_cast<std::uint8_t>(h.note);
                    e.value = h.off ? 0.0f : 1.0f;
                    events.push_back(e);
                }
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            part->render(left.data(), right.data(), block, events, settings, {});
            out.insert(out.end(), left.begin(), left.end());
        }
        if (activeAtEnd != nullptr)
            *activeAtEnd = part->activeVoiceCountForTesting();
        return out;
    }

    float kitPeak(const std::vector<float>& x, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1))
    {
        float p = 0.0f;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
            p = std::max(p, std::abs(x[i]));
        return p;
    }

    /** Upward zero crossings per second between two times. */
    double kitCrossingHz(const std::vector<float>& x, double rate, double from, double to)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (auto i = static_cast<std::size_t>(from * rate) + 1; i < std::min(x.size(), static_cast<std::size_t>(to * rate)); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                last = static_cast<double>(i - 1) + static_cast<double>(x[i - 1] / (x[i - 1] - x[i]));
                if (first < 0.0)
                    first = last;
                ++count;
            }
        return count > 1 ? rate * (count - 1) / (last - first) : 0.0;
    }
}

/*
    The 808 Snare & Clap in the Kit play mode (D256): its keys, the clap's switch set by the key, hits at once, the
    voice ending once everything has rung out, and a reset kit playing as a fresh one.
*/
class V3Tr808SnareClapKitTests final : public juce::UnitTest
{
public:
    V3Tr808SnareClapKitTests() : juce::UnitTest("V3 TR-808 snare and clap kit", "V3") {}

    void runTest() override
    {
        using G = Tr808SnareClapGenerator;

        beginTest("the_808_snare_and_clap_is_generator_type_fifteen_with_eleven_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 16);
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::snareClap808).name), juce::String("808 Snare & Clap"));
            expectEquals(G::descriptor().usedParams, 11);
            bool other = false;
            const std::pair<int, int> keys[] = { { 38, 0 }, { 40, 0 }, { 39, 1 }, { 70, 1 }, { 36, -1 }, { 42, -1 } };
            for (const auto& [note, instrument] : keys)
                expectEquals(G::instrumentFor(note, other), instrument, "key " + juce::String(note));
            (void) G::instrumentFor(70, other);
            expect(other, "the maracas are the clap's circuit's other side");
        }

        beginTest("the_snare_rings_at_its_two_shells");
        {
            // Accent 0, Snappy 0 so the noise stays out: Tone 0 the lower shell near 173 Hz (both units 172.3 to
            // 172.9 Hz); Tone 1 brings the upper shell (334 Hz; the units 331 to 339) in.
            const auto lowTone = playKit(snareClapKit({ { G::accent, 0.0f }, { G::sdTone, 0.0f }, { G::sdSnappy, 0.0f } }), { { 0, 38 } }, 0.3);
            const auto highTone = playKit(snareClapKit({ { G::accent, 0.0f }, { G::sdTone, 1.0f }, { G::sdSnappy, 0.0f } }), { { 0, 40 } }, 0.3);
            const double lowHz = kitCrossingHz(lowTone, 48000.0, 0.05, 0.2);
            const double highHz = kitCrossingHz(highTone, 48000.0, 0.01, 0.04);
            logMessage("snare, Tone 0: " + juce::String(lowHz, 1) + " Hz; Tone 1, early: " + juce::String(highHz, 1) + " Hz");
            expectWithinAbsoluteError(lowHz, 173.0, 4.0);
            expectGreaterThan(highHz, 250.0, "Tone up brings the upper shell in");
        }

        beginTest("the_maracas_key_switches_the_claps_circuit");
        {
            const auto clap = playKit(snareClapKit(), { { 0, 39 } }, 0.3);
            const auto maracas = playKit(snareClapKit(), { { 0, 70 } }, 0.3);
            const auto both = playKit(snareClapKit(), { { 0, 39 }, { 9600, 70 } }, 0.4);
            logMessage("clap peak " + juce::String(kitPeak(clap), 4) + ", maracas peak " + juce::String(kitPeak(maracas), 4)
                       + "; maracas over a ringing clap " + juce::String(kitPeak(both, 9600, 9600 + 2400), 4));
            expectGreaterThan(kitPeak(clap), 0.01f);
            expectGreaterThan(kitPeak(maracas), 0.01f);
            // The maracas end within some 50 ms; the clap's tail runs on past 100 ms.
            expectLessThan(kitPeak(maracas, 4800), 0.05f * kitPeak(maracas));
            expectGreaterThan(kitPeak(clap, 4800, 7200), 0.01f * kitPeak(clap));
            expectGreaterThan(kitPeak(both, 9600, 9600 + 2400), 0.01f, "the maracas sound over the clap's tail");
        }

        beginTest("note_offs_change_nothing_and_the_voice_ends");
        {
            const auto held = playKit(snareClapKit(), { { 0, 38 } }, 0.4);
            const auto tapped = playKit(snareClapKit(), { { 0, 38 }, { 100, 38, true } }, 0.4);
            expect(held == tapped, "a note-off leaves the drum ringing as it was");
            int activeEarly = 0, activeLate = 0;
            (void) playKit(snareClapKit(), { { 0, 38 }, { 0, 39 } }, 0.1, &activeEarly);
            (void) playKit(snareClapKit(), { { 0, 38 }, { 0, 39 } }, 4.0, &activeLate);
            expectEquals(activeEarly, 1, "one voice holds the kit while it rings");
            expectEquals(activeLate, 0, "and lets go once it has rung down");
        }

        beginTest("after_reset_a_hit_is_the_fresh_kits_bit_for_bit");
        {
            Rig fresh(96000.0, 5u), used(96000.0, 5u);
            (void) used.render(39, 1.0f, 0.1, 96000.0);
            used.generator->reset();
            used.generator->reseed();
            const auto a = fresh.render(38, 0.7f, 0.3, 96000.0);
            const auto b = used.render(38, 0.7f, 0.3, 96000.0);
            expect(a == b, "a reset kit plays its next hit as a fresh one, bit for bit");
        }
    }
};

static V3Tr808SnareClapKitTests v3Tr808SnareClapKitTests;

/*
    The clap's tail against both units (D258), opt-in: with COOLSYNTH_808_CLAP_FIT set to CP Decay knob turns (space
    separated; "1" for a default grid), the clap through the generator in-process (its own noise bus; no engine, so no
    overload rule), CP Level full, an unaccented 4 V hit, 96 kHz, four seeds; a 10 ms rms envelope in dB under its peak
    at fixed times after the onset, the seeds' median, against Fischer's CP and the median of Audiorealism's sixteen
    shots measured alike (scratchpad clapfit.py). Logged, nothing asserted.
*/
class V3Tr808ClapTailFit final : public juce::UnitTest
{
public:
    V3Tr808ClapTailFit() : juce::UnitTest("V3 TR-808 clap tail fit (opt-in)", "V3") {}

    static constexpr int timeCount = 10;
    static constexpr double times[timeCount] = { 0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.7, 1.0 };

    static std::array<double, timeCount> envelopeDb(const std::vector<double>& x, double rate)
    {
        double peakAbs = 0.0;
        for (const double v : x)
            peakAbs = std::max(peakAbs, std::abs(v));
        std::size_t onset = 0;
        while (onset < x.size() && std::abs(x[onset]) <= 0.02 * peakAbs)
            ++onset;
        const std::size_t k = static_cast<std::size_t>(std::max(3, static_cast<int>(0.010 * rate)));
        std::vector<double> env(x.size() - onset, 0.0);
        double sum = 0.0;
        for (std::size_t i = onset; i < x.size(); ++i)
        {
            sum += x[i] * x[i];
            if (i >= onset + k)
                sum -= x[i - k] * x[i - k];
            const std::size_t j = i - onset;
            if (j >= k / 2)
                env[j - k / 2] = std::sqrt(std::max(0.0, sum) / static_cast<double>(k));
        }
        const double top = *std::max_element(env.begin(), env.end());
        std::array<double, timeCount> out {};
        for (int t = 0; t < timeCount; ++t)
        {
            const auto i = std::min(env.size() - 1, static_cast<std::size_t>(times[t] * rate));
            out[static_cast<std::size_t>(t)] = 20.0 * std::log10(env[i] / top + 1.0e-12);
        }
        return out;
    }

    void runTest() override
    {
        const auto grid = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_CLAP_FIT", {});
        if (grid.isEmpty())
            return;
        beginTest("clap_tail_grid");
        using G = Tr808SnareClapGenerator;
        const double rate = 96000.0;
        const double fischer[timeCount] = { -7.9, -6.7, -22.2, -28.1, -32.4, -36.6, -39.8, -41.3, -46.5, -57.8 };
        const double audiorealism[timeCount] = { -4.3, -3.3, -18.8, -27.3, -31.5, -37.3, -42.0, -45.4, -49.6, -57.2 };
        // Each trial: a CP Decay turn (C139 and C138 together), or "c139:c138:r349", the parts' scales of the drawn ones
        // at CP Decay's middle, set on the circuit after the generator's first block has applied the knobs.
        struct Trial { float turn; std::array<double, 3> parts; bool byParts; };
        std::vector<Trial> trials;
        if (grid == "1")
            for (const float t : { 0.5f, 0.55f, 0.6f, 0.625f, 0.65f, 0.7f, 0.75f })
                trials.push_back({ t, { 1.0, 1.0, 1.0 }, false });
        else
            for (const auto& t : juce::StringArray::fromTokens(grid, " ", ""))
            {
                const auto v = juce::StringArray::fromTokens(t, ":", "");
                if (v.size() == 3)
                    trials.push_back({ 0.5f, { v[0].getDoubleValue(), v[1].getDoubleValue(), v[2].getDoubleValue() }, true });
                else
                    trials.push_back({ t.getFloatValue(), { 1.0, 1.0, 1.0 }, false });
            }
        juce::String head = "ms:";
        for (const double t : times)
            head << " " << juce::String(1000.0 * t, 0);
        logMessage(head);
        for (const auto& trial : trials)
        {
            const float turn = trial.turn;
            std::array<std::array<double, timeCount>, 4> seeds {};
            for (std::uint32_t seed = 1; seed <= 4; ++seed)
            {
                auto rig = std::make_unique<Rig>(rate, seed, std::initializer_list<std::pair<int, float>> { { G::cpLevel, 1.0f }, { G::accent, 0.0f }, { G::cpDecay, turn } });
                if (trial.byParts)
                {
                    std::array<float, 32> l {}, r {};
                    rig->generator->process(l.data(), r.data(), 32, rig->params);
                    rig->generator->clapForTesting().setReverbParts(Tr808Clap::c139 * trial.parts[0], Tr808Clap::c138 * trial.parts[1],
                                                                    1.0e6 * trial.parts[2]);
                }
                seeds[seed - 1] = envelopeDb(rig->render(39, 1.0f, 1.2, rate), rate);
            }
            juce::String line = trial.byParts ? "C139 x" + juce::String(trial.parts[0], 2) + " C138 x" + juce::String(trial.parts[1], 2)
                                                    + " R349 x" + juce::String(trial.parts[2], 2) + ":"
                                              : "CP Decay " + juce::String(turn, 3) + " (x" + juce::String(std::pow(4.0, 2.0 * (turn - 0.5)), 2) + "):";
            double late = 0.0, early = 0.0;
            int lateCount = 0;
            for (int t = 0; t < timeCount; ++t)
            {
                std::array<double, 4> v {};
                for (int s = 0; s < 4; ++s)
                    v[static_cast<std::size_t>(s)] = seeds[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)];
                std::sort(v.begin(), v.end());
                const double m = 0.5 * (v[1] + v[2]);
                line << " " << juce::String(m, 1);
                for (const double u : { fischer[t], audiorealism[t] })
                {
                    if (times[t] >= 0.15)
                    {
                        late += (m - u) * (m - u);
                        ++lateCount;
                    }
                    else
                        early = std::max(early, std::abs(m - u));
                }
            }
            line << " | from 150 ms rms " << juce::String(std::sqrt(late / lateCount), 1) << " dB, before it worst "
                 << juce::String(early, 1) << " dB";
            logMessage(line);
        }
    }
};

constexpr double V3Tr808ClapTailFit::times[];
static V3Tr808ClapTailFit v3Tr808ClapTailFit;
