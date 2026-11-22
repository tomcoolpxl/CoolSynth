#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"
#include "Tr808BassDrum.h"
#include "Tr808Tom.h"

/*
    The Roland TR-808's hand clap or maracas (D256), one circuit with its switch (SW12) as on the machine, from the
    service notes (pp. 6, 9, Fig. 13; page 9's change, 10 kOhm from the -15 V line to Q70's emitter) and
    tools/tr808/clap.cir, its reference. Ten circuits, each a wave digital filter with an R-type root (CircuitWdf.h),
    each driving the next one way, as an ideal source, or joined a sample apart through a resistor:

        Front       the trigger as the bass drum's: Q32 onto the TRIG line, the PNP switch Q29 (R105, R106 through
                    Q28), the CP trigger out's load (page 8's Q30, R109, R110); SW12 to the clap's side, Q69 following
                    the trigger, charging C139 through D69 and C143 through D68, R360 and R361 bringing it to IC23's
                    pin 8; or to the maracas' side, C135 charged through D67 (R345 below it)
        Tail        C139, C143 and C135 once the hit has passed, with D69 and D68, whose leakage near no bias ties the
                    reverb's envelope to the C.P. OFFSET at rest
        IC21        a multiple-feedback band-pass on the white noise (R332 with C127 across it, R333, C128, C129,
                    R334), and its way to IC22 (C136, R372 over R382)
        Reverb      R348 from C139 onto C138, R349 into Q70's base with the band-passed noise through R342 over R346
                    and C137; R369 and the change's 10 kOhm in its emitter, R363 and C146 on its collector, C152 and
                    R376 into IC19
        Bursts      IC23 (AN6912) as four open-collector switches to -15 V: A holds C140 low while the trigger stands,
                    R350 gives it up; B holds pin 1 low while C140 is under -5 V (the ladder R353, R352, R351); C lets
                    C144 charge through R365 and D71 while pin 1 is low; D fires Q73 through R358 when C144 (through
                    R356, C142 its positive feedback) rises above pin 1's share (R355, R354), and Q73 empties C144
                    through R367; C141 and D70 pass pin 1's rise to Q73 through R357, the fourth clap
        Pair        Q72 and Q71 from R366: Q72's base from C144 (R368 over R371), Q71's from the C.P. OFFSET (TM3
                    through R370), the accent (C143 through R362) and R364; Q72's collector through R374 into IC22's
                    control input, a Wilson mirror (one junction and some 400 Ohm above -15 V)
        IC22        the BA662: Iabc tanh(v / 2 VT) into R383, its buffer a follower (Open Music Labs' and Alfa's
                    AS662D's clone schematics), through C150 and R378 into IC19
        Maracas     two circuits: the envelope, R344 from C135 into Q67, whose collector (R343) empties C134 through
                    R341 (refilled through D66 when it lets go); and the noise, Q66 following C134, Q65 (its base on
                    the white noise through R335 and C130, R336) feeding Q66's emitter through D65 and R337, that node
                    through C131 (R338) into Q68's high-pass (C132, C133, R339 back from its emitter, R340, R347), C151
                    and R377 into IC19. They join at Q66's base: the noise takes C134's voltage, the envelope Q66's base
                    current (a few uA into 33 nF) a sample later
        Mixer       IC19: R376, R378 and R377's currents; R375 470 kOhm, C149 220 pF and the LEVEL pot (100 kOhm A)
                    with C145 in its feedback

    The resistors R348, R362, R344 and R368 join their circuits a sample apart, each circuit holding the other's end as
    a source (1 MOhm against 18 nF, 330 kOhm against 1 uF, 220 kOhm against 0.1 uF, 470 kOhm against C144). IC23's
    switches change at the sample where their inputs cross: the sample is solved again with the new switch. The
    capacitors a diode or a switch charges within a sample (C139, C143, C135, C140, C141) and the junction capacitances
    that untie series devices take the backward Euler rule: the bilinear transform would overshoot there, and the diode
    lock the overshoot in. Q68's high-pass is prewarped to its 10.6 kHz resonance.

    The user's choices (D257): C140 0.027 uF (printed 0.0027; the text's 30 ms, Fig. 13 and both units), C144 0.39 uF
    (drawn 0.47; both units' clap spacing), TM3 at the service notes' adjustment (each clap falls from 6 to 0.5 Vpp),
    IC21's C127 0.82 nF (drawn 1.8) and C128, C129 5.2 nF (+10 %), fitted to both units' spectrum. Transistors and diodes
    as the bass drum's; IC21's op-amp of 3 MHz gain-bandwidth and a +-14 V swing, IC19 ideal. The bends (D256): Decay
    scales the reverb's envelope (C139, C138), Spread the claps' timing (C140, C144), Maracas Decay C135 and C134. Volts
    in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808Clap final
    {
    public:
        static constexpr double thermal = Tr808BassDrum::thermal;
        static constexpr double transistorSaturation = Tr808BassDrum::transistorSaturation;
        static constexpr double betaF = Tr808BassDrum::betaF, betaR = Tr808BassDrum::betaR;
        static constexpr double diodeSaturation = Tr808BassDrum::diodeSaturation;
        static constexpr double diodeThermal = thermal * Tr808BassDrum::diodeEmission;
        static constexpr double supply = 15.0;
        static constexpr double followerDrop = Tr808BassDrum::followerDrop;
        static constexpr double instrumentHold = Tr808BassDrum::instrumentHold;
        static constexpr double switchOff = Tr808BassDrum::switchOff;
        static constexpr double switchOn = 1.0;      // SW12's closed side
        static constexpr double comparatorOn = 67.0; // the AN6912's output: 0.2 V at 3 mA
        static constexpr double c140 = 0.027e-6, c144 = 0.39e-6; // the user's choices (D257)
        static constexpr double tm3 = 0.62;                       // the C.P. OFFSET, as the service notes adjust it
        static constexpr double c127 = 0.82e-9, c128 = 5.17e-9, c129 = 5.17e-9; // fitted to both units' spectrum (D257)
        static constexpr double c139 = 39.0e-9, c138 = 47.0e-9, c135 = 0.1e-6, c134 = 33.0e-9; // C139 printed 18 nF, fitted to both units' tails (D258)
        static constexpr double mirrorSaturation = 10.0e-15, mirrorOhms = 400.0;
        static constexpr double ota6Ohms = 82.0e3; // R383, IC22's load
        static constexpr double senseOhms = 1.0, injectOhms = 1.0e9; // the maracas' two circuits' join at Q66's base
        /** Q68's high-pass's resonance: 1 / (2 pi 1 nF sqrt(R339 R340)). */
        static constexpr double maracasPassHz = 1.0 / (6.283185307179586 * 1.0e-9 * 14980.0);

        Tr808Clap()
        {
            front.setNewtonLimit(40); // the trigger's edges switch four transistors at once
            tail.setNewtonLimit(40);
            reverb.setNewtonLimit(40);
            bursts.setNewtonLimit(40);
            pair.setNewtonLimit(40);
            maEnvelope.setNewtonLimit(40);
            maNoise.setNewtonLimit(40);
            buildFront();
            buildTail();
            buildBandPass();
            buildReverb();
            buildBursts();
            buildPair();
            buildMaracasEnvelope();
            buildMaracasNoise();
            buildMixer();
            setMaracas(false);
            setOffset(tm3);
        }

        /** At a sample rate, at rest (a copy of the solved rest state: cheap enough for the audio thread). */
        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            front.setSampleRate(rate);
            tail.setSampleRate(rate);
            bandPass.setSampleRate(rate);
            reverb.setSampleRate(rate);
            bursts.setSampleRate(rate);
            pair.setSampleRate(rate);
            maEnvelope.setSampleRate(rate);
            maNoise.setSampleRate(rate);
            // Q68's high-pass rings at 10.6 kHz (C132, C133 1 nF, R339, R340: Q 2.3), where the bilinear transform
            // would put it 3.7 % low at 96 kHz: its two capacitors are prewarped to it, so it rings where the circuit does.
            const double half = 3.141592653589793 * maracasPassHz / rate;
            const double warp = half / std::tan(std::min(half, 1.5));
            maNoise.setCapacitance(c132Ma, 1.0e-9 * warp);
            maNoise.setCapacitance(c133Ma, 1.0e-9 * warp);
            mixer.setSampleRate(rate);
            rest();
        }

        /** Back to rest: a copy of the solved rest state, no solving. */
        void rest() noexcept
        {
            const RestState& r = restState(isMaracas);
            front.setResistance(r106, switchOff);
            front.restore(r.front);
            tail.restore(r.tail);
            restClap(r);
            restMaracas(r);
            mixer.restore(r.mixer);
            triggerSample = -1;
            frontResting = true;
            lastC139 = r.c139Volts;
            lastC143 = r.c143Volts;
            lastC135 = r.c135Volts;
            setRestInputs();
        }

        /** The rest states (both SW12 positions), solved on first use and kept for the process: call it off the audio thread. */
        static void warmUp() noexcept
        {
            (void) restState(false);
            (void) restState(true);
        }

        /** Back to the circuit at rest by solving it: capacitors charged as they are with no trigger. */
        void settle() noexcept
        {
            triggerSample = -1;
            front.setResistance(r106, switchOff);
            comparators = { false, false, true, true }; // as the service notes have them at rest: pins 13 and 2 low
            applyComparators();
            lastC138 = lastNh = lastVb72 = lastVb67 = lastC134 = lastQ66Base = 0.0;
            lastC139 = lastC143 = lastC135 = 0.0;
            setRestInputs();
            // R348's chain (C139, C138, Q70's base) carries no current at rest, Q70 being off: C139 rests where D69 and
            // D68 tie it, at the C.P. OFFSET's voltage. Passed back and forth a sample apart it would creep there by
            // D69's leakage times 1 MOhm a pass, so the front and the tail solve it with R348 open.
            front.setResistance(r348Front, switchOff);
            tail.setResistance(r348Tail, switchOff);
            for (int pass = 0; pass < 12; ++pass)
            {
                front.setSource(r348Front, lastC138);
                front.setSource(r362Front, lastNh);
                front.setSource(r344Front, lastVb67);
                front.settle();
                lastC139 = front.probe(frontC139);
                lastC143 = front.probe(frontC143);
                lastC135 = front.probe(frontC135);
                bandPass.settle();
                reverb.setSource(r348Reverb, lastC139);
                reverb.setInput(reverbNoise, bandPass.probe(bandOut));
                reverb.settle();
                lastC138 = reverb.probe(reverbC138);
                bursts.setSource(r368Bursts, lastVb72);
                bursts.settle();
                const bool changed = decideComparators(front.probe(frontPin8));
                pair.setSource(r368Pair, bursts.probe(burstsQ73Collector));
                pair.setSource(r362Pair, lastC143);
                pair.settle();
                lastVb72 = pair.probe(pairQ72Base);
                lastNh = pair.probe(pairQ71Base);
                stepMaracas(0.0, true);
                if (changed)
                    applyComparators();
            }
            tail.setSource(r348Tail, lastC138);
            tail.setSource(r362Tail, lastNh);
            tail.setSource(r344Tail, lastVb67);
            tail.settle();
            front.setResistance(r348Front, 1.0e6);
            tail.setResistance(r348Tail, 1.0e6);
            lastIabc = abcCurrent();
            lastOta = 0.0;
            mixer.setInput(mixerReverb, reverb.probe(reverbCollector));
            mixer.setInput(mixerBursts, 0.0);
            mixer.setInput(mixerMaracas, maNoise.probe(maFollower));
            mixer.settle();
            frontResting = reverbResting = burstsResting = maracasResting = true;
            reverbQuiet = burstsQuiet = maracasQuiet = 0;
            heldReverb = reverb.probe(reverbCollector);
            heldMaracas = maNoise.probe(maFollower);
        }

        /** A hit: the TRIG line at this many volts (4 unaccented to 14), for this long. */
        void trigger(double volts, double widthSeconds = 0.001) noexcept
        {
            if (frontResting)
            {
                // The front wakes from rest with its capacitors as the tail left them.
                front.restore(restState(isMaracas).front);
                front.adoptSharedState(tail);
                frontResting = false;
            }
            if (isMaracas)
                maracasResting = false;
            else
                reverbResting = burstsResting = false;
            reverbQuiet = burstsQuiet = maracasQuiet = 0;
            triggerVolts = volts;
            triggerWidth = widthSeconds;
            triggerSample = 0;
            front.setResistance(r106, 10.0e3); // the instrument data stands: Q28 on
        }

        /** SW12: the maracas' side (or the clap's). While the front rests it takes that side's rest state. */
        void setMaracas(bool maracasSide) noexcept
        {
            isMaracas = maracasSide;
            front.setResistance(sw12Clap, maracasSide ? switchOff : switchOn);
            front.setResistance(sw12Maracas, maracasSide ? switchOn : switchOff);
        }
        [[nodiscard]] bool maracasSelected() const noexcept { return isMaracas; }

        /** Prepares SW12 for a hit on this side: from rest, the front takes that side's rest state. */
        void select(bool maracasSide) noexcept
        {
            if (maracasSide == isMaracas)
                return;
            setMaracas(maracasSide);
            if (frontResting)
                front.restore(restState(maracasSide).front);
        }

        /** LEVEL 0..1: the 100 kOhm audio-taper pot in IC19's feedback. */
        void setLevel(double turn) noexcept { mixer.setResistance(vr17, Tr808Tom::levelOhms(turn)); }

        /** The C.P. OFFSET trimmer TM3, 0 at ground to 1 at -15 V (a calibration: the service notes' adjustment). */
        void setOffset(double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            pair.setResistance(tm3Top, std::max(1.0, 10.0e3 * t));
            pair.setResistance(tm3Bottom, std::max(1.0, 10.0e3 * (1.0 - t)));
        }

        /** Decay: the reverb's envelope times this (C139 and C138). */
        void setDecayScale(double scale) noexcept
        {
            const double k = std::clamp(scale, 0.1, 10.0);
            front.setCapacitance(c139Front, c139 * k);
            tail.setCapacitance(c139Tail, c139 * k);
            reverb.setCapacitance(c138Reverb, c138 * k);
        }

        /** For fitting the clap's tail (tests only, D258): C139, C138 and R349. None moves the rest: R348's chain carries
            no current there. */
        void setReverbParts(double c139Farads, double c138Farads, double r349Ohms) noexcept
        {
            front.setCapacitance(c139Front, c139Farads);
            tail.setCapacitance(c139Tail, c139Farads);
            reverb.setCapacitance(c138Reverb, c138Farads);
            reverb.setResistance(r349Reverb, r349Ohms);
        }

        /** Spread: the claps' spacing and window times this (C144 and C140). */
        void setSpreadScale(double scale) noexcept
        {
            const double k = std::clamp(scale, 0.1, 10.0);
            bursts.setCapacitance(c140Bursts, c140 * k);
            bursts.setCapacitance(c144Bursts, c144 * k);
        }

        /** Maracas Decay: the maracas' envelope times this (C135 and C134). */
        void setMaracasDecayScale(double scale) noexcept
        {
            const double k = std::clamp(scale, 0.1, 10.0);
            front.setCapacitance(c135Front, c135 * k);
            tail.setCapacitance(c135Tail, c135 * k);
            maEnvelope.setCapacitance(c134Ma, c134 * k);
        }

        /** One sample, the white noise bus at its volts; returns the output in volts. */
        double process(double whiteNoise) noexcept
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
                    front.setResistance(r106, switchOff);
                    triggerSample = -1;
                }
                else
                    ++triggerSample;
            }
            const bool active = triggerSample >= 0;
            const RestState& r = restState(isMaracas);
            double pin8 = r.pin8Volts;
            if (! frontResting)
            {
                front.setInput(frontDrive, drive);
                front.setSource(r348Front, lastC138);
                front.setSource(r362Front, lastNh);
                front.setSource(r344Front, lastVb67);
                front.process();
                lastC139 = front.probe(frontC139);
                lastC143 = front.probe(frontC143);
                lastC135 = front.probe(frontC135);
                pin8 = front.probe(frontPin8);
                // The front rests once the hit has passed, its nodes are back at rest and Q69 has let go: the tail
                // takes the capacitors over.
                if (! active && std::abs(front.probe(frontTrigger) - r.triggerVolts) < 1.0e-3
                    && std::abs(pin8 - r.pin8Volts) < 1.0e-3 && front.probe(frontClapSide) - front.probe(frontQ69Emitter) < 0.3)
                {
                    tail.adoptSharedState(front);
                    frontResting = true;
                }
            }
            else if (! clapIsResting() || ! maracasResting)
            {
                tail.setSource(r348Tail, lastC138);
                tail.setSource(r362Tail, lastNh);
                tail.setSource(r344Tail, lastVb67);
                tail.process();
                lastC139 = tail.probe(tailC139);
                lastC143 = tail.probe(tailC143);
                lastC135 = tail.probe(tailC135);
            }

            // A part that has gone quiet stops being solved and holds its last output, so nothing steps at the mixer;
            // rest() takes every part back to rest before a hit once the generator has let the instrument ring down.
            if (! clapIsResting())
            {
                bandPass.setInput(0, whiteNoise);
                bandPass.process();
            }
            if (! reverbResting)
            {
                reverb.setInput(reverbNoise, bandPass.probe(bandOut));
                reverb.setSource(r348Reverb, lastC139);
                reverb.process();
                lastC138 = reverb.probe(reverbC138);
                heldReverb = reverb.probe(reverbCollector);
                // The reverb is quiet once C139 has given C138 its charge (so its envelope only falls from here) and
                // its current into IC19 has stayed under 2 nA (0.1 mV across R376) for 5 ms.
                reverbQuiet = frontResting && lastC139 - lastC138 < 0.01 && std::abs(reverb.probe(reverbOut)) < 1.0e-4 ? reverbQuiet + 1 : 0;
                if (reverbQuiet > static_cast<int>(0.005 * sampleRate))
                    reverbResting = true;
            }
            if (! burstsResting)
            {
                stepBursts(pin8);
                pair.setSource(r368Pair, bursts.probe(burstsQ73Collector));
                pair.setSource(r362Pair, lastC143);
                pair.process();
                lastVb72 = pair.probe(pairQ72Base);
                lastNh = pair.probe(pairQ71Base);
                lastIabc = abcCurrent();
                // IC22: the OTA's current into R383, the buffer following it.
                lastOta = ota6Ohms * lastIabc * std::tanh(-bandPass.probe(bandOta) / (2.0 * thermal));
                // The claps are over once IC23 is back at rest and the control current has stayed under 1 nA (IC22's
                // output under 0.1 mV) for 5 ms.
                const bool quiet = frontResting && comparators == restComparators && std::abs(lastIabc - r.iabc) < 1.0e-9;
                burstsQuiet = quiet ? burstsQuiet + 1 : 0;
                if (burstsQuiet > static_cast<int>(0.005 * sampleRate))
                    burstsResting = true;
            }
            if (! maracasResting)
            {
                stepMaracas(whiteNoise, false);
                heldMaracas = maNoise.probe(maFollower);
                // The maracas are quiet once Q67 has let go for good (C135 under 0.4 V), C134 is back (Q66 off, no
                // current for Q65 to gate) and the output has stayed within 0.1 mV of rest for 5 ms.
                const bool quiet = frontResting && lastC135 - r.c135Volts < 0.4
                                   && std::abs(lastC134 - r.maracasEnvelopeVolts) < 0.05
                                   && std::abs(heldMaracas - r.maracasFollowerVolts) < 1.0e-4;
                maracasQuiet = quiet ? maracasQuiet + 1 : 0;
                if (maracasQuiet > static_cast<int>(0.005 * sampleRate))
                    maracasResting = true;
            }
            mixer.setInput(mixerReverb, heldReverb);
            mixer.setInput(mixerBursts, lastOta);
            mixer.setInput(mixerMaracas, heldMaracas);
            mixer.process();
            return mixer.probe(mixerOut);
        }

        // The stages' last outputs, for tests against the reference.
        [[nodiscard]] double triggerNodeVolts() const noexcept { return frontResting ? restState(isMaracas).triggerVolts : front.probe(frontTrigger); }
        [[nodiscard]] double pin8Volts() const noexcept { return frontResting ? restState(isMaracas).pin8Volts : front.probe(frontPin8); }
        [[nodiscard]] double c139Volts() const noexcept { return lastC139; }
        [[nodiscard]] double c143Volts() const noexcept { return lastC143; }
        [[nodiscard]] double c135Volts() const noexcept { return lastC135; }
        [[nodiscard]] double bandPassVolts() const noexcept { return bandPass.probe(bandOut); }
        [[nodiscard]] double c138Volts() const noexcept { return lastC138; }
        [[nodiscard]] double reverbBaseVolts() const noexcept { return reverb.probe(reverbBase); }
        [[nodiscard]] double reverbCollectorVolts() const noexcept { return reverb.probe(reverbCollector); }
        [[nodiscard]] double c140Volts() const noexcept { return bursts.probe(burstsPin14); }
        [[nodiscard]] double pin1Volts() const noexcept { return bursts.probe(burstsPin1); }
        [[nodiscard]] double pin13Volts() const noexcept { return bursts.probe(burstsPin13); }
        [[nodiscard]] double c144Volts() const noexcept { return bursts.probe(burstsC144); }
        [[nodiscard]] double pin2Volts() const noexcept { return bursts.probe(burstsPin2); }
        [[nodiscard]] double pin5Volts() const noexcept { return bursts.probe(burstsPin5); }
        [[nodiscard]] double q72BaseVolts() const noexcept { return lastVb72; }
        [[nodiscard]] double q71BaseVolts() const noexcept { return lastNh; }
        [[nodiscard]] double q72CollectorVolts() const noexcept { return pair.probe(pairQ72Collector); }
        [[nodiscard]] double abcAmps() const noexcept { return lastIabc; }
        [[nodiscard]] double otaVolts() const noexcept { return lastOta; }
        [[nodiscard]] double q67BaseVolts() const noexcept { return lastVb67; }
        [[nodiscard]] double maracasEnvelopeVolts() const noexcept { return lastC134; }
        [[nodiscard]] double maracasNoiseVolts() const noexcept { return maNoise.probe(maNoiseNode); }
        [[nodiscard]] double maracasFollowerVolts() const noexcept { return maNoise.probe(maFollower); }
        [[nodiscard]] int newtonSteps() const noexcept
        {
            return std::max({ frontResting ? 0 : front.lastNewtonSteps(), reverbResting ? 0 : reverb.lastNewtonSteps(),
                              burstsResting ? 0 : bursts.lastNewtonSteps(), burstsResting ? 0 : pair.lastNewtonSteps(),
                              maracasResting ? 0 : std::max(maEnvelope.lastNewtonSteps(), maNoise.lastNewtonSteps()) });
        }
        [[nodiscard]] bool isResting() const noexcept { return frontResting && clapIsResting() && maracasResting; }
        [[nodiscard]] bool lastSolveConverged() const noexcept
        {
            return (frontResting || front.lastSolveConverged()) && (reverbResting || reverb.lastSolveConverged())
                   && (burstsResting || (bursts.lastSolveConverged() && pair.lastSolveConverged())) && (maracasResting || (maEnvelope.lastSolveConverged() && maNoise.lastSolveConverged()));
        }
        [[nodiscard]] bool isWellPosed() noexcept
        {
            return front.isWellPosed() && tail.isWellPosed() && bandPass.isWellPosed() && reverb.isWellPosed() && bursts.isWellPosed()
                   && pair.isWellPosed() && maEnvelope.isWellPosed() && maNoise.isWellPosed() && mixer.isWellPosed();
        }

    private:
        struct RestState;
        [[nodiscard]] bool clapIsResting() const noexcept { return reverbResting && burstsResting; }
        using Comparators = std::array<bool, 4>; // A, B, C, D: on (sinking to -15 V)
        static constexpr Comparators restComparators { false, false, true, true };

        /** An AN6912 comparator as the reference's switch: on while its minus input is above its plus input less
            1 mV, with 0.1 mV of hysteresis. */
        static bool comparatorState(bool on, double minus, double plus) noexcept
        {
            const double d = minus - plus;
            return on ? d > -1.1e-3 : d > -0.9e-3;
        }

        /** The four comparators from the bursts' probes and pin 8; true when any changed. */
        bool decideComparators(double pin8) noexcept
        {
            const double pin6 = bursts.probe(burstsPin6), pin9 = bursts.probe(burstsPin9);
            const Comparators next {
                comparatorState(comparators[0], pin8, pin9),
                comparatorState(comparators[1], pin6, bursts.probe(burstsPin14)),
                comparatorState(comparators[2], bursts.probe(burstsPin1), pin6),
                comparatorState(comparators[3], bursts.probe(burstsPin4), bursts.probe(burstsPin5)),
            };
            const bool changed = next != comparators;
            comparators = next;
            return changed;
        }

        void applyComparators() noexcept
        {
            for (std::size_t k = 0; k < 4; ++k)
                bursts.setResistance(comparatorPort[k], comparators[k] ? comparatorOn : switchOff);
        }

        /** One sample of the bursts, solved again while a comparator changes inside it. */
        void stepBursts(double pin8) noexcept
        {
            bursts.setSource(r368Bursts, lastVb72);
            const auto before = bursts.snapshot();
            for (int pass = 0; pass < 4; ++pass)
            {
                bursts.process();
                if (! decideComparators(pin8))
                    break;
                applyComparators();
                bursts.restore(before);
            }
        }

        [[nodiscard]] double abcCurrent() const noexcept { return (pair.probe(pairQ72Collector) - pair.probe(pairPin1)) / 10.0e3; }

        void restClap(const RestState& r) noexcept
        {
            bandPass.restore(r.bandPass);
            reverb.restore(r.reverb);
            bursts.restore(r.bursts);
            pair.restore(r.pair);
            comparators = restComparators;
            applyComparators();
            lastC138 = r.c138Volts;
            lastVb72 = r.vb72Volts;
            lastNh = r.nhVolts;
            lastIabc = r.iabc;
            lastOta = 0.0;
            heldReverb = r.reverbCollectorVolts;
            reverbResting = burstsResting = true;
            reverbQuiet = burstsQuiet = 0;
        }

        void restMaracas(const RestState& r) noexcept
        {
            maEnvelope.restore(r.maEnvelope);
            maNoise.restore(r.maNoise);
            lastC134 = r.maracasEnvelopeVolts;
            lastQ66Base = r.q66BaseAmps;
            lastVb67 = r.vb67Volts;
            heldMaracas = r.maracasFollowerVolts;
            maracasResting = true;
            maracasQuiet = 0;
        }

        void setRestInputs() noexcept
        {
            front.setInput(frontDrive, 0.0);
            front.setInput(frontSupply, supply);
            front.setInput(frontNegative, -supply);
            bandPass.setInput(0, 0.0);
            reverb.setInput(reverbNoise, 0.0);
            reverb.setInput(reverbSupply, supply);
            reverb.setInput(reverbNegative, -supply);
            bursts.setInput(burstsNegative, -supply);
            pair.setInput(pairSupply, supply);
            pair.setInput(pairNegative, -supply);
            maEnvelope.setInput(maSupplyE, supply);
            maNoise.setInput(maNoiseIn, 0.0);
            maNoise.setInput(maSupplyN, supply);
            maNoise.setInput(maNegativeN, -supply);
            maNoise.setInput(maC134In, lastC134);
            mixer.setInput(mixerBursts, 0.0);
        }

        // Front: 1 Q32's drive (input), 2 its base, 3 the supply (input), 4 the TRIG line, 5 Q29's base, 6 its
        // collector (the trigger), 7 Q30's base, 8 its collector, 9 SW12's clap side (Q69's base), 10 its maracas
        // side, 11 Q69's emitter, 12 C139, 13 C143, 14 IC23's pin 8, 15 the negative supply (input), 16 C135.
        // The capacitors, their resistors and D69, D68 come first, as the tail's.
        void buildFront()
        {
            c139Front = front.addCapacitor(12, 0, c139, 0.0);                            // C139
            r348Front = front.addSource(12, 0, 1.0e6);                                   // R348, to C138 in the reverb
            front.addCapacitor(13, 0, 1.0e-6, 0.0);                                      // C143
            r362Front = front.addSource(13, 0, 330.0e3);                                 // R362, to Q71's base in the pair
            c135Front = front.addCapacitor(16, 0, c135, 0.0);                            // C135
            front.addResistor(16, 0, 150.0e3);                                           // R345
            r344Front = front.addSource(16, 0, 220.0e3);                                 // R344, to Q67's base in the maracas
            front.addDiode(11, 12, diodeSaturation, diodeThermal);                       // D69
            front.addDiode(11, 13, diodeSaturation, diodeThermal);                       // D68
            frontDrive = front.addInput(1);
            frontSupply = front.addInput(3);
            frontNegative = front.addInput(15);
            front.addResistor(1, 2, 4.7e3);                                              // R120
            front.addTransistor(3, 2, 4, transistorSaturation, betaF, betaR, thermal);   // Q32
            front.addResistor(4, 0, 4.7e3);                                              // R113
            front.addResistor(4, 5, 4.7e3);                                              // R105
            r106 = front.addResistor(5, 0, switchOff);                                   // R106 through Q28: open but while the data stands
            front.addPnpTransistor(6, 5, 4, transistorSaturation, betaF, betaR, thermal); // Q29
            front.addResistor(6, 7, 33.0e3);                                             // R109
            front.addResistor(7, 0, 10.0e3);                                             // R110
            front.addTransistor(8, 7, 0, transistorSaturation, betaF, betaR, thermal);   // Q30
            front.addResistor(3, 8, 33.0e3);                                             // R111
            sw12Clap = front.addResistor(6, 9, switchOn);                                // SW12
            sw12Maracas = front.addResistor(6, 10, switchOff);
            front.addTransistor(3, 9, 11, transistorSaturation, betaF, betaR, thermal);  // Q69
            front.addCapacitor(9, 11, 8.0e-12, 0.0);                                     // Q69's emitter junction (the reference's CJE), backward Euler
            front.addResistor(9, 14, 27.0e3);                                            // R360
            front.addResistor(14, 15, 22.0e3);                                           // R361
            front.addDiode(10, 16, diodeSaturation, diodeThermal);                       // D67
            frontTrigger = front.addProbe(6);
            frontClapSide = front.addProbe(9);
            frontQ69Emitter = front.addProbe(11);
            frontC139 = front.addProbe(12);
            frontC143 = front.addProbe(13);
            frontPin8 = front.addProbe(14);
            frontC135 = front.addProbe(16);
        }

        // Tail: 1 C139, 2 C143, 3 C135, 4 Q69's emitter. Its first nine ports are the front's, in the same order;
        // the tenth is a resistor (a resistor keeps no wave, so taking another port's state is harmless).
        void buildTail()
        {
            c139Tail = tail.addCapacitor(1, 0, c139, 0.0);       // C139
            r348Tail = tail.addSource(1, 0, 1.0e6);              // R348
            tail.addCapacitor(2, 0, 1.0e-6, 0.0);                // C143
            r362Tail = tail.addSource(2, 0, 330.0e3);            // R362
            c135Tail = tail.addCapacitor(3, 0, c135, 0.0);       // C135
            tail.addResistor(3, 0, 150.0e3);                     // R345
            r344Tail = tail.addSource(3, 0, 220.0e3);            // R344
            tail.addDiode(4, 1, diodeSaturation, diodeThermal);  // D69
            tail.addDiode(4, 2, diodeSaturation, diodeThermal);  // D68
            tail.addResistor(4, 0, switchOff);                   // Q69's emitter, its junction reverse-biased: nearly open
            tailC139 = tail.addProbe(1);
            tailC143 = tail.addProbe(2);
            tailC135 = tail.addProbe(3);
        }

        // IC21: 1 the white noise (input), 2 the middle node, 3 the minus input, 4 the output, 5 the op-amp's inner
        // node, 6 C136 to R372, 7 IC22's pin 2.
        void buildBandPass()
        {
            bandPass.addInput(1);
            bandPass.addResistor(1, 2, 27.0e3);      // R332
            bandPass.addCapacitor(1, 2, c127);       // C127
            bandPass.addResistor(2, 0, 10.0e3);      // R333
            bandPass.addCapacitor(2, 3, c128);       // C128
            bandPass.addCapacitor(2, 4, c129);       // C129
            bandPass.addResistor(4, 3, 100.0e3);     // R334
            bandPass.addOpAmp(0, 3, 4, 5, Tr808Tom::opAmpBandwidth, Tr808Tom::opAmpRail); // IC21
            bandPass.addCapacitor(4, 6, 22.0e-9);    // C136
            bandPass.addResistor(6, 7, 39.0e3);      // R372
            bandPass.addResistor(7, 0, 1.0e3);       // R382
            bandOut = bandPass.addProbe(4);
            bandOta = bandPass.addProbe(7);
        }

        // Reverb: 1 IC21's output (input), 2 R342 over R346, 3 Q70's base, 4 C138, 5 its collector, 6 its emitter,
        // 7 the supply (input), 8 the negative supply (input), 9 C152 to R376.
        void buildReverb()
        {
            reverbNoise = reverb.addInput(1);
            reverbSupply = reverb.addInput(7);
            reverbNegative = reverb.addInput(8);
            r348Reverb = reverb.addSource(4, 0, 1.0e6);                                   // R348, from C139
            c138Reverb = reverb.addCapacitor(4, 0, c138);                                 // C138
            r349Reverb = reverb.addResistor(4, 3, 1.0e6);                                 // R349
            reverb.addResistor(1, 2, 15.0e3);                                             // R342
            reverb.addResistor(2, 0, 680.0);                                              // R346
            reverb.addCapacitor(2, 3, 33.0e-9);                                           // C137
            reverb.addTransistor(5, 3, 6, transistorSaturation, betaF, betaR, thermal);   // Q70
            reverb.addResistor(6, 0, 330.0);                                              // R369
            reverb.addResistor(6, 8, 10.0e3);                                             // the change's 10 kOhm
            reverb.addResistor(7, 5, 10.0e3);                                             // R363
            reverb.addCapacitor(5, 0, 18.0e-9);                                           // C146
            reverb.addCapacitor(5, 9, 4.7e-9);                                            // C152
            reverb.addResistor(9, 0, 47.0e3);                                             // R376, to IC19's virtual ground
            reverbC138 = reverb.addProbe(4);
            reverbBase = reverb.addProbe(3);
            reverbCollector = reverb.addProbe(5);
            reverbOut = reverb.addProbe(9);
        }

        // Bursts: 1 the negative supply (input), 2 pins 6 and 11, 3 pin 9, 4 pin 14 (C140), 5 pin 1, 6 pin 4, 7 pin
        // 13, 8 C144, 9 pin 5, 10 pin 2, 11 C141 to D70, 12 Q73's base, 13 its collector.
        void buildBursts()
        {
            burstsNegative = bursts.addInput(1);
            bursts.addResistor(0, 2, 4.7e3);                                             // R353
            bursts.addResistor(2, 3, 4.7e3);                                             // R352
            bursts.addResistor(3, 1, 4.7e3);                                             // R351
            comparatorPort[0] = bursts.addResistor(4, 1, switchOff);                     // IC23 A
            bursts.addResistor(4, 0, 1.0e6);                                             // R350
            c140Bursts = bursts.addCapacitor(4, 0, c140, 0.0);                           // C140
            comparatorPort[1] = bursts.addResistor(5, 1, switchOff);                     // IC23 B
            bursts.addResistor(5, 6, 2.7e3);                                             // R355
            bursts.addResistor(6, 0, 5.6e3);                                             // R354
            comparatorPort[2] = bursts.addResistor(7, 1, switchOff);                     // IC23 C
            bursts.addResistor(0, 7, 82.0e3);                                            // R365
            bursts.addDiode(7, 8, diodeSaturation, diodeThermal);                        // D71
            c144Bursts = bursts.addCapacitor(8, 0, c144);                                // C144
            bursts.addResistor(8, 9, 1.0e6);                                             // R356
            comparatorPort[3] = bursts.addResistor(10, 1, switchOff);                    // IC23 D
            bursts.addCapacitor(9, 10, 220.0e-12);                                       // C142
            bursts.addResistor(10, 0, 10.0e3);                                           // R359
            bursts.addResistor(10, 12, 68.0e3);                                          // R358
            bursts.addCapacitor(5, 11, 1.0e-9, 0.0);                                     // C141
            bursts.addDiode(1, 11, diodeSaturation, diodeThermal);                       // D70
            bursts.addResistor(11, 12, 68.0e3);                                          // R357
            bursts.addTransistor(13, 12, 1, transistorSaturation, betaF, betaR, thermal); // Q73
            bursts.addResistor(8, 13, 330.0);                                            // R367
            r368Bursts = bursts.addSource(13, 0, 470.0e3);                               // R368, to Q72's base in the pair
            burstsPin6 = bursts.addProbe(2);
            burstsPin9 = bursts.addProbe(3);
            burstsPin14 = bursts.addProbe(4);
            burstsPin1 = bursts.addProbe(5);
            burstsPin4 = bursts.addProbe(6);
            burstsPin13 = bursts.addProbe(7);
            burstsC144 = bursts.addProbe(8);
            burstsPin5 = bursts.addProbe(9);
            burstsPin2 = bursts.addProbe(10);
            burstsQ73Collector = bursts.addProbe(13);
        }

        // Pair: 1 the supply (input), 2 the negative supply (input), 3 the emitters, 4 Q72's base, 5 Q71's base, 6
        // TM3's wiper, 7 Q72's collector, 8 IC22's pin 1, 9 the mirror's junction.
        void buildPair()
        {
            pairSupply = pair.addInput(1);
            pairNegative = pair.addInput(2);
            r368Pair = pair.addSource(4, 0, 470.0e3);                                     // R368, from Q73's collector
            pair.addResistor(4, 0, 15.0e3);                                               // R371
            pair.addResistor(1, 3, 15.0e3);                                               // R366
            pair.addPnpTransistor(7, 4, 3, transistorSaturation, betaF, betaR, thermal);  // Q72
            pair.addCapacitor(7, 4, 4.0e-12, 0.0);                                        // its collector junction (the reference's CJC), backward Euler
            pair.addPnpTransistor(2, 5, 3, transistorSaturation, betaF, betaR, thermal);  // Q71
            pair.addResistor(5, 0, 2.2e3);                                                // R364
            pair.addResistor(6, 5, 47.0e3);                                               // R370
            tm3Top = pair.addResistor(0, 6, 5.0e3);                                       // TM3, C.P. OFFSET
            tm3Bottom = pair.addResistor(6, 2, 5.0e3);
            r362Pair = pair.addSource(5, 0, 330.0e3);                                     // R362, from C143
            pair.addResistor(7, 8, 10.0e3);                                               // R374
            pair.addResistor(8, 9, mirrorOhms);                                           // IC22's control input
            pair.addDiode(9, 2, mirrorSaturation, thermal);
            pairQ72Base = pair.addProbe(4);
            pairQ71Base = pair.addProbe(5);
            pairQ72Collector = pair.addProbe(7);
            pairPin1 = pair.addProbe(8);
        }

        // The maracas' envelope: 1 the supply (input), 2 Q67's base, 3 its collector, 4 C134 (Q66's base).
        void buildMaracasEnvelope()
        {
            maSupplyE = maEnvelope.addInput(1);
            r344Ma = maEnvelope.addSource(2, 0, 220.0e3);                                       // R344, from C135
            maEnvelope.addTransistor(3, 2, 0, transistorSaturation, betaF, betaR, thermal);     // Q67
            maEnvelope.addResistor(1, 3, 100.0e3);                                              // R343
            maEnvelope.addResistor(3, 4, 470.0e3);                                              // R341
            maEnvelope.addDiode(3, 4, diodeSaturation, diodeThermal);                           // D66
            c134Ma = maEnvelope.addCapacitor(4, 0, c134);                                       // C134
            q66BaseMa = maEnvelope.addSource(4, 0, injectOhms);                                 // Q66's base current
            maQ67Base = maEnvelope.addProbe(2);
            maC134 = maEnvelope.addProbe(4);
            maEnvelope.addProbe(3);
        }

        // The maracas' noise: 1 the supply (input), 2 the negative supply (input), 3 the white noise (input), 4 C134
        // (input), 5 Q66's base, 6 its emitter, 7 D65's cathode, 8 Q65's collector, 9 its base, 10 C130 to R335, 11
        // C131 to R338, 12 the high-pass's middle, 13 Q68's base, 14 its emitter, 15 C151 to R377.
        void buildMaracasNoise()
        {
            maSupplyN = maNoise.addInput(1);
            maNegativeN = maNoise.addInput(2);
            maNoiseIn = maNoise.addInput(3);
            maC134In = maNoise.addInput(4);
            maNoise.addResistor(4, 5, senseOhms);                                               // senses Q66's base current
            maNoise.addPnpTransistor(0, 5, 6, transistorSaturation, betaF, betaR, thermal);     // Q66
            maNoise.addResistor(6, 7, 10.0e3);                                                  // R337
            maNoise.addDiode(8, 7, diodeSaturation, diodeThermal);                              // D65
            maNoise.addPnpTransistor(8, 9, 1, transistorSaturation, betaF, betaR, thermal);     // Q65
            maNoise.addCapacitor(8, 9, 4.0e-12, 0.0);                                           // Q65's collector junction (the reference's CJC), backward Euler
            maNoise.addResistor(9, 0, 1.0e6);                                                   // R336
            maNoise.addCapacitor(9, 10, 47.0e-9);                                               // C130
            maNoise.addResistor(10, 3, 6.8e3);                                                  // R335
            maNoise.addCapacitor(7, 11, 10.0e-9);                                               // C131
            maNoise.addResistor(11, 0, 15.0e3);                                                 // R338
            c132Ma = maNoise.addCapacitor(11, 12, 1.0e-9);                                      // C132
            maNoise.addResistor(12, 14, 3.3e3);                                                 // R339
            c133Ma = maNoise.addCapacitor(12, 13, 1.0e-9);                                      // C133
            maNoise.addResistor(13, 0, 68.0e3);                                                 // R340
            maNoise.addTransistor(1, 13, 14, transistorSaturation, betaF, betaR, thermal);      // Q68
            maNoise.addResistor(14, 2, 22.0e3);                                                 // R347
            maNoise.addCapacitor(14, 15, 10.0e-9);                                              // C151
            maNoise.addResistor(15, 0, 100.0e3);                                                // R377, to IC19's virtual ground
            maBaseIn = maNoise.addProbe(4);
            maBase = maNoise.addProbe(5);
            maNoiseNode = maNoise.addProbe(7);
            maFollower = maNoise.addProbe(14);
        }

        /** One sample of the maracas (or their rest, solved): the envelope with Q66's base current of the sample
            before, then the noise part on C134 as it now is. */
        void stepMaracas(double whiteNoise, bool solveRest) noexcept
        {
            maEnvelope.setSource(r344Ma, lastC135);
            maEnvelope.setSource(q66BaseMa, lastC134 + lastQ66Base * injectOhms);
            if (solveRest)
                maEnvelope.settle();
            else
                maEnvelope.process();
            lastVb67 = maEnvelope.probe(maQ67Base);
            lastC134 = maEnvelope.probe(maC134);
            maNoise.setInput(maNoiseIn, whiteNoise);
            maNoise.setInput(maC134In, lastC134);
            if (solveRest)
                maNoise.settle();
            else
                maNoise.process();
            lastQ66Base = (maNoise.probe(maBase) - maNoise.probe(maBaseIn)) / senseOhms; // out of the PNP's base into C134
        }

        // Mixer: 1 Q70's collector (input), 2 C152 to R376, 3 IC22's pin 8 (input), 4 C150 to R378, 5 Q68's emitter
        // (input), 6 C151 to R377, 7 IC19's minus, 8 its output, 9 the LEVEL pot to C145.
        void buildMixer()
        {
            mixerReverb = mixer.addInput(1);
            mixerBursts = mixer.addInput(3);
            mixerMaracas = mixer.addInput(5);
            mixer.addCapacitor(1, 2, 4.7e-9);                         // C152
            mixer.addResistor(2, 7, 47.0e3);                          // R376
            mixer.addCapacitor(3, 4, 1.0e-9);                         // C150
            mixer.addResistor(4, 7, 100.0e3);                         // R378
            mixer.addCapacitor(5, 6, 10.0e-9);                        // C151
            mixer.addResistor(6, 7, 100.0e3);                         // R377
            mixer.addResistor(7, 8, 470.0e3);                         // R375
            mixer.addCapacitor(7, 8, 220.0e-12);                      // C149
            vr17 = mixer.addResistor(7, 9, Tr808Tom::levelOhms(0.8)); // VR17, LEVEL
            mixer.addCapacitor(9, 8, 33.0e-6);                        // C145
            mixer.addNullor(0, 7, 8);                                 // IC19
            mixerOut = mixer.addProbe(8);
        }

        wdf::Circuit<16, 30, 3, 0, 11, 7> front;
        wdf::Circuit<4, 10, 1, 0, 2, 3> tail; // room for an input it does not use: MSVC finds code unreachable with none
        wdf::Circuit<7, 12, 1, 1, 1, 2> bandPass;
        wdf::Circuit<9, 14, 3, 0, 2, 4> reverb;
        wdf::Circuit<13, 25, 1, 0, 4, 10> bursts;
        wdf::Circuit<9, 16, 2, 0, 5, 4> pair;
        wdf::Circuit<4, 8, 1, 0, 3, 3> maEnvelope;
        wdf::Circuit<15, 22, 4, 0, 7, 4> maNoise;
        wdf::Circuit<9, 10, 3, 1, 0, 1> mixer;

        struct RestState
        {
            decltype(front)::Snapshot front {};
            decltype(tail)::Snapshot tail {};
            decltype(bandPass)::Snapshot bandPass {};
            decltype(reverb)::Snapshot reverb {};
            decltype(bursts)::Snapshot bursts {};
            decltype(pair)::Snapshot pair {};
            decltype(maEnvelope)::Snapshot maEnvelope {};
            decltype(maNoise)::Snapshot maNoise {};
            decltype(mixer)::Snapshot mixer {};
            double c139Volts = 0.0, c143Volts = 0.0, c135Volts = 0.0, c138Volts = 0.0, nhVolts = 0.0, vb72Volts = 0.0, vb67Volts = 0.0;
            double triggerVolts = 0.0, pin8Volts = 0.0, iabc = 0.0, reverbCollectorVolts = 0.0;
            double maracasEnvelopeVolts = 0.0, maracasFollowerVolts = 0.0, q66BaseAmps = 0.0;
        };

        static const RestState& restState(bool maracasSide) noexcept
        {
            static const auto solve = [](bool side)
            {
                auto clap = std::make_unique<Tr808Clap>();
                clap->setMaracas(side);
                clap->settle();
                RestState s;
                s.front = clap->front.snapshot();
                s.tail = clap->tail.snapshot();
                s.bandPass = clap->bandPass.snapshot();
                s.reverb = clap->reverb.snapshot();
                s.bursts = clap->bursts.snapshot();
                s.pair = clap->pair.snapshot();
                s.maEnvelope = clap->maEnvelope.snapshot();
                s.maNoise = clap->maNoise.snapshot();
                s.mixer = clap->mixer.snapshot();
                s.c139Volts = clap->lastC139;
                s.c143Volts = clap->lastC143;
                s.c135Volts = clap->lastC135;
                s.c138Volts = clap->lastC138;
                s.nhVolts = clap->lastNh;
                s.vb72Volts = clap->lastVb72;
                s.vb67Volts = clap->lastVb67;
                s.triggerVolts = clap->front.probe(clap->frontTrigger);
                s.pin8Volts = clap->front.probe(clap->frontPin8);
                s.iabc = clap->lastIabc;
                s.reverbCollectorVolts = clap->reverb.probe(clap->reverbCollector);
                s.maracasEnvelopeVolts = clap->lastC134;
                s.maracasFollowerVolts = clap->maNoise.probe(clap->maFollower);
                s.q66BaseAmps = clap->lastQ66Base;
                return s;
            };
            static const RestState clapSide = solve(false);
            static const RestState maracasSide2 = solve(true);
            return maracasSide ? maracasSide2 : clapSide;
        }

        double sampleRate = 96000.0;
        double triggerVolts = 4.0, triggerWidth = 0.001;
        int triggerSample = -1; // samples since the hit, -1 between hits
        bool isMaracas = false, frontResting = true, reverbResting = true, burstsResting = true, maracasResting = true;
        int reverbQuiet = 0, burstsQuiet = 0, maracasQuiet = 0;
        double heldReverb = 0.0, heldMaracas = 0.0; // the quiet parts' last outputs, which the mixer keeps
        Comparators comparators { false, false, true, true };
        std::array<int, 4> comparatorPort {};
        double lastC139 = 0.0, lastC143 = 0.0, lastC135 = 0.0, lastC138 = 0.0, lastNh = 0.0, lastVb72 = 0.0, lastVb67 = 0.0;
        double lastIabc = 0.0, lastOta = 0.0;

        int frontDrive = 0, frontSupply = 0, frontNegative = 0, frontTrigger = 0, frontClapSide = 0, frontQ69Emitter = 0;
        int frontC139 = 0, frontC143 = 0, frontPin8 = 0, frontC135 = 0;
        int c139Front = 0, r348Front = 0, r362Front = 0, c135Front = 0, r344Front = 0, r106 = 0, sw12Clap = 0, sw12Maracas = 0;
        int r349Reverb = 0;
        int c139Tail = 0, r348Tail = 0, r362Tail = 0, c135Tail = 0, r344Tail = 0, tailC139 = 0, tailC143 = 0, tailC135 = 0;
        int bandOut = 0, bandOta = 0;
        int reverbNoise = 0, reverbSupply = 0, reverbNegative = 0, r348Reverb = 0, c138Reverb = 0, reverbC138 = 0, reverbBase = 0, reverbCollector = 0, reverbOut = 0;
        int burstsNegative = 0, c140Bursts = 0, c144Bursts = 0, r368Bursts = 0;
        int burstsPin6 = 0, burstsPin9 = 0, burstsPin14 = 0, burstsPin1 = 0, burstsPin4 = 0, burstsPin13 = 0, burstsC144 = 0, burstsPin5 = 0, burstsPin2 = 0, burstsQ73Collector = 0;
        int pairSupply = 0, pairNegative = 0, r368Pair = 0, r362Pair = 0, tm3Top = 0, tm3Bottom = 0, pairQ72Base = 0, pairQ71Base = 0, pairQ72Collector = 0, pairPin1 = 0;
        int maSupplyE = 0, r344Ma = 0, c134Ma = 0, q66BaseMa = 0, maQ67Base = 0, maC134 = 0;
        int maSupplyN = 0, maNegativeN = 0, maNoiseIn = 0, maC134In = 0, c132Ma = 0, c133Ma = 0, maBaseIn = 0, maBase = 0, maNoiseNode = 0, maFollower = 0;
        double lastC134 = 0.0, lastQ66Base = 0.0; // the maracas' envelope and Q66's base current, the sample before
        int mixerReverb = 0, mixerBursts = 0, mixerMaracas = 0, mixerOut = 0, vr17 = 0;
    };
}
