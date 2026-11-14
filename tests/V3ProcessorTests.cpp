#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include "synth/v3/Envelope.h"
#include "synth/v3/LadderProcessor.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Mode = LadderProcessor::Mode;

    constexpr int kSubBlock = 32;
    constexpr double kRate = 48000.0;

    std::vector<float> sine(float hz, float amplitude, int samples)
    {
        std::vector<float> x(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
            x[static_cast<std::size_t>(i)] = amplitude * std::sin(static_cast<float>(i) * 6.2831853f * hz / static_cast<float>(kRate));
        return x;
    }

    SlotParams ladderParams(float cutoffHz, float res, Mode mode = Mode::lowpass24, float driveDb = 0.0f, float bass = 0.5f,
                            float key = 0.0f, float noteHz = LadderProcessor::keyReferenceHz)
    {
        SlotParams p;
        p.p[LadderProcessor::cutoff] = cutoffHz;
        p.p[LadderProcessor::resonance] = res;
        p.p[LadderProcessor::drive] = driveDb;
        p.p[LadderProcessor::keyTrack] = key;
        p.p[LadderProcessor::mode] = static_cast<float>(mode);
        p.p[LadderProcessor::bass] = bass;
        p.frequencyHz = noteHz;
        return p;
    }

    std::vector<float> runLadder(const std::vector<float>& input, const SlotParams& params)
    {
        SlotMemory memory;
        memory.allocate(0);
        auto ladder = std::make_unique<LadderProcessor>();
        ladder->prepare(kRate, kSubBlock, memory);
        std::vector<float> left(input), right(input);
        for (std::size_t start = 0; start < left.size(); start += kSubBlock)
        {
            const int n = static_cast<int>(std::min<std::size_t>(kSubBlock, left.size() - start));
            ladder->process(left.data() + start, right.data() + start, n, params);
        }
        return left;
    }

    float rms(const std::vector<float>& x, std::size_t skip)
    {
        double sum = 0.0;
        for (std::size_t i = skip; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return static_cast<float>(std::sqrt(sum / static_cast<double>(x.size() - skip)));
    }

    float gainDb(const std::vector<float>& out, const std::vector<float>& in, std::size_t skip)
    {
        return 20.0f * std::log10(rms(out, skip) / rms(in, skip));
    }

    float peakAbs(const std::vector<float>& x)
    {
        float peak = 0.0f;
        for (auto v : x)
            peak = std::max(peak, std::abs(v));
        return peak;
    }

    double measureFrequency(const std::vector<float>& x, std::size_t skip)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(skip, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
    }

    constexpr int kFftOrder = 13;
    constexpr int kFftSize = 1 << kFftOrder;

    float peakNearHz(const std::vector<float>& x, std::size_t skip, double hz)
    {
        std::vector<float> buffer(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            buffer[static_cast<std::size_t>(i)] = x[skip + static_cast<std::size_t>(i)];
        juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::blackmanHarris);
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(kFftSize));
        juce::dsp::FFT fft(kFftOrder);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        const int bin = static_cast<int>(std::round(hz * kFftSize / kRate));
        float peak = 0.0f;
        for (int b = std::max(1, bin - 4); b <= bin + 4; ++b)
            peak = std::max(peak, buffer[static_cast<std::size_t>(b)]);
        return peak;
    }

    std::unique_ptr<Envelope> makeEnvelope(const Envelope::Parameters& parameters)
    {
        auto envelope = std::make_unique<Envelope>();
        envelope->setSampleRate(kRate);
        envelope->setParameters(parameters);
        return envelope;
    }

    /** Samples until the stage changes away from `stage`, counting the sample that ends it. */
    int samplesInStage(Envelope& envelope, Envelope::Stage stage, int limit = 480000)
    {
        int samples = 0;
        while (envelope.getStage() == stage && samples < limit)
        {
            envelope.getNextSample();
            ++samples;
        }
        return samples;
    }
}

class V3LadderTests final : public juce::UnitTest
{
public:
    V3LadderTests() : juce::UnitTest("V3 ladder", "V3") {}

