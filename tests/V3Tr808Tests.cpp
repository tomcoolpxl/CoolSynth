#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/Tr808BassDrum.h"

/*
    The TR-808 bass drum circuit model (D235) against ngspice renders of the same schematic
    (tools/kick808/bassdrum.cir, rendered by tools/kick808/render.py; the references are in tests/data/kick808).
*/
namespace
{
    using coolsynth::synth::v3::Tr808BassDrum;

    struct Reference
    {
        std::vector<double> vout, nout, neg, npulse;
        double rate = 96000.0;
    };

    Reference loadReference(const juce::File& file)
    {
        Reference r;
        juce::StringArray lines;
        file.readLines(lines);
        if (lines.size() < 3)
            return r;
        const auto names = juce::StringArray::fromTokens(lines[0], ",", "");
        const int iv = names.indexOf("vout"), in = names.indexOf("nout"), ie = names.indexOf("neg"), ip = names.indexOf("npulse");
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
            r.vout.push_back(f[iv].getDoubleValue());
            r.nout.push_back(f[in].getDoubleValue());
            r.neg.push_back(f[ie].getDoubleValue());
            r.npulse.push_back(f[ip].getDoubleValue());
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
        return juce::File(__FILE__).getParentDirectory().getChildFile("data").getChildFile("kick808");
    }

