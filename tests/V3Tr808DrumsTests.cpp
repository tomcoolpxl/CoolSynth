#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <memory>
#include <vector>

#include "synth/v3/Part.h"
#include "synth/v3/Tr808RimShot.h"
#include "synth/v3/Tr808Tom.h"

/*
    The 808 Drums' circuits (D252) against ngspice renders of the same schematics (tools/tr808/tom.cir and
    rimclave.cir, rendered by tools/tr808/render.py; the references are in tests/data/tr808): each stage's node,
    sample by sample, over the first 30 ms of a hit.
*/
namespace
{
    using coolsynth::synth::v3::Tr808RimShot;
    using coolsynth::synth::v3::Tr808Tom;

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

    /** The rms difference over the rms of the reference, from sample `from` on. */
    double relativeError(const std::vector<double>& model, const std::vector<double>& spice, std::size_t from)
    {
        double e = 0.0, s = 0.0;
        const std::size_t n = std::min(model.size(), spice.size());
        for (std::size_t i = from; i < n; ++i)
        {
            e += (model[i] - spice[i]) * (model[i] - spice[i]);
            s += spice[i] * spice[i];
        }
        return std::sqrt(e / std::max(1.0e-30, s));
    }

    struct TomCase
    {
        const char* file;
        Tr808Tom::Pair pair;
        bool conga;
        double volts, noise;
    };

    struct RimCase
    {
        const char* file;
        bool claves;
        double volts;
    };
}

class V3Tr808DrumsTests final : public juce::UnitTest
{
public:
    V3Tr808DrumsTests() : juce::UnitTest("V3 TR-808 drums circuits", "V3") {}

    void runTest() override
    {
        beginTest("the_toms_and_congas_match_spice_stage_by_stage");
        {
            const TomCase cases[] = {
                { "lt.csv", Tr808Tom::Pair::low, false, 4.0, 0.02 },  { "lc.csv", Tr808Tom::Pair::low, true, 4.0, 0.0 },
                { "mt.csv", Tr808Tom::Pair::mid, false, 4.0, 0.02 },  { "mc.csv", Tr808Tom::Pair::mid, true, 4.0, 0.0 },
                { "ht.csv", Tr808Tom::Pair::high, false, 4.0, 0.02 }, { "hc.csv", Tr808Tom::Pair::high, true, 4.0, 0.0 },
                { "lt14.csv", Tr808Tom::Pair::low, false, 14.0, 0.02 },
            };
            for (const auto& c : cases)
            {
                const auto file = referenceFolder().getChildFile(c.file);
                if (! file.existsAsFile())
                {
                    expect(false, juce::String("the SPICE reference is missing: ") + c.file);
                    continue;
                }
                const auto ref = loadReference(file);
                auto tom = std::make_unique<Tr808Tom>(c.pair);
                tom->prepare(ref.rate);
                tom->setConga(c.conga);
                tom->setTuning(0.5);
                tom->setLevel(0.8);
                tom->settle();
                tom->trigger(c.volts);
                const std::size_t n = ref.size();
                std::map<juce::String, std::vector<double>> model;
                int worstNewton = 0;
                bool converged = true;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const double t = static_cast<double>(i) / ref.rate;
                    const double out = tom->process(c.noise * std::sin(2.0 * juce::MathConstants<double>::pi * 150.0 * t));
                    model["vout"].push_back(out);
                    model["vt"].push_back(tom->triggerNodeVolts());
                    model["np"].push_back(tom->pulseVolts());
                    model["nev"].push_back(tom->envelopeVolts());
                    model["nq"].push_back(tom->gateBaseVolts());
                    model["nfl"].push_back(tom->noiseVolts());
                    model["nm"].push_back(tom->middleVolts());
                    model["no2"].push_back(tom->resonatorVolts());
                    worstNewton = std::max(worstNewton, tom->newtonSteps());
                    converged = converged && tom->lastSolveConverged();
                }
                juce::String line = juce::String(c.file) + ":";
                for (const auto* name : { "vt", "np", "nev", "nq", "nfl", "nm", "no2", "vout" })
                {
                    const double err = relativeError(model[name], ref.columns.at(name), 0);
                    line << " " << name << " " << juce::String(err, 4);
                    // The noise only reaches the mixer for the toms; the congas' gate output is all but nothing.
                    if (juce::String(name) == "nfl" && c.conga)
                        continue;
                    expectLessThan(err, 0.05, juce::String(c.file) + " " + name);
                }
                line << " | most Newton steps " << worstNewton;
                logMessage(line);
                expect(converged, juce::String(c.file) + ": every sample converged");
            }
        }

