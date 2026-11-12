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

    constexpr int kSubBlock = 32;
    constexpr double kRate = 48000.0;

    std::vector<float> sine(float hz, float amplitude, int samples)
    {
        std::vector<float> x(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
            x[static_cast<std::size_t>(i)] = amplitude * std::sin(static_cast<float>(i) * 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(kRate));
        return x;
    }

    SlotParams ladderParams(float cutoffHz, float res, float driveDb = 0.0f, float key = 0.0f, float noteHz = LadderProcessor::keyReferenceHz)
    {
        SlotParams p;
        p.p[LadderProcessor::cutoff] = cutoffHz;
        p.p[LadderProcessor::resonance] = res;
        p.p[LadderProcessor::drive] = driveDb;
        p.p[LadderProcessor::keyTrack] = key;
        p.frequencyHz = noteHz;
        return p;
    }

    std::vector<float> runLadder(const std::vector<float>& input, const SlotParams& params)
    {
        SlotMemory memory;
        memory.allocate(0);
        LadderProcessor ladder;
        ladder.prepare(kRate, kSubBlock, memory);
        ladder.noteOn(NoteContext {});

        std::vector<float> left(input), right(input);
        for (std::size_t start = 0; start < left.size(); start += kSubBlock)
        {
            const int n = static_cast<int>(std::min<std::size_t>(kSubBlock, left.size() - start));
            ladder.process(left.data() + start, right.data() + start, n, params);
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
        juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(kFftSize));
        juce::dsp::FFT fft(kFftOrder);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        const int bin = static_cast<int>(std::round(hz * kFftSize / kRate));
        float peak = 0.0f;
        for (int b = std::max(1, bin - 3); b <= bin + 3; ++b)
            peak = std::max(peak, buffer[static_cast<std::size_t>(b)]);
        return peak;
    }

    Envelope makeEnvelope(const Envelope::Parameters& parameters)
    {
        Envelope envelope;
        envelope.setSampleRate(kRate);
        envelope.setParameters(parameters);
        return envelope;
    }
}

class V3ProcessorTests final : public juce::UnitTest
{
public:
    V3ProcessorTests() : juce::UnitTest("V3 ladder and envelope", "V3") {}

