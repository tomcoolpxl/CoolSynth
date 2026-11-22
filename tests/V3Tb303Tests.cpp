#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#include "synth/v3/Tb303Generator.h"
#include "synth/v3/Voice.h"

/*
    The TB-303 generator (D242): its registration, its pitch and level, the accent and the slide, and checks against
    published figures: the filter's slope (Stinchcombe: four poles, 24 dB/oct) and self-oscillation near a feedback
    of 17, the slide's time constant (some 22 ms, by owners' measurements), the square's duty (about 45 % on the
    x0xb0x clone).
*/
namespace
{
    using namespace coolsynth::synth::v3;
    using Tb = Tb303Generator;

    constexpr int kSubBlock = 32;
    constexpr double kVoiceRate = 96000.0;

    struct Rig
    {
        SlotMemory memory;
        Tb303Generator generator;
    };

    std::unique_ptr<Rig> makeRig()
    {
        auto r = std::make_unique<Rig>();
        r->memory.allocate(Tb::memoryFloats(kVoiceRate, kSubBlock));
        r->generator.prepare(kVoiceRate, kSubBlock, r->memory);
        return r;
    }

    SlotParams defaults(int note)
    {
        SlotParams p;
        const auto& d = Tb::descriptor();
        for (int i = 0; i < d.usedParams; ++i)
            p.p[static_cast<std::size_t>(i)] = d.params[static_cast<std::size_t>(i)].def;
        p.frequencyHz = static_cast<float>(440.0 * std::exp2((note - 69) / 12.0));
        return p;
    }

    void render(Tb& g, const SlotParams& p, int samples, std::vector<float>& out, std::vector<double>* pitch = nullptr)
    {
        std::array<float, kSubBlock> l {}, r {};
        for (int s = 0; s < samples; s += kSubBlock)
        {
            const int n = std::min(kSubBlock, samples - s);
            g.process(l.data(), r.data(), n, p);
            for (int i = 0; i < n; ++i)
                out.push_back(g.link().signal[i]);
            if (pitch != nullptr)
                pitch->push_back(g.pitchHz());
        }
    }

    double peak(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (std::size_t i = from; i < std::min(to, x.size()); ++i)
            m = std::max(m, static_cast<double>(std::abs(x[i])));
        return m;
    }

