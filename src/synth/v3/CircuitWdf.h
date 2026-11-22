#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/*
    A wave digital filter of one circuit with an R-type root that holds every element and the circuit's nonlinear
    devices together (Werner's 2016 thesis, chapters 2 and 4; D235). The 808 bass drum's resonator and envelope have
    several nonlinear ports (transistors, a diode) inside one non-series-parallel network with op-amps, which no
    shipped library solves, so the root is built here.

    Every element is a port of the root: resistors, capacitors (the bilinear transform, or the alpha transform the
    thesis uses for the pulse shaper's C40, as chowdsp_wdf's CapacitorAlphaT), resistive voltage sources, and the
    devices' ports. Ideal voltage inputs and ideal op-amps (nullors) are part of the root's nodal analysis.

    The scattering matrix comes from modified nodal analysis of the root with each port replaced by its Norton
    equivalent (Werner, Smith, Abel, DAFx 2015), recomputed only when a resistance or capacitance changes:
    b = S a + Q u, with u the ideal inputs. The nonlinear ports are solved at the root each sample by Newton's method
    in their port voltages (Werner, Nangia, Smith, Abel, DAFx 2015; Olsen, Werner, Smith, DAFx 2016), started from
    where the last samples point (solveDevices), with SPICE's junction limiting. Diodes and transistors follow the thesis's models
    (Shockley; Ebers-Moll in transport form). Double precision; all storage inline, no heap.
*/
namespace coolsynth::synth::v3::wdf
{
    /** Limits of one circuit: nodes besides ground, ports, ideal inputs, op-amps, nonlinear ports, probes. */
    template <int MaxNodes, int MaxPorts, int MaxInputs, int MaxNullors, int MaxNonlinear, int MaxProbes>
    class Circuit final
    {
        template <int, int, int, int, int, int>
        friend class Circuit; // takeSharedState reads another variant's state

    public:
        static constexpr int maxUnknowns = MaxNodes + MaxInputs + MaxNullors;
        static constexpr int maxColumns = MaxPorts + MaxInputs;
        static constexpr int maxDevicePorts = MaxNonlinear > 0 ? MaxNonlinear : 1;

        // ---- Building (message thread or prepare: no heap, so also safe on the audio thread) -------------------

        int addResistor(int n1, int n2, double ohms) { return addPort(n1, n2, Kind::resistor, ohms, 0.0, 1.0); }

        /** alpha 1 is the bilinear transform, 0 backward Euler (the thesis's C40 uses 0.029). */
        int addCapacitor(int n1, int n2, double farads, double alpha = 1.0) { return addPort(n1, n2, Kind::capacitor, 1.0, farads, alpha); }

        /** A voltage source in series with a resistance, its value set each sample with setSource. */
        int addSource(int n1, int n2, double ohms) { return addPort(n1, n2, Kind::source, ohms, 0.0, 1.0); }

        /** An ideal voltage source from a node to ground, set each sample with setInput. */
        int addInput(int node)
        {
            inputNode[static_cast<std::size_t>(inputCount)] = node;
            return inputCount++;
        }

        /** An ideal op-amp: the voltages at plus and minus equal, any current into out. */
        void addNullor(int plus, int minus, int out)
        {
            nullors[static_cast<std::size_t>(nullorCount++)] = { plus, minus, out };
        }

        /** A voltage-controlled current source: siemens times (V(plus) - V(minus)) into node out. At most one an op-amp. */
        void addTransconductance(int plus, int minus, int out, double siemens)
        {
            gains[static_cast<std::size_t>(gainCount++)] = { plus, minus, out, siemens };
        }

        /**
            An op-amp of finite gain-bandwidth, as the SPICE references' single-pole macro (D252): 1 mS into the node
            inner, held there by 100 MOhm (a DC gain of 100 dB) and the pole's capacitor, then an ideal follower to out.
            It takes a node, two ports and a nullor; with railVolts, also a clamp pair (one more port, a nonlinear one)
            on the inner node that holds the output inside that swing, as the references' clamp diodes (1 pA each, into
            sources 0.55 V inside it).
        */
        void addOpAmp(int plus, int minus, int out, int inner, double gainBandwidthHz, double railVolts = 0.0)
        {
            addTransconductance(plus, minus, inner, 1.0e-3);
            addResistor(inner, 0, 1.0e8);
            addCapacitor(inner, 0, 1.0e-3 / (6.283185307179586 * gainBandwidthHz));
            addNullor(inner, out, out);
            if (railVolts > 0.0)
                addClampPair(inner, 0, railVolts - 0.55, 1.0e-12, 0.02585);
        }

        /** A junction diode from anode to cathode (Shockley), its port seen through portOhms. */
        void addDiode(int anode, int cathode, double saturation, double thermal, double portOhms = 1000.0)
        {
            Device& d = devices[static_cast<std::size_t>(deviceCount++)];
            d.isTransistor = false;
            d.saturation = saturation;
            d.thermal = thermal;
            d.derive();
            d.port[0] = addPort(anode, cathode, Kind::nonlinear, portOhms, 0.0, 1.0);
            d.slot[0] = nonlinearCount++;
            nonlinearPort[static_cast<std::size_t>(d.slot[0])] = d.port[0];
        }

        /** Two diodes back to back between two nodes (a soft clipper), as one port: Is (e^(v / VT) - e^(-v / VT)). */
        void addDiodePair(int n1, int n2, double saturation, double thermal, double portOhms = 1000.0)
        {
            addDiode(n1, n2, saturation, thermal, portOhms);
            devices[static_cast<std::size_t>(deviceCount - 1)].isPair = true;
        }

        /**
            An op-amp's output swing (D252): a diode pair from n1 to n2 that conducts only past volts either way, as two
            diodes into a source of volts each, Is (e^((v - volts) / VT) - e^((-v - volts) / VT)). On an op-amp's inner
            node (addOpAmp) it holds the output inside its rails as the reference's clamp diodes do.
        */
        void addClampPair(int n1, int n2, double volts, double saturation, double thermal, double portOhms = 1000.0)
        {
            addDiodePair(n1, n2, saturation, thermal, portOhms);
            devices[static_cast<std::size_t>(deviceCount - 1)].offset = volts;
        }

        /** An NPN transistor (Ebers-Moll, transport form), as two ports: base-emitter and collector-emitter. */
        void addTransistor(int collector, int base, int emitter, double saturation, double betaF, double betaR, double thermal)
        {
            Device& d = devices[static_cast<std::size_t>(deviceCount++)];
            d.isTransistor = true;
            d.saturation = saturation;
            d.betaF = betaF;
            d.betaR = betaR;
            d.thermal = thermal;
            d.derive();
            d.port[0] = addPort(base, emitter, Kind::nonlinear, 2000.0, 0.0, 1.0);
            d.port[1] = addPort(collector, emitter, Kind::nonlinear, 20000.0, 0.0, 1.0);
            d.slot[0] = nonlinearCount++;
            d.slot[1] = nonlinearCount++;
            nonlinearPort[static_cast<std::size_t>(d.slot[0])] = d.port[0];
            nonlinearPort[static_cast<std::size_t>(d.slot[1])] = d.port[1];
        }

        /** A PNP transistor: the NPN's equations with every voltage and current turned round. */
        void addPnpTransistor(int collector, int base, int emitter, double saturation, double betaF, double betaR, double thermal)
        {
            addTransistor(collector, base, emitter, saturation, betaF, betaR, thermal);
            devices[static_cast<std::size_t>(deviceCount - 1)].polarity = -1.0;
        }

        int addProbe(int node)
        {
            probeNode[static_cast<std::size_t>(probeCount)] = node;
            return probeCount++;
        }

        void setResistance(int port, double ohms) noexcept
        {
            auto& p = ports[static_cast<std::size_t>(port)];
            if (p.value != ohms)
            {
                p.value = ohms;
                dirty = true;
            }
        }

        void setCapacitance(int port, double farads) noexcept
        {
            auto& p = ports[static_cast<std::size_t>(port)];
            if (p.farads != farads)
            {
                p.farads = farads;
                dirty = true;
            }
        }

        void setSource(int port, double volts) noexcept { ports[static_cast<std::size_t>(port)].volts = volts; }

        /** A capacitor charged to a voltage and still, as settle() leaves it: for handing a state from elsewhere. */
        void setCapacitorVoltage(int port, double volts) noexcept
        {
            auto& p = ports[static_cast<std::size_t>(port)];
            p.toRoot = volts;
            p.fromRoot = volts;
            p.alphaState = volts;
        }
        void setInput(int input, double volts) noexcept { inputs[static_cast<std::size_t>(input)] = volts; }

        void setSampleRate(double rate) noexcept
        {
            sampleRate = rate;
            dirty = true;
        }

        // ---- Running -------------------------------------------------------------------------------------------

        /** Clears every state: capacitors at 0 V, devices off. */
        void reset() noexcept
        {
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                p.toRoot = 0.0;
                p.fromRoot = 0.0;
            }
            for (int m = 0; m < nonlinearCount; ++m)
                voltage[static_cast<std::size_t>(m)] = 0.0;
            previousVoltage = earlierVoltage = voltage;
        }

        /**
            The circuit at rest with its inputs as set: capacitors open, then their states set to the voltages they
            hold, so a note starts from the circuit's own operating point, not from its power-up. Cheap enough for
            prepare: one analysis and a damped Newton solve.
        */
        void settle() noexcept
        {
            // Capacitors as open circuits: a very large port resistance and no stored wave.
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                if (p.kind == Kind::capacitor)
                    p.toRoot = 0.0;
            }
            openCapacitors = true;
            dirty = true;
            for (int pass = 0; pass < 4; ++pass)
                step(60);
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                if (p.kind == Kind::capacitor)
                {
                    const double v = 0.5 * (p.toRoot + p.fromRoot);
                    p.toRoot = v;
                    p.fromRoot = v;
                    p.alphaState = v;
                }
            }
            openCapacitors = false;
            dirty = true;
        }

        /** The circuit's state (its waves and its devices' last solution), to come back to cheaply. */
        struct Snapshot
        {
            std::array<double, MaxPorts> toRoot {}, fromRoot {}, alphaState {};
            std::array<double, MaxNonlinear> voltage {}, current {};
        };

        [[nodiscard]] Snapshot snapshot() const noexcept
        {
            Snapshot s;
            for (int k = 0; k < portCount; ++k)
            {
                const auto& p = ports[static_cast<std::size_t>(k)];
                s.toRoot[static_cast<std::size_t>(k)] = p.toRoot;
                s.fromRoot[static_cast<std::size_t>(k)] = p.fromRoot;
                s.alphaState[static_cast<std::size_t>(k)] = p.alphaState;
            }
            s.voltage = voltage;
            s.current = current;
            return s;
        }

        /**
            Takes another circuit's state for the ports and devices both have, when the other was built first in the
            same order (a variant with parts added at the end, D251); the rest start at rest.
        */
        template <class Other>
        void takeSharedState(const Other& other) noexcept
        {
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                const bool shared = k < other.portCount;
                p.toRoot = shared ? other.ports[static_cast<std::size_t>(k)].toRoot : 0.0;
                p.fromRoot = shared ? other.ports[static_cast<std::size_t>(k)].fromRoot : 0.0;
                p.alphaState = shared ? other.ports[static_cast<std::size_t>(k)].alphaState : 0.0;
            }
            for (int m = 0; m < nonlinearCount; ++m)
            {
                const bool shared = m < other.nonlinearCount;
                const auto i = static_cast<std::size_t>(m);
                voltage[i] = shared ? other.voltage[i] : 0.0;
                current[i] = shared ? other.current[i] : 0.0;
                previousVoltage[i] = shared ? other.previousVoltage[i] : 0.0;
                earlierVoltage[i] = shared ? other.earlierVoltage[i] : 0.0;
            }
        }

        /**
            As takeSharedState, but the ports and devices only this circuit has keep their state: a variant with parts
            added at the end taking over from the plain one while its own parts stay as they were (at rest, say: the
            808 Metal's envelopes, D255).
        */
        template <class Other>
        void adoptSharedState(const Other& other) noexcept
        {
            const int sharedPorts = std::min(portCount, other.portCount);
            for (int k = 0; k < sharedPorts; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                const auto& o = other.ports[static_cast<std::size_t>(k)];
                p.toRoot = o.toRoot;
                p.fromRoot = o.fromRoot;
                p.alphaState = o.alphaState;
            }
            const int shared = std::min(nonlinearCount, other.nonlinearCount);
            for (int m = 0; m < shared; ++m)
            {
                const auto i = static_cast<std::size_t>(m);
                voltage[i] = other.voltage[i];
                current[i] = other.current[i];
                previousVoltage[i] = other.previousVoltage[i];
                earlierVoltage[i] = other.earlierVoltage[i];
            }
            newtonSteps = 0;
        }

        void restore(const Snapshot& s) noexcept
        {
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                p.toRoot = s.toRoot[static_cast<std::size_t>(k)];
                p.fromRoot = s.fromRoot[static_cast<std::size_t>(k)];
                p.alphaState = s.alphaState[static_cast<std::size_t>(k)];
            }
            voltage = s.voltage;
            previousVoltage = earlierVoltage = s.voltage; // a rest state is still
            current = s.current;
            newtonSteps = 0; // as a fresh circuit, so a restored one solves its next sample the same way
            solved = true;
        }

        /** One sample. */
        void process() noexcept { step(newtonLimit); }

        /** The most Newton iterations a sample may take: 12 by default; circuits that switch hard on an edge need more. */
        void setNewtonLimit(int limit) noexcept { newtonLimit = std::max(1, limit); }

        [[nodiscard]] double probe(int index) const noexcept { return probes[static_cast<std::size_t>(index)]; }
        [[nodiscard]] double portVoltage(int port) const noexcept
        {
            const auto& p = ports[static_cast<std::size_t>(port)];
            return 0.5 * (p.toRoot + p.fromRoot);
        }
        [[nodiscard]] int lastNewtonSteps() const noexcept { return newtonSteps; }
        /**
            False when the nonlinear ports' currents are tied by the linear part alone (two devices in series with
            nothing else at the node between them, say): the root's solve is then meaningless. A junction's own
            capacitance, as SPICE has it, unties them.
        */
        [[nodiscard]] bool isWellPosed() noexcept
        {
            if (dirty)
                analyse();
            return wellPosed;
        }

        /** False when the last sample's devices did not converge, even by source stepping. */
        [[nodiscard]] bool lastSolveConverged() const noexcept { return solved; }

        static constexpr int maxNewtonSteps = 12;

    private:
        enum class Kind : int { resistor, capacitor, source, nonlinear };

        struct Port
        {
            int n1 = 0, n2 = 0;
            Kind kind = Kind::resistor;
            double value = 1.0;   // ohms: resistors, sources and the devices' port resistances
            double farads = 0.0;  // capacitors
            double alpha = 1.0;
            double keep = 0.0, take = 1.0; // the alpha transform's update: (1 - alpha) / 2 and (1 + alpha) / 2
            double volts = 0.0;   // sources
            double resistance = 1.0; // the port resistance in use
            double toRoot = 0.0;   // the wave from the element into the root (a)
            double fromRoot = 0.0; // the wave from the root into the element (b)
            double alphaState = 0.0; // the alpha transform's last reflected wave
        };

        struct Nullor
        {
            int plus = 0, minus = 0, out = 0;
        };

        struct Gain
        {
            int plus = 0, minus = 0, out = 0;
            double siemens = 0.0;
        };

        struct Device
        {
            bool isTransistor = false, isPair = false;
            double polarity = 1.0; // -1 for a PNP: its junction voltages and currents are the NPN's turned round
            double offset = 0.0;   // a clamp pair's: each diode conducts only past this many volts
            double saturation = 1.0e-14, betaF = 100.0, betaR = 1.0, thermal = 0.02585;
            int port[2] {}, slot[2] {};
            // Derived once, for the solver: 1 / VT, 1 / betaF, 1 / betaR, 1 + 1 / betaR, pnjlim's critical voltage.
            double inverseThermal = 1.0 / 0.02585, inverseBetaF = 0.01, inverseBetaR = 1.0, reverseFactor = 2.0, critical = 0.6;

            void derive() noexcept
            {
                inverseThermal = 1.0 / thermal;
                inverseBetaF = 1.0 / betaF;
                inverseBetaR = 1.0 / betaR;
                reverseFactor = 1.0 + inverseBetaR;
                critical = thermal * std::log(thermal / (1.4142135623730951 * saturation));
            }
        };

        int addPort(int n1, int n2, Kind kind, double ohms, double farads, double alpha)
        {
            Port& p = ports[static_cast<std::size_t>(portCount)];
            p = Port {};
            p.n1 = n1;
            p.n2 = n2;
            p.kind = kind;
            p.value = ohms;
            p.farads = farads;
            p.alpha = alpha;
            p.keep = 0.5 * (1.0 - alpha);
            p.take = 0.5 * (1.0 + alpha);
            dirty = true;
            return portCount++;
        }

        double portResistance(const Port& p) const noexcept
        {
            switch (p.kind)
            {
                case Kind::capacitor:
                    return openCapacitors ? 1.0e12 : 1.0 / ((1.0 + p.alpha) * p.farads * sampleRate);
                case Kind::resistor:
                case Kind::source:
                case Kind::nonlinear:
                    break;
            }
            return p.value;
        }

        // ---- The root's analysis -------------------------------------------------------------------------------

        /** The analysis's matrices, needed only while it runs: one set a thread, not one a circuit. */
        struct Scratch
        {
            std::array<std::array<double, maxUnknowns>, maxUnknowns> Y {};
            std::array<std::array<double, maxColumns>, maxUnknowns> X {};
            std::array<std::array<double, MaxPorts>, MaxPorts> S {};
            std::array<std::array<double, MaxInputs>, MaxPorts> Q {};
            std::array<std::array<double, maxColumns>, MaxProbes> W {};
            std::array<std::array<double, (MaxNonlinear > 0 ? MaxNonlinear : 1)>, (MaxNonlinear > 0 ? MaxNonlinear : 1)> M {};
        };

        void analyse() noexcept
        {
            static thread_local Scratch scratchSpace;
            auto& Y = scratchSpace.Y;
            auto& X = scratchSpace.X;
            auto& S = scratchSpace.S;
            auto& Q = scratchSpace.Q;
            auto& W = scratchSpace.W;
            auto& M = scratchSpace.M;
            const int unknowns = nodeCount() + inputCount + nullorCount;
            const int columns = portCount + inputCount;
            for (int r = 0; r < unknowns; ++r)
            {
                for (int c = 0; c < unknowns; ++c)
                    Y[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = 0.0;
                for (int c = 0; c < columns; ++c)
                    X[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = 0.0;
            }
            auto stampY = [&](int row, int col, double v)
            {
                if (row > 0 && col > 0)
                    Y[static_cast<std::size_t>(row - 1)][static_cast<std::size_t>(col - 1)] += v;
            };
            // Each port as its Norton equivalent: a conductance and a current a/R into n1.
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                p.resistance = portResistance(p);
                const double g = 1.0 / p.resistance;
                stampY(p.n1, p.n1, g);
                stampY(p.n2, p.n2, g);
                stampY(p.n1, p.n2, -g);
                stampY(p.n2, p.n1, -g);
                if (p.n1 > 0)
                    X[static_cast<std::size_t>(p.n1 - 1)][static_cast<std::size_t>(k)] += g;
                if (p.n2 > 0)
                    X[static_cast<std::size_t>(p.n2 - 1)][static_cast<std::size_t>(k)] -= g;
            }
            const int n = nodeCount();
            // Ideal inputs: V(node) = u, their current an unknown.
            for (int j = 0; j < inputCount; ++j)
            {
                const int row = n + j;
                const int node = inputNode[static_cast<std::size_t>(j)];
                Y[static_cast<std::size_t>(row)][static_cast<std::size_t>(node - 1)] = 1.0;
                Y[static_cast<std::size_t>(node - 1)][static_cast<std::size_t>(row)] -= 1.0;
                X[static_cast<std::size_t>(row)][static_cast<std::size_t>(portCount + j)] = 1.0;
            }
            // Op-amps: V(plus) - V(minus) = 0, the output's current an unknown.
            for (int j = 0; j < nullorCount; ++j)
            {
                const int row = n + inputCount + j;
                const auto& o = nullors[static_cast<std::size_t>(j)];
                if (o.plus > 0)
                    Y[static_cast<std::size_t>(row)][static_cast<std::size_t>(o.plus - 1)] += 1.0;
                if (o.minus > 0)
                    Y[static_cast<std::size_t>(row)][static_cast<std::size_t>(o.minus - 1)] -= 1.0;
                Y[static_cast<std::size_t>(o.out - 1)][static_cast<std::size_t>(row)] -= 1.0;
            }
            // Transconductances: g (V(plus) - V(minus)) into out, moved to the left of out's KCL row.
            for (int j = 0; j < gainCount; ++j)
            {
                const auto& t = gains[static_cast<std::size_t>(j)];
                stampY(t.out, t.plus, -t.siemens);
                stampY(t.out, t.minus, t.siemens);
            }

            // Solve Y X = [Ja Ju] by Gaussian elimination with partial pivoting, all columns at once.
            for (int col = 0; col < unknowns; ++col)
            {
                int pivot = col;
                for (int r = col + 1; r < unknowns; ++r)
                    if (std::abs(Y[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) > std::abs(Y[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)]))
                        pivot = r;
                if (pivot != col)
                {
                    std::swap(Y[static_cast<std::size_t>(pivot)], Y[static_cast<std::size_t>(col)]);
                    std::swap(X[static_cast<std::size_t>(pivot)], X[static_cast<std::size_t>(col)]);
                }
                const double d = Y[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
                if (std::abs(d) < 1.0e-300)
                    continue;
                for (int r = col + 1; r < unknowns; ++r)
                {
                    const double f = Y[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)] / d;
                    if (f == 0.0)
                        continue;
                    for (int c = col; c < unknowns; ++c)
                        Y[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -= f * Y[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                    for (int c = 0; c < columns; ++c)
                        X[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -= f * X[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                }
            }
            for (int col = unknowns - 1; col >= 0; --col)
            {
                const double d = Y[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
                for (int c = 0; c < columns; ++c)
                {
                    double v = X[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                    for (int k = col + 1; k < unknowns; ++k)
                        v -= Y[static_cast<std::size_t>(col)][static_cast<std::size_t>(k)] * X[static_cast<std::size_t>(k)][static_cast<std::size_t>(c)];
                    X[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)] = std::abs(d) < 1.0e-300 ? 0.0 : v / d;
                }
            }

            // Port voltages: v = A [a; u]; b = 2v - a, so S = 2A_a - I and Q = 2A_u. Probes read X's node rows.
            auto nodeRow = [&](int node, int c) { return node > 0 ? X[static_cast<std::size_t>(node - 1)][static_cast<std::size_t>(c)] : 0.0; };
            for (int k = 0; k < portCount; ++k)
            {
                const auto& p = ports[static_cast<std::size_t>(k)];
                for (int c = 0; c < columns; ++c)
                {
                    const double v = nodeRow(p.n1, c) - nodeRow(p.n2, c);
                    if (c < portCount)
                        S[static_cast<std::size_t>(k)][static_cast<std::size_t>(c)] = 2.0 * v - (c == k ? 1.0 : 0.0);
                    else
                        Q[static_cast<std::size_t>(k)][static_cast<std::size_t>(c - portCount)] = 2.0 * v;
                }
            }
            for (int j = 0; j < probeCount; ++j)
                for (int c = 0; c < columns; ++c)
                    W[static_cast<std::size_t>(j)][static_cast<std::size_t>(c)] = nodeRow(probeNode[static_cast<std::size_t>(j)], c);

            // Which ports send a wave into the root (capacitors, sources, devices) and which keep one (capacitors).
            // The linear senders come first in `sending`, the devices after them.
            sendingCount = capacitorCount = 0;
            for (int k = 0; k < portCount; ++k)
            {
                const auto kind = ports[static_cast<std::size_t>(k)].kind;
                if (kind == Kind::capacitor || kind == Kind::source)
                    sending[static_cast<std::size_t>(sendingCount++)] = k;
                if (kind == Kind::capacitor)
                    capacitorPort[static_cast<std::size_t>(capacitorCount++)] = k;
            }
            linearSendingCount = sendingCount;
            for (int k = 0; k < portCount; ++k)
                if (ports[static_cast<std::size_t>(k)].kind == Kind::nonlinear)
                    sending[static_cast<std::size_t>(sendingCount++)] = k;

            // The nonlinear ports' part: b_N = c + S_NN a_N; with a_N = b_N - 2 R i, v = b_N - R i:
            // v = p + K i, where p = M c, M = (I - S_NN)^-1, K = -(2 M S_NN + I) R.
            const int m = nonlinearCount;
            std::array<std::array<double, MaxNonlinear>, MaxNonlinear> A {};
            std::array<std::array<double, MaxNonlinear>, MaxNonlinear> inv {};
            for (int r = 0; r < m; ++r)
                for (int c = 0; c < m; ++c)
                {
                    A[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = (r == c ? 1.0 : 0.0) - S[static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(r)])][static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(c)])];
                    inv[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = r == c ? 1.0 : 0.0;
                }
            wellPosed = invert(A, inv, m);
            for (int r = 0; r < m; ++r)
                for (int c = 0; c < m; ++c)
                    M[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = inv[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
            for (int r = 0; r < m; ++r)
                for (int c = 0; c < m; ++c)
                {
                    double ms = 0.0;
                    for (int k = 0; k < m; ++k)
                        ms += M[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] * S[static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(k)])][static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(c)])];
                    const double rc = ports[static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(c)])].resistance;
                    K[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = -(2.0 * ms + (r == c ? 1.0 : 0.0)) * rc;
                }

            // The matrices a sample uses, packed in the order its waves are: linear senders, inputs, devices.
            const int linear = linearSendingCount + inputCount;
            auto entry = [&](auto&& rowOfS, auto&& rowOfQ, int c) -> double
            {
                if (c < linearSendingCount)
                    return rowOfS(sending[static_cast<std::size_t>(c)]);
                if (c < linear)
                    return rowOfQ(c - linearSendingCount);
                return rowOfS(nonlinearPort[static_cast<std::size_t>(c - linear)]);
            };
            for (int r = 0; r < m; ++r)
                for (int c = 0; c < linear; ++c)
                {
                    double sum = 0.0;
                    for (int k = 0; k < m; ++k)
                    {
                        const auto row = static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(k)]);
                        sum += M[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)]
                               * entry([&](int col) { return S[row][static_cast<std::size_t>(col)]; },
                                       [&](int in) { return Q[row][static_cast<std::size_t>(in)]; }, c);
                    }
                    toDevices[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = sum;
                }
            for (int r = 0; r < capacitorCount; ++r)
            {
                const auto row = static_cast<std::size_t>(capacitorPort[static_cast<std::size_t>(r)]);
                for (int c = 0; c < linear + m; ++c)
                    toCapacitors[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]
                        = entry([&](int col) { return S[row][static_cast<std::size_t>(col)]; },
                                [&](int in) { return Q[row][static_cast<std::size_t>(in)]; }, c);
            }
            for (int j = 0; j < probeCount; ++j)
                for (int c = 0; c < linear + m; ++c)
                    toProbes[static_cast<std::size_t>(j)][static_cast<std::size_t>(c)]
                        = entry([&](int col) { return W[static_cast<std::size_t>(j)][static_cast<std::size_t>(col)]; },
                                [&](int in) { return W[static_cast<std::size_t>(j)][static_cast<std::size_t>(portCount + in)]; }, c);
            dirty = false;
        }

        /** Gauss-Jordan; false when a pivot vanishes (the nonlinear ports' currents tied by the linear part alone). */
        static bool invert(std::array<std::array<double, MaxNonlinear>, MaxNonlinear>& a,
                           std::array<std::array<double, MaxNonlinear>, MaxNonlinear>& out, int m) noexcept
        {
            bool regular = true;
            for (int col = 0; col < m; ++col)
            {
                int pivot = col;
                for (int r = col + 1; r < m; ++r)
                    if (std::abs(a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) > std::abs(a[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)]))
                        pivot = r;
                std::swap(a[static_cast<std::size_t>(pivot)], a[static_cast<std::size_t>(col)]);
                std::swap(out[static_cast<std::size_t>(pivot)], out[static_cast<std::size_t>(col)]);
                const double d = a[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
                if (std::abs(d) < 1.0e-9) // its entries are of order one: a pivot this small is a singular matrix
                    regular = false;
                if (std::abs(d) < 1.0e-300)
                    continue;
                for (int c = 0; c < m; ++c)
                {
                    a[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)] /= d;
                    out[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)] /= d;
                }
                for (int r = 0; r < m; ++r)
                {
                    if (r == col)
                        continue;
                    const double f = a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];
                    if (f == 0.0)
                        continue;
                    for (int c = 0; c < m; ++c)
                    {
                        a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -= f * a[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                        out[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -= f * out[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                    }
                }
            }
            return regular;
        }

        // ---- One sample ----------------------------------------------------------------------------------------

        void step(int iterationLimit) noexcept
        {
            if (dirty)
                analyse();

            // Plain pointers from here: a sample's loops are short and many, and a checked index costs more than the
            // arithmetic in a Debug build (the one the voice cap is measured in).
            Port* port = ports.data();
            const int* send = sending.data();
            double* x = packed.data();

            // The waves into the root, packed: the linear senders (capacitors and sources: a resistor sends none),
            // the inputs, then the devices (below).
            for (int j = 0; j < linearSendingCount; ++j)
            {
                Port& p = port[send[j]];
                if (p.kind == Kind::capacitor)
                {
                    if (openCapacitors)
                        p.toRoot = 0.0;
                    else
                    {
                        p.alphaState = p.keep * p.alphaState + p.take * p.fromRoot;
                        p.toRoot = p.alphaState;
                    }
                }
                else
                    p.toRoot = p.volts;
                x[j] = p.toRoot;
            }
            const double* in = inputs.data();
            for (int j = 0; j < inputCount; ++j)
                x[linearSendingCount + j] = in[j];
            const int linear = linearSendingCount + inputCount;

            const int m = nonlinearCount;
            if (m > 0)
            {
                // p = M (S a + Q u) over the linear part, for the devices' ports (folded into one matrix).
                double p[maxDevicePorts];
                for (int r = 0; r < m; ++r)
                {
                    const double* row = toDevices[static_cast<std::size_t>(r)].data();
                    double sum = 0.0;
                    for (int c = 0; c < linear; ++c)
                        sum += row[c] * x[c];
                    p[r] = sum;
                }
                solveDevices(p, iterationLimit);
                // The waves the devices send back: a = v - R i.
                const int* devicePort = nonlinearPort.data();
                const double* v = voltage.data();
                const double* i = current.data();
                for (int r = 0; r < m; ++r)
                {
                    Port& d = port[devicePort[r]];
                    d.toRoot = v[r] - d.resistance * i[r];
                    x[linear + r] = d.toRoot;
                }
            }

            // Scatter only into the ports that keep a wave (the capacitors: resistors and sources absorb what comes
            // back), and read the probes, both from the packed waves.
            const int columns = linear + m;
            const int* keeper = capacitorPort.data();
            for (int r = 0; r < capacitorCount; ++r)
            {
                const double* row = toCapacitors[static_cast<std::size_t>(r)].data();
                double sum = 0.0;
                for (int c = 0; c < columns; ++c)
                    sum += row[c] * x[c];
                port[keeper[r]].fromRoot = sum;
            }
            double* probe = probes.data();
            for (int j = 0; j < probeCount; ++j)
            {
                const double* row = toProbes[static_cast<std::size_t>(j)].data();
                double sum = 0.0;
                for (int c = 0; c < columns; ++c)
                    sum += row[c] * x[c];
                probe[j] = sum;
            }
        }

        /**
            The devices' currents at their port voltages v, and their conductances: g[k][c] = di_k / dv_c, written only
            inside each device's own block (a diode's one port, a transistor's two), where the solver reads it.
        */
        void deviceCurrents(const double* v, double* i, double (*g)[maxDevicePorts]) const noexcept
        {
            const Device* device = devices.data();
            for (int d = 0; d < deviceCount; ++d)
            {
                const Device& dev = device[d];
                const int s0 = dev.slot[0];
                if (dev.isPair)
                {
                    // Two diodes back to back: Is (e^(v / VT) - e^(-v / VT)), each past its offset (a clamp's).
                    const double up = std::exp(std::min((v[s0] - dev.offset) * dev.inverseThermal, 80.0));
                    const double down = std::exp(std::min((-v[s0] - dev.offset) * dev.inverseThermal, 80.0));
                    i[s0] = dev.saturation * (up - down);
                    g[s0][s0] = dev.saturation * (up + down) * dev.inverseThermal;
                    continue;
                }
                if (! dev.isTransistor)
                {
                    const double e = std::exp(std::min(v[s0] * dev.inverseThermal, 80.0));
                    i[s0] = dev.saturation * (e - 1.0);
                    g[s0][s0] = dev.saturation * e * dev.inverseThermal;
                    continue;
                }
                const int s1 = dev.slot[1];
                const double sign = dev.polarity; // a PNP's: i = -f(-v), so di/dv = f'(-v)
                const double vbe = sign * v[s0], vce = sign * v[s1], vbc = vbe - vce;
                const double ef = std::exp(std::min(vbe * dev.inverseThermal, 80.0));
                const double er = std::exp(std::min(vbc * dev.inverseThermal, 80.0));
                const double iF = dev.saturation * (ef - 1.0), iR = dev.saturation * (er - 1.0);
                const double gF = dev.saturation * ef * dev.inverseThermal, gR = dev.saturation * er * dev.inverseThermal;
                // Base current iF / bF + iR / bR, collector current iF - iR (1 + 1 / bR).
                i[s0] = sign * (iF * dev.inverseBetaF + iR * dev.inverseBetaR);
                i[s1] = sign * (iF - iR * dev.reverseFactor);
                // d/dvbe and d/dvce, with dvbc/dvbe = 1 and dvbc/dvce = -1.
                g[s0][s0] = gF * dev.inverseBetaF + gR * dev.inverseBetaR;
                g[s0][s1] = -gR * dev.inverseBetaR;
                g[s1][s0] = gF - gR * dev.reverseFactor;
                g[s1][s1] = gR * dev.reverseFactor;
            }
        }

        /** SPICE's pnjlim: a junction's new voltage kept from leaping up the exponential. */
        static double limitJunction(double vNew, double vOld, const Device& dev) noexcept
        {
            if (vNew > dev.critical && std::abs(vNew - vOld) > 2.0 * dev.thermal)
            {
                if (vOld > 0.0)
                {
                    const double arg = 1.0 + (vNew - vOld) * dev.inverseThermal;
                    return arg > 0.0 ? vOld + dev.thermal * std::log(arg) : dev.critical;
                }
                return dev.thermal * std::log(vNew * dev.inverseThermal);
            }
            return vNew;
        }

        /** A diode's or a diode pair's port voltage limited as a junction's (the pair's in whichever way it conducts). */
        static double limitPort(double vNew, double vOld, const Device& dev) noexcept
        {
            if (! dev.isPair)
                return limitJunction(vNew, vOld, dev);
            const double o = dev.offset; // a clamp's junctions sit past their offset
            return vNew >= 0.0 ? o + limitJunction(vNew - o, vOld - o, dev) : -(o + limitJunction(-vNew - o, -vOld - o, dev));
        }

        /** Newton's method on v = p + K f(v), from the last sample's voltages. */
        void solveDevices(const double* p, int limit) noexcept
        {
            const int m = nonlinearCount;
            const Device* device = devices.data();
            double* v = voltage.data();
            double f[maxDevicePorts], residual[maxDevicePorts], delta[maxDevicePorts];
            double g[maxDevicePorts][maxDevicePorts];
            double jac[maxDevicePorts][maxDevicePorts];
            const double* k[maxDevicePorts];
            for (int r = 0; r < m; ++r)
                k[r] = K[static_cast<std::size_t>(r)].data();

            // The start. While every junction is below its knee (much of the drum's tail), one fixed-point step,
            // v = p + K i with the last sample's currents: then exact, so the check below usually ends the solve at
            // once. With a junction conducting such a step can overshoot, so after a sample that solved in a few steps
            // (three at most), the last three samples' voltages carried on in a parabola, each junction limited as a Newton
            // step is: close enough that one step usually ends it. Where the circuit turns sharply (a trigger's edge)
            // a parabola overshoots too: there the last voltages.
            double* before = previousVoltage.data();
            double* earlier = earlierVoltage.data();
            double last[maxDevicePorts];
            for (int r = 0; r < m; ++r)
                last[r] = v[r];
            bool allOff = true;
            for (int d = 0; d < deviceCount && allOff; ++d)
            {
                const Device& dev = device[d];
                const double v0 = dev.isPair ? std::abs(v[dev.slot[0]]) - dev.offset : dev.polarity * v[dev.slot[0]];
                allOff = v0 < 0.4 && (! dev.isTransistor || v0 - dev.polarity * v[dev.slot[1]] < 0.4);
            }
            bool guessed = false;
            if (allOff && ! openCapacitors)
            {
                const double* i = current.data();
                double guess[maxDevicePorts];
                for (int r = 0; r < m; ++r)
                {
                    double kf = 0.0;
                    for (int c = 0; c < m; ++c)
                        kf += k[r][c] * i[c];
                    guess[r] = p[r] + kf;
                }
                // Used only if it stays below the knees too.
                bool stillOff = true;
                for (int d = 0; d < deviceCount && stillOff; ++d)
                {
                    const Device& dev = device[d];
                    const double g0 = dev.isPair ? std::abs(guess[dev.slot[0]]) - dev.offset : dev.polarity * guess[dev.slot[0]];
                    stillOff = g0 < 0.4 && (! dev.isTransistor || g0 - dev.polarity * guess[dev.slot[1]] < 0.4);
                }
                if (stillOff)
                {
                    for (int r = 0; r < m; ++r)
                        v[r] = guess[r];
                    guessed = true;
                }
            }
            if (! guessed && ! openCapacitors && newtonSteps <= 3)
                for (int d = 0; d < deviceCount; ++d)
                {
                    const Device& dev = device[d];
                    const int s0 = dev.slot[0];
                    const double vbe = limitPort(3.0 * (last[s0] - before[s0]) + earlier[s0], last[s0], dev);
                    if (! dev.isTransistor)
                    {
                        v[s0] = vbe;
                        continue;
                    }
                    // A transistor's junctions in its own sense (a PNP's turned round).
                    const int s1 = dev.slot[1];
                    const double sign = dev.polarity;
                    const double l0 = sign * last[s0], b0 = sign * before[s0], e0 = sign * earlier[s0];
                    const double ube = limitJunction(3.0 * (l0 - b0) + e0, l0, dev);
                    const double vbcLast = l0 - sign * last[s1], vbcBefore = b0 - sign * before[s1], vbcEarlier = e0 - sign * earlier[s1];
                    const double ubc = limitJunction(3.0 * (vbcLast - vbcBefore) + vbcEarlier, vbcLast, dev);
                    v[s0] = sign * ube;
                    v[s1] = sign * (ube - ubc);
                }
            for (int r = 0; r < m; ++r)
            {
                earlier[r] = before[r];
                before[r] = last[r];
            }

            // Newton from the voltages as they are toward a set of the linear part's inputs: true once converged.
            auto iterate = [&](const double* target) -> bool
            {
                for (int it = 0; it < limit; ++it)
                {
                    deviceCurrents(v, f, g);
                    double worst = 0.0;
                    for (int r = 0; r < m; ++r)
                    {
                        double kf = 0.0;
                        for (int c = 0; c < m; ++c)
                            kf += k[r][c] * f[c];
                        residual[r] = v[r] - target[r] - kf;
                        worst = std::max(worst, std::abs(residual[r]));
                    }
                    if (worst < 1.0e-7) // 100 nV: some -150 dB under the drum's volts
                    {
                        return true; // f is the current at these voltages
                    }
                    // The Jacobian I - K G, G block diagonal by device: each column has a device's one or two terms.
                    for (int d = 0; d < deviceCount; ++d)
                    {
                        const Device& dev = device[d];
                        const int s0 = dev.slot[0];
                        if (! dev.isTransistor)
                        {
                            const double g00 = g[s0][s0];
                            for (int r = 0; r < m; ++r)
                                jac[r][s0] = (r == s0 ? 1.0 : 0.0) - k[r][s0] * g00;
                            continue;
                        }
                        const int s1 = dev.slot[1];
                        const double g00 = g[s0][s0], g01 = g[s0][s1], g10 = g[s1][s0], g11 = g[s1][s1];
                        for (int r = 0; r < m; ++r)
                        {
                            const double k0 = k[r][s0], k1 = k[r][s1];
                            jac[r][s0] = (r == s0 ? 1.0 : 0.0) - (k0 * g00 + k1 * g10);
                            jac[r][s1] = (r == s1 ? 1.0 : 0.0) - (k0 * g01 + k1 * g11);
                        }
                    }
                    // delta = jac^-1 residual, by elimination with partial pivoting (rows swapped as pointers).
                    double* row[maxDevicePorts];
                    for (int r = 0; r < m; ++r)
                    {
                        row[r] = jac[r];
                        delta[r] = residual[r];
                    }
                    for (int col = 0; col < m; ++col)
                    {
                        int pivot = col;
                        for (int r = col + 1; r < m; ++r)
                            if (std::abs(row[r][col]) > std::abs(row[pivot][col]))
                                pivot = r;
                        std::swap(row[pivot], row[col]);
                        std::swap(delta[pivot], delta[col]);
                        const double d = row[col][col];
                        if (std::abs(d) < 1.0e-300)
                            continue;
                        for (int r = col + 1; r < m; ++r)
                        {
                            const double fct = row[r][col] / d;
                            for (int c = col; c < m; ++c)
                                row[r][c] -= fct * row[col][c];
                            delta[r] -= fct * delta[col];
                        }
                    }
                    for (int col = m - 1; col >= 0; --col)
                    {
                        double s = delta[col];
                        for (int c = col + 1; c < m; ++c)
                            s -= row[col][c] * delta[c];
                        const double d = row[col][col];
                        delta[col] = std::abs(d) < 1.0e-300 ? 0.0 : s / d;
                    }
                    // A step this small leaves the answer within some 10 nV (Newton's error goes as the step squared
                    // over twice VT): take it, the currents carried along their slopes, without evaluating the junctions
                    // again, as SPICE's step tolerance ends its iterations.
                    double largest = 0.0;
                    for (int r = 0; r < m; ++r)
                        largest = std::max(largest, std::abs(delta[r]));
                    if (largest < 2.0e-5)
                    {
                        for (int r = 0; r < m; ++r)
                            v[r] -= delta[r];
                        for (int d = 0; d < deviceCount; ++d)
                        {
                            const Device& dev = device[d];
                            const int s0 = dev.slot[0];
                            if (! dev.isTransistor)
                            {
                                f[s0] -= g[s0][s0] * delta[s0];
                                continue;
                            }
                            const int s1 = dev.slot[1];
                            const double d0 = delta[s0], d1 = delta[s1];
                            f[s0] -= g[s0][s0] * d0 + g[s0][s1] * d1;
                            f[s1] -= g[s1][s0] * d0 + g[s1][s1] * d1;
                        }
                        ++newtonSteps;
                        return true;
                    }
                    // The step, its junctions limited as SPICE limits them.
                    for (int d = 0; d < deviceCount; ++d)
                    {
                        const Device& dev = device[d];
                        const int s0 = dev.slot[0];
                        if (! dev.isTransistor)
                        {
                            v[s0] = limitPort(v[s0] - delta[s0], v[s0], dev);
                            continue;
                        }
                        const int s1 = dev.slot[1];
                        const double sign = dev.polarity;
                        const double vbeOld = sign * v[s0], vbcOld = sign * (v[s0] - v[s1]);
                        const double vbeNew = sign * (v[s0] - delta[s0]), vceNew = sign * (v[s1] - delta[s1]);
                        const double vbe = limitJunction(vbeNew, vbeOld, dev);
                        const double vbc = limitJunction(vbeNew - vceNew, vbcOld, dev);
                        v[s0] = sign * vbe;
                        v[s1] = sign * (vbe - vbc);
                    }
                    ++newtonSteps;
                }
                return false;
            };

            newtonSteps = 0;
            bool converged = iterate(p);
            if (! converged)
            {
                // Source stepping, as SPICE finds a hard operating point: the linear part's pull on the junctions
                // walked from the last sample's (v = p + K i there, so p = v - K i) to this one's in stages, each
                // solved from the one before. Only a sample whose solve failed pays for it (a hard edge).
                double lastP[maxDevicePorts], staged[maxDevicePorts];
                const double* i = current.data();
                for (int r = 0; r < m; ++r)
                {
                    double ki = 0.0;
                    for (int c = 0; c < m; ++c)
                        ki += k[r][c] * i[c];
                    lastP[r] = last[r] - ki;
                    v[r] = last[r];
                }
                constexpr int stages = 16;
                for (int s = 1; s <= stages; ++s)
                {
                    const double w = static_cast<double>(s) / stages;
                    for (int r = 0; r < m; ++r)
                        staged[r] = lastP[r] + w * (p[r] - lastP[r]);
                    converged = iterate(staged);
                }
            }
            solved = converged;
            double* i = current.data();
            if (converged)
                for (int r = 0; r < m; ++r)
                    i[r] = f[r];
            else
                deviceCurrents(v, i, g);
        }

        int nodeCount() const noexcept
        {
            int highest = 0;
            for (int k = 0; k < portCount; ++k)
                highest = std::max({ highest, ports[static_cast<std::size_t>(k)].n1, ports[static_cast<std::size_t>(k)].n2 });
            for (int j = 0; j < inputCount; ++j)
                highest = std::max(highest, inputNode[static_cast<std::size_t>(j)]);
            for (int j = 0; j < nullorCount; ++j)
                highest = std::max({ highest, nullors[static_cast<std::size_t>(j)].plus, nullors[static_cast<std::size_t>(j)].minus,
                                     nullors[static_cast<std::size_t>(j)].out });
            return highest;
        }

        std::array<Port, MaxPorts> ports {};
        std::array<Nullor, MaxNullors> nullors {};
        std::array<Gain, MaxNullors> gains {}; // transconductances, one at most an op-amp
        std::array<Device, (MaxNonlinear > 0 ? MaxNonlinear : 1)> devices {};
        std::array<int, MaxInputs> inputNode {};
        std::array<double, MaxInputs> inputs {};
        std::array<int, MaxNonlinear> nonlinearPort {};
        std::array<int, MaxProbes> probeNode {};
        std::array<double, MaxProbes> probes {};
        std::array<double, MaxNonlinear> voltage {}, current {};
        std::array<double, MaxNonlinear> previousVoltage {}, earlierVoltage {}; // two samples back and three, for the solver's start
        std::array<double, maxColumns> packed {};
        std::array<std::array<double, maxColumns>, (MaxNonlinear > 0 ? MaxNonlinear : 1)> toDevices {};
        std::array<std::array<double, maxColumns>, MaxPorts> toCapacitors {};
        std::array<std::array<double, maxColumns>, MaxProbes> toProbes {};
        std::array<int, MaxPorts> sending {}, capacitorPort {};
        int sendingCount = 0, linearSendingCount = 0, capacitorCount = 0;

        std::array<std::array<double, MaxNonlinear>, MaxNonlinear> K {};

        int portCount = 0, inputCount = 0, nullorCount = 0, gainCount = 0, deviceCount = 0, nonlinearCount = 0, probeCount = 0;
        int newtonSteps = 0, newtonLimit = maxNewtonSteps;
        bool solved = true;
        bool wellPosed = true;
        double sampleRate = 96000.0;
        bool dirty = true, openCapacitors = false;
    };
}
