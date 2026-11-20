#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/Tr909BassDrum.h"

/*
    The TR-909 bass drum circuit model (D237, D238) against ngspice renders of the same schematic
    (tools/kick909/bassdrum.cir, rendered by tools/kick909/render.py; the references are in tests/data/kick909) and,
    env-gated, against Rob Roy's 1995 recordings of a real 909 (analysis only, never shipped).
*/
namespace
{
    using coolsynth::synth::v3::Tr909BassDrum;

    struct Knobs
    {
        double dac = 2.5, tune = 0.5, decay = 0.5, attack = 0.5, level = 0.5;
    };

    /** The knobs as render.py turns them into parts. */
    void setKnobs(Tr909BassDrum& drum, const Knobs& k)
    {
        drum.setTuneOhms(100.0e3 * Tr909BassDrum::audioTaper(k.tune));
        drum.setDecayOhms(1.0e6 * Tr909BassDrum::audioTaper(k.decay));
        drum.setAttackOhms(500.0 * (1.0 - k.attack));
        drum.setLevelOhms(100.0e3 * k.level);
        if (const auto leak = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_C11LEAK", {}); leak.isNotEmpty())
            drum.setC11LeakOhms(leak.getDoubleValue());
        if (const auto width = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_TRIGGER", {}); width.isNotEmpty())
            drum.setTriggerSeconds(width.getDoubleValue());
    }

    struct Rendered
    {
        std::vector<double> out, tri, c8, cv, n4, c12, q9e;
    };

    /** The drum at 96 kHz, the trigger at 5 ms as render.py puts it. */
    Rendered render(const Knobs& k, double seconds)
    {
        auto drum = std::make_unique<Tr909BassDrum>();
        drum->prepare(96000.0);
        setKnobs(*drum, k);
        Rendered r;
        const int samples = static_cast<int>(seconds * 96000.0);
        for (int i = 0; i < samples; ++i)
        {
            if (i == static_cast<int>(0.005 * 96000.0))
                drum->trigger(k.dac);
            r.out.push_back(drum->process());
            const auto& s = drum->stage();
            r.tri.push_back(drum->triangleVolts());
            r.c8.push_back(s.c8);
            r.cv.push_back(s.cv);
            r.n4.push_back(s.n4);
            r.c12.push_back(s.c12);
            r.q9e.push_back(s.q9e);
        }
        return r;
    }

    /** The half periods' frequencies from the zero crossings, as (time, Hz). */
    std::vector<std::pair<double, double>> halfPeriods(const std::vector<double>& v, double rate)
    {
        std::vector<double> edges;
        for (std::size_t i = 1; i < v.size(); ++i)
            if ((v[i - 1] < 0.0 && v[i] >= 0.0) || (v[i - 1] > 0.0 && v[i] <= 0.0))
                edges.push_back((static_cast<double>(i - 1) + v[i - 1] / (v[i - 1] - v[i])) / rate);
        std::vector<std::pair<double, double>> out;
        for (std::size_t i = 1; i < edges.size(); ++i)
            out.emplace_back(0.5 * (edges[i] + edges[i - 1]), 0.5 / (edges[i] - edges[i - 1]));
        return out;
    }

    /** The settled pitch of a long hit: its half periods after 0.6 s. */
    double settledOf(const Rendered& r)
    {
        double sum = 0.0;
        int count = 0;
        for (const auto& [t, hz] : halfPeriods(r.tri, 96000.0))
            if (t > 0.6)
            {
                sum += hz;
                ++count;
            }
        return sum / std::max(1, count);
    }

    /** What the comparison with recordings reads from a hit (the recordings are normalised: shapes, not levels). */
    struct HitMeasures
    {
        double settledHz = 0.0, semis10 = 0.0, semis20 = 0.0, semis40 = 0.0, down10 = 0.0, down20 = 0.0, down40 = 0.0, click = 0.0;
    };

    /** settled: the drum's settled pitch, measured on its longest hits (0 to measure it here). */
    HitMeasures measureHit(const std::vector<double>& x, double rate, double settled = 0.0)
    {
        HitMeasures m;
        double peak = 0.0;
        for (const double v : x)
            peak = std::max(peak, std::abs(v));
        if (peak <= 0.0)
            return m;
        std::size_t start = 0;
        while (start < x.size() && std::abs(x[start]) < 0.05 * peak)
            ++start;
        std::vector<double> edges;
        for (std::size_t i = std::max<std::size_t>(start, 1); i < x.size(); ++i)
            if ((x[i - 1] < 0.0 && x[i] >= 0.0) || (x[i - 1] > 0.0 && x[i] <= 0.0))
                edges.push_back((static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]) - static_cast<double>(start)) / rate);
        // The settled pitch: the half periods between 200 and 400 ms, or the last 200 ms there are.
        const double to = std::min(0.4, static_cast<double>(x.size() - start) / rate - 0.02), from = std::max(0.15, to - 0.2);
        std::vector<double> inside;
        for (const double e : edges)
            if (e >= from && e <= to)
                inside.push_back(e);
        if (settled > 0.0)
            m.settledHz = settled;
        else if (inside.size() > 4)
            m.settledHz = 0.5 * static_cast<double>(inside.size() - 1) / (inside.back() - inside.front());
        const auto hzAt = [&](double t)
        {
            for (std::size_t i = 1; i + 1 < edges.size(); ++i)
                if (edges[i] >= t)
                    return 1.0 / (edges[i + 1] - edges[i - 1]);
            return 0.0;
        };
        if (m.settledHz > 0.0)
        {
            m.semis10 = 12.0 * std::log2(std::max(1.0e-9, hzAt(0.01)) / m.settledHz);
            m.semis20 = 12.0 * std::log2(std::max(1.0e-9, hzAt(0.02)) / m.settledHz);
            m.semis40 = 12.0 * std::log2(std::max(1.0e-9, hzAt(0.04)) / m.settledHz);
        }
        // The level: peaks over 5 ms; the times from the hit to 10, 20 and 40 dB under the peak (-1: not within).
        const auto window = static_cast<std::size_t>(0.005 * rate);
        std::vector<double> peaks;
        for (std::size_t i = start; i + window <= x.size(); i += window)
        {
            double p = 0.0;
            for (std::size_t j = i; j < i + window; ++j)
                p = std::max(p, std::abs(x[j]));
            peaks.push_back(p);
        }
        const auto downBy = [&](double db)
        {
            const double level = peak * std::pow(10.0, -db / 20.0);
            std::size_t last = 0;
            for (std::size_t w = 0; w < peaks.size(); ++w)
                if (peaks[w] > level)
                    last = w + 1;
            return last >= peaks.size() ? -1.0 : 0.005 * static_cast<double>(last);
        };
        m.down10 = downBy(10.0);
        m.down20 = downBy(20.0);
        m.down40 = downBy(40.0);
        // The click: the first 2 ms's peak against the body's between 10 and 20 ms.
        double first = 0.0, body = 0.0;
        for (std::size_t i = start; i < std::min(x.size(), start + static_cast<std::size_t>(0.002 * rate)); ++i)
            first = std::max(first, std::abs(x[i]));
        for (std::size_t i = start + static_cast<std::size_t>(0.01 * rate); i < std::min(x.size(), start + static_cast<std::size_t>(0.02 * rate)); ++i)
            body = std::max(body, std::abs(x[i]));
        m.click = body > 0.0 ? first / body : 0.0;
        return m;
    }
}

