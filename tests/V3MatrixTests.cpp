#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "parameters/v3/PartParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/Voice.h"

namespace
{
    using namespace coolsynth::synth::v3;
    using Wave = VaGenerator::Wave;
    namespace dst = coolsynth::synth::v3::modDestination;

    constexpr double kRate = 48000.0;

    float vaNorm(int param, float units)
    {
        return unitsToNormalised(VaGenerator::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    float ladderNorm(int param, float units)
    {
        return unitsToNormalised(LadderProcessor::descriptor().params[static_cast<std::size_t>(param)], units);
    }

    GeneratorSettings va(Wave wave)
    {
        GeneratorSettings g;
        g.type = generatorType::va;
        g.p[VaGenerator::shape] = vaNorm(VaGenerator::shape, static_cast<float>(wave));
        g.p[VaGenerator::pulseWidth] = vaNorm(VaGenerator::pulseWidth, 0.5f);
        g.p[VaGenerator::unison] = 0.0f;
        g.p[VaGenerator::mix] = vaNorm(VaGenerator::mix, 0.67f);
        g.p[VaGenerator::randomPhase] = 0.0f;
        g.level = 1.0f;
        return g;
    }

    ProcessorSettings ladder(float cutoffHz)
    {
        ProcessorSettings p;
        p.type = processorType::ladder;
        p.p[LadderProcessor::cutoff] = ladderNorm(LadderProcessor::cutoff, cutoffHz);
        p.p[LadderProcessor::resonance] = 0.0f;
        p.p[LadderProcessor::bass] = ladderNorm(LadderProcessor::bass, 0.5f);
        return p;
    }

    VoiceSettings sineVoice()
    {
        VoiceSettings s;
        s.generators[0] = va(Wave::sine);
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.01f;
        s.envelopes = { held, held, held };
        return s;
    }

    MatrixSlot route(ModSource source, int destination, float amount, ModCurve curve = ModCurve::linear, ModSource via = ModSource::none)
    {
        MatrixSlot slot;
        slot.source = source;
        slot.destination = destination;
        slot.amount = amount;
        slot.curve = curve;
        slot.via = via;
        return slot;
    }

    struct Stereo
    {
        std::vector<float> left, right;
    };

    Stereo play(const VoiceSettings& settings, int samples, int note = 57, float velocity = 1.0f)
    {
        auto voice = std::make_unique<Voice>();
        voice->prepare(kRate, 1);
        NoteStart start;
        start.note = note;
        start.velocity = velocity;
        voice->start(start);
        Stereo out { std::vector<float>(static_cast<std::size_t>(samples)), std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int s = 0; s < samples; s += 256)
            voice->render(out.left.data() + s, out.right.data() + s, std::min(256, samples - s), settings, {});
        return out;
    }

    double frequency(const std::vector<float>& x, std::size_t from)
    {
        std::vector<double> crossings;
        for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kRate / ((crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1));
    }

    float peak(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
    {
        float p = 0.0f;
        for (std::size_t i = from; i < (to == 0 ? x.size() : to); ++i)
            p = std::max(p, std::abs(x[i]));
        return p;
    }

    double rms(const std::vector<float>& x, std::size_t from)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(x.size() - from));
    }
}

class V3MatrixTests final : public juce::UnitTest
{
public:
    V3MatrixTests() : juce::UnitTest("V3 matrix", "V3") {}

    template <typename T>
    void expectWithinRelativeError(T actual, T expected, T tolerance)
    {
        expectWithinAbsoluteError(actual, expected, std::abs(expected) * tolerance);
    }

