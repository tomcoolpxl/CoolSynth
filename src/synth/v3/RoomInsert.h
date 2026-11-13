#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

/*
    V3 room effect on a part (decision D108): a small room around an instrument, as a recording
    of a real one has it. The plate reverb starts weak and rings long; a room answers at once,
    with separate reflections, and is over in a fraction of a second.

    Two stages, after Moorer ("About This Reverberation Business", Computer Music Journal 3(2),
    1979): early reflections from a tapped delay line, feeding a late field.

    Early reflections: the image sources of the first and second order (24 of them) of a
    box-shaped room, 5.0 x 3.9 x 2.7 m at the middle of Size, as two microphones a little apart
    hear an instrument 1.3 m away. Each is a tap on a delay line of the part's sound, as late as
    its path is longer than the direct one and as much weaker (distance, and one loss per wall
    it met). The two microphones hear different paths: that is the room's width.

    Late field: sixteen delay lines fed back through a Hadamard matrix (a feedback delay network;
    Jot and Chaigne, "Digital delay networks for designing artificial reverberators", AES 90th
    Convention, 1991). Every line loses per pass what gives the same decay time on all of them,
    through a one-pole low-pass that makes the treble decay faster (Damping). The reflections
    feed it, so it builds as the room fills.

    Size sets the decay time, 0.1 s to 1.2 s (0.1 * 12^Size), the room's dimensions (growing
    with the root of the decay time) and its walls' loss (from Sabine's formula for that volume
    and decay time), so reflections spread out and last longer together. The wet sound is
    brought to the same level at every Size. A change of Size fades the room out, rebuilds it and
    fades it in (30 ms): its delay lines cannot change length while they sound.
*/
namespace coolsynth::synth::v3
{
    class RoomInsert final
    {
    public:
        struct Settings
        {
            float size = 0.4f;     // 0..1
            float damping = 0.5f;  // 0..1: how much faster the treble dies
            float width = 1.0f;    // 0 mono room .. 1 as the two microphones hear it
            float mix = 0.3f;      // 0 dry .. 1 the room alone
        };

        static constexpr int lineCount = 16;
        static constexpr int tapCount = 24;

        /** The decay time (60 dB) a Size asks for. */
        [[nodiscard]] static double reverbSeconds(float size) noexcept
        {
            return 0.1 * std::pow(12.0, static_cast<double>(std::clamp(size, 0.0f, 1.0f)));
        }

        void prepare(double newSampleRate)
        {
            sampleRate = std::max(8000.0, newSampleRate);
            earlyLine.assign(static_cast<std::size_t>(std::ceil(0.1 * sampleRate)) + 8, 0.0f);
            for (int j = 0; j < lineCount; ++j)
                lines[static_cast<std::size_t>(j)].assign(static_cast<std::size_t>(std::ceil(lineMs[static_cast<std::size_t>(j)] * 0.001 * 2.01 * sampleRate)) + 8, 0.0f);
            fadeStep = static_cast<float>(1.0 / (0.015 * sampleRate));
            configured = false;
            clear();
        }

        /** Silence: the room empty. */
        void clear() noexcept
        {
            std::fill(earlyLine.begin(), earlyLine.end(), 0.0f);
            for (auto& line : lines)
                std::fill(line.begin(), line.end(), 0.0f);
            lowPassed.fill(0.0f);
            heads.fill(0);
            earlyWrite = 0;
            tapState = { 0.0f, 0.0f };
            fade = 1.0f;
            fadingOut = false;
            firstBlock = true;
        }

