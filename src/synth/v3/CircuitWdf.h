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
    in their port voltages (Werner, Nangia, Smith, Abel, DAFx 2015; Olsen, Werner, Smith, DAFx 2016), warm-started
    from the last sample, with SPICE's junction limiting. Diodes and transistors follow the thesis's models
    (Shockley; Ebers-Moll in transport form). Double precision; all storage inline, no heap.
*/
namespace coolsynth::synth::v3::wdf
{
    /** Limits of one circuit: nodes besides ground, ports, ideal inputs, op-amps, nonlinear ports, probes. */
    template <int MaxNodes, int MaxPorts, int MaxInputs, int MaxNullors, int MaxNonlinear, int MaxProbes>
    class Circuit final
    {
    public:
        static constexpr int maxUnknowns = MaxNodes + MaxInputs + MaxNullors;
        static constexpr int maxColumns = MaxPorts + MaxInputs;

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

        /** A junction diode from anode to cathode (Shockley), its port seen through portOhms. */
        void addDiode(int anode, int cathode, double saturation, double thermal, double portOhms = 1000.0)
        {
            Device& d = devices[static_cast<std::size_t>(deviceCount++)];
            d.isTransistor = false;
            d.saturation = saturation;
            d.thermal = thermal;
            d.port[0] = addPort(anode, cathode, Kind::nonlinear, portOhms, 0.0, 1.0);
            d.slot[0] = nonlinearCount++;
            nonlinearPort[static_cast<std::size_t>(d.slot[0])] = d.port[0];
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
            d.port[0] = addPort(base, emitter, Kind::nonlinear, 2000.0, 0.0, 1.0);
            d.port[1] = addPort(collector, emitter, Kind::nonlinear, 20000.0, 0.0, 1.0);
            d.slot[0] = nonlinearCount++;
            d.slot[1] = nonlinearCount++;
            nonlinearPort[static_cast<std::size_t>(d.slot[0])] = d.port[0];
            nonlinearPort[static_cast<std::size_t>(d.slot[1])] = d.port[1];
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

        /** One sample. */
        void process() noexcept { step(maxNewtonSteps); }

        [[nodiscard]] double probe(int index) const noexcept { return probes[static_cast<std::size_t>(index)]; }
        [[nodiscard]] double portVoltage(int port) const noexcept
        {
            const auto& p = ports[static_cast<std::size_t>(port)];
            return 0.5 * (p.toRoot + p.fromRoot);
        }
        [[nodiscard]] int lastNewtonSteps() const noexcept { return newtonSteps; }

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

        struct Device
        {
            bool isTransistor = false;
            double saturation = 1.0e-14, betaF = 100.0, betaR = 1.0, thermal = 0.02585;
            std::array<int, 2> port {}, slot {};
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

        void analyse() noexcept
        {
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
            invert(A, inv, m);
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

        static void invert(std::array<std::array<double, MaxNonlinear>, MaxNonlinear>& a,
                           std::array<std::array<double, MaxNonlinear>, MaxNonlinear>& out, int m) noexcept
        {
            for (int col = 0; col < m; ++col)
            {
                int pivot = col;
                for (int r = col + 1; r < m; ++r)
                    if (std::abs(a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) > std::abs(a[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)]))
                        pivot = r;
                std::swap(a[static_cast<std::size_t>(pivot)], a[static_cast<std::size_t>(col)]);
                std::swap(out[static_cast<std::size_t>(pivot)], out[static_cast<std::size_t>(col)]);
                const double d = a[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
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
        }

        // ---- One sample ----------------------------------------------------------------------------------------

        void step(int newtonLimit) noexcept
        {
            if (dirty)
                analyse();

            // The waves into the root from every linear element.
            for (int k = 0; k < portCount; ++k)
            {
                auto& p = ports[static_cast<std::size_t>(k)];
                switch (p.kind)
                {
                    case Kind::resistor: p.toRoot = 0.0; break;
                    case Kind::source: p.toRoot = p.volts; break;
                    case Kind::capacitor:
                        if (openCapacitors)
                            p.toRoot = 0.0;
                        else
                        {
                            const double bCoef = 0.5 * (1.0 - p.alpha), aCoef = 0.5 * (1.0 + p.alpha);
                            p.alphaState = bCoef * p.alphaState + aCoef * p.fromRoot;
                            p.toRoot = p.alphaState;
                        }
                        break;
                    case Kind::nonlinear: break;
                }
            }

            // The waves into the root, packed: the linear senders, the inputs, then the devices (below).
            const int linear = linearSendingCount + inputCount;
            double* x = packed.data();
            for (int j = 0; j < linearSendingCount; ++j)
                x[j] = ports[static_cast<std::size_t>(sending[static_cast<std::size_t>(j)])].toRoot;
            for (int j = 0; j < inputCount; ++j)
                x[linearSendingCount + j] = inputs[static_cast<std::size_t>(j)];

            const int m = nonlinearCount;
            if (m > 0)
            {
                // p = M (S a + Q u) over the linear part, for the devices' ports (folded into one matrix).
                std::array<double, MaxNonlinear> p {};
                for (int r = 0; r < m; ++r)
                {
                    const double* row = toDevices[static_cast<std::size_t>(r)].data();
                    double sum = 0.0;
                    for (int c = 0; c < linear; ++c)
                        sum += row[c] * x[c];
                    p[static_cast<std::size_t>(r)] = sum;
                }
                solveDevices(p, newtonLimit);
                // The waves the devices send back: a = v - R i.
                for (int r = 0; r < m; ++r)
                {
                    auto& port = ports[static_cast<std::size_t>(nonlinearPort[static_cast<std::size_t>(r)])];
                    port.toRoot = voltage[static_cast<std::size_t>(r)] - port.resistance * current[static_cast<std::size_t>(r)];
                    x[linear + r] = port.toRoot;
                }
            }

            // Scatter only into the ports that keep a wave (the capacitors: resistors and sources absorb what comes
            // back), and read the probes, both from the packed waves.
            const int columns = linear + m;
            for (int r = 0; r < capacitorCount; ++r)
            {
                const double* row = toCapacitors[static_cast<std::size_t>(r)].data();
                double sum = 0.0;
                for (int c = 0; c < columns; ++c)
                    sum += row[c] * x[c];
                ports[static_cast<std::size_t>(capacitorPort[static_cast<std::size_t>(r)])].fromRoot = sum;
            }
            for (int j = 0; j < probeCount; ++j)
            {
                const double* row = toProbes[static_cast<std::size_t>(j)].data();
                double sum = 0.0;
                for (int c = 0; c < columns; ++c)
                    sum += row[c] * x[c];
                probes[static_cast<std::size_t>(j)] = sum;
            }
        }

        /** The devices' currents for their port voltages, and the Jacobian. */
        void deviceCurrents(const std::array<double, MaxNonlinear>& v, std::array<double, MaxNonlinear>& i,
                            std::array<std::array<double, MaxNonlinear>, MaxNonlinear>& jacobian) const noexcept
        {
            for (int d = 0; d < deviceCount; ++d)
            {
                const auto& dev = devices[static_cast<std::size_t>(d)];
                const auto s0 = static_cast<std::size_t>(dev.slot[0]);
                if (! dev.isTransistor)
                {
                    const double e = std::exp(std::min(v[s0] / dev.thermal, 80.0));
                    i[s0] = dev.saturation * (e - 1.0);
                    jacobian[s0][s0] = dev.saturation * e / dev.thermal;
                    continue;
                }
                const auto s1 = static_cast<std::size_t>(dev.slot[1]);
                const double vbe = v[s0], vce = v[s1], vbc = vbe - vce;
                const double ef = std::exp(std::min(vbe / dev.thermal, 80.0));
                const double er = std::exp(std::min(vbc / dev.thermal, 80.0));
                const double iF = dev.saturation * (ef - 1.0), iR = dev.saturation * (er - 1.0);
                const double gF = dev.saturation * ef / dev.thermal, gR = dev.saturation * er / dev.thermal;
                // Base current iF / bF + iR / bR, collector current iF - iR (1 + 1 / bR).
                i[s0] = iF / dev.betaF + iR / dev.betaR;
                i[s1] = iF - iR * (1.0 + 1.0 / dev.betaR);
                // d/dvbe and d/dvce, with dvbc/dvbe = 1 and dvbc/dvce = -1.
                jacobian[s0][s0] = gF / dev.betaF + gR / dev.betaR;
                jacobian[s0][s1] = -gR / dev.betaR;
                jacobian[s1][s0] = gF - gR * (1.0 + 1.0 / dev.betaR);
                jacobian[s1][s1] = gR * (1.0 + 1.0 / dev.betaR);
            }
        }

        /** SPICE's pnjlim: a junction's new voltage kept from leaping up the exponential. */
        static double limitJunction(double vNew, double vOld, double thermal, double saturation) noexcept
        {
            const double vCritical = thermal * std::log(thermal / (1.4142135623730951 * saturation));
            if (vNew > vCritical && std::abs(vNew - vOld) > 2.0 * thermal)
            {
                if (vOld > 0.0)
                {
                    const double arg = 1.0 + (vNew - vOld) / thermal;
                    return arg > 0.0 ? vOld + thermal * std::log(arg) : vCritical;
                }
                return thermal * std::log(vNew / thermal);
            }
            return vNew;
        }

        /** Newton's method on v = p + K f(v), from the last sample's voltages. */
        void solveDevices(const std::array<double, MaxNonlinear>& p, int limit) noexcept
        {
            const int m = nonlinearCount;
            std::array<double, MaxNonlinear> f {}, residual {}, delta {};
            std::array<std::array<double, MaxNonlinear>, MaxNonlinear> df {}, jac {};
            // While every junction is below its knee (the drum's tail), start from one fixed-point step, v = p + K i
            // with the last sample's currents: then exact, so the check below usually ends the solve at once. With a
            // junction conducting, such a step can overshoot, so the last voltages are the start.
            bool allOff = true;
            for (int d = 0; d < deviceCount && allOff; ++d)
            {
                const auto& dev = devices[static_cast<std::size_t>(d)];
                const double v0 = voltage[static_cast<std::size_t>(dev.slot[0])];
                allOff = v0 < 0.4 && (! dev.isTransistor || v0 - voltage[static_cast<std::size_t>(dev.slot[1])] < 0.4);
            }
            if (allOff && ! openCapacitors)
            {
                std::array<double, MaxNonlinear> guess {};
                for (int r = 0; r < m; ++r)
                {
                    double kf = 0.0;
                    for (int c = 0; c < m; ++c)
                        kf += K[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] * current[static_cast<std::size_t>(c)];
                    guess[static_cast<std::size_t>(r)] = p[static_cast<std::size_t>(r)] + kf;
                }
                // Used only if it stays below the knees too.
                bool stillOff = true;
                for (int d = 0; d < deviceCount && stillOff; ++d)
                {
                    const auto& dev = devices[static_cast<std::size_t>(d)];
                    const double g0 = guess[static_cast<std::size_t>(dev.slot[0])];
                    stillOff = g0 < 0.4 && (! dev.isTransistor || g0 - guess[static_cast<std::size_t>(dev.slot[1])] < 0.4);
                }
                if (stillOff)
                    voltage = guess;
            }
            newtonSteps = 0;
            bool converged = false;
            for (int it = 0; it < limit; ++it)
            {
                deviceCurrents(voltage, f, df);
                double worst = 0.0;
                for (int r = 0; r < m; ++r)
                {
                    double kf = 0.0;
                    for (int c = 0; c < m; ++c)
                        kf += K[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] * f[static_cast<std::size_t>(c)];
                    residual[static_cast<std::size_t>(r)] = voltage[static_cast<std::size_t>(r)] - p[static_cast<std::size_t>(r)] - kf;
                    worst = std::max(worst, std::abs(residual[static_cast<std::size_t>(r)]));
                }
                if (worst < 1.0e-8) // 10 nV
                {
                    converged = true; // f is the current at these voltages
                    break;
                }
                for (int r = 0; r < m; ++r)
                    for (int c = 0; c < m; ++c)
                    {
                        double kd = 0.0;
                        for (int k = 0; k < m; ++k)
                            kd += K[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] * df[static_cast<std::size_t>(k)][static_cast<std::size_t>(c)];
                        jac[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = (r == c ? 1.0 : 0.0) - kd;
                    }
                // delta = jac^-1 residual, by elimination.
                for (int r = 0; r < m; ++r)
                    delta[static_cast<std::size_t>(r)] = residual[static_cast<std::size_t>(r)];
                for (int col = 0; col < m; ++col)
                {
                    int pivot = col;
                    for (int r = col + 1; r < m; ++r)
                        if (std::abs(jac[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) > std::abs(jac[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)]))
                            pivot = r;
                    std::swap(jac[static_cast<std::size_t>(pivot)], jac[static_cast<std::size_t>(col)]);
                    std::swap(delta[static_cast<std::size_t>(pivot)], delta[static_cast<std::size_t>(col)]);
                    const double d = jac[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
                    if (std::abs(d) < 1.0e-300)
                        continue;
                    for (int r = col + 1; r < m; ++r)
                    {
                        const double fct = jac[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)] / d;
                        for (int c = col; c < m; ++c)
                            jac[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -= fct * jac[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
                        delta[static_cast<std::size_t>(r)] -= fct * delta[static_cast<std::size_t>(col)];
                    }
                }
                for (int col = m - 1; col >= 0; --col)
                {
                    double v = delta[static_cast<std::size_t>(col)];
                    for (int k = col + 1; k < m; ++k)
                        v -= jac[static_cast<std::size_t>(col)][static_cast<std::size_t>(k)] * delta[static_cast<std::size_t>(k)];
                    const double d = jac[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
                    delta[static_cast<std::size_t>(col)] = std::abs(d) < 1.0e-300 ? 0.0 : v / d;
                }
                // The step, its junctions limited as SPICE limits them.
                for (int d = 0; d < deviceCount; ++d)
                {
                    const auto& dev = devices[static_cast<std::size_t>(d)];
                    const auto s0 = static_cast<std::size_t>(dev.slot[0]);
                    if (! dev.isTransistor)
                    {
                        voltage[s0] = limitJunction(voltage[s0] - delta[s0], voltage[s0], dev.thermal, dev.saturation);
                        continue;
                    }
                    const auto s1 = static_cast<std::size_t>(dev.slot[1]);
                    const double vbeOld = voltage[s0], vbcOld = voltage[s0] - voltage[s1];
                    const double vbe = limitJunction(vbeOld - delta[s0], vbeOld, dev.thermal, dev.saturation);
                    const double vbc = limitJunction((vbeOld - delta[s0]) - (voltage[s1] - delta[s1]), vbcOld, dev.thermal, dev.saturation);
                    voltage[s0] = vbe;
                    voltage[s1] = vbe - vbc;
                }
                ++newtonSteps;
            }
            if (converged)
                current = f;
            else
                deviceCurrents(voltage, current, df);
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
        std::array<Device, (MaxNonlinear > 0 ? MaxNonlinear : 1)> devices {};
        std::array<int, MaxInputs> inputNode {};
        std::array<double, MaxInputs> inputs {};
        std::array<int, MaxNonlinear> nonlinearPort {};
        std::array<int, MaxProbes> probeNode {};
        std::array<double, MaxProbes> probes {};
        std::array<double, MaxNonlinear> voltage {}, current {};
        std::array<double, maxColumns> packed {};
        std::array<std::array<double, maxColumns>, (MaxNonlinear > 0 ? MaxNonlinear : 1)> toDevices {};
        std::array<std::array<double, maxColumns>, MaxPorts> toCapacitors {};
        std::array<std::array<double, maxColumns>, MaxProbes> toProbes {};
        std::array<int, MaxPorts> sending {}, capacitorPort {};
        int sendingCount = 0, linearSendingCount = 0, capacitorCount = 0;

        std::array<std::array<double, maxUnknowns>, maxUnknowns> Y {};
        std::array<std::array<double, maxColumns>, maxUnknowns> X {};
        std::array<std::array<double, MaxPorts>, MaxPorts> S {};
        std::array<std::array<double, MaxInputs>, MaxPorts> Q {};
        std::array<std::array<double, maxColumns>, MaxProbes> W {};
        std::array<std::array<double, MaxNonlinear>, MaxNonlinear> M {}, K {};

        int portCount = 0, inputCount = 0, nullorCount = 0, deviceCount = 0, nonlinearCount = 0, probeCount = 0;
        int newtonSteps = 0;
        double sampleRate = 96000.0;
        bool dirty = true, openCapacitors = false;
    };
}
