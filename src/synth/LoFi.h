#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    Tape and vinyl lo-fi stage (docs/design/SOUND_BANK_V3.md LOFI; docs/design/IMPLEMENTATION_PLAN_V3.md 5.2: wow,
    flutter, hiss, crackle, bandwidth, bit depth, sample rate).

    Wow and flutter move a short delay line: wow a slow 0.45 Hz drift (up to about 14 cents)
    that wanders a little from cycle to cycle, flutter a 6.5 Hz shimmer (up to about 6 cents),
    as a worn cassette transport does. Bandwidth closes a 2-pole low-pass from 20 kHz down to
    1.5 kHz. Bits and Rate crush as the crusher processor does (16 bits and the top rate are
    off). Hiss (up to about -33 dBFS) and Crackle (clicks up to 40 a second and a quiet bed) are
    the medium's noise, below. Mix blends with the clean signal, which is delayed by
    the same fixed 6 ms as the tape path, so a partial mix never comb-filters.

    The noise (D221, docs/research/noise-modelling.md) is as tape and records make it, not as the music moves:
    - Its level is steady while anything plays and fades out after a second of silence, as u-he Satin's Auto
      Mute does; before D221 it followed every note, which a noise reduction's "breathing" is the defect of.
    - Hiss has tape's spectrum after playback EQ: rising above the 70 us turnover (2.3 kHz), rolled off near
      12 kHz, a little less below 500 Hz (+8 dB above, -4 dB below); it goes in before Bandwidth and the crush, so it dulls with the music
      and dithers the crush, as ChowTapeModel and RC-20 add theirs. With it, modulation (asperity) noise rides on
      the signal itself, about 45 dB under it at full Hiss.
    - Crackle is many short filtered clicks of 20 us to 4 ms, mostly in one channel (a record's groove walls carry
      one channel each), many small and few large; a quiet dense bed whose rate drifts and swells with the turn of
      a 33 1/3 record (1.8 s); two ticks that come back each turn; and rare pops with a low thump that falls
      (Moliner and Valimaki, DAFx 2022).
    - Each slot draws its own random numbers (setSeed), so two LoFi slots never make the same noise.
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
            depthGlide = 1.0 - std::exp(-1.0 / (0.05 * sampleRate)); // about 50 ms
            // The hiss's shape: a low shelf 4 dB down under 500 Hz, a rise of 8 dB above 2.3 kHz, a roll-off at 12 kHz.
            const auto onePole = [this](double hz) { return static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * hz / sampleRate)); };
            shelfLow = onePole(500.0);
            shelfHigh = onePole(2300.0);
            hissTop = onePole(std::min(12000.0, 0.45 * sampleRate));
            quietHold = static_cast<int>(sampleRate * quietHoldSeconds);
            gainUp = static_cast<float>(1.0 / (0.01 * sampleRate));
            gainDown = static_cast<float>(1.0 / (fadeOutSeconds * sampleRate));
            rotationStep = (100.0 / 3.0 / 60.0) / sampleRate; // 33 1/3 turns a minute
            reset();
        }

        /** The slot's own random numbers (D221): set before prepare; reset starts them again from it. */
        void setSeed(std::uint32_t newSeed) noexcept
        {
            seed = newSeed == 0 ? 0x2545F491u : newSeed;
        }

        /** Back to the slot's first random numbers, the record's marks again where they were (D222). */
        void reseed() noexcept
        {
            rng = seed;
            noiseRng = seed * 0x9E3779B1u + 0x7F4A7C15u;
            if (noiseRng == 0)
                noiseRng = 1;
            rotation = 0.0;
            bedDrift = bedDriftTarget = 0.0f;
            for (auto& mark : marks)
            {
                mark.phase = 0.5 * (nextNoise() + 1.0f);
                mark.size = 0.02f + 0.04f * (0.5f * (nextNoise() + 1.0f));
                mark.channel = nextNoise() > 0.0f ? 1 : 0;
            }
        }

        void reset() noexcept
        {
            for (auto& c : channels)
            {
                std::fill(c.line.begin(), c.line.end(), 0.0f);
                c.ic1 = c.ic2 = c.held = 0.0f;
                c.hissLow = c.hissHigh = c.hissTop = c.hissTop2 = c.asperity = 0.0f;
                for (auto& v : c.clicks)
                    v = {};
                c.thump = {};
            }
            write = 0;
            noiseGain = 0.0f;
            quietFor = quietHold;
            reseed(); // two marks on the record, at their places on the turn, ticking each time round
            wowPhase = flutterPhase = 0.0;
            wowDrift = wowDriftTarget = 0.0f;
            holdPhase = 1.0;
            depthsPrimed = false; // the next block starts at its own depths
        }

        void process(juce::AudioBuffer<float>& buffer, const LoFiParameters& p) noexcept
        {
            if (! p.enabled || channels.empty())
                return;
            const int numChannels = std::min(buffer.getNumChannels(), static_cast<int>(channels.size()));
            const int numSamples = buffer.getNumSamples();

            // Wow and flutter move the delay line's read point: their depths glide (D179), or a change would jump
            // it, a click, at every block (an effect route changes them every 32 samples).
            const double wowTarget = 0.0025 * juce::jlimit(0.0, 1.0, static_cast<double>(p.wow)) * sampleRate;       // samples
            const double flutterTarget = 0.00015 * juce::jlimit(0.0, 1.0, static_cast<double>(p.flutter)) * sampleRate;
            if (! depthsPrimed)
            {
                wowDepth = wowTarget;
                flutterDepth = flutterTarget;
                depthsPrimed = true;
            }
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

            const float hiss = juce::jlimit(0.0f, 1.0f, p.hiss);
            // White noise spreads over the whole band: at a higher rate the same level per hertz takes more.
            const float rateScale = std::sqrt(static_cast<float>(sampleRate / 48000.0));
            const float hissLevel = hissScale * hiss * rateScale;
            const float asperityLevel = asperityScale * hiss * rateScale;
            const float crackle = juce::jlimit(0.0f, 1.0f, p.crackle);
            const bool noisy = hiss > 0.0f || crackle > 0.0f;
            const float wet = juce::jlimit(0.0f, 1.0f, p.mix);

            for (int i = 0; i < numSamples; ++i)
            {
                // Anything playing keeps the noise at its level; a second of silence fades it out (D221).
                if (noisy)
                {
                    bool sounding = false;
                    for (int ch = 0; ch < numChannels && ! sounding; ++ch)
                        sounding = std::abs(buffer.getSample(ch, i)) > presenceLevel;
                    quietFor = sounding ? 0 : std::min(quietFor + 1, quietHold);
                    noiseGain = quietFor < quietHold ? std::min(1.0f, noiseGain + gainUp) : std::max(0.0f, noiseGain - gainDown);
                    if (crackle > 0.0f && noiseGain > 0.0f)
                        startCrackle(crackle, numChannels);
                }

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
                wowDepth += (wowTarget - wowDepth) * depthGlide;
                flutterDepth += (flutterTarget - flutterDepth) * depthGlide;
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

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& c = channels[static_cast<std::size_t>(ch)];
                    const float x = buffer.getSample(ch, i);
                    c.line[static_cast<std::size_t>(write)] = x;
                    const float dry = read(c, baseDelay);
                    float y = read(c, delay);

                    // The medium's noise, before Bandwidth and the crush (D221).
                    if (noisy && noiseGain > 0.0f)
                        y += noiseGain * (hissLevel > 0.0f ? hissSample(c, hissLevel, asperityLevel, y) : 0.0f)
                             + noiseGain * (crackle > 0.0f ? crackleSample(c) : 0.0f);

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

                    buffer.setSample(ch, i, dry + (y - dry) * wet);
                }
                write = (write + 1) % length;
            }
        }

    private:
        static constexpr float hissScale = 0.0281f;      // full Hiss: about -33 dBFS, as before D221
        static constexpr float asperityScale = 0.0056f;  // full Hiss: modulation noise about 45 dB under the signal
        static constexpr float presenceLevel = 3.2e-4f;  // -70 dBFS: anything louder is playing
        static constexpr double quietHoldSeconds = 1.0;
        static constexpr double fadeOutSeconds = 1.0;
        static constexpr int clickVoices = 12;

        /** One click: a burst of filtered noise that dies away over its length. */
        struct Click
        {
            int left = 0;           // samples to go
            float level = 0.0f;
            float decay = 0.0f;     // per sample
            float colour = 1.0f;    // one-pole low-pass coefficient for its cutoff in hertz
            float state = 0.0f;
        };

        /** A pop's thump: a damped low sine whose frequency falls. */
        struct Thump
        {
            int left = 0;
            float level = 0.0f, decay = 0.0f, phase = 0.0f, hz = 0.0f, glide = 1.0f;
        };

        struct Mark
        {
            double phase = 0.0;
            float size = 0.0f;
            int channel = 0;
        };

        struct Channel
        {
            std::vector<float> line;
            float ic1 = 0.0f, ic2 = 0.0f, held = 0.0f;
            float hissLow = 0.0f, hissHigh = 0.0f, hissTop = 0.0f, hissTop2 = 0.0f, asperity = 0.0f;
            std::array<Click, clickVoices> clicks {};
            Thump thump {};
        };

        /** Tape hiss after playback EQ, and the modulation noise riding on the signal x. */
        float hissSample(Channel& c, float level, float asperity, float x) noexcept
        {
            const float white = nextNoise();
            c.hissLow += (white - c.hissLow) * shelfLow;      // what lies under 500 Hz
            c.hissHigh += (white - c.hissHigh) * shelfHigh;   // under 2.3 kHz; the rest rises
            float shaped = white - 0.37f * c.hissLow + 1.5f * (white - c.hissHigh);
            c.hissTop += (shaped - c.hissTop) * hissTop;      // tape's loss at the top, two poles
            c.hissTop2 += (c.hissTop - c.hissTop2) * hissTop;
            shaped = c.hissTop2;
            c.asperity += (nextNoise() - c.asperity) * hissTop;
            return level * shaped + asperity * c.asperity * x;
        }

        float crackleSample(Channel& c) noexcept
        {
            float y = 0.0f;
            for (auto& v : c.clicks)
            {
                if (v.left <= 0)
                    continue;
                v.state += (nextNoise() - v.state) * v.colour;
                y += v.level * v.state;
                v.level *= v.decay;
                --v.left;
            }
            if (c.thump.left > 0)
            {
                auto& t = c.thump;
                y += t.level * std::sin(t.phase);
                t.phase += 2.0f * juce::MathConstants<float>::pi * t.hz / static_cast<float>(sampleRate);
                t.hz *= t.glide;
                t.level *= t.decay;
                --t.left;
            }
            return y;
        }

        /** A click of a random length (20 us to 4 ms) and size on one channel, or both with a pan; returns the
            gains it took on each side, for a pop's thump to follow. */
        std::array<float, 2> addClick(float size, int numChannels) noexcept
        {
            const float lengthSeconds = 2.0e-5f * std::pow(200.0f, 0.5f * (nextNoise() + 1.0f)); // 20 us to 4 ms
            const int clickLength = std::max(1, static_cast<int>(lengthSeconds * static_cast<float>(sampleRate)));
            const float sign = nextNoise() > 0.0f ? 1.0f : -1.0f;
            // Mostly one groove wall, so one channel; sometimes both, panned.
            const float side = nextNoise();
            float leftGain = 1.0f, rightGain = 1.0f;
            if (numChannels > 1)
            {
                if (side < 0.0f)
                    (side < -0.5f ? rightGain : leftGain) = 0.0f;
                else
                {
                    const float pan = 0.5f * (nextNoise() + 1.0f);
                    leftGain = std::sqrt(1.0f - pan);
                    rightGain = std::sqrt(pan);
                }
            }
            // A short click is bright, a long one dull: its cutoff in hertz, and its level made up for what the
            // filter takes, so a click sounds the same at any sample rate.
            const double cutoffHz = std::clamp(0.5 / static_cast<double>(lengthSeconds), 300.0, 0.45 * sampleRate);
            const float colour = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate));
            const float makeUp = std::sqrt((2.0f - colour) / colour);
            const float decay = std::exp(-4.0f / static_cast<float>(clickLength));
            for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
            {
                const float gain = ch == 0 ? leftGain : rightGain;
                if (gain <= 0.0f)
                    continue;
                auto& clicks = channels[static_cast<std::size_t>(ch)].clicks;
                auto* slot = &clicks[0];
                for (auto& v : clicks)
                    if (v.left <= 0 || v.level < slot->level)
                        slot = &v; // a free voice, or the quietest
                *slot = { clickLength, sign * size * gain * makeUp, decay, colour, 0.0f };
            }
            return { leftGain, rightGain };
        }

        /** Whether this sample starts a click, a bed click, a mark's tick or a pop; the record turns on. */
        void startCrackle(float crackle, int numChannels) noexcept
        {
            const float rate = static_cast<float>(sampleRate);
            rotation += rotationStep;
            const bool turned = rotation >= 1.0;
            if (turned)
                rotation -= 1.0;
            // The bed: hundreds a second, very quiet, its rate drifting and swelling with the turn.
            if (nextUnit() < 2.0e-4f)
                bedDriftTarget = nextNoise();
            bedDrift += (bedDriftTarget - bedDrift) * 1.0e-4f;
            const float swell = 1.0f + 0.4f * std::sin(2.0f * juce::MathConstants<float>::pi * static_cast<float>(rotation));
            const float bedRate = crackle * (300.0f + 200.0f * bedDrift) * swell;
            if (nextUnit() < bedRate / rate)
                addClick(0.0022f * crackle * (0.2f + nextExponential()), numChannels);
            // The clicks you hear one by one: up to 40 a second, many small, a few large.
            if (nextUnit() < 40.0f * crackle / rate)
                addClick(std::min(0.35f, 0.03f * std::pow(1.0f - 0.999f * nextUnit(), -0.9f)), numChannels);
            // The marks, once a turn each, never quite the same.
            for (const auto& mark : marks)
            {
                const double before = rotation - rotationStep;
                if ((before < mark.phase && rotation >= mark.phase) || (turned && mark.phase <= rotation))
                    if (nextUnit() < 0.85f)
                        addClick(mark.size * crackle * (0.7f + 0.6f * nextUnit()), numChannels);
            }
            // A pop now and then: a click with a low thump that falls.
            if (nextUnit() < 0.25f * crackle / rate)
            {
                const float size = 0.08f + 0.12f * nextUnit();
                const auto sides = addClick(size, numChannels);
                for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
                {
                    if (sides[static_cast<std::size_t>(ch)] <= 0.0f)
                        continue; // the thump on the click's side
                    auto& t = channels[static_cast<std::size_t>(ch)].thump;
                    const float seconds = 0.06f + 0.08f * nextUnit();
                    t = { static_cast<int>(seconds * rate), size * 0.8f * sides[static_cast<std::size_t>(ch)], std::exp(-4.0f / (seconds * rate)), 0.0f,
                          90.0f + 50.0f * nextUnit(), std::pow(0.4f, 1.0f / (seconds * rate)) };
                }
            }
        }

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

        /** The noise's own numbers, apart from the transport's, so the wow stays as it was. -1..1. */
        float nextNoise() noexcept
        {
            noiseRng ^= noiseRng << 13;
            noiseRng ^= noiseRng >> 17;
            noiseRng ^= noiseRng << 5;
            return static_cast<float>(noiseRng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        float nextUnit() noexcept { return 0.5f * (nextNoise() + 1.0f); }

        /** Exponentially distributed, mean 1: many small values, a few large. */
        float nextExponential() noexcept { return -std::log(std::max(1.0e-7f, nextUnit())); }

        double sampleRate = 48000.0;
        int length = 64;
        int write = 0;
        std::vector<Channel> channels;
        double wowPhase = 0.0, flutterPhase = 0.0, holdPhase = 1.0;
        float wowDrift = 0.0f, wowDriftTarget = 0.0f;
        double wowDepth = 0.0, flutterDepth = 0.0, depthGlide = 1.0; // in samples; the glide per sample
        bool depthsPrimed = false;
        float shelfLow = 0.1f, shelfHigh = 0.2f, hissTop = 0.8f;
        int quietHold = 48000, quietFor = 48000;
        float noiseGain = 0.0f, gainUp = 0.002f, gainDown = 2.0e-5f;
        double rotation = 0.0, rotationStep = 1.0e-5;
        float bedDrift = 0.0f, bedDriftTarget = 0.0f;
        std::array<Mark, 2> marks {};
        std::uint32_t seed = 0x2545F491u;
        std::uint32_t rng = 0x2545F491u;
        std::uint32_t noiseRng = 0x6C8E9CF5u;
    };
}
