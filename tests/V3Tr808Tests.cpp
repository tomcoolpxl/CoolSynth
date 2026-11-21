#include <juce_audio_formats/juce_audio_formats.h>
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

    /** What the comparison with recordings reads from a hit: its pitch early and late, its decay, its brightness. */
    struct HitMeasures
    {
        double earlyHz = 0.0, tailHz = 0.0, t60 = 0.0, bright = 0.0;
    };

    HitMeasures measureHit(const std::vector<double>& x, double rate)
    {
        HitMeasures m;
        double peak = 0.0;
        for (const double v : x)
            peak = std::max(peak, std::abs(v));
        if (peak <= 0.0)
            return m;
        std::size_t start = 0;
        while (start < x.size() && std::abs(x[start]) < 0.1 * peak)
            ++start;
        const auto at = [&](double seconds) { return std::min(x.size(), start + static_cast<std::size_t>(seconds * rate)); };
        // Peaks over 10 ms; the slope from 60 ms down to -40 dB under it, as the decay table measures.
        const auto window = static_cast<std::size_t>(0.01 * rate);
        std::vector<double> peaks;
        for (std::size_t i = start; i + window <= x.size(); i += window)
        {
            double p = 0.0;
            for (std::size_t j = i; j < i + window; ++j)
                p = std::max(p, std::abs(x[j]));
            peaks.push_back(p);
        }
        if (peaks.size() > 8)
        {
            std::size_t end = 7;
            while (end + 1 < peaks.size() && peaks[end] > peaks[6] * 0.01)
                ++end;
            const double dbPerSecond = 20.0 * std::log10(std::max(1.0e-12, peaks[end]) / peaks[6]) / (static_cast<double>(end - 6) * 0.01);
            m.t60 = dbPerSecond < 0.0 ? -60.0 / dbPerSecond : 0.0;
        }
        const auto frequencyBetween = [&](std::size_t from, std::size_t to)
        {
            std::vector<double> c;
            for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i)
                if (x[i - 1] < 0.0 && x[i] >= 0.0)
                    c.push_back(static_cast<double>(i - 1) - x[i - 1] / (x[i] - x[i - 1]));
            return c.size() < 2 ? 0.0 : rate * static_cast<double>(c.size() - 1) / (c.back() - c.front());
        };
        m.earlyHz = frequencyBetween(at(0.02), at(0.06));
        // The tail: from 120 ms for a quarter second, or until it is 50 dB down.
        std::size_t tailEnd = at(0.37);
        for (std::size_t k = 12; k < peaks.size() && k < 37; ++k)
            if (peaks[k] < peak * 0.00316)
            {
                tailEnd = at(0.01 * static_cast<double>(k));
                break;
            }
        m.tailHz = frequencyBetween(at(0.12), tailEnd);
        // Brightness: the share above about 500 Hz in the first 20 ms (a one-pole high-pass).
        const double a = std::exp(-2.0 * 3.141592653589793 * 500.0 / rate);
        double hp = 0.0, lastX = 0.0, high = 0.0, all = 0.0;
        for (std::size_t i = start; i < at(0.02); ++i)
        {
            hp = a * (hp + x[i] - lastX);
            lastX = x[i];
            high += hp * hp;
            all += x[i] * x[i];
        }
        m.bright = all > 0.0 ? std::sqrt(high / all) : 0.0;
        return m;
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
            if (const auto dump = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_DUMP", {}); dump.isNotEmpty())
            {
                // The model's stages sample by sample, to set beside the SPICE render (D251).
                auto again = std::make_unique<Tr808BassDrum>();
                again->prepare(ref.rate);
                again->setDecay(0.4);
                again->setTone(0.99);
                again->setLevel(0.99);
                again->settle();
                again->trigger(4.0);
                juce::String text = "vin,npulse,neg,nout,vout\n";
                for (int i = 0; i < 2000; ++i)
                {
                    const double v = again->process();
                    text << juce::String(again->triggerNodeVolts(), 6) << "," << juce::String(again->pulseVolts(), 6) << ","
                         << juce::String(again->envelopeVolts(), 6) << "," << juce::String(again->resonatorVolts(), 6) << ","
                         << juce::String(v, 6) << "\n";
                }
                juce::File(dump).replaceWithText(text);
            }
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

        beginTest("after_rest_a_hit_is_the_fresh_drums_bit_for_bit");
        {
            // The determinism tests' rule (D224) at the drum: a hit after rest() is a fresh drum's hit.
            auto fresh = std::make_unique<Tr808BassDrum>(), used = std::make_unique<Tr808BassDrum>();
            for (auto* d : { fresh.get(), used.get() })
            {
                d->prepare(96000.0);
                d->setDecay(0.4);
            }
            used->trigger(9.0);
            for (int i = 0; i < 20000; ++i)
                used->process();
            used->rest();
            fresh->trigger(4.0);
            used->trigger(4.0);
            int first = -1;
            for (int i = 0; i < 48000 && first < 0; ++i)
                if (fresh->process() != used->process())
                    first = i;
            expectEquals(first, -1, "the first sample that differs");
        }

        beginTest("newton_stays_short_under_the_hardest_settings");
        {
            // A wide trigger (Click full), the strongest accent, the longest decay: the solver's iterations a sample.
            auto drum = std::make_unique<Tr808BassDrum>();
            drum->prepare(96000.0);
            drum->setDecay(1.48);
            drum->setRails(4.0);
            drum->settle();
            drum->trigger(14.0, 0.01);
            long long total = 0;
            int worst = 0, atLimit = 0, failed = 0;
            const int samples = 96000;
            for (int i = 0; i < samples; ++i)
            {
                drum->process();
                const int n = drum->newtonSteps();
                total += n;
                worst = std::max(worst, n);
                atLimit += n >= 12 ? 1 : 0;
                failed += drum->lastSolveConverged() ? 0 : 1;
                if (i == 48000)
                    drum->trigger(14.0, 0.01); // and a hit on the ringing drum
            }
            logMessage("Newton: " + juce::String(static_cast<double>(total) / samples, 3) + " steps a sample on average, at most "
                       + juce::String(worst) + ", " + juce::String(atLimit) + " samples over 12 steps, " + juce::String(failed) + " unsolved");
            // The trigger's edges, in the front and in the envelope generator, may take more (D251); nothing else does.
            expectLessOrEqual(atLimit, 6, "only the two hits' edges take more than 12 steps");
            expectEquals(failed, 0, "every sample solves");
        }

        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_PROFILE", {}).isNotEmpty())
        {
            beginTest("profile");
            // The cost report's hardest: Click full (10 ms), accent full, the longest decay, Shape full (4 V rails).
            auto drum = std::make_unique<Tr808BassDrum>();
            drum->prepare(96000.0);
            drum->setDecay(1.48);
            drum->setRails(4.0);
            drum->setTone(0.5);
            drum->setGlide(juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_PROFILE", {}).getDoubleValue()); // the glide's size
            drum->rest();
            drum->trigger(14.0, 0.01);
            const int window = 960; // 10 ms
            juce::String report;
            double sink = 0.0;
            for (int w = 0; w < 300; ++w)
            {
                long long frontSteps = 0, resonatorSteps = 0;
                int frontRunning = 0;
                const auto start = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < window; ++i)
                {
                    sink += drum->process();
                    frontSteps += drum->frontNewtonSteps();
                    resonatorSteps += drum->resonatorNewtonSteps();
                    frontRunning += drum->frontIsResting() ? 0 : 1;
                }
                const double seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
                if (w < 8 || w % 25 == 0)
                    report << "\n  " << juce::String(w * 10) << " ms: " << juce::String(100.0 * seconds / 0.01, 2) << " % of real time, front on "
                           << juce::String(frontRunning) << " (" << juce::String(static_cast<double>(frontSteps) / window, 2) << " steps), resonator "
                           << juce::String(static_cast<double>(resonatorSteps) / window, 2) << " steps";
            }
            logMessage("profile:" + report + "\n  (" + juce::String(sink, 1) + ")");
        }

        if (const auto fischer = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_FISCHER", {}); fischer.isNotEmpty())
        {
            // Michael Fischer's 1994 recordings of one 808 (analysis only, never shipped: docs/research/kick-808-wdf.md),
            // named BD<tone><decay> at the knobs' marks 0, 2.5, 5, 7.5 and 10, against the model at the same marks
            // (Decay linear, as VR6 is a B-taper pot; Tone through VR5's C taper), unaccented. The folder holds the BD*.WAV files.
            beginTest("against_fischers_recordings");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const juce::StringArray marks { "00", "25", "50", "75", "10" };
            juce::String table;
            for (int t = 0; t < 5; ++t)
                for (int d = 0; d < 5; ++d)
                {
                    const auto file = juce::File(fischer).getChildFile("BD" + marks[t] + marks[d] + ".WAV");
                    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                    if (reader == nullptr)
                        continue;
                    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
                    std::vector<double> recorded(static_cast<std::size_t>(buffer.getNumSamples()));
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        recorded[static_cast<std::size_t>(i)] = buffer.getSample(0, i);
                    const auto real = measureHit(recorded, reader->sampleRate);

                    auto drum = std::make_unique<Tr808BassDrum>();
                    drum->prepare(96000.0);
                    drum->setTone(Tr808BassDrum::toneForKnob(0.25 * t));
                    drum->setDecay(0.25 * d);
                    drum->rest();
                    drum->trigger(4.0);
                    std::vector<double> modelled(static_cast<std::size_t>(2.5 * 96000.0));
                    for (auto& v : modelled)
                        v = drum->process();
                    const auto model = measureHit(modelled, 96000.0);
                    table << "\n  tone " << juce::String(2.5 * t, 1) << ", decay " << juce::String(2.5 * d, 1) << ": early "
                          << juce::String(real.earlyHz, 1) << "/" << juce::String(model.earlyHz, 1) << " Hz, tail "
                          << juce::String(real.tailHz, 1) << "/" << juce::String(model.tailHz, 1) << " Hz, T60 "
                          << juce::String(real.t60, 2) << "/" << juce::String(model.t60, 2) << " s, bright "
                          << juce::String(real.bright, 3) << "/" << juce::String(model.bright, 3);
                }
            logMessage("recorded/model:" + table);
        }

        if (const auto jobs = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_JOBS", {}); jobs.isNotEmpty())
        {
            // Hits of the bare circuit written as WAV files, for fitting against recordings (D251). Each line of the
            // file: out.wav tone decay volts tuning c39scale glideFarads width seconds rate [c38scale]
            beginTest("render_jobs");
            juce::StringArray lines;
            juce::File(jobs).readLines(lines);
            int written = 0;
            for (const auto& line : lines)
            {
                const auto f = juce::StringArray::fromTokens(line, " ", "\"");
                if (f.size() < 10)
                    continue;
                const double rate = f[9].getDoubleValue();
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->prepare(rate);
                drum->setTone(f[1].getDoubleValue());
                drum->setDecay(f[2].getDoubleValue());
                drum->setTuning(f[4].getDoubleValue());
                drum->setRetriggerScale(f[5].getDoubleValue());
                drum->setGlide(f[6].getDoubleValue());
                if (f.size() > 10)
                    drum->setEnvelopeScale(f[10].getDoubleValue());
                drum->setLevel(0.99);
                drum->rest();
                drum->trigger(f[3].getDoubleValue(), f[7].getDoubleValue());
                const int n = static_cast<int>(f[8].getDoubleValue() * rate);
                juce::AudioBuffer<float> buffer(1, n);
                for (int i = 0; i < n; ++i)
                    buffer.setSample(0, i, static_cast<float>(drum->process() * 0.1));
                const juce::File out(f[0]);
                out.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(out);
                if (auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32)
                                                                  .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
                {
                    writer->writeFromAudioSampleBuffer(buffer, 0, n);
                    ++written;
                }
            }
            logMessage("rendered " + juce::String(written) + " hits");
        }

        if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH_808_DECAY_TABLE", {}).isNotEmpty())
        {
            beginTest("decay_table");
            juce::String table;
            std::vector<double> settings { 0.0, 0.01, 0.02, 0.03, 0.04 };
            for (int step = 1; step <= 29; ++step)
                settings.push_back(0.05 * step);
            for (const double d : settings)
            {
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->prepare(96000.0);
                drum->setDecay(d);
                drum->settle();
                drum->trigger(4.0);
                // Peaks over 10 ms; the slope from 60 ms down to -40 dB under it (or 4 s).
                const int window = static_cast<int>(0.01 * 96000.0);
                std::vector<double> peaks;
                for (int w = 0; w < 400; ++w)
                {
                    double peak = 0.0;
                    for (int i = 0; i < window; ++i)
                        peak = std::max(peak, std::abs(drum->process()));
                    peaks.push_back(peak);
                }
                const double start = peaks[6];
                std::size_t end = 7;
                while (end + 1 < peaks.size() && peaks[end] > start * 0.01)
                    ++end;
                const double dbPerSecond = 20.0 * std::log10(peaks[end] / start) / (static_cast<double>(end - 6) * 0.01);
                // and directly: until the peaks stay 60 dB under the hit's top
                const double top = *std::max_element(peaks.begin(), peaks.end());
                std::size_t last = 0;
                for (std::size_t w = 0; w < peaks.size(); ++w)
                    if (peaks[w] > top * 0.001)
                        last = w + 1;
                table << juce::String(d, 2) << ":" << juce::String(-60.0 / dbPerSecond, 3) << "/" << juce::String(0.01 * static_cast<double>(last), 2) << " ";
            }
            logMessage("decay -> T60 s: " + table);
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
            // From the ngspice renders (tools/kick808, C39 at its fitted 1.2): the pitch by upward zero crossings settles
            // from about 59 Hz to 49.4 Hz, slower when accented; with the glide bend at size 1 it comes down from 100 Hz
            // in some 120 ms; the tail falls 84.4 dB/s at Decay 0.4 and 33.3 dB/s at 0.8.
            struct Case { double volts, decay, glide, at, hz, within; };
            for (const auto& c : { Case { 4.0, 0.4, 0.0, 0.0142, 59.11, 0.25 }, Case { 4.0, 0.4, 0.0, 0.0501, 52.56, 0.25 },
                                   Case { 4.0, 0.4, 0.0, 0.1503, 49.39, 0.25 }, Case { 14.0, 0.4, 0.0, 0.0503, 55.67, 0.25 },
                                   Case { 14.0, 0.4, 0.0, 0.1065, 51.09, 0.25 }, Case { 14.0, 0.4, 0.0, 0.2071, 49.41, 0.25 },
                                   Case { 4.0, 0.4, 1.0, 0.0214, 85.79, 1.0 }, Case { 4.0, 0.4, 1.0, 0.0620, 63.01, 1.0 },
                                   Case { 4.0, 0.4, 1.0, 0.1174, 49.90, 0.5 } })
            {
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->prepare(96000.0);
                drum->setDecay(c.decay);
                drum->setGlide(c.glide);
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
                expectWithinAbsoluteError(heard, c.hz, c.within, juce::String(c.volts) + " V, glide " + juce::String(c.glide) + ", at "
                                                                      + juce::String(c.at * 1000.0, 0) + " ms");
            }
            for (const auto& [decay, dbPerSecond] : { std::pair { 0.4, -84.4 }, std::pair { 0.8, -33.3 } })
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
