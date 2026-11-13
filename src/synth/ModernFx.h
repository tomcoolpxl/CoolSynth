#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    The 5.3 effects (IMPLEMENTATION_PLAN_V3.md: pump, EQ, flanger).

    Pump: a tempo-synced ducker, the sidechain feel. At every division the gain drops by Depth
    within 2 ms and recovers along a squared curve over Release (a share of the division). The
    engine hands it the beat: the phase at the block start and its step per sample, locked to the
    host's song position while it plays, otherwise free-running and restarted at the first note
    after silence.

    EQ: a low shelf at 150 Hz, a high shelf at 6 kHz (each +-12 dB) and a tilt (+-6 dB, pivoting
    at 1 kHz, made of a complementary pair of shelves there), RBJ cookbook biquads in transposed
    direct form II, coefficients recomputed only when a setting changes.

    Flanger: a 0.3 to 5 ms delay swept by a triangle LFO (the right channel a quarter cycle
    ahead), Feedback from -0.95 to 0.95 (negative is the hollow flavour), a Hermite read, Mix.
*/
namespace coolsynth::synth
{
    struct PumpParameters
    {
        bool enabled = false;
        float depth = 0.6f;
        int division = 8;      // index into delayDivisions: 1/4
        float release = 0.5f;  // share of the division
        double phaseAtStart = 0.0;   // set by the engine
        double phasePerSample = 0.0; // set by the engine
    };

    struct EqParameters
    {
        bool enabled = false;
        float lowDb = 0.0f;
        float tiltDb = 0.0f;
        float highDb = 0.0f;
    };

    struct FlangerParameters
    {
        bool enabled = false;
        float rateHz = 0.3f;
        float depth = 0.7f;
        float feedback = 0.5f;
        float mix = 0.5f;
    };

    class Pump final
    {
    public:
        void prepare(double newSampleRate) { sampleRate = std::max(1000.0, newSampleRate); }

        /** The gain at a phase of the division. */
        static float gainAt(double phase, float depth, float release, double periodSamples, double attackSamples) noexcept
        {
            const double p = phase - std::floor(phase);
            const double r = std::clamp(static_cast<double>(release), 0.02, 1.0);
            if (p >= r)
                return 1.0f;
            const double attack = std::min(1.0, p * periodSamples / std::max(1.0, attackSamples));
            const double recover = 1.0 - p / r;
            return static_cast<float>(1.0 - std::clamp(static_cast<double>(depth), 0.0, 1.0) * attack * recover * recover);
        }

        void process(juce::AudioBuffer<float>& buffer, const PumpParameters& p) noexcept
        {
            if (! p.enabled || p.depth <= 0.0f || p.phasePerSample <= 0.0)
                return;
            const double period = 1.0 / p.phasePerSample;
            const double attackSamples = 0.002 * sampleRate;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float g = gainAt(p.phaseAtStart + p.phasePerSample * i, p.depth, p.release, period, attackSamples);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
            }
        }