        beginTest("the_rim_shot_and_claves_match_spice_stage_by_stage");
        {
            const RimCase cases[] = { { "rs.csv", false, 4.0 }, { "cl.csv", true, 4.0 }, { "rs14.csv", false, 14.0 } };
            for (const auto& c : cases)
            {
                const auto file = referenceFolder().getChildFile(c.file);
                if (! file.existsAsFile())
                {
                    expect(false, juce::String("the SPICE reference is missing: ") + c.file);
                    continue;
                }
                const auto ref = loadReference(file);
                auto drum = std::make_unique<Tr808RimShot>();
                drum->prepare(ref.rate);
                drum->setClaves(c.claves);
                drum->setLevel(0.8);
                drum->settle();
                drum->trigger(c.volts);
                const std::size_t n = ref.size();
                std::map<juce::String, std::vector<double>> model;
                int worstNewton = 0;
                bool converged = true;
                for (std::size_t i = 0; i < n; ++i)
                {
                    model["vout"].push_back(drum->process());
                    model["vt"].push_back(drum->triggerNodeVolts());
                    model["nenv"].push_back(drum->envelopeVolts());
                    model["no21"].push_back(drum->lowVolts());
                    model["no20b"].push_back(drum->highVolts());
                    model["ns"].push_back(drum->swingVolts());
                    model["nf"].push_back(drum->collectorVolts());
                    model["ne63"].push_back(drum->followerVolts());
                    worstNewton = std::max(worstNewton, drum->newtonSteps());
                    converged = converged && drum->lastSolveConverged();
                }
                juce::String line = juce::String(c.file) + ":";
                for (const auto* name : { "vt", "nenv", "no21", "no20b", "ns", "nf", "ne63", "vout" })
                {
                    const juce::String node(name);
                    // The swing VCA runs for the claves only while their envelope drains; its nodes do not reach them.
                    // The claves' bridged-T rings near its own oscillation, where the bilinear transform's warping
                    // puts it some 5 Hz under the reference's 2498 Hz (3 cents): the phase drifts through a 30 ms
                    // window, so they are held to their pitch and their peak below, not sample by sample.
                    const bool unused = c.claves && (node == "nf" || node == "ne63" || node == "no20b" || node == "ns" || node == "vout");
                    // The accented rim shot's swing VCA switches inside a sample in its first 3 ms: its peak there
                    // runs 8 % over the reference's, which resolves the edge in sub-microsecond steps; after them its
                    // output is within 7 %.
                    const double limit = c.volts > 10.0 && (node == "nf" || node == "ne63" || node == "vout") ? 0.10 : 0.05;
                    // Q63's emitter carries its bias (some -0.6 V): its signal is what is compared.
                    std::vector<double> m = model[name], s = ref.columns.at(name);
                    if (juce::String(name) == "ne63" || juce::String(name) == "nf")
                    {
                        const double m0 = m.front(), s0 = s.front();
                        for (auto& v : m)
                            v -= m0;
                        for (auto& v : s)
                            v -= s0;
                    }
                    const double err = relativeError(m, s, 0);
                    line << " " << name << " " << juce::String(err, 4);
                    if (! unused)
                        expectLessThan(err, limit, juce::String(c.file) + " " + name);
                }
                if (c.claves)
                {
                    const auto hz = [&ref](const std::vector<double>& v)
                    {
                        // Upward zero crossings from 5 ms to the end, interpolated: the ring's frequency.
                        double first = -1.0, last = -1.0;
                        int count = 0;
                        for (std::size_t i = static_cast<std::size_t>(0.005 * ref.rate) + 1; i < v.size(); ++i)
                            if (v[i - 1] < 0.0 && v[i] >= 0.0)
                            {
                                last = static_cast<double>(i - 1) + v[i - 1] / (v[i - 1] - v[i]);
                                if (first < 0.0)
                                    first = last;
                                ++count;
                            }
                        return count > 1 ? ref.rate * (count - 1) / (last - first) : 0.0;
                    };
                    const auto peak = [](const std::vector<double>& v)
                    {
                        double p = 0.0;
                        for (const double x : v)
                            p = std::max(p, std::abs(x));
                        return p;
                    };
                    const auto& spice = ref.columns.at("vout");
                    const double modelHz = hz(model["vout"]), spiceHz = hz(spice);
                    line << " | ring " << juce::String(modelHz, 1) << " Hz against " << juce::String(spiceHz, 1) << ", peak "
                         << juce::String(peak(model["vout"]), 3) << " V against " << juce::String(peak(spice), 3);
                    expectLessThan(std::abs(1200.0 * std::log2(modelHz / spiceHz)), 6.0, "the claves' pitch, cents");
                    expectLessThan(std::abs(peak(model["vout"]) / peak(spice) - 1.0), 0.03, "the claves' peak");
                }
                line << " | most Newton steps " << worstNewton;
                logMessage(line);
                expect(converged, juce::String(c.file) + ": every sample converged");
            }
        }

