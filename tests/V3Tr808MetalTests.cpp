#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include "synth/v3/Part.h"
#include "synth/v3/Tr808Cowbell.h"
#include "synth/v3/Tr808Cymbal.h"
#include "synth/v3/Tr808Hats.h"
#include "synth/v3/Tr808MetalBank.h"
#include "synth/v3/Tr808MetalGenerator.h"

/*
    The 808 Metal's circuits (D254, D255) against ngspice renders of the voicing board (tools/tr808/metal.cir, rendered
    by tools/tr808/render.py at its default oscillator phases; the references are in tests/data/tr808): each stage's
    node, sample by sample, over the first 30 ms of a hit from rest.
*/
namespace
{
    using coolsynth::synth::v3::Tr808Cowbell;
    using coolsynth::synth::v3::Tr808Cymbal;
    using coolsynth::synth::v3::Tr808Hats;
    using coolsynth::synth::v3::Tr808MetalBank;
    using coolsynth::synth::v3::Tr808MetalTriggerBuffer;

    constexpr std::array<double, 6> referencePhases { 0.1, 0.37, 0.62, 0.05, 0.81, 0.44 }; // render.py's default

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

    /** The rms difference over the rms of the reference; with ac, each less its own mean (an emitter follower's
        offset, which the model's ideal follower does not have, is no difference that reaches the output). */
    double relativeError(const std::vector<double>& model, const std::vector<double>& spice, bool ac = false)
    {
        const std::size_t n = std::min(model.size(), spice.size());
        double mm = 0.0, ms = 0.0;
        if (ac && n > 0)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                mm += model[i];
                ms += spice[i];
            }
            mm /= static_cast<double>(n);
            ms /= static_cast<double>(n);
        }
        double e = 0.0, s = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = model[i] - mm, b = spice[i] - ms;
            e += (a - b) * (a - b);
            s += b * b;
        }
        return std::sqrt(e / std::max(1.0e-30, s));
    }

    /** The rms ratio of the model to the reference, in dB: a level's agreement, whatever the waveforms' phases. */
    double levelDb(const std::vector<double>& model, const std::vector<double>& spice, std::size_t from, std::size_t to)
    {
        double a = 0.0, b = 0.0;
        for (std::size_t i = from; i < std::min({ to, model.size(), spice.size() }); ++i)
        {
            a += model[i] * model[i];
            b += spice[i] * spice[i];
        }
        return 10.0 * std::log10(std::max(1.0e-30, a) / std::max(1.0e-30, b));
    }

    /**
        The rms of the model's third-octave band levels less the reference's, 1 to 20 kHz, in dB (Hann window over
        the first 15 ms; the bands within 30 dB of the reference's strongest): an output's spectrum, which the switching VCAs' edges falling a fraction of a sample apart
        leave alone while they move its samples (D255).
    */
    double bandDifferenceDb(const std::vector<double>& model, const std::vector<double>& spice, double rate)
    {
        constexpr int order = 15;
        constexpr int size = 1 << order;
        const std::size_t n = std::min({ model.size(), spice.size(), static_cast<std::size_t>(0.015 * rate), static_cast<std::size_t>(size) });
        juce::dsp::FFT fft(order);
        const auto levels = [&](const std::vector<double>& x)
        {
            std::vector<float> buffer(2 * size, 0.0f);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * static_cast<double>(i) / static_cast<double>(n - 1));
                buffer[i] = static_cast<float>(x[i] * w);
            }
            fft.performFrequencyOnlyForwardTransform(buffer.data());
            std::vector<double> out;
            for (const double centre : { 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0 })
            {
                double e = 0.0;
                for (int k = 0; k <= size / 2; ++k)
                {
                    const double f = rate * k / size;
                    if (f >= centre / std::pow(2.0, 1.0 / 6.0) && f < centre * std::pow(2.0, 1.0 / 6.0))
                        e += static_cast<double>(buffer[(std::size_t) k]) * static_cast<double>(buffer[(std::size_t) k]);
                }
                out.push_back(10.0 * std::log10(e + 1.0e-30));
            }
            return out;
        };
        // The bands that carry the sound: within 30 dB of the reference's strongest.
        const auto a = levels(model), b = levels(spice);
        const double strongest = *std::max_element(b.begin(), b.end());
        double sum = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (b[i] > strongest - 30.0)
            {
                sum += (a[i] - b[i]) * (a[i] - b[i]);
                ++count;
            }
        return std::sqrt(sum / std::max(1, count));
    }

    /**
        How close each node must come to the reference, sample by sample: the envelopes and IC3's bands within 5 %;
        the swing VCAs' gates, which switch when the band crosses their base's knee, and the high-passes and outputs
        behind them within what 96 kHz allows (the same circuits at 192 kHz come within 1 to 9 %, D255). The outputs
        are held to their spectrum and level besides.
    */
    double toleranceOf(const juce::String& node)
    {
        if (node.startsWith("v"))
            return node == "vcb" ? 0.05 : 0.6;
        if (node == "np1" || node == "np2" || node == "ncbo")
            return 0.08;
        if (node == "nh1" || node == "nh2" || node == "nv1" || node == "nv2c")
            return 0.4;
        if (node == "nhpa" || node == "nhpb" || node == "nhpo" || node == "nhpc")
            return 0.6;
        return 0.05;
    }

    /** The bank and IC3 at the reference's time 0. */
    std::unique_ptr<Tr808MetalBank> referenceBank(double rate)
    {
        auto bank = std::make_unique<Tr808MetalBank>();
        bank->prepare(rate);
        bank->setTiming(1.0, 0.0);
        bank->start(referencePhases);
        return bank;
    }

    /** With COOLSYNTH_808_DUMP set to a folder, the model's columns there as CSV, for comparing by eye. */
    void dumpModel(const char* name, const std::map<juce::String, std::vector<double>>& model)
    {
        const auto folder = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_DUMP", {});
        if (folder.isEmpty() || model.empty())
            return;
        juce::String text;
        for (const auto& [node, values] : model)
            text << (text.isEmpty() ? "" : ",") << node;
        text << "\n";
        const std::size_t n = model.begin()->second.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            juce::String row;
            for (const auto& [node, values] : model)
                row << (row.isEmpty() ? "" : ",") << juce::String(values[i], 7);
            text << row << "\n";
        }
        juce::File(folder).getChildFile(juce::String("model_") + name).replaceWithText(text);
    }

    bool hasReference(juce::UnitTest& test, const char* name)
    {
        if (referenceFolder().getChildFile(name).existsAsFile())
            return true;
        test.expect(false, juce::String("the SPICE reference is missing: ") + name);
        return false;
    }
}

