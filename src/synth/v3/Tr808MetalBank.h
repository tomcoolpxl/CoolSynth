#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "CircuitWdf.h"
#include "Tr808BassDrum.h"

/*
    The TR-808 voicing board's shared parts (D254): the six square-wave oscillators the cowbell, the cymbal and both
    hats share, IC3's two band-passes over their mix, and IC2a, which narrows every metal hit's trigger to some 7 to
    14 V. From the service notes (pp. 6, 13) and tools/tr808/metal.cir, their reference.

    The oscillators are an HD14584's six Schmitt-trigger inverters, each with a resistor from its output back to its
    input and a capacitor to ground. As Werner, Abel and Smith take them (ICMC-SMC 2014, from the datasheet's typical
    values at 5 V): thresholds 2.7 and 2.1 V, the output 0 or 5 V, so each runs high for RC ln(2.9 / 2.3) and low for
    RC ln(2.7 / 2.1), a duty of 47.98 %: 205.3, 304.4, 369.6 and 522.7 Hz as drawn, the cowbell's two set by the
    trimmers TM1 and TM2 to 554 and 825 Hz, as both real units have them (D255; the service notes give 540 and 800).
    Their edges are exact in time and band-limited (a two-sample polyBLEP).

    IC3 mixes the six through 120 kOhm each into R53 (1 kOhm), then filters the mix twice, each a bridged-T in an
    op-amp's feedback: the high band (C11, R55, C15, C16, R58, R59) peaks at 7.09 kHz, the low band (C10, R52, C13,
    C14, R56, R57) at 3.43 kHz. A wave digital filter (CircuitWdf.h), its op-amps of 3 MHz gain-bandwidth; each band's
    capacitors are prewarped at its peak, which the bilinear transform would otherwise move 1.7 % down at 96 kHz.
*/
namespace coolsynth::synth::v3
{
    class Tr808MetalBank final
    {
    public:
        static constexpr int oscillatorCount = 6;
        static constexpr double outputHigh = 5.0;
        static constexpr double highOverRc = 0.23180161405732816; // ln((5 - 2.1) / (5 - 2.7))
        static constexpr double lowOverRc = 0.25131442828090605;  // ln(2.7 / 2.1)
        static constexpr double periodOverRc = highOverRc + lowOverRc;
        /** TM1 and TM2 as both units have them (D255; the service notes set 540 and 800 Hz). */
        static constexpr double cowbellLow = 554.0, cowbellHigh = 825.0;
        static constexpr double highBandPeak = 7094.0, lowBandPeak = 3432.0;
        static constexpr double opAmpBandwidth = 3.0e6;

        /** Each oscillator's RC as drawn, the first two (TM1 + R44 with C6, TM2 + R45 with C5) at 540 and 800 Hz. */
        static constexpr std::array<double, oscillatorCount> stockRc {
            1.0 / (cowbellLow * periodOverRc), 1.0 / (cowbellHigh * periodOverRc),
            220.0e3 * 18.0e-9, 680.0e3 * 10.0e-9, 560.0e3 * 10.0e-9, 560.0e3 * 18.0e-9 };

        /** The cowbell's trimmers' reach (TM1 0 to 220 kOhm past R44 150k with C6 22n, 254 to 627 Hz; TM2 past R45 100k
            with C5 18n, 359 to 1150 Hz): the two together, in semitones from 554 and 825 Hz, within both. */
        static constexpr double cowbellLowestSemitones = -12.0, cowbellHighestSemitones = 2.0;