    void runTest() override
    {
        beginTest("descriptor_and_four_pole_slope");
        {
            expect(juce::String(LadderProcessor::descriptor().name) == "Ladder");
            expectEquals(LadderProcessor::descriptor().usedParams, static_cast<int>(LadderProcessor::paramCount));
            const auto input = sine(440.0f, 0.1f, 8192);
            const float near = gainDb(runLadder(input, ladderParams(220.0f, 0.0f)), input, 4096);
            const float far = gainDb(runLadder(input, ladderParams(110.0f, 0.0f)), input, 4096);
            logMessage("LP24 one octave above cutoff " + juce::String(near, 1) + " dB, two octaves " + juce::String(far, 1) + " dB");
            expect(near - far > 18.0f);
        }

        beginTest("passband_is_flat");
        {
            const auto input = sine(440.0f, 0.1f, 8192);
            const float db = gainDb(runLadder(input, ladderParams(8000.0f, 0.0f)), input, 4096);
            logMessage("440 Hz through cutoff 8 kHz: " + juce::String(db, 2) + " dB");
            expect(std::abs(db) < 0.5f);
        }

        beginTest("modes_have_their_slopes");
        {
            const auto input440 = sine(440.0f, 0.1f, 8192);
            const float lp12Near = gainDb(runLadder(input440, ladderParams(220.0f, 0.0f, Mode::lowpass12)), input440, 4096);
            const float lp12Far = gainDb(runLadder(input440, ladderParams(110.0f, 0.0f, Mode::lowpass12)), input440, 4096);
            logMessage("LP12 one octave above " + juce::String(lp12Near, 1) + " dB, two octaves " + juce::String(lp12Far, 1) + " dB");
            expect(lp12Near - lp12Far > 8.0f && lp12Near - lp12Far < 14.0f);

            const auto high = sine(8000.0f, 0.1f, 8192);
            const auto low = sine(110.0f, 0.1f, 8192);
            const float hpPass = gainDb(runLadder(high, ladderParams(1000.0f, 0.0f, Mode::highpass12)), high, 4096);
            const float hpStop = gainDb(runLadder(low, ladderParams(1000.0f, 0.0f, Mode::highpass12)), low, 4096);
            logMessage("HP12 at 1 kHz: 8 kHz " + juce::String(hpPass, 1) + " dB, 110 Hz " + juce::String(hpStop, 1) + " dB");
            expect(std::abs(hpPass) < 1.0f);
            expect(hpStop < -30.0f);

            const auto atCutoff = sine(1000.0f, 0.1f, 8192);
            const auto decade = sine(10000.0f, 0.1f, 8192);
            const float bpPeak = gainDb(runLadder(atCutoff, ladderParams(1000.0f, 0.0f, Mode::bandpass)), atCutoff, 4096);
            const float bpFar = gainDb(runLadder(decade, ladderParams(1000.0f, 0.0f, Mode::bandpass)), decade, 4096);
            logMessage("BP at 1 kHz: centre " + juce::String(bpPeak, 1) + " dB, 10 kHz " + juce::String(bpFar, 1) + " dB");
            expect(std::abs(bpPeak) < 1.0f);
            expect(bpFar < -12.0f);
        }

        beginTest("self_oscillates_from_silence_at_the_cutoff");
        {
            const std::vector<float> silence(96000, 0.0f);
            const auto output = runLadder(silence, ladderParams(1000.0f, 1.0f));
            std::size_t onset = 0;
            while (onset < output.size() && std::abs(output[onset]) < 0.01f)
                ++onset;
            const double hz = measureFrequency(output, 48000);
            const float level = rms(output, 48000);
            logMessage("self-oscillation from the noise floor: reaches 0.01 after " + juce::String(static_cast<double>(onset) / kRate * 1000.0, 1)
                       + " ms, " + juce::String(hz, 1) + " Hz, rms " + juce::String(level, 3) + ", peak " + juce::String(peakAbs(output), 3));
            expect(onset < 24000);
            expect(level > 0.05f);
            expect(peakAbs(output) < 2.0f);

            for (float cutoffHz : { 250.0f, 1000.0f, 4000.0f })
            {
                const auto sung = runLadder(silence, ladderParams(cutoffHz, 1.0f));
                const double cents = 1200.0 * std::log2(measureFrequency(sung, 48000) / static_cast<double>(cutoffHz));
                logMessage("self-oscillation at cutoff " + juce::String(cutoffHz, 0) + " Hz is " + juce::String(cents, 1) + " cents off");
                expect(std::abs(cents) < 25.0);
            }
        }

        beginTest("key_tracking_raises_the_cutoff_with_the_note");
        {
            const auto input = sine(2000.0f, 0.1f, 8192);
            const float twoOctavesUp = LadderProcessor::keyReferenceHz * 4.0f;
            const float tracked = gainDb(runLadder(input, ladderParams(500.0f, 0.0f, Mode::lowpass24, 0.0f, 0.5f, 1.0f, twoOctavesUp)), input, 4096);
            const float fixed = gainDb(runLadder(input, ladderParams(500.0f, 0.0f, Mode::lowpass24, 0.0f, 0.5f, 0.0f, twoOctavesUp)), input, 4096);
            expect(tracked - fixed > 12.0f);
        }

        beginTest("drive_adds_harmonics_and_keeps_the_peak");
        {
            const auto input = sine(234.375f, 0.5f, 4096 + kFftSize); // bin 40; its third harmonic sits on bin 120
            const auto clean = runLadder(input, ladderParams(20000.0f, 0.0f, Mode::lowpass24, 0.0f));
            const auto driven = runLadder(input, ladderParams(20000.0f, 0.0f, Mode::lowpass24, 24.0f));
            const float cleanThird = peakNearHz(clean, 4096, 703.125) / peakNearHz(clean, 4096, 234.375);
            const float drivenThird = peakNearHz(driven, 4096, 703.125) / peakNearHz(driven, 4096, 234.375);
            logMessage("third harmonic: clean " + juce::String(20.0f * std::log10(cleanThird), 1)
                       + " dB, driven " + juce::String(20.0f * std::log10(drivenThird), 1) + " dB, driven peak " + juce::String(peakAbs(driven), 3));
            expect(20.0f * std::log10(cleanThird) < -45.0f);
            expect(20.0f * std::log10(drivenThird) > -25.0f);
            expect(peakAbs(driven) < 1.1f);
        }

        beginTest("bass_restores_the_low_end_lost_to_resonance");
        {
            const auto input = sine(50.0f, 0.05f, 24000);
            const float thin = gainDb(runLadder(input, ladderParams(2000.0f, 0.8f, Mode::lowpass24, 0.0f, 0.0f)), input, 12000);
            const float full = gainDb(runLadder(input, ladderParams(2000.0f, 0.8f, Mode::lowpass24, 0.0f, 1.0f)), input, 12000);
            logMessage("50 Hz under a resonant ladder: bass 0 " + juce::String(thin, 1) + " dB, bass 1 " + juce::String(full, 1) + " dB");
            expect(thin < -10.0f);
            expect(std::abs(full) < 1.5f);
        }

        beginTest("extreme_settings_and_sweeps_stay_finite_and_bounded");
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            std::vector<float> noise(48000);
            for (auto& v : noise)
                v = dist(rng);

            for (auto mode : { Mode::lowpass24, Mode::lowpass12, Mode::bandpass, Mode::highpass12 })
            {
                const auto output = runLadder(noise, ladderParams(20000.0f, 1.0f, mode, 24.0f, 1.0f, 1.0f, 8000.0f));
                bool finite = true;
                for (auto v : output)
                    finite = finite && std::isfinite(v);
                expect(finite);
                const auto selfOscillating = runLadder(std::vector<float>(96000, 0.0f), ladderParams(1000.0f, 1.0f, mode));
                logMessage("mode " + juce::String(static_cast<int>(mode)) + ": extreme noise peak " + juce::String(peakAbs(output), 2)
                           + ", self-oscillation at 1 kHz rms " + juce::String(rms(selfOscillating, 48000), 3));
                expect(peakAbs(output) < 8.0f, "mode " + juce::String(static_cast<int>(mode)) + " peak " + juce::String(peakAbs(output), 2));
            }

            SlotMemory memory;
            memory.allocate(0);
            auto ladder = std::make_unique<LadderProcessor>();
            ladder->prepare(kRate, kSubBlock, memory);
            std::vector<float> left(noise), right(noise);
            float peak = 0.0f;
            bool finite = true;
            for (std::size_t start = 0, block = 0; start + kSubBlock <= left.size(); start += kSubBlock, ++block)
            {
                const float sweep = 0.5f + 0.5f * std::sin(static_cast<float>(block) * 0.05f);
                ladder->process(left.data() + start, right.data() + start, kSubBlock, ladderParams(20.0f * std::pow(1000.0f, sweep), 0.95f));
                for (std::size_t i = start; i < start + kSubBlock; ++i)
                {
                    finite = finite && std::isfinite(left[i]);
                    peak = std::max(peak, std::abs(left[i]));
                }
            }
            expect(finite);
            expect(peak < 8.0f, "sweep peak " + juce::String(peak, 2));
        }

