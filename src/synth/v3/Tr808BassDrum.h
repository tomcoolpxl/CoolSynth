#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "CircuitWdf.h"

/*
    The Roland TR-808 bass drum as its circuit (D235, D251), after Kurt James Werner's 2016 thesis and the service
    notes: four circuits, each a wave digital filter with an R-type root (CircuitWdf.h), each driving the next one
    way, as an ideal source.

        Front            the trigger as the 808 makes it (D251): Q32 an emitter follower onto the TRIG line, held
                         down by R113 when it lets go, and the bass drum's Q40, a PNP switch that can only push current
                         (R155, R154 through Q39, which lets go just after the pulse, as the instrument data changes);
                         then the pulse shaper (C40, R163, R162, D53) and, as the trigger's load, the envelope
                         generator's input (R157, R158, Q41's base as a diode)
        Envelope gen.    Q41 and Q42 hold Q43 on for some 5 ms; C39, D52 and R161 as its load
        Resonator        the bridged-T (C41, C42, R166, R165, R167) around the first op-amp, Q43 shorting R165 in the
                         attack, the retrigger pulse through C39, D52 and R161, the feedback buffer (R164, VR6, C43,
                         R169) around the second op-amp back through R170: the Decay. While Punch Time asks for a glide
                         (past 6 ms), a second resonator runs instead, the same with the glide bend beside Q43 (D251):
                         a second transistor across R165 whose base capacitor (0.47 uF at size 1) the trigger charges
                         through a diode and 4.7 kOhm (at size 1), drained by its own base current and 1 MOhm, so it
                         lets go of R165 gradually; the state passes between the two as the knob crosses 6 ms
        Output filter    the tone (R171, R172, VR5, C45), the level (VR4), the buffer Q44 as an ideal follower that
                         loads its base as the real one does

    Component values from the thesis's tables and the service notes; R164 is 47 kOhm as in the DAFx 2014 paper (the
    thesis's 4.7 kOhm makes the feedback loop's gain about 8 and the drum oscillate). Transistors as the thesis's 2N3904
    stand-in for the 2SC945(P) (Is 10 fA, betaF 300, betaR 4), the PNP Q40 the same turned round; diodes as the
    standard 1N4148 model for the 1S2473 (Is 2.52 nA, emission coefficient 1.752; the thesis keeps the Is without the
    coefficient, so its diodes turn on at half a silicon diode's voltage). The envelope generator's R159 is 106 kOhm, as
    the thesis tunes it; its added 6.8 nF C0 and C40's alpha transform are left out, as the trigger's step now falls
    between two samples and the solver passes it (D251). Volts in, volts out.
*/
namespace coolsynth::synth::v3
{
    class Tr808BassDrum final
    {
    public:
        static constexpr double thermal = 0.02585;
        static constexpr double transistorSaturation = 10.0e-15, betaF = 300.0, betaR = 4.0;
        static constexpr double diodeSaturation = 2.52e-9, diodeEmission = 1.752;
        static constexpr double supply = 15.0;
        static constexpr double nominalC41 = 0.015e-6, nominalC42 = 0.015e-6;
        /** C39 as drawn, 0.033 uF; the circuit holds it 20 % up, the top of its tolerance, as fitted to two real 808s
            (D251): their sighs settle slower than the drawing's, and their body's harmonics are stronger. */
        static constexpr double nominalC39 = 0.033e-6, fittedC39Scale = 1.2;
        /** Q32's base-emitter drop: its base sits this far above the TRIG line it drives. */
        static constexpr double followerDrop = 0.68;
        /** Q39 stays on this long after the pulse, until the instrument data changes (service notes, Fig. 6). */
        static constexpr double instrumentHold = 20.0e-6;
        static constexpr double switchOff = 1.0e9; // an open switch, as a resistance
        static constexpr double glideFarads = 0.47e-6; // the glide bend's capacitor at size 1

        Tr808BassDrum()
        {
            front.setNewtonLimit(40);    // the trigger's edges switch two transistors and a junction at once
            envelope.setNewtonLimit(40); // and the envelope generator with them
            buildFront();
            buildEnvelope();
            buildResonator(resonator, false);
            buildResonator(bentResonator, true);
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
            front.setSampleRate(rate);
            envelope.setSampleRate(rate);
            resonator.setSampleRate(rate);
            bentResonator.setSampleRate(rate);
            output.setSampleRate(rate);
            rest();
        }