        Tr808MetalBank() { buildBands(); }

        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            bands.setSampleRate(rate);
            setBandCapacitors();
            setTiming(1.0, 0.0);
            start({ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
        }

        /**
            The timing: the six capacitors scaled together (the Tune), the cowbell's trimmers moving both its
            oscillators this many semitones. A running oscillator keeps its place in its cycle.
        */
        void setTiming(double capacitorScale, double cowbellSemitones) noexcept
        {
            const double cb = std::pow(2.0, -std::clamp(cowbellSemitones, cowbellLowestSemitones, cowbellHighestSemitones) / 12.0);
            for (int k = 0; k < oscillatorCount; ++k)
            {
                auto& o = oscillators[static_cast<std::size_t>(k)];
                const double rc = stockRc[static_cast<std::size_t>(k)] * capacitorScale * (k < 2 ? cb : 1.0);
                const double place = o.period > 0.0 ? o.time / o.period : 0.0;
                o.high = rc * highOverRc;
                o.period = rc * periodOverRc;
                o.time = place * o.period;
            }
        }

        /**
            Each oscillator at this place in its cycle (0 a rising edge; high for its first 47.98 %), IC3 settled with
            them standing: a start from silence, as the reference's operating point at time 0.
        */
        void start(const std::array<double, oscillatorCount>& phases) noexcept
        {
            for (int k = 0; k < oscillatorCount; ++k)
            {
                auto& o = oscillators[static_cast<std::size_t>(k)];
                const double place = phases[static_cast<std::size_t>(k)] - std::floor(phases[static_cast<std::size_t>(k)]);
                o.time = place * o.period;
                o.carry = 0.0;
            }
            // IC3 at its operating point: C10 and C11 hold the mix (the op-amps' minus inputs at ground), the rest none.
            bands.reset();
            double current = 0.0;
            for (int k = 0; k < oscillatorCount; ++k)
            {
                bands.setInput(inputs[static_cast<std::size_t>(k)], levelOf(k));
                current += levelOf(k) / 120.0e3;
            }
            const double mix = current / (oscillatorCount / 120.0e3 + 1.0 / 1.0e3);
            bands.setCapacitorVoltage(c10, mix);
            bands.setCapacitorVoltage(c11, mix);
            outputs.fill(0.0);
            highBand = lowBand = 0.0;
        }

        /** One sample: the oscillators' outputs (volts, band-limited) and IC3's two bands. */
        void process() noexcept
        {
            const double dt = 1.0 / sampleRate;
            for (int k = 0; k < oscillatorCount; ++k)
            {
                auto& o = oscillators[static_cast<std::size_t>(k)];
                const double level = o.time < o.high ? outputHigh : 0.0;
                double out = level + o.carry;
                o.carry = 0.0;
                // An edge before the next sample, at a fraction d of the way: this sample takes h (1 - d)^2 / 2 of
                // its step h, the next -h d^2 / 2 on top of the new level (polyBLEP).
                double next = o.time + dt;
                double edge = -1.0, step = 0.0;
                if (o.time < o.high && next >= o.high)
                {
                    edge = o.high;
                    step = -outputHigh;
                }
                else if (next >= o.period)
                {
                    edge = o.period;
                    step = outputHigh;
                }
                if (edge >= 0.0)
                {
                    const double d = std::clamp((edge - o.time) / dt, 0.0, 1.0);
                    out += step * 0.5 * (1.0 - d) * (1.0 - d);
                    o.carry = -step * 0.5 * d * d;
                }
                if (next >= o.period)
                    next -= o.period;
                o.time = next;
                outputs[static_cast<std::size_t>(k)] = out;
                bands.setInput(inputs[static_cast<std::size_t>(k)], out);
            }
            bands.process();
            highBand = bands.probe(highProbe);
            lowBand = bands.probe(lowProbe);
        }

        /** An oscillator's output this sample, volts (0 the cowbell's 540 Hz, 1 its 800 Hz). */
        [[nodiscard]] double oscillator(int k) const noexcept { return outputs[static_cast<std::size_t>(k)]; }
        /** An oscillator's level as it stands before the next sample (0 or 5 V): a start's operating point. */
        [[nodiscard]] double standing(int k) const noexcept { return levelOf(k); }
        [[nodiscard]] double high() const noexcept { return highBand; }
        [[nodiscard]] double low() const noexcept { return lowBand; }
        [[nodiscard]] double mixVolts() const noexcept { return bands.probe(sumProbe); }
        [[nodiscard]] bool wellPosed() noexcept { return bands.isWellPosed(); }
        [[nodiscard]] double frequencyOf(int k) const noexcept { return 1.0 / oscillators[static_cast<std::size_t>(k)].period; }
        [[nodiscard]] double dutyOf(int k) const noexcept
        {
            const auto& o = oscillators[static_cast<std::size_t>(k)];
            return o.high / o.period;
        }

        /** A capacitor scaled so the bilinear transform puts a peak at this frequency where the circuit has it. */
        [[nodiscard]] static double prewarp(double hz, double rate) noexcept
        {
            const double x = 3.141592653589793 * hz / rate;
            return x < 1.0e-6 ? 1.0 : x / std::tan(std::min(x, 1.5));
        }

    private:
        struct Oscillator
        {
            double high = 0.001, period = 0.002; // seconds
            double time = 0.0;                   // into its cycle at this sample
            double carry = 0.0;                  // the last edge's share of this sample
        };

        [[nodiscard]] double levelOf(int k) const noexcept
        {
            const auto& o = oscillators[static_cast<std::size_t>(k)];
            return o.time < o.high ? outputHigh : 0.0;
        }

        // 1 to 6 the oscillators (inputs), 7 the mix, then the high band: 8 after C11, 9 the op-amp's minus, 10 the
        // bridged-T's middle, 11 its output; the low band: 12 after C10, 13 minus, 14 middle, 15 output; 16 and 17
        // the op-amps' inner nodes.
        void buildBands()
        {
            for (int k = 0; k < oscillatorCount; ++k)
                inputs[static_cast<std::size_t>(k)] = bands.addInput(k + 1);
            for (int k = 0; k < oscillatorCount; ++k)
                bands.addResistor(k + 1, 7, 120.0e3);             // R50, R48, R46, R39, R37, R35
            bands.addResistor(7, 0, 1.0e3);                        // R53
            c11 = bands.addCapacitor(7, 8, 1.0e-9);                // C11
            bands.addResistor(8, 9, 22.0e3);                       // R55
            c15 = bands.addCapacitor(9, 10, 3.3e-9);               // C15
            c16 = bands.addCapacitor(10, 11, 3.3e-9);              // C16
            bands.addResistor(10, 0, 560.0);                       // R58
            bands.addResistor(9, 11, 82.0e3);                      // R59
            bands.addOpAmp(0, 9, 11, 16, opAmpBandwidth);          // IC3b
            c10 = bands.addCapacitor(7, 12, 3.3e-9);               // C10
            bands.addResistor(12, 13, 33.0e3);                     // R52
            c13 = bands.addCapacitor(13, 14, 6.8e-9);              // C13
            c14 = bands.addCapacitor(14, 15, 6.8e-9);              // C14
            bands.addResistor(14, 0, 560.0);                       // R56
            bands.addResistor(13, 15, 82.0e3);                     // R57
            bands.addOpAmp(0, 13, 15, 17, opAmpBandwidth);         // IC3a
            sumProbe = bands.addProbe(7);
            highProbe = bands.addProbe(11);
            lowProbe = bands.addProbe(15);
        }

        void setBandCapacitors() noexcept
        {
            const double h = prewarp(highBandPeak, sampleRate), l = prewarp(lowBandPeak, sampleRate);
            bands.setCapacitance(c11, 1.0e-9 * h);
            bands.setCapacitance(c15, 3.3e-9 * h);
            bands.setCapacitance(c16, 3.3e-9 * h);
            bands.setCapacitance(c10, 3.3e-9 * l);
            bands.setCapacitance(c13, 6.8e-9 * l);
            bands.setCapacitance(c14, 6.8e-9 * l);
        }

        wdf::Circuit<17, 23, 6, 2, 1, 3> bands; // no devices: one nonlinear slot so addOpAmp's unused clamp compiles
        std::array<Oscillator, oscillatorCount> oscillators {};
        std::array<int, oscillatorCount> inputs {};
        std::array<double, oscillatorCount> outputs {};
        int c10 = 0, c11 = 0, c13 = 0, c14 = 0, c15 = 0, c16 = 0;
        int sumProbe = 0, highProbe = 0, lowProbe = 0;
        double sampleRate = 96000.0;
        double highBand = 0.0, lowBand = 0.0;
    };