    double rmsOf(const std::vector<double>& x, std::size_t from, std::size_t to)
    {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += x[i] * x[i];
        return std::sqrt(s / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double rmsDifference(const std::vector<double>& a, const std::vector<double>& b, std::size_t from, std::size_t to)
    {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += (a[i] - b[i]) * (a[i] - b[i]);
        return std::sqrt(s / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    std::vector<std::pair<double, double>> pitchTrack(const std::vector<double>& v, double rate)
    {
        std::vector<double> crossings;
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i - 1] < 0.0 && v[i] >= 0.0)
                crossings.push_back((static_cast<double>(i - 1) - v[i - 1] / (v[i] - v[i - 1])) / rate);
        std::vector<std::pair<double, double>> out;
        for (std::size_t i = 1; i < crossings.size(); ++i)
            out.emplace_back(0.5 * (crossings[i] + crossings[i - 1]), 1.0 / (crossings[i] - crossings[i - 1]));
        return out;
    }
}

class V3Tr808Tests final : public juce::UnitTest
{
public:
    V3Tr808Tests() : juce::UnitTest("V3 TR-808 circuit", "V3") {}

    void runTest() override
    {
        beginTest("the_circuit_matches_spice_stage_by_stage");
        {
            const auto file = referenceFolder().getChildFile("a4_d04.csv");
            if (! file.existsAsFile())
            {
                logMessage("no reference at " + file.getFullPathName());
                expect(false, "the SPICE reference is missing");
                return;
            }
            const auto ref = loadReference(file);
            auto drum = std::make_unique<Tr808BassDrum>();
            drum->prepare(ref.rate);
            drum->setDecay(0.4);
            drum->setTone(0.99);
            drum->setLevel(0.99);
            drum->settle();
            drum->trigger(4.0);
            const std::size_t n = ref.vout.size();
            std::vector<double> vout(n), nout(n), neg(n), npulse(n);
            int worstNewton = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                vout[i] = drum->process();
                nout[i] = drum->resonatorVolts();
                neg[i] = drum->envelopeVolts();
                npulse[i] = drum->pulseVolts();
                worstNewton = std::max(worstNewton, drum->newtonSteps());
            }
            const auto early = static_cast<std::size_t>(0.03 * ref.rate);
            auto report = [&](const char* name, const std::vector<double>& model, const std::vector<double>& spice)
            {
                const double err = rmsDifference(model, spice, 0, early), level = rmsOf(spice, 0, early);
                logMessage(juce::String(name) + ": rms error " + juce::String(err, 4) + " V against " + juce::String(level, 4)
                           + " V in the first 30 ms; at 1, 4, 11, 18 ms: "
                           + juce::String(model[static_cast<std::size_t>(0.001 * ref.rate)], 3) + "/" + juce::String(spice[static_cast<std::size_t>(0.001 * ref.rate)], 3) + " "
                           + juce::String(model[static_cast<std::size_t>(0.004 * ref.rate)], 3) + "/" + juce::String(spice[static_cast<std::size_t>(0.004 * ref.rate)], 3) + " "
                           + juce::String(model[static_cast<std::size_t>(0.011 * ref.rate)], 3) + "/" + juce::String(spice[static_cast<std::size_t>(0.011 * ref.rate)], 3) + " "
                           + juce::String(model[static_cast<std::size_t>(0.018 * ref.rate)], 3) + "/" + juce::String(spice[static_cast<std::size_t>(0.018 * ref.rate)], 3));
                return err / std::max(1.0e-9, level);
            };
            const double pulseError = report("pulse shaper", npulse, ref.npulse);
            const double envelopeError = report("envelope", neg, ref.neg);
            const double resonatorError = report("resonator", nout, ref.nout);
            const double outputError = report("output", vout, ref.vout);
            logMessage("most Newton steps in a sample: " + juce::String(worstNewton));
            {
                auto timed = std::make_unique<Tr808BassDrum>();
                timed->prepare(96000.0);
                timed->trigger(4.0);
                const auto start = juce::Time::getHighResolutionTicks();
                double sink = 0.0;
                for (int i = 0; i < 96000; ++i)
                    sink += timed->process();
                const double seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
                logMessage("one second of the drum at 96 kHz took " + juce::String(seconds * 1000.0, 1) + " ms (" + juce::String(sink, 1) + ")");
            }
            expectLessThan(pulseError, 0.05, "pulse shaper");
            expectLessThan(envelopeError, 0.02, "envelope");
            expectLessThan(resonatorError, 0.05, "resonator");
            expectLessThan(outputError, 0.03, "output");
        }

        beginTest("an_accented_hit_matches_spice");
        {
            const auto ref = loadReference(referenceFolder().getChildFile("a14_d04.csv"));
            expect(! ref.vout.empty(), "the SPICE reference is missing");
            if (ref.vout.empty())
                return;
            auto drum = std::make_unique<Tr808BassDrum>();
            drum->prepare(ref.rate);
            drum->setDecay(0.4);
            drum->setTone(0.99);
            drum->setLevel(0.99);
            drum->settle();
            drum->trigger(14.0);
            std::vector<double> vout(ref.vout.size());
            for (auto& v : vout)
                v = drum->process();
            const double error = rmsDifference(vout, ref.vout, 0, vout.size()) / rmsOf(ref.vout, 0, vout.size());
            logMessage("14 V: output rms error " + juce::String(error * 100.0, 2) + " %");
            expectLessThan(error, 0.03);
        }

        beginTest("the_sigh_and_the_decay_follow_spice");
        {
            // From the ngspice renders (tools/kick808): the pitch by upward zero crossings settles from about 59 Hz to
            // 49.4 Hz, slower when accented; the tail falls 85.7 dB/s at Decay 0.4 and 33.9 dB/s at 0.8.
            struct Case { double volts, decay, at, hz; };
            for (const auto& c : { Case { 4.0, 0.4, 0.0147, 58.89 }, Case { 4.0, 0.4, 0.0511, 51.82 }, Case { 4.0, 0.4, 0.1516, 49.39 },
                                   Case { 14.0, 0.4, 0.0507, 55.78 }, Case { 14.0, 0.4, 0.1061, 51.68 }, Case { 14.0, 0.4, 0.2068, 49.42 } })
            {
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->prepare(96000.0);
                drum->setDecay(c.decay);
                drum->settle();
                drum->trigger(c.volts);
                std::vector<double> v(static_cast<std::size_t>(0.3 * 96000.0));
                for (auto& s : v)
                    s = drum->process();
                double heard = 0.0, nearest = 1.0;
                for (const auto& [t, hz] : pitchTrack(v, 96000.0))
                    if (std::abs(t - c.at) < nearest)
                    {
                        nearest = std::abs(t - c.at);
                        heard = hz;
                    }
                expectWithinAbsoluteError(heard, c.hz, 0.25, juce::String(c.volts) + " V at " + juce::String(c.at * 1000.0, 0) + " ms");
            }
            for (const auto& [decay, dbPerSecond] : { std::pair { 0.4, -85.7 }, std::pair { 0.8, -33.9 } })
            {
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->prepare(96000.0);
                drum->setDecay(decay);
                drum->settle();
                drum->trigger(4.0);
                const int window = static_cast<int>(0.02 * 96000.0);
                std::vector<double> peaks;
                for (int w = 0; w < 30; ++w)
                {
                    double peak = 0.0;
                    for (int i = 0; i < window; ++i)
                        peak = std::max(peak, std::abs(drum->process()));
                    peaks.push_back(peak);
                }
                const double measured = 20.0 * std::log10(peaks.back() / peaks[1]) / (28.0 * 0.02);
                logMessage("decay " + juce::String(decay) + ": " + juce::String(measured, 1) + " dB/s");
                expectWithinAbsoluteError(measured, dbPerSecond, std::abs(dbPerSecond) * 0.03);
            }
        }
    }
};

static V3Tr808Tests v3Tr808Tests;