class V3Tr808MetalTests final : public juce::UnitTest
{
public:
    V3Tr808MetalTests() : juce::UnitTest("V3 TR-808 metal circuits", "V3") {}

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_PROFILE", {}).isNotEmpty())
        {
            beginTest("profile");
            constexpr int n = 96000;
            auto bank = referenceBank(96000.0);
            Tr808MetalTriggerBuffer buffer;
            auto bell = std::make_unique<Tr808Cowbell>();
            auto cymbal = std::make_unique<Tr808Cymbal>();
            auto hats = std::make_unique<Tr808Hats>();
            bell->prepare(96000.0);
            cymbal->prepare(96000.0);
            hats->prepare(96000.0);
            const auto time = [](auto&& body)
            {
                const auto start = juce::Time::getHighResolutionTicks();
                body();
                return 1.0e6 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) / n;
            };
            std::vector<double> hb(n), lb(n), o1(n), o2(n);
            const double tBank = time([&] { for (int i = 0; i < n; ++i) { bank->process(); hb[(size_t) i] = bank->high(); lb[(size_t) i] = bank->low(); o1[(size_t) i] = bank->oscillator(0); o2[(size_t) i] = bank->oscillator(1); } });
            const double tCymVcaRest = time([&] { for (int i = 0; i < n; ++i) cymbal->processVcas(hb[(size_t) i], lb[(size_t) i]); });
            const double tHatVcaRest = time([&] { for (int i = 0; i < n; ++i) hats->processVcas(hb[(size_t) i]); });
            {
                std::array<double, 5> steps {};
                std::array<int, 5> most {};
                for (int i = 0; i < n; ++i)
                {
                    cymbal->processVcas(hb[(size_t) i], lb[(size_t) i]);
                    hats->processVcas(hb[(size_t) i]);
                    for (int w = 0; w < 5; ++w)
                    {
                        const int k = w < 3 ? cymbal->vcaNewtonSteps(w) : hats->vcaNewtonSteps(w - 3);
                        steps[(size_t) w] += k;
                        most[(size_t) w] = std::max(most[(size_t) w], k);
                    }
                }
                juce::String line = "Newton steps a sample at rest (top, middle, low, open, closed):";
                for (int w = 0; w < 5; ++w)
                    line << " " << juce::String(steps[(size_t) w] / n, 2) << " (most " << most[(size_t) w] << ")";
                logMessage(line);
            }
            cymbal->trigger(buffer.volts(14.0));
            hats->trigger(Tr808Hats::open, buffer.volts(14.0));
            bell->trigger(buffer.volts(14.0));
            double tCymEnv = 0.0, tCymVca = 0.0, tCymMix = 0.0, tHatEnv = 0.0, tHatVca = 0.0, tHatOut = 0.0, tBell = 0.0;
            tCymEnv = time([&] { for (int i = 0; i < n; ++i) cymbal->processEnvelope(); });
            tCymVca = time([&] { for (int i = 0; i < n; ++i) cymbal->processVcas(hb[(size_t) i], lb[(size_t) i]); });
            tCymMix = time([&] { for (int i = 0; i < n; ++i) cymbal->processMix(); });
            tHatEnv = time([&] { for (int i = 0; i < n; ++i) hats->processEnvelope(Tr808Hats::open); });
            tHatVca = time([&] { for (int i = 0; i < n; ++i) hats->processVcas(hb[(size_t) i]); });
            tHatOut = time([&] { for (int i = 0; i < n; ++i) hats->processOutput(Tr808Hats::open); });
            tBell = time([&] { for (int i = 0; i < n; ++i) bell->process(o1[(size_t) i], o2[(size_t) i]); });
            logMessage("us a sample at 96 kHz: bank " + juce::String(tBank, 3) + ", cymbal VCAs at rest " + juce::String(tCymVcaRest, 3)
                       + ", hat VCAs at rest " + juce::String(tHatVcaRest, 3) + "; sounding: cymbal envelope " + juce::String(tCymEnv, 3)
                       + ", its VCAs " + juce::String(tCymVca, 3) + ", its mix " + juce::String(tCymMix, 3) + ", open hat envelope "
                       + juce::String(tHatEnv, 3) + ", hat VCAs " + juce::String(tHatVca, 3) + ", its output " + juce::String(tHatOut, 3)
                       + ", cowbell " + juce::String(tBell, 3) + " (a block's share: us x 512 / 5333)");
            return;
        }

        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_COUPLING", {}).isNotEmpty())
        {
            // Where the VCAs' coupling capacitors settle with the bank running and nothing struck (the rest states'
            // constants): the five on IC3's bands over 1 s, the cowbell's two over 20 s.
            beginTest("coupling");
            auto bank = referenceBank(96000.0);
            auto cymbal = std::make_unique<Tr808Cymbal>();
            auto hats = std::make_unique<Tr808Hats>();
            auto bell = std::make_unique<Tr808Cowbell>();
            cymbal->prepare(96000.0);
            hats->prepare(96000.0);
            bell->prepare(96000.0);
            cymbal->settle(0.0, 0.0);
            hats->settle(0.0);
            bell->settle(bank->standing(0), bank->standing(1));
            std::array<double, 7> sum {};
            int count = 0;
            for (int i = 0; i < 20 * 96000; ++i)
            {
                bank->process();
                if (i < 96000)
                {
                    cymbal->processVcas(bank->high(), bank->low());
                    hats->processVcas(bank->high());
                }
                (void) bell->process(bank->oscillator(0), bank->oscillator(1));
                if (i >= 48000 && i < 96000)
                {
                    for (int w = 0; w < 3; ++w)
                        sum[(size_t) w] += cymbal->couplingVolts(w);
                    sum[3] += hats->couplingVolts(Tr808Hats::open);
                    sum[4] += hats->couplingVolts(Tr808Hats::closed);
                    ++count;
                }
                if (i % (2 * 96000) == 0 || i == 20 * 96000 - 1)
                    logMessage("at " + juce::String(i / 96000.0, 1) + " s: C32 " + juce::String(bell->couplingVolts(0), 4) + ", C33 "
                               + juce::String(bell->couplingVolts(1), 4));
            }
            logMessage("means 0.5 to 1 s: C42 " + juce::String(sum[0] / count, 4) + ", C44 " + juce::String(sum[1] / count, 4) + ", C46 "
                       + juce::String(sum[2] / count, 4) + ", C67 " + juce::String(sum[3] / count, 4) + ", C71 " + juce::String(sum[4] / count, 4));
            return;
        }

        beginTest("the_oscillators_run_as_werner_abel_and_smith_give_them");
        {
            auto bank = std::make_unique<Tr808MetalBank>();
            bank->prepare(96000.0);
            const double expected[] = { 554.0, 825.0, 522.7, 304.4, 369.6, 205.3 }; // the cowbell's as both units (D255)
            for (int k = 0; k < 6; ++k)
            {
                expectWithinAbsoluteError(bank->frequencyOf(k), expected[k], 0.1, "oscillator " + juce::String(k + 1));
                expectWithinAbsoluteError(bank->dutyOf(k), 0.4798, 0.0001);
            }
            bank->setTiming(0.5, 2.0); // Tune an octave up, the cowbell's trimmers 2 semitones up
            expectWithinAbsoluteError(bank->frequencyOf(2), 2.0 * 522.7, 0.2);
            expectWithinAbsoluteError(bank->frequencyOf(0), 2.0 * 554.0 * std::pow(2.0, 2.0 / 12.0), 0.2);
            // Counted: 1 s of the 205 Hz oscillator, its rising edges through the middle.
            bank->setTiming(1.0, 0.0);
            bank->start({ 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 });
            int rises = 0;
            double last = bank->oscillator(5);
            for (int i = 0; i < 96000; ++i)
            {
                bank->process();
                const double v = bank->oscillator(5);
                rises += last < 2.5 && v >= 2.5 ? 1 : 0;
                last = v;
            }
            expectWithinAbsoluteError(rises, 205, 1);
        }

        beginTest("the_trigger_buffer_narrows_the_trigger_as_spice");
        {
            for (const auto& [file, trig] : { std::pair { "cb.csv", 4.0 }, std::pair { "cb14.csv", 14.0 } })
            {
                if (! hasReference(*this, file))
                    continue;
                const auto ref = loadReference(referenceFolder().getChildFile(file));
                // IC2a's output in the middle of the pulse.
                const double spice = ref.columns.at("nv2")[48];
                Tr808MetalTriggerBuffer buffer;
                const double model = buffer.volts(trig);
                logMessage(juce::String(file) + ": IC2a at " + juce::String(model, 4) + " V, SPICE " + juce::String(spice, 4));
                expectWithinAbsoluteError(model, spice, 0.02 * spice);
            }
        }

        beginTest("the_bank_matches_spice");
        {
            if (hasReference(*this, "cb.csv"))
            {
                const auto ref = loadReference(referenceFolder().getChildFile("cb.csv"));
                auto bank = referenceBank(ref.rate);
                expect(bank->wellPosed());
                std::map<juce::String, std::vector<double>> model;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    bank->process();
                    model["nsum"].push_back(bank->mixVolts());
                    model["nhb"].push_back(bank->high());
                    model["nlb"].push_back(bank->low());
                }
                juce::String line = "bank:";
                for (const auto* node : { "nsum", "nhb", "nlb" })
                {
                    const double err = relativeError(model[node], ref.columns.at(node));
                    line << " " << node << " " << juce::String(err, 4) << " (" << juce::String(levelDb(model[node], ref.columns.at(node), 0, 2880), 2) << " dB)";
                    expectLessThan(err, 0.05, node);
                }
                logMessage(line);
            }
        }

        beginTest("the_cowbell_matches_spice_stage_by_stage");
        {
            for (const auto& [file, trig] : { std::pair { "cb.csv", 4.0 }, std::pair { "cb14.csv", 14.0 } })
            {
                if (! hasReference(*this, file))
                    continue;
                const auto ref = loadReference(referenceFolder().getChildFile(file));
                auto bank = referenceBank(ref.rate);
                Tr808MetalTriggerBuffer buffer;
                auto bell = std::make_unique<Tr808Cowbell>();
                bell->prepare(ref.rate);
                bell->setLevel(0.8);
                bell->settle(bank->standing(0), bank->standing(1));
                expect(bell->wellPosed(), "the cowbell's circuits are solvable");
                bell->trigger(buffer.volts(trig));
                std::map<juce::String, std::vector<double>> model;
                bool converged = true;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    bank->process();
                    model["vcb"].push_back(bell->process(bank->oscillator(0), bank->oscillator(1)));
                    model["ncbe"].push_back(bell->envelopeVolts());
                    model["np1"].push_back(bell->gateVolts(0));
                    model["np2"].push_back(bell->gateVolts(1));
                    model["ncbo"].push_back(bell->bandVolts());
                    converged = converged && bell->lastSolveConverged();
                }
                dumpModel(file, model);
                juce::String line = juce::String(file) + ":";
                for (const auto* node : { "ncbe", "np1", "np2", "ncbo", "vcb" })
                {
                    const double err = relativeError(model[node], ref.columns.at(node));
                    line << " " << node << " " << juce::String(err, 4);
                    expectLessThan(err, toleranceOf(node), juce::String(file) + " " + node);
                }
                logMessage(line);
                expect(converged, juce::String(file) + ": every sample converged");
            }
        }

        beginTest("the_cymbal_matches_spice_stage_by_stage");
        {
            for (const auto& [file, trig] : { std::pair { "cy.csv", 4.0 }, std::pair { "cy14.csv", 14.0 } })
            {
                if (! hasReference(*this, file))
                    continue;
                const auto ref = loadReference(referenceFolder().getChildFile(file));
                auto bank = referenceBank(ref.rate);
                Tr808MetalTriggerBuffer buffer;
                auto cymbal = std::make_unique<Tr808Cymbal>();
                cymbal->prepare(ref.rate);
                cymbal->setLevel(0.8);
                cymbal->setTone(0.5);
                cymbal->setDecay(0.5);
                cymbal->settle(0.0, 0.0);
                expect(cymbal->wellPosed(), "the cymbal's circuits are solvable");
                cymbal->trigger(buffer.volts(trig));
                std::map<juce::String, std::vector<double>> model;
                bool converged = true;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    bank->process();
                    cymbal->processEnvelope();
                    cymbal->processVcas(bank->high(), bank->low());
                    model["vcy"].push_back(cymbal->processMix());
                    model["nj"].push_back(cymbal->envelopeVolts(0));
                    model["nm"].push_back(cymbal->envelopeVolts(1));
                    model["nv"].push_back(cymbal->envelopeVolts(2));
                    model["nt"].push_back(cymbal->envelopeVolts(3));
                    model["nn"].push_back(cymbal->envelopeVolts(4));
                    model["nh1"].push_back(cymbal->gateVolts(0));
                    model["nh2"].push_back(cymbal->gateVolts(1));
                    model["nu"].push_back(cymbal->gateVolts(2));
                    model["nhpa"].push_back(cymbal->bandOut(0));
                    model["nhpb"].push_back(cymbal->bandOut(1));
                    model["nhp3"].push_back(cymbal->bandOut(2));
                    model["nk"].push_back(cymbal->envelopeVolts(5));
                    converged = converged && cymbal->lastSolveConverged();
                }
                dumpModel(file, model);
                juce::String line = juce::String(file) + ":";
                for (const auto* node : { "nj", "nm", "nv", "nt", "nn", "nh1", "nh2", "nu", "nhpa", "nhpb", "nhp3", "vcy" })
                {
                    const bool ac = juce::String(node) == "nhp3";
                    const double err = relativeError(model[node], ref.columns.at(node), ac);
                    line << " " << node << " " << juce::String(err, 4);
                    expectLessThan(err, toleranceOf(node), juce::String(file) + " " + node);
                }
                const double level = levelDb(model["vcy"], ref.columns.at("vcy"), 0, ref.size());
                const double bands = bandDifferenceDb(model["vcy"], ref.columns.at("vcy"), ref.rate);
                line << " | level " << juce::String(level, 2) << " dB, bands " << juce::String(bands, 2) << " dB rms";
                expectLessThan(std::abs(level), 1.0, juce::String(file) + " level");
                expectLessThan(bands, 1.5, juce::String(file) + " spectrum");
                logMessage(line);
                expect(converged, juce::String(file) + ": every sample converged");
            }
        }

        beginTest("the_hats_match_spice_stage_by_stage");
        {
            struct HatCase
            {
                const char* name;
                double trig, closedAt; // the closed hat's hit, seconds (-1: none)
                bool open;
            };
            const HatCase cases[] = { { "oh.csv", 4.0, -1.0, true }, { "oh14.csv", 14.0, -1.0, true },
                                      { "ch.csv", 4.0, -1.0, false }, { "ch14.csv", 14.0, -1.0, false },
                                      { "ohch.csv", 4.0, 0.012, true } };
            for (const auto& c : cases)
            {
                if (! hasReference(*this, c.name))
                    continue;
                const auto ref = loadReference(referenceFolder().getChildFile(c.name));
                auto bank = referenceBank(ref.rate);
                Tr808MetalTriggerBuffer buffer;
                auto hats = std::make_unique<Tr808Hats>();
                hats->prepare(ref.rate);
                hats->setLevel(Tr808Hats::open, 0.8);
                hats->setLevel(Tr808Hats::closed, 0.8);
                hats->setOpenDecay(0.5);
                hats->settle(0.0);
                const int first = c.open ? Tr808Hats::open : Tr808Hats::closed;
                expect(hats->wellPosed(), "the hats' circuits are solvable");
                hats->trigger(first, buffer.volts(c.trig));
                const auto closedSample = c.closedAt < 0.0 ? std::size_t(-1) : static_cast<std::size_t>(std::lround(c.closedAt * ref.rate));
                std::map<juce::String, std::vector<double>> model;
                bool converged = true, closedRunning = ! c.open;
                for (std::size_t i = 0; i < ref.size(); ++i)
                {
                    if (i == closedSample)
                    {
                        hats->trigger(Tr808Hats::closed, buffer.volts(c.trig));
                        closedRunning = true;
                    }
                    bank->process();
                    if (c.open)
                        hats->processEnvelope(Tr808Hats::open);
                    if (closedRunning)
                        hats->processEnvelope(Tr808Hats::closed);
                    hats->processVcas(bank->high());
                    model["voh"].push_back(hats->processOutput(Tr808Hats::open));
                    model["vch"].push_back(hats->processOutput(Tr808Hats::closed));
                    model["ne1"].push_back(hats->openEnvelopeVolts());
                    model["noenv"].push_back(hats->openEnvelopeOutVolts());
                    model["ng2"].push_back(hats->openTimerVolts());
                    model["nv1"].push_back(hats->gateVolts(Tr808Hats::open));
                    model["nhpo"].push_back(hats->highPassVolts(Tr808Hats::open));
                    model["nce"].push_back(hats->closedEnvelopeVolts());
                    model["nv2c"].push_back(hats->gateVolts(Tr808Hats::closed));
                    model["nhpc"].push_back(hats->highPassVolts(Tr808Hats::closed));
                    converged = converged && hats->lastSolveConverged();
                }
                dumpModel(c.name, model);
                juce::String line = juce::String(c.name) + ":";
                for (const auto& [node, values] : ref.columns)
                {
                    if (model.count(node) == 0)
                        continue;
                    const bool ac = node == "nhpo" || node == "nhpc";
                    const double err = relativeError(model[node], values, ac);
                    line << " " << node << " " << juce::String(err, 4);
                    expectLessThan(err, toleranceOf(node), juce::String(c.name) + " " + node);
                }
                const juce::String out = c.open ? "voh" : "vch";
                const double level = levelDb(model[out], ref.columns.at(out), 0, ref.size());
                const double bands = bandDifferenceDb(model[out], ref.columns.at(out), ref.rate);
                line << " | level " << juce::String(level, 2) << " dB, bands " << juce::String(bands, 2) << " dB rms";
                expectLessThan(std::abs(level), 1.0, juce::String(c.name) + " level");
                expectLessThan(bands, 1.5, juce::String(c.name) + " spectrum");
                logMessage(line);
                expect(converged, juce::String(c.name) + ": every sample converged");
            }
        }
    }
};