    /**
        IC2a (D254): the TRIG line through R19, and through R18 clamped by D1 then R20, onto its plus input (R21 to
        ground), a gain of 31 (R23 over R22), its output swing +-14 V: some 7 V at an unaccented 4 V, 14 V from about
        12.7 V up. Its output passes to each instrument while the instrument's switch is on; it is solved at each hit's
        level once (its own bandwidth, some 100 kHz, is past a sample).
    */
    class Tr808MetalTriggerBuffer final
    {
    public:
        Tr808MetalTriggerBuffer()
        {
            trig = circuit.addInput(1);
            circuit.addResistor(1, 2, 3.3e3);                                                                  // R18
            circuit.addDiode(2, 0, Tr808BassDrum::diodeSaturation, Tr808BassDrum::thermal * Tr808BassDrum::diodeEmission); // D1
            circuit.addResistor(1, 3, 100.0e3);                                                                // R19
            circuit.addResistor(2, 3, 15.0e3);                                                                 // R20
            circuit.addResistor(3, 0, 3.3e3);                                                                  // R21
            circuit.addResistor(4, 0, 3.3e3);                                                                  // R22
            circuit.addResistor(4, 5, 100.0e3);                                                                // R23
            circuit.addOpAmp(3, 4, 5, 6, Tr808MetalBank::opAmpBandwidth, 14.0);                                // IC2a
            out = circuit.addProbe(5);
            circuit.setNewtonLimit(60);
        }

        /** IC2a's output at a TRIG of this many volts (solved from the same start each time, so it is the same bits). */
        double volts(double trigVolts) noexcept
        {
            if (trigVolts != lastTrig)
            {
                lastTrig = trigVolts;
                circuit.restore({});
                circuit.setInput(trig, trigVolts);
                circuit.settle();
                lastOut = circuit.probe(out);
            }
            return lastOut;
        }

        void reset() noexcept { lastTrig = -1.0; }

    private:
        wdf::Circuit<6, 10, 1, 1, 2, 1> circuit;
        int trig = 0, out = 0;
        double lastTrig = -1.0, lastOut = 0.0;
    };
}