class V3Tr909Tests final : public juce::UnitTest
{
public:
    V3Tr909Tests() : juce::UnitTest("V3 TR-909 circuit", "V3") {}

    void runTest() override
    {
        beginTest("the_circuit_matches_spice_stage_by_stage");
        {
            // tests/data/kick909: ngspice renders of the schematic as drawn (no C11 leak), TI's RC4558 for the op-amps,
            // from the trigger for 200 ms at every 4th sample of 96 kHz (tools/kick909/reduce.py).
            struct Case
            {
                const char* file;
                Knobs knobs;
            };
            const Case cases[] = { { "dac25_tune10_decay05.csv", { 2.5, 1.0, 0.5, 0.5, 1.0 } },
                                   { "dac40_tune0_decay10.csv", { 4.0, 0.0, 1.0, 1.0, 1.0 } } };
            const auto folder = juce::File(__FILE__).getParentDirectory().getChildFile("data").getChildFile("kick909");
            for (const auto& c : cases)
            {
                juce::StringArray lines;
                folder.getChildFile(c.file).readLines(lines);
                std::vector<std::vector<double>> spice(9);
                for (int i = 1; i < lines.size(); ++i)
                {
                    const auto f = juce::StringArray::fromTokens(lines[i], ",", "");
                    if (f.size() < 9)
                        continue;
                    for (int k = 0; k < 9; ++k)
                        spice[static_cast<std::size_t>(k)].push_back(f[k].getDoubleValue());
                }
                expect(spice[0].size() > 4000, juce::String("reference ") + c.file);
                if (spice[0].size() < 4000)
                    continue;

                auto drum = std::make_unique<Tr909BassDrum>();
                drum->prepare(96000.0);
                setKnobs(*drum, c.knobs);
                drum->setC11LeakOhms(1.0e12); // the schematic as drawn
                std::vector<std::vector<double>> model(9);
                const int start = static_cast<int>(0.005 * 96000.0);
                for (int i = 0; i < start + static_cast<int>(0.2 * 96000.0); ++i)
                {
                    if (i == start)
                        drum->trigger(c.knobs.dac);
                    const double out = drum->process();
                    if (i >= start && (i - start) % 4 == 0)
                    {
                        const auto& s = drum->stage();
                        const double row[] = { (i - start) / 96000.0, s.n4, s.c9, s.cv, drum->triangleVolts(), s.c8, s.c12, s.q9e, out };
                        for (std::size_t k = 0; k < 9; ++k)
                            model[k].push_back(row[k]);
                    }
                }
                const std::size_t n = std::min(spice[0].size(), model[0].size());
                const auto relative = [&](std::size_t k)
                {
                    double err = 0.0, ref = 0.0;
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        err += (spice[k][i] - model[k][i]) * (spice[k][i] - model[k][i]);
                        ref += spice[k][i] * spice[k][i];
                    }
                    return std::sqrt(err / std::max(1.0e-30, ref));
                };
                // The pulse: its height and how long it stays over 7.5 V.
                const auto width = [&](const std::vector<double>& v)
                {
                    int count = 0;
                    for (std::size_t i = 0; i < n; ++i)
                        count += v[i] > 7.5 ? 1 : 0;
                    return 4.0 * count / 96.0; // ms
                };
                const double pulseHeight = *std::max_element(model[1].begin(), model[1].begin() + static_cast<long>(n))
                                           / *std::max_element(spice[1].begin(), spice[1].begin() + static_cast<long>(n));
                // The sweep: semitones above the pitch at 170 ms, at 20, 50 and 100 ms (each its own settled pitch).
                const auto semitones = [&](const std::vector<double>& tri, std::vector<double>& at)
                {
                    std::vector<double> edges;
                    for (std::size_t i = 1; i < n; ++i)
                        if ((tri[i - 1] < 0.0 && tri[i] >= 0.0) || (tri[i - 1] > 0.0 && tri[i] <= 0.0))
                            edges.push_back((static_cast<double>(i - 1) + tri[i - 1] / (tri[i - 1] - tri[i])) * 4.0 / 96000.0);
                    // Each half period's pitch at its middle, read between them at a time.
                    const auto hz = [&](double t)
                    {
                        for (std::size_t i = 2; i < edges.size(); ++i)
                        {
                            const double mid1 = 0.5 * (edges[i - 2] + edges[i - 1]), mid2 = 0.5 * (edges[i - 1] + edges[i]);
                            if (mid2 >= t && mid1 <= t)
                            {
                                const double f1 = 0.5 / (edges[i - 1] - edges[i - 2]), f2 = 0.5 / (edges[i] - edges[i - 1]);
                                return f1 + (f2 - f1) * (t - mid1) / (mid2 - mid1);
                            }
                        }
                        return 1.0;
                    };
                    const double settled = hz(0.17);
                    for (const double t : { 0.02, 0.05, 0.1 })
                        at.push_back(12.0 * std::log2(hz(t) / settled));
                };
                std::vector<double> spiceSweep, modelSweep;
                semitones(spice[4], spiceSweep);
                semitones(model[4], modelSweep);
                // The output's envelope: peaks over 25 ms (a period and more, so the pitch's small drift does not move it), in dB.
                double worstDb = 0.0;
                for (std::size_t w = 0; w + 600 <= std::min<std::size_t>(n, 4800); w += 600)
                {
                    double a = 0.0, b = 0.0;
                    for (std::size_t i = w; i < w + 600; ++i)
                    {
                        a = std::max(a, std::abs(spice[8][i]));
                        b = std::max(b, std::abs(model[8][i]));
                    }
                    if (a > 0.05)
                        worstDb = std::max(worstDb, std::abs(20.0 * std::log10(b / a)));
                }
                logMessage(juce::String(c.file) + ": pulse " + juce::String(width(spice[1]), 2) + "/" + juce::String(width(model[1]), 2)
                           + " ms, height x" + juce::String(pulseHeight, 4) + "; rms error C9 " + juce::String(100.0 * relative(2), 2)
                           + " %, CV " + juce::String(100.0 * relative(3), 2) + " %, C8 " + juce::String(100.0 * relative(5), 2) + " %, C12 "
                           + juce::String(100.0 * relative(6), 2) + " %, Q9's emitter " + juce::String(100.0 * relative(7), 2)
                           + " %; sweep at 20, 50, 100 ms " + juce::String(spiceSweep[0], 2) + "/" + juce::String(modelSweep[0], 2) + " "
                           + juce::String(spiceSweep[1], 2) + "/" + juce::String(modelSweep[1], 2) + " " + juce::String(spiceSweep[2], 2) + "/"
                           + juce::String(modelSweep[2], 2) + " semitones; output envelope within " + juce::String(worstDb, 2) + " dB");
                expectWithinAbsoluteError(width(model[1]), width(spice[1]), 0.1, "the pulse's width");
                expectWithinAbsoluteError(pulseHeight, 1.0, 0.005, "the pulse's height");
                expectLessThan(relative(2), 0.02, "C9");
                expectLessThan(relative(3), 0.01, "the control voltage");
                expectLessThan(relative(5), 0.01, "C8");
                expectLessThan(relative(6), 0.02, "C12");
                expectLessThan(relative(7), 0.02, "Q9's emitter");
                for (std::size_t k = 0; k < 3; ++k)
                    expectWithinAbsoluteError(modelSweep[k], spiceSweep[k], 0.3, "the sweep");
                expectLessThan(worstDb, 1.5, "the output's envelope");
            }
        }