        void process(juce::AudioBuffer<float>& stereo, const Settings& settings) noexcept
        {
            const int numSamples = stereo.getNumSamples();
            if (earlyLine.empty() || stereo.getNumChannels() < 2 || numSamples <= 0)
                return;

            const float size = std::clamp(settings.size, 0.0f, 1.0f);
            const float damping = std::clamp(settings.damping, 0.0f, 1.0f);
            if (! configured)
            {
                configure(size, damping);
                configured = true;
            }
            else if (std::abs(size - activeSize) > 0.004f)
            {
                fadingOut = true; // the room is rebuilt at this size once it has faded out
            }
            if (std::abs(damping - activeDamping) > 1.0e-4f)
                setDamping(damping);

            const float mixTarget = std::clamp(settings.mix, 0.0f, 1.0f);
            const float widthTarget = std::clamp(settings.width, 0.0f, 1.0f);
            if (firstBlock)
            {
                mix = mixTarget;
                width = widthTarget;
                firstBlock = false;
            }
            const float mixStep = (mixTarget - mix) / static_cast<float>(numSamples);
            const float widthStep = (widthTarget - width) / static_cast<float>(numSamples);

            auto* left = stereo.getWritePointer(0);
            auto* right = stereo.getWritePointer(1);
            const int earlyLength = static_cast<int>(earlyLine.size());

            for (int s = 0; s < numSamples; ++s)
            {
                mix += mixStep;
                width += widthStep;
                const float dryLeft = left[s], dryRight = right[s];

                earlyLine[static_cast<std::size_t>(earlyWrite)] = 0.5f * (dryLeft + dryRight);

                // Early reflections: each microphone's 24 paths.
                float earlyLeft = 0.0f, earlyRight = 0.0f;
                for (int t = 0; t < tapCount; ++t)
                {
                    int at = earlyWrite - tapDelay[0][static_cast<std::size_t>(t)];
                    if (at < 0)
                        at += earlyLength;
                    earlyLeft += tapGain[0][static_cast<std::size_t>(t)] * earlyLine[static_cast<std::size_t>(at)];
                    at = earlyWrite - tapDelay[1][static_cast<std::size_t>(t)];
                    if (at < 0)
                        at += earlyLength;
                    earlyRight += tapGain[1][static_cast<std::size_t>(t)] * earlyLine[static_cast<std::size_t>(at)];
                }
                earlyWrite = (earlyWrite + 1) % earlyLength;
                tapState[0] = (1.0f - tapCoefficient) * earlyLeft + tapCoefficient * tapState[0];
                tapState[1] = (1.0f - tapCoefficient) * earlyRight + tapCoefficient * tapState[1];
                earlyLeft = tapState[0];
                earlyRight = tapState[1];

                // Late field: read the lines, take each one's loss, mix them, feed them again.
                std::array<float, lineCount> mixed;
                float lateLeft = 0.0f, lateRight = 0.0f;
                for (int j = 0; j < lineCount; ++j)
                {
                    const auto index = static_cast<std::size_t>(j);
                    float v = lineGain[index] * lines[index][static_cast<std::size_t>(heads[index])] + lineDamp[index] * lowPassed[index];
                    if (std::abs(v) < 1.0e-18f)
                        v = 0.0f;
                    lowPassed[index] = v;
                    mixed[index] = v;
                    lateLeft += sign(3, j) * v;
                    lateRight += sign(5, j) * v;
                }
                hadamard(mixed);
                const float feed = lateSend * 0.5f * (earlyLeft + earlyRight);
                for (int j = 0; j < lineCount; ++j)
                {
                    const auto index = static_cast<std::size_t>(j);
                    lines[index][static_cast<std::size_t>(heads[index])] = 0.25f * (mixed[index] + sign(15, j) * feed);
                    heads[index] = (heads[index] + 1) % lineLength[index];
                }

                float wetLeft = wetGain * (earlyLeft + 0.25f * lateLeft);
                float wetRight = wetGain * (earlyRight + 0.25f * lateRight);
                const float mid = 0.5f * (wetLeft + wetRight), side = 0.5f * (wetLeft - wetRight) * width;
                wetLeft = (mid + side) * fade;
                wetRight = (mid - side) * fade;

                left[s] = dryLeft * (1.0f - mix) + wetLeft * mix;
                right[s] = dryRight * (1.0f - mix) + wetRight * mix;

                if (fadingOut)
                {
                    fade -= fadeStep;
                    if (fade <= 0.0f)
                    {
                        fade = 0.0f;
                        fadingOut = false;
                        for (auto& line : lines)
                            std::fill(line.begin(), line.end(), 0.0f);
                        lowPassed.fill(0.0f);
                        heads.fill(0);
                        tapState = { 0.0f, 0.0f };
                        configure(size, damping); // the reflections' line keeps the part's sound: the new room has something to answer
                    }
                }
                else if (fade < 1.0f)
                {
                    fade = std::min(1.0f, fade + fadeStep);
                }
            }
        }

    private:
        // Line lengths in milliseconds at the middle Size: none a simple multiple of another.
        static constexpr std::array<double, lineCount> lineMs { 9.1, 10.7, 12.1, 13.7, 15.5, 17.3, 19.1, 20.9, 22.9, 25.1, 27.1, 29.3, 31.9, 34.3, 37.1, 40.3 };

        /** Entry (row, column) of the 16 x 16 Hadamard matrix: +1 or -1. */
        [[nodiscard]] static float sign(int row, int column) noexcept
        {
            unsigned int bits = static_cast<unsigned int>(row & column);
            bits ^= bits >> 2;
            bits ^= bits >> 1;
            return (bits & 1u) != 0u ? -1.0f : 1.0f;
        }

        /** The Hadamard transform in place, unscaled (the caller divides by 4). */
        static void hadamard(std::array<float, lineCount>& v) noexcept
        {
            for (int span = 1; span < lineCount; span *= 2)
                for (int start = 0; start < lineCount; start += 2 * span)
                    for (int i = start; i < start + span; ++i)
                    {
                        const float a = v[static_cast<std::size_t>(i)], b = v[static_cast<std::size_t>(i + span)];
                        v[static_cast<std::size_t>(i)] = a + b;
                        v[static_cast<std::size_t>(i + span)] = a - b;
                    }
        }