    void runTest() override
    {
        beginTest("ladder_descriptor_and_four_pole_slope");
        {
            expect(juce::String(LadderProcessor::descriptor().name) == "Ladder");
            expectEquals(LadderProcessor::descriptor().usedParams, static_cast<int>(LadderProcessor::paramCount));

            const auto input = sine(440.0f, 0.1f, 8192);
            const auto oneOctaveAbove = runLadder(input, ladderParams(220.0f, 0.0f));
            const auto twoOctavesAbove = runLadder(input, ladderParams(110.0f, 0.0f));
            const float nearRms = rms(oneOctaveAbove, 4096);
            const float farRms = rms(twoOctavesAbove, 4096);
            logMessage("one octave above cutoff: " + juce::String(20.0f * std::log10(nearRms / rms(input, 4096)), 1)
                       + " dB, two octaves: " + juce::String(20.0f * std::log10(farRms / rms(input, 4096)), 1) + " dB");
            expect(farRms > 0.0f);
            expect(nearRms > farRms * 8.0f); // one extra octave costs about 24 dB, not 12 dB
        }

        beginTest("ladder_passes_signal_well_below_cutoff");
        {
            const auto input = sine(440.0f, 0.1f, 8192);
            const auto output = runLadder(input, ladderParams(8000.0f, 0.0f));
            const float gainDb = 20.0f * std::log10(rms(output, 4096) / rms(input, 4096));
            logMessage("440 Hz through cutoff 8 kHz: " + juce::String(gainDb, 2) + " dB");
            expect(std::abs(gainDb) < 0.5f);
        }

        beginTest("ladder_self_oscillates_at_the_cutoff_with_full_resonance");
        {
            std::vector<float> input(24000, 0.0f);
            input[0] = 0.5f;
            const auto output = runLadder(input, ladderParams(1000.0f, 1.0f));
            const double hz = measureFrequency(output, 12000);
            logMessage("self-oscillation at cutoff 1 kHz: " + juce::String(hz, 1) + " Hz, peak " + juce::String(peakAbs(output), 3));
            expect(std::abs(hz - 1000.0) < 30.0);
            expect(rms(output, 12000) > 0.05f);
            expect(peakAbs(output) < 4.0f);
        }

        beginTest("ladder_key_tracking_raises_the_cutoff_with_the_note");
        {
            const auto input = sine(2000.0f, 0.1f, 8192);
            const float twoOctavesUp = LadderProcessor::keyReferenceHz * 4.0f;
            const auto tracked = runLadder(input, ladderParams(500.0f, 0.0f, 0.0f, 1.0f, twoOctavesUp));
            const auto fixed = runLadder(input, ladderParams(500.0f, 0.0f, 0.0f, 0.0f, twoOctavesUp));
            const float ratioDb = 20.0f * std::log10(rms(tracked, 4096) / rms(fixed, 4096));
            logMessage("full key tracking two octaves up passes " + juce::String(ratioDb, 1) + " dB more");
            expect(ratioDb > 12.0f);
        }

        beginTest("ladder_drive_adds_harmonics_and_stays_bounded");
        {
            const auto input = sine(234.375f, 0.5f, 4096 + kFftSize); // bin 40: its third harmonic sits on bin 120
            const auto clean = runLadder(input, ladderParams(20000.0f, 0.0f, 0.0f));
            const auto driven = runLadder(input, ladderParams(20000.0f, 0.0f, 24.0f));
            const float cleanThird = peakNearHz(clean, 4096, 703.125) / peakNearHz(clean, 4096, 234.375);
            const float drivenThird = peakNearHz(driven, 4096, 703.125) / peakNearHz(driven, 4096, 234.375);
            logMessage("third harmonic: clean " + juce::String(20.0f * std::log10(cleanThird), 1)
                       + " dB, driven " + juce::String(20.0f * std::log10(drivenThird), 1) + " dB");
            expect(drivenThird > cleanThird * 10.0f);
            expect(20.0f * std::log10(drivenThird) > -30.0f);
            expect(peakAbs(driven) <= 1.0f + 1.0e-4f);
        }

        beginTest("ladder_extreme_settings_stay_finite_and_bounded");
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            std::vector<float> noise(8192);
            for (auto& v : noise)
                v = dist(rng);
            const auto output = runLadder(noise, ladderParams(20000.0f, 1.0f, 24.0f, 1.0f, 8000.0f));
            bool finite = true;
            for (auto v : output)
                finite = finite && std::isfinite(v);
            expect(finite);
            logMessage("extreme settings peak: " + juce::String(peakAbs(output), 3));
            expect(peakAbs(output) < 2.0f * (1.0f + 0.5f * 4.3f)); // twice the saturator bound times the bass makeup
        }

        beginTest("envelope_attack_reaches_full_level_in_the_programmed_time");
        {
            for (float curve : { 0.0f, 0.58f, 1.0f })
            {
                Envelope::Parameters p;
                p.attack = 0.010f;
                p.attackCurve = curve;
                auto envelope = makeEnvelope(p);
                envelope.noteOn(1.0f);
                int samples = 0;
                while (envelope.getNextSample() < 1.0f && samples < 2000)
                    ++samples;
                logMessage("curve " + juce::String(curve, 2) + ": full level after " + juce::String(samples + 1) + " samples");
                expect(std::abs(samples + 1 - 480) <= 3);
            }
        }

        beginTest("envelope_curve_shapes_the_attack");
        {
            auto valueAtHalfTime = [](float curve)
            {
                Envelope::Parameters p;
                p.attack = 0.010f;
                p.attackCurve = curve;
                auto envelope = makeEnvelope(p);
                envelope.noteOn(1.0f);
                float value = 0.0f;
                for (int i = 0; i < 240; ++i)
                    value = envelope.getNextSample();
                return value;
            };
            const float convex = valueAtHalfTime(0.0f);
            const float linear = valueAtHalfTime(1.0f);
            logMessage("attack at half time: curve 0 " + juce::String(convex, 3) + ", curve 1 " + juce::String(linear, 3));
            expect(convex > 0.75f);
            expect(std::abs(linear - 0.5f) < 0.06f);
        }

