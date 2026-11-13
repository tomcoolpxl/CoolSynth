#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "Samples.h"

/*
    V3 sample generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: one-shot or loop, start,
    loop points, pitch tracking on or off, factory samples).

    Plays one of the factory samples (Samples.h). C4 is the sample's own speed; with the slot's
    key tracking off every key plays it there. Start is where a note begins. With Loop off the
    sample plays once to its end and fades out over its last 10 ms. With Loop on it runs to Loop
    End and continues from Loop Start; over the last part of the loop (Loop Fade, up to half the
    loop) what comes before Loop Start is faded in under what is going out, so the join does not
    click. A loop over the whole sample needs no fade: the samples are seamless. Reverse plays
    the sample backwards, every position counted from its end.

    The read is a 4-point Hermite one, between two of the sample's band levels chosen by how far
    the note transposes it, so playing high does not alias. Output is mono, panned, two samples
    late like every generator; a retriggered voice starts the sample again.
*/
namespace coolsynth::synth::v3
{
    class SampleGenerator final
    {
    public:
        enum Param : int { sample = 0, start, loop, loopStart, loopEnd, fade, reverse, paramCount };
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double endFadeFrames = 480.0; // 10 ms of the source
        static constexpr double shortestLoopFrames = 64.0;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Sample", 1,
                {{ { "sample",  "Sample",     0.0f, static_cast<float>(SampleBank::sampleCount - 1), 0.0f, Skew::linear, Unit::choice, false, SampleBank::optionNames },
                   { "start",   "Start",      0.0f, 1.0f, 0.0f, Skew::linear, Unit::percent, true },
                   { "loop",    "Loop",       0.0f, 1.0f, 1.0f, Skew::linear, Unit::choice,  false, "Off|On" },
                   { "lstart",  "Loop Start", 0.0f, 1.0f, 0.0f, Skew::linear, Unit::percent, true },
                   { "lend",    "Loop End",   0.0f, 1.0f, 1.0f, Skew::linear, Unit::percent, true },
                   { "xfade",   "Loop Fade",  0.0f, 1.0f, 0.2f, Skew::linear, Unit::percent, true },
                   { "reverse", "Reverse",    0.0f, 1.0f, 0.0f, Skew::linear, Unit::choice,  false, "Off|On" } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &SampleBank::instance();
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            reset();
        }

        void reset() noexcept
        {
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            position = 0.0;
            startPending = false;
            finished = true;
            ring.fill(0.0f);
            cursor = 0;
            input = {};
        }

        void noteOn(const NoteContext&) noexcept { startPending = true; }
        /** The same voice again: the sample starts over. */
        void strike() noexcept { startPending = true; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        [[nodiscard]] double positionForTesting() const noexcept { return position; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            constexpr double frames = SampleBank::length;
            const int which = juce::jlimit(0, SampleBank::sampleCount - 1, static_cast<int>(std::lround(params.p[sample])));
            const bool looping = params.p[loop] >= 0.5f;
            const bool backwards = params.p[reverse] >= 0.5f;

            // How far the note transposes the sample, and the frames that is per output sample.
            const double ratio = juce::jlimit(1.0 / 64.0, 16.0, static_cast<double>(params.frequencyHz) / SampleBank::rootHz);
            const double step = ratio * SampleBank::sourceRate / sampleRate;

            // Two band levels around the transposition: the full band up to an octave, then one level per octave.
            const double levelPosition = juce::jlimit(0.0, static_cast<double>(SampleBank::levels - 1), std::log2(std::max(1.0, ratio)) - 1.0);
            const int lowerLevel = std::min(SampleBank::levels - 2, static_cast<int>(levelPosition));
            const float levelBlend = static_cast<float>(levelPosition - lowerLevel);
            const float* fuller = bank->data(which, lowerLevel);
            const float* darker = bank->data(which, lowerLevel + 1);

            const double loopFrom = juce::jlimit(0.0, frames - shortestLoopFrames, static_cast<double>(params.p[loopStart]) * frames);
            const double loopTo = juce::jlimit(loopFrom + shortestLoopFrames, frames, static_cast<double>(params.p[loopEnd]) * frames);
            const double loopFrames = loopTo - loopFrom;
            const bool wholeSample = loopFrames >= frames - 0.5;
            const double fadeFrames = wholeSample ? 0.0 : 0.5 * loopFrames * juce::jlimit(0.0, 1.0, static_cast<double>(params.p[fade]));

            if (startPending)
            {
                position = juce::jlimit(0.0, frames - 1.0, static_cast<double>(params.p[start]) * frames);
                finished = false;
                startPending = false;
            }

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;

            auto read = [&](double at) noexcept
            {
                // The sample is a loop: any position has a frame, and backwards counts from the end.
                double p = backwards ? frames - at : at;
                p -= frames * std::floor(p / frames);
                const int index = std::min(SampleBank::length - 1, static_cast<int>(p));
                const float t = static_cast<float>(p - index);
                float v = hermite(fuller + index, t);
                if (levelBlend > 0.0f)
                    v += (hermite(darker + index, t) - v) * levelBlend;
                return v;
            };

            for (int s = 0; s < numSamples; ++s)
            {
                float value = 0.0f;
                if (! finished)
                {
                    value = read(position);
                    if (looping)
                    {
                        const double intoFade = position - (loopTo - fadeFrames);
                        if (fadeFrames > 0.0 && intoFade > 0.0 && position < loopTo)
                        {
                            // What comes before the loop's start rises under what is going out.
                            const float x = static_cast<float>(intoFade / fadeFrames) * juce::MathConstants<float>::halfPi;
                            value = value * std::cos(x) + read(position - loopFrames) * std::sin(x);
                        }
                        position += step;
                        if (position >= loopTo)
                            position -= loopFrames;
                    }
                    else
                    {
                        const double remaining = frames - position;
                        if (remaining < endFadeFrames)
                            value *= static_cast<float>(std::max(0.0, remaining / endFadeFrames));
                        position += step;
                        if (position >= frames)
                            finished = true;
                    }
                }

                const unsigned int now = cursor & 3u;
                ring[now] = value;
                float out = ring[(cursor + 2u) & 3u];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    out *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = out * gainLeft;
                right[s] = out * gainRight;
                signalTrack[static_cast<std::size_t>(s)] = out;
            }
        }

        /** 4-point, 3rd-order Hermite read between frame[0] and frame[1]; frame[-1] and frame[2] must exist. */
        static float hermite(const float* frame, float t) noexcept
        {
            const float xm1 = frame[-1], x0 = frame[0], x1 = frame[1], x2 = frame[2];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

    private:
        double sampleRate = 96000.0;
        const SampleBank* bank = nullptr;
        double position = 0.0; // in source frames
        bool startPending = false;
        bool finished = true;
        std::array<float, 4> ring {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<SampleGenerator>);
}
