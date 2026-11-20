#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"

/*
    The Roland TR-808 bass drum as its circuit (D235), after Kurt James Werner's 2016 thesis: four sub-circuits as
    the thesis divides them, each a wave digital filter with an R-type root (CircuitWdf.h), each driving the next
    one way, as an ideal source.

        Pulse shaper     the trigger through C40 and R163, R162 to ground, D53 clipping the falling edge
        Envelope gen.    Q41 and Q42 hold Q43 on for some 5 ms; C39, D52 and R161 as its load
        Resonator        the bridged-T (C41, C42, R166, R165, R167) around the first op-amp, Q43 shorting R165 in the
                         attack, the retrigger pulse through C39, D52 and R161, the feedback buffer (R164, VR6, C43,
                         R169) around the second op-amp back through R170: the Decay
        Output filter    the tone (R171, R172, VR5, C45), the level (VR4), the buffer Q44 as an ideal follower

    Component values from the thesis's tables; R164 is 47 kOhm as in the DAFx 2014 paper (the thesis's 4.7 kOhm
    makes the feedback loop's gain about 8 and the drum oscillate). Transistors as the thesis's 2N3904 substitute
    (Is 10 fA, betaF 300, betaR 4), diodes as its 1N4148 (Is 2.52 nA). The envelope generator's R159 is 106 kOhm and
    it has the thesis's added 6.8 nF C0, as there. Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808BassDrum final
    {
    public:
        static constexpr double thermal = 0.02585;
        static constexpr double transistorSaturation = 10.0e-15, betaF = 300.0, betaR = 4.0;
        static constexpr double diodeSaturation = 2.52e-9;
        static constexpr double supply = 15.0;
        static constexpr double nominalC41 = 0.015e-6, nominalC42 = 0.015e-6;

        Tr808BassDrum()
        {
            buildPulseShaper();
            buildEnvelope();
            buildResonator();
            buildOutput();
        }

        /**
            At a sample rate, at rest. Cheap enough for the audio thread, where a slot switching to the Kick prepares
            it: the rest state is solved once for the process (restState), and each circuit's analysis waits for its
            first sample.
        */
        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            pulse.setSampleRate(rate);
            envelope.setSampleRate(rate);
            resonator.setSampleRate(rate);
            output.setSampleRate(rate);
            rest();
        }

        /** Back to rest: a copy of the solved rest state, no solving. */
        void rest() noexcept
        {
            const RestState& r = restState();
            pulse.restore(r.pulse);
            envelope.restore(r.envelope);
            resonator.restore(r.resonator);
            output.restore(r.output);
            triggerTime = -1.0;
            pulseResting = envelopeResting = true;
            lastEnvelope = r.envelopeVolts;
            stillSamples = 0;
            resonator.setInput(resonatorEnvelope, r.envelopeVolts);
            resonator.setInput(resonatorPulse, 0.0);
            pulse.setInput(pulseIn, 0.0);
            envelope.setInput(envelopeIn, 0.0);
            envelope.setInput(envelopeSupply, supply);
            output.setInput(outputIn, 0.0);
        }

        /**
            The circuit's rest state, solved on first use and kept for the process: call it once off the audio thread
            (Part::prepare does) so no slot ever solves it there. It depends on neither the sample rate nor the knobs:
            the capacitors are open at rest, and the knobs are capacitors and resistors no direct current flows through.
        */
        static void warmUp() noexcept { restState(); }

        /** Back to the circuit at rest: capacitors charged as they are with no trigger. */
        void settle() noexcept
        {
            triggerTime = -1.0;
            pulse.setInput(pulseIn, 0.0);
            pulse.settle();
            envelope.setInput(envelopeIn, 0.0);
            envelope.setInput(envelopeSupply, supply);
            envelope.settle();
            const double eg = envelope.probe(envelopeOut);
            resonator.setInput(resonatorEnvelope, eg);
            resonator.setInput(resonatorPulse, 0.0);
            resonator.settle();
            output.setInput(outputIn, 0.0);
            output.settle();
            pulseResting = envelopeResting = true;
            lastEnvelope = eg;
            stillSamples = 0;
        }

        /** A hit: the trigger pulse at this many volts (4 unaccented to 14), this wide. */
        void trigger(double volts, double widthSeconds = 0.001) noexcept
        {
            triggerVolts = volts;
            triggerWidth = widthSeconds;
            triggerTime = 0.0;
        }

        /** Decay 0..1: VR6 from 0 to 500 kOhm; past 1 the extended-decay bend, VR6 up to 740 kOhm (the loop gain then nears the 0.94 where the drum would ring on). */
        void setDecay(double amount) noexcept { resonator.setResistance(vr6, std::max(1.0, std::clamp(amount, 0.0, 1.48) * 500.0e3)); }
        /** Tone 0..1: VR5 from 10 kOhm to 0 (the thesis's (1 - c) VR5). */
        void setTone(double amount) noexcept { output.setResistance(vr5, std::max(1.0, (1.0 - std::clamp(amount, 0.0, 1.0)) * 10.0e3)); }
        /**
            The Tone knob's turn, 0..1, as setTone's amount. VR5 is a C-taper pot (the service notes' parts list), so
            the share of it left in the circuit falls fast as the knob turns up: about a quarter at mid-travel, as
            fitted to Michael Fischer's recordings of a real 808, whose brightness steps evenly with the knob.
        */
        [[nodiscard]] static double toneForKnob(double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            return 1.0 - (std::pow(8.0, 1.0 - t) - 1.0) / 7.0;
        }
        /** Level 0..1: VR4's wiper. */
        void setLevel(double amount) noexcept
        {
            const double l = std::clamp(amount, 0.0, 1.0);
            output.setResistance(vr4Top, std::max(1.0, (1.0 - l) * 100.0e3));
            output.setResistance(vr4Bottom, std::max(1.0, l * 100.0e3));
        }
        /** The bridged-T's capacitors scaled together: the pitch by 1 / scale, its Q kept (the tuning mod). */
        void setTuning(double scale) noexcept
        {
            tuning = scale;
            resonator.setCapacitance(c41, nominalC41 * scale);
            resonator.setCapacitance(c42, nominalC42 * scale);
        }

        // The circuit bends the Kick's knobs reach (docs/research/kick-808-wdf.md).
        /** R166, the resistance left when Q43 shorts R165: smaller, a higher attack shift. Stock 6.8 kOhm. */
        void setAttackResistor(double ohms) noexcept
        {
            r166Ohms = std::max(10.0, ohms);
            resonator.setResistance(r166, r166Ohms);
        }
        /** C38 scaled: the envelope generator, and so the attack shift, lasts longer or shorter. Stock 0.1 uF. */
        void setEnvelopeScale(double scale) noexcept { envelope.setCapacitance(c38, 0.1e-6 * std::max(0.01, scale)); }
        /** R161, the leakage that makes the sigh: smaller, more sigh. Stock 1 MOhm. */
        void setLeakResistor(double ohms) noexcept
        {
            r161Ohms = std::clamp(ohms, 1.0e3, 1.0e9);
            resonator.setResistance(r161Resonator, r161Ohms);
            envelope.setResistance(r161Envelope, r161Ohms);
        }
        /** The op-amps' rails: the resonator's output clips softly near them. Stock 15 V. */
        void setRails(double volts) noexcept { rails = std::clamp(volts, 1.0, 15.0); }

        /** The settled pitch of the tail at a tuning, with Q43 off: the bridged-T's centre frequency. */
        [[nodiscard]] double tailHz(double scale) const noexcept
        {
            const double g = 1.0 / (47.0e3 + r166Ohms) + 1.0 / 470.0e3 + 1.0 / r161Ohms;
            const double c = scale * nominalC41;
            return std::sqrt(g / (c * c * 1.0e6)) / 6.283185307179586;
        }
        [[nodiscard]] double currentTuning() const noexcept { return tuning; }

        /** One sample of the whole drum; returns the output in volts. */
        double process() noexcept
        {
            double vin = 0.0;
            if (triggerTime >= 0.0)
            {
                const double edge = 4.0 / 44100.0; // the thesis's ramped edges
                const double t = triggerTime;
                if (t < edge)
                    vin = triggerVolts * t / edge;
                else if (t < triggerWidth)
                    vin = triggerVolts;
                else if (t < triggerWidth + edge)
                    vin = triggerVolts * (1.0 - (t - triggerWidth) / edge);
                else
                    triggerTime = -1.0;
                if (triggerTime >= 0.0)
                    triggerTime += 1.0 / sampleRate;
            }
            // The pulse shaper and the envelope rest once they have settled after a hit (their outputs still), so the
            // tail costs only the resonator and the output; a new hit wakes them.
            const bool active = triggerTime >= 0.0;
            if (active || ! pulseResting)
            {
                pulse.setInput(pulseIn, vin);
                pulse.process();
                const double v = pulse.probe(pulseOut);
                pulseResting = ! active && std::abs(v) < 1.0e-9;
            }
            if (active || ! envelopeResting)
            {
                envelope.setInput(envelopeIn, vin);
                envelope.process();
                const double v = envelope.probe(envelopeOut);
                stillSamples = std::abs(v - lastEnvelope) < 1.0e-6 ? stillSamples + 1 : 0; // above the solver's 0.1 uV
                lastEnvelope = v;
                envelopeResting = ! active && stillSamples > static_cast<int>(0.005 * sampleRate);
            }
            resonator.setInput(resonatorEnvelope, envelope.probe(envelopeOut));
            resonator.setInput(resonatorPulse, pulseResting ? 0.0 : pulse.probe(pulseOut));
            resonator.process();
            output.setInput(outputIn, clipToRails(resonator.probe(resonatorOut)));
            output.process();
            return output.probe(outputOut);
        }

        /** The op-amps' rails, as a soft clip of the resonator's output: untouched below 80 % of them. */
        [[nodiscard]] double clipToRails(double v) const noexcept
        {
            const double knee = 0.8 * rails;
            const double a = std::abs(v);
            if (a <= knee)
                return v;
            const double over = knee + (rails - knee) * std::tanh((a - knee) / (rails - knee));
            return v < 0.0 ? -over : over;
        }

        // The stages' last outputs, for tests against the reference.
        [[nodiscard]] double pulseVolts() const noexcept { return pulse.probe(pulseOut); }
        [[nodiscard]] double envelopeVolts() const noexcept { return envelope.probe(envelopeOut); }
        [[nodiscard]] double resonatorVolts() const noexcept { return resonator.probe(resonatorOut); }
        [[nodiscard]] int newtonSteps() const noexcept { return std::max(envelope.lastNewtonSteps(), resonator.lastNewtonSteps()); }
        [[nodiscard]] int envelopeNewtonSteps() const noexcept { return envelopeResting ? 0 : envelope.lastNewtonSteps(); }
        [[nodiscard]] int resonatorNewtonSteps() const noexcept { return resonator.lastNewtonSteps(); }
        [[nodiscard]] bool pulseIsResting() const noexcept { return pulseResting; }
        [[nodiscard]] bool envelopeIsResting() const noexcept { return envelopeResting; }

    private:
        // Pulse shaper: 1 vin, 2 npulse.
        void buildPulseShaper()
        {
            pulseIn = pulse.addInput(1);
            pulse.addCapacitor(1, 2, 0.015e-6, 0.029); // C40: the thesis's alpha transform (the bilinear one rings)
            pulse.addResistor(1, 2, 100.0e3);          // R163
            pulse.addResistor(2, 0, 4.7e3);            // R162
            pulse.addDiode(0, 2, diodeSaturation, thermal); // D53, its anode at ground
            pulseOut = pulse.addProbe(2);
        }

        // Envelope generator: 1 vin, 2 Q41 base, 3 Q41 collector, 4 supply, 5 Q42 collector (the output), 6 D52.
        void buildEnvelope()
        {
            envelopeIn = envelope.addInput(1);
            envelopeSupply = envelope.addInput(4);
            envelope.addResistor(1, 2, 8.2e3);     // R157
            envelope.addResistor(2, 0, 2.7e3);     // R158
            envelope.addCapacitor(2, 0, 6.8e-9);   // C0, added by the thesis for the solver
            envelope.addResistor(4, 3, 1.0e6);     // R156
            c38 = envelope.addCapacitor(3, 0, 0.1e-6); // C38
            envelope.addResistor(4, 5, 22.0e3);    // R160
            envelope.addCapacitor(5, 6, 0.033e-6); // C39
            r161Envelope = envelope.addResistor(6, 0, 1.0e6); // R161, to the resonator's top node, near ground
            envelope.addResistor(5, 0, 106.0e3);   // R159 to Q43's base, as the thesis tunes it
            envelope.addTransistor(3, 2, 0, transistorSaturation, betaF, betaR, thermal); // Q41
            envelope.addTransistor(5, 3, 0, transistorSaturation, betaF, betaR, thermal); // Q42
            envelope.addDiode(6, 0, diodeSaturation, thermal);                            // D52
            envelopeOut = envelope.addProbe(5);
        }

        // Resonator: 1 the envelope (input), 2 the shaped pulse (input), 3 D52, 4 the top of the T, 5 Q43's base,
        // 6 Q43's collector, 7 the first op-amp's minus, 8 its output, 9 the second op-amp's minus, 10 VR6 to C43,
        // 11 the second op-amp's output, 12 its plus.
        void buildResonator()
        {
            resonatorEnvelope = resonator.addInput(1);
            resonatorPulse = resonator.addInput(2);
            resonator.addCapacitor(1, 3, 0.033e-6);      // C39
            r161Resonator = resonator.addResistor(3, 4, 1.0e6); // R161
            resonator.addResistor(1, 5, 100.0e3);        // R159
            r166 = resonator.addResistor(4, 6, 6.8e3);   // R166
            resonator.addResistor(6, 0, 47.0e3);         // R165
            c41 = resonator.addCapacitor(4, 7, nominalC41); // C41
            c42 = resonator.addCapacitor(4, 8, nominalC42); // C42
            resonator.addResistor(7, 8, 1.0e6);          // R167
            resonator.addResistor(8, 0, 1.0e6);          // the load
            resonator.addResistor(8, 9, 47.0e3);         // R164
            vr6 = resonator.addResistor(9, 10, 200.0e3); // VR6
            resonator.addCapacitor(10, 11, 33.0e-6);     // C43
            resonator.addResistor(9, 11, 47.0e3);        // R169
            resonator.addResistor(12, 0, 22.0e3);        // R168
            resonator.addResistor(11, 4, 470.0e3);       // R170
            resonator.addNullor(2, 7, 8);                // the first op-amp
            resonator.addNullor(12, 9, 11);              // the second
            resonator.addTransistor(6, 5, 0, transistorSaturation, betaF, betaR, thermal); // Q43
            resonator.addDiode(3, 0, diodeSaturation, thermal);                            // D52
            resonatorOut = resonator.addProbe(8);
        }

        // Output filter: 1 the resonator (input), 2 after R171, 3 the tone's capacitor, 4 VR4's top, 5 its wiper,
        // 6 after C49, 7 Q44's base, 8 its emitter, 9 the output.
        void buildOutput()
        {
            outputIn = output.addInput(1);
            output.addResistor(1, 2, 220.0);            // R171
            output.addResistor(2, 3, 10.0e3);           // R172
            vr5 = output.addResistor(2, 3, 100.0);      // VR5
            output.addCapacitor(3, 0, 0.1e-6);          // C45
            output.addCapacitor(3, 4, 0.47e-6);         // C47
            vr4Top = output.addResistor(4, 5, 1.0e3);   // VR4, above the wiper
            vr4Bottom = output.addResistor(5, 0, 99.0e3); // and below
            output.addCapacitor(5, 6, 0.47e-6);         // C49
            output.addResistor(6, 7, 82.0e3);           // R177
            output.addResistor(7, 0, 100.0e3);          // R176
            output.addNullor(7, 8, 8);                  // Q44 as an ideal emitter follower
            output.addResistor(8, 0, 6.8e3);            // R173 (to the negative supply: ground for the signal)
            output.addCapacitor(8, 0, 220.0e-12);       // C48
            output.addCapacitor(8, 9, 1.0e-6);          // C50
            output.addResistor(9, 0, 100.0e3);          // R174
            outputOut = output.addProbe(9);
        }

        wdf::Circuit<2, 4, 1, 0, 1, 1> pulse;
        wdf::Circuit<6, 14, 2, 0, 5, 1> envelope;
        wdf::Circuit<12, 18, 2, 2, 3, 1> resonator;
        wdf::Circuit<9, 14, 1, 1, 0, 1> output;

        int pulseIn = 0, pulseOut = 0;
        int envelopeIn = 0, envelopeSupply = 0, envelopeOut = 0;
        int resonatorEnvelope = 0, resonatorPulse = 0, resonatorOut = 0, c41 = 0, c42 = 0, vr6 = 0;
        int outputIn = 0, outputOut = 0, vr5 = 0, vr4Top = 0, vr4Bottom = 0;

        struct RestState
        {
            decltype(pulse)::Snapshot pulse {};
            decltype(envelope)::Snapshot envelope {};
            decltype(resonator)::Snapshot resonator {};
            decltype(output)::Snapshot output {};
            double envelopeVolts = 0.0;
        };

        static const RestState& restState() noexcept
        {
            static const RestState state = []
            {
                auto drum = std::make_unique<Tr808BassDrum>();
                drum->settle();
                RestState s;
                s.pulse = drum->pulse.snapshot();
                s.envelope = drum->envelope.snapshot();
                s.resonator = drum->resonator.snapshot();
                s.output = drum->output.snapshot();
                s.envelopeVolts = drum->envelope.probe(drum->envelopeOut);
                return s;
            }();
            return state;
        }

        double sampleRate = 96000.0;
        double triggerTime = -1.0, triggerVolts = 4.0, triggerWidth = 0.001;
        int c38 = 0, r161Envelope = 0, r161Resonator = 0, r166 = 0;
        double r166Ohms = 6.8e3, r161Ohms = 1.0e6, rails = 15.0, tuning = 1.0;
        bool pulseResting = false, envelopeResting = false;
        double lastEnvelope = 0.0;
        int stillSamples = 0;
    };
}
