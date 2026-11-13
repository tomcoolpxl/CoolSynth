#include "Samples.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace coolsynth::synth::v3
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double twoPi = 2.0 * pi;
        constexpr double rate = SampleBank::sourceRate;
        constexpr int length = SampleBank::length;
        constexpr int foldLength = 24000; // the half second after the end that is folded into the start
        constexpr int rawLength = length + foldLength;
        constexpr double c4 = SampleBank::rootHz;

        /** Deterministic noise in -1..1: the bank is the same on every run. */
        struct Random
        {
            std::uint32_t state;
            explicit Random(std::uint32_t seed) : state(seed != 0 ? seed : 1u) {}

            double next() noexcept
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                return static_cast<double>(state >> 8) * (2.0 / 16777216.0) - 1.0;
            }

            double unit() noexcept { return 0.5 * (next() + 1.0); }
        };

        /** One second-order section, coefficients after the Audio EQ Cookbook. */
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

            void set(double n0, double n1, double n2, double d0, double d1, double d2) noexcept
            {
                b0 = n0 / d0;
                b1 = n1 / d0;
                b2 = n2 / d0;
                a1 = d1 / d0;
                a2 = d2 / d0;
            }

            void lowPass(double hz, double q) noexcept
            {
                const double w = twoPi * hz / rate, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
                set(0.5 * (1.0 - c), 1.0 - c, 0.5 * (1.0 - c), 1.0 + alpha, -2.0 * c, 1.0 - alpha);
            }

            void highPass(double hz, double q) noexcept
            {
                const double w = twoPi * hz / rate, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
                set(0.5 * (1.0 + c), -(1.0 + c), 0.5 * (1.0 + c), 1.0 + alpha, -2.0 * c, 1.0 - alpha);
            }

            /** Unity gain at the centre. */
            void bandPass(double hz, double q) noexcept
            {
                const double w = twoPi * hz / rate, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
                set(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
            }

            double process(double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        /** One cycle of a saw with a fixed number of harmonics, read by phase. */
        struct SawTable
        {
            static constexpr int size = 4096;
            std::array<float, size + 1> cycle {};

            explicit SawTable(int harmonics)
            {
                for (int i = 0; i < size; ++i)
                {
                    double sum = 0.0;
                    for (int k = 1; k <= harmonics; ++k)
                        sum += std::sin(twoPi * k * i / size) / k;
                    cycle[static_cast<std::size_t>(i)] = static_cast<float>(sum * (2.0 / pi));
                }
                cycle[size] = cycle[0];
            }

            double read(double phase) const noexcept
            {
                const double position = phase * size;
                const int index = static_cast<int>(position);
                const double t = position - index;
                return cycle[static_cast<std::size_t>(index)] * (1.0 - t) + cycle[static_cast<std::size_t>(index + 1)] * t;
            }
        };

        /** The highest harmonic of a note near C4 that stays under 19 kHz, detune included. */
        constexpr int sawHarmonics = 70;

        /** Detuned saws, each wandering a few cents on its own slow sine. */
        struct Ensemble
        {
            static constexpr int maxVoices = 8;
            int voices = 0;
            std::array<double, maxVoices> cents {}, phase {}, wanderPhase {}, wanderHz {}, step {};
            double wanderCents = 0.0;

            Ensemble(std::initializer_list<double> detune, double wanderAmount, double slowestHz, double fastestHz, Random& random)
            {
                for (double c : detune)
                {
                    const auto v = static_cast<std::size_t>(voices++);
                    cents[v] = c;
                    phase[v] = random.unit();
                    wanderPhase[v] = random.unit();
                    wanderHz[v] = slowestHz + (fastestHz - slowestHz) * random.unit();
                }
                wanderCents = wanderAmount;
            }

            double next(const SawTable& saw, int sampleIndex) noexcept
            {
                if ((sampleIndex & 31) == 0) // the pitch moves slowly: every 32 samples is enough
                    for (int v = 0; v < voices; ++v)
                    {
                        const auto i = static_cast<std::size_t>(v);
                        const double wander = std::sin(twoPi * wanderPhase[i]);
                        step[i] = c4 * std::exp2((cents[i] + wanderCents * wander) / 1200.0) / rate;
                        wanderPhase[i] += 32.0 * wanderHz[i] / rate;
                        wanderPhase[i] -= std::floor(wanderPhase[i]);
                    }
                double sum = 0.0;
                for (int v = 0; v < voices; ++v)
                {
                    const auto i = static_cast<std::size_t>(v);
                    sum += saw.read(phase[i]);
                    phase[i] += step[i];
                    if (phase[i] >= 1.0)
                        phase[i] -= 1.0;
                }
                return sum / voices;
            }
        };

        std::vector<double> makeGlass()
        {
            std::vector<double> x(static_cast<std::size_t>(rawLength));
            Random random(0x61A55u);
            Biquad rubLow, rubHigh;
            rubLow.bandPass(c4, 12.0);
            rubHigh.bandPass(2.32 * c4, 10.0);
            double cycles = 0.0;
            for (int i = 0; i < rawLength; ++i)
            {
                const double t = i / rate;
                // A finger circling a rim: the pitch hardly moves, the level beats as the pattern turns.
                const double driftCents = 2.5 * std::sin(twoPi * 0.31 * t + 0.7) + 1.5 * std::sin(twoPi * 0.83 * t + 2.1);
                cycles += c4 * std::exp2(driftCents / 1200.0) / rate;
                const double beat = 1.0 - 0.30 * (0.5 + 0.5 * std::sin(twoPi * 3.7 * t)) - 0.15 * (0.5 + 0.5 * std::sin(twoPi * 0.6 * t + 1.0));
                const double tone = std::sin(twoPi * cycles) + 0.12 * std::sin(2.0 * twoPi * cycles + 0.4)
                                    + 0.05 * std::sin(3.0 * twoPi * cycles + 1.1) + 0.02 * std::sin(4.0 * twoPi * cycles);
                // The glass's own higher modes, which are not harmonics, ring faintly along.
                const double ring = 0.04 * std::sin(2.32 * twoPi * cycles) + 0.02 * std::sin(4.25 * twoPi * cycles + 0.9);
                const double noise = random.next();
                const double friction = 0.06 * rubLow.process(noise) + 0.03 * rubHigh.process(noise);
                x[static_cast<std::size_t>(i)] = beat * (tone + ring) + (0.6 + 0.4 * beat) * friction;
            }
            return x;
        }

        std::vector<double> makeChoir()
        {
            std::vector<double> x(static_cast<std::size_t>(rawLength));
            const SawTable saw(sawHarmonics);
            Random random(0xC4015u);
            Ensemble singers({ -11.0, -6.0, -2.0, 3.0, 7.0, 12.0 }, 6.0, 4.4, 5.6, random); // vibrato
            Biquad first, second, third, body;
            body.lowPass(520.0, 0.7);
            for (int i = 0; i < rawLength; ++i)
            {
                if ((i & 63) == 0)
                {
                    // "ah" to "oo" and back, once over the sample's four seconds.
                    const double m = 0.5 - 0.5 * std::cos(twoPi * i / length);
                    first.bandPass(800.0 + (350.0 - 800.0) * m, 6.0);
                    second.bandPass(1150.0 + (700.0 - 1150.0) * m, 8.0);
                    third.bandPass(2900.0 + (2700.0 - 2900.0) * m, 10.0);
                }
                const double s = singers.next(saw, i);
                x[static_cast<std::size_t>(i)] = first.process(s) + 0.6 * second.process(s) + 0.3 * third.process(s) + 0.5 * body.process(s);
            }
            return x;
        }

        std::vector<double> makeStrings()
        {
            std::vector<double> x(static_cast<std::size_t>(rawLength));
            const SawTable saw(sawHarmonics);
            Random random(0x57216u);
            Ensemble players({ -14.0, -9.0, -4.0, 0.0, 5.0, 10.0, 15.0 }, 4.0, 0.11, 0.37, random);
            Biquad top, bottom;
            top.lowPass(5000.0, 0.7);
            bottom.highPass(120.0, 0.7);
            for (int i = 0; i < rawLength; ++i)
            {
                const double swell = 0.85 + 0.15 * std::sin(twoPi * 0.23 * i / rate + 0.5);
                x[static_cast<std::size_t>(i)] = swell * bottom.process(top.process(players.next(saw, i)));
            }
            return x;
        }

        std::vector<double> makeAir()
        {
            std::vector<double> x(static_cast<std::size_t>(rawLength));
            Random random(0xA1250u);
            Biquad low, high;
            for (int i = 0; i < rawLength; ++i)
            {
                const double t = i / rate;
                if ((i & 31) == 0)
                {
                    low.bandPass(900.0 * std::exp2(0.8 * std::sin(twoPi * 0.19 * t)), 1.2);
                    high.bandPass(3500.0 * std::exp2(0.6 * std::sin(twoPi * 0.31 * t + 2.0)), 2.0);
                }
                const double noise = random.next();
                const double gust = 0.7 + 0.3 * std::sin(twoPi * 0.27 * t + 1.0);
                x[static_cast<std::size_t>(i)] = gust * (low.process(noise) + 0.5 * high.process(noise));
            }
            return x;
        }

        /** One plucked string (Karplus and Strong), ringing from `start` to the end. */
        void pluck(std::vector<double>& x, int start, double hz, double amplitude, Random& random)
        {
            const double delay = rate / hz - 0.5; // the two-point average in the loop is half a sample long
            const int whole = static_cast<int>(delay);
            const double fraction = delay - whole;
            const int size = whole + 2;
            std::vector<double> line(static_cast<std::size_t>(size));
            double smooth = 0.0, mean = 0.0;
            for (auto& v : line)
            {
                smooth += 0.35 * (random.next() - smooth);
                v = smooth;
                mean += smooth;
            }
            mean /= size;
            for (auto& v : line)
                v -= mean;

            const double gain = std::pow(10.0, -3.0 / (hz * 2.8)); // 60 dB in 2.8 s
            int write = 0;
            double previous = 0.0;
            for (int i = start; i < rawLength; ++i)
            {
                const int newer = (write - whole + 2 * size) % size;
                const int older = (newer - 1 + size) % size;
                const double out = line[static_cast<std::size_t>(newer)] * (1.0 - fraction) + line[static_cast<std::size_t>(older)] * fraction;
                line[static_cast<std::size_t>(write)] = gain * 0.5 * (out + previous);
                previous = out;
                write = (write + 1) % size;
                x[static_cast<std::size_t>(i)] += amplitude * out;
            }
        }

        std::vector<double> makeHarp()
        {
            std::vector<double> x(static_cast<std::size_t>(rawLength), 0.0);
            Random random(0x4A29Fu);
            // C major add9, a string every quarter of a second; the last two fall after the loop's end and open it.
            constexpr int semitones[] { 0, 4, 7, 14, 7, 4, 12, 7, 0, 7, 4, 14, 12, 7, 4, 7, 0, 4 };
            int index = 0;
            for (int note : semitones)
            {
                pluck(x, index * 12000, c4 * std::exp2(note / 12.0), 0.6 + 0.4 * random.unit(), random);
                ++index;
            }
            return x;
        }

        /** Folds what follows the end into the start, so the sample loops without a seam. */
        std::vector<float> closeLoop(const std::vector<double>& raw)
        {
            std::vector<double> x(raw.begin(), raw.begin() + length);
            for (int i = 0; i < foldLength; ++i)
            {
                const double a = 0.5 * pi * i / foldLength;
                x[static_cast<std::size_t>(i)] = raw[static_cast<std::size_t>(i)] * std::sin(a) + raw[static_cast<std::size_t>(length + i)] * std::cos(a);
            }

            // No DC, one loudness for the whole set, and never over full scale.
            double mean = 0.0;
            for (double v : x)
                mean += v;
            mean /= length;
            double energy = 0.0, peak = 0.0;
            for (auto& v : x)
            {
                v -= mean;
                energy += v * v;
                peak = std::max(peak, std::abs(v));
            }
            const double rms = std::sqrt(energy / length);
            const double scale = rms > 0.0 ? std::min(0.22 / rms, 0.98 / peak) : 0.0;

            std::vector<float> out(static_cast<std::size_t>(length));
            for (int i = 0; i < length; ++i)
                out[static_cast<std::size_t>(i)] = static_cast<float>(x[static_cast<std::size_t>(i)] * scale);
            return out;
        }

        /** A fourth-order Butterworth low-pass run forwards and backwards around the loop: no phase shift, no seam. */
        std::vector<float> bandLimited(const std::vector<float>& source, double hz)
        {
            auto x = source;
            const int n = static_cast<int>(x.size());
            constexpr int runIn = 4096;
            for (int pass = 0; pass < 2; ++pass)
            {
                Biquad a, b;
                a.lowPass(hz, 0.5411961);
                b.lowPass(hz, 1.3065630);
                auto at = [&](int i) -> float& { return x[static_cast<std::size_t>(pass == 0 ? i : n - 1 - i)]; };
                for (int i = n - runIn; i < n; ++i)
                    b.process(a.process(at(i)));
                for (int i = 0; i < n; ++i)
                    at(i) = static_cast<float>(b.process(a.process(at(i))));
            }
            return x;
        }
    }

    const SampleBank& SampleBank::instance()
    {
        static const SampleBank bank;
        return bank;
    }

    const float* SampleBank::data(int sample, int level) const noexcept
    {
        const auto s = static_cast<std::size_t>(std::clamp(sample, 0, sampleCount - 1));
        const auto l = static_cast<std::size_t>(std::clamp(level, 0, levels - 1));
        return storage.data() + (s * levels + l) * stride + 1;
    }

    SampleBank::SampleBank()
    {
        storage.assign(static_cast<std::size_t>(sampleCount) * levels * stride, 0.0f);
        const std::array<std::vector<float>, sampleCount> made { closeLoop(makeGlass()), closeLoop(makeChoir()), closeLoop(makeStrings()),
                                                                 closeLoop(makeAir()), closeLoop(makeHarp()) };
        constexpr double limits[levels] { 0.0, 10000.0, 5000.0 }; // level 0 is the sample as made
        for (std::size_t s = 0; s < made.size(); ++s)
        {
            for (std::size_t l = 0; l < static_cast<std::size_t>(levels); ++l)
            {
                const auto x = l == 0 ? made[s] : bandLimited(made[s], limits[l]);
                float* frame = storage.data() + (s * levels + l) * stride + 1;
                std::copy(x.begin(), x.end(), frame);
                frame[-1] = x[static_cast<std::size_t>(length - 1)];
                for (int g = 0; g < 3; ++g)
                    frame[length + g] = x[static_cast<std::size_t>(g)];
            }
        }
    }
}