        beginTest("envelope_loop_cycles_attack_and_decay_while_held");
        {
            Envelope::Parameters p;
            p.attack = 0.010f;
            p.decay = 0.010f;
            p.sustain = 0.0f;
            p.loop = true;
            auto envelope = makeEnvelope(p);
            envelope.noteOn(1.0f);

            std::vector<int> peaks;
            bool armed = true;
            for (int i = 0; i < 48000; ++i)
            {
                const float v = envelope.getNextSample();
                if (armed && v >= 0.999f)
                {
                    peaks.push_back(i);
                    armed = false;
                }
                if (v < 0.5f)
                    armed = true;
            }
            expect(peaks.size() >= 40);
            for (std::size_t i = 1; i < peaks.size(); ++i)
                expect(std::abs(peaks[i] - peaks[i - 1] - 960) <= 10, "loop period " + juce::String(peaks[i] - peaks[i - 1]));
            envelope.noteOff();
            for (int i = 0; i < 48000; ++i)
                envelope.getNextSample();
            expect(! envelope.isActive());
        }

        beginTest("envelope_one_shot_ignores_the_gate_and_finishes");
        {
            Envelope::Parameters p;
            p.attack = 0.005f;
            p.decay = 0.010f;
            p.sustain = 0.5f;
            p.release = 0.010f;
            p.oneShot = true;
            auto envelope = makeEnvelope(p);
            envelope.noteOn(1.0f);
            float value = 1.0f;
            int samples = 0;
            while (samples < 48000)
            {
                value = envelope.getNextSample();
                ++samples;
                if (! envelope.isActive())
                    break;
            }
            logMessage("one-shot finished after " + juce::String(samples) + " samples");
            expect(! envelope.isActive());
            expect(samples < 1440); // attack + decay + release
            expectEquals(value, 0.0f);

            // A note off during the decay is honoured only once the decay has ended.
            auto second = makeEnvelope(p);
            second.noteOn(1.0f);
            for (int i = 0; i < 300; ++i)
                second.getNextSample();
            second.noteOff();
            expect(second.getState() == Envelope::State::decay);
        }

        beginTest("envelope_velocity_sensitivity_scales_the_peak");
        {
            auto peakFor = [](float sensitivity, float velocity)
            {
                Envelope::Parameters p;
                p.attack = 0.005f;
                p.decay = 0.1f;
                p.velocitySensitivity = sensitivity;
                auto envelope = makeEnvelope(p);
                envelope.noteOn(velocity);
                float peak = 0.0f;
                for (int i = 0; i < 480; ++i)
                    peak = std::max(peak, envelope.getNextSample());
                return peak;
            };
            expectWithinAbsoluteError(peakFor(1.0f, 0.5f), 0.5f, 1.0e-4f);
            expectWithinAbsoluteError(peakFor(0.0f, 0.5f), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError(peakFor(0.5f, 0.5f), 0.75f, 1.0e-4f);
        }

        beginTest("envelope_retrigger_continues_from_the_current_level");
        {
            Envelope::Parameters p;
            p.attack = 0.020f;
            p.release = 0.050f;
            auto envelope = makeEnvelope(p);
            envelope.noteOn(1.0f);
            float value = 0.0f;
            for (int i = 0; i < 480; ++i)
                value = envelope.getNextSample();
            envelope.noteOff();
            for (int i = 0; i < 240; ++i)
                value = envelope.getNextSample();
            expect(value > 0.1f && value < 1.0f);
            envelope.noteOn(1.0f);
            const float next = envelope.getNextSample();
            expect(next >= value - 1.0e-4f);
            expect(next < value + 0.05f);
        }
    }
};

static V3ProcessorTests v3ProcessorTests;
