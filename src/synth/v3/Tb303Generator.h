#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    The TB-303 generator (D242, TODO 32): the Roland TB-303 Bass Line's voice, the Chicago acid bass. Its knobs are
    the 303's own: Waveform (saw to square), Cutoff, Resonance, Env Mod, Decay, Accent, and the slide's time to 95 %
    (66 ms on the 303: an RC of 22 ms). A note at velocity 100 of 127 or more is accented, as the 303's steps are: louder, its filter envelope
    shorter and its cutoff swept higher, and repeated accents climb. A note that arrives while another is held (the
    layer in legato) slides to it without a new envelope, as the 303's slide does.

    Rewritten from Open303 (Robin Schmidt, 2009, MIT), whose sound it keeps: the 303's saw and its square (the saw
    through a tanh with an offset, phase-aligned to it) as band-limited tables; the filter as a ladder of four coupled
    one-pole stages, unbuffered from each other as the 303's diode ladder is, with a high-pass in its feedback and a
    feedback that self-oscillates near 17 (after mystran and kunn on KVR); a decay envelope whose cutoff mapping was
    measured on a real 303; the accent's sweep through a 15 ms RC; high-passes before and after the filter, an
    all-pass and a notch, as Open303 has them. Changed for CoolSynth: the tables are built once for the process and
    shared (Open303 kept two of some 210 kB in every instance); the note list is the layer's (Open303's allocated);
    the core runs 2x inside the voice's 96 kHz, near Open303's own design point (4x its 44.1 kHz), with its
    quarter-band filter, so the whole stays band-limited to 24 kHz; the output is two samples late, as every
    generator's.

    Open303's licence, as it asks to be kept with substantial portions of it:

        The MIT License. Copyright (c) 2009 Robin Schmidt (www.rs-met.com)

        Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
        associated documentation files (the "Software"), to deal in the Software without restriction, including
        without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
        copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
        following conditions:

        The above copyright notice and this permission notice shall be included in all copies or substantial
        portions of the Software.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
        LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
        EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
        USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
namespace coolsynth::synth::v3
{
    namespace tb303
    {
        /** The 303's saw and square as band-limited tables, twelve octaves each: the first table as drawn, each next
            one with the upper half of the last one's harmonics gone. Built once (warmUp), shared by every voice. */
        struct Tables
        {
            static constexpr int length = 2048, levels = 12;
            std::array<std::array<float, length + 4>, levels> saw {}, square {};
        };

        inline void bandLimit(const std::vector<double>& prototype, std::array<std::array<float, Tables::length + 4>, Tables::levels>& out)
        {
            constexpr int n = Tables::length;
            // The prototype's harmonics, once (a plain DFT: this runs off the audio thread, once for the process).
            std::vector<std::complex<double>> spectrum(static_cast<std::size_t>(n / 2), 0.0);
            const double step = 2.0 * juce::MathConstants<double>::pi / n;
            for (int k = 1; k < n / 2; ++k)
            {
                std::complex<double> sum = 0.0;
                for (int i = 0; i < n; ++i)
                    sum += prototype[static_cast<std::size_t>(i)] * std::polar(1.0, -step * k * i);
                spectrum[static_cast<std::size_t>(k)] = sum * (2.0 / n);
            }
            for (int t = 0; t < Tables::levels; ++t)
            {
                auto& table = out[static_cast<std::size_t>(t)];
                if (t == 0)
                    for (int i = 0; i < n; ++i)
                        table[static_cast<std::size_t>(i)] = static_cast<float>(prototype[static_cast<std::size_t>(i)]);
                else
                {
                    const int highest = n >> t; // harmonics below this one stay (DC and Nyquist never)
                    for (int i = 0; i < n; ++i)
                    {
                        double value = 0.0;
                        for (int k = 1; k < highest; ++k)
                        {
                            const auto& c = spectrum[static_cast<std::size_t>(k)];
                            const double angle = step * k * i;
                            value += c.real() * std::cos(angle) - c.imag() * std::sin(angle);
                        }
                        table[static_cast<std::size_t>(i)] = static_cast<float>(value);
                    }
                }
                for (int i = 0; i < 4; ++i) // for the interpolation past the end
                    table[static_cast<std::size_t>(n + i)] = table[static_cast<std::size_t>(i)];
            }
        }

