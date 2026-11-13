#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SincKernels.h"

/*
    The granular effect on a part (decision D82): an insert that remembers the last four seconds
    of whatever the part plays, chords and all, and plays them back as a cloud of short grains.

    Size is a grain's length and Density how many start per second. Position is how far back in
    time the grains are taken from, Spray scatters that by up to a second further back. Pitch
    reads the grains faster or slower, Spread scatters them across the stereo field. Freeze
    stops the recording: the cloud then keeps playing what was caught for as long as it is on,
    whatever the part does. Mix sets the cloud against the part's own sound.

    The grains' level follows the root of their overlap (as the Grain generator's does), so a
    thin and a thick cloud are about as loud. A grain never reads across the point where the
    recording is being written: one that would catch up with it, or run back past the oldest
    sound, starts where it has room instead. Grains are read with the sampled instruments'
    windowed sinc, narrower when a grain is pitched up, so a cloud two octaves up does not fold
    back on itself. Nothing is allocated while playing.
*/
namespace coolsynth::synth::v3
{
    class GrainInsert final
    {
    public:
        static constexpr int maxGrains = 32;
        static constexpr double bufferSeconds = 4.0;

        struct Settings
        {
            float sizeMs = 120.0f;
            float density = 20.0f;     // grains per second
            float positionMs = 250.0f; // how far back the grains are taken from
            float spray = 0.2f;        // 0..1: up to a second further back, at random
            float pitchSemitones = 0.0f;
            float spread = 0.5f;       // 0..1: how far a grain may sit from the middle
            bool freeze = false;
            float mix = 0.5f;
        };

        void prepare(double sampleRate)
        {
            rate = std::max(8000.0, sampleRate);
            length = static_cast<int>(std::ceil(bufferSeconds * rate));
            buffer.assign(2u * static_cast<std::size_t>(length), 0.0f);
            clear();
        }

        /** Silence: the recording and every grain gone. */
        void clear() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            for (auto& grain : grains)
                grain.remaining = 0;
            write = 0;
            recorded = 0;
            untilNext = 0.0;
            level = 1.0f;
        }