        /** Back to rest: a copy of the solved rest state, no solving. */
        void rest() noexcept
        {
            const RestState& r = restState();
            front.setResistance(r154, switchOff);
            front.restore(r.front);
            envelope.restore(r.envelope);
            resonator.restore(r.resonator);
            bentResonator.restore(r.bentResonator);
            output.restore(r.output);
            triggerSample = -1;
            frontResting = envelopeResting = true;
            bent = false; // as a fresh drum: a glide asked for takes the stock resonator's rest state (setGlide)
            lastEnvelope = r.envelopeVolts;
            stillSamples = 0;
            front.setInput(frontDrive, 0.0);
            front.setInput(frontSupply, supply);
            envelope.setInput(envelopeIn, 0.0);
            envelope.setInput(envelopeSupply, supply);
            setResonatorInputs(resonator, { r.envelopeVolts, 0.0, 0.0 }, false);
            setResonatorInputs(bentResonator, { r.envelopeVolts, 0.0, 0.0 }, true);
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
            triggerSample = -1;
            front.setResistance(r154, switchOff);
            front.setInput(frontDrive, 0.0);
            front.setInput(frontSupply, supply);
            front.settle();
            envelope.setInput(envelopeIn, 0.0);
            envelope.setInput(envelopeSupply, supply);
            envelope.settle();
            const double eg = envelope.probe(envelopeOut);
            setResonatorInputs(resonator, { eg, 0.0, 0.0 }, false);
            resonator.settle();
            setResonatorInputs(bentResonator, { eg, 0.0, 0.0 }, true);
            bentResonator.settle();
            output.setInput(outputIn, 0.0);
            output.settle();
            frontResting = envelopeResting = true;
            lastEnvelope = eg;
            stillSamples = 0;
        }

        /** A hit: the TRIG line at this many volts (4 unaccented to 14), for this long. */
        void trigger(double volts, double widthSeconds = 0.001) noexcept
        {
            triggerVolts = volts;
            triggerWidth = widthSeconds;
            triggerSample = 0;
            front.setResistance(r154, 10.0e3); // the instrument data stands: Q39 on
        }

        /** Decay 0..1: VR6 from 0 to 500 kOhm; past 1 the extended-decay bend, VR6 up to 740 kOhm (the loop gain then nears the 0.94 where the drum would ring on). */
        void setDecay(double amount) noexcept
        {
            const double ohms = std::max(1.0, std::clamp(amount, 0.0, 1.48) * 500.0e3);
            resonator.setResistance(vr6, ohms);
            bentResonator.setResistance(vr6, ohms);
        }
        /** Tone 0..1: VR5 from 10 kOhm to 0 (the thesis's (1 - c) VR5). */
        void setTone(double amount) noexcept { output.setResistance(vr5, std::max(1.0, (1.0 - std::clamp(amount, 0.0, 1.0)) * 10.0e3)); }
        /**
            The Tone knob's turn, 0..1, as setTone's amount. VR5 is a C-taper pot (the service notes' parts list), so
            the share of it left in the circuit falls fast as the knob turns up: some 7 % at mid-travel, as fitted to
            the early brightness of two real 808s at the knob's marks (D251), as a textbook C taper leaves about 10 %.
        */
        [[nodiscard]] static double toneForKnob(double turn) noexcept
        {
            const double t = std::clamp(turn, 0.0, 1.0);
            constexpr double b = 190.0;
            return 1.0 - (std::pow(b, 1.0 - t) - 1.0) / (b - 1.0);
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
            for (auto c : { c41, c42 })
            {
                resonator.setCapacitance(c, nominalC41 * scale);
                bentResonator.setCapacitance(c, nominalC41 * scale);
            }
        }