        inline const Tables& tables()
        {
            static const Tables built = []
            {
                Tables t;
                constexpr int n = Tables::length;
                // The 303's saw: rising from 0 to 1 over the first half, from -1 to 0 over the second.
                const int n1 = std::clamp(static_cast<int>(std::lround(0.5 * (n - 1))), 1, n - 1), n2 = n - n1;
                std::vector<double> saw(static_cast<std::size_t>(n));
                for (int i = 0; i < n1; ++i)
                    saw[static_cast<std::size_t>(i)] = static_cast<double>(i) / (n1 - 1);
                for (int i = n1; i < n; ++i)
                    saw[static_cast<std::size_t>(i)] = -1.0 + static_cast<double>(i - n1) / n2;
                bandLimit(saw, t.saw);
                // Its square: the saw through a steep tanh with an offset (36.9 dB, 4.37), inverted, then turned half a
                // period so that it lines up with the saw when the two are blended.
                std::vector<double> square(static_cast<std::size_t>(n));
                const double factor = std::pow(10.0, 36.9 / 20.0);
                for (int i = 0; i < n; ++i)
                    square[static_cast<std::size_t>((i + n / 2) % n)] = -std::tanh(factor * saw[static_cast<std::size_t>(i)] + 4.37);
                bandLimit(square, t.square);
                return t;
            }();
            return built;
        }

        /** A one-pole high-pass (dspguide's), or an all-pass (DAFX's). */
        struct OnePole
        {
            double b0 = 1.0, b1 = 0.0, a1 = 0.0, x1 = 0.0, y1 = 0.0;
            void highPass(double hz, double rate) noexcept
            {
                const double x = std::exp(-2.0 * juce::MathConstants<double>::pi * hz / rate);
                b0 = 0.5 * (1.0 + x);
                b1 = -b0;
                a1 = x;
            }
            void allPass(double hz, double rate) noexcept
            {
                const double t = std::tan(juce::MathConstants<double>::pi * hz / rate);
                const double x = (t - 1.0) / (t + 1.0);
                b0 = x;
                b1 = 1.0;
                a1 = -x;
            }
            double run(double in) noexcept
            {
                y1 = b0 * in + b1 * x1 + a1 * y1;
                x1 = in;
                return y1;
            }
            void clear() noexcept { x1 = y1 = 0.0; }
        };