        [[nodiscard]] int activeGrainsForTesting() const noexcept
        {
            return static_cast<int>(std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.remaining > 0; }));
        }

        void process(juce::AudioBuffer<float>& stereo, const Settings& settings) noexcept
        {
            if (length <= 0 || stereo.getNumChannels() < 2)
                return;

            const double sizeSamples = std::clamp(static_cast<double>(settings.sizeMs) * 0.001 * rate, 32.0, 0.5 * static_cast<double>(length));
            const double interval = rate / std::clamp(static_cast<double>(settings.density), 0.5, 200.0);
            const double step = std::exp2(std::clamp(static_cast<double>(settings.pitchSemitones), -36.0, 36.0) / 12.0);
            const int cutoff = SincKernels::cutoffFor(step);
            const double mix = std::clamp(static_cast<double>(settings.mix), 0.0, 1.0);
            // Overlapping grains of different sound add in power, and the window's mean square is 0.41: the level
            // follows the root of the overlap. A thin cloud's single grains are left at the sound's own level.
            // With no Spray and no Pitch, while recording, every grain plays the same moment of the past: they are
            // one delayed copy and add in amplitude (the window's mean is a half), so the level follows the overlap.
            const double overlap = std::max(1.0e-9, std::min(static_cast<double>(maxGrains), sizeSamples / interval));
            const double scattered = std::min(1.0, 0.9 / std::sqrt(0.41 * overlap));
            const double together = std::min(1.0, 1.0 / (0.5 * overlap));
            const double alike = settings.freeze ? 0.0
                                                 : std::clamp(1.0 - 25.0 * static_cast<double>(settings.spray), 0.0, 1.0)
                                                       * std::clamp(1.0 - 2.0 * std::abs(static_cast<double>(settings.pitchSemitones)), 0.0, 1.0);
            const float cloudLevel = static_cast<float>(alike * together + (1.0 - alike) * scattered);
            // A cloud made thinner still has its thick grains playing for one grain's length: the level comes
            // down at once and goes up only as slowly as those grains end.
            const float rise = static_cast<float>(1.0 - std::exp(-3.0 / sizeSamples));
            const float wetMix = static_cast<float>(mix);
            const float dry = static_cast<float>(1.0 - mix);

            float* left = stereo.getWritePointer(0);
            float* right = stereo.getWritePointer(1);
            for (int s = 0; s < stereo.getNumSamples(); ++s)
            {
                if (! settings.freeze)
                {
                    buffer[2u * static_cast<std::size_t>(write)] = left[s];
                    buffer[2u * static_cast<std::size_t>(write) + 1u] = right[s];
                    write = write + 1 == length ? 0 : write + 1;
                    recorded = std::min(recorded + 1, length);
                }

                untilNext -= 1.0;
                if (untilNext <= 0.0)
                {
                    start(settings, sizeSamples, step);
                    untilNext += interval * (0.75 + 0.5 * random()); // a quarter early or late: no pitch of its own
                }

                float sumLeft = 0.0f, sumRight = 0.0f;
                for (auto& grain : grains)
                {
                    if (grain.remaining <= 0)
                        continue;
                    // The raised-cosine window, its cosine turned one step per sample instead of worked out each time.
                    const float window = 0.5f - 0.5f * grain.windowCos;
                    const float turned = grain.windowCos * grain.turnCos - grain.windowSin * grain.turnSin;
                    grain.windowSin = grain.windowSin * grain.turnCos + grain.windowCos * grain.turnSin;
                    grain.windowCos = turned;
                    float valueLeft = 0.0f, valueRight = 0.0f;
                    read(grain.position, cutoff, valueLeft, valueRight);
                    sumLeft += valueLeft * window * grain.gainLeft;
                    sumRight += valueRight * window * grain.gainRight;
                    grain.position += grain.step;
                    if (grain.position >= static_cast<double>(length))
                        grain.position -= static_cast<double>(length);
                    --grain.remaining;
                }
                level = cloudLevel < level ? cloudLevel : level + (cloudLevel - level) * rise;
                left[s] = dry * left[s] + wetMix * level * sumLeft;
                right[s] = dry * right[s] + wetMix * level * sumRight;
            }
        }

    private:
        struct Grain
        {
            double position = 0.0; // in the recording, in frames
            double step = 1.0;
            int remaining = 0, total = 1;
            float gainLeft = 1.0f, gainRight = 1.0f;
            float windowCos = 1.0f, windowSin = 0.0f; // where the window's cosine stands
            float turnCos = 1.0f, turnSin = 0.0f;     // one sample's turn of it
        };

        /** Starts one grain if a place is free and the recording has room for it. */
        void start(const Settings& settings, double sizeSamples, double step) noexcept
        {
            auto free = std::find_if(grains.begin(), grains.end(), [](const Grain& g) { return g.remaining <= 0; });
            if (free == grains.end())
                return;

            // How far behind the newest sound a grain may begin. While recording, the newest sound moves on one frame
            // for every frame played: a grain read faster than that must begin far enough back not to catch up with
            // it, and one read slower falls back and must not reach the oldest sound. Frozen, nothing moves.
            constexpr double margin = static_cast<double>(SincKernels::points / 2 + 2);
            const double advance = settings.freeze ? 0.0 : 1.0;
            const double gained = (step - advance) * sizeSamples; // how much nearer the newest sound the grain ends
            const double nearest = margin + std::max(0.0, gained);
            const double farthest = static_cast<double>(recorded) - margin - std::max(0.0, -gained);
            if (farthest < nearest)
                return; // not enough recorded yet for a grain of this size and pitch

            const double wanted = static_cast<double>(settings.positionMs) * 0.001 * rate
                                  + std::clamp(static_cast<double>(settings.spray), 0.0, 1.0) * random() * rate;
            const double behind = std::clamp(wanted, nearest, farthest);

            double position = static_cast<double>(write) - behind;
            if (position < 0.0)
                position += static_cast<double>(length);

            const float pan = static_cast<float>(std::clamp(static_cast<double>(settings.spread), 0.0, 1.0) * (2.0 * random() - 1.0));
            free->position = position;
            free->step = step;
            free->total = std::max(8, static_cast<int>(sizeSamples));
            free->remaining = free->total;
            const double turn = 2.0 * juce::MathConstants<double>::pi / static_cast<double>(free->total);
            free->windowCos = 1.0f;
            free->windowSin = 0.0f;
            free->turnCos = static_cast<float>(std::cos(turn));
            free->turnSin = static_cast<float>(std::sin(turn));
            // A balance, not a pan: the recording is stereo already.
            free->gainLeft = pan > 0.0f ? 1.0f - pan : 1.0f;
            free->gainRight = pan < 0.0f ? 1.0f + pan : 1.0f;
        }

        /** The recording at a fractional frame, through the windowed sinc, wrapping round its end. */
        void read(double position, int cutoff, float& outLeft, float& outRight) const noexcept
        {
            const int index = static_cast<int>(position);
            std::array<float, SincKernels::points> w {};
            kernels->weights(cutoff, static_cast<float>(position - static_cast<double>(index)), w.data());
            const int first = index - (SincKernels::points / 2 - 1);
            float sumLeft = 0.0f, sumRight = 0.0f;
            if (first >= 0 && first + SincKernels::points <= length)
            {
                const float* frame = buffer.data() + 2 * first;
                for (int k = 0; k < SincKernels::points; ++k)
                {
                    sumLeft += frame[2 * k] * w[static_cast<std::size_t>(k)];
                    sumRight += frame[2 * k + 1] * w[static_cast<std::size_t>(k)];
                }
            }
            else
            {
                for (int k = 0; k < SincKernels::points; ++k)
                {
                    int i = first + k;
                    i = i < 0 ? i + length : i >= length ? i - length : i;
                    sumLeft += buffer[2u * static_cast<std::size_t>(i)] * w[static_cast<std::size_t>(k)];
                    sumRight += buffer[2u * static_cast<std::size_t>(i) + 1u] * w[static_cast<std::size_t>(k)];
                }
            }
            outLeft = sumLeft;
            outRight = sumRight;
        }

        /** 0..1 */
        double random() noexcept
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<double>(seed >> 8) / 16777216.0;
        }

        const SincKernels* kernels = &SincKernels::instance();
        double rate = 48000.0;
        int length = 0;
        std::vector<float> buffer; // interleaved left, right
        std::array<Grain, maxGrains> grains {};
        int write = 0;     // where the next frame is recorded
        int recorded = 0;  // how many frames back from there hold sound
        double untilNext = 0.0;
        float level = 1.0f; // the cloud's level as it is now: down at once, up slowly
        std::uint32_t seed = 0x9E3779B9u;
    };
}