        // The circuit bends the Kick's knobs reach (docs/research/kick-808-wdf.md).
        /** R166, the resistance left when Q43 shorts R165: smaller, a higher attack shift. Stock 6.8 kOhm. */
        void setAttackResistor(double ohms) noexcept
        {
            r166Ohms = std::max(10.0, ohms);
            resonator.setResistance(r166, r166Ohms);
            bentResonator.setResistance(r166, r166Ohms);
        }
        /** C38 scaled: the envelope generator, and so the attack shift, lasts longer or shorter. Stock 0.1 uF. */
        void setEnvelopeScale(double scale) noexcept { envelope.setCapacitance(c38, 0.1e-6 * std::max(0.01, scale)); }
        /**
            The glide bend's size: 0 leaves it out (the stock drum, its own resonator); larger, a longer glide onto the
            note. CG grows with it and RG shrinks, so the 1 ms trigger charges it as fully (D251). Crossing 0 hands the
            ringing drum's state from one resonator to the other.
        */
        void setGlide(double scale) noexcept
        {
            const bool wanted = scale > 0.0;
            bentResonator.setResistance(rg, wanted ? 4.7e3 / scale : switchOff);
            bentResonator.setCapacitance(cg, glideFarads * std::max(1.0e-3, scale));
            if (wanted == bent)
                return;
            if (wanted)
                bentResonator.takeSharedState(resonator);
            else
                resonator.takeSharedState(bentResonator);
            bent = wanted;
        }
        /**
            The common trigger's volts for a hit of the 808 family generators (D258): 4 V unaccented; Accent times the
            velocity lifts it to 11.5 V, where a real 808's AC LEVEL at its top acts in these circuits (808 From Mars's
            toms and congas by their pitch drop, page 14's accented over normal amplitudes). The service notes' text
            gives 4 to 14 V.
        */
        [[nodiscard]] static constexpr double triggerFor(double accentTimesVelocity) noexcept
        {
            return 4.0 + 7.5 * accentTimesVelocity;
        }