        beginTest("generator_3_shakes_the_cutoff_at_audio_rate");
        {
            // A 2 kHz tone into a ladder closed to 1 kHz, and a 110 Hz tone at full level as generator 3's signal.
            const int total = 48000;
            const auto input = sine(2000.0f, 0.05f, total);
            const auto shaker = sine(110.0f, 1.0f, total);
            auto run = [&](float octaves, bool withShaker, float res = 0.0f, const std::vector<float>* source = nullptr)
            {
                SlotMemory memory;
                memory.allocate(0);
                auto ladder = std::make_unique<LadderProcessor>();
                ladder->prepare(kRate, kSubBlock, memory);
                std::vector<float> left(source != nullptr ? *source : input), right(left);
                for (std::size_t start = 0; start + kSubBlock <= left.size(); start += kSubBlock)
                {
                    auto params = ladderParams(1000.0f, res);
                    params.p[LadderProcessor::fm] = octaves;
                    params.modulator = withShaker ? shaker.data() + start : nullptr;
                    ladder->process(left.data() + start, right.data() + start, kSubBlock, params);
                }
                return left;
            };
            auto toneLevel = [&](const std::vector<float>& x, float hz)
            {
                double re = 0.0, im = 0.0;
                for (std::size_t i = 24000; i < x.size(); ++i)
                {
                    const double phase = static_cast<double>(i) * 6.283185307179586 * hz / kRate;
                    re += x[i] * std::cos(phase);
                    im += x[i] * std::sin(phase);
                }
                return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size() - 24000) + 1.0e-12);
            };

            // At FM 0 the signal of generator 3 changes nothing, and without that signal neither does FM.
            const auto plain = run(0.0f, false);
            const auto unused = run(0.0f, true);
            const auto unfed = run(2.0f, false);
            bool same = true;
            for (std::size_t i = 0; i < plain.size(); ++i)
                same = same && plain[i] == unused[i] && plain[i] == unfed[i];
            expect(same, "FM at 0, or no generator 3 signal: the filter is as it was");

            // Shaken by two octaves the 2 kHz tone gets side tones 110 Hz either side, which it did not have,
            // and comes through louder: the cutoff is above it half the time.
            const auto shaken = run(2.0f, true);
            logMessage("2 kHz through a 1 kHz ladder: plain " + juce::String(toneLevel(plain, 2000.0f), 1) + " dB, shaken "
                       + juce::String(toneLevel(shaken, 2000.0f), 1) + " dB; side tone at 2110 Hz plain "
                       + juce::String(toneLevel(plain, 2110.0f), 1) + " dB, shaken " + juce::String(toneLevel(shaken, 2110.0f), 1) + " dB");
            expectGreaterThan(toneLevel(shaken, 2110.0f), toneLevel(plain, 2110.0f) + 30.0);
            expectGreaterThan(toneLevel(shaken, 1890.0f), toneLevel(plain, 1890.0f) + 30.0);
            expectGreaterThan(toneLevel(shaken, 2000.0f), toneLevel(plain, 2000.0f) + 3.0);

            // Full swing, full resonance, noise: finite and bounded.
            std::mt19937 rng(11);
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            std::vector<float> noise(static_cast<std::size_t>(total));
            for (auto& v : noise)
                v = dist(rng);
            const auto wild = run(3.0f, true, 1.0f, &noise);
            bool finite = true;
            for (auto v : wild)
                finite = finite && std::isfinite(v);
            expect(finite);
            expect(peakAbs(wild) < 8.0f, "shaken at full: peak " + juce::String(peakAbs(wild), 2));

            const auto& d = LadderProcessor::descriptor().params[LadderProcessor::fm];
            expectEquals(juce::String(d.id), juce::String("fm"));
            expectEquals(d.def, 0.0f);
            expectEquals(d.max, 3.0f);
        }
    }
};

