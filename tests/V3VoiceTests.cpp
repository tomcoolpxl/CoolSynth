#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/Voice.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Wave = VaGenerator::Wave;

    constexpr double kRate = 48000.0;
    constexpr int kFftOrder = 13;
    constexpr int kFftSize = 1 << kFftOrder;

    float vaNorm(int param, float units)
    {
        return unitsToNormalised(VaGenerator::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    float ladderNorm(int param, float units)
    {
        return unitsToNormalised(LadderProcessor::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    GeneratorSettings vaGenerator(Wave wave, float level = 1.0f)
    {
        GeneratorSettings g;
        g.type = generatorType::va;
        g.p[VaGenerator::shape] = vaNorm(VaGenerator::shape, static_cast<float>(wave));
        g.p[VaGenerator::pulseWidth] = vaNorm(VaGenerator::pulseWidth, 0.5f);
        g.p[VaGenerator::unison] = vaNorm(VaGenerator::unison, 1.0f);
        g.p[VaGenerator::detune] = 0.0f;
        g.p[VaGenerator::spread] = 0.0f;
        g.p[VaGenerator::mix] = vaNorm(VaGenerator::mix, 0.67f);
        g.p[VaGenerator::randomPhase] = 0.0f;
        g.p[VaGenerator::sub] = 0.0f;
        g.level = level;
        return g;
    }

    ProcessorSettings ladder(float cutoffHz, float resonance)
    {
        ProcessorSettings p;
        p.type = processorType::ladder;
        p.p[LadderProcessor::cutoff] = ladderNorm(LadderProcessor::cutoff, cutoffHz);
        p.p[LadderProcessor::resonance] = ladderNorm(LadderProcessor::resonance, resonance);
        p.p[LadderProcessor::drive] = 0.0f;
        p.p[LadderProcessor::keyTrack] = 0.0f;
        p.p[LadderProcessor::mode] = 0.0f;
        p.p[LadderProcessor::bass] = ladderNorm(LadderProcessor::bass, 0.5f);
        return p;
    }

    Envelope::Parameters heldEnvelope()
    {
        Envelope::Parameters p;
        p.attack = 0.002f;
        p.decay = 0.1f;
        p.sustain = 1.0f;
        p.release = 0.01f;
        return p;
    }

    VoiceSettings basicSettings(Wave wave)
    {
        VoiceSettings s;
        s.generators[0] = vaGenerator(wave);
        s.envelopes = { heldEnvelope(), heldEnvelope(), heldEnvelope() };
        return s;
    }

    std::unique_ptr<Voice> makeVoice(std::uint32_t seed = 1)
    {
        auto voice = std::make_unique<Voice>();
        voice->prepare(kRate, seed);
        return voice;
    }

    NoteStart noteAt(int note, float velocity = 1.0f)
    {
        NoteStart n;
        n.note = note;
        n.velocity = velocity;
        return n;
    }

    struct Stereo
    {
        std::vector<float> left, right;
    };

    Stereo renderVoice(Voice& voice, const VoiceSettings& settings, int samples, const VoiceGlobals& globals = {})
    {
        Stereo out { std::vector<float>(static_cast<std::size_t>(samples), 0.0f), std::vector<float>(static_cast<std::size_t>(samples), 0.0f) };
        constexpr int block = 256;
        for (int start = 0; start < samples; start += block)
            voice.render(out.left.data() + start, out.right.data() + start, std::min(block, samples - start), settings, globals);
        return out;
    }

    void append(std::vector<float>& to, const std::vector<float>& from) { to.insert(to.end(), from.begin(), from.end()); }

    double measureFrequency(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, x.size()); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
    }

    double cents(double measured, double target) { return 1200.0 * std::log2(measured / target); }

    float rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
    }

    float maxStep(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float step = 0.0f;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, x.size()); ++i)
            step = std::max(step, std::abs(x[i] - x[i - 1]));
        return step;
    }

    float brightness(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double diff = 0.0, level = 0.0;
        for (std::size_t i = from + 1; i < to; ++i)
        {
            const double d = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
            diff += d * d;
            level += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        }
        return static_cast<float>(std::sqrt(diff / std::max(level, 1.0e-30)));
    }

    std::vector<float> spectrum(const std::vector<float>& x, std::size_t skip)
    {
        std::vector<float> buffer(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            buffer[static_cast<std::size_t>(i)] = x[skip + static_cast<std::size_t>(i)];
        juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::blackmanHarris);
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(kFftSize));
        juce::dsp::FFT fft(kFftOrder);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        buffer.resize(static_cast<std::size_t>(kFftSize / 2));
        return buffer;
    }

    float peakNear(const std::vector<float>& mags, double hz)
    {
        const int bin = static_cast<int>(std::round(hz * kFftSize / kRate));
        float peak = 0.0f;
        for (int b = std::max(1, bin - 4); b <= std::min(kFftSize / 2 - 1, bin + 4); ++b)
            peak = std::max(peak, mags[static_cast<std::size_t>(b)]);
        return peak;
    }

    float hzFor(float pitch) { return 440.0f * std::exp2((pitch - 69.0f) / 12.0f); }
}

