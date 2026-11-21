#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"
#include "Tr808BassDrum.h"

/*
    The Roland TR-808's low, mid and high tom or conga (D252): one circuit a pair, as on the machine, from the service
    notes (pp. 6, 9) and tools/tr808/tom.cir, its reference. Four circuits, each a wave digital filter with an R-type
    root (CircuitWdf.h), each driving the next one way, as an ideal source:

        Front       the trigger as the bass drum's: Q32 onto the TRIG line, the pair's PNP switch (Q51, Q54, Q57) through
                    the instrument line's transistor, R215 (R251, R270) as its load; the pulse into the resonator
                    (C73, D56 clamping its falling edge, R226 into the op-amp's plus input over R225, and for the conga
                    R224 too); the noise envelope (R219, D55, C72), which gives its charge up through R218 to the gate
        Noise gate  the pink noise bus through R216, R217 and C71 onto Q52's base, which the envelope opens through
                    R218 and R220; Q52's collector (R221) through C74, R223, R222 and C75 to the mixer through R232,
                    which the conga leaves open
        Resonator   a bridged-T around the first op-amp: cin and cout, the tom's capacitor across cout and for the mid
                    pair C89 across cin, R228 across both, R231 and the TUNING pot (500 Ohm B) to ground with the
                    germanium pair D80, D81 across R231 and the pot's other part (the pitch drop); the second op-amp
                    inverts it back to the middle node through the feedback resistor (R230, R259, R286)
        Mixer       IC13 (IC16): the tone through R233 and cmix, the noise through R232, R235 470 kOhm and C83 1 nF and
                    the LEVEL pot (100 kOhm A) with C82 in its feedback

    R218 joins the front and the gate a sample apart, each circuit holding the other's end as a source: 2.2 MOhm
    against C72 is some 80 ms, thousands of samples. The feedback resistors, printed 820 kOhm, with which the toms and
    congas ring some 10 % to 30 % longer than both real units, are fitted per pair to the units' decays (D252). The
    germanium pair (1S188FM) is a typical germanium small-signal diode, Is 2 uA, emission 1.3, 10 Ohm in series;
    transistors and silicon diodes as the bass drum's; the resonator's op-amps of 3 MHz gain-bandwidth and a +-14 V
    swing, the mixer's ideal. Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808Tom final
    {
    public:
        enum class Pair : int { low = 0, mid, high };

        /** A pair's own parts (service notes, p. 9): low (IC15, IC13), mid (IC17, IC16), high (IC18, IC16). */
        struct PairValues
        {
            double cin, cout, ctom, cintom, cpulse, rbias, rconga, cenv, cn1, cn2, cmix, rfb;
        };

        static constexpr double thermal = Tr808BassDrum::thermal;
        static constexpr double transistorSaturation = Tr808BassDrum::transistorSaturation;
        static constexpr double betaF = Tr808BassDrum::betaF, betaR = Tr808BassDrum::betaR;
        static constexpr double diodeSaturation = Tr808BassDrum::diodeSaturation;
        static constexpr double diodeThermal = thermal * Tr808BassDrum::diodeEmission;
        static constexpr double germaniumSaturation = 2.0e-6, germaniumThermal = thermal * 1.3;
        static constexpr double supply = 15.0;
        static constexpr double followerDrop = Tr808BassDrum::followerDrop;
        static constexpr double instrumentHold = Tr808BassDrum::instrumentHold;
        static constexpr double switchOff = Tr808BassDrum::switchOff;
        static constexpr double absent = 1.0e-12; // a capacitor the switch takes out, as the reference's 1 pF
        /** The resonators' op-amps' gain-bandwidth (uPC4558), as the reference's single pole: at the congas' and the
            claves' Q it moves their ring, so the resonators keep it; the mixers' op-amps are ideal. */
        static constexpr double opAmpBandwidth = 3.0e6;
        /** Their output swing on the +-15 V rails: the uPC4558's typical +-14 V into 10 kOhm. Only a full accent
            reaches it, the rim shot's and the claves' resonators most. */
        static constexpr double opAmpRail = 14.0;

        static constexpr PairValues valuesOf(Pair p) noexcept
        {
            switch (p)
            {
                case Pair::mid:  return { 27.0e-9, 12.0e-9, 27.0e-9, 8.2e-9, 2.7e-9, 6.8e3, 2.2e3, 33.0e-9, 33.0e-9, 15.0e-9, 39.0e-9, 865.0e3 };
                case Pair::high: return { 27.0e-9, 5.6e-9, 22.0e-9, absent, 2.7e-9, 5.6e3, 1.5e3, 33.0e-9, 27.0e-9, 10.0e-9, 33.0e-9, 880.0e3 };
                case Pair::low:
                default:         return { 56.0e-9, 12.0e-9, 47.0e-9, absent, 6.8e-9, 4.7e3, 1.5e3, 39.0e-9, 47.0e-9, 18.0e-9, 47.0e-9, 865.0e3 };
            }
        }

        explicit Tr808Tom(Pair which = Pair::low) : pair(which), parts(valuesOf(which))
        {
            front.setNewtonLimit(40); // the trigger's edges switch two transistors and two junctions at once
            gate.setNewtonLimit(40);
            buildFront();
            buildGate();
            buildResonator();
            buildMixer();
        }

        /** At a sample rate, at rest (a copy of the solved rest state: cheap enough for the audio thread). */
        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            front.setSampleRate(rate);
            gate.setSampleRate(rate);
            resonator.setSampleRate(rate);
            mixer.setSampleRate(rate);
            rest();
        }

        /** Back to rest: a copy of the solved rest state, no solving. */
        void rest() noexcept
        {
            const RestState& r = restState(pair);
            front.setResistance(r213, switchOff);
            front.restore(r.front);
            gate.restore(r.gate);
            resonator.restore(r.resonator);
            mixer.restore(r.mixer);
            triggerSample = -1;
            frontResting = gateResting = true;
            lastEnvelope = r.envelopeVolts;
            lastGateBase = r.gateBaseVolts;
            lastNoise = 0.0;
            stillSamples = 0;
            setRestInputs();
        }

        /** The rest state of every pair, solved on first use and kept for the process: call it off the audio thread. */
        static void warmUp() noexcept
        {
            for (const auto p : { Pair::low, Pair::mid, Pair::high })
                (void) restState(p);
        }

        /** Back to the circuit at rest by solving it: capacitors charged as they are with no trigger. */
        void settle() noexcept
        {
            triggerSample = -1;
            front.setResistance(r213, switchOff);
            lastEnvelope = lastGateBase = lastNoise = 0.0;
            setRestInputs();
            for (int pass = 0; pass < 3; ++pass)
            {
                front.setSource(r218Front, lastGateBase);
                front.settle();
                lastEnvelope = front.probe(frontEnvelope);
                gate.setInput(gateEnvelope, lastEnvelope);
                gate.settle();
                lastGateBase = gate.probe(gateBase);
            }
            resonator.setInput(resonatorIn, front.probe(frontPulse));
            resonator.settle();
            mixer.setInput(mixerTone, resonator.probe(resonatorOut));
            mixer.setInput(mixerNoise, gate.probe(gateOut));
            mixer.settle();
            frontResting = gateResting = true;
            stillSamples = 0;
        }

        /** A hit: the TRIG line at this many volts (4 unaccented to 14), for this long. */
        void trigger(double volts, double widthSeconds = 0.001) noexcept
        {
            triggerVolts = volts;
            triggerWidth = widthSeconds;
            triggerSample = 0;
            front.setResistance(r213, 10.0e3); // the instrument data stands: the line's transistor on
        }

        /** SW8 (SW9, SW10): the conga takes the tom's capacitors out, adds R224 at the plus input and leaves the noise out. */
        void setConga(bool conga) noexcept
        {
            isConga = conga;
            resonator.setCapacitance(ctomPort, conga ? absent : parts.ctom);
            resonator.setCapacitance(cintomPort, conga ? absent : parts.cintom);
            front.setResistance(r224, conga ? parts.rconga : switchOff);
            gate.setResistance(r232Gate, conga ? switchOff : 100.0e3);
            mixer.setResistance(r232Mixer, conga ? switchOff : 100.0e3);
        }
        [[nodiscard]] bool conga() const noexcept { return isConga; }

        /** TUNING 0..1: the 500 Ohm linear pot, its share to ground (with R231) falling as it turns up. */
        void setTuning(double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            resonator.setResistance(rvb, std::max(0.01, (1.0 - t) * 500.0));
            resonator.setResistance(rva, std::max(0.01, t * 500.0));
        }

        /** LEVEL 0..1: the 100 kOhm audio-taper pot in the mixer's feedback. */
        void setLevel(double turn) noexcept { mixer.setResistance(vr10, levelOhms(turn)); }

        /** An audio-taper (A) 100 kOhm pot's resistance at a turn of 0..1, as the reference reads it. */
        [[nodiscard]] static double levelOhms(double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            return std::max(1.0, 100.0e3 * (std::pow(10.0, 2.0 * t) - 1.0) / 99.0);
        }

        /** One sample, the pink noise bus at its volts; returns the output in volts. */
        double process(double pinkNoise) noexcept
        {
            // Q32's base, as the bass drum's (Tr808BassDrum::process): the step half a sample into the hit.
            double drive = 0.0;
            if (triggerSample >= 0)
            {
                const int width = std::max(1, static_cast<int>(std::lround(triggerWidth * sampleRate)));
                if (triggerSample >= 1 && triggerSample <= width)
                    drive = triggerVolts + followerDrop;
                if (triggerSample >= width + static_cast<int>(std::lround(instrumentHold * sampleRate)))
                {
                    front.setResistance(r213, switchOff);
                    triggerSample = -1;
                }
                else
                    ++triggerSample;
            }
            // The front and the gate rest once the hit has passed through them and the envelope has let go of Q52
            // (their outputs still), so the tail costs only the resonator and the mixer; a new hit wakes them.
            const bool active = triggerSample >= 0;
            if (active || ! frontResting)
            {
                front.setInput(frontDrive, drive);
                front.setSource(r218Front, lastGateBase);
                front.process();
                const double e = front.probe(frontEnvelope);
                stillSamples = std::abs(e - lastEnvelope) < 1.0e-6 ? stillSamples + 1 : 0;
                lastEnvelope = e;
                frontResting = ! active && std::abs(front.probe(frontPulse)) < 1.0e-9 && std::abs(front.probe(frontTrigger)) < 1.0e-6
                               && stillSamples > static_cast<int>(0.005 * sampleRate);
            }
            if (active || ! frontResting || ! gateResting)
            {
                gate.setInput(gateEnvelope, lastEnvelope);
                gate.setInput(gateNoise, pinkNoise);
                gate.process();
                lastGateBase = gate.probe(gateBase);
                lastNoise = gate.probe(gateOut);
                gateResting = frontResting && std::abs(lastNoise) < 1.0e-7;
            }
            resonator.setInput(resonatorIn, frontResting ? 0.0 : front.probe(frontPulse));
            resonator.process();
            mixer.setInput(mixerTone, resonator.probe(resonatorOut));
            mixer.setInput(mixerNoise, gateResting ? 0.0 : lastNoise);
            mixer.process();
            return mixer.probe(mixerOut);
        }

        // The stages' last outputs, for tests against the reference.
        [[nodiscard]] double triggerNodeVolts() const noexcept { return front.probe(frontTrigger); }
        [[nodiscard]] double pulseVolts() const noexcept { return front.probe(frontPulse); }
        [[nodiscard]] double envelopeVolts() const noexcept { return lastEnvelope; }
        [[nodiscard]] double gateBaseVolts() const noexcept { return lastGateBase; }
        [[nodiscard]] double noiseVolts() const noexcept { return lastNoise; }
        [[nodiscard]] double middleVolts() const noexcept { return resonator.probe(resonatorMiddle); }
        [[nodiscard]] double resonatorVolts() const noexcept { return resonator.probe(resonatorOut); }
        [[nodiscard]] int newtonSteps() const noexcept
        {
            return std::max({ frontResting ? 0 : front.lastNewtonSteps(), gateResting ? 0 : gate.lastNewtonSteps(), resonator.lastNewtonSteps() });
        }
        [[nodiscard]] bool frontIsResting() const noexcept { return frontResting && gateResting; }
        [[nodiscard]] bool lastSolveConverged() const noexcept
        {
            return front.lastSolveConverged() && gate.lastSolveConverged() && resonator.lastSolveConverged();
        }

    private:
        void setRestInputs() noexcept
        {
            front.setInput(frontDrive, 0.0);
            front.setInput(frontSupply, supply);
            front.setSource(r218Front, lastGateBase);
            gate.setInput(gateEnvelope, lastEnvelope);
            gate.setInput(gateNoise, 0.0);
            gate.setInput(gateSupply, supply);
            resonator.setInput(resonatorIn, 0.0);
            mixer.setInput(mixerTone, 0.0);
            mixer.setInput(mixerNoise, 0.0);
        }

        // Front: 1 Q32's drive (input), 2 its base, 3 the supply (input), 4 the TRIG line, 5 the PNP switch's base,
        // 6 its collector (the pair's trigger), 7 after C73, 8 the resonator's plus input, 9 D55's anode, 10 C72.
        void buildFront()
        {
            frontDrive = front.addInput(1);
            frontSupply = front.addInput(3);
            front.addResistor(1, 2, 4.7e3);                                               // R120
            front.addTransistor(3, 2, 4, transistorSaturation, betaF, betaR, thermal);    // Q32
            front.addResistor(4, 0, 4.7e3);                                               // R113
            front.addResistor(4, 5, 4.7e3);                                               // R214
            r213 = front.addResistor(5, 0, switchOff);                                    // R213 through Q50: open but while the data stands
            front.addPnpTransistor(6, 5, 4, transistorSaturation, betaF, betaR, thermal); // Q51
            front.addResistor(6, 0, 4.7e3);                                               // R215
            front.addCapacitor(6, 7, parts.cpulse);                                       // C73
            front.addDiode(0, 7, diodeSaturation, diodeThermal);                          // D56, its anode at ground
            front.addResistor(7, 8, 22.0e3);                                              // R226
            front.addResistor(8, 0, parts.rbias);                                         // R225
            r224 = front.addResistor(8, 0, switchOff);                                    // R224, the conga's
            front.addResistor(6, 9, 4.7e3);                                               // R219
            front.addDiode(9, 10, diodeSaturation, diodeThermal);                         // D55
            front.addCapacitor(10, 0, parts.cenv);                                        // C72
            r218Front = front.addSource(10, 0, 2.2e6);                                    // R218, to the gate's base node
            frontTrigger = front.addProbe(6);
            frontPulse = front.addProbe(8);
            frontEnvelope = front.addProbe(10);
        }

        // Noise gate: 1 C72 (input), 2 the gate's base node, 3 the pink noise bus (input), 4 its divider, 5 Q52's
        // base, 6 its collector, 7 the supply (input), 8 after C74, 9 C75 (the noise to the mixer).
        void buildGate()
        {
            gateEnvelope = gate.addInput(1);
            gateNoise = gate.addInput(3);
            gateSupply = gate.addInput(7);
            gate.addResistor(1, 2, 2.2e6);                                             // R218
            gate.addResistor(3, 4, 15.0e3);                                            // R216
            gate.addResistor(4, 0, 3.3e3);                                             // R217
            gate.addCapacitor(4, 2, 47.0e-9);                                          // C71
            gate.addResistor(2, 5, 33.0e3);                                            // R220
            gate.addTransistor(6, 5, 0, transistorSaturation, betaF, betaR, thermal);  // Q52
            gate.addResistor(7, 6, 4.7e3);                                             // R221
            gate.addCapacitor(6, 8, parts.cn1);                                        // C74
            gate.addResistor(8, 0, 22.0e3);                                            // R223
            gate.addResistor(8, 9, 22.0e3);                                            // R222
            gate.addCapacitor(9, 0, parts.cn2);                                        // C75
            r232Gate = gate.addResistor(9, 0, 100.0e3);                                // R232, to the mixer's virtual ground
            gateBase = gate.addProbe(2);
            gateOut = gate.addProbe(9);
        }

        // Resonator: 1 the plus input (input), 2 the first op-amp's minus, 3 the middle node, 4 its output, 5 the
        // TUNING pot's wiper, 6 its other end, 7 the second op-amp's minus, 8 its output, 9 and 10 the op-amps' inner
        // nodes, 11 the germanium diodes' junction side.
        void buildResonator()
        {
            resonatorIn = resonator.addInput(1);
            resonator.addCapacitor(2, 3, parts.cin);                                   // C76 (C90, C103)
            cintomPort = resonator.addCapacitor(2, 3, parts.cintom);                   // C89, the mid tom's
            resonator.addCapacitor(3, 4, parts.cout);                                  // C78 (C92, C105)
            ctomPort = resonator.addCapacitor(3, 4, parts.ctom);                       // C77 (C91, C104), the tom's
            resonator.addResistor(2, 4, 820.0e3);                                      // R228
            resonator.addResistor(3, 5, 1.0e3);                                        // R231
            rvb = resonator.addResistor(5, 0, 250.0);                                  // TUNING, to ground
            rva = resonator.addResistor(6, 5, 250.0);                                  // and its other part
            resonator.addResistor(3, 11, 10.0);                                        // the germanium diodes' series resistance
            resonator.addDiodePair(11, 6, germaniumSaturation, germaniumThermal);      // D80, D81
            resonator.addResistor(4, 7, 33.0e3);                                       // R229
            resonator.addResistor(7, 8, 33.0e3);                                       // R227
            resonator.addResistor(8, 3, parts.rfb);                                    // R230 (R259, R286), fitted
            resonator.addOpAmp(1, 2, 4, 9, opAmpBandwidth, opAmpRail);                 // IC15a
            resonator.addOpAmp(0, 7, 8, 10, opAmpBandwidth, opAmpRail);                // IC15b
            resonatorMiddle = resonator.addProbe(3);
            resonatorOut = resonator.addProbe(8);
        }

        // Mixer: 1 the resonator (input), 2 after R233, 3 the op-amp's minus, 4 its output, 5 the LEVEL pot to C82,
        // 6 the noise (input).
        void buildMixer()
        {
            mixerTone = mixer.addInput(1);
            mixerNoise = mixer.addInput(6);
            mixer.addResistor(1, 2, 100.0e3);                  // R233
            mixer.addCapacitor(2, 3, parts.cmix);              // C81 (C96, C108)
            r232Mixer = mixer.addResistor(6, 3, 100.0e3);      // R232
            mixer.addResistor(3, 4, 470.0e3);                  // R235
            mixer.addCapacitor(3, 4, 1.0e-9);                  // C83
            vr10 = mixer.addResistor(3, 5, levelOhms(0.8));    // VR10, LEVEL
            mixer.addCapacitor(5, 4, 33.0e-6);                 // C82
            mixer.addNullor(0, 3, 4);                          // IC13
            mixerOut = mixer.addProbe(4);
        }

        wdf::Circuit<10, 18, 2, 0, 6, 3> front;
        wdf::Circuit<9, 13, 3, 0, 2, 2> gate;
        wdf::Circuit<11, 19, 1, 2, 3, 2> resonator;
        wdf::Circuit<6, 7, 2, 1, 0, 1> mixer;

        struct RestState
        {
            decltype(front)::Snapshot front {};
            decltype(gate)::Snapshot gate {};
            decltype(resonator)::Snapshot resonator {};
            decltype(mixer)::Snapshot mixer {};
            double envelopeVolts = 0.0, gateBaseVolts = 0.0;
        };

        static const RestState& restState(Pair which) noexcept
        {
            static const std::array<RestState, 3> states = []
            {
                std::array<RestState, 3> s {};
                for (int k = 0; k < 3; ++k)
                {
                    auto tom = std::make_unique<Tr808Tom>(static_cast<Pair>(k));
                    tom->sampleRate = 96000.0;
                    tom->settle();
                    auto& r = s[static_cast<std::size_t>(k)];
                    r.front = tom->front.snapshot();
                    r.gate = tom->gate.snapshot();
                    r.resonator = tom->resonator.snapshot();
                    r.mixer = tom->mixer.snapshot();
                    r.envelopeVolts = tom->lastEnvelope;
                    r.gateBaseVolts = tom->lastGateBase;
                }
                return s;
            }();
            return states[static_cast<std::size_t>(std::clamp(static_cast<int>(which), 0, 2))];
        }

        Pair pair = Pair::low;
        PairValues parts {};
        double sampleRate = 96000.0;
        double triggerVolts = 4.0, triggerWidth = 0.001;
        int triggerSample = -1; // samples since the hit, -1 between hits
        bool frontResting = true, gateResting = true, isConga = false;
        double lastEnvelope = 0.0, lastGateBase = 0.0, lastNoise = 0.0;
        int stillSamples = 0;

        int frontDrive = 0, frontSupply = 0, frontTrigger = 0, frontPulse = 0, frontEnvelope = 0, r213 = 0, r224 = 0, r218Front = 0;
        int gateEnvelope = 0, gateNoise = 0, gateSupply = 0, gateBase = 0, gateOut = 0, r232Gate = 0;
        int resonatorIn = 0, resonatorMiddle = 0, resonatorOut = 0, ctomPort = 0, cintomPort = 0, rvb = 0, rva = 0;
        int mixerTone = 0, mixerNoise = 0, mixerOut = 0, r232Mixer = 0, vr10 = 0;
    };
}