        if (const auto dump = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_DRUMS_DUMP", {}); dump.isNotEmpty())
        {
            // Long renders of every instrument, for the decay and pitch comparison with SPICE and the units (D252):
            // vout only, one CSV each, into that folder.
            beginTest("dump");
            const double rate = 96000.0;
            const juce::File folder(dump);
            folder.createDirectory();
            struct Job { const char* name; int pair; bool conga; double volts, tune; };
            const Job jobs[] = { { "lt", 0, false, 4, 0.5 }, { "lc", 0, true, 4, 0.5 }, { "mt", 1, false, 4, 0.5 }, { "mc", 1, true, 4, 0.5 },
                                 { "ht", 2, false, 4, 0.5 }, { "hc", 2, true, 4, 0.5 }, { "lt14", 0, false, 14, 0.5 }, { "lc14", 0, true, 14, 0.5 },
                                 { "mt14", 1, false, 14, 0.5 }, { "mc14", 1, true, 14, 0.5 }, { "ht14", 2, false, 14, 0.5 }, { "hc14", 2, true, 14, 0.5 },
                                 { "lt_t0", 0, false, 4, 0.0 }, { "lt_t1", 0, false, 4, 1.0 }, { "lc_t0", 0, true, 4, 0.0 }, { "lc_t1", 0, true, 4, 1.0 },
                                 { "mt_t0", 1, false, 4, 0.0 }, { "mt_t1", 1, false, 4, 1.0 }, { "mc_t0", 1, true, 4, 0.0 }, { "mc_t1", 1, true, 4, 1.0 },
                                 { "ht_t0", 2, false, 4, 0.0 }, { "ht_t1", 2, false, 4, 1.0 }, { "hc_t0", 2, true, 4, 0.0 }, { "hc_t1", 2, true, 4, 1.0 } };
            for (const auto& j : jobs)
            {
                auto tom = std::make_unique<Tr808Tom>(static_cast<Tr808Tom::Pair>(j.pair));
                tom->prepare(rate);
                tom->setConga(j.conga);
                tom->setTuning(j.tune);
                tom->setLevel(0.8);
                tom->settle();
                tom->trigger(j.volts);
                juce::String text = "time,vout\n";
                for (int i = 0; i < static_cast<int>(0.5 * rate); ++i)
                    text << juce::String(i / rate, 9) << "," << juce::String(tom->process(0.0), 7) << "\n";
                folder.getChildFile(juce::String(j.name) + ".csv").replaceWithText(text);
            }
            for (const auto& [name, claves, volts] : { std::tuple { "rs", false, 4.0 }, std::tuple { "cl", true, 4.0 },
                                                      std::tuple { "rs14", false, 14.0 }, std::tuple { "cl14", true, 14.0 } })
            {
                auto drum = std::make_unique<Tr808RimShot>();
                drum->prepare(rate);
                drum->setClaves(claves);
                drum->setLevel(0.8);
                drum->settle();
                drum->trigger(volts);
                juce::String text = "time,vout\n";
                for (int i = 0; i < static_cast<int>(0.2 * rate); ++i)
                    text << juce::String(i / rate, 9) << "," << juce::String(drum->process(), 7) << "\n";
                folder.getChildFile(juce::String(name) + ".csv").replaceWithText(text);
            }
        }
    }
};

