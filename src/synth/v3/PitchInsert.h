#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SincKernels.h"

/*
    V3 pitch effect (decision D205): the sound shifted up or down by up to two octaves, in two ways.

    Heads: the delay-line shifter of the Eventide H910 and H949. A read head plays the recent sound
    faster (up) or slower (down) than it was written, so its pitch moves by that ratio; before it
    runs into the write head (up) or too far behind it (down) a second head takes over a window
    away and the two crossfade (a splice). Where the new head starts is found by correlation, as
    the H949's de-glitching did: within a search range around one window away, the start whose
    recent sound best matches the old head's, so the two are in step and the crossfade neither
    dips nor cancels (NFC-TSM; Haghparast, Penttinen and Valimaki, DAFx-07). The crossfade's gains
    follow that correlation, so its power stays even whether the two heads match or not. Window
    sets how far apart the heads are: short is tight and buzzy on low notes, long is smooth and
    blurs attacks into the soft, attack-less sound. Character runs from clean (the search, long
    correlated crossfades) to rough (no search, short plain crossfades: the H910's glitch). A
    shift up reads faster than the sound was written, so what is above half the band divided by
    the ratio is filtered out first (a sixth-order low-pass) and nothing folds back.

    Spectral: a phase vocoder that moves each spectral peak and its region to the new frequency
    (Laroche and Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and other
    exotic effects", IEEE WASPAA 1999). Hann frames overlap by 75 %; each peak's frequency comes
    from its phase advance; its region moves by a fractional number of bins (linear interpolation,
    whose side bands at this overlap are about 51 dB down) and turns by the frequency change times
    the hop, cumulated from frame to frame. All bins of a region turn alike, which locks their
    phases (the "identity phase locking" that keeps the vocoder from sounding phasey). A partial
    that has just begun keeps its own phase, so attacks keep their shape. Clean on chords and big
    shifts; it is late by its frame (21, 43 or 85 ms at 48 kHz, chosen by Window). Character here
    runs from clean to rough: moves rounded to whole bins (a frame-rate shimmer) and, at the top,
    phases scattered (the phasey wash).

    Spread shifts the left side down and the right side up by that many cents (a wide doubling).
    Delay holds the shifted sound back, and Feedback returns it through a gentle band-pass and a
    soft clip into the shifter, so each repeat climbs or falls another step (the H3000's pitch
    staircase). A delay shorter than a mode's own lateness is that lateness. Mode and frame size
    changes crossfade over 20 ms, the new engine started on the sound already written.
*/
namespace coolsynth::synth::v3
{
    /** One channel of the Spectral mode at one frame size. It reads its frames from the effect's delay line. */
    class SpectralShifter final
    {
    public:
        void prepare(int frameSize)
        {
            size = std::max(64, frameSize);
            half = size / 2;
            hop = size / 4;
            int order = 0;
            while ((1 << order) < size)
                ++order;
            fft = std::make_unique<juce::dsp::FFT>(order);
            window.resize(static_cast<std::size_t>(size));
            for (int i = 0; i < size; ++i)
                window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / size));
            buffer.assign(static_cast<std::size_t>(2 * size), 0.0f);
            const auto bins = static_cast<std::size_t>(half + 1);
            input.assign(bins, {});
            previous.assign(bins, {});
            shifted.assign(bins, {});
            magnitude.assign(bins, 0.0f);
            previousMagnitude.assign(bins, 0.0f);
            rotation.assign(bins, { 1.0f, 0.0f });
            previousRotation.assign(bins, { 1.0f, 0.0f });
            accumulator.assign(static_cast<std::size_t>(size), 0.0f);
            peaks.clear();
            peaks.reserve(bins);
            clear();
        }

        void clear() noexcept
        {
            std::fill(accumulator.begin(), accumulator.end(), 0.0f);
            std::fill(previous.begin(), previous.end(), std::complex<float> {});
            std::fill(previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
            std::fill(previousRotation.begin(), previousRotation.end(), std::complex<float> { 1.0f, 0.0f });
            readPos = 0;
            sinceFrame = 0;
            havePrevious = false;
        }

        [[nodiscard]] int frameSize() const noexcept { return size; }
        [[nodiscard]] int hopSize() const noexcept { return hop; }

        /** This sample's output; the accumulator moves on. */
        [[nodiscard]] float pop() noexcept
        {
            const float y = accumulator[static_cast<std::size_t>(readPos)];
            accumulator[static_cast<std::size_t>(readPos)] = 0.0f;
            if (++readPos == size)
                readPos = 0;
            return y;
        }

        /** True once a hop has passed since the last frame. */
        [[nodiscard]] bool frameDue() noexcept
        {
            if (++sinceFrame < hop)
                return false;
            sinceFrame = 0;
            return true;
        }

        void restartHop() noexcept { sinceFrame = 0; }

        /**
            Analyses the frame of `ring` that ends `back` samples before index `newest`, shifts it by `ratio` and adds it
            to what is still to be played: sample i of the frame lands i - back samples after the next output. A frame
            for the past (back > 0) starts an engine on sound already written.
        */
        void addFrame(const float* ring, int ringSize, int newest, int back, float ratio, float character, std::uint32_t& noise) noexcept
        {
            constexpr float twoPi = juce::MathConstants<float>::twoPi;
            constexpr float pi = juce::MathConstants<float>::pi;

            // The frame, oldest sample first, windowed and turned so that its middle is at index 0: phases then refer
            // to the frame's middle, and a region moved along the frequency axis keeps its place in time.
            int start = newest - back - (size - 1);
            while (start < 0)
                start += ringSize;
            for (int i = 0; i < size; ++i)
            {
                int at = start + i;
                if (at >= ringSize)
                    at -= ringSize;
                int to = i + half;
                if (to >= size)
                    to -= size;
                buffer[static_cast<std::size_t>(to)] = ring[at] * window[static_cast<std::size_t>(i)];
            }
            fft->performRealOnlyForwardTransform(buffer.data(), true);

            float loudest = 0.0f;
            for (int k = 0; k <= half; ++k)
            {
                const auto ku = static_cast<std::size_t>(k);
                input[ku] = { buffer[2 * ku], buffer[2 * ku + 1] };
                magnitude[ku] = std::abs(input[ku]);
                loudest = std::max(loudest, magnitude[ku]);
            }

            // Peaks: louder than two bins on each side, and within 80 dB of the frame's loudest.
            const float floor = std::max(loudest * 1.0e-4f, 1.0e-9f);
            peaks.clear();
            for (int k = 1; k <= half - 1; ++k)
            {
                const float m = magnitude[static_cast<std::size_t>(k)];
                const float left2 = k >= 2 ? magnitude[static_cast<std::size_t>(k - 2)] : 0.0f;
                const float right2 = k + 2 <= half ? magnitude[static_cast<std::size_t>(k + 2)] : 0.0f;
                if (m > floor && m > magnitude[static_cast<std::size_t>(k - 1)] && m >= magnitude[static_cast<std::size_t>(k + 1)]
                    && m > left2 && m >= right2)
                    peaks.push_back(k);
            }

            std::fill(shifted.begin(), shifted.end(), std::complex<float> {});
            const float c = std::clamp(character, 0.0f, 1.0f);
            const float snap = std::clamp((c - 0.3f) / 0.7f, 0.0f, 1.0f);           // moves rounded to whole bins
            const bool keepsAttacks = c < 0.5f;                                      // new partials keep their phase
            const float scatter = std::max(0.0f, c - 0.6f) / 0.4f * (0.5f * pi);   // phases scattered at the top
            const float hopF = static_cast<float>(hop);
            const float sizeF = static_cast<float>(size);

            int regionStart = 0;
            for (std::size_t p = 0; p < peaks.size(); ++p)
            {
                const int k = peaks[p];
                int regionEnd = half;
                if (p + 1 < peaks.size())
                {
                    // The region ends at the quietest bin before the next peak.
                    regionEnd = k;
                    for (int b = k + 1; b < peaks[p + 1]; ++b)
                        if (magnitude[static_cast<std::size_t>(b)] < magnitude[static_cast<std::size_t>(regionEnd)])
                            regionEnd = b;
                }

                // The peak's frequency in radians per sample: from its phase advance since the last frame, or, for a
                // partial that has just appeared, from a parabola through the log magnitudes around it.
                const auto ku = static_cast<std::size_t>(k);
                float omega = twoPi * static_cast<float>(k) / sizeF;
                if (havePrevious && previousMagnitude[ku] > floor)
                {
                    const float advance = std::arg(input[ku] * std::conj(previous[ku]));
                    omega += wrap(advance - omega * hopF) / hopF;
                }
                else
                {
                    const float a = std::log(magnitude[ku - 1] + 1.0e-12f);
                    const float b = std::log(magnitude[ku] + 1.0e-12f);
                    const float d = std::log(magnitude[ku + 1] + 1.0e-12f);
                    const float curve = a - 2.0f * b + d;
                    if (curve < 0.0f)
                        omega += twoPi * std::clamp(0.5f * (a - d) / curve, -0.5f, 0.5f) / sizeF;
                }
                omega = std::clamp(omega, 0.0f, pi);

                const float deltaOmega = (ratio - 1.0f) * omega;
                float deltaBins = deltaOmega * sizeF / twoPi;
                deltaBins += snap * (std::round(deltaBins) - deltaBins);

                auto z = previousRotation[ku];
                if (keepsAttacks && magnitude[ku] > 2.0f * previousMagnitude[ku] + floor)
                    z = { 1.0f, 0.0f };
                z *= std::polar(1.0f, deltaOmega * hopF);
                const float length = std::abs(z);
                z = length > 1.0e-6f ? z / length : std::complex<float> { 1.0f, 0.0f };

                for (int b = regionStart; b <= regionEnd; ++b)
                {
                    const auto bu = static_cast<std::size_t>(b);
                    rotation[bu] = z;
                    auto v = input[bu] * z;
                    if (scatter > 0.0f)
                        v *= std::polar(1.0f, scatter * random(noise));
                    const float target = static_cast<float>(b) + deltaBins;
                    if (target < 0.0f || target > static_cast<float>(half))
                        continue;
                    const int i0 = static_cast<int>(target);
                    const float fraction = target - static_cast<float>(i0);
                    shifted[static_cast<std::size_t>(i0)] += v * (1.0f - fraction);
                    if (i0 + 1 <= half)
                        shifted[static_cast<std::size_t>(i0 + 1)] += v * fraction;
                }
                regionStart = regionEnd + 1;
            }
            if (peaks.empty())
                std::fill(rotation.begin(), rotation.end(), std::complex<float> { 1.0f, 0.0f });

            for (int k = 0; k <= half; ++k)
            {
                const auto ku = static_cast<std::size_t>(k);
                buffer[2 * ku] = shifted[ku].real();
                buffer[2 * ku + 1] = shifted[ku].imag();
            }
            buffer[1] = 0.0f;
            buffer[static_cast<std::size_t>(2 * half + 1)] = 0.0f;
            fft->performRealOnlyInverseTransform(buffer.data());

            // Hann out as in, four frames over each sample: their squares add up to 1.5.
            constexpr float overlapScale = 1.0f / 1.5f;
            for (int i = back; i < size; ++i)
            {
                int from = i + half;
                if (from >= size)
                    from -= size;
                int at = readPos + i - back;
                if (at >= size)
                    at -= size;
                accumulator[static_cast<std::size_t>(at)] += buffer[static_cast<std::size_t>(from)] * window[static_cast<std::size_t>(i)] * overlapScale;
            }

            std::swap(previous, input);
            std::swap(previousMagnitude, magnitude);
            std::swap(previousRotation, rotation);
            havePrevious = true;
        }

    private:
        [[nodiscard]] static float wrap(float phase) noexcept
        {
            constexpr float twoPi = juce::MathConstants<float>::twoPi;
            return phase - twoPi * std::round(phase / twoPi);
        }

        [[nodiscard]] static float random(std::uint32_t& state) noexcept
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0f / 2147483648.0f);
        }

        int size = 1024, half = 512, hop = 256;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, buffer, magnitude, previousMagnitude, accumulator;
        std::vector<std::complex<float>> input, previous, shifted, rotation, previousRotation;
        std::vector<int> peaks;
        int readPos = 0, sinceFrame = 0;
        bool havePrevious = false;
    };

    class PitchInsert final
    {
    public:
        struct Settings
        {
            int mode = 0;               // 0 Heads, 1 Spectral
            float semitones = 12.0f;    // -24..24
            float cents = 0.0f;         // -100..100
            float spreadCents = 0.0f;   // 0..50: left down, right up
            float mix = 0.5f;           // 0 dry .. 1 the shifted sound alone
            float windowMs = 50.0f;     // 10..200
            float character = 0.25f;    // 0 clean .. 1 rough
            float delayMs = 0.0f;       // 0..1000
            float feedback = 0.0f;      // 0..0.9
        };

        static constexpr double maxDelaySeconds = 1.0;
        static constexpr int frameSizes = 3;
        static constexpr int heads = 0; // engine 0; engines 1..3 are Spectral at each frame size

        /** The engine a mode and window ask for: Heads, or Spectral at the frame size nearest the window. */
        [[nodiscard]] static int engineFor(int mode, float windowMs) noexcept
        {
            if (mode == 0)
                return heads;
            return 1 + (windowMs < 30.0f ? 0 : (windowMs < 60.0f ? 1 : 2));
        }

        /** How long the effect keeps sounding once its input is silent. */
        [[nodiscard]] static double tailSeconds(const Settings& s) noexcept
        {
            if (s.mix <= 0.0f)
                return 0.0;
            const double lateness = s.mode == 0 ? 1.6 * std::clamp(static_cast<double>(s.windowMs), 10.0, 200.0) * 0.001 : 0.09;
            const double loop = std::max(std::clamp(static_cast<double>(s.delayMs), 0.0, 1000.0) * 0.001, lateness);
            const double feedback = std::clamp(static_cast<double>(s.feedback), 0.0, 0.9);
            const double repeats = feedback > 0.001 ? std::ceil(std::log(0.001) / std::log(feedback)) : 1.0;
            return std::min(30.0, loop * (repeats + 1.0) + 0.25);
        }

        void prepare(double newSampleRate)
        {
            sampleRate = std::max(8000.0, newSampleRate);
            ringSize = static_cast<int>(std::ceil(1.45 * sampleRate)) + 64;
            for (auto& r : ring)
                r.assign(static_cast<std::size_t>(ringSize), 0.0f);
            for (auto& s : candidates)
                s.assign(static_cast<std::size_t>(std::ceil(0.04 * sampleRate)) + 64, 0.0f);
            for (auto& s : recent)
                s.assign(static_cast<std::size_t>(std::ceil(0.01 * sampleRate)) + 64, 0.0f);
            const int base = sampleRate <= 50000.0 ? 1024 : 2048;
            for (auto& channel : spectral)
                for (int s = 0; s < frameSizes; ++s)
                    channel[static_cast<std::size_t>(s)].prepare(base << s);
            engineFadeSamples = std::max(1, static_cast<int>(std::lround(0.02 * sampleRate)));
            minimumFade = std::max(8.0, 0.0015 * sampleRate);
            searchStep = std::max(1, static_cast<int>(std::lround(sampleRate / 24000.0)));
            correlationLength = std::max(32, static_cast<int>(std::lround(0.010 * sampleRate)));
            auto pole = [&](double seconds) { return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate))); };
            fastPole = pole(0.015);
            slowPole = pole(0.05);
            delayPole = pole(0.1);
            loopRumblePole = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * 60.0 / sampleRate));
            loopTonePole = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * std::min(9000.0, 0.4 * sampleRate) / sampleRate));
            clear();
        }

        /** The slot's own random numbers (D222); clear and reseed start them again. */
        void setSeed(std::uint32_t newSeed) noexcept { noiseStart = newSeed == 0 ? 0x12345678u : newSeed; noise = noiseStart; }
        void reseed() noexcept { noise = noiseStart; }

        /** Silence: the delay line and both engines empty. */
        void clear() noexcept
        {
            noise = noiseStart;
            for (auto& r : ring)
                std::fill(r.begin(), r.end(), 0.0f);
            for (auto& channel : spectral)
                for (auto& s : channel)
                    s.clear();
            for (auto& channel : filters)
                for (auto& f : channel)
                    f.z1 = f.z2 = 0.0f;
            loopRumble = { 0.0f, 0.0f };
            loopTone = { 0.0f, 0.0f };
            filterRatio = { 0.0f, 0.0f };
            headState = {};
            primeNow = {};
            writeIndex = 0;
            active = -1;
            incoming = -1;
            engineFade = 0;
            fresh = true;
        }

        [[nodiscard]] int activeEngine() const noexcept { return active; }
        [[nodiscard]] int incomingEngine() const noexcept { return incoming; }

        void process(juce::AudioBuffer<float>& stereo, const Settings& settings) noexcept
        {
            const int n = stereo.getNumSamples();
            if (ringSize <= 0 || stereo.getNumChannels() < 2 || n <= 0)
                return;

            const float semitonesTarget = std::clamp(settings.semitones, -24.0f, 24.0f);
            const float centsTarget = std::clamp(settings.cents, -100.0f, 100.0f);
            const float spreadTarget = std::clamp(settings.spreadCents, 0.0f, 50.0f);
            const float mixTarget = std::clamp(settings.mix, 0.0f, 1.0f);
            const float windowTarget = std::clamp(settings.windowMs, 10.0f, 200.0f);
            const float characterTarget = std::clamp(settings.character, 0.0f, 1.0f);
            const float delayTarget = std::clamp(settings.delayMs, 0.0f, static_cast<float>(maxDelaySeconds * 1000.0));
            const float feedbackTarget = std::clamp(settings.feedback, 0.0f, 0.9f);
            if (fresh)
            {
                semitones = semitonesTarget;
                cents = centsTarget;
                spread = spreadTarget;
                mix = mixTarget;
                windowMs = windowTarget;
                character = characterTarget;
                delayMs = delayTarget;
                feedback = feedbackTarget;
                fresh = false;
            }

            const int wanted = engineFor(settings.mode, windowTarget);
            if (active < 0)
            {
                active = wanted;
                startPending = true;
            }
            else if (wanted != active && incoming < 0)
            {
                incoming = wanted;
                engineFade = 0;
                startPending = true;
            }

            auto* left = stereo.getWritePointer(0);
            auto* right = stereo.getWritePointer(1);
            for (int s = 0; s < n; ++s)
            {
                semitones += (semitonesTarget - semitones) * fastPole;
                cents += (centsTarget - cents) * fastPole;
                spread += (spreadTarget - spread) * slowPole;
                mix += (mixTarget - mix) * fastPole;
                windowMs += (windowTarget - windowMs) * slowPole;
                character += (characterTarget - character) * slowPole;
                delayMs += (delayTarget - delayMs) * delayPole;
                feedback += (feedbackTarget - feedback) * fastPole;

                const std::array<float, 2> ratio {
                    std::exp2((semitones + (cents - spread) * 0.01f) * (1.0f / 12.0f)),
                    std::exp2((semitones + (cents + spread) * 0.01f) * (1.0f / 12.0f))
                };
                if ((s & 31) == 0)
                    updateFilters(ratio);

                const std::array<float, 2> dry { left[s], right[s] };
                if (startPending)
                {
                    startEngine(incoming >= 0 ? incoming : active, ratio);
                    startPending = false;
                }

                std::array<float, 2> wet = run(active, ratio);
                if (incoming >= 0)
                {
                    const auto coming = run(incoming, ratio);
                    const float t = (static_cast<float>(engineFade) + 0.5f) / static_cast<float>(engineFadeSamples);
                    const float out = std::cos(0.5f * juce::MathConstants<float>::pi * t);
                    const float in = std::sin(0.5f * juce::MathConstants<float>::pi * t);
                    for (int ch = 0; ch < 2; ++ch)
                        wet[static_cast<std::size_t>(ch)] = wet[static_cast<std::size_t>(ch)] * out + coming[static_cast<std::size_t>(ch)] * in;
                }

                // Into the delay line: the input, and the shifted sound returned through a gentle band-pass and a soft clip.
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto c = static_cast<std::size_t>(ch);
                    float back = 0.0f;
                    if (feedback > 1.0e-4f)
                    {
                        loopRumble[c] += (wet[c] - loopRumble[c]) * loopRumblePole;
                        loopTone[c] += ((wet[c] - loopRumble[c]) - loopTone[c]) * loopTonePole;
                        back = feedback * softClip(loopTone[c]);
                    }
                    ring[c][static_cast<std::size_t>(writeIndex)] = prefilter(ch, dry[c] + back);
                }
                if (++writeIndex == ringSize)
                    writeIndex = 0;

                advanceHeads(ratio);
                if (incoming >= 0 && ++engineFade >= engineFadeSamples)
                {
                    active = incoming;
                    incoming = -1;
                }

                left[s] = dry[0] + mix * (wet[0] - dry[0]);
                right[s] = dry[1] + mix * (wet[1] - dry[1]);
            }
        }

    private:
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
            float process(float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        /** A head pair on one channel. Positions are samples behind the band's near edge, `nearEdge` samples behind the write head. */
        struct HeadPair
        {
            std::array<double, 2> position { 0.0, 0.0 };
            int main = 0;
            bool fading = false;
            int fadeDone = 0, fadeLength = 1;
            float correlation = 1.0f;
        };

        static constexpr double nearEdge = 10.0; // the 16-point read reaches 8 samples ahead of where it reads

        [[nodiscard]] static float softClip(float x) noexcept
        {
            const float c = std::clamp(x, -3.0f, 3.0f);
            return c * (27.0f + c * c) / (27.0f + 9.0f * c * c);
        }

        [[nodiscard]] double windowSamples() const noexcept { return static_cast<double>(windowMs) * 0.001 * sampleRate; }

        [[nodiscard]] double delaySamples() const noexcept
        {
            return std::min(static_cast<double>(delayMs) * 0.001, maxDelaySeconds) * sampleRate;
        }

        /** How far behind the band the heads read, so that the delay heard is the Delay set once it is longer than their own. */
        [[nodiscard]] double headsBase() const noexcept
        {
            return std::max(0.0, delaySamples() - (nearEdge + 0.5 * windowSamples()));
        }

        [[nodiscard]] int spectralBase(int engine) const noexcept
        {
            const int size = spectral[0][static_cast<std::size_t>(engine - 1)].frameSize();
            return static_cast<int>(std::lround(std::max(0.0, delaySamples() - (size + 1))));
        }

        /** A sixth-order Butterworth low-pass at 0.45 of the band over the ratio, faded in just above a ratio of 1. */
        void updateFilters(const std::array<float, 2>& ratio) noexcept
        {
            static constexpr std::array<float, 3> q { 0.51763809f, 0.70710678f, 1.93185165f };
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto c = static_cast<std::size_t>(ch);
                const float r = std::max(1.0f, ratio[c]);
                if (std::abs(r - filterRatio[c]) < 1.0e-4f)
                    continue;
                filterRatio[c] = r;
                const double w0 = juce::MathConstants<double>::twoPi * (0.45 / r);
                const double cosine = std::cos(w0), sine = std::sin(w0);
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const double alpha = sine / (2.0 * q[k]);
                    const double a0 = 1.0 + alpha;
                    auto& f = filters[c][k];
                    f.b0 = static_cast<float>((1.0 - cosine) * 0.5 / a0);
                    f.b1 = static_cast<float>((1.0 - cosine) / a0);
                    f.b2 = f.b0;
                    f.a1 = static_cast<float>(-2.0 * cosine / a0);
                    f.a2 = static_cast<float>((1.0 - alpha) / a0);
                }
                filterBlend[c] = std::clamp((r - 1.0f) * 20.0f, 0.0f, 1.0f);
            }
        }

        [[nodiscard]] float prefilter(int ch, float x) noexcept
        {
            const auto c = static_cast<std::size_t>(ch);
            float y = x;
            for (auto& f : filters[c])
                y = f.process(y);
            return x + filterBlend[c] * (y - x);
        }

        /** The delay line `delay` samples behind the write head, read between samples by the 16-point sinc. */
        [[nodiscard]] float read(int ch, double delay) const noexcept
        {
            double pos = static_cast<double>(writeIndex) - delay;
            while (pos < 0.0)
                pos += ringSize;
            const int i = static_cast<int>(pos);
            const float fraction = static_cast<float>(pos - i);
            std::array<float, SincKernels::points> w;
            SincKernels::instance().weights(0, fraction, w.data());
            const float* r = ring[static_cast<std::size_t>(ch)].data();
            const int first = i - (SincKernels::points / 2 - 1);
            float sum = 0.0f;
            if (first >= 0 && first + SincKernels::points <= ringSize)
            {
                for (int k = 0; k < SincKernels::points; ++k)
                    sum += r[first + k] * w[static_cast<std::size_t>(k)];
            }
            else
            {
                for (int k = 0; k < SincKernels::points; ++k)
                {
                    int at = first + k;
                    if (at < 0)
                        at += ringSize;
                    else if (at >= ringSize)
                        at -= ringSize;
                    sum += r[at] * w[static_cast<std::size_t>(k)];
                }
            }
            return sum;
        }

        void startEngine(int engine, const std::array<float, 2>& ratio) noexcept
        {
            juce::ignoreUnused(ratio);
            if (engine == heads)
            {
                for (auto& h : headState)
                {
                    h = {};
                    h.position[0] = 0.5 * windowSamples();
                }
                return;
            }
            // Spectral: its first sample primes it with frames of the sound already written (run()).
            const auto size = static_cast<std::size_t>(engine - 1);
            for (auto& channel : spectral)
                channel[size].clear();
            primeNow[size] = true;
        }

        [[nodiscard]] int newestFor(int engine) const noexcept
        {
            int newest = writeIndex - 1 - spectralBase(engine);
            while (newest < 0)
                newest += ringSize;
            return newest;
        }

        [[nodiscard]] std::array<float, 2> run(int engine, const std::array<float, 2>& ratio) noexcept
        {
            if (engine == heads)
                return runHeads(ratio);
            const auto size = static_cast<std::size_t>(engine - 1);
            const int newest = newestFor(engine);
            std::array<float, 2> out {};
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& e = spectral[static_cast<std::size_t>(ch)][size];
                const float* r = ring[static_cast<std::size_t>(ch)].data();
                const float k = ratio[static_cast<std::size_t>(ch)];
                out[static_cast<std::size_t>(ch)] = e.pop();
                if (primeNow[size])
                {
                    // Started: three frames of the sound already written, a hop apart, then this one, fill what is to be
                    // played as if the engine had run all along, so it sounds at once.
                    for (int j = 3; j >= 1; --j)
                        e.addFrame(r, ringSize, newest, j * e.hopSize(), k, character, noise);
                    e.addFrame(r, ringSize, newest, 0, k, character, noise);
                    e.restartHop();
                }
                else if (e.frameDue())
                {
                    e.addFrame(r, ringSize, newest, 0, k, character, noise);
                }
            }
            primeNow[size] = false;
            return out;
        }

        // ---- Heads ----------------------------------------------------------------------------------------------

        [[nodiscard]] std::array<float, 2> runHeads(const std::array<float, 2>& ratio) noexcept
        {
            const double base = headsBase() + nearEdge;
            const bool linked = spread < 0.005f;
            if (linked)
            {
                if (! headState[0].fading)
                    considerSplice(0, ratio[0], true);
            }
            else
            {
                for (int ch = 0; ch < 2; ++ch)
                    if (! headState[static_cast<std::size_t>(ch)].fading)
                        considerSplice(ch, ratio[static_cast<std::size_t>(ch)], false);
            }

            std::array<float, 2> out {};
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = headState[static_cast<std::size_t>(ch)];
                const int main = h.main;
                if (! h.fading)
                {
                    out[static_cast<std::size_t>(ch)] = read(ch, base + h.position[static_cast<std::size_t>(main)]);
                    continue;
                }
                float gOld = 1.0f, gNew = 0.0f;
                spliceGains(h, gOld, gNew);
                out[static_cast<std::size_t>(ch)] = read(ch, base + h.position[static_cast<std::size_t>(main)]) * gOld
                                                  + read(ch, base + h.position[static_cast<std::size_t>(1 - main)]) * gNew;
            }
            return out;
        }

        /** Even power through a splice: the two heads' sum keeps its level whether their sound matches (correlation 1,
            plain gains adding to 1) or not (correlation 0, equal power). Rough uses the plain gains either way. */
        void spliceGains(const HeadPair& h, float& gOld, float& gNew) const noexcept
        {
            const float t = (static_cast<float>(h.fadeDone) + 0.5f) / static_cast<float>(h.fadeLength);
            const float s = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * std::clamp(t, 0.0f, 1.0f));
            const float u = 1.0f - s, v = s;
            const float rho = std::clamp(h.correlation, 0.0f, 1.0f);
            const float even = 1.0f / std::sqrt(std::max(1.0e-6f, u * u + v * v + 2.0f * rho * u * v));
            const float k = 1.0f + (even - 1.0f) * (1.0f - character);
            gOld = u * k;
            gNew = v * k;
        }

        void advanceHeads(const std::array<float, 2>& ratio) noexcept
        {
            if (active != heads && incoming != heads)
                return;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = headState[static_cast<std::size_t>(ch)];
                const double slope = 1.0 - static_cast<double>(ratio[static_cast<std::size_t>(ch)]);
                h.position[0] = std::max(-nearEdge + 1.0, h.position[0] + slope);
                h.position[1] = std::max(-nearEdge + 1.0, h.position[1] + slope);
                if (h.fading && ++h.fadeDone >= h.fadeLength)
                {
                    h.fading = false;
                    h.main = 1 - h.main;
                }
            }
        }

        /** Starts a splice on a channel (and its pair when linked) when its head is about to leave the band. */
        void considerSplice(int ch, float ratio, bool linked) noexcept
        {
            auto& h = headState[static_cast<std::size_t>(ch)];
            const double window = windowSamples();
            const double slope = static_cast<double>(ratio) - 1.0; // the head gains this much on the write head per sample
            const double d = h.position[static_cast<std::size_t>(h.main)];
            const double life = std::abs(slope) > 1.0e-7 ? window / std::abs(slope) : 1.0e12;
            const double share = 0.5 - 0.45 * static_cast<double>(character);
            const int fade = static_cast<int>(std::clamp(share * life, minimumFade, window));
            const int search = static_cast<int>(std::min(0.5 * window, 0.0125 * sampleRate) * (1.0 - static_cast<double>(character)));

            // Up, the head nears the band's near edge and the new one starts a window further back; down, the head drifts
            // to the far edge and the new one starts near the front, with room to search on both sides.
            double target = 0.0;
            if (slope > 0.0 && d - fade * slope <= 0.0)
                target = d + window;
            else if (slope < 0.0 && d - fade * slope >= window + search)
                target = std::max(d - window, search + 1.0);
            else if (d < -1.0 || d > 1.6 * window + search + 4.0)
                target = 0.5 * window; // out of the band (the window was made smaller): back to its middle
            else
                return;

            // The new head's room: ahead of the band's near edge through the whole fade, and inside the delay line.
            const double lowest = std::max(0.0, fade * slope) + 1.0;
            const double highest = static_cast<double>(ringSize) - headsBase() - nearEdge - correlationLength - 64.0;
            const int jNominal = static_cast<int>(std::lround(target - d));
            const int jLow = static_cast<int>(std::ceil(std::max(lowest, target - search) - d));
            const int jHigh = static_cast<int>(std::floor(std::min(highest, target + search) - d));
            float rho = 1.0f;
            const int j = findSplice(ch, linked, d, std::clamp(jNominal, jLow, std::max(jLow, jHigh)), jLow, std::max(jLow, jHigh), search > 0, rho);

            auto begin = [&](HeadPair& pair, double from)
            {
                if (pair.fading)
                {
                    pair.fading = false;
                    pair.main = 1 - pair.main;
                }
                pair.position[static_cast<std::size_t>(1 - pair.main)] = from;
                pair.fading = true;
                pair.fadeDone = 0;
                pair.fadeLength = std::max(1, fade);
                pair.correlation = rho;
            };
            begin(h, d + j);
            if (linked)
                begin(headState[1], d + j); // the right side follows the left, so the image stays where it was
        }

        /**
            The best offset for the new head within [jLow, jHigh]: the one whose last 10 ms best match the old head's
            (normalised correlation), searched coarsely and then to the sample. Both sides are summed when linked.
        */
        int findSplice(int ch, bool linked, double d, int jNominal, int jLow, int jHigh, bool searching, float& rho) noexcept
        {
            const double base = headsBase() + nearEdge;
            double pos = static_cast<double>(writeIndex) - (base + d);
            while (pos < 0.0)
                pos += ringSize;
            const int i0 = static_cast<int>(pos);
            const int length = std::min(correlationLength, static_cast<int>(recent[0].size()));
            if (! searching || jHigh <= jLow)
                jLow = jHigh = jNominal;
            const int span = jHigh - jLow + length;
            if (span > static_cast<int>(candidates[0].size()))
            {
                rho = 0.0f;
                return jNominal;
            }

            // recent[c][t]: the old head's last `length` samples, oldest first, ending at i0. candidates[c][t]: the
            // delay line from i0 - jHigh - length + 1 on; a candidate j's own last samples end at index jHigh - j + length - 1.
            const int first = ch, last = linked ? 1 : ch;
            auto copy = [&](std::vector<float>& to, const std::vector<float>& from, int startIndex, int count)
            {
                int at = startIndex;
                while (at < 0)
                    at += ringSize;
                while (at >= ringSize)
                    at -= ringSize;
                for (int t = 0; t < count; ++t)
                {
                    to[static_cast<std::size_t>(t)] = from[static_cast<std::size_t>(at)];
                    if (++at == ringSize)
                        at = 0;
                }
            };
            for (int c = first; c <= last; ++c)
            {
                const auto cu = static_cast<std::size_t>(c);
                copy(recent[cu], ring[cu], i0 - length + 1, length);
                copy(candidates[cu], ring[cu], i0 - jHigh - length + 1, span);
            }

            auto energyOld = [&](int step)
            {
                float e = 0.0f;
                for (int c = first; c <= last; ++c)
                {
                    const float* a = recent[static_cast<std::size_t>(c)].data();
                    for (int k = length - 1; k >= 0; k -= step)
                        e += a[k] * a[k];
                }
                return e;
            };
            auto measure = [&](int j, int step, float& energyB)
            {
                float cross = 0.0f;
                energyB = 0.0f;
                for (int c = first; c <= last; ++c)
                {
                    const float* a = recent[static_cast<std::size_t>(c)].data() + (length - 1);
                    const float* b = candidates[static_cast<std::size_t>(c)].data() + (jHigh - j + length - 1);
                    for (int k = 0; k < length; k += step)
                    {
                        cross += a[-k] * b[-k];
                        energyB += b[-k] * b[-k];
                    }
                }
                return cross;
            };

            const float energyA = energyOld(1);
            if (energyA < 1.0e-10f)
            {
                rho = 1.0f; // silence: any start matches
                return jNominal;
            }
            if (jHigh > jLow)
            {
                int best = jNominal;
                const float energyCoarse = energyOld(searchStep);
                float bestScore = -2.0f;
                for (int j = jLow; j <= jHigh; j += searchStep)
                {
                    float energyB = 0.0f;
                    const float cross = measure(j, searchStep, energyB);
                    const float score = cross / std::sqrt(energyCoarse * energyB + 1.0e-20f);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = j;
                    }
                }
                float bestFine = -2.0f;
                const int from = std::max(jLow, best - searchStep + 1), to = std::min(jHigh, best + searchStep - 1);
                for (int j = from; j <= to; ++j)
                {
                    float energyB = 0.0f;
                    const float cross = measure(j, 1, energyB);
                    const float score = cross / std::sqrt(energyA * energyB + 1.0e-20f);
                    if (score > bestFine)
                    {
                        bestFine = score;
                        best = j;
                    }
                }
                rho = bestFine;
                return best;
            }
            float energyB = 0.0f;
            const float cross = measure(jNominal, 1, energyB);
            rho = cross / std::sqrt(energyA * energyB + 1.0e-20f);
            return jNominal;
        }

        double sampleRate = 48000.0;
        int ringSize = 0, writeIndex = 0;
        std::array<std::vector<float>, 2> ring, candidates, recent;
        std::array<std::array<SpectralShifter, frameSizes>, 2> spectral;
        std::array<bool, frameSizes> primeNow {};
        std::array<HeadPair, 2> headState {};
        std::array<std::array<Biquad, 3>, 2> filters {};
        std::array<float, 2> filterRatio { 0.0f, 0.0f }, filterBlend { 0.0f, 0.0f };
        std::array<float, 2> loopRumble { 0.0f, 0.0f }, loopTone { 0.0f, 0.0f };
        float loopRumblePole = 0.01f, loopTonePole = 0.6f;
        float semitones = 0.0f, cents = 0.0f, spread = 0.0f, mix = 0.0f, windowMs = 50.0f, character = 0.25f, delayMs = 0.0f, feedback = 0.0f;
        float fastPole = 0.001f, slowPole = 0.0005f, delayPole = 0.0002f;
        int active = -1, incoming = -1, engineFade = 0, engineFadeSamples = 960;
        bool startPending = false, fresh = true;
        double minimumFade = 72.0;
        int searchStep = 2, correlationLength = 480;
        std::uint32_t noise = 0x12345678u, noiseStart = 0x12345678u;
    };
}