    double frequencyIn(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        std::vector<double> crossings;
        for (std::size_t i = from + 1; i < std::min(to, x.size()); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back(static_cast<double>(i - 1) - x[i - 1] / static_cast<double>(x[i] - x[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return kVoiceRate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
    }

    /** The ladder's steady gain at a frequency, by a sine through it at the core's rate. */
    double ladderGain(double cutoffHz, double resonance, double hz)
    {
        tb303::Ladder ladder;
        const double rate = kVoiceRate * Tb::oversampling;
        ladder.feedbackHighPass.highPass(150.0, rate);
        ladder.setResonance(resonance);
        ladder.setCutoff(cutoffHz, rate);
        const int settle = static_cast<int>(rate * 0.2), measure = static_cast<int>(rate * 0.1);
        double top = 0.0;
        for (int i = 0; i < settle + measure; ++i)
        {
            const double y = ladder.run(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
            if (i >= settle)
                top = std::max(top, std::abs(y));
        }
        return top;
    }
}

class V3Tb303Tests final : public juce::UnitTest
{
public:
    V3Tb303Tests() : juce::UnitTest("V3 TB-303 generator", "V3") {}

    void runTest() override
    {
        beginTest("tb303_is_generator_type_twelve_with_the_303s_knobs");
        {
            expectEquals(GeneratorSlot::typeCount, 16); // the 808 Drums, the 808 Metal and the 808 Snare & Clap came after it (D253, D255, D257)
            expectEquals(juce::String(GeneratorSlot::descriptorFor(generatorType::tb303).name), juce::String("TB-303"));
            expectEquals(Tb::descriptor().usedParams, 7);
        }

        beginTest("a_note_plays_at_its_key_and_level");
        {
            auto rig = makeRig();
            NoteContext note;
            note.noteNumber = 36;
            note.velocity = 0.6f;
            rig->generator.noteOn(note);
            auto p = defaults(36);
            p.p[Tb::resonance] = 0.0f;
            p.p[Tb::envMod] = 0.0f;
            std::vector<float> out;
            render(rig->generator, p, static_cast<int>(0.6 * kVoiceRate), out);
            const double hz = frequencyIn(out, static_cast<std::size_t>(0.1 * kVoiceRate), out.size());
            const double top = peak(out, 0, out.size());
            logMessage("C2: " + juce::String(hz, 2) + " Hz (65.41 wanted), peak " + juce::String(top, 3));
            expectWithinAbsoluteError(hz, 65.406, 0.5, "the pitch");
            expect(top > 0.6 && top < 1.2, "a sensible level");

            // The panel's middle, a held note: the level the output gain is set for.
            auto mid = makeRig();
            mid->generator.noteOn(note);
            std::vector<float> held;
            render(mid->generator, defaults(36), static_cast<int>(0.6 * kVoiceRate), held);
            logMessage("the panel's middle, C2: peak " + juce::String(peak(held, 0, held.size()), 3));
        }

        beginTest("an_accent_is_on_or_off_at_velocity_100");
        {
            double tops[3] {};
            const float velocities[3] { 0.5f, 98.0f / 127.0f, 100.0f / 127.0f };
            for (int i = 0; i < 3; ++i)
            {
                auto rig = makeRig();
                NoteContext note;
                note.noteNumber = 36;
                note.velocity = velocities[i];
                rig->generator.noteOn(note);
                std::vector<float> out;
                render(rig->generator, defaults(36), static_cast<int>(0.2 * kVoiceRate), out);
                tops[i] = peak(out, 0, out.size());
            }
            logMessage("peaks at velocity 64, 98 and 100: " + juce::String(tops[0], 3) + " " + juce::String(tops[1], 3) + " " + juce::String(tops[2], 3));
            expectWithinAbsoluteError(tops[1], tops[0], 1.0e-6, "no accent below 100");
            expectGreaterThan(tops[2], 1.2 * tops[1], "an accent at 100");
        }

        beginTest("a_slide_glides_to_the_new_key_without_a_new_envelope");
        {
            auto rig = makeRig();
            NoteContext note;
            note.noteNumber = 36;
            note.velocity = 0.6f;
            rig->generator.noteOn(note);
            std::vector<float> out;
            std::vector<double> pitch;
            render(rig->generator, defaults(36), static_cast<int>(0.1 * kVoiceRate), out, &pitch);
            const double before = peak(out, out.size() - static_cast<std::size_t>(0.01 * kVoiceRate), out.size());
            rig->generator.legato(48, 0.6f);
            pitch.clear();
            const std::size_t at = out.size();
            render(rig->generator, defaults(48), static_cast<int>(0.2 * kVoiceRate), out, &pitch);
            // The time to 63 % of the change in hertz: the slide's time constant.
            double tau = 0.0;
            for (std::size_t b = 0; b < pitch.size(); ++b)
                if (pitch[b] >= 65.406 + 0.632 * 65.406)
                {
                    tau = (static_cast<double>(b) + 1.0) * kSubBlock / kVoiceRate;
                    break;
                }
            const double after = peak(out, at, at + static_cast<std::size_t>(0.01 * kVoiceRate));
            logMessage("slide (66 ms) to an octave up: time constant " + juce::String(1000.0 * tau, 1)
                       + " ms (the 303: 100 kOhm and 220 nF, 22 ms); level before/after " + juce::String(before, 3) + "/" + juce::String(after, 3));
            expectWithinAbsoluteError(1000.0 * tau, 22.0, 1.5, "the slide's time constant");
            expectLessThan(after, 1.5 * before, "no new envelope on a slide");
        }

        beginTest("the_filter_is_stinchcombes_and_rings_without_self_oscillating");
        {
            const double fc = 1000.0;
            const double g4 = ladderGain(fc, 0.0, 4.0 * fc), g8 = ladderGain(fc, 0.0, 8.0 * fc), g16 = ladderGain(fc, 0.0, 16.0 * fc);
            const double slope = 20.0 * std::log10(g16 / g8);
            juce::String response;
            for (const double ratio : { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 })
                response << " " << juce::String(ratio, 3) << ":" << juce::String(20.0 * std::log10(ladderGain(fc, 0.0, ratio * fc)), 1);
            logMessage("the ladder at 1 kHz, no resonance, dB by frequency ratio:" + response + "; 8 to 16 kHz: " + juce::String(slope, 1)
                       + " dB (Stinchcombe: four poles, 24 dB/oct); 4 to 8: " + juce::String(20.0 * std::log10(g8 / g4), 1));
            expectWithinAbsoluteError(slope, -24.0, 4.0, "the slope");
            // Against Stinchcombe's analysis of the 303's ladder, -1 / (s^4 + 6.727 s^3 + 14.142 s^2 + 9.514 s + 1), with
            // the frequency read as 1.2 f / fc and the level offset found: the shape within half a decibel.
            const auto stinchcombe = [](double w)
            {
                const std::complex<double> s(0.0, w);
                return 20.0 * std::log10(1.0 / std::abs(s * s * s * s + 6.727 * s * s * s + 14.142 * s * s + 9.514 * s + 1.0));
            };
            const double ratios[] { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
            double offset = 0.0;
            std::vector<double> measured;
            for (const double ratio : ratios)
            {
                measured.push_back(20.0 * std::log10(ladderGain(fc, 0.0, ratio * fc)));
                offset += measured.back() - stinchcombe(1.2 * ratio);
            }
            offset /= std::size(ratios);
            double worst = 0.0;
            for (std::size_t i = 0; i < std::size(ratios); ++i)
                worst = std::max(worst, std::abs(measured[i] - stinchcombe(1.2 * ratios[i]) - offset));
            logMessage("against Stinchcombe's response: within " + juce::String(worst, 2) + " dB (offset " + juce::String(offset, 1) + " dB)");
            expectLessThan(worst, 0.5, "the shape of Stinchcombe's response");

            // Full resonance: the ladder rings on by itself after a kick.
            tb303::Ladder ladder;
            const double rate = kVoiceRate * Tb::oversampling;
            ladder.feedbackHighPass.highPass(150.0, rate);
            ladder.setResonance(1.0);
            ladder.setCutoff(fc, rate);
            double late = 0.0, early = 0.0;
            for (int i = 0; i < static_cast<int>(rate * 0.5); ++i)
            {
                const double y = ladder.run(i == 0 ? 1.0 : 0.0);
                if (i > static_cast<int>(rate * 0.01) && i < static_cast<int>(rate * 0.05))
                    early = std::max(early, std::abs(y));
                if (i > static_cast<int>(rate * 0.45))
                    late = std::max(late, std::abs(y));
            }
            logMessage("full resonance, an impulse: peak 10-50 ms " + juce::String(early, 6) + ", 450-500 ms " + juce::String(late, 6)
                       + " (feedback " + juce::String(ladder.k, 2) + ")");
            expectGreaterThan(ladder.k, 16.0, "the feedback near 17 at full resonance");
            expectLessThan(late, early, "rings, but does not self-oscillate, as a stock 303");
        }

        beginTest("the_square_is_near_45_percent");
        {
            const auto& table = tb303::tables().square[0];
            const auto [lo, hi] = std::minmax_element(table.begin(), table.begin() + tb303::Tables::length);
            const double middle = 0.5 * (*lo + *hi);
            int above = 0;
            for (int i = 0; i < tb303::Tables::length; ++i)
                above += table[static_cast<std::size_t>(i)] > middle ? 1 : 0;
            const double duty = static_cast<double>(above) / tb303::Tables::length;
            logMessage("the square's duty " + juce::String(100.0 * duty, 1) + " % (the x0xb0x: about 45 %)");
            expectWithinAbsoluteError(duty, 0.45, 0.06, "the duty");
        }

        if (const auto path = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_303_DUMP", {}); path.isNotEmpty())
        {
            // The hardest note for aliasing: a high square, full resonance, envelope and accent, as raw samples.
            beginTest("dump");
            auto rig = makeRig();
            NoteContext note;
            note.noteNumber = 72;
            note.velocity = 1.0f;
            rig->generator.noteOn(note);
            auto p = defaults(72);
            p.p[Tb::waveform] = 1.0f;
            p.p[Tb::resonance] = 1.0f;
            p.p[Tb::envMod] = 1.0f;
            p.p[Tb::accent] = 1.0f;
            p.p[Tb::cutoff] = 2394.0f;
            std::vector<float> out;
            render(rig->generator, p, static_cast<int>(1.0 * kVoiceRate), out);
            juce::File(path).replaceWithData(out.data(), out.size() * sizeof(float));
            logMessage("wrote " + juce::String(static_cast<int>(out.size())) + " samples at " + juce::String(kVoiceRate, 0) + " Hz, core "
                       + juce::String(Tb::oversampling) + "x");
        }

        beginTest("a_released_note_ends_in_silence");
        {
            auto rig = makeRig();
            NoteContext note;
            note.noteNumber = 36;
            note.velocity = 0.6f;
            rig->generator.noteOn(note);
            std::vector<float> out;
            render(rig->generator, defaults(36), static_cast<int>(0.2 * kVoiceRate), out);
            expect(! rig->generator.silentUntilNextNote(), "sounding while held");
            rig->generator.noteOff();
            render(rig->generator, defaults(36), static_cast<int>(0.3 * kVoiceRate), out);
            expect(rig->generator.silentUntilNextNote(), "silent after the release");
            expectLessThan(peak(out, out.size() - 1000, out.size()), 1.0e-5, "nothing left");
        }
    }
};

static V3Tb303Tests v3Tb303Tests;