static V3Tr808DrumsTests v3Tr808DrumsTests;

namespace
{
    using namespace coolsynth::synth::v3;

    GeneratorSettings drumsOf(std::initializer_list<std::pair<int, float>> units = {})
    {
        GeneratorSettings g;
        g.type = generatorType::drums808;
        const auto& d = GeneratorSlot::typeTable().descriptor(g.type);
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, value] : units)
            g.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], value);
        g.level = 0.5f;
        return g;
    }

    PartSettings kitOf(PlayMode mode = PlayMode::kit)
    {
        PartSettings s;
        s.playMode = mode;
        s.voice.generators[0] = drumsOf();
        Envelope::Parameters amp;
        amp.attack = 0.0005f;
        amp.decay = 0.2f;
        amp.sustain = 1.0f;
        amp.release = 0.01f;
        s.voice.envelopes = { amp, amp, amp };
        return s;
    }

    struct Hit
    {
        int sample, note;
        bool off = false;
    };

    /** A part playing these hits (note-ons, or note-offs), host-rate mono out; the part's voices active at the end. */
    std::vector<float> play(const PartSettings& settings, const std::vector<Hit>& hits, double seconds, int* activeAtEnd = nullptr)
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

    float peakOf(const std::vector<float>& x, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1))
    {
        float p = 0.0f;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
            p = std::max(p, std::abs(x[i]));
        return p;
    }

    /** Upward zero crossings per second between two times: a ringing drum's frequency. */
    double crossingHz(const std::vector<float>& x, double rate, double from, double to)
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
    The 808 Drums in the Kit play mode (D252): one voice holds the kit, each note strikes its instrument, note-offs
    change nothing, the voice ends once everything has rung out; the generator's keys and its pairs' switches.
*/
class V3Tr808KitTests final : public juce::UnitTest
{
public:
    V3Tr808KitTests() : juce::UnitTest("V3 TR-808 drums kit", "V3") {}