class V3EnvelopeTests final : public juce::UnitTest
{
public:
    V3EnvelopeTests() : juce::UnitTest("V3 envelope", "V3") {}

    void runTest() override
    {
        beginTest("a_leak_lets_a_share_of_the_input_round_the_ladder");
        {
            // A 6 kHz tone into a ladder closed to 200 Hz: next to nothing comes through, unless some of the input
            // leaks round the filter, and then exactly that share.
            const auto input = sine(6000.0f, 0.5f, 24000);
            auto level = [&](float leakAmount)
            {
                auto params = ladderParams(200.0f, 0.0f);
                params.p[LadderProcessor::leak] = leakAmount;
                const auto out = runLadder(input, params);
                double sum = 0.0;
                for (std::size_t i = 12000; i < out.size(); ++i)
                    sum += static_cast<double>(out[i]) * out[i];
                return std::sqrt(sum / static_cast<double>(out.size() - 12000));
            };
            const double full = 0.5 / std::sqrt(2.0);
            expectLessThan(level(0.0f), full * 1.0e-3);
            expectWithinAbsoluteError(level(0.05f), full * 0.05, full * 0.002);
            expectWithinAbsoluteError(level(0.2f), full * 0.2, full * 0.004);
        }

        beginTest("every_segment_lasts_its_programmed_time");
        {
            for (float curve : { -1.0f, 0.0f, 1.0f })
            {
                Envelope::Parameters p;
                p.attack = 0.010f;
                p.decay = 0.020f;
                p.sustain = 0.5f;
                p.release = 0.030f;
                p.attackCurve = p.decayCurve = p.releaseCurve = curve;
                auto envelope = makeEnvelope(p);
                envelope->noteOn(1.0f);
                expectEquals(samplesInStage(*envelope, Envelope::Stage::attack), 480);
                expectEquals(envelope->getValue(), 1.0f);
                expectEquals(samplesInStage(*envelope, Envelope::Stage::decay), 960);
                expectEquals(envelope->getValue(), 0.5f);
                envelope->noteOff();
                expectEquals(samplesInStage(*envelope, Envelope::Stage::release), 1440);
                expectEquals(envelope->getValue(), 0.0f);
                expect(! envelope->isActive());
            }
        }

        beginTest("curve_sign_bends_the_segment");
        {
            auto valueAtHalfAttack = [](float curve)
            {
                Envelope::Parameters p;
                p.attack = 0.010f;
                p.attackCurve = curve;
                auto envelope = makeEnvelope(p);
                envelope->noteOn(1.0f);
                return envelope->advance(240);
            };
            const float fast = valueAtHalfAttack(-1.0f);
            const float straight = valueAtHalfAttack(0.0f);
            const float slow = valueAtHalfAttack(1.0f);
            logMessage("attack at half time: curve -1 " + juce::String(fast, 3) + ", 0 " + juce::String(straight, 3) + ", +1 " + juce::String(slow, 3));
            expect(fast > 0.9f);
            expectWithinAbsoluteError(straight, 0.5f, 1.0e-4f);
            expect(slow < 0.1f);
        }

        beginTest("delay_and_hold_last_their_times");
        {
            Envelope::Parameters p;
            p.delay = 0.005f;
            p.attack = 0.002f;
            p.hold = 0.010f;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f);
            expect(envelope->getStage() == Envelope::Stage::delay);
            expectEquals(samplesInStage(*envelope, Envelope::Stage::delay), 240);
            expectEquals(samplesInStage(*envelope, Envelope::Stage::attack), 96);
            expectEquals(samplesInStage(*envelope, Envelope::Stage::hold), 480);
            expect(envelope->getStage() == Envelope::Stage::decay);
        }

