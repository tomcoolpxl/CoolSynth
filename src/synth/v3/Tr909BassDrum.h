#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

#include "CircuitWdf.h"

/*
    The Roland TR-909 bass drum as its circuit (D237, D238), read from the service notes' voicing board (page 11),
    units from serial 381500 (C9 0.33 uF). The research: docs/research/kick-909-circuit.md; the SPICE reference:
    tools/kick909/bassdrum.cir.

    Wave digital filters with an R-type root each (CircuitWdf.h), cut where a part drives the next hard (an op-amp's
    output, a saturated switch, a large resistor into a low impedance), each driving the next one way as an ideal
    source or an injected current, read a sample late where they meet:

        Front        Q1 takes the 5 V, 2 ms trigger; Q2 makes the unaccented +15 V pulse; Q7, through C1, the reset
                     (to Q11) and the comparator's forcing (D12, with R49 and R50 here, as that loop is too stiff to
                     cut); Q5 passes the accent into ENV2 (R41)
        Pitch        ENV3: the pulse charges C9 through D7, TUNE (VR2) sets its fall; IC12a makes the control voltage
        Hold         C7 holds Q3, which keeps Q4 passing the accent into C8 (ENV1) through R35 and D9, clamped softly
                     by D8 at R36 and R22. Runs from a hit until the hold is over; then Q4 and D9 are off and C8 falls
                     through DECAY (VR5) and R58, and through R54 into Q12's base: an RC, followed exactly
        Click        Q8 follows the pulse; the Sallen-Key around Q9 rings, Q9's emitter held by R8 toward -15 V, and
                     C11 passes it through R44 toward the attack's VCA. Runs from a hit until it is back at rest
        Attack       the click (through R44, as a current) and the noise (R10 to C13) through ATTACK (VR3) into the VCA
                     Q6, its base on ENV2 (C12, R42)
        Body         the triangle through R52 into the diode pair D10, D11, then R53 and R43 into the VCA Q12

    The oscillator is not a circuit of its own: IC12b passes plus or minus the control voltage as the PNP Q10 (the
    comparator's) says, IC13a integrates it on C14, IC13b flips at R49 and R50's thresholds. Between flips the
    triangle is a straight line, so it is followed exactly, the flip found inside the sample and its corner
    band-limited (polyBLAMP, Esqueda, Valimaki and Bilbao, DAFx 2016); Q11 discharges C14 through R51 while Q7
    drives it. The mixer IC11a sums Q12's and Q6's emitter currents into LEVEL (VR4); an ideal op-amp.

    Transistors and diodes as generic models (2SC2603, 2SA1115 and 1SS133 have no published ones): Ebers-Moll with
    Is 10 fA, betaF 300 (NPN) or 250 (PNP), betaR 5; Shockley diodes as the 1N4148's model, Is 2.52 nA, n 1.752.
    The op-amps (M5218L) ideal; the comparator swings to +-13 V, as TI's RC4558 macromodel does in SPICE. One reading
    differs from the drawing (D238): R8 returns to -15 V, as operator reads it, not to Q4's collector as printed.
    So Q9 is a working follower, C11 sits forward-biased as its + marking says, and with nothing fitted the trigger
    stays quiet, the accent spans what two real 909s' recordings show and long decays land on theirs (as printed, the
    accent reaches the attack's VCA through C11 during the trigger and C11 feeds C8 after the hold, nearly doubling
    the decay). Still open: short decays' tails fall faster than the recordings', and the click is weaker (D238).
    Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr909BassDrum final
    {
    public:
        static constexpr double thermal = 0.02585;
        static constexpr double transistorSaturation = 10.0e-15, npnBetaF = 300.0, pnpBetaF = 250.0, betaR = 5.0;
        static constexpr double diodeSaturation = 2.52e-9, diodeThermal = 1.752 * thermal;
        static constexpr double supply = 15.0;
        static constexpr double nominalC14 = 0.033e-6;
        static constexpr double r49 = 100.0e3, r50 = 47.0e3, r51 = 2.2e3, r59 = 47.0e3;
        static constexpr double triggerVolts = 5.0;

        Tr909BassDrum()
        {
            buildFront();
            buildPitch();
            buildHold();
            buildClick();
            buildAttack();
            buildBody();
            // The transistors switch hard on the trigger's edges: more room than the default 12 iterations.
            front.setNewtonLimit(80);
            hold.setNewtonLimit(80);
            click.setNewtonLimit(80);
            attack.setNewtonLimit(80);
        }

        /** At a sample rate, at rest: a copy of the rest state solved once for the process (restState). */
        void prepare(double rate) noexcept
        {
            sampleRate = rate;
            front.setSampleRate(rate);
            pitch.setSampleRate(rate);
            hold.setSampleRate(rate);
            click.setSampleRate(rate);
            attack.setSampleRate(rate);
            body.setSampleRate(rate);
            rest();
        }

        /** Back to rest: the circuits as the rest state holds them, the oscillator at its reset phase. */
        void rest() noexcept
        {
            const RestState& r = restState();
            front.restore(r.front);
            pitch.restore(r.pitch);
            hold.restore(r.hold);
            click.restore(r.click);
            attack.restore(r.attack);
            body.restore(r.body);
            values = r.values;
            triangle = lastTriangle = blampNext = triangleOut = 0.0;
            comparatorHigh = true;
            triggerTime = -1.0;
            frontResting = attackResting = clickResting = pitchLinear = true;
            envelopeRunning = false;
            frontStill = attackStill = clickStill = envelopeQuiet = 0;
            applyInputs();
        }

        /** Solves the rest state once (call it off the audio thread: Part::prepare does). */
        static void warmUp() noexcept { restState(); }

        /** An audio-taper (A) pot's share of its track at a turn, 0..1: 15 % at mid-travel (TUNE, DECAY). */
        [[nodiscard]] static double audioTaper(double turn) noexcept
        {
            constexpr double base = 32.1;
            return (std::pow(base, std::clamp(turn, 0.0, 1.0)) - 1.0) / (base - 1.0);
        }

        /** A hit: the 6-bit accent DAC's volts (0..5) latched with it, then the 5 V, 2 ms trigger. */
        void trigger(double dacVolts) noexcept
        {
            accentVolts = std::clamp(3.13 * std::clamp(dacVolts, 0.0, 5.0), -13.0, 13.0); // IC11b, x(1 + 10k / 4.7k)
            triggerTime = 0.0;
            frontResting = attackResting = clickResting = false;
            if (pitchLinear)
            {
                pitch.setCapacitorVoltage(pitchC9, values.c9); // the circuit takes C9 back from the RC
                pitchLinear = false;
            }
            frontStill = attackStill = clickStill = envelopeQuiet = 0;
            if (! envelopeRunning)
            {
                // The hold takes over from the decay's RC: its rest, with C8 as it is.
                hold.restore(restState().hold);
                hold.setCapacitorVoltage(holdC8, values.c8);
                envelopeRunning = true;
            }
        }

        // ---- The knobs, as parts ------------------------------------------------------------------------------
        /** TUNE: VR2, 0..100 kOhm (the sweep's fall). */
        void setTuneOhms(double ohms) noexcept
        {
            vr2Ohms = std::max(1.0, ohms);
            pitch.setResistance(vr2, vr2Ohms);
        }
        /** The sweep's depth: R26, stock 330 kOhm (smaller, deeper). */
        void setSweepOhms(double ohms) noexcept
        {
            r26Ohms = std::max(1.0e3, ohms);
            pitch.setResistance(r26, r26Ohms);
        }
        /** DECAY: VR5, 0..1 MOhm. */
        void setDecayOhms(double ohms) noexcept
        {
            vr5Ohms = std::max(1.0, ohms);
            hold.setResistance(holdVr5, vr5Ohms);
        }
        /** ATTACK: VR3, 500 Ohm (none of the attack) to 0 (all of it). */
        void setAttackOhms(double ohms) noexcept { attack.setResistance(vr3, std::max(0.1, ohms)); }
        /** LEVEL: VR4, the mixer's feedback, 0..100 kOhm. */
        void setLevelOhms(double ohms) noexcept { levelOhms = std::max(0.0, ohms); }
        /** The drive mod: R52, stock 10 kOhm (smaller, the diodes clip harder toward a square). */
        void setShaperOhms(double ohms) noexcept { body.setResistance(r52, std::max(100.0, ohms)); }
        /** The noise branch: R10, stock 12 kOhm (smaller, more noise). */
        void setNoiseOhms(double ohms) noexcept { attack.setResistance(r10, std::max(100.0, ohms)); }
        /** The oscillator's capacitor scaled: the pitch by 1 / scale (the key). */
        void setTuning(double scale) noexcept
        {
            tuning = std::max(1.0e-3, scale);
            c14 = nominalC14 * tuning;
        }
        [[nodiscard]] double currentTuning() const noexcept { return tuning; }

        /** The settled pitch at a tuning: |V_cv| / (4 V_th R59 C14), the control voltage at rest. */
        [[nodiscard]] double restHz(double scale) const noexcept
        {
            const double threshold = comparatorSwing * r50 / (r49 + r50);
            return std::abs(restState().values.cv) / (4.0 * threshold * r59 * nominalC14 * scale);
        }

        /** The noise generator's seed (the 4006s run free, so each hit's noise differs). */
        void setNoiseSeed(std::uint32_t seed) noexcept { noiseState = seed == 0 ? 1u : seed; }

        /** The CPU's trigger pulse, its width in seconds. */
        void setTriggerSeconds(double seconds) noexcept { triggerSeconds = std::max(1.0e-4, seconds); }

        /** One sample of the whole drum; returns the mixer's output in volts. */
        double process() noexcept
        {
            // The trigger: 5 V for triggerSeconds.
            double trig = 0.0;
            if (triggerTime >= 0.0)
            {
                trig = triggerVolts;
                triggerTime += 1.0 / sampleRate;
                if (triggerTime > triggerSeconds)
                    triggerTime = -1.0;
            }
            const bool active = trig > 0.0;

            if (active || ! frontResting)
            {
                front.setInput(frontTrig, trig);
                front.setInput(frontAcc, accentVolts);
                front.setInput(frontCmp, comparatorHigh ? comparatorSwing : -comparatorSwing);
                front.setInput(frontC12, values.c12);
                front.process();
                const double n4 = front.probe(probeN4), n7c = front.probe(probeN7c);
                const double moved = std::abs(n4 - values.n4) + std::abs(n7c - values.n7c);
                values.n4 = n4;
                values.n7c = n7c;
                values.n5c = front.probe(probeN5c);
                values.x = front.probe(probeX);
                frontStill = moved < 1.0e-6 ? frontStill + 1 : 0;
                frontResting = ! active && frontStill > static_cast<int>(0.002 * sampleRate);
            }
            else
                values.x = (comparatorHigh ? comparatorSwing : -comparatorSwing) * r50 / (r49 + r50);

            // The pitch envelope: the circuit while the pulse can reach C9; once the front rests D7 is off and C9 is an
            // RC, R57 and TUNE to ground and R26 into IC12a's virtual ground, solved exactly a sample at a time.
            if (! pitchLinear)
            {
                pitch.setInput(pitchN4, values.n4);
                pitch.process();
                values.cv = pitch.probe(probeCv);
                values.c9 = pitch.probe(probeC9);
                pitchLinear = frontResting;
            }
            else
            {
                const auto& rest = restState().values;
                const double g = 1.0 / (22.0e3 + vr2Ohms) + 1.0 / r26Ohms;
                values.c9 = rest.c9 + (values.c9 - rest.c9) * std::exp(-g / (0.33e-6 * sampleRate));
                values.cv = rest.cv - 150.0e3 / r26Ohms * (values.c9 - rest.c9);
            }

            stepOscillator();

            body.setInput(bodyTri, triangleOut);
            body.setInput(bodyC8, values.c8);
            body.process();
            const double nd = body.probe(probeNd), nvca = body.probe(probeNvca);
            values.q12b = body.probe(probeQ12b);
            // Q12's emitter current into the mixer: its base's through R54, its collector's from R53 less R43's.
            const double emitter12 = (values.c8 - values.q12b) / r54 + (nd - nvca) / r53 - nvca / r43;

            stepEnvelope(active);
            stepClick(active);

            double emitter6 = 0.0;
            const double injected41 = frontResting ? 0.0 : (values.n5c - values.c12) / r41;
            const double injected44 = (values.n11c - values.a1) / r44;
            if (active || ! attackResting)
            {
                attack.setInput(attackNoise, noiseVolts());
                attack.setSource(attackFrom41, injected41 * injectionOhms);
                attack.setSource(attackFrom44, injected44 * injectionOhms);
                attack.process();
                const double a1 = attack.probe(probeA1), a2 = attack.probe(probeA2), n13 = attack.probe(probeN13);
                const double q6b = attack.probe(probeQ6b);
                values.a1 = a1;
                values.c12 = attack.probe(probeC12);
                // Q6's emitter current: through VR3 (what R44 and R45 bring to a1, less R56's) less R55's, and its base's.
                const double throughVr3 = injected44 + (n13 - a1) / r45 - a1 / r56;
                emitter6 = throughVr3 - a2 / r55 + (values.c12 - q6b) / r42;
                // Rests once Q6 passes under a nanoampere (under -90 dB at the output) and Q5 has stopped feeding ENV2.
                attackStill = std::abs(emitter6) < 1.0e-9 && injected41 == 0.0 ? attackStill + 1 : 0;
                attackResting = ! active && attackStill > static_cast<int>(0.002 * sampleRate);
            }

            // IC11a: the currents into its virtual ground through LEVEL; clipped at the rails.
            return std::clamp(-levelOhms * (emitter12 + emitter6), -13.0, 13.0);
        }

        // The stages' last values, for tests against the reference.
        struct Values
        {
            double n4 = 0.0, n7c = 0.0, n5c = 0.0, x = 0.0, cv = 0.0, c9 = 0.0, c8 = 0.0, c7 = 0.0, e1 = 0.0, nk = 0.0,
                   q8e = 0.0, q9e = 0.0, n11c = 0.0, a1 = 0.0, c12 = 0.0, q12b = 0.0;
        };
        [[nodiscard]] const Values& stage() const noexcept { return values; }
        [[nodiscard]] double triangleVolts() const noexcept { return triangleOut; }
        [[nodiscard]] bool comparatorIsHigh() const noexcept { return comparatorHigh; }
        [[nodiscard]] bool frontIsResting() const noexcept { return frontResting; }
        [[nodiscard]] bool envelopeIsRunning() const noexcept { return envelopeRunning; }
        /** The solver's last iterations in each circuit: front, pitch, hold, click, attack, body. */
        [[nodiscard]] std::array<int, 6> newtonSteps() const noexcept
        {
            return { front.lastNewtonSteps(), pitch.lastNewtonSteps(), hold.lastNewtonSteps(), click.lastNewtonSteps(),
                     attack.lastNewtonSteps(), body.lastNewtonSteps() };
        }
        [[nodiscard]] bool clickIsResting() const noexcept { return clickResting; }

        double comparatorSwing = 13.0;

    private:
        static constexpr double r41 = 22.0e3, r42 = 470.0e3, r43 = 47.0, r44 = 22.0e3, r45 = 4.7e3, r53 = 2.2e3, r54 = 470.0e3,
                                r55 = 47.0, r56 = 330.0, r58 = 47.0e3, c8Farads = 0.33e-6;
        static constexpr double injectionOhms = 1.0e9; // a current from another circuit, as a source behind this

        // ---- The oscillator --------------------------------------------------------------------------------------
        /**
            One sample of IC12b, IC13a and IC13b. The triangle moves at -V_sq / (R59 C14), V_sq = +-V_cv as the
            comparator says (high: Q10 off, +V_cv; low: Q10 on, -V_cv; V_cv is negative); while Q7 drives Q11, C14 also
            discharges through R51, as far as Q11's base current lets it. A flip inside the sample is found exactly
            and its corner band-limited; the triangle is handed on a sample late, for that.
        */
        void stepOscillator() noexcept
        {
            const double dt = 1.0 / sampleRate;
            const double cv = values.cv;
            // Q11: its base current from Q7 through R47, the diode drop taken as 0.65 V.
            const double baseCurrent = frontResting ? 0.0 : std::max(0.0, (values.n7c - 0.65) / 330.0e3);
            double out = triangle;
            double corner = 0.0, cornerAt = -1.0;
            if (baseCurrent > 0.0)
            {
                // The reset: D12 holds the comparator where X says, Q11 drains C14 through R51. Saturated, Q11 is
                // R51 across C14 and the triangle relaxes exactly to -V_sq R51 / R59; past its base current's
                // reach, a fixed current.
                comparatorHigh = values.x > out;
                const double square = comparatorHigh ? cv : -cv;
                const double settled = -square * r51 / r59;
                const double next = settled + (out - settled) * std::exp(-dt / (r51 * c14));
                const double limit = (out > 0.0 ? npnBetaF : betaR) * baseCurrent * r51;
                out = std::abs(out) <= limit ? next : out + slopeNow(cv, baseCurrent, out) * dt;
            }
            else
            {
                double t = 0.0;
                for (int pass = 0; pass < 2 && t < dt; ++pass)
                {
                    const double slope = slopeNow(cv, 0.0, out);
                    const double remaining = dt - t;
                    const double threshold = values.x;
                    // The comparator flips high-to-low when the triangle rises past X, low-to-high when it falls below.
                    double hit = -1.0;
                    if (comparatorHigh && slope > 0.0 && out < threshold && out + slope * remaining >= threshold)
                        hit = (threshold - out) / slope;
                    else if (! comparatorHigh && slope < 0.0 && out > threshold && out + slope * remaining <= threshold)
                        hit = (threshold - out) / slope;
                    if (hit < 0.0)
                    {
                        out += slope * remaining;
                        break;
                    }
                    out += slope * hit;
                    t += hit;
                    comparatorHigh = ! comparatorHigh;
                    values.x = -values.x;
                    cornerAt = t / dt;
                    corner = (slopeNow(cv, 0.0, out) - slope) * dt; // the slope's change, volts a sample
                }
            }
            triangle = out;

            // polyBLAMP: a corner at c (0..1 into this sample) adds corner (1 - c)^3 / 6 to the last sample and
            // corner c^3 / 6 to this one; the triangle goes out a sample late, so both can be corrected.
            double delayed = lastTriangle + blampNext;
            blampNext = 0.0;
            if (cornerAt >= 0.0)
            {
                const double c = cornerAt;
                delayed += corner * (1.0 - c) * (1.0 - c) * (1.0 - c) / 6.0;
                blampNext = corner * c * c * c / 6.0;
            }
            triangleOut = delayed;
            lastTriangle = out;
        }

        /** The triangle's slope, volts a second, at a triangle voltage. */
        [[nodiscard]] double slopeNow(double cv, double q11Base, double tri) const noexcept
        {
            const double square = comparatorHigh ? cv : -cv;
            double through11 = 0.0;
            if (q11Base > 0.0)
                through11 = std::clamp(tri / r51, -betaR * q11Base, npnBetaF * q11Base);
            return -(square / r59 + through11) / c14;
        }

        // ---- The envelope and the decay ------------------------------------------------------------------------
        /**
            The hold from a hit until it has let go (C7 drained below Q3's knee, so Q4 is off); then C8 is an RC, VR5
            and R58 to ground and R54 to Q12's base, followed exactly a sample at a time (D9 is off: nothing drives
            its anode any more). C7 under 0.6 V leaves Q3's base under 0.3 V: a nanoampere, and Q4 passes a few hundred
            nanoamperes, against the microamperes C8 loses.
        */
        void stepEnvelope(bool active) noexcept
        {
            if (envelopeRunning)
            {
                hold.setInput(holdN4, values.n4);
                hold.setInput(holdAcc, accentVolts);
                hold.setInput(holdQ12b, values.q12b);
                hold.process();
                values.c8 = hold.probe(probeC8);
                values.c7 = hold.probe(probeC7);
                values.e1 = hold.probe(probeE1);
                values.nk = hold.probe(probeNk);
                const bool quiet = ! active && values.c7 < 0.6 && values.n4 < 1.0;
                envelopeQuiet = quiet ? envelopeQuiet + 1 : 0;
                envelopeRunning = envelopeQuiet <= static_cast<int>(0.002 * sampleRate);
                return;
            }
            const double g = 1.0 / (vr5Ohms + r58) + 1.0 / r54;
            const double target = values.q12b / r54 / g;
            values.c8 = target + (values.c8 - target) * std::exp(-g / (c8Farads * sampleRate));
        }

        /** The click from a hit until Q8, Q9 and C11 are back at rest (C11 recovers in tens of milliseconds); at rest
            its last values stand. */
        void stepClick(bool active) noexcept
        {
            if (clickResting && ! active)
                return;
            click.setInput(clickN4, values.n4);
            click.setInput(clickA1, values.a1);
            click.process();
            const auto& rest = restState().values;
            values.q9e = click.probe(probeQ9e);
            values.n11c = click.probe(probeN11c);
            values.q8e = click.probe(probeQ8e);
            // C11 within a millivolt of rest (it carries a1's noise too, through R44, so never closer): what stops then,
            // under 45 nA through R44, Q6 passes far under the noise floor.
            const bool still = std::abs(values.q8e - rest.q8e) < 1.0e-4 && std::abs(values.q9e - rest.q9e) < 1.0e-4
                               && std::abs((values.n11c - values.q9e) - (rest.n11c - rest.q9e)) < 1.0e-3;
            clickStill = still ? clickStill + 1 : 0;
            clickResting = ! active && clickStill > static_cast<int>(0.002 * sampleRate);
        }

        /** The 4006 noise: 0/15 V bits at about 380 kHz, whose density below 20 kHz is a white 3.0e-4 V^2/Hz, given
            as a seeded white source of that density at this rate (mean 7.5 V, which C15 blocks). */
        double noiseVolts() noexcept
        {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            const double u = static_cast<double>(noiseState) / 4294967296.0 * 2.0 - 1.0; // -1..1, variance 1/3
            const double sigma = std::sqrt(2.0 * 7.5 * 7.5 / 380.0e3 * sampleRate / 2.0);
            return 7.5 + sigma * std::sqrt(3.0) * u;
        }

        void applyInputs() noexcept
        {
            front.setInput(frontVcc, supply);
            front.setInput(frontTrig, 0.0);
            front.setInput(frontAcc, accentVolts);
            front.setInput(frontCmp, comparatorSwing);
            front.setInput(frontC12, values.c12);
            pitch.setInput(pitchVcc, supply);
            pitch.setInput(pitchN4, values.n4);
            hold.setInput(holdVcc, supply);
            hold.setInput(holdN4, values.n4);
            hold.setInput(holdAcc, accentVolts);
            hold.setInput(holdQ12b, values.q12b);
            click.setInput(clickVcc, supply);
            click.setInput(clickVee, -supply);
            click.setInput(clickN4, values.n4);
            click.setInput(clickA1, values.a1);
            attack.setInput(attackNoise, 7.5);
            attack.setSource(attackFrom41, 0.0);
            attack.setSource(attackFrom44, 0.0);
            body.setInput(bodyTri, 0.0);
            body.setInput(bodyC8, values.c8);
        }

        // ---- The circuits -------------------------------------------------------------------------------------
        void addNpn(auto& c, int collector, int base, int emitter) { c.addTransistor(collector, base, emitter, transistorSaturation, npnBetaF, betaR, thermal); }
        void addPnp(auto& c, int collector, int base, int emitter) { c.addPnpTransistor(collector, base, emitter, transistorSaturation, pnpBetaF, betaR, thermal); }
        void addDiode(auto& c, int anode, int cathode) { c.addDiode(anode, cathode, diodeSaturation, diodeThermal); }

        // Front: 1 trig, 2 Q1 base, 3 n1 (Q1's collector), 4 +15, 5 Q2's base, 6 R3/D4, 7 C6, 8 n4 (the pulse), 9 Q7's base,
        // 10 C1/R12, 11 n7c (Q7's collector), 12 Q11's base, 13 R48/D12, 14 X (the comparator's +), 15 the accent, 16 Q5's
        // base, 17 R34/D5, 18 n5c (Q5's collector), 19 C12 (input), 20 Q8's base (R5/R6, which lift n4 at rest), 21 the
        // comparator's output (input).
        void buildFront()
        {
            frontTrig = front.addInput(1);
            frontVcc = front.addInput(4);
            frontAcc = front.addInput(15);
            frontC12 = front.addInput(19);
            frontCmp = front.addInput(21);
            front.addResistor(1, 2, 22.0e3);  // R1
            front.addResistor(2, 0, 10.0e3);  // R2
            addNpn(front, 3, 2, 0);           // Q1
            front.addResistor(4, 5, 47.0e3);  // R4
            front.addResistor(5, 6, 100.0e3); // R3
            addDiode(front, 6, 3);            // D4
            front.addResistor(6, 7, 4.7e3);   // R11
            front.addCapacitor(7, 0, 2.2e-9); // C6
            addPnp(front, 8, 5, 4);           // Q2
            front.addResistor(8, 0, 2.2e3);   // R19
            front.addResistor(8, 20, 100.0e3); // R5
            front.addResistor(4, 20, 22.0e3); // R6
            front.addResistor(4, 9, 47.0e3);  // R13
            front.addResistor(9, 10, 100.0e3); // R12
            front.addCapacitor(10, 3, 0.068e-6); // C1
            addPnp(front, 11, 9, 4);          // Q7
            front.addResistor(11, 12, 330.0e3); // R47
            // Q7's collector, Q11's base and D12 meet only junctions: a leak across Q11's base (as SPICE's gmin) gives
            // the solver a way to ground, without which those nodes have no defined voltage to it.
            front.addResistor(12, 0, 10.0e6);
            // Q11's base, its emitter on IC13a's virtual ground: both junctions near it when saturated.
            front.addDiode(12, 0, transistorSaturation * (1.0 / npnBetaF + 1.0 / betaR), thermal);
            front.addResistor(11, 13, 22.0e3); // R48
            addDiode(front, 13, 14);          // D12
            front.addResistor(21, 14, r49);   // R49, from the comparator's output
            front.addResistor(14, 0, r50);    // R50
            front.addResistor(15, 16, 1.0e6); // R39
            front.addResistor(16, 17, 100.0e3); // R34
            addDiode(front, 17, 3);           // D5
            addPnp(front, 18, 16, 15);        // Q5
            front.addResistor(18, 19, r41);   // R41
            probeN4 = front.addProbe(8);
            probeN7c = front.addProbe(11);
            probeN5c = front.addProbe(18);
            probeX = front.addProbe(14);
        }

        // Pitch: 1 n4 (input), 2 D7/R21, 3 C9, 4 R57/VR2, 5 IC12a's minus, 6 +15 (input), 7 the control voltage, 8 IC12a's plus.
        void buildPitch()
        {
            pitchN4 = pitch.addInput(1);
            pitchVcc = pitch.addInput(6);
            addDiode(pitch, 1, 2);             // D7
            pitch.addResistor(2, 3, 2.2e3);    // R21
            pitchC9 = pitch.addCapacitor(3, 0, 0.33e-6); // C9
            pitch.addResistor(3, 4, 22.0e3);   // R57
            vr2 = pitch.addResistor(4, 0, 50.0e3); // VR2, TUNE
            r26 = pitch.addResistor(3, 5, 330.0e3); // R26
            pitch.addResistor(6, 5, 1.5e6);    // R27
            pitch.addResistor(5, 7, 150.0e3);  // R28
            pitch.addResistor(8, 0, 100.0e3);  // R25
            pitch.addNullor(8, 5, 7);          // IC12a
            probeCv = pitch.addProbe(7);
            probeC9 = pitch.addProbe(3);
        }

        // Hold: 1 n4 (input), 2 D6/R20, 3 C7, 4 Q3's base, 5 Q3's collector, 6 Q4's base, 7 the accent (input), 8 e1 (Q4's
        // collector), 9 nk, 10 the clamp's divider, 11 +15 (input), 12 C8, 13 VR5/R58, 14 Q12's base (input).
        void buildHold()
        {
            holdN4 = hold.addInput(1);
            holdAcc = hold.addInput(7);
            holdVcc = hold.addInput(11);
            holdQ12b = hold.addInput(14);
            addDiode(hold, 1, 2);              // D6
            hold.addResistor(2, 3, 6.8e3);     // R20
            hold.addCapacitor(3, 0, 0.033e-6); // C7
            hold.addResistor(3, 4, 470.0e3);   // R18
            hold.addResistor(4, 0, 470.0e3);   // R17
            addNpn(hold, 5, 4, 0);             // Q3
            hold.addResistor(5, 6, 47.0e3);    // R32
            hold.addResistor(7, 6, 1.0e6);     // R33
            addPnp(hold, 8, 6, 7);             // Q4
            hold.addResistor(8, 9, 1.8e3);     // R35
            addDiode(hold, 9, 10);             // D8
            hold.addResistor(11, 10, 47.0e3);  // R36
            hold.addResistor(10, 0, 10.0e3);   // R22
            addDiode(hold, 9, 12);             // D9
            holdC8 = hold.addCapacitor(12, 0, c8Farads); // C8
            holdVr5 = hold.addResistor(12, 13, 150.0e3); // VR5, DECAY
            hold.addResistor(13, 0, r58);      // R58
            hold.addResistor(12, 14, r54);     // R54
            // e1 and nk meet only junctions while Q4 is off: a leak (as SPICE's gmin) gives the solver a way to ground.
            hold.addResistor(8, 0, 10.0e6);
            probeC8 = hold.addProbe(12);
            probeC7 = hold.addProbe(3);
            probeE1 = hold.addProbe(8);
            probeNk = hold.addProbe(9);
        }

        // Click: 1 n4 (input), 2 +15 (input), 3 Q8's base, 4 Q8's emitter, 5 C2/R7, 6 the Sallen-Key's middle, 7 Q9's base,
        // 8 Q9's emitter, 9 -15 (input), 10 C11/R44, 11 a1 (input).
        void buildClick()
        {
            clickN4 = click.addInput(1);
            clickVcc = click.addInput(2);
            clickVee = click.addInput(9);
            clickA1 = click.addInput(11);
            click.addResistor(1, 3, 100.0e3);  // R5
            click.addResistor(2, 3, 22.0e3);   // R6
            addNpn(click, 2, 3, 4);            // Q8
            click.addCapacitor(4, 0, 6.8e-9);  // C5
            click.addResistor(4, 0, 22.0e3);   // R14
            click.addCapacitor(4, 5, 3.3e-9);  // C2
            click.addResistor(5, 6, 100.0e3);  // R7
            click.addResistor(6, 0, 100.0e3);  // R15
            click.addResistor(6, 7, 39.0e3);   // R16
            click.addCapacitor(7, 0, 100.0e-12); // C4
            click.addCapacitor(6, 8, 4.7e-9);  // C3
            addNpn(click, 2, 7, 8);            // Q9
            click.addResistor(8, 9, 10.0e3);   // R8, to -15 V (operator's reading; printed to Q4's collector, D238)
            click.addCapacitor(8, 10, 0.47e-6); // C11
            click.addResistor(10, 11, r44);    // R44, to a1
            probeQ9e = click.addProbe(8);
            probeN11c = click.addProbe(10);
            probeQ8e = click.addProbe(4);
        }

        // Attack: 1 a1, 2 the noise bus (input), 3 R10/R9, 4 C15/R46, 5 C13, 6 a2 (Q6's collector), 7 C12, 8 Q6's base.
        void buildAttack()
        {
            attackNoise = attack.addInput(2);
            r10 = attack.addResistor(2, 3, 12.0e3); // R10
            attack.addResistor(3, 0, 4.7e3);    // R9
            attack.addCapacitor(3, 4, 0.47e-6); // C15
            attack.addResistor(4, 5, 4.7e3);    // R46
            attack.addCapacitor(5, 0, 0.1e-6);  // C13
            attack.addResistor(5, 1, r45);      // R45
            attack.addResistor(1, 0, r56);      // R56
            vr3 = attack.addResistor(1, 6, 250.0); // VR3, ATTACK
            attack.addResistor(6, 0, r55);      // R55
            attack.addCapacitor(7, 0, 0.033e-6); // C12
            attack.addResistor(7, 8, r42);      // R42
            attackFrom41 = attack.addSource(7, 0, injectionOhms); // what R41 brings from Q5
            attackFrom44 = attack.addSource(1, 0, injectionOhms); // what R44 brings from the click
            addNpn(attack, 6, 8, 0);            // Q6, its emitter on IC11a's virtual ground
            probeA1 = attack.addProbe(1);
            probeN13 = attack.addProbe(5);
            probeA2 = attack.addProbe(6);
            probeC12 = attack.addProbe(7);
            probeQ6b = attack.addProbe(8);
        }

        // Body: 1 the triangle (input), 2 the diodes, 3 Q12's collector, 4 Q12's base, 5 C8 (input).
        void buildBody()
        {
            bodyTri = body.addInput(1);
            bodyC8 = body.addInput(5);
            r52 = body.addResistor(1, 2, 10.0e3); // R52
            body.addDiodePair(2, 0, diodeSaturation, diodeThermal); // D10 and D11, back to back
            body.addResistor(2, 3, r53);       // R53
            body.addResistor(3, 0, r43);       // R43
            body.addResistor(5, 4, r54);       // R54
            addNpn(body, 3, 4, 0);             // Q12, its emitter on IC11a's virtual ground
            probeNd = body.addProbe(2);
            probeNvca = body.addProbe(3);
            probeQ12b = body.addProbe(4);
        }

        wdf::Circuit<21, 32, 5, 0, 12, 4> front;
        wdf::Circuit<8, 9, 2, 1, 1, 2> pitch;
        wdf::Circuit<14, 21, 4, 0, 7, 4> hold;
        wdf::Circuit<11, 17, 4, 0, 4, 3> click;
        wdf::Circuit<8, 16, 1, 0, 2, 5> attack;
        wdf::Circuit<5, 7, 2, 0, 3, 3> body;

        int frontTrig = 0, frontVcc = 0, frontAcc = 0, frontC12 = 0, frontCmp = 0;
        int probeN4 = 0, probeN7c = 0, probeN5c = 0, probeX = 0;
        int pitchN4 = 0, pitchVcc = 0, pitchC9 = 0, vr2 = 0, r26 = 0, probeCv = 0, probeC9 = 0;
        int holdN4 = 0, holdAcc = 0, holdVcc = 0, holdQ12b = 0, holdC8 = 0, holdVr5 = 0;
        int clickN4 = 0, clickVcc = 0, clickVee = 0, clickA1 = 0;
        int probeC8 = 0, probeC7 = 0, probeE1 = 0, probeNk = 0, probeQ9e = 0, probeN11c = 0, probeQ8e = 0;
        int attackNoise = 0, attackFrom41 = 0, attackFrom44 = 0, r10 = 0, vr3 = 0;
        int probeA1 = 0, probeN13 = 0, probeA2 = 0, probeC12 = 0, probeQ6b = 0;
        int bodyTri = 0, bodyC8 = 0, r52 = 0, probeNd = 0, probeNvca = 0, probeQ12b = 0;

        struct RestState
        {
            decltype(front)::Snapshot front {};
            decltype(pitch)::Snapshot pitch {};
            decltype(hold)::Snapshot hold {};
            decltype(click)::Snapshot click {};
            decltype(attack)::Snapshot attack {};
            decltype(body)::Snapshot body {};
            Values values {};
        };

        /** The rest state: each circuit settled with no trigger, upstream first, at the stock knobs. */
        static const RestState& restState() noexcept
        {
            static const RestState state = []
            {
                auto drum = std::make_unique<Tr909BassDrum>();
                auto& v = drum->values;
                drum->accentVolts = 3.13 * 2.5;
                drum->applyInputs();
                drum->front.settle();
                v.n4 = drum->front.probe(drum->probeN4);
                v.n7c = drum->front.probe(drum->probeN7c);
                v.n5c = drum->front.probe(drum->probeN5c);
                v.x = drum->front.probe(drum->probeX);
                drum->applyInputs();
                drum->pitch.settle();
                v.cv = drum->pitch.probe(drum->probeCv);
                v.c9 = drum->pitch.probe(drum->probeC9);
                drum->body.settle();
                v.q12b = drum->body.probe(drum->probeQ12b);
                drum->applyInputs();
                drum->hold.settle();
                v.c8 = drum->hold.probe(drum->probeC8);
                v.c7 = drum->hold.probe(drum->probeC7);
                v.e1 = drum->hold.probe(drum->probeE1);
                v.nk = drum->hold.probe(drum->probeNk);
                drum->applyInputs();
                drum->click.settle();
                v.q9e = drum->click.probe(drum->probeQ9e);
                v.n11c = drum->click.probe(drum->probeN11c);
                v.q8e = drum->click.probe(drum->probeQ8e);
                drum->applyInputs();
                drum->attack.settle();
                v.a1 = drum->attack.probe(drum->probeA1);
                v.c12 = drum->attack.probe(drum->probeC12);
                RestState s;
                s.front = drum->front.snapshot();
                s.pitch = drum->pitch.snapshot();
                s.hold = drum->hold.snapshot();
                s.click = drum->click.snapshot();
                s.attack = drum->attack.snapshot();
                s.body = drum->body.snapshot();
                s.values = v;
                return s;
            }();
            return state;
        }

        Values values {};
        double sampleRate = 96000.0;
        double triggerTime = -1.0, triggerSeconds = 0.002, accentVolts = 3.13 * 2.5;
        double triangle = 0.0, lastTriangle = 0.0, blampNext = 0.0, triangleOut = 0.0;
        bool comparatorHigh = true;
        double c14 = nominalC14, tuning = 1.0;
        double levelOhms = 50.0e3, vr2Ohms = 50.0e3, r26Ohms = 330.0e3, vr5Ohms = 150.0e3;
        bool frontResting = true, attackResting = true, clickResting = true, pitchLinear = true, envelopeRunning = false;
        int frontStill = 0, attackStill = 0, clickStill = 0, envelopeQuiet = 0;
        std::uint32_t noiseState = 0x9E3779B9u;
    };
}
