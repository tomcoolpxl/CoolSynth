#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    Tape and vinyl lo-fi stage (SOUND_BANK_V3.md LOFI; IMPLEMENTATION_PLAN_V3.md 5.2: wow,
    flutter, hiss, crackle, bandwidth, bit depth, sample rate).

    Wow and flutter move a short delay line: wow a slow 0.45 Hz drift (up to about 14 cents)
    that wanders a little from cycle to cycle, flutter a 6.5 Hz shimmer (up to about 6 cents),
    as a worn cassette transport does. Bandwidth closes a 2-pole low-pass from 20 kHz down to
    1.5 kHz. Bits and Rate crush as the crusher processor does (16 bits and the top rate are
    off). Hiss is filtered noise up to -30 dBFS, Crackle random clicks up to 40 a second; both
    follow the signal through an envelope (fast attack, 0.4 s release), so silence stays silent
    and the noise breathes with the music. Mix blends with the clean signal, which is delayed by
    the same fixed 6 ms as the tape path, so a partial mix never comb-filters.
*/
namespace coolsynth::synth
{
    struct LoFiParameters
    {
        bool enabled = false;
        float wow = 0.0f;
        float flutter = 0.0f;
        float hiss = 0.0f;
        float crackle = 0.0f;
        float bandwidth = 1.0f; // 1 is full band
        float bits = 16.0f;
        float rateHz = 48000.0f;
        float mix = 1.0f;
    };

    class LoFi final
    {
    public:
        static constexpr double baseDelaySeconds = 0.006;

