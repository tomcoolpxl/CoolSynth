#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"
#include "Tr808BassDrum.h"
#include "Tr808Cymbal.h"
#include "Tr808MetalBank.h"

/*
    The Roland TR-808's open and closed hi-hat (D254), from the service notes (pp. 6, 13) and tools/tr808/metal.cir,
    their reference. Both gate IC3's high band; the closed hat cuts the open one short. Eight circuits, each a wave
    digital filter (CircuitWdf.h):

        OH envelope IC2a's narrowed trigger through the switch Q7: through R135 into Q24, which charges C62 (its
                    collector fed through R137 and R136 with C61), and through R131 and R130 into Q21, which empties
                    C60. C62 drains slowly through R139 until C60, charging again through R132 and OH DECAY (VR3, 2
                    MOhm B), turns Q22 on through R133, which drains C62 through R134; Q23 does the same when the closed
                    hat's envelope reaches it through R173. Q29 follows C62 (R140, R143) into the VCA through R144
        CH envelope the switch Q8 through R141 into Q28, which charges C63 (its collector fed through R138 and R136 with
                    C61); C63 feeds the VCA through R142 and Q23's base through R173
        VCAs        the high band through C67 (C71) onto Q27's (Q30's) base, 2 MOhm its bias: a swing VCA pulling R144's
                    (R142's) end down through D13 (D14), C65 and C64 (C70 and C69) around it; then a Sallen-Key
                    high-pass around the emitter follower Q26 (Q31): 8.2 kHz (12.2 kHz), a Q of 2.5
        Outputs     C80 470 pF and R160 39 kOhm (C74, R162 33 kOhm) into IC7a (IC7b), its level pot (VR7, VR8, 100 kOhm A)
                    with C81 (C83) in its feedback, beside R159 (R161) and C79 (C82)

    R144, R142 and R173 join the circuits a sample apart, each holding the other's end as a source. Seen from the
    closed hat, Q23's base is a saturated transistor's (Is (1 / betaF + 1 / betaR)), as it is whenever the closed hat
    reaches it: the open hat's Q22 or Q23 then holds their collectors down. Once a hit has passed (its switch, Q21,
    Q24 or Q28 off), each envelope runs without the trigger's parts, the state passed across. The VCAs run while the
    bank does. Each high-pass's capacitors are prewarped at its peak. Transistors and diodes as the bass drum's; Q26,
    Q31 and IC7 ideal. Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808Hats final
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
        static constexpr double openPeak = 8157.0, closedPeak = 12246.0;
        /** C67's and C71's voltage with the bank running and the hats at rest (as Tr808Cymbal's C42). */
        static constexpr double couplingRest = Tr808Cymbal::highCouplingRest;
        static constexpr double triggerSettle = 0.002;
        static constexpr double c63 = 0.47e-6;
        /** C60 (printed 10 uF) and R134 (printed 100 kOhm), fitted to both units' open hat across OH DECAY (D258). */
        static constexpr double c60Farads = 8.0e-6, r134Ohms = 82.0e3;

        enum Hat : int { open = 0, closed = 1 };

        Tr808Hats()
        {
            openFull.setNewtonLimit(40);
            openTail.setNewtonLimit(40);
            closedFull.setNewtonLimit(40);
            closedTail.setNewtonLimit(40);
            buildOpenEnvelope(openTail, false);
            buildOpenEnvelope(openFull, true);
            buildClosedEnvelope(closedTail, false);
            buildClosedEnvelope(closedFull, true);
            buildVca(openVca, openGate);
            buildVca(closedVca, closedGate);
            buildOutput(openOutput, openOut, 39.0e3);
            buildOutput(closedOutput, closedOut, 33.0e3);
        }

        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            openFull.setSampleRate(rate);
            openTail.setSampleRate(rate);
            closedFull.setSampleRate(rate);
            closedTail.setSampleRate(rate);
            openVca.setSampleRate(rate);
            closedVca.setSampleRate(rate);
            openOutput.setSampleRate(rate);
            closedOutput.setSampleRate(rate);
            const double o = Tr808MetalBank::prewarp(openPeak, rate), c = Tr808MetalBank::prewarp(closedPeak, rate);
            openVca.setCapacitance(openGate.hp1, 1.5e-9 * o);
            openVca.setCapacitance(openGate.hp2, 1.5e-9 * o);
            closedVca.setCapacitance(closedGate.hp1, 1.0e-9 * c);
            closedVca.setCapacitance(closedGate.hp2, 1.0e-9 * c);
            rest();
        }

        void rest() noexcept
        {
            restEnvelope(open);
            restEnvelope(closed);
            restVcas();
        }

        /** A hat's envelope and output at rest (the VCAs, which run with the bank, as they are). */
        void restEnvelope(int hat) noexcept
        {
            const RestState& r = restState();
            if (hat == open)
            {
                openFull.setResistance(openSwitch, switchOff);
                openFull.restore(r.openFull);
                openTail.restore(r.openTail);
                openOutput.restore(r.openOutput);
            }
            else
            {
                closedFull.setResistance(closedSwitch, switchOff);
                closedFull.restore(r.closedFull);
                closedTail.restore(r.closedTail);
                closedOutput.restore(r.closedOutput);
            }
            auto& s = state[static_cast<std::size_t>(hat)];
            s.usingFull = false;
            s.triggerSample = -1;
            s.sinceHit = 1 << 30;
            setRestInputs();
            if (hat == open)
                readOpen(openTail);
            else
                readClosed(closedTail);
        }

        void restVcas() noexcept
        {
            const RestState& r = restState();
            openVca.restore(r.openVca);
            closedVca.restore(r.closedVca);
            openGateVolts = closedGateVolts = 0.0;
        }

        static void warmUp() noexcept { (void) restState(); }

        /** Back to the circuit at rest by solving it, the high band at these volts (as the reference's time 0). */
        void settle(double highBand) noexcept
        {
            for (int hat = 0; hat < 2; ++hat)
            {
                auto& s = state[static_cast<std::size_t>(hat)];
                s.usingFull = false;
                s.triggerSample = -1;
                s.sinceHit = 1 << 30;
            }
            openFull.setResistance(openSwitch, switchOff);
            closedFull.setResistance(closedSwitch, switchOff);
            setRestInputs();
            openVca.setInput(openGate.band, highBand);
            closedVca.setInput(closedGate.band, highBand);
            openGateVolts = closedGateVolts = 0.0;
            openEnvelopeOut = closedEnvelope = 0.0;
            openChokeBase = closedChokeBase = 0.0;
            for (int pass = 0; pass < 4; ++pass)
            {
                setOpenSources(openTail);
                openTail.settle();
                readOpen(openTail);
                setClosedSources(closedTail);
                closedTail.settle();
                readClosed(closedTail);
                setVcaSources();
                openVca.settle();
                closedVca.settle();
                openGateVolts = openVca.probe(openGate.gateProbe);
                closedGateVolts = closedVca.probe(closedGate.gateProbe);
            }
            setOpenSources(openFull);
            openFull.settle();
            setClosedSources(closedFull);
            closedFull.settle();
            openOutput.setInput(openOut.in, openVca.probe(openGate.outProbe));
            openOutput.settle();
            closedOutput.setInput(closedOut.in, closedVca.probe(closedGate.outProbe));
            closedOutput.settle();
        }

        /** A hit: IC2a's output at this many volts for this long, through the hat's switch. */
        void trigger(int hat, double bufferVolts, double widthSeconds = 0.001) noexcept
        {
            auto& s = state[static_cast<std::size_t>(hat)];
            if (! s.usingFull)
            {
                if (hat == open)
                {
                    openFull.restore(restState().openFull);
                    openFull.adoptSharedState(openTail);
                }
                else
                {
                    closedFull.restore(restState().closedFull);
                    closedFull.adoptSharedState(closedTail);
                }
                s.usingFull = true;
            }
            s.pulseVolts = bufferVolts;
            s.pulseWidth = widthSeconds;
            s.triggerSample = 0;
            s.sinceHit = 0;
            if (hat == open)
                openFull.setResistance(openSwitch, 22.0e3); // the data stands: Q3 on, R12 to ground
            else
                closedFull.setResistance(closedSwitch, 22.0e3); // Q4 on, R11 to ground
        }

        /** OH DECAY 0..1: VR3, 2 MOhm linear, as a rheostat. */
        void setOpenDecay(double turn) noexcept
        {
            const double ohms = std::max(0.01, 2.0e6 * std::clamp(turn, 0.0, 1.0));
            openFull.setResistance(vr3, ohms);
            openTail.setResistance(vr3, ohms);
        }

        /** For fitting the open hat's decay (tests only, D258): C60, R134 and R139. The rest is not solved again: C60
            and R139 leave it as it is, R134 moves C62's by microvolts (Q22's leakage). */
        void setOpenEnvelopeParts(double c60, double r134, double r139) noexcept
        {
            const auto set = [&](auto& c)
            {
                c.setCapacitance(c60Port, c60);
                c.setResistance(r134Port, r134);
                c.setResistance(r139Port, r139);
            };
            set(openFull);
            set(openTail);
        }

        /** The CH Decay bend: C63 scaled (1 as drawn). */
        void setClosedDecayScale(double scale) noexcept
        {
            closedFull.setCapacitance(c63Port, c63 * scale);
            closedTail.setCapacitance(c63Port, c63 * scale);
        }

        /** OH LEVEL and CH LEVEL 0..1: the 100 kOhm audio-taper pots in IC7's feedback. */
        void setLevel(int hat, double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            const double ohms = std::max(1.0, 100.0e3 * (std::pow(10.0, 2.0 * t) - 1.0) / 99.0);
            if (hat == open)
                openOutput.setResistance(openOut.level, ohms);
            else
                closedOutput.setResistance(closedOut.level, ohms);
        }

        /** A hat's envelope, one sample (before processVcas), while it sounds. */
        void processEnvelope(int hat) noexcept
        {
            auto& s = state[static_cast<std::size_t>(hat)];
            double drive = 0.0;
            if (s.triggerSample >= 0)
            {
                const int width = std::max(1, static_cast<int>(std::lround(s.pulseWidth * sampleRate)));
                if (s.triggerSample >= 1 && s.triggerSample <= width)
                    drive = s.pulseVolts;
                if (s.triggerSample >= width + static_cast<int>(std::lround(instrumentHold * sampleRate)))
                {
                    if (hat == open)
                        openFull.setResistance(openSwitch, switchOff);
                    else
                        closedFull.setResistance(closedSwitch, switchOff);
                    s.triggerSample = -1;
                }
                else
                    ++s.triggerSample;
            }
            ++s.sinceHit;
            const int pulse = static_cast<int>(std::lround((s.pulseWidth + instrumentHold + triggerSettle) * sampleRate));
            const bool handOver = s.usingFull && s.triggerSample < 0 && s.sinceHit > pulse;
            if (hat == open)
            {
                if (s.usingFull)
                {
                    openFull.setInput(openDrive, drive);
                    setOpenSources(openFull);
                    openFull.process();
                    readOpen(openFull);
                    if (handOver)
                    {
                        openTail.adoptSharedState(openFull);
                        s.usingFull = false;
                    }
                }
                else
                {
                    setOpenSources(openTail);
                    openTail.process();
                    readOpen(openTail);
                }
            }
            else
            {
                if (s.usingFull)
                {
                    closedFull.setInput(closedDrive, drive);
                    setClosedSources(closedFull);
                    closedFull.process();
                    readClosed(closedFull);
                    if (handOver)
                    {
                        closedTail.adoptSharedState(closedFull);
                        s.usingFull = false;
                    }
                }
                else
                {
                    setClosedSources(closedTail);
                    closedTail.process();
                    readClosed(closedTail);
                }
            }
        }

        /** Both VCAs, one sample, the high band at these volts (while the bank runs). */
        void processVcas(double highBand) noexcept
        {
            openVca.setInput(openGate.band, highBand);
            closedVca.setInput(closedGate.band, highBand);
            setVcaSources();
            openVca.process();
            closedVca.process();
            openGateVolts = openVca.probe(openGate.gateProbe);
            closedGateVolts = closedVca.probe(closedGate.gateProbe);
        }

        /** A hat's output stage, one sample (after processVcas); returns the output in volts. */
        double processOutput(int hat) noexcept
        {
            if (hat == open)
            {
                openOutput.setInput(openOut.in, openVca.probe(openGate.outProbe));
                openOutput.process();
                return openOutput.probe(openOut.probe);
            }
            closedOutput.setInput(closedOut.in, closedVca.probe(closedGate.outProbe));
            closedOutput.process();
            return closedOutput.probe(closedOut.probe);
        }

        /** The closed hat's envelope reaching the open hat's Q23 through R173: while it does, the open hat is choked. */
        [[nodiscard]] double closedEnvelopeVolts() const noexcept { return closedEnvelope; }
        [[nodiscard]] double openEnvelopeVolts() const noexcept { return openEnvelopeC62; }
        [[nodiscard]] double openEnvelopeOutVolts() const noexcept { return openEnvelopeOut; }
        [[nodiscard]] double openTimerVolts() const noexcept { return openTimer; }
        [[nodiscard]] double gateVolts(int hat) const noexcept { return hat == open ? openGateVolts : closedGateVolts; }
        [[nodiscard]] double highPassVolts(int hat) const noexcept
        {
            return hat == open ? openVca.probe(openGate.outProbe) : closedVca.probe(closedGate.outProbe);
        }
        [[nodiscard]] double couplingVolts(int hat) const noexcept
        {
            return hat == open ? openVca.portVoltage(openGate.coupling) : closedVca.portVoltage(closedGate.coupling);
        }
        [[nodiscard]] bool usingTriggerParts(int hat) const noexcept { return state[static_cast<std::size_t>(hat)].usingFull; }
        /** A hat's VCA's last Newton steps, for the cost tests. */
        [[nodiscard]] int vcaNewtonSteps(int hat) const noexcept { return hat == open ? openVca.lastNewtonSteps() : closedVca.lastNewtonSteps(); }
        [[nodiscard]] bool lastSolveConverged() const noexcept
        {
            return (state[0].usingFull ? openFull.lastSolveConverged() : openTail.lastSolveConverged())
                   && (state[1].usingFull ? closedFull.lastSolveConverged() : closedTail.lastSolveConverged())
                   && openVca.lastSolveConverged() && closedVca.lastSolveConverged();
        }

        /** Every circuit solvable (CircuitWdf::isWellPosed). */
        [[nodiscard]] bool wellPosed() noexcept
        {
            return openFull.isWellPosed() && openTail.isWellPosed() && closedFull.isWellPosed() && closedTail.isWellPosed()
                   && openVca.isWellPosed() && closedVca.isWellPosed() && openOutput.isWellPosed() && closedOutput.isWellPosed();
        }

    private:
        struct HatState
        {
            bool usingFull = false;
            int triggerSample = -1, sinceHit = 1 << 30;
            double pulseVolts = 7.0, pulseWidth = 0.001;
        };

        struct Gate
        {
            int band = 0, supplyIn = 0, coupling = 0, envelope = 0, hp1 = 0, hp2 = 0, gateProbe = 0, outProbe = 0;
        };

        struct Output
        {
            int in = 0, level = 0, probe = 0;
        };

        void setRestInputs() noexcept
        {
            openFull.setInput(openFullSupply, supply);
            openFull.setInput(openDrive, 0.0);
            openTail.setInput(openTailSupply, supply);
            closedFull.setInput(closedFullSupply, supply);
            closedFull.setInput(closedDrive, 0.0);
            openVca.setInput(openGate.supplyIn, supply);
            closedVca.setInput(closedGate.supplyIn, supply);
            openOutput.setInput(openOut.in, 0.0);
            closedOutput.setInput(closedOut.in, 0.0);
        }

        template <class C>
        void setOpenSources(C& c) noexcept
        {
            c.setSource(r173Open, closedEnvelope);
            c.setSource(r144Env, openGateVolts);
        }

        template <class C>
        void setClosedSources(C& c) noexcept
        {
            c.setSource(r142Env, closedGateVolts);
        }

        template <class C>
        void readOpen(const C& c) noexcept
        {
            openEnvelopeC62 = c.probe(e1Probe);
            openEnvelopeOut = c.probe(oenvProbe);
            openTimer = c.probe(g2Probe);
            openChokeBase = c.probe(b23Probe);
        }

        template <class C>
        void readClosed(const C& c) noexcept
        {
            closedEnvelope = c.probe(ceProbe);
            closedChokeBase = c.probe(b23cProbe);
        }

        void setVcaSources() noexcept
        {
            openVca.setSource(openGate.envelope, openEnvelopeOut);
            closedVca.setSource(closedGate.envelope, closedEnvelope);
        }

        // OH envelope. The tail: 1 the supply (input), 2 C62, 3 Q22's and Q23's collectors, 4 Q22's base, 5 C60, 6
        // VR3's end, 7 Q23's base, 8 Q29's base, 9 its emitter. The trigger's parts after them: 10 IC2a's output
        // (input), 11 Q7's base, 12 its collector, 13 Q21's base, 14 Q24's base, 15 its collector, 16 C61.
        template <class C>
        void buildOpenEnvelope(C& c, bool withTrigger)
        {
            const int s = c.addInput(1);
            c.addCapacitor(2, 0, 0.47e-6);                                                // C62
            r139Port = c.addResistor(2, 0, 1.0e6);                                        // R139
            r134Port = c.addResistor(2, 3, r134Ohms);                                     // R134
            c.addTransistor(3, 4, 0, transistorSaturation, betaF, betaR, thermal);        // Q22
            c.addResistor(5, 4, 10.0e3);                                                  // R133
            c60Port = c.addCapacitor(5, 0, c60Farads);                                    // C60
            c.addResistor(6, 5, 100.0e3);                                                 // R132
            vr3 = c.addResistor(1, 6, 1.0e6);                                             // OH DECAY
            c.addTransistor(3, 7, 0, transistorSaturation, betaF, betaR, thermal);        // Q23
            r173Open = c.addSource(7, 0, 330.0e3);                                        // R173, from C63
            c.addResistor(2, 8, 33.0e3);                                                  // R140
            c.addTransistor(1, 8, 9, transistorSaturation, betaF, betaR, thermal);        // Q29
            c.addResistor(9, 0, 33.0e3);                                                  // R143
            r144Env = c.addSource(9, 0, 33.0e3);                                          // R144, to Q27's gate
            e1Probe = c.addProbe(2);
            oenvProbe = c.addProbe(9);
            g2Probe = c.addProbe(5);
            b23Probe = c.addProbe(7);
            if (! withTrigger)
            {
                openTailSupply = s;
                return;
            }
            openFullSupply = s;
            openDrive = c.addInput(10);
            c.addResistor(10, 11, 4.7e3);                                                 // R17
            openSwitch = c.addResistor(11, 0, switchOff);                                 // R12 through Q3
            c.addPnpTransistor(12, 11, 10, transistorSaturation, betaF, betaR, thermal);  // Q7
            c.addResistor(12, 13, 22.0e3);                                                // R131
            c.addResistor(13, 0, 10.0e3);                                                 // R130
            c.addTransistor(5, 13, 0, transistorSaturation, betaF, betaR, thermal);       // Q21
            c.addResistor(12, 14, 10.0e3);                                                // R135
            c.addTransistor(15, 14, 2, transistorSaturation, betaF, betaR, thermal);      // Q24
            c.addResistor(15, 16, 470.0);                                                 // R137
            c.addCapacitor(16, 0, 47.0e-6);                                               // C61
            c.addResistor(1, 16, 100.0);                                                  // R136
            c.addProbe(12);
        }

        // CH envelope. The tail: 1 C63, 2 Q23's base, 3 the supply (input). The trigger's parts after them: 4 IC2a's
        // output (input), 5 Q8's base, 6 its collector, 7 Q28's base, 8 its collector, 9 C61.
        template <class C>
        void buildClosedEnvelope(C& c, bool withTrigger)
        {
            const int s = c.addInput(3);
            c63Port = c.addCapacitor(1, 0, c63);                                          // C63
            c.addResistor(1, 2, 330.0e3);                                                 // R173
            c.addDiode(2, 0, transistorSaturation * (1.0 / betaF + 1.0 / betaR), thermal); // Q23's base, saturated
            r142Env = c.addSource(1, 0, 33.0e3);                                          // R142, to Q30's gate
            ceProbe = c.addProbe(1);
            b23cProbe = c.addProbe(2);
            if (! withTrigger)
                return;
            closedFullSupply = s;
            closedDrive = c.addInput(4);
            c.addResistor(4, 5, 4.7e3);                                                   // R16
            closedSwitch = c.addResistor(5, 0, switchOff);                                // R11 through Q4
            c.addPnpTransistor(6, 5, 4, transistorSaturation, betaF, betaR, thermal);     // Q8
            c.addResistor(6, 0, 22.0e3);                                                  // R13
            c.addResistor(6, 7, 33.0e3);                                                  // R141
            c.addTransistor(8, 7, 1, transistorSaturation, betaF, betaR, thermal);        // Q28
            c.addResistor(8, 9, 470.0);                                                   // R138
            c.addCapacitor(9, 0, 47.0e-6);                                                // C61
            c.addResistor(3, 9, 100.0);                                                   // R136
            c.addProbe(6);
        }

        // A hat's VCA and high-pass: 1 the high band (input), 2 the supply (input), 3 the base, 4 the collector, 5 the
        // emitter, 6 the gate (R144's or R142's end), 7 between the high-pass's capacitors, 8 the follower's base, 9 its
        // emitter.
        template <class C>
        void buildVca(C& c, Gate& g)
        {
            const bool isOpen = &g == &openGate;
            const double hp = isOpen ? 1.5e-9 : 1.0e-9;
            g.band = c.addInput(1);
            g.supplyIn = c.addInput(2);
            g.coupling = c.addCapacitor(1, 3, 22.0e-9);                                   // C67 (C71)
            c.addResistor(2, 3, 2.0e6);                                                   // R148 and R149 (R150, R151)
            c.addTransistor(4, 3, 5, transistorSaturation, betaF, betaR, thermal);        // Q27 (Q30)
            c.addResistor(5, 0, 100.0);                                                   // R145 (R152)
            c.addCapacitor(4, 0, 1.5e-9);                                                 // C65 (C70)
            c.addDiode(6, 4, diodeSaturation, diodeThermal);                              // D13 (D14)
            c.addCapacitor(6, 0, 1.5e-9);                                                 // C64 (C69)
            g.envelope = c.addSource(6, 0, 33.0e3);                                       // R144 (R142)
            g.hp1 = c.addCapacitor(6, 7, hp);                                             // C66 (C72)
            c.addResistor(7, 9, 2.7e3);                                                   // R147 (R153)
            g.hp2 = c.addCapacitor(7, 8, hp);                                             // C68 (C73)
            c.addResistor(8, 0, 68.0e3);                                                  // R146 (R155)
            c.addNullor(8, 9, 9);                                                         // Q26 (Q31), a follower
            g.gateProbe = c.addProbe(6);
            g.outProbe = c.addProbe(9);
            c.addProbe(3);
        }

        // An output: 1 the follower's emitter (input), 2 after C80 (C74), 3 IC7's minus, 4 its output, 5 after C81 (C83).
        template <class C>
        static void buildOutput(C& c, Output& o, double inputOhms)
        {
            o.in = c.addInput(1);
            c.addCapacitor(1, 2, 470.0e-12);                   // C80 (C74)
            c.addResistor(2, 3, inputOhms);                    // R160 39k (R162 33k)
            c.addResistor(3, 4, 470.0e3);                      // R159 (R161)
            c.addCapacitor(3, 4, 220.0e-12);                   // C79 (C82)
            c.addCapacitor(3, 5, 33.0e-6);                     // C81 (C83)
            o.level = c.addResistor(5, 4, 39.0e3);             // VR7 (VR8)
            c.addNullor(0, 3, 4);                              // IC7a (IC7b)
            o.probe = c.addProbe(4);
        }

        wdf::Circuit<16, 31, 2, 0, 12, 5> openFull;
        wdf::Circuit<9, 17, 1, 0, 6, 4> openTail;
        wdf::Circuit<9, 15, 2, 0, 5, 3> closedFull;
        wdf::Circuit<3, 4, 1, 0, 1, 2> closedTail;
        wdf::Circuit<9, 13, 2, 1, 3, 3> openVca, closedVca;
        wdf::Circuit<5, 6, 1, 1, 0, 1> openOutput, closedOutput;

        struct RestState
        {
            decltype(openFull)::Snapshot openFull {};
            decltype(openTail)::Snapshot openTail {};
            decltype(closedFull)::Snapshot closedFull {};
            decltype(closedTail)::Snapshot closedTail {};
            decltype(openVca)::Snapshot openVca {}, closedVca {};
            decltype(openOutput)::Snapshot openOutput {}, closedOutput {};
        };

        static const RestState& restState() noexcept
        {
            static const RestState rest = []
            {
                RestState s {};
                auto hats = std::make_unique<Tr808Hats>();
                hats->sampleRate = 96000.0;
                hats->settle(0.0);
                hats->openVca.setCapacitorVoltage(hats->openGate.coupling, couplingRest);
                hats->closedVca.setCapacitorVoltage(hats->closedGate.coupling, couplingRest);
                s.openFull = hats->openFull.snapshot();
                s.openTail = hats->openTail.snapshot();
                s.closedFull = hats->closedFull.snapshot();
                s.closedTail = hats->closedTail.snapshot();
                s.openVca = hats->openVca.snapshot();
                s.closedVca = hats->closedVca.snapshot();
                s.openOutput = hats->openOutput.snapshot();
                s.closedOutput = hats->closedOutput.snapshot();
                return s;
            }();
            return rest;
        }

        double sampleRate = 96000.0;
        std::array<HatState, 2> state {};
        double openEnvelopeC62 = 0.0, openEnvelopeOut = 0.0, openTimer = 0.0, openChokeBase = 0.0;
        double closedEnvelope = 0.0, closedChokeBase = 0.0;
        double openGateVolts = 0.0, closedGateVolts = 0.0;

        int openFullSupply = 0, openTailSupply = 0, openDrive = 0, openSwitch = 0, vr3 = 0, r173Open = 0, r144Env = 0;
        int c60Port = 0, r134Port = 0, r139Port = 0;
        int e1Probe = 0, oenvProbe = 0, g2Probe = 0, b23Probe = 0;
        int closedFullSupply = 0, closedDrive = 0, closedSwitch = 0, c63Port = 0, r142Env = 0, ceProbe = 0, b23cProbe = 0;
        Gate openGate, closedGate;
        Output openOut, closedOut;
    };
}