static V3Tr808MetalTests v3Tr808MetalTests;

namespace
{
    using namespace coolsynth::synth::v3;

    GeneratorSettings metalOf(std::initializer_list<std::pair<int, float>> units = {})
    {
        GeneratorSettings g;
        g.type = generatorType::metal808;
        const auto& d = GeneratorSlot::typeTable().descriptor(g.type);
        for (int i = 0; i < d.usedParams; ++i)
            g.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, value] : units)
            g.p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], value);
        g.level = 0.5f;
        return g;
    }

    PartSettings metalKitOf(std::initializer_list<std::pair<int, float>> units = {})
    {
        PartSettings s;
        s.playMode = PlayMode::kit;
        s.voice.generators[0] = metalOf(units);
        Envelope::Parameters amp;
        amp.attack = 0.0005f;
        amp.decay = 0.2f;
        amp.sustain = 1.0f;
        amp.release = 0.01f;
        s.voice.envelopes = { amp, amp, amp };
        return s;
    }

    struct MetalHit
    {
        int sample, note;
        bool off = false;
    };

    /** A part playing these hits, host-rate mono out; the part's voices active at the end. */
    std::vector<float> playMetal(const PartSettings& settings, const std::vector<MetalHit>& hits, double seconds, int* activeAtEnd = nullptr,
                                 std::uint32_t seed = 11)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        auto part = std::make_unique<Part>();
        part->prepare(rate, block, seed);
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

    double energyOf(const std::vector<float>& x, double rate, double from, double to)
    {
        double e = 0.0;
        for (auto i = static_cast<std::size_t>(from * rate); i < std::min(x.size(), static_cast<std::size_t>(to * rate)); ++i)
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return e;
    }

    float metalPeakOf(const std::vector<float>& x)
    {
        float p = 0.0f;
        for (const float v : x)
            p = std::max(p, std::abs(v));
        return p;
    }
}