        void prepare(double newSampleRate, int channelCount)
        {
            sampleRate = std::max(1000.0, newSampleRate);
            length = static_cast<int>(std::ceil(sampleRate * 0.03)) + 8;
            channels.assign(static_cast<std::size_t>(std::max(1, channelCount)), Channel {});
            for (auto& c : channels)
                c.line.assign(static_cast<std::size_t>(length), 0.0f);
            followerAttack = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));
            followerRelease = static_cast<float>(1.0 - std::exp(-1.0 / (0.4 * sampleRate)));
            hissLowPass = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * 6000.0 / sampleRate));
            clickDecay = static_cast<float>(std::exp(-1.0 / (0.0004 * sampleRate)));
            reset();
        }

        void reset() noexcept
        {
            for (auto& c : channels)
            {
                std::fill(c.line.begin(), c.line.end(), 0.0f);
                c.ic1 = c.ic2 = c.held = c.hissState = c.click = c.clickPrevious = 0.0f;
            }
            write = 0;
            wowPhase = flutterPhase = 0.0;
            wowDrift = wowDriftTarget = 0.0f;
            follower = 0.0f;
            holdPhase = 1.0;
        }

        void process(juce::AudioBuffer<float>& buffer, const LoFiParameters& p) noexcept
        {
            if (! p.enabled || channels.empty())
                return;
            const int numChannels = std::min(buffer.getNumChannels(), static_cast<int>(channels.size()));
            const int numSamples = buffer.getNumSamples();

            const double wowDepth = 0.0025 * juce::jlimit(0.0, 1.0, static_cast<double>(p.wow)) * sampleRate;       // samples
            const double flutterDepth = 0.00015 * juce::jlimit(0.0, 1.0, static_cast<double>(p.flutter)) * sampleRate;
            const double baseDelay = baseDelaySeconds * sampleRate;
            const double wowStep = 0.45 / sampleRate, flutterStep = 6.5 / sampleRate;

            const float bandwidth = juce::jlimit(0.0f, 1.0f, p.bandwidth);
            const bool filtering = bandwidth < 0.999f;
            const float cutoff = 1500.0f * std::pow(20000.0f / 1500.0f, bandwidth);
            const float g = std::tan(juce::MathConstants<float>::pi * std::min(cutoff, 0.45f * static_cast<float>(sampleRate)) / static_cast<float>(sampleRate));
            const float k = 1.41421356f;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;

            const float bits = juce::jlimit(1.0f, 16.0f, p.bits);
            const bool crushing = bits < 15.99f;
            const float step = 2.0f / std::exp2(bits);
            const bool holding = p.rateHz < 47900.0f;
            const double holdStep = juce::jlimit(1000.0, sampleRate, static_cast<double>(p.rateHz)) / sampleRate;

            const float hissLevel = 0.032f * juce::jlimit(0.0f, 1.0f, p.hiss);
            const float clickChance = 40.0f * juce::jlimit(0.0f, 1.0f, p.crackle) / static_cast<float>(sampleRate);
            const float wet = juce::jlimit(0.0f, 1.0f, p.mix);

            for (int i = 0; i < numSamples; ++i)
            {
                // Signal level for the noise to follow.
                float level = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    level = std::max(level, std::abs(buffer.getSample(ch, i)));
                follower += (level - follower) * (level > follower ? followerAttack : followerRelease);
                const float noiseGate = std::min(1.0f, follower * 6.0f);

                // Tape transport.
                wowPhase += wowStep;
                if (wowPhase >= 1.0)
                {
                    wowPhase -= 1.0;
                    wowDriftTarget = 0.6f * nextRandom();
                }
                wowDrift += (wowDriftTarget - wowDrift) * 2.0e-5f;
                flutterPhase += flutterStep;
                if (flutterPhase >= 1.0)
                    flutterPhase -= 1.0;
                const double delay = baseDelay
                                     + wowDepth * (std::sin(2.0 * juce::MathConstants<double>::pi * wowPhase) + static_cast<double>(wowDrift))
                                     + flutterDepth * std::sin(2.0 * juce::MathConstants<double>::pi * flutterPhase);

                bool newHold = false;
                if (holding)
                {
                    holdPhase += holdStep;
                    if (holdPhase >= 1.0)
                    {
                        holdPhase -= std::floor(holdPhase);
                        newHold = true;
                    }
                }

                const bool click = clickChance > 0.0f && (nextRandom() * 0.5f + 0.5f) < clickChance;
                const float clickAmount = click ? (0.08f + 0.25f * (nextRandom() * 0.5f + 0.5f)) * (nextRandom() > 0.0f ? 1.0f : -1.0f) : 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& c = channels[static_cast<std::size_t>(ch)];
                    const float x = buffer.getSample(ch, i);
                    c.line[static_cast<std::size_t>(write)] = x;
                    const float dry = read(c, baseDelay);
                    float y = read(c, delay);

                    if (filtering)
                    {
                        const float v3 = y - c.ic2;
                        const float v1 = a1 * c.ic1 + a2 * v3;
                        const float v2 = c.ic2 + g * v1;
                        c.ic1 = 2.0f * v1 - c.ic1;
                        c.ic2 = 2.0f * v2 - c.ic2;
                        y = v2;
                    }
                    if (holding)
                    {
                        if (newHold)
                            c.held = y;
                        y = c.held;
                    }
                    if (crushing)
                        y = std::round(y / step) * step;

                    if (hissLevel > 0.0f)
                    {
                        c.hissState += (nextRandom() - c.hissState) * hissLowPass;
                        y += hissLevel * 2.0f * c.hissState * noiseGate;
                    }
                    if (clickChance > 0.0f)
                    {
                        c.click = c.click * clickDecay + clickAmount;
                        const float tick = c.click - c.clickPrevious; // a high-passed, sharp tick
                        c.clickPrevious = c.click;
                        y += tick * noiseGate;
                    }

                    buffer.setSample(ch, i, dry + (y - dry) * wet);
                }
                write = (write + 1) % length;
            }
        }

    private:
        struct Channel
        {
            std::vector<float> line;
            float ic1 = 0.0f, ic2 = 0.0f, held = 0.0f, hissState = 0.0f, click = 0.0f, clickPrevious = 0.0f;
        };

        float read(const Channel& c, double delaySamples) const noexcept
        {
            const double position = static_cast<double>(write) - juce::jlimit(2.0, static_cast<double>(length - 4), delaySamples);
            const double floorPosition = std::floor(position);
            const float t = static_cast<float>(position - floorPosition);
            int base = static_cast<int>(floorPosition);
            auto at = [&](int index)
            {
                index %= length;
                if (index < 0)
                    index += length;
                return c.line[static_cast<std::size_t>(index)];
            };
            const float xm1 = at(base - 1), x0 = at(base), x1 = at(base + 1), x2 = at(base + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        float nextRandom() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        double sampleRate = 48000.0;
        int length = 64;
        int write = 0;
        std::vector<Channel> channels;
        double wowPhase = 0.0, flutterPhase = 0.0, holdPhase = 1.0;
        float wowDrift = 0.0f, wowDriftTarget = 0.0f;
        float follower = 0.0f, followerAttack = 0.1f, followerRelease = 0.001f;
        float hissLowPass = 0.5f, clickDecay = 0.9f;
        std::uint32_t rng = 0x2545F491u;
    };
}