    private:
        double sampleRate = 48000.0;
    };

    class MasterEq final
    {
    public:
        void prepare(double newSampleRate, int channelCount)
        {
            sampleRate = std::max(1000.0, newSampleRate);
            states.assign(static_cast<std::size_t>(std::max(1, channelCount)), {});
            applied = { false, 99.0f, 99.0f, 99.0f };
        }

        void reset() noexcept
        {
            for (auto& s : states)
                s = {};
        }

        void process(juce::AudioBuffer<float>& buffer, const EqParameters& p) noexcept
        {
            if (! p.enabled)
                return;
            if (p.lowDb != applied.lowDb || p.tiltDb != applied.tiltDb || p.highDb != applied.highDb)
            {
                const float tilt = std::clamp(p.tiltDb, -6.0f, 6.0f);
                stages[0] = shelf(false, 150.0, std::clamp(p.lowDb, -12.0f, 12.0f));
                stages[1] = shelf(false, 1000.0, -0.5f * tilt);
                stages[2] = shelf(true, 1000.0, 0.5f * tilt);
                stages[3] = shelf(true, 6000.0, std::clamp(p.highDb, -12.0f, 12.0f));
                applied = p;
            }
            const int channels = std::min(buffer.getNumChannels(), static_cast<int>(states.size()));
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& st = states[static_cast<std::size_t>(ch)];
                float* x = buffer.getWritePointer(ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    float v = x[i];
                    for (std::size_t k = 0; k < stages.size(); ++k)
                    {
                        const auto& c = stages[k];
                        auto& z = st[k];
                        const float y = c.b0 * v + z.z1;
                        z.z1 = c.b1 * v - c.a1 * y + z.z2;
                        z.z2 = c.b2 * v - c.a2 * y;
                        v = y;
                    }
                    x[i] = v;
                }
            }
        }

        /** Magnitude in dB of the whole EQ at a frequency, from its current coefficients. */
        [[nodiscard]] double responseDb(double hz) const noexcept
        {
            double total = 0.0;
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
            for (const auto& c : stages)
            {
                const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
                const auto h = (static_cast<double>(c.b0) + static_cast<double>(c.b1) * z1 + static_cast<double>(c.b2) * z2)
                               / (1.0 + static_cast<double>(c.a1) * z1 + static_cast<double>(c.a2) * z2);
                total += 20.0 * std::log10(std::abs(h));
            }
            return total;
        }

    private:
        struct Coefficients
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        };

        struct Memory
        {
            float z1 = 0.0f, z2 = 0.0f;
        };

        /** RBJ shelf, slope 1. */
        Coefficients shelf(bool high, double hz, float gainDb) const noexcept
        {
            Coefficients c;
            if (std::abs(gainDb) < 1.0e-3f)
                return c;
            const double a = std::pow(10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * std::min(hz, 0.45 * sampleRate) / sampleRate;
            const double cw = std::cos(w0), sw = std::sin(w0);
            const double alpha = sw / 2.0 * std::sqrt(2.0);
            const double sq = 2.0 * std::sqrt(a) * alpha;
            double b0, b1, b2, a0, a1, a2;
            if (! high)
            {
                b0 = a * ((a + 1) - (a - 1) * cw + sq);
                b1 = 2 * a * ((a - 1) - (a + 1) * cw);
                b2 = a * ((a + 1) - (a - 1) * cw - sq);
                a0 = (a + 1) + (a - 1) * cw + sq;
                a1 = -2 * ((a - 1) + (a + 1) * cw);
                a2 = (a + 1) + (a - 1) * cw - sq;
            }
            else
            {
                b0 = a * ((a + 1) + (a - 1) * cw + sq);
                b1 = -2 * a * ((a - 1) + (a + 1) * cw);
                b2 = a * ((a + 1) + (a - 1) * cw - sq);
                a0 = (a + 1) - (a - 1) * cw + sq;
                a1 = 2 * ((a - 1) - (a + 1) * cw);
                a2 = (a + 1) - (a - 1) * cw - sq;
            }
            c.b0 = static_cast<float>(b0 / a0);
            c.b1 = static_cast<float>(b1 / a0);
            c.b2 = static_cast<float>(b2 / a0);
            c.a1 = static_cast<float>(a1 / a0);
            c.a2 = static_cast<float>(a2 / a0);
            return c;
        }

        double sampleRate = 48000.0;
        std::array<Coefficients, 4> stages {};
        std::vector<std::array<Memory, 4>> states;
        EqParameters applied {};
    };

    class Flanger final
    {
    public:
        void prepare(double newSampleRate, int channelCount)
        {
            sampleRate = std::max(1000.0, newSampleRate);
            length = static_cast<int>(std::ceil(sampleRate * 0.012)) + 8;
            lines.assign(static_cast<std::size_t>(std::max(1, channelCount)), std::vector<float>(static_cast<std::size_t>(length), 0.0f));
            reset();
        }

        void reset() noexcept
        {
            for (auto& l : lines)
                std::fill(l.begin(), l.end(), 0.0f);
            write = 0;
            phase = 0.0;
        }

        void process(juce::AudioBuffer<float>& buffer, const FlangerParameters& p) noexcept
        {
            if (! p.enabled || lines.empty())
                return;
            const int channels = std::min(buffer.getNumChannels(), static_cast<int>(lines.size()));
            const double step = std::clamp(static_cast<double>(p.rateHz), 0.01, 20.0) / sampleRate;
            const double minDelay = 0.0003 * sampleRate;
            const double sweep = (0.005 - 0.0003) * sampleRate * std::clamp(static_cast<double>(p.depth), 0.0, 1.0);
            const float fb = std::clamp(p.feedback, -0.95f, 0.95f);
            const float wet = std::clamp(p.mix, 0.0f, 1.0f);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                phase += step;
                if (phase >= 1.0)
                    phase -= 1.0;
                for (int ch = 0; ch < channels; ++ch)
                {
                    double ph = phase + (ch == 1 ? 0.25 : 0.0);
                    ph -= std::floor(ph);
                    const double triangle = ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph;
                    auto& line = lines[static_cast<std::size_t>(ch)];
                    const float delayed = read(line, minDelay + sweep * triangle);
                    const float x = buffer.getSample(ch, i);
                    const float in = x + fb * delayed;
                    line[static_cast<std::size_t>(write)] = std::abs(in) < 1.0e-20f ? 0.0f : std::tanh(in);
                    // Mix 1 is equal parts dry and delayed: the deepest notches.
                    buffer.setSample(ch, i, (1.0f - 0.5f * wet) * x + 0.5f * wet * delayed);
                }
                write = (write + 1) % length;
            }
        }

    private:
        float read(const std::vector<float>& line, double delaySamples) const noexcept
        {
            const double position = static_cast<double>(write) - std::clamp(delaySamples, 2.0, static_cast<double>(length - 4));
            const double f = std::floor(position);
            const float t = static_cast<float>(position - f);
            const int base = static_cast<int>(f);
            auto at = [&](int index)
            {
                index %= length;
                if (index < 0)
                    index += length;
                return line[static_cast<std::size_t>(index)];
            };
            const float xm1 = at(base - 1), x0 = at(base), x1 = at(base + 1), x2 = at(base + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        double sampleRate = 48000.0;
        int length = 64;
        int write = 0;
        double phase = 0.0;
        std::vector<std::vector<float>> lines;
    };
}