        beginTest("a_pnp_switch_mirrors_an_npn_switch");
        {
            // The same switch twice, mirrored: an NPN from ground pulled up by its load, driven at the base through
            // 47 k from an input; a PNP from +15 V pulled down by its load. Each conducts when its base is driven.
            using namespace coolsynth::synth::v3;
            auto npn = std::make_unique<wdf::Circuit<4, 6, 2, 0, 2, 2>>();
            auto pnp = std::make_unique<wdf::Circuit<4, 6, 2, 0, 2, 2>>();
            const int npnIn = npn->addInput(1), npnRail = npn->addInput(4);
            npn->addResistor(1, 2, 47.0e3);
            npn->addResistor(2, 0, 1.0e6);
            npn->addResistor(4, 3, 10.0e3);
            npn->addTransistor(3, 2, 0, 10.0e-15, 300.0, 5.0, 0.02585);
            const int npnOut = npn->addProbe(3);
            const int pnpIn = pnp->addInput(1), pnpRail = pnp->addInput(4);
            pnp->addResistor(1, 2, 47.0e3);
            pnp->addResistor(2, 4, 1.0e6);
            pnp->addResistor(3, 0, 10.0e3);
            pnp->addPnpTransistor(3, 2, 4, 10.0e-15, 300.0, 5.0, 0.02585);
            const int pnpOut = pnp->addProbe(3);
            npn->setSampleRate(96000.0);
            pnp->setSampleRate(96000.0);
            npn->setInput(npnRail, 15.0);
            pnp->setInput(pnpRail, 15.0);
            juce::String log;
            int worst = 0;
            for (int i = 0; i < 40; ++i)
            {
                const double drive = i < 10 ? 0.0 : (i < 25 ? 5.0 : 0.0);
                npn->setInput(npnIn, drive);
                pnp->setInput(pnpIn, 15.0 - drive);
                npn->process();
                pnp->process();
                worst = std::max({ worst, npn->lastNewtonSteps(), pnp->lastNewtonSteps() });
                if (i == 9 || i == 10 || i == 11 || i == 24 || i == 25 || i == 26)
                    log << " " << i << ": " << juce::String(npn->probe(npnOut), 3) << "/" << juce::String(15.0 - pnp->probe(pnpOut), 3)
                        << " (" << npn->lastNewtonSteps() << "/" << pnp->lastNewtonSteps() << ")";
                expectWithinAbsoluteError(15.0 - pnp->probe(pnpOut), npn->probe(npnOut), 1.0e-6, "mirrored at sample " + juce::String(i));
            }
            logMessage("NPN/PNP collector:" + log + "; most steps " + juce::String(worst));
        }