    void runTest() override
    {
        beginTest("curves_bend_the_source_as_named");
        {
            expectWithinAbsoluteError(applyCurve(ModCurve::linear, -0.5f), -0.5f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::unipolar, -1.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::unipolar, 0.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::exponential, -0.5f), -0.25f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::logarithmic, 0.25f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::sCurve, 0.5f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(applyCurve(ModCurve::sCurve, -0.25f), -0.15625f, 1.0e-6f);
            for (int c = 0; c < modCurveCount; ++c)
            {
                expectWithinAbsoluteError(applyCurve(static_cast<ModCurve>(c), 1.0f), 1.0f, 1.0e-6f);
                if (static_cast<ModCurve>(c) != ModCurve::unipolar)
                    expectWithinAbsoluteError(applyCurve(static_cast<ModCurve>(c), 0.0f), 0.0f, 1.0e-6f);
            }
        }

        beginTest("slots_add_up_scale_by_via_and_land_in_their_destination");
        {
            ModSources sources;
            sources.set(ModSource::velocity, 0.8f);
            sources.set(ModSource::wheel, 0.5f);
            sources.set(ModSource::lfo1, -1.0f);
            MatrixSettings m;
            m.add(route(ModSource::velocity, dst::generatorParameter(0, 1), 0.5f, ModCurve::linear, ModSource::wheel)); // 0.2
            m.add(route(ModSource::velocity, dst::generatorParameter(0, 1), 0.25f));                                      // 0.2
            m.add(route(ModSource::lfo1, dst::pitch, 0.5f, ModCurve::unipolar));                                          // 0
            m.add(route(ModSource::velocity, dst::generator(1, dst::generatorField::pitch), 0.25f));                      // 9.6 st
            m.add(route(ModSource::velocity, dst::processorParameter(2, 7), -1.0f));
            m.add(route(ModSource::velocity, dst::lfo2Rate, 0.25f));
            m.add(route(ModSource::lfo1, dst::vca, 0.5f));
            m.add(route(ModSource::velocity, dst::generator(2, dst::generatorField::amount), 0.5f));
            MatrixOffsets out;
            evaluateMatrix(m, sources, out);
            expectWithinAbsoluteError(out.generators[0].p[1], 0.4f, 1.0e-6f);
            expectWithinAbsoluteError(out.pitchSemitones, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(out.generators[1].pitchSemitones, 0.8f * 0.25f * 48.0f, 1.0e-5f);
            expectWithinAbsoluteError(out.processors[2].p[7], -0.8f, 1.0e-6f);
            expectWithinAbsoluteError(out.lfoRateOctaves[1], 0.8f, 1.0e-6f);
            expectWithinAbsoluteError(out.vca, -0.5f, 1.0e-6f);
            expectWithinAbsoluteError(out.generators[2].amount, 0.4f, 1.0e-6f);
            expectWithinAbsoluteError(out.generators[0].pitchSemitones, 0.0f, 1.0e-6f);
        }

        beginTest("empty_slots_are_dropped_and_the_names_match_the_numbers");
        {
            MatrixSettings m;
            m.add(route(ModSource::none, dst::pitch, 1.0f));
            m.add(route(ModSource::velocity, dst::none, 1.0f));
            m.add(route(ModSource::velocity, dst::pitch, 0.0f));
            m.add(route(ModSource::velocity, dst::count, 1.0f));
            expectEquals(m.count, 0);
            namespace pv3 = coolsynth::parameters::v3;
            expectEquals(pv3::modDestinationNames().size(), dst::count);
            expectEquals(pv3::modSourceNames().size(), ModSources::count);
            expectEquals(pv3::modCurveNames().size(), modCurveCount);
            expectEquals(pv3::modDestinationNames()[dst::generatorParameter(1, 2)], juce::String("G2 Param 3"));
            expectEquals(pv3::modDestinationNames()[dst::processorParameter(0, 0)], juce::String("P1 Param 1"));
            expectEquals(pv3::modSourceNames()[static_cast<int>(ModSource::glfo2)], juce::String("GLFO 2"));
            expectEquals(pv3::modSourceNames()[static_cast<int>(ModSource::wheel)], juce::String("Mod Wheel"));
        }

        beginTest("a_pitch_route_transposes_by_the_amount");
        {
            auto plain = sineVoice();
            const double base = frequency(play(plain, 9600).left, 2400);

            auto voicePitch = sineVoice();
            voicePitch.matrix.add(route(ModSource::velocity, dst::pitch, 12.0f / 48.0f));
            expectWithinRelativeError(frequency(play(voicePitch, 9600).left, 2400), base * 2.0, 0.001);

            auto slotPitch = sineVoice();
            slotPitch.matrix.add(route(ModSource::velocity, dst::generator(0, dst::generatorField::pitch), -7.0f / 48.0f));
            expectWithinRelativeError(frequency(play(slotPitch, 9600).left, 2400), base * std::exp2(-7.0 / 12.0), 0.001);
        }

        beginTest("a_vca_route_scales_and_a_gating_lfo_does_not_click");
        {
            const float full = peak(play(sineVoice(), 9600).left, 4800);
            auto ducked = sineVoice();
            ducked.matrix.add(route(ModSource::velocity, dst::vca, -0.5f));
            expectWithinRelativeError(peak(play(ducked, 9600).left, 4800), full * 0.5f, 0.01f);

            auto gated = sineVoice();
            gated.lfos[0].shape = LfoShape::square;
            gated.lfos[0].rateHz = 4.0f;
            gated.matrix.add(route(ModSource::lfo1, dst::vca, -1.0f, ModCurve::unipolar)); // high half: gain 0
            const auto out = play(gated, 24000).left;
            expectLessThan(peak(out, 600, 5400), 1.0e-6f, "the high half of the square closes the VCA");
            expectGreaterThan(peak(out, 6600, 11400), full * 0.95f, "the low half opens it");
            float largestStep = 0.0f;
            for (std::size_t i = 1; i < out.size(); ++i)
                largestStep = std::max(largestStep, std::abs(out[i] - out[i - 1]));
            const float sineStep = full * static_cast<float>(2.0 * 3.14159265 * 220.0 / kRate);
            expectLessThan(largestStep, sineStep + full / 32.0f + 1.0e-4f, "the gate ramps over a sub-block");
        }

        beginTest("a_processor_route_opens_the_filter_and_a_pan_route_moves_the_voice");
        {
            auto closed = sineVoice();
            closed.generators[0] = va(Wave::saw);
            closed.processors[0] = ladder(100.0f);
            auto open = closed;
            open.matrix.add(route(ModSource::velocity, dst::processorParameter(0, LadderProcessor::cutoff), 0.4f));
            expectGreaterThan(rms(play(open, 9600).left, 2400), 1.5 * rms(play(closed, 9600).left, 2400));

            auto panned = sineVoice();
            panned.matrix.add(route(ModSource::velocity, dst::pan, -1.0f));
            const auto out = play(panned, 9600);
            expectGreaterThan(peak(out.left, 2400), 0.1f);
            expectLessThan(peak(out.right, 2400), 0.01f * peak(out.left, 2400));
        }

        beginTest("an_lfo_rate_route_speeds_the_lfo");
        {
            auto settings = sineVoice();
            settings.lfos[0].rateHz = 0.5f;
            settings.lfos[0].shape = LfoShape::sawUp;
            settings.matrix.add(route(ModSource::velocity, dst::lfo1Rate, 0.25f)); // one octave faster
            auto voice = std::make_unique<Voice>();
            voice->prepare(kRate, 1);
            NoteStart start;
            start.note = 60;
            voice->start(start);
            std::vector<float> l(256), r(256);
            for (int i = 0; i < 75; ++i) // 0.4 s
                voice->render(l.data(), r.data(), 256, settings, {});
            // 1 Hz for 0.4 s, less the first sub-block at the unscaled rate
            expectWithinAbsoluteError(voice->lfoForTesting(0).phaseForTesting(), 0.4 - 0.5 * 32.0 / kRate, 1.0e-4);
        }

        beginTest("every_destination_takes_full_modulation_without_nan");
        {
            VoiceSettings base = sineVoice();
            for (int g = 0; g < 3; ++g)
            {
                base.generators[static_cast<std::size_t>(g)] = va(Wave::saw);
                base.generators[static_cast<std::size_t>(g)].combine = g == 0 ? Combine::off : Combine::fm;
            }
            for (auto& p : base.processors)
                p = ladder(1000.0f);
            int checked = 0;
            for (int d = 1; d < dst::count; ++d)
            {
                for (float amount : { 1.0f, -1.0f })
                {
                    auto settings = base;
                    for (int s = 0; s < matrixSlotCount; ++s)
                        settings.matrix.add(route(ModSource::velocity, d, amount));
                    const auto out = play(settings, 4096, 96);
                    bool finite = true;
                    for (std::size_t i = 0; i < out.left.size(); ++i)
                        finite = finite && std::isfinite(out.left[i]) && std::isfinite(out.right[i]);
                    expect(finite, coolsynth::parameters::v3::modDestinationNames()[d] + " made a non-finite sample");
                    expectLessThan(std::max(peak(out.left), peak(out.right)), 16.0f, coolsynth::parameters::v3::modDestinationNames()[d]);
                    ++checked;
                }
            }
            expectEquals(checked, 2 * (dst::count - 1));
        }
    }
};

static V3MatrixTests v3MatrixTests;

class V3MatrixBankTests final : public juce::UnitTest
{
public:
    V3MatrixBankTests() : juce::UnitTest("V3 matrix in bank files", "V3") {}

