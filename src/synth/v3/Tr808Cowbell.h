#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"
#include "Tr808BassDrum.h"
#include "Tr808MetalBank.h"

/*
    The Roland TR-808's cowbell (D254), from the service notes (pp. 6, 13) and tools/tr808/metal.cir, its reference.
    Three circuits, each a wave digital filter (CircuitWdf.h):

        Envelope    IC2a's narrowed trigger through the switch Q5 (Q1 on while the instrument's data stands: R9 to
                    ground), R122 and D2 into C9; R82 and C34 take part of the charge at once (the abrupt fall after the
                    attack); R28 and R29 feed the two VCAs
        VCAs        two swing VCAs: the 540 Hz oscillator through R62, R63 and C32 onto Q15's base (R30 its bias),
                    which pulls R28's end down through D3 while the oscillator is high; the 800 Hz one likewise through
                    Q14 and D4; both through R26, C30 and R27, C31 into IC2b's band-pass (R24, C28, C29, R25; 870 Hz)
        Output      C75, R116 and CB LEVEL (VR5, 5 kOhm A, a divider), C76 and R117 into IC6a's follower

    R28 and R29 join the envelope and the VCAs a sample apart, each circuit holding the other's end as a source (100
    kOhm against C9's 0.47 uF is some 47 ms). C32 and C33 (10 uF behind 470 kOhm) settle over seconds as the
    oscillators run; a cowbell struck from rest starts with them where the running bank holds them. Transistors and
    diodes as the bass drum's; IC2b of 3 MHz and a +-14 V swing; IC6a ideal. Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808Cowbell final
    {
    public:
        static constexpr double thermal = Tr808BassDrum::thermal;
        static constexpr double transistorSaturation = Tr808BassDrum::transistorSaturation;
        static constexpr double betaF = Tr808BassDrum::betaF, betaR = Tr808BassDrum::betaR;
        static constexpr double diodeSaturation = Tr808BassDrum::diodeSaturation;
        static constexpr double diodeThermal = thermal * Tr808BassDrum::diodeEmission;
        static constexpr double supply = 15.0;
        static constexpr double instrumentHold = Tr808BassDrum::instrumentHold;
        static constexpr double switchOff = Tr808BassDrum::switchOff;
        /** A transistor's collector junction as the reference has it (CJC 4 pF), where the transistor and a diode stand
            in series with nothing else between them: without it their currents are one and the root cannot be solved. */
        static constexpr double junctionCollector = 4.0e-12;
        /** C32's and C33's voltage (the oscillator's side less the base's) with the bank running and the cowbell at
            rest: where they settle within 2 s (measured on the model, V3Tr808MetalTests with COOLSYNTH_808_COUPLING). */
        static constexpr double couplingRest = -0.390;

        Tr808Cowbell()
        {
            front.setNewtonLimit(40);
            vca.setNewtonLimit(40);
            buildFront();
            buildVca();
            buildOutput();
        }

        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            front.setSampleRate(rate);
            vca.setSampleRate(rate);
            output.setSampleRate(rate);
            rest();
        }

        /** Back to rest, the bank running: a copy of the solved rest state, no solving. */
        void rest() noexcept
        {
            const RestState& r = restState();
            front.setResistance(r9, switchOff);
            front.restore(r.front);
            vca.restore(r.vca);
            output.restore(r.output);
            triggerSample = -1;
            lastEnvelope = lastP1 = lastP2 = 0.0;
            setRestInputs();
        }

        static void warmUp() noexcept { (void) restState(); }

        /**
            Back to the circuit at rest by solving it, the oscillators standing at these volts: capacitors charged as
            they are with no trigger (as the reference's operating point at time 0, C32 and C33 included).
        */
        void settle(double osc540, double osc800) noexcept
        {
            triggerSample = -1;
            front.setResistance(r9, switchOff);
            lastEnvelope = lastP1 = lastP2 = 0.0;
            setRestInputs();
            vca.setInput(vcaOsc1, osc540);
            vca.setInput(vcaOsc2, osc800);
            for (int pass = 0; pass < 3; ++pass)
            {
                front.setSource(r28Front, lastP1);
                front.setSource(r29Front, lastP2);
                front.settle();
                lastEnvelope = front.probe(envelopeProbe);
                vca.setSource(r28Vca, lastEnvelope);
                vca.setSource(r29Vca, lastEnvelope);
                vca.settle();
                lastP1 = vca.probe(p1Probe);
                lastP2 = vca.probe(p2Probe);
            }
            output.setInput(outputIn, vca.probe(bandProbe));
            output.settle();
        }

        /** A hit: IC2a's output at this many volts for this long, through the switch. */
        void trigger(double bufferVolts, double widthSeconds = 0.001) noexcept
        {
            pulseVolts = bufferVolts;
            pulseWidth = widthSeconds;
            triggerSample = 0;
            front.setResistance(r9, 22.0e3); // the data stands: Q1 on, R9 to ground
        }

        /** CB LEVEL 0..1: the 5 kOhm audio-taper divider. */
        void setLevel(double turn) noexcept
        {
            const double share = (std::pow(10.0, 2.0 * std::clamp(turn, 0.0, 1.0)) - 1.0) / 99.0;
            output.setResistance(vr5Upper, std::max(0.01, 5.0e3 * (1.0 - share)));
            output.setResistance(vr5Lower, std::max(0.01, 5.0e3 * share));
        }

        /** Where each sample's Newton ends in the envelope and the VCAs (D259): 100 nV by default, as every circuit. */
        void setNewtonTolerance(double volts) noexcept
        {
            front.setNewtonTolerance(volts);
            vca.setNewtonTolerance(volts);
        }

        [[nodiscard]] int vcaNewtonStepsForTesting() const noexcept { return vca.lastNewtonSteps(); }
        [[nodiscard]] int frontNewtonStepsForTesting() const noexcept { return front.lastNewtonSteps(); }

        /** The CB Decay bend: C9 and C34 scaled together (1 as drawn). */
        void setDecayScale(double scale) noexcept
        {
            front.setCapacitance(c9, 0.47e-6 * scale);
            front.setCapacitance(c34, 1.0e-6 * scale);
        }

        /** One sample, the 540 and 800 Hz oscillators at their volts; returns the output in volts. */
        double process(double osc540, double osc800) noexcept
        {
            double drive = 0.0;
            if (triggerSample >= 0)
            {
                const int width = std::max(1, static_cast<int>(std::lround(pulseWidth * sampleRate)));
                if (triggerSample >= 1 && triggerSample <= width)
                    drive = pulseVolts;
                if (triggerSample >= width + static_cast<int>(std::lround(instrumentHold * sampleRate)))
                {
                    front.setResistance(r9, switchOff);
                    triggerSample = -1;
                }
                else
                    ++triggerSample;
            }
            front.setInput(frontDrive, drive);
            front.setSource(r28Front, lastP1);
            front.setSource(r29Front, lastP2);
            front.process();
            lastEnvelope = front.probe(envelopeProbe);
            vca.setInput(vcaOsc1, osc540);
            vca.setInput(vcaOsc2, osc800);
            vca.setSource(r28Vca, lastEnvelope);
            vca.setSource(r29Vca, lastEnvelope);
            vca.process();
            lastP1 = vca.probe(p1Probe);
            lastP2 = vca.probe(p2Probe);
            output.setInput(outputIn, vca.probe(bandProbe));
            output.process();
            return output.probe(outputProbe);
        }

        // The stages' last outputs, for tests against the reference.
        [[nodiscard]] double envelopeVolts() const noexcept { return lastEnvelope; }
        [[nodiscard]] double gateVolts(int which) const noexcept { return which == 0 ? lastP1 : lastP2; }
        [[nodiscard]] double bandVolts() const noexcept { return vca.probe(bandProbe); }
        [[nodiscard]] double baseVolts(int which) const noexcept { return vca.probe(which == 0 ? b15Probe : b14Probe); }
        [[nodiscard]] double couplingVolts(int which) const noexcept { return vca.portVoltage(which == 0 ? c32 : c33); }
        [[nodiscard]] bool lastSolveConverged() const noexcept { return front.lastSolveConverged() && vca.lastSolveConverged(); }
        /** Every circuit solvable (CircuitWdf::isWellPosed). */
        [[nodiscard]] bool wellPosed() noexcept { return front.isWellPosed() && vca.isWellPosed() && output.isWellPosed(); }

    private:
        void setRestInputs() noexcept
        {
            front.setInput(frontDrive, 0.0);
            front.setSource(r28Front, 0.0);
            front.setSource(r29Front, 0.0);
            vca.setInput(vcaSupply, supply);
            vca.setSource(r28Vca, 0.0);
            vca.setSource(r29Vca, 0.0);
            output.setInput(outputIn, 0.0);
        }

        // Envelope: 1 IC2a's output (input), 2 Q5's base, 3 its collector, 4 D2's anode, 5 C9 (the envelope), 6 C34.
        void buildFront()
        {
            frontDrive = front.addInput(1);
            front.addResistor(1, 2, 4.7e3);                                               // R14
            r9 = front.addResistor(2, 0, switchOff);                                      // R9 through Q1
            front.addPnpTransistor(3, 2, 1, transistorSaturation, betaF, betaR, thermal); // Q5
            front.addCapacitor(3, 2, junctionCollector);                                  // its collector junction (Q5 and D2 in series)
            front.addResistor(3, 4, 1.0e3);                                               // R122
            front.addDiode(4, 5, diodeSaturation, diodeThermal);                          // D2
            c9 = front.addCapacitor(5, 0, 0.47e-6);                                       // C9
            front.addResistor(5, 6, 33.0e3);                                              // R82
            c34 = front.addCapacitor(6, 0, 1.0e-6);                                       // C34
            r28Front = front.addSource(5, 0, 100.0e3);                                    // R28, to Q15's gate
            r29Front = front.addSource(5, 0, 100.0e3);                                    // R29, to Q14's
            envelopeProbe = front.addProbe(5);
            front.addProbe(3);
        }

        // VCAs: 1 and 2 the oscillators (inputs), 3 the supply (input); Q15's side 4 after R62, 5 its base, 6 its
        // collector, 7 its emitter, 8 R28's end; Q14's 9 to 13 likewise; 14 after R26, 15 after R27, 16 the
        // band-pass's input node, 17 IC2b's minus, 18 its output, 19 its inner node.
        void buildVca()
        {
            vcaOsc1 = vca.addInput(1);
            vcaOsc2 = vca.addInput(2);
            vcaSupply = vca.addInput(3);
            vca.addResistor(1, 4, 33.0e3);                                                // R62
            vca.addResistor(4, 0, 2.2e3);                                                 // R63
            c32 = vca.addCapacitor(4, 5, 10.0e-6);                                        // C32
            vca.addResistor(3, 5, 470.0e3);                                               // R30
            vca.addTransistor(6, 5, 7, transistorSaturation, betaF, betaR, thermal);      // Q15
            vca.addCapacitor(6, 5, junctionCollector);                                    // its collector junction (Q15 and D3 in series)
            vca.addResistor(7, 0, 100.0);                                                 // R31
            vca.addDiode(8, 6, diodeSaturation, diodeThermal);                            // D3
            r28Vca = vca.addSource(8, 0, 100.0e3);                                        // R28, from the envelope
            vca.addResistor(2, 9, 33.0e3);                                                // R64
            vca.addResistor(9, 0, 2.2e3);                                                 // R65
            c33 = vca.addCapacitor(9, 10, 10.0e-6);                                       // C33
            vca.addResistor(3, 10, 470.0e3);                                              // R32
            vca.addTransistor(11, 10, 12, transistorSaturation, betaF, betaR, thermal);   // Q14
            vca.addCapacitor(11, 10, junctionCollector);                                  // its collector junction
            vca.addResistor(12, 0, 100.0);                                                // R33
            vca.addDiode(13, 11, diodeSaturation, diodeThermal);                          // D4
            r29Vca = vca.addSource(13, 0, 100.0e3);                                       // R29
            vca.addResistor(8, 14, 10.0e3);                                               // R26
            vca.addCapacitor(14, 16, 2.2e-9);                                             // C30
            vca.addResistor(13, 15, 10.0e3);                                              // R27
            vca.addCapacitor(15, 16, 2.2e-9);                                             // C31
            vca.addResistor(16, 0, 2.2e3);                                                // R24
            vca.addCapacitor(16, 17, 10.0e-9);                                            // C28
            vca.addCapacitor(16, 18, 3.3e-9);                                             // C29
            vca.addResistor(17, 18, 470.0e3);                                             // R25
            vca.addOpAmp(0, 17, 18, 19, Tr808MetalBank::opAmpBandwidth, 14.0);            // IC2b
            p1Probe = vca.addProbe(8);
            p2Probe = vca.addProbe(13);
            bandProbe = vca.addProbe(18);
            b15Probe = vca.addProbe(5);
            b14Probe = vca.addProbe(10);
        }

        // Output: 1 IC2b's output (input), 2 after C75, 3 VR5's top, 4 its wiper, 5 IC6a's input (the output).
        void buildOutput()
        {
            outputIn = output.addInput(1);
            output.addCapacitor(1, 2, 1.0e-6);                 // C75
            output.addResistor(2, 3, 8.2e3);                   // R116
            vr5Upper = output.addResistor(3, 4, 1.0e3);        // VR5 above its wiper
            vr5Lower = output.addResistor(4, 0, 4.0e3);        // and below
            output.addCapacitor(4, 5, 22.0e-9);                // C76
            output.addResistor(5, 0, 22.0e3);                  // R117
            outputProbe = output.addProbe(5);
        }

        wdf::Circuit<6, 12, 1, 0, 3, 2> front;
        wdf::Circuit<19, 31, 3, 1, 7, 5> vca;
        wdf::Circuit<5, 6, 1, 0, 0, 1> output;

        struct RestState
        {
            decltype(front)::Snapshot front {};
            decltype(vca)::Snapshot vca {};
            decltype(output)::Snapshot output {};
        };

        static const RestState& restState() noexcept
        {
            static const RestState state = []
            {
                RestState s {};
                auto cowbell = std::make_unique<Tr808Cowbell>();
                cowbell->sampleRate = 96000.0;
                cowbell->settle(0.0, 0.0);
                cowbell->vca.setCapacitorVoltage(cowbell->c32, couplingRest);
                cowbell->vca.setCapacitorVoltage(cowbell->c33, couplingRest);
                s.front = cowbell->front.snapshot();
                s.vca = cowbell->vca.snapshot();
                s.output = cowbell->output.snapshot();
                return s;
            }();
            return state;
        }

        double sampleRate = 96000.0;
        double pulseVolts = 7.0, pulseWidth = 0.001;
        int triggerSample = -1;
        double lastEnvelope = 0.0, lastP1 = 0.0, lastP2 = 0.0;

        int frontDrive = 0, r9 = 0, c9 = 0, c34 = 0, r28Front = 0, r29Front = 0, envelopeProbe = 0;
        int vcaOsc1 = 0, vcaOsc2 = 0, vcaSupply = 0, c32 = 0, c33 = 0, r28Vca = 0, r29Vca = 0;
        int p1Probe = 0, p2Probe = 0, bandProbe = 0, b15Probe = 0, b14Probe = 0;
        int outputIn = 0, vr5Upper = 0, vr5Lower = 0, outputProbe = 0;
    };
}