        beginTest("velocity_sets_peak_and_sustain");
        {
            Envelope::Parameters p;
            p.attack = 0.002f;
            p.decay = 0.010f;
            p.sustain = 0.5f;
            p.velocitySensitivity = 1.0f;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(0.6f);
            samplesInStage(*envelope, Envelope::Stage::attack);
            expectWithinAbsoluteError(envelope->getValue(), 0.6f, 1.0e-6f);
            samplesInStage(*envelope, Envelope::Stage::decay);
            expectWithinAbsoluteError(envelope->getValue(), 0.3f, 1.0e-6f);
        }

        beginTest("retrigger_never_jumps_even_with_a_new_velocity");
        {
            Envelope::Parameters p;
            p.attack = 0.020f;
            p.decay = 0.050f;
            p.sustain = 0.7f;
            p.release = 0.100f;
            p.velocitySensitivity = 1.0f;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f);
            float value = envelope->advance(2400);
            envelope->noteOff();
            value = envelope->advance(480);
            float largestStep = 0.0f;
            envelope->noteOn(0.3f); // softer than the level it is at
            float previous = value;
            for (int i = 0; i < 9600; ++i)
            {
                const float v = envelope->getNextSample();
                largestStep = std::max(largestStep, std::abs(v - previous));
                previous = v;
            }
            logMessage("largest step after a softer retrigger: " + juce::String(largestStep, 5));
            expect(largestStep < 0.01f);
            expectWithinAbsoluteError(envelope->getValue(), 0.21f, 1.0e-3f); // sustain of the new velocity
        }

        beginTest("loop_cycles_while_held");
        {
            Envelope::Parameters p;
            p.attack = 0.010f;
            p.hold = 0.005f;
            p.decay = 0.010f;
            p.sustain = 0.0f;
            p.loop = true;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f);
            std::vector<int> peaks;
            bool armed = true;
            for (int i = 0; i < 48000; ++i)
            {
                const float v = envelope->getNextSample();
                if (armed && v >= 1.0f)
                {
                    peaks.push_back(i);
                    armed = false;
                }
                if (v < 0.5f)
                    armed = true;
            }
            expect(peaks.size() >= 30);
            for (std::size_t i = 1; i < peaks.size(); ++i)
                expect(std::abs(peaks[i] - peaks[i - 1] - 1200) <= 1, "loop period " + juce::String(peaks[i] - peaks[i - 1]));
            envelope->noteOff();
            envelope->advance(48000);
            expect(! envelope->isActive());
        }

        beginTest("one_shot_runs_its_decay_whatever_the_gate_does");
        {
            Envelope::Parameters p;
            p.attack = 0.005f;
            p.decay = 0.010f;
            p.sustain = 0.5f;
            p.release = 0.010f;
            p.oneShot = true;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f);
            envelope->advance(300);
            envelope->noteOff();
            expect(envelope->getStage() == Envelope::Stage::decay);

            auto held = makeEnvelope(p);
            held->noteOn(1.0f);
            int samples = 0;
            while (held->isActive() && samples < 48000)
            {
                held->getNextSample();
                ++samples;
            }
            expectEquals(samples, 240 + 480 + 480); // attack, decay, release, with the gate still held

            // A preset change turns one-shot off while a released one-shot is still decaying: the
            // envelope must still let go at the end of its decay instead of waiting at sustain.
            auto switched = makeEnvelope(p);
            switched->noteOn(1.0f);
            switched->advance(300);
            switched->noteOff();
            auto plain = p;
            plain.oneShot = false;
            plain.sustain = 1.0f;
            switched->setParameters(plain);
            switched->advance(48000);
            expect(! switched->isActive(), "a released one-shot hung at sustain after one-shot was turned off");
        }

        beginTest("key_time_tracking_halves_times_an_octave_up");
        {
            Envelope::Parameters p;
            p.attack = 0.010f;
            p.keyTimeTracking = 1.0f;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f, 1.0f);
            expectEquals(samplesInStage(*envelope, Envelope::Stage::attack), 240);
        }

        beginTest("sustain_changes_are_smoothed");
        {
            Envelope::Parameters p;
            p.attack = 0.001f;
            p.decay = 0.001f;
            p.sustain = 0.8f;
            auto envelope = makeEnvelope(p);
            envelope->noteOn(1.0f);
            envelope->advance(480);
            p.sustain = 0.2f;
            envelope->setParameters(p);
            float previous = envelope->getValue();
            float largestStep = 0.0f;
            for (int i = 0; i < 4800; ++i)
            {
                const float v = envelope->getNextSample();
                largestStep = std::max(largestStep, std::abs(v - previous));
                previous = v;
            }
            expect(largestStep < 0.01f);
            expectWithinAbsoluteError(envelope->getValue(), 0.2f, 1.0e-3f);
        }
    }
};

static V3LadderTests v3LadderTests;
static V3EnvelopeTests v3EnvelopeTests;