    void runTest() override
    {
        beginTest("bank_routes_name_their_destinations_by_key");
        {
            const auto result = coolsynth::presets::parseBankFile(
                "[Routes]\n"
                "g1 = VA shape=pulse\n"
                "g2 = VA\n"
                "p1 = Ladder\n"
                "mod = src=lfo1 dst=g1.pw amount=0.3 via=wheel curve=unipolar\n"
                "mod = src=env3 dst=pitch amount=0.5\n"
                "mod5 = src=glfo1 dst=p1.cutoff amount=-0.2\n"
                "mod = src=velocity dst=g2.level amount=0.1\n",
                "routes.csbank");
            expect(result.errors.isEmpty(), result.errors.joinIntoString("; "));
            juce::StringArray entries;
            for (const auto& e : result.presets.front().entries)
                entries.add(e.parameterId + "=" + e.text);
            for (const auto* wanted : { "pA.mod1.src=LFO 1", "pA.mod1.dst=G1 Param 2", "pA.mod1.amount=0.3", "pA.mod1.via=Mod Wheel",
                                        "pA.mod1.curve=unipolar", "pA.mod2.src=Env 3", "pA.mod2.dst=Pitch", "pA.mod5.src=GLFO 1",
                                        "pA.mod5.dst=P1 Param 1", "pA.mod6.dst=G2 Level" })
                expect(entries.contains(wanted), juce::String("missing ") + wanted + " in " + entries.joinIntoString(", "));
        }

        beginTest("bank_routes_report_what_this_preset_cannot_reach");
        {
            const auto result = coolsynth::presets::parseBankFile(
                "[Bad]\n"
                "mod = src=lfo1 dst=g1.cutoff amount=0.3\n" // VA has no cutoff
                "mod = src=lfo1 dst=p2.cutoff amount=0.3\n" // P2 is Off
                "mod = src=lfo9 dst=pitch amount=0.3\n"
                "mod25 = src=lfo1 dst=pitch amount=0.3\n"
                "mod = src=lfo1 dst=pitch depth=0.3\n",
                "bad.csbank");
            expectEquals(result.errors.size(), 5, result.errors.joinIntoString("; "));
        }

        beginTest("an_applied_route_reaches_the_voice_settings");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto parsed = coolsynth::presets::parseBankFile(
                "[Applied]\ng1 = VA shape=pulse\nmod3 = src=lfo1 dst=g1.pw amount=0.3 via=wheel curve=unipolar\n", "applied.csbank");
            expect(parsed.errors.isEmpty());
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            expect(problems.isEmpty(), problems.joinIntoString("; "));

            namespace pv3 = coolsynth::parameters::v3;
            const auto pointers = pv3::bindPartPointers(processor->getValueTreeState(), pv3::Part::a);
            PartSettings settings;
            pv3::readPartSettings(pointers, settings);
            expectEquals(settings.voice.matrix.count, 1);
            const auto& slot = settings.voice.matrix.slots[0];
            expect(slot.source == ModSource::lfo1);
            expectEquals(slot.destination, dst::generatorParameter(0, VaGenerator::pulseWidth));
            expectWithinAbsoluteError(slot.amount, 0.3f, 1.0e-6f);
            expect(slot.via == ModSource::wheel);
            expect(slot.curve == ModCurve::unipolar);
        }
    }
};

static V3MatrixBankTests v3MatrixBankTests;