        /** A biquad from Robert Bristow-Johnson's cookbook: a band-reject or a two-pole low-pass. */
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            void bandReject(double hz, double octaves, double rate) noexcept
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate, s = std::sin(w), c = std::cos(w);
                const double alpha = s * std::sinh(0.5 * std::log(2.0) * octaves * w / s);
                const double scale = 1.0 / (1.0 + alpha);
                a1 = 2.0 * c * scale;
                a2 = (alpha - 1.0) * scale;
                b0 = scale;
                b1 = -2.0 * c * scale;
                b2 = scale;
            }
            void lowPass(double hz, double q, double rate) noexcept
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate, s = std::sin(w), c = std::cos(w);
                const double alpha = s / (2.0 * q);
                const double scale = 1.0 / (1.0 + alpha);
                a1 = 2.0 * c * scale;
                a2 = (alpha - 1.0) * scale;
                b1 = (1.0 - c) * scale;
                b0 = b2 = 0.5 * b1;
            }
            double run(double in) noexcept
            {
                const double y = b0 * in + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = y;
                return y;
            }
            void clear() noexcept { x1 = x2 = y1 = y2 = 0.0; }
        };

        /** Open303's twelfth-order elliptic low-pass at a quarter of its rate: here, inside the 2x core, 24 kHz. */
        struct QuarterBand
        {
            std::array<double, 12> w {};
            double run(double in) noexcept
            {
                static constexpr double a[12] { -9.1891604652189471, 40.177553696870497, -110.11636661771178, 210.18506612078195,
                                                -293.84744771903240, 308.16345558359234, -244.06786780384243, 144.81877911392738,
                                                -62.770692151724198, 18.867762095902137, -3.5327094230551848, 0.31183189275203149 };
                static constexpr double b[13] { 0.00013671732099945628, -0.00055538501265606384, 0.0013681887636296387,
                                                -0.0022158566490711852, 0.0028320091007278322, -0.0029776933151090413,
                                                0.0030283628243514991, -0.0029776933151090413, 0.0028320091007278331,
                                                -0.0022158566490711861, 0.0013681887636296393, -0.00055538501265606384,
                                                0.00013671732099945636 };
                double v = in;
                for (int i = 0; i < 12; ++i)
                    v -= a[i] * w[static_cast<std::size_t>(i)];
                double y = b[0] * v;
                for (int i = 0; i < 12; ++i)
                    y += b[i + 1] * w[static_cast<std::size_t>(i)];
                for (int i = 11; i > 0; --i)
                    w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 1)];
                w[0] = v;
                return y;
            }
            void clear() noexcept { w.fill(0.0); }
        };

        /** The filter: four coupled one-pole stages (each feeding back into the one before, as the 303's unbuffered
            diode stages do), the feedback through a 150 Hz high-pass, its amount to 17 at full resonance. */
        struct Ladder
        {
            double y1 = 0.0, y2 = 0.0, y3 = 0.0, y4 = 0.0, b0 = 0.0, k = 0.0, g = 1.0;
            double skewedResonance = 0.0;
            OnePole feedbackHighPass;

            void setResonance(double amount) noexcept
            {
                const double r = std::clamp(amount, 0.0, 1.0);
                skewedResonance = (1.0 - std::exp(-3.0 * r)) / (1.0 - std::exp(-3.0));
            }
            void setCutoff(double hz, double rate) noexcept
            {
                const double cutoff = std::clamp(hz, 200.0, 20000.0);
                const double fx = 2.0 * juce::MathConstants<double>::pi * cutoff / rate / std::sqrt(2.0) / (2.0 * juce::MathConstants<double>::pi);
                b0 = (0.00045522346 + 6.1922189 * fx) / (1.0 + 12.358354 * fx + 4.4156345 * fx * fx);
                const double full = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997) - 5837.7917) - 476.47308) + 614.95611) + 213.87126) + 16.998792;
                const double r = skewedResonance;
                g = ((full / 17.0 - 1.0) * r + 1.0) * (1.0 + r);
                k = full * r;
            }
            double run(double in) noexcept
            {
                const double y0 = in - feedbackHighPass.run(k * y4);
                y1 += 2.0 * b0 * (y0 - y1 + y2);
                y2 += b0 * (y1 - 2.0 * y2 + y3);
                y3 += b0 * (y2 - 2.0 * y3 + y4);
                y4 += b0 * (y3 - 2.0 * y4);
                return 2.0 * g * y4;
            }
            void clear() noexcept
            {
                y1 = y2 = y3 = y4 = 0.0;
                feedbackHighPass.clear();
            }
        };
    }

    class Tb303Generator final
    {
    public:
        enum Param : int { waveform = 0, cutoff, resonance, envMod, decay, accent, slide, paramCount };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr int oversampling = 2;               // the core's rate over the voice's
        static constexpr float accentVelocity = 100.0f / 127.0f;
        static constexpr double outputGain = 0.4;            // a held saw note, the filter open, peaks near 0.9; an accent near 1

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "TB-303", 1,
                {{ { "wave",   "Waveform",  0.0f,   1.0f,    0.0f,   Skew::linear,      Unit::percent,      true },
                   { "cutoff", "Cutoff",    314.0f, 2394.0f, 870.0f, Skew::logarithmic, Unit::hertz,        true },
                   { "res",    "Resonance", 0.0f,   1.0f,    0.5f,   Skew::linear,      Unit::percent,      true },
                   { "envmod", "Env Mod",   0.0f,   1.0f,    0.5f,   Skew::linear,      Unit::percent,      true },
                   { "decay",  "Decay",     200.0f, 2000.0f, 630.0f, Skew::logarithmic, Unit::milliseconds, true },
                   { "accent", "Accent",    0.0f,   1.0f,    0.5f,   Skew::linear,      Unit::percent,      true },
                   { "slide",  "Slide",     0.0f,   500.0f,  66.0f,  Skew::squared,     Unit::milliseconds, true } }},
                paramCount };
            return d;
        }

        /** Builds the shared tables once for the process (call it off the audio thread: Part::prepare does). */
        static void warmUp() { (void) tb303::tables(); }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            rate = std::max(1000.0, newSampleRate);
            coreRate = rate * oversampling;
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            highPassIn.highPass(44.486, coreRate);
            ladder.feedbackHighPass.highPass(150.0, coreRate);
            highPassOut.highPass(24.167, rate);
            allPass.allPass(14.008, rate);
            notch.bandReject(7.5164, 4.7, rate);
            deClicker.lowPass(200.0, std::sqrt(0.5), rate);
            accentRc = std::exp(-1.0 / (rate * 0.015)); // the accent's sweep through 15 ms
            ampDecayCoefficient = 1.0 - std::exp(-1.0 / (rate * 1.230)); // the VCA's own fall, 1.23 s
            reset();
        }

        void reset() noexcept
        {
            phase = 0.0;
            ladder.clear();
            highPassIn.clear();
            highPassOut.clear();
            allPass.clear();
            notch.clear();
            quarterBand.clear();
            deClicker.clear();
            filterEnvelope = 0.0;
            accentSweep = 0.0;
            ampLevel = 0.0;
            ampHeld = false;
            ampStage = AmpStage::idle;
            accentGain = 0.0;
            accented = false;
            pitch = target = 0.0;
            snapPitch = true;
            idle = true;
            ring.fill(0.0f);
            cursor = 0;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            input = {};
        }

        /** A new note: the envelopes start again, the pitch jumps to it (a slide is legato()). */
        void noteOn(const NoteContext& note) noexcept { trigger(note.velocity); }

        /** The same voice struck again (mono, not legato): as a new note. */
        void strike(float velocity) noexcept { trigger(velocity); }

        /** The layer in legato: another key while one is held slides to it, the envelopes running on; its accent counts. */
        void legato(int, float velocity) noexcept
        {
            setAccent(velocity >= accentVelocity);
            idle = false;
        }

        /** Key up: the VCA lets go (1 ms, or 50 ms after an accent). */
        void noteOff() noexcept
        {
            ampHeld = false;
            if (ampStage != AmpStage::idle)
                ampStage = AmpStage::release;
        }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        [[nodiscard]] bool silentUntilNextNote() const noexcept { return idle; }

        /** The oscillator's pitch now, after the slide (for tests). */
        [[nodiscard]] double pitchHz() const noexcept { return pitch; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const auto& tables = tb303::tables();
            const double blend = std::clamp(static_cast<double>(params.p[waveform]), 0.0, 1.0);
            const double nominalCutoff = std::clamp(static_cast<double>(params.p[cutoff]), 314.0, 2394.0);
            ladder.setResonance(static_cast<double>(params.p[resonance]));
            const double envAmount = std::clamp(static_cast<double>(params.p[envMod]), 0.0, 1.0);
            normalDecayMs = std::clamp(static_cast<double>(params.p[decay]), 200.0, 2000.0);
            accentAmount = std::clamp(static_cast<double>(params.p[accent]), 0.0, 1.0);
            const double slideMs = std::clamp(static_cast<double>(params.p[slide]), 0.0, 500.0);
            if (accented)
                accentGain = accentAmount;
            setFilterDecay();

            // The cutoff's envelope mapping, as Open303 measured it on a 303: its scale and offset move with Env Mod and
            // with where the cutoff sits between its lowest (313.8 Hz) and highest (2394.4 Hz).
            const double c = std::log(nominalCutoff / 313.8152786059267) / std::log(2394.411986817546 / 313.8152786059267);
            const double low = 3.773996325111173 * envAmount + 0.736965594166206;
            const double high = 4.194548788411135 * envAmount + 0.864344900642434;
            const double envScale = (1.0 - c) * low + c * high;
            const double envOffset = 0.048292930943553 * c + 0.294391201442418;

            target = std::clamp(static_cast<double>(params.frequencyHz), 5.0, 0.2 * coreRate);
            if (snapPitch)
            {
                pitch = target;
                snapPitch = false;
            }
            // The slide: a leaky integrator on the pitch, Slide the time to 95 % (three time constants). The 303's is an RC
            // of 100 kOhm and 220 nF, 22 ms, done by some 66 ms (Open303 used 12 ms).
            const double slideCoefficient = slideMs > 0.0 ? std::exp(-1.0 / (rate * 0.001 * slideMs / 3.0)) : 0.0;
            const double releaseCoefficient = 1.0 - std::exp(-1.0 / (rate * 0.001 * (accentedRelease ? 50.0 : 1.0)));

            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const double gain = outputGain * static_cast<double>(level);

            for (int s = 0; s < numSamples; ++s)
            {
                double value = 0.0;
                if (! idle)
                {
                    pitch = target + slideCoefficient * (pitch - target);
                    const double increment = tb303::Tables::length * pitch / coreRate;

                    // The filter's envelope and the accent's sweep set this sample's cutoff.
                    filterEnvelope *= filterFall;
                    accentSweep = (accentGain > 0.0 ? filterEnvelope : 0.0) + accentRc * (accentSweep - (accentGain > 0.0 ? filterEnvelope : 0.0));
                    const double octaves = envScale * (filterEnvelope - envOffset) + accentGain * accentSweep;
                    ladder.setCutoff(nominalCutoff * std::exp2(octaves), coreRate);

                    // The VCA: its own envelope, plus the filter envelope's share while the key is held (more after an accent).
                    switch (ampStage)
                    {
                        case AmpStage::decay: ampLevel += ampDecayCoefficient * (0.0 - ampLevel); break;
                        case AmpStage::release: ampLevel += releaseCoefficient * (0.0 - ampLevel); break;
                        case AmpStage::idle: break;
                    }
                    double amp = ampLevel;
                    if (ampHeld)
                        amp += (0.45 + 4.0 * accentGain) * filterEnvelope;
                    amp = deClicker.run(amp);

                    // The core, twice per sample: the oscillator, the high-pass before the filter, the filter, the
                    // quarter-band low-pass; the second result is kept.
                    const int table = std::clamp(std::ilogb(increment) + 2, 0, tb303::Tables::levels - 1);
                    const auto& sawTable = tables.saw[static_cast<std::size_t>(table)];
                    const auto& squareTable = tables.square[static_cast<std::size_t>(table)];
                    double core = 0.0;
                    for (int o = 0; o < oversampling; ++o)
                    {
                        while (phase >= tb303::Tables::length)
                            phase -= tb303::Tables::length;
                        const int i = static_cast<int>(phase);
                        const double f = phase - i;
                        const double saw = sawTable[static_cast<std::size_t>(i)] + f * (sawTable[static_cast<std::size_t>(i + 1)] - sawTable[static_cast<std::size_t>(i)]);
                        const double square = squareTable[static_cast<std::size_t>(i)] + f * (squareTable[static_cast<std::size_t>(i + 1)] - squareTable[static_cast<std::size_t>(i)]);
                        phase += increment;
                        double x = -((1.0 - blend) * saw + blend * 0.5 * square);
                        x = highPassIn.run(x);
                        x = ladder.run(x);
                        core = quarterBand.run(x);
                    }
                    core = allPass.run(core);
                    core = highPassOut.run(core);
                    core = notch.run(core);
                    value = core * amp * gain;

                    if (! ampHeld && ampStage == AmpStage::release && ampLevel < 1.0e-7 && std::abs(value) < 1.0e-7)
                        idle = true;
                }

                // Two samples late, as every generator.
                const unsigned int now = cursor & 3u;
                ring[now] = static_cast<float>(value);
                float out = ring[(cursor + 2u) & 3u];
                ++cursor;
                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    out *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = out;
                right[s] = out;
                signalTrack[static_cast<std::size_t>(s)] = out;
            }

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            for (int s = 0; s < numSamples; ++s)
            {
                left[s] *= gainLeft;
                right[s] *= gainRight;
            }
        }

    private:
        enum class AmpStage { idle, decay, release };
        static constexpr float level = 1.0f;

        void setAccent(bool on) noexcept
        {
            accented = on;
            accentGain = on ? accentAmount : 0.0;
            accentedRelease = on;
            setFilterDecay();
        }

        /** The filter envelope's fall: Decay for a plain note, 200 ms after an accent (the 303's). */
        void setFilterDecay() noexcept
        {
            const double ms = accented ? 200.0 : normalDecayMs;
            filterFall = std::exp(-1.0 / (0.001 * ms * rate));
        }

        void trigger(float velocity) noexcept
        {
            if (idle)
            {
                // From silence the oscillator and the filters start afresh; from a ringing note they run on (no click).
                phase = 0.0;
                ladder.clear();
                highPassIn.clear();
                highPassOut.clear();
                allPass.clear();
                notch.clear();
                quarterBand.clear();
                deClicker.clear();
            }
            setAccent(velocity >= accentVelocity);
            filterEnvelope = 1.0 / filterFall; // the first sample reads 1
            ampLevel = std::max(ampLevel, 1.0);
            ampStage = AmpStage::decay;
            ampHeld = true;
            snapPitch = true;
            idle = false;
        }

        double rate = 96000.0, coreRate = 192000.0;
        double phase = 0.0;
        tb303::Ladder ladder;
        tb303::OnePole highPassIn, highPassOut, allPass;
        tb303::Biquad notch, deClicker;
        tb303::QuarterBand quarterBand;
        double filterEnvelope = 0.0, filterFall = 0.999, accentSweep = 0.0, accentRc = 0.99;
        double ampLevel = 0.0, ampDecayCoefficient = 1.0e-5;
        AmpStage ampStage = AmpStage::idle;
        bool ampHeld = false;
        double accentGain = 0.0, accentAmount = 0.5, normalDecayMs = 630.0;
        bool accented = false, accentedRelease = false;
        double pitch = 0.0, target = 0.0;
        bool snapPitch = true, idle = true;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        GeneratorLink input;
    };
}