        void setDamping(float damping) noexcept
        {
            activeDamping = damping;
            tapCoefficient = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * (12000.0 - 8000.0 * damping) / sampleRate));
            // Jot's absorbent filter: the decay time at the top of the band is `ratio` times the one at the bottom.
            const double ratio = 0.85 - 0.6 * static_cast<double>(damping);
            for (int j = 0; j < lineCount; ++j)
            {
                const auto index = static_cast<std::size_t>(j);
                const double gain = std::pow(10.0, -3.0 * static_cast<double>(lineLength[index]) / sampleRate / activeSeconds);
                const double pole = 1.0 - 2.0 / (1.0 + std::pow(gain, 1.0 - 1.0 / ratio));
                lineGain[index] = static_cast<float>(gain * (1.0 - pole));
                lineDamp[index] = static_cast<float>(pole);
            }
            // The wet sound's level for a steady input, measured on the prototype over Size and Damping, taken out.
            const double s = static_cast<double>(activeSize);
            const double levelDb = -1.87 * s * s + 7.78 * s - 5.11 - 3.9 * (static_cast<double>(damping) - 0.5);
            wetGain = static_cast<float>(std::pow(10.0, -levelDb / 20.0));
        }

        void configure(float size, float damping) noexcept
        {
            activeSize = size;
            activeSeconds = reverbSeconds(size);
            const double scale = std::sqrt(activeSeconds / 0.3);
            const std::array<double, 3> box { 5.0 * scale, 3.9 * scale, 2.7 * scale };
            const std::array<double, 3> source { 0.31 * box[0], 0.42 * box[1], 0.40 * box[2] };
            const std::array<std::array<double, 3>, 2> microphones {{ { 0.55 * box[0], 0.33 * box[1], 0.50 * box[2] },
                                                                      { 0.55 * box[0], 0.57 * box[1], 0.50 * box[2] } }};
            // Sabine: decay time = 0.161 V / (S a). The walls' loss a for this volume and decay time; a reflection keeps sqrt(1 - a).
            const double volume = box[0] * box[1] * box[2];
            const double surface = 2.0 * (box[0] * box[1] + box[0] * box[2] + box[1] * box[2]);
            const double reflection = std::sqrt(1.0 - std::min(0.95, 0.161 * volume / (surface * activeSeconds)));
            const int longest = static_cast<int>(earlyLine.size()) - 2;

            for (int m = 0; m < 2; ++m)
            {
                const auto& microphone = microphones[static_cast<std::size_t>(m)];
                auto distanceTo = [&](const std::array<double, 3>& p)
                {
                    return std::sqrt((p[0] - microphone[0]) * (p[0] - microphone[0]) + (p[1] - microphone[1]) * (p[1] - microphone[1])
                                     + (p[2] - microphone[2]) * (p[2] - microphone[2]));
                };
                const double direct = distanceTo(source);
                int tap = 0;
                for (int nx = -2; nx <= 2; ++nx)
                    for (int ny = -2; ny <= 2; ++ny)
                        for (int nz = -2; nz <= 2; ++nz)
                        {
                            const int order = std::abs(nx) + std::abs(ny) + std::abs(nz);
                            if (order == 0 || order > 2 || tap >= tapCount)
                                continue;
                            const std::array<int, 3> n { nx, ny, nz };
                            std::array<double, 3> image {};
                            for (std::size_t axis = 0; axis < 3; ++axis) // an odd count of reflections along an axis mirrors the source in it
                                image[axis] = n[axis] * box[axis] + (n[axis] % 2 == 0 ? source[axis] : box[axis] - source[axis]);
                            const double distance = distanceTo(image);
                            tapDelay[static_cast<std::size_t>(m)][static_cast<std::size_t>(tap)]
                                = std::clamp(static_cast<int>(std::lround((distance - direct) / 343.0 * sampleRate)), 1, longest);
                            tapGain[static_cast<std::size_t>(m)][static_cast<std::size_t>(tap)]
                                = static_cast<float>(direct / distance * std::pow(reflection, order));
                            ++tap;
                        }
            }

            for (int j = 0; j < lineCount; ++j)
            {
                const auto index = static_cast<std::size_t>(j);
                lineLength[index] = std::clamp(static_cast<int>(std::lround(lineMs[index] * 0.001 * scale * sampleRate)), 8,
                                               static_cast<int>(lines[index].size()) - 1);
                heads[index] = 0;
            }
            setDamping(damping);
        }

        static constexpr float lateSend = 5.0f; // the late field's share: with it the decay runs straight from the first reflection on

        double sampleRate = 48000.0;
        std::vector<float> earlyLine;
        std::array<std::vector<float>, lineCount> lines;
        std::array<int, lineCount> lineLength {};
        std::array<int, lineCount> heads {};
        std::array<float, lineCount> lineGain {};
        std::array<float, lineCount> lineDamp {};
        std::array<float, lineCount> lowPassed {};
        std::array<std::array<int, tapCount>, 2> tapDelay {};
        std::array<std::array<float, tapCount>, 2> tapGain {};
        std::array<float, 2> tapState { 0.0f, 0.0f };
        float tapCoefficient = 0.0f;
        int earlyWrite = 0;
        float activeSize = 0.4f, activeDamping = 0.5f;
        double activeSeconds = 0.3;
        float wetGain = 1.0f;
        float mix = 0.3f, width = 1.0f;
        float fade = 1.0f, fadeStep = 0.001f;
        bool fadingOut = false;
        bool configured = false;
        bool firstBlock = true;
    };
}