        /**
            The glide bend's size for a Punch Time: none up to the stock 6 ms; past it, the size at which the attack's
            pitch comes halfway down (in semitones, from its peak to the note) in 7.8 ms times Punch Time over 6 ms, as
            the stock drum does in 7.8 ms. Measured on the circuit model (tests/V3Tr808Tests.cpp, COOLSYNTH_808_JOBS). The
            Kick's and the 808 Drums' Punch Time.
        */
        [[nodiscard]] static double glideScaleFor(double seconds) noexcept
        {
            if (seconds <= 0.006 * 1.0001) // the knob's 6 ms arrives a hair over it: still the stock drum
                return 0.0;
            static constexpr std::array<double, 14> size { 0.0, 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0 };
            static constexpr std::array<double, 14> halfway { 0.0078, 0.0090, 0.0209, 0.0255, 0.0300, 0.0371, 0.0382, 0.0473,
                                                              0.0567, 0.0766, 0.0978, 0.1835, 0.3176, 0.5118 };
            const double target = 0.0078 * seconds / 0.006;
            for (std::size_t i = 1; i < size.size(); ++i)
                if (target <= halfway[i])
                    return size[i - 1] + (size[i] - size[i - 1]) * std::log(target / halfway[i - 1]) / std::log(halfway[i] / halfway[i - 1]);
            const double past = std::log(target / halfway.back()) / std::log(halfway.back() / halfway[halfway.size() - 2]);
            return std::min(32.0, size.back() + (size.back() - size[size.size() - 2]) * past);
        }
        /** R161, the leakage that makes the sigh: smaller, more sigh. Stock 1 MOhm. */
        void setLeakResistor(double ohms) noexcept
        {
            r161Ohms = std::clamp(ohms, 1.0e3, 1.0e9);
            resonator.setResistance(r161Resonator, r161Ohms);
            bentResonator.setResistance(r161Resonator, r161Ohms);
            envelope.setResistance(r161Envelope, r161Ohms);
        }
        /** C39 scaled from the drawing's value, in every circuit that holds it: the retrigger's charge, and so the
            sigh's length. The circuit starts at fittedC39Scale. */
        void setRetriggerScale(double scale) noexcept
        {
            envelope.setCapacitance(c39Envelope, nominalC39 * scale);
            resonator.setCapacitance(c39Resonator, nominalC39 * scale);
            bentResonator.setCapacitance(c39Resonator, nominalC39 * scale);
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
            // Q32's base: Q31 holds it down but for the pulse, when it rises to the accent's level. The step falls
            // between the hit's first two samples (the trapezoidal rule takes it as a ramp across that interval, so
            // its middle is half a sample in), and lasts the pulse's width. Q39 lets go just after the pulse.
            double drive = 0.0;
            if (triggerSample >= 0)
            {
                const int width = std::max(1, static_cast<int>(std::lround(triggerWidth * sampleRate)));
                if (triggerSample >= 1 && triggerSample <= width)
                    drive = triggerVolts + followerDrop;
                if (triggerSample >= width + static_cast<int>(std::lround(instrumentHold * sampleRate)))
                {
                    front.setResistance(r154, switchOff);
                    triggerSample = -1;
                }
                else
                    ++triggerSample;
            }
            // The front and the envelope rest once they have settled after a hit (their outputs still), so the tail
            // costs only the resonator and the output; a new hit wakes them.
            const bool active = triggerSample >= 0;
            if (active || ! frontResting)
            {
                front.setInput(frontDrive, drive);
                front.process();
                frontResting = ! active && std::abs(front.probe(frontPulse)) < 1.0e-9 && std::abs(front.probe(frontTrigger)) < 1.0e-6;
            }
            const double vin = frontResting ? 0.0 : front.probe(frontTrigger);
            if (active || ! envelopeResting)
            {
                envelope.setInput(envelopeIn, vin);
                envelope.process();
                const double e = envelope.probe(envelopeOut);
                stillSamples = std::abs(e - lastEnvelope) < 1.0e-6 ? stillSamples + 1 : 0; // above the solver's 0.1 uV
                lastEnvelope = e;
                envelopeResting = frontResting && stillSamples > static_cast<int>(0.005 * sampleRate);
            }
            // At rest the envelope's output is the rest state's (lastEnvelope), as rest() and settle() leave it.
            const std::array<double, 3> in { lastEnvelope, frontResting ? 0.0 : front.probe(frontPulse), vin };
            const double v = bent ? runResonator(bentResonator, in, true) : runResonator(resonator, in, false);
            output.setInput(outputIn, clipToRails(v));
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
        [[nodiscard]] double pulseVolts() const noexcept { return front.probe(frontPulse); }
        [[nodiscard]] double triggerNodeVolts() const noexcept { return front.probe(frontTrigger); }
        [[nodiscard]] double envelopeVolts() const noexcept { return lastEnvelope; }
        [[nodiscard]] double resonatorVolts() const noexcept { return bent ? bentResonator.probe(resonatorOut) : resonator.probe(resonatorOut); }
        [[nodiscard]] int newtonSteps() const noexcept { return std::max(frontNewtonSteps(), resonatorNewtonSteps()); }
        [[nodiscard]] int frontNewtonSteps() const noexcept
        {
            return std::max(frontResting ? 0 : front.lastNewtonSteps(), envelopeResting ? 0 : envelope.lastNewtonSteps());
        }
        [[nodiscard]] int resonatorNewtonSteps() const noexcept { return bent ? bentResonator.lastNewtonSteps() : resonator.lastNewtonSteps(); }
        [[nodiscard]] bool frontIsResting() const noexcept { return frontResting && envelopeResting; }
        [[nodiscard]] bool lastSolveConverged() const noexcept
        {
            return front.lastSolveConverged() && envelope.lastSolveConverged()
                   && (bent ? bentResonator.lastSolveConverged() : resonator.lastSolveConverged());
        }

    private:
        static constexpr double diodeThermal = thermal * diodeEmission;
        /** Q41's base as the front sees it: in saturation (as it is while the trigger drives it) its base current is
            Is (1 / betaF + 1 / betaR) e^(vbe / VT), a diode's. */
        static constexpr double baseSaturation = transistorSaturation * (1.0 / betaF + 1.0 / betaR);

        // Front: 1 Q32's drive (input), 2 its base, 3 the supply (input), 4 the TRIG line, 5 Q40's base, 6 its
        // collector (the drum's trigger), 7 the shaped pulse, 8 Q41's base.
        void buildFront()
        {
            frontDrive = front.addInput(1);
            frontSupply = front.addInput(3);
            front.addResistor(1, 2, 4.7e3);                       // R120
            front.addTransistor(3, 2, 4, transistorSaturation, betaF, betaR, thermal);    // Q32
            front.addResistor(4, 0, 4.7e3);                       // R113
            front.addResistor(4, 5, 4.7e3);                       // R155
            r154 = front.addResistor(5, 0, switchOff);            // R154 through Q39: open but while the data stands
            front.addPnpTransistor(6, 5, 4, transistorSaturation, betaF, betaR, thermal); // Q40
            front.addCapacitor(6, 7, 0.015e-6);                   // C40
            front.addResistor(6, 7, 100.0e3);                     // R163
            front.addResistor(7, 0, 4.7e3);                       // R162
            front.addDiode(0, 7, diodeSaturation, diodeThermal);  // D53, its anode at ground
            front.addResistor(6, 8, 8.2e3);                       // R157
            front.addResistor(8, 0, 2.7e3);                       // R158
            front.addDiode(8, 0, baseSaturation, thermal);        // Q41's base, as the trigger's load
            frontTrigger = front.addProbe(6);
            frontPulse = front.addProbe(7);
        }

        // Envelope generator: 1 the trigger (input), 2 Q41's base, 3 its collector, 4 the supply (input), 5 Q42's
        // collector (the output), 6 D52.
        void buildEnvelope()
        {
            envelopeIn = envelope.addInput(1);
            envelopeSupply = envelope.addInput(4);
            envelope.addResistor(1, 2, 8.2e3);                    // R157
            envelope.addResistor(2, 0, 2.7e3);                    // R158
            envelope.addResistor(4, 3, 1.0e6);                    // R156
            c38 = envelope.addCapacitor(3, 0, 0.1e-6);            // C38
            envelope.addResistor(4, 5, 22.0e3);                   // R160
            c39Envelope = envelope.addCapacitor(5, 6, nominalC39 * fittedC39Scale); // C39
            r161Envelope = envelope.addResistor(6, 0, 1.0e6);     // R161, to the resonator's top node, near ground
            envelope.addResistor(5, 0, 106.0e3);                  // R159 to Q43's base, as the thesis tunes it
            envelope.addTransistor(3, 2, 0, transistorSaturation, betaF, betaR, thermal); // Q41
            envelope.addTransistor(5, 3, 0, transistorSaturation, betaF, betaR, thermal); // Q42
            envelope.addDiode(6, 0, diodeSaturation, diodeThermal);                       // D52
            envelopeOut = envelope.addProbe(5);
        }

        // Resonator: 1 the envelope (input), 2 the shaped pulse (input), 3 D52, 4 the top of the T, 5 Q43's base,
        // 6 Q43's collector, 7 the first op-amp's minus, 8 its output, 9 the second op-amp's minus, 10 VR6 to C43,
        // 11 the second op-amp's output, 12 its plus; with the glide bend, 13 the trigger (input), 14 the glide's
        // diode, 15 the glide transistor's base. The bend's parts come last, so the stock resonator's ports and
        // devices are the bent one's first (takeSharedState).
        template <class Circuit>
        void buildResonator(Circuit& r, bool bend)
        {
            r.addInput(1);
            r.addInput(2);
            if (bend)
                r.addInput(13);
            c39Resonator = r.addCapacitor(1, 3, nominalC39 * fittedC39Scale); // C39
            r161Resonator = r.addResistor(3, 4, 1.0e6); // R161
            r.addResistor(1, 5, 100.0e3);               // R159
            r166 = r.addResistor(4, 6, 6.8e3);          // R166
            r.addResistor(6, 0, 47.0e3);                // R165
            c41 = r.addCapacitor(4, 7, nominalC41);     // C41
            c42 = r.addCapacitor(4, 8, nominalC42);     // C42
            r.addResistor(7, 8, 1.0e6);                 // R167
            r.addResistor(8, 0, 1.0e6);                 // the load
            r.addResistor(8, 9, 47.0e3);                // R164
            vr6 = r.addResistor(9, 10, 200.0e3);        // VR6
            r.addCapacitor(10, 11, 33.0e-6);            // C43
            r.addResistor(9, 11, 47.0e3);               // R169
            r.addResistor(12, 0, 22.0e3);               // R168
            r.addResistor(11, 4, 470.0e3);              // R170
            r.addNullor(2, 7, 8);                       // the first op-amp
            r.addNullor(12, 9, 11);                     // the second
            r.addTransistor(6, 5, 0, transistorSaturation, betaF, betaR, thermal); // Q43
            r.addDiode(3, 0, diodeSaturation, diodeThermal);                       // D52
            if (bend)
            {
                r.addDiode(13, 14, diodeSaturation, diodeThermal);                      // DG
                rg = r.addResistor(14, 15, switchOff);                                  // RG
                cg = r.addCapacitor(15, 0, glideFarads * 1.0e-3);                       // CG
                r.addResistor(15, 0, 1.0e6);                                            // RB
                r.addTransistor(6, 15, 0, transistorSaturation, betaF, betaR, thermal); // QG, across R165
            }
            resonatorOut = r.addProbe(8);
        }

        /** A resonator's inputs: the envelope, the shaped pulse and, for the bent one only, the trigger. */
        template <class Circuit>
        static void setResonatorInputs(Circuit& r, const std::array<double, 3>& in, bool withTrigger) noexcept
        {
            r.setInput(0, in[0]);
            r.setInput(1, in[1]);
            if (withTrigger)
                r.setInput(2, in[2]);
        }

        template <class Circuit>
        double runResonator(Circuit& r, const std::array<double, 3>& in, bool withTrigger) noexcept
        {
            setResonatorInputs(r, in, withTrigger);
            r.process();
            return r.probe(resonatorOut);
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
            output.addResistor(7, 0, (betaF + 1.0) * (6.8e3 * 100.0e3 / 106.8e3)); // Q44's input: (betaF + 1)(R173 || R174)
            output.addNullor(7, 8, 8);                  // Q44 as an ideal emitter follower
            output.addResistor(8, 0, 6.8e3);            // R173 (to the negative supply: ground for the signal)
            output.addCapacitor(8, 0, 220.0e-12);       // C48
            output.addCapacitor(8, 9, 1.0e-6);          // C50
            output.addResistor(9, 0, 100.0e3);          // R174
            outputOut = output.addProbe(9);
        }

        wdf::Circuit<8, 15, 2, 0, 6, 2> front;
        wdf::Circuit<6, 13, 2, 0, 5, 1> envelope;
        wdf::Circuit<12, 18, 2, 2, 3, 1> resonator;
        wdf::Circuit<15, 24, 3, 2, 6, 1> bentResonator;
        wdf::Circuit<9, 15, 1, 1, 0, 1> output;

        int frontDrive = 0, frontSupply = 0, frontTrigger = 0, frontPulse = 0;
        int envelopeIn = 0, envelopeSupply = 0, envelopeOut = 0;
        int resonatorOut = 0, c41 = 0, c42 = 0, vr6 = 0;
        int outputIn = 0, outputOut = 0, vr5 = 0, vr4Top = 0, vr4Bottom = 0;

        struct RestState
        {
            decltype(front)::Snapshot front {};
            decltype(envelope)::Snapshot envelope {};
            decltype(resonator)::Snapshot resonator {};
            decltype(bentResonator)::Snapshot bentResonator {};
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
                s.front = drum->front.snapshot();
                s.envelope = drum->envelope.snapshot();
                s.resonator = drum->resonator.snapshot();
                s.bentResonator = drum->bentResonator.snapshot();
                s.output = drum->output.snapshot();
                s.envelopeVolts = drum->envelope.probe(drum->envelopeOut);
                return s;
            }();
            return state;
        }

        double sampleRate = 96000.0;
        double triggerVolts = 4.0, triggerWidth = 0.001;
        int triggerSample = -1; // samples since the hit, -1 between hits
        int c38 = 0, c39Envelope = 0, c39Resonator = 0, r161Envelope = 0, r161Resonator = 0, r166 = 0, r154 = 0, rg = 0, cg = 0;
        double r166Ohms = 6.8e3, r161Ohms = 1.0e6, rails = 15.0, tuning = 1.0;
        bool frontResting = false, envelopeResting = false, bent = false;
        double lastEnvelope = 0.0;
        int stillSamples = 0;
    };
}