/*
    The 808 Metal in the Kit play mode (D254, D255): its keys and knobs, hits in one block, the closed hat cutting the
    open one, the voice ending once everything has rung out, and its start from silence (seeded, bit for bit).
*/
class V3Tr808MetalKitTests final : public juce::UnitTest
{
public:
    V3Tr808MetalKitTests() : juce::UnitTest("V3 TR-808 metal kit", "V3") {}

    void runTest() override
    {
        if (const auto folder = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_METAL_RENDER", {}); folder.isNotEmpty())
        {
            // The generator's hits at the units' settings, its circuits' volts as raw float32 at 96 kHz, for
            // docs/research/808-metal.md: every level full, unaccented unless named _a1.
            beginTest("render");
            const auto render = [&](const juce::String& name, std::initializer_list<std::pair<int, float>> units, int note, double seconds, std::uint32_t seed = 5)
            {
                SlotMemory memory;
                memory.allocate(Tr808MetalGenerator::memoryFloats(96000.0, 64));
                memory.setSeed(seed);
                auto g = std::make_unique<Tr808MetalGenerator>();
                g->prepare(96000.0, 64, memory);
                SlotParams params;
                const auto& d = Tr808MetalGenerator::descriptor();
                for (int i = 0; i < d.usedParams; ++i)
                    params.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
                for (const int level : { Tr808MetalGenerator::cbLevel, Tr808MetalGenerator::cyLevel, Tr808MetalGenerator::ohLevel, Tr808MetalGenerator::chLevel })
                    params.p[static_cast<std::size_t>(level)] = 1.0f;
                params.p[Tr808MetalGenerator::accent] = 0.0f;
                for (const auto& [index, value] : units)
                    params.p[static_cast<std::size_t>(index)] = value;
                NoteContext context;
                context.noteNumber = note;
                context.velocity = 1.0f;
                g->noteOn(context);
                std::vector<float> out;
                std::array<float, 64> l {}, r {};
                for (int b = 0; b < static_cast<int>(seconds * 96000.0) / 64; ++b)
                {
                    g->process(l.data(), r.data(), 64, params);
                    for (const float v : l)
                        out.push_back(v / static_cast<float>(Tr808MetalGenerator::circuitVoltsToLevel));
                }
                juce::File(folder).getChildFile(name + ".f32").replaceWithData(out.data(), out.size() * sizeof(float));
            };
            for (std::uint32_t seed = 1; seed <= 4; ++seed)
            {
                render("cb_s" + juce::String(seed), {}, 56, 0.5, seed);
                render("ch_s" + juce::String(seed), {}, 42, 0.5, seed);
                render("oh_d50_s" + juce::String(seed), { { Tr808MetalGenerator::ohDecay, 0.5f } }, 46, 1.5, seed);
                render("cy_t50_d50_s" + juce::String(seed), { { Tr808MetalGenerator::cyTone, 0.5f }, { Tr808MetalGenerator::cyDecay, 0.5f } }, 49, 3.5, seed);
            }
            render("cb_a1", { { Tr808MetalGenerator::accent, 1.0f } }, 56, 0.5);
            render("ch_a1", { { Tr808MetalGenerator::accent, 1.0f } }, 42, 0.5);
            for (const int k : { 0, 25, 50, 75, 100 })
                render("oh_d" + juce::String(k), { { Tr808MetalGenerator::ohDecay, k / 100.0f } }, 46, 1.5);
            render("oh_d50_a1", { { Tr808MetalGenerator::ohDecay, 0.5f }, { Tr808MetalGenerator::accent, 1.0f } }, 46, 1.5);
            for (const int t : { 0, 25, 50, 75, 100 })
                for (const int k : { 0, 25, 50, 75, 100 })
                    render("cy_t" + juce::String(t) + "_d" + juce::String(k),
                           { { Tr808MetalGenerator::cyTone, t / 100.0f }, { Tr808MetalGenerator::cyDecay, k / 100.0f } }, 49, 3.5);
            render("cy_t50_d50_a1", { { Tr808MetalGenerator::cyTone, 0.5f }, { Tr808MetalGenerator::cyDecay, 0.5f }, { Tr808MetalGenerator::accent, 1.0f } }, 49, 3.5);
            return;
        }

        beginTest("the_808_metal_is_generator_type_fourteen_with_twelve_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 15);
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::metal808).name), juce::String("808 Metal"));
            expectEquals(Tr808MetalGenerator::descriptor().usedParams, 12);
            logMessage("sizes: 808 Metal " + juce::String((int) sizeof(Tr808MetalGenerator)) + " bytes, 808 Drums "
                       + juce::String((int) sizeof(Tr808DrumsGenerator)) + ", a voice " + juce::String((int) sizeof(Voice)));
            const std::pair<int, int> keys[] = { { 42, Tr808MetalGenerator::closedHat }, { 44, Tr808MetalGenerator::closedHat },
                                                 { 46, Tr808MetalGenerator::openHat },   { 49, Tr808MetalGenerator::cymbal },
                                                 { 51, Tr808MetalGenerator::cymbal },    { 57, Tr808MetalGenerator::cymbal },
                                                 { 56, Tr808MetalGenerator::cowbell },   { 60, -1 } };
            for (const auto& [note, instrument] : keys)
                expectEquals(Tr808MetalGenerator::instrumentFor(note), instrument, "key " + juce::String(note));
        }

        beginTest("two_hits_in_one_block_both_sound");
        {
            // The cowbell and the cymbal struck in the same block: the kit plays both, the sum of each alone (their
            // circuits are apart; the bank they share runs alike in all three).
            const auto both = playMetal(metalKitOf(), { { 0, 56 }, { 0, 49 } }, 0.4);
            const auto bell = playMetal(metalKitOf(), { { 0, 56 } }, 0.4);
            const auto cymbal = playMetal(metalKitOf(), { { 0, 49 } }, 0.4);
            float worst = 0.0f;
            for (std::size_t i = 0; i < both.size(); ++i)
                worst = std::max(worst, std::abs(both[i] - bell[i] - cymbal[i]));
            logMessage("both against the sum: worst " + juce::String(worst, 7) + " of a peak of " + juce::String(metalPeakOf(both), 4));
            expectGreaterThan(metalPeakOf(bell), 0.01f);
            expectGreaterThan(metalPeakOf(cymbal), 0.01f);
            expectLessThan(worst, 1.0e-4f * metalPeakOf(both));
        }

        beginTest("note_offs_change_nothing");
        {
            const auto held = playMetal(metalKitOf(), { { 0, 46 } }, 0.4);
            const auto tapped = playMetal(metalKitOf(), { { 0, 46 }, { 100, 46, true } }, 0.4);
            expect(held == tapped, "a note-off leaves the hat ringing as it was");
        }

        beginTest("the_closed_hat_cuts_the_open_one");
        {
            // The open hat alone, then struck again with the closed hat 100 ms in: from 200 ms on, the choked open hat
            // is far quieter (the closed hat itself has rung down by then).
            const auto open = playMetal(metalKitOf(), { { 0, 46 } }, 0.5);
            const auto choked = playMetal(metalKitOf(), { { 0, 46 }, { 4800, 42 } }, 0.5);
            const double ratio = 10.0 * std::log10(energyOf(choked, 48000.0, 0.2, 0.4) / std::max(1.0e-30, energyOf(open, 48000.0, 0.2, 0.4)));
            logMessage("the open hat 200 to 400 ms after a closed hat at 100 ms: " + juce::String(ratio, 1) + " dB against alone");
            expectLessThan(ratio, -20.0);
        }

        beginTest("the_voice_ends_once_every_instrument_has_rung_out");
        {
            int activeEarly = 0, activeLate = 0, activeUnmapped = 0;
            (void) playMetal(metalKitOf(), { { 0, 56 }, { 0, 42 } }, 0.05, &activeEarly);
            (void) playMetal(metalKitOf(), { { 0, 56 }, { 0, 42 } }, 3.0, &activeLate);
            (void) playMetal(metalKitOf(), { { 0, 60 } }, 0.5, &activeUnmapped);
            expectEquals(activeEarly, 1, "one voice holds the kit while it rings");
            expectEquals(activeLate, 0, "and lets go once it has rung down");
            expectEquals(activeUnmapped, 0, "a key with no instrument ends at once");
        }

        beginTest("tune_and_cb_pitch_move_the_oscillators");
        {
            SlotMemory memory;
            memory.allocate(Tr808MetalGenerator::memoryFloats(96000.0, 32));
            memory.setSeed(3);
            auto g = std::make_unique<Tr808MetalGenerator>();
            g->prepare(96000.0, 32, memory);
            SlotParams params;
            const auto& d = Tr808MetalGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                params.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
            params.p[Tr808MetalGenerator::tune] = -12.0f;
            params.p[Tr808MetalGenerator::cbPitch] = 2.0f;
            std::array<float, 32> l {}, r {};
            g->process(l.data(), r.data(), 32, params);
            expectWithinAbsoluteError(g->bankForTesting().frequencyOf(5), 205.3 / 2.0, 0.1);
            expectWithinAbsoluteError(g->bankForTesting().frequencyOf(1), 825.0 / 2.0 * std::pow(2.0, 2.0 / 12.0), 0.2);
        }

        beginTest("after_reset_a_hit_is_the_fresh_kits_bit_for_bit");
        {
            // The determinism tests' rule (D224) at the generator: a kit reset and reseeded plays as a fresh one,
            // the oscillators' places included.
            SlotMemory memory;
            memory.allocate(Tr808MetalGenerator::memoryFloats(96000.0, 32));
            memory.setSeed(5);
            auto fresh = std::make_unique<Tr808MetalGenerator>();
            fresh->prepare(96000.0, 32, memory);
            SlotMemory memoryUsed;
            memoryUsed.allocate(Tr808MetalGenerator::memoryFloats(96000.0, 32));
            memoryUsed.setSeed(5);
            auto used = std::make_unique<Tr808MetalGenerator>();
            used->prepare(96000.0, 32, memoryUsed);
            SlotParams params;
            const auto& d = Tr808MetalGenerator::descriptor();
            for (int i = 0; i < d.usedParams; ++i)
                params.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
            std::array<float, 32> l {}, r {};
            NoteContext note;
            note.noteNumber = 49;
            used->noteOn(note);
            for (int b = 0; b < 300; ++b)
                used->process(l.data(), r.data(), 32, params);
            used->reset();
            used->reseed();
            for (auto* g : { fresh.get(), used.get() })
            {
                note.noteNumber = 46;
                g->noteOn(note);
                g->noteChanged(56);
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

        beginTest("a_start_from_silence_takes_its_oscillators_places_from_the_seed");
        {
            const auto a = playMetal(metalKitOf(), { { 0, 42 } }, 0.1, nullptr, 11);
            const auto b = playMetal(metalKitOf(), { { 0, 42 } }, 0.1, nullptr, 11);
            const auto c = playMetal(metalKitOf(), { { 0, 42 } }, 0.1, nullptr, 12);
            expect(a == b, "the same seed, the same hit");
            expect(a != c, "another seed, other places");
            const double ratio = 10.0 * std::log10(energyOf(a, 48000.0, 0.0, 0.1) / std::max(1.0e-30, energyOf(c, 48000.0, 0.0, 0.1)));
            logMessage("the closed hat's energy, one seed against another: " + juce::String(ratio, 2) + " dB");
            expectLessThan(std::abs(ratio), 3.0, "but the same hat");
        }
    }
};

static V3Tr808MetalKitTests v3Tr808MetalKitTests;