        if (const auto folder = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_ROBROY", {}); folder.isNotEmpty())
        {
            // Rob Roy's 1995 recordings of one 909 (analysis only, never shipped: docs/research/kick-909-circuit.md),
            // named bt<tune>a<attack>d<decay> at the knobs' marks 0, 3, 7, 10 ('a'), against the model at the same turns,
            // a normal hit (the accent DAC at 2 V). The recordings are normalised, so the shapes compare, not the levels.
            beginTest("against_rob_roys_recordings");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const juce::StringArray marks { "0", "3", "7", "a" };
            const double turns[] = { 0.0, 0.3, 0.7, 1.0 };
            Knobs longest;
            longest.decay = 1.0;
            const double modelSettled = settledOf(render(longest, 1.0));
            juce::String table = "\n  file: recorded/model  settled Hz | semitones at 10, 20, 40 ms | ms to -10, -20, -40 dB | click";
            for (int t = 0; t < 4; ++t)
                for (int a = 0; a < 2; ++a)
                    for (int d = 0; d < 4; ++d)
                    {
                        const juce::String file = "bt" + marks[t] + "a" + (a == 0 ? "0" : "a") + "d" + marks[d] + ".wav";
                        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(folder).getChildFile(file)));
                        if (reader == nullptr)
                            continue;
                        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
                        std::vector<double> recorded(static_cast<std::size_t>(buffer.getNumSamples()));
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                            recorded[static_cast<std::size_t>(i)] = buffer.getSample(0, i);
                        // The unit settles at 49.4 Hz (its longest hits; mw909 measured the same set at 49.2 to 49.6).
                        const auto real = measureHit(recorded, reader->sampleRate, 49.4);

                        Knobs k;
                        k.dac = 2.0;
                        k.tune = turns[t];
                        k.decay = turns[d];
                        k.attack = a == 0 ? 0.0 : 1.0;
                        k.level = 1.0;
                        const auto r = render(k, 1.2);
                        std::vector<double> modelled(r.out.begin() + static_cast<long>(0.005 * 96000.0), r.out.end());
                        const auto model = measureHit(modelled, 96000.0, modelSettled);
                        const auto pair = [](double x, double y, int digits) { return juce::String(x, digits) + "/" + juce::String(y, digits); };
                        table << "\n  " << file << ": " << pair(real.settledHz, model.settledHz, 1) << " | " << pair(real.semis10, model.semis10, 1)
                              << " " << pair(real.semis20, model.semis20, 1) << " " << pair(real.semis40, model.semis40, 1) << " | "
                              << pair(1.0e3 * real.down10, 1.0e3 * model.down10, 0) << " " << pair(1.0e3 * real.down20, 1.0e3 * model.down20, 0)
                              << " " << pair(1.0e3 * real.down40, 1.0e3 * model.down40, 0) << " | " << pair(real.click, model.click, 2);
                    }
            logMessage("recorded/model:" + table);
        }

        if (const auto folder = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_AUDIOREALISM", {}); folder.isNotEmpty())
        {
            // AudioRealism's TR-909 pack (analysis only, never shipped; its licence forbids redistribution), recorded at
            // 96 kHz from the drum's own output with the part volume at half: BassDrum909-tuneTTT-attackAAA-decayDDD.wav
            // at 0, 25, 50, 75 and 100 %, and eleven accent levels at full knobs. Against the model at the same turns, a
            // normal hit; the accent's span to set against the model's DAC from 2 to 4 V.
            beginTest("against_audiorealisms_recordings");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto load = [&](const juce::String& file, double& rate)
            {
                std::vector<double> x;
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(folder).getChildFile(file)));
                if (reader == nullptr)
                    return x;
                juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
                x.resize(static_cast<std::size_t>(buffer.getNumSamples()));
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    x[static_cast<std::size_t>(i)] = buffer.getSample(0, i);
                rate = reader->sampleRate;
                return x;
            };
            Knobs longest;
            longest.decay = 1.0;
            const double modelSettled = settledOf(render(longest, 1.0));
            // The unit settles at 49.3 Hz (TUNE 0) to 49.9 Hz (TUNE full), measured on its longest hits through a
            // 300 Hz low-pass; the raw zero crossings near the files' gated ends read too high.
            const double realSettled = 49.6;
            juce::String table = "\n  settled " + juce::String(realSettled, 1) + "/" + juce::String(modelSettled, 1)
                                 + " Hz\n  knobs (tune attack decay): recorded/model  semitones at 10, 20, 40 ms | ms to -10, -20, -40 dB | click";
            for (const int tune : { 0, 100 })
                for (const int attack : { 0, 100 })
                    for (const int decay : { 0, 25, 50, 75, 100 })
                    {
                        const auto pad = [](int v) { return juce::String(v).paddedLeft('0', 3); };
                        double rate = 44100.0;
                        const auto recorded = load("BassDrum909-tune" + pad(tune) + "-attack" + pad(attack) + "-decay" + pad(decay) + ".wav", rate);
                        if (recorded.empty())
                            continue;
                        const auto real = measureHit(recorded, rate, realSettled);
                        Knobs k;
                        k.dac = 2.0;
                        k.tune = 0.01 * tune;
                        k.attack = 0.01 * attack;
                        k.decay = 0.01 * decay;
                        k.level = 1.0;
                        const auto r = render(k, 1.2);
                        std::vector<double> modelled(r.out.begin() + static_cast<long>(0.005 * 96000.0), r.out.end());
                        const auto model = measureHit(modelled, 96000.0, modelSettled);
                        const auto pair = [](double x, double y, int digits) { return juce::String(x, digits) + "/" + juce::String(y, digits); };
                        table << "\n  " << tune << " " << attack << " " << decay << ": " << pair(real.semis10, model.semis10, 1) << " "
                              << pair(real.semis20, model.semis20, 1) << " " << pair(real.semis40, model.semis40, 1) << " | "
                              << pair(1.0e3 * real.down10, 1.0e3 * model.down10, 0) << " " << pair(1.0e3 * real.down20, 1.0e3 * model.down20, 0)
                              << " " << pair(1.0e3 * real.down40, 1.0e3 * model.down40, 0) << " | " << pair(real.click, model.click, 2);
                    }
            logMessage("recorded/model:" + table);
            // The hit's first 6 ms, every 0.25 ms against the body's peak (10 to 20 ms), from the first sound.
            for (const int attack : { 0, 100 })
            {
                juce::String shapes;
                double rate = 44100.0;
                const auto pad = [](int v) { return juce::String(v).paddedLeft('0', 3); };
                const auto recorded = load("BassDrum909-tune050-attack" + pad(attack) + "-decay050.wav", rate);
                Knobs k { 2.0, 0.5, 0.5, 0.01 * attack, 1.0 };
                const auto r = render(k, 0.05);
                std::vector<double> modelled(r.out.begin() + static_cast<long>(0.005 * 96000.0), r.out.end());
                const double rest = modelled.front();
                for (auto& v : modelled)
                    v -= rest; // Q12's base current at rest, which the Kick's DC block removes
                const auto describe = [&shapes](const std::vector<double>& x, double xRate, const char* label)
                {
                    if (x.empty())
                        return;
                    std::size_t first = 0;
                    while (first < x.size() && std::abs(x[first]) < 3.0e-4)
                        ++first;
                    double body = 0.0;
                    for (std::size_t i = first + static_cast<std::size_t>(0.01 * xRate); i < std::min(x.size(), first + static_cast<std::size_t>(0.02 * xRate)); ++i)
                        body = std::max(body, std::abs(x[i]));
                    shapes << "\n  " << label << ":";
                    for (double t = 0.0; t < 0.006; t += 0.00025)
                        shapes << " " << juce::String(x[std::min(x.size() - 1, first + static_cast<std::size_t>(t * xRate))] / body, 2);
                };
                describe(recorded, rate, "recorded");
                describe(modelled, 96000.0, "model   ");
                logMessage("the first 6 ms at ATTACK " + juce::String(attack) + " (TUNE and DECAY at half):" + shapes);
            }
            // The accent: each level's peak over the first 100 ms and the body's between 10 and 60 ms, in dB from the
            // lowest level's (the click grows with the accent too, so the body says more about the VCA).
            const auto levels = [](const std::vector<double>& x, double rate, std::size_t start)
            {
                double peak = 0.0, body = 0.0;
                for (std::size_t i = start; i < std::min(x.size(), start + static_cast<std::size_t>(0.1 * rate)); ++i)
                {
                    peak = std::max(peak, std::abs(x[i]));
                    if (i >= start + static_cast<std::size_t>(0.01 * rate) && i < start + static_cast<std::size_t>(0.06 * rate))
                        body = std::max(body, std::abs(x[i]));
                }
                return std::make_pair(peak, body);
            };
            const auto db = [](double v, double ref) { return juce::String(20.0 * std::log10(v / ref), 1); };
            juce::String accents;
            std::pair<double, double> first;
            for (int a = 0; a <= 100; a += 10)
            {
                double rate = 44100.0;
                const auto x = load("BassDrum909-accent" + juce::String(a).paddedLeft('0', 3) + "-tune100-attack100-decay100.wav", rate);
                if (x.empty())
                    continue;
                const auto l = levels(x, rate, 0);
                if (a == 0)
                    first = l;
                accents << " " << a << ":" << db(l.first, first.first) << "/" << db(l.second, first.second);
            }
            logMessage("accent, dB above accent 0, peak/body (recorded):" + accents);
            juce::String dacs;
            std::pair<double, double> lowest;
            for (double dac = 2.0; dac < 5.05; dac += 0.2)
            {
                Knobs k { dac, 1.0, 1.0, 1.0, 0.5 };
                const auto r = render(k, 0.11);
                const auto l = levels(r.out, 96000.0, static_cast<std::size_t>(0.005 * 96000.0));
                if (dac < 2.1)
                    lowest = l;
                dacs << " " << juce::String(dac, 1) << "V:" << db(l.first, lowest.first) << "/" << db(l.second, lowest.second);
            }
            logMessage("accent, dB above a 2 V DAC, peak/body (model):" + dacs);
        }

        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_TABLES", {}).isNotEmpty())
        {
            // What the Kick's knobs need from the model: the time to -60 dB at each DECAY resistance (from the hit's
            // peak, by the peaks over 10 ms), and the hit's peak at each accent DAC voltage.
            beginTest("tables");
            auto peaksOf = [](const std::vector<double>& out)
            {
                std::vector<double> peaks;
                for (std::size_t i = 0; i + 960 <= out.size(); i += 960)
                {
                    double p = 0.0;
                    for (std::size_t j = i; j < i + 960; ++j)
                        p = std::max(p, std::abs(out[j]));
                    peaks.push_back(p);
                }
                return peaks;
            };
            juce::String decays;
            for (const double ohms : { 1.0, 10.0e3, 30.0e3, 60.0e3, 100.0e3, 150.0e3, 220.0e3, 330.0e3, 470.0e3, 680.0e3, 1.0e6,
                                       1.5e6, 2.2e6, 3.3e6, 4.7e6, 6.8e6, 10.0e6 })
            {
                auto drum = std::make_unique<Tr909BassDrum>();
                drum->prepare(96000.0);
                drum->setDecayOhms(ohms);
                drum->setLevelOhms(100.0e3);
                drum->trigger(2.5);
                std::vector<double> out(static_cast<std::size_t>(12.0 * 96000.0));
                for (auto& y : out)
                    y = drum->process();
                const auto peaks = peaksOf(out);
                const double top = *std::max_element(peaks.begin(), peaks.end());
                std::size_t end = 0;
                for (std::size_t w = 0; w < peaks.size(); ++w)
                    if (peaks[w] > top * 0.001)
                        end = w + 1;
                decays << " " << juce::String(ohms / 1000.0, 0) << "k:" << juce::String(0.01 * static_cast<double>(end), 3);
            }
            logMessage("DECAY ohms -> seconds to -60 dB:" + decays);
            juce::String levels;
            for (const double dac : { 0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0 })
            {
                auto drum = std::make_unique<Tr909BassDrum>();
                drum->prepare(96000.0);
                drum->setDecayOhms(150.0e3);
                drum->setLevelOhms(100.0e3);
                drum->trigger(dac);
                std::vector<double> out(static_cast<std::size_t>(0.1 * 96000.0));
                for (auto& y : out)
                    y = drum->process();
                const auto peaks = peaksOf(out);
                levels << " " << juce::String(dac, 2) << ":" << juce::String(*std::max_element(peaks.begin(), peaks.end()), 3);
            }
            logMessage("accent DAC volts -> peak volts in the first 100 ms (LEVEL full):" + levels);
        }

        if (const auto path = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_DUMP", {}); path.isNotEmpty())
        {
            // The model's stages in render.py's columns, for tools/kick909/inspect.py.
            beginTest("dump");
            Knobs k;
            k.dac = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_DAC", "2.5").getDoubleValue();
            k.tune = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_TUNE", "1").getDoubleValue();
            k.decay = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_DECAY", "0.5").getDoubleValue();
            k.attack = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_ATTACK", "0.5").getDoubleValue();
            k.level = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_LEVEL", "0.5").getDoubleValue();
            auto drum = std::make_unique<Tr909BassDrum>();
            drum->prepare(96000.0);
            setKnobs(*drum, k);
            if (const auto ohms = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_SHAPER", {}); ohms.isNotEmpty())
                drum->setShaperOhms(ohms.getDoubleValue());
            if (const auto ohms = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_NOISE", {}); ohms.isNotEmpty())
                drum->setNoiseOhms(ohms.getDoubleValue());
            if (const auto ohms = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_909_VR5", {}); ohms.isNotEmpty())
                drum->setDecayOhms(ohms.getDoubleValue());
            juce::File file(path);
            file.deleteFile();
            juce::FileOutputStream csv(file);
            csv << "time,n1,n4,n7c,c9,cv,sq,tri,cmp,nx,c7,e1,nk,c8,c12,q8e,q9e,a2,nvca,out,acc\n";
            const int samples = static_cast<int>(0.4 * 96000.0);
            const auto start = juce::Time::getHighResolutionTicks();
            double busy = 0.0, window = 0.0;
            juce::String profile;
            std::array<int, 6> most {}, atLimit {};
            std::array<double, 6> totalSteps {};
            juce::String early;
            for (int i = 0; i < samples; ++i)
            {
                if (i == static_cast<int>(0.005 * 96000.0))
                    drum->trigger(k.dac);
                const auto before = juce::Time::getHighResolutionTicks();
                const double out = drum->process();
                const double spent = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - before);
                busy += spent;
                window += spent;
                if ((i + 1) % 960 == 0)
                {
                    profile << " " << (i + 1) / 96 - 10 << ":" << juce::String(100.0 * window / 0.01, 1) << (drum->envelopeIsRunning() ? "E" : "D")
                            << (drum->frontIsResting() ? "" : "F");
                    window = 0.0;
                }
                const auto steps = drum->newtonSteps();
                for (std::size_t c = 0; c < 6; ++c)
                {
                    most[c] = std::max(most[c], steps[c]);
                    totalSteps[c] += steps[c];
                    atLimit[c] += steps[c] >= 12 ? 1 : 0;
                }
                if (i >= 480 && i < 480 + 24)
                    early << "\n  " << i - 480 << ": n4 " << juce::String(drum->stage().n4, 3) << " e1 " << juce::String(drum->stage().e1, 3)
                          << " nk " << juce::String(drum->stage().nk, 3) << " c8 " << juce::String(drum->stage().c8, 4)
                          << " steps " << steps[0] << "/" << steps[2];
                const auto& s = drum->stage();
                const double cmp = drum->comparatorIsHigh() ? drum->comparatorSwing : -drum->comparatorSwing;
                csv << juce::String(i / 96000.0, 8) << ",0," << juce::String(s.n4, 6) << "," << juce::String(s.n7c, 6) << ","
                    << juce::String(s.c9, 6) << "," << juce::String(s.cv, 6) << ",0," << juce::String(drum->triangleVolts(), 6) << ","
                    << juce::String(cmp, 3) << "," << juce::String(s.x, 6) << "," << juce::String(s.c7, 6) << ","
                    << juce::String(s.e1, 6) << "," << juce::String(s.nk, 6) << "," << juce::String(s.c8, 6) << "," << juce::String(s.c12, 6) << "," << juce::String(s.q8e, 6) << ","
                    << juce::String(s.q9e, 6) << ",0,0," << juce::String(out, 6) << "," << juce::String(3.13 * k.dac, 4) << "\n";
            }
            csv.flush();
            juce::String solver;
            for (std::size_t c = 0; c < 6; ++c)
                solver << " " << most[c] << "(" << atLimit[c] << ", avg " << juce::String(totalSteps[c] / samples, 2) << ")";
            logMessage("most Newton steps (samples at the limit), front pitch envelope decay attack body:" + solver + early);
            logMessage("% of real time per 10 ms (ms after the hit: E envelope, D decay, F front):" + profile);
            logMessage("wrote " + path + "; the drum took " + juce::String(1.0e3 * busy, 1) + " ms for 0.4 s, all of it "
                       + juce::String(1.0e3 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start), 1) + " ms");
        }
    }
};

static V3Tr909Tests v3Tr909Tests;