class V3VoiceTests final : public juce::UnitTest
{
public:
    V3VoiceTests() : juce::UnitTest("V3 voice", "V3") {}

    void runTest() override
    {
        beginTest("a_note_plays_at_its_pitch_centred");
        {
            auto voice = makeVoice();
            voice->start(noteAt(69));
            const auto out = renderVoice(*voice, basicSettings(Wave::saw), 48000);
            const double hz = measureFrequency(out.left, 4800, out.left.size());
            expect(std::abs(cents(hz, 440.0)) < 1.0, "measured " + juce::String(hz, 3) + " Hz");
            float difference = 0.0f;
            for (std::size_t i = 0; i < out.left.size(); ++i)
                difference = std::max(difference, std::abs(out.left[i] - out.right[i]));
            expect(difference < 1.0e-6f);
        }

        beginTest("pitch_offsets_and_key_tracking");
        {
            auto settings = basicSettings(Wave::sine);
            settings.generators[0].pitchSemitones = 12.0f;
            auto up = makeVoice();
            up->start(noteAt(69));
            const auto octave = renderVoice(*up, settings, 24000);
            expect(std::abs(cents(measureFrequency(octave.left, 2400, octave.left.size()), 880.0)) < 1.0);

            settings.generators[0].pitchSemitones = 0.0f;
            settings.generators[0].keyTrack = false;
            auto fixed = makeVoice();
            fixed->start(noteAt(81));
            const auto untracked = renderVoice(*fixed, settings, 24000);
            expect(std::abs(cents(measureFrequency(untracked.left, 2400, untracked.left.size()), 261.6256)) < 1.0);
        }

        beginTest("mixer_levels_scale_each_generator");
        {
            auto settings = basicSettings(Wave::saw);
            auto full = makeVoice();
            full->start(noteAt(57));
            const auto a = renderVoice(*full, settings, 9600);
            settings.generators[0].level = 0.5f;
            auto half = makeVoice();
            half->start(noteAt(57));
            const auto b = renderVoice(*half, settings, 9600);
            const float db = 20.0f * std::log10(rms(b.left, 960, 9600) / rms(a.left, 960, 9600));
            expectWithinAbsoluteError(db, -6.0206f, 0.05f);
        }

        beginTest("ring_combine_gives_sum_and_difference_from_a_silent_modulator");
        {
            auto settings = basicSettings(Wave::sine);
            settings.generators[0].level = 0.0f;
            settings.generators[0].pitchSemitones = 7.0f;
            settings.generators[1] = vaGenerator(Wave::sine);
            settings.generators[1].combine = Combine::ring;
            settings.generators[1].combineAmount = 1.0f;
            auto voice = makeVoice();
            voice->start(noteAt(69));
            const auto out = renderVoice(*voice, settings, 4096 + kFftSize);
            const auto mags = spectrum(out.left, 4096);
            const double modulator = hzFor(76.0f);
            const float sum = peakNear(mags, 440.0 + modulator);
            const float difference = peakNear(mags, modulator - 440.0);
            expect(peakNear(mags, 440.0) / sum < 0.01f);
            expect(peakNear(mags, modulator) / sum < 0.01f);
            expect(difference / sum > 0.5f);
        }

        beginTest("sync_combine_locks_the_slave_to_the_master");
        {
            auto settings = basicSettings(Wave::saw);
            settings.generators[0].level = 0.0f;
            settings.generators[1] = vaGenerator(Wave::saw);
            settings.generators[1].pitchSemitones = 7.0f;
            settings.generators[1].combine = Combine::sync;
            auto voice = makeVoice();
            voice->start(noteAt(69));
            const auto out = renderVoice(*voice, settings, 4096 + kFftSize);
            const auto mags = spectrum(out.left, 4096);
            const float ratio = peakNear(mags, hzFor(76.0f)) / peakNear(mags, 440.0);
            logMessage("synced: free-running slave pitch vs master pitch " + juce::String(20.0f * std::log10(ratio), 1) + " dB");
            expect(ratio < 0.03f);
        }

        beginTest("fm_combine_adds_sidebands");
        {
            auto settings = basicSettings(Wave::sine);
            auto plain = makeVoice();
            plain->start(noteAt(69));
            const auto dry = spectrum(renderVoice(*plain, settings, 4096 + kFftSize).left, 4096);
            expect(peakNear(dry, 880.0) / peakNear(dry, 440.0) < 0.001f);

            settings.generators[0].level = 0.0f;
            settings.generators[1] = vaGenerator(Wave::sine);
            settings.generators[1].combine = Combine::fm;
            settings.generators[1].combineAmount = 0.5f;
            auto voice = makeVoice();
            voice->start(noteAt(69));
            const auto wet = spectrum(renderVoice(*voice, settings, 4096 + kFftSize).left, 4096);
            expect(peakNear(wet, 880.0) / peakNear(wet, 440.0) > 0.1f);
        }

        beginTest("ladder_in_p1_filters_and_env2_opens_it");
        {
            auto settings = basicSettings(Wave::saw);
            auto open = makeVoice();
            open->start(noteAt(45));
            const auto dry = renderVoice(*open, settings, 9600);

            settings.processors[0] = ladder(200.0f, 0.0f);
            auto closed = makeVoice();
            closed->start(noteAt(45));
            const auto filtered = renderVoice(*closed, settings, 9600);
            expect(brightness(filtered.left, 4800, 9600) < 0.5f * brightness(dry.left, 4800, 9600));

            Envelope::Parameters sweep;
            sweep.attack = 0.001f;
            sweep.decay = 0.3f;
            sweep.sustain = 0.0f;
            settings.envelopes[1] = sweep;
            settings.env2ToP1 = 0.5f;
            auto swept = makeVoice();
            swept->start(noteAt(45));
            const auto out = renderVoice(*swept, settings, 28800);
            const float early = brightness(out.left, 480, 2880);
            const float late = brightness(out.left, 24000, 26400);
            logMessage("env2 sweep brightness early " + juce::String(early, 3) + ", late " + juce::String(late, 3));
            expect(early > 2.0f * late);
        }

        beginTest("release_ends_the_voice_and_an_idle_voice_adds_nothing");
        {
            auto settings = basicSettings(Wave::saw);
            settings.envelopes[0].release = 0.02f;
            auto voice = makeVoice();
            voice->start(noteAt(60));
            renderVoice(*voice, settings, 4800);
            voice->release();
            expect(voice->isReleased());
            renderVoice(*voice, settings, 9600);
            expect(! voice->isActive());

            std::vector<float> left(512, 0.25f), right(512, 0.25f);
            voice->render(left.data(), right.data(), 512, settings, {});
            for (auto v : left)
                expectEquals(v, 0.25f);
        }

        beginTest("steal_fades_out_then_starts_the_new_note_without_a_step");
        {
            auto settings = basicSettings(Wave::sine);
            auto voice = makeVoice();
            voice->start(noteAt(57));
            std::vector<float> out;
            append(out, renderVoice(*voice, settings, 9600).left);
            voice->start(noteAt(69));
            expect(voice->isStealing());
            append(out, renderVoice(*voice, settings, 72).left);
            expect(! voice->isStealing());
            expectEquals(voice->currentNote(), 69);
            append(out, renderVoice(*voice, settings, 9600).left);

            const float oldSteady = maxStep(out, 4800, 9600);
            const float newSteady = maxStep(out, 9600 + 72 + 4800, out.size());
            const float handover = maxStep(out, 9599, 9600 + 72 + 960);
            logMessage("steal: steady steps " + juce::String(oldSteady, 4) + " and " + juce::String(newSteady, 4)
                       + ", handover " + juce::String(handover, 4));
            expect(handover <= 1.1f * std::max(oldSteady, newSteady));
            expect(std::abs(cents(measureFrequency(out, out.size() - 4800, out.size()), 440.0)) < 1.0);
        }

        beginTest("release_during_a_handover_still_plays_and_ends");
        {
            auto settings = basicSettings(Wave::saw);
            auto voice = makeVoice();
            voice->start(noteAt(57));
            renderVoice(*voice, settings, 4800);
            voice->start(noteAt(69));
            voice->release(0.4f);
            expectEquals(voice->currentNote(), 69);
            renderVoice(*voice, settings, 24000);
            expect(! voice->isActive());
        }

        beginTest("type_change_while_held_crossfades");
        {
            auto settings = basicSettings(Wave::sine);
            auto voice = makeVoice();
            voice->start(noteAt(57));
            std::vector<float> out;
            append(out, renderVoice(*voice, settings, 9600).left);
            const float steady = maxStep(out, 4800, 9600);
            settings.generators[0].type = generatorType::off;
            append(out, renderVoice(*voice, settings, 4800).left);
            expectEquals(voice->generatorTypeForTesting(0), generatorType::off);
            expect(rms(out, 9600 + 480, out.size()) < 1.0e-6f);
            settings.generators[0].type = generatorType::va;
            append(out, renderVoice(*voice, settings, 4800).left);
            expect(maxStep(out, 9599, out.size()) <= 1.1f * steady);
            expect(rms(out, out.size() - 2400, out.size()) > 0.3f);
        }

        beginTest("glide_and_legato_move_pitch_in_constant_time");
        {
            auto settings = basicSettings(Wave::sine);
            settings.glideSeconds = 0.1f;
            auto voice = makeVoice();
            auto start = noteAt(69);
            start.glideFrom = 57;
            voice->start(start);
            renderVoice(*voice, settings, 2400);
            expectWithinAbsoluteError(voice->pitchForTesting(), 63.0f, 0.02f);
            renderVoice(*voice, settings, 3200);
            expectEquals(voice->pitchForTesting(), 69.0f);

            auto glideDown = noteAt(57);
            glideDown.glideFrom = 69;
            voice->legato(glideDown);
            renderVoice(*voice, settings, 2400);
            expectWithinAbsoluteError(voice->pitchForTesting(), 63.0f, 0.02f);
            expect(voice->ampLevel() >= 0.999f);

            voice->legato(noteAt(48)); // no glide source: jumps
            renderVoice(*voice, settings, 32);
            expectEquals(voice->pitchForTesting(), 48.0f);
        }

        beginTest("local_and_global_sources");
        {
            auto settings = basicSettings(Wave::saw);
            VoiceGlobals globals;
            globals.sources.set(ModSource::wheel, 0.7f);
            auto voice = makeVoice();
            voice->start(noteAt(81, 0.6f));
            renderVoice(*voice, settings, 256, globals);
            const auto& sources = voice->sourcesForTesting();
            expectEquals(sources.get(ModSource::velocity), 0.6f);
            expectWithinAbsoluteError(sources.get(ModSource::key), 21.0f / 64.0f, 1.0e-6f);
            expect(std::abs(sources.get(ModSource::noteRandom)) <= 1.0f);
            expectEquals(sources.get(ModSource::env1), voice->ampLevel());
            expectEquals(sources.get(ModSource::wheel), 0.7f);
            voice->release(0.3f);
            renderVoice(*voice, settings, 32, globals);
            expectEquals(voice->sourcesForTesting().get(ModSource::releaseVelocity), 0.3f);
        }

        beginTest("drift_wanders_within_its_range_and_differs_per_voice");
        {
            auto settings = basicSettings(Wave::sine);
            settings.driftCents = 10.0f;
            auto a = makeVoice(1);
            auto b = makeVoice(2);
            a->start(noteAt(60));
            b->start(noteAt(60));
            float lowest = 1.0f, highest = -1.0f, largestGap = 0.0f;
            std::vector<float> scratch(256);
            for (int block = 0; block < 750; ++block) // 4 s
            {
                renderVoice(*a, settings, 256);
                renderVoice(*b, settings, 256);
                lowest = std::min(lowest, a->driftForTesting());
                highest = std::max(highest, a->driftForTesting());
                largestGap = std::max(largestGap, std::abs(a->driftForTesting() - b->driftForTesting()));
            }
            logMessage("drift range " + juce::String(lowest, 3) + " .. " + juce::String(highest, 3));
            expect(lowest >= -1.0f && highest <= 1.0f);
            expect(highest - lowest > 0.2f);
            expect(largestGap > 0.05f);
        }

        beginTest("a_note_on_a_silent_voice_uses_the_current_patch_from_its_first_sample");
        {
            // Regression: the envelopes used to start with the previous patch's times, because the
            // note began before the block read the new settings; after a preset change the first
            // note on every voice then sounded like the old preset.
            auto voice = makeVoice();
            auto slow = basicSettings(Wave::sine);
            slow.envelopes[0].attack = 0.5f;
            slow.envelopes[0].release = 0.01f;
            voice->start(noteAt(60));
            renderVoice(*voice, slow, 48000);
            voice->release();
            renderVoice(*voice, slow, 4800);
            expect(! voice->isActive());

            auto fast = basicSettings(Wave::sine);
            fast.envelopes[0].attack = 0.001f;
            voice->start(noteAt(60));
            renderVoice(*voice, fast, 96); // 2 ms
            expect(voice->ampLevel() > 0.99f, "the attack still runs at the previous patch's speed: amp " + juce::String(voice->ampLevel(), 3));
        }

        beginTest("voice_pan_shifts_the_image");
        {
            auto voice = makeVoice();
            auto start = noteAt(69);
            start.pan = -1.0f;
            voice->start(start);
            const auto out = renderVoice(*voice, basicSettings(Wave::saw), 4800);
            expect(rms(out.left, 480, 4800) > 0.3f);
            expect(rms(out.right, 480, 4800) < 1.0e-6f);
        }

        beginTest("amplifier_minimum_times_keep_onsets_and_cutoffs_soft");
        {
            auto settings = basicSettings(Wave::pulse);
            settings.envelopes[0].attack = 0.0f;
            settings.envelopes[0].release = 0.0f;
            auto voice = makeVoice();
            voice->start(noteAt(69));
            const auto onset = renderVoice(*voice, settings, 48);
            const float onsetStep = maxStep(onset.left, 0, onset.left.size());
            logMessage("largest step at a zero-attack onset: " + juce::String(onsetStep, 3));
            expect(onsetStep < 0.15f);

            auto sine = basicSettings(Wave::sine);
            sine.envelopes[0].release = 0.0f;
            auto held = makeVoice();
            held->start(noteAt(69));
            std::vector<float> out;
            append(out, renderVoice(*held, sine, 4800).left);
            held->release();
            append(out, renderVoice(*held, sine, 480).left);
            const float releaseStep = maxStep(out, 4799, out.size());
            logMessage("largest step at a zero-release cutoff: " + juce::String(releaseStep, 3));
            expect(releaseStep < 0.12f);
        }
    }
};

static V3VoiceTests v3VoiceTests;