    void runTest() override
    {
        beginTest("the_808_drums_is_generator_type_thirteen_with_twelve_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 14);
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::drums808).name), juce::String("808 Drums"));
            expectEquals(Tr808DrumsGenerator::descriptor().usedParams, 12);
            bool other = false;
            const std::pair<int, int> keys[] = { { 35, 0 }, { 36, 0 }, { 37, 4 }, { 75, 4 }, { 41, 1 }, { 43, 1 }, { 64, 1 },
                                                 { 45, 2 }, { 47, 2 }, { 63, 2 }, { 48, 3 }, { 50, 3 }, { 62, 3 }, { 60, -1 } };
            for (const auto& [note, instrument] : keys)
                expectEquals(Tr808DrumsGenerator::instrumentFor(note, other), instrument, "key " + juce::String(note));
        }

        beginTest("two_hits_in_one_block_both_sound");
        {
            // The bass drum and the low tom struck in the same block, the second while the voice's note still waits
            // to start: the kit plays both, the sum of each alone (their circuits are apart).
            const auto both = play(kitOf(), { { 0, 36 }, { 0, 43 } }, 0.5);
            const auto bass = play(kitOf(), { { 0, 36 } }, 0.5);
            const auto tom = play(kitOf(), { { 0, 43 } }, 0.5);
            float worst = 0.0f;
            for (std::size_t i = 0; i < both.size(); ++i)
                worst = std::max(worst, std::abs(both[i] - bass[i] - tom[i]));
            logMessage("both against the sum: worst " + juce::String(worst, 7) + " of a peak of " + juce::String(peakOf(both), 4));
            expectGreaterThan(peakOf(bass), 0.01f);
            expectGreaterThan(peakOf(tom), 0.01f);
            expectLessThan(worst, 1.0e-4f * peakOf(both));
            // And a later hit in another block while the first rings: retriggered, not restarted.
            const auto later = play(kitOf(), { { 0, 36 }, { 4800, 43 } }, 0.5);
            const auto tomLater = play(kitOf(), { { 4800, 43 } }, 0.5);
            worst = 0.0f;
            for (std::size_t i = 0; i < later.size(); ++i)
                worst = std::max(worst, std::abs(later[i] - bass[i] - tomLater[i]));
            expectLessThan(worst, 1.0e-4f * peakOf(later), "a hit while another rings adds to it");
        }

        beginTest("note_offs_change_nothing");
        {
            const auto held = play(kitOf(), { { 0, 43 } }, 0.6);
            const auto tapped = play(kitOf(), { { 0, 43 }, { 100, 43, true } }, 0.6);
            expect(held == tapped, "a note-off leaves the drum ringing as it was");
        }

        beginTest("the_voice_ends_once_every_instrument_has_rung_out");
        {
            int activeEarly = 0, activeLate = 0, activeUnmapped = 0;
            (void) play(kitOf(), { { 0, 36 }, { 0, 37 } }, 0.1, &activeEarly);
            (void) play(kitOf(), { { 0, 36 }, { 0, 37 } }, 4.0, &activeLate);
            (void) play(kitOf(), { { 0, 60 } }, 0.5, &activeUnmapped);
            expectEquals(activeEarly, 1, "one voice holds the kit while it rings");
            expectEquals(activeLate, 0, "and lets go once it has rung down");
            expectEquals(activeUnmapped, 0, "a key with no instrument ends at once");
        }

        beginTest("a_conga_takes_over_its_toms_circuit");
        {
            // The low tom near 90 Hz; the low conga's key switches the same circuit to near 200 Hz (D252, page 14).
            // Accent 0: the 4 V trigger of the SPICE references, whose late pitch these are.
            auto plain = kitOf();
            plain.voice.generators[0] = drumsOf({ { Tr808DrumsGenerator::accent, 0.0f } });
            const auto tom = play(plain, { { 0, 43 } }, 0.3);
            const auto conga = play(plain, { { 0, 43 }, { 4800, 64 } }, 0.4);
            const double tomHz = crossingHz(tom, 48000.0, 0.03, 0.09);
            const double congaHz = crossingHz(conga, 48000.0, 0.13, 0.19);
            logMessage("low tom " + juce::String(tomHz, 1) + " Hz, then its conga " + juce::String(congaHz, 1) + " Hz");
            expectWithinAbsoluteError(tomHz, 90.3, 3.0);
            expectWithinAbsoluteError(congaHz, 200.0, 6.0);
            // The claves' key switches the rim shot's circuit: its ring near 2.5 kHz.
            const auto claves = play(plain, { { 0, 75 } }, 0.1);
            const double clavesHz = crossingHz(claves, 48000.0, 0.005, 0.02);
            logMessage("claves " + juce::String(clavesHz, 1) + " Hz");
            expectWithinAbsoluteError(clavesHz, 2489.0, 30.0);
        }

        beginTest("after_reset_a_hit_is_the_fresh_kits_bit_for_bit");
        {
            // The determinism tests' rule (D224) at the generator: a kit reset and reseeded plays as a fresh one.
            SlotMemory memory;
            memory.allocate(Tr808DrumsGenerator::memoryFloats(96000.0, 32));
            memory.setSeed(5);
            auto fresh = std::make_unique<Tr808DrumsGenerator>();
            fresh->prepare(96000.0, 32, memory);
            SlotMemory memoryUsed;
            memoryUsed.allocate(Tr808DrumsGenerator::memoryFloats(96000.0, 32));
            memoryUsed.setSeed(5);
            auto used = std::make_unique<Tr808DrumsGenerator>();
            used->prepare(96000.0, 32, memoryUsed);
            SlotParams params;
            const auto& d = Tr808DrumsGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                params.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
            std::array<float, 32> l {}, r {};
            NoteContext note;
            note.noteNumber = 41;
            used->noteOn(note);
            for (int b = 0; b < 300; ++b)
                used->process(l.data(), r.data(), 32, params);
            used->reset();
            used->reseed();
            for (auto* g : { fresh.get(), used.get() })
            {
                note.noteNumber = 47;
                g->noteOn(note);
                g->noteChanged(36);
                g->strike(0.7f);
            }
            bool same = true;
            std::array<float, 32> l2 {}, r2 {};
            for (int b = 0; b < 3000 && same; ++b)
            {
                fresh->process(l.data(), r.data(), 32, params);
                used->process(l2.data(), r2.data(), 32, params);
                same = l == l2;
            }
            expect(same, "a reset kit plays its next hits as a fresh one, bit for bit");
        }
    }
};

static V3Tr808KitTests v3Tr808KitTests;
