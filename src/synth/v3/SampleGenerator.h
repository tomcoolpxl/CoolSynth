#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "InstrumentCatalogue.h"
#include "Module.h"
#include "SampleLibrary.h"
#include "Samples.h"

/*
    V3 sample generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: one-shot or loop, start,
    loop points, pitch tracking on or off, factory samples; decisions D83 and D96 to D98).

    It plays one of two kinds of source, chosen by Sample:

    The five generated samples (Samples.h). C4 is the sample's own speed; with the slot's key
    tracking off every key plays it there. With Loop off the sample plays once to its end and
    fades out over its last 10 ms; with Loop on it runs to Loop End and continues from Loop
    Start, and over the last part of the loop (Loop Fade) what comes before Loop Start is faded in
    under what is going out. Reverse plays it backwards. The read is a 4-point Hermite one between
    two band levels.

    A sampled instrument from the catalogue (InstrumentCatalogue.h), in the Style chosen. Every
    note picks the recording nearest its pitch, the dynamic layer its velocity asks for, and the
    next of several takes, and plays it in stereo at the speed that gives the note. Loop on keeps
    a sustaining style going on the loop its recording carries, crossfaded; Loop off, and every
    struck or plucked style, lets the recording ring out. Start skips into the recording. The
    read is a 16-point windowed-sinc one (Kaiser, 256 phases, interpolated), with a narrower
    kernel when a note lies so far above its recording that it would alias. The instrument's
    audio comes from the sample library, which loads it in the background: until it is in
    memory the generator is silent, and a note never starts in the middle of a recording.

    Output is two samples late like every generator; a retriggered voice starts again.
*/
namespace coolsynth::synth::v3
{
    /** Windowed-sinc kernels for reading a recording between its frames: three cutoffs, 256 phases, 16 points. */
    class SincKernels final
    {
    public:
        static constexpr int points = 16;
        static constexpr int phases = 256;
        static constexpr int cutoffs = 3; // 1, 1/2 and 1/4 of the recording's own band

        static const SincKernels& instance()
        {
            static const SincKernels kernels;
            return kernels;
        }

        /** The sixteen weights for a position `fraction` (0..1) past a frame; they reach from 7 frames before it to 8 after. */
        void weights(int cutoff, float fraction, float* out) const noexcept
        {
            const float position = std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(phases);
            const int phase = std::min(phases - 1, static_cast<int>(position));
            const float t = position - static_cast<float>(phase);
            const float* a = &table[static_cast<std::size_t>((cutoff * (phases + 1) + phase) * points)];
            const float* b = a + points;
            for (int k = 0; k < points; ++k)
                out[k] = a[k] + (b[k] - a[k]) * t;
        }

        /** The kernel for a read that advances `step` frames per output sample: narrower when it skips frames. */
        [[nodiscard]] static int cutoffFor(double step) noexcept { return step <= 1.0 ? 0 : (step <= 2.0 ? 1 : 2); }

    private:
        SincKernels() : table(static_cast<std::size_t>(cutoffs * (phases + 1) * points))
        {
            constexpr double pi = 3.14159265358979323846;
            constexpr double beta = 9.0;
            auto bessel = [](double x)
            {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 30; ++k)
                {
                    term *= (0.5 * x / k) * (0.5 * x / k);
                    sum += term;
                }
                return sum;
            };
            for (int c = 0; c < cutoffs; ++c)
            {
                const double cutoff = 1.0 / static_cast<double>(1 << c);
                for (int phase = 0; phase <= phases; ++phase)
                {
                    const double fraction = static_cast<double>(phase) / phases;
                    double total = 0.0;
                    std::array<double, points> w {};
                    for (int k = 0; k < points; ++k)
                    {
                        const double x = static_cast<double>(k - (points / 2 - 1)) - fraction; // frames from the read position
                        const double window = std::abs(x) < points / 2.0
                            ? bessel(beta * std::sqrt(1.0 - (x / (points / 2.0)) * (x / (points / 2.0)))) / bessel(beta) : 0.0;
                        const double sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(pi * cutoff * x) / (pi * cutoff * x);
                        w[static_cast<std::size_t>(k)] = cutoff * sinc * window;
                        total += w[static_cast<std::size_t>(k)];
                    }
                    for (int k = 0; k < points; ++k) // unity gain at every phase
                        table[static_cast<std::size_t>((c * (phases + 1) + phase) * points + k)] = static_cast<float>(w[static_cast<std::size_t>(k)] / total);
                }
            }
        }

        std::vector<float> table;
    };

    class SampleGenerator final
    {
    public:
        enum Param : int { sample = 0, start, loop, loopStart, loopEnd, fade, reverse, style, paramCount };
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double endFadeFrames = 480.0; // 10 ms of a generated sample
        static constexpr double shortestLoopFrames = 64.0;
        static constexpr int firstInstrument = SampleBank::sampleCount; // Sample values from here on are catalogue instruments

        /** Every source by name: the generated samples, then the catalogue's instruments. */
        static const std::string& sourceNames()
        {
            static const std::string names = std::string(SampleBank::optionNames) + (InstrumentCatalogue::instance().count() > 0 ? "|" : "")
                                             + InstrumentCatalogue::instance().joinedNames();
            return names;
        }

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Sample", 2,
                {{ { "sample",  "Sample",     0.0f, static_cast<float>(firstInstrument + InstrumentCatalogue::instance().count() - 1), 0.0f,
                     Skew::linear, Unit::choice, false, sourceNames().c_str() },
                   { "start",   "Start",      0.0f, 1.0f, 0.0f, Skew::linear, Unit::percent, true },
                   { "loop",    "Loop",       0.0f, 1.0f, 1.0f, Skew::linear, Unit::choice,  false, "Off|On" },
                   { "lstart",  "Loop Start", 0.0f, 1.0f, 0.0f, Skew::linear, Unit::percent, true },
                   { "lend",    "Loop End",   0.0f, 1.0f, 1.0f, Skew::linear, Unit::percent, true },
                   { "xfade",   "Loop Fade",  0.0f, 1.0f, 0.2f, Skew::linear, Unit::percent, true },
                   { "reverse", "Reverse",    0.0f, 1.0f, 0.0f, Skew::linear, Unit::choice,  false, "Off|On" },
                   { "style",   "Style",      0.0f, static_cast<float>(maxInstrumentStyles - 1), 0.0f, Skew::linear, Unit::choice, false, "1|2|3|4" } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &SampleBank::instance();
            kernels = &SincKernels::instance();
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            reset();
        }

        void reset() noexcept
        {
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            position = 0.0;
            startPending = false;
            finished = true;
            playingStyle = nullptr;
            zone = nullptr;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            ringMono.fill(0.0f);
            cursor = 0;
            input = {};
        }

        void noteOn(const NoteContext& context) noexcept
        {
            startPending = true;
            noteVelocity = context.velocity;
        }

        /** The same voice again: the sample starts over. */
        void strike() noexcept { startPending = true; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        [[nodiscard]] double positionForTesting() const noexcept { return position; }
        [[nodiscard]] const SampleZone* zoneForTesting() const noexcept { return zone; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const int which = std::max(0, static_cast<int>(std::lround(params.p[sample])));
            if (which >= firstInstrument)
                renderInstrument(left, right, numSamples, params, which - firstInstrument);
            else
                renderGenerated(left, right, numSamples, params, std::min(which, SampleBank::sampleCount - 1));
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
        /** Hands one stereo sample out two samples late, with the previous generator's ring or AM on it.
            `valueMono` is what the next generator's combine input gets: the signal before panning. */
        void emit(float* left, float* right, int s, float valueLeft, float valueRight, float valueMono, const SlotParams& params) noexcept
        {
            const unsigned int now = cursor & 3u;
            ringLeft[now] = valueLeft;
            ringRight[now] = valueRight;
            ringMono[now] = valueMono;
            const unsigned int delayed = (cursor + 2u) & 3u;
            float outLeft = ringLeft[delayed], outRight = ringRight[delayed], outMono = ringMono[delayed];
            ++cursor;

            if (input.signal != nullptr && (params.combine == Combine::ring || params.combine == Combine::am))
            {
                const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
                const float m = input.signal[s];
                const float k = params.combine == Combine::ring ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                outLeft *= k;
                outRight *= k;
                outMono *= k;
            }
            left[s] = outLeft;
            right[s] = outRight;
            signalTrack[static_cast<std::size_t>(s)] = outMono;
        }

        void renderGenerated(float* left, float* right, int numSamples, const SlotParams& params, int which) noexcept
        {
            constexpr double frames = SampleBank::length;
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
                zone = nullptr;
                playingStyle = nullptr;
            }

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);

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
                emit(left, right, s, value * gainLeft, value * gainRight, value, params);
            }
        }

        void renderInstrument(float* left, float* right, int numSamples, const SlotParams& params, int instrument) noexcept
        {
            const int styleIndex = juce::jlimit(0, maxInstrumentStyles - 1, static_cast<int>(std::lround(params.p[style])));
            const LoadedStyle* loaded = SampleLibrary::instance().request(instrument, styleIndex);
            const double hz = std::max(1.0, static_cast<double>(params.frequencyHz));

            if (startPending)
            {
                startPending = false;
                playingStyle = loaded;
                finished = true;
                zone = nullptr;
                if (loaded != nullptr)
                {
                    const float midi = static_cast<float>(69.0 + 12.0 * std::log2(hz / 440.0));
                    static std::atomic<unsigned> turns { 0 };
                    zone = loaded->pick(midi, noteVelocity, turns.fetch_add(1, std::memory_order_relaxed));
                    if (zone != nullptr && zone->frames > 0)
                    {
                        position = juce::jlimit(0.0, static_cast<double>(zone->frames - 1), static_cast<double>(params.p[start]) * zone->frames);
                        finished = false;
                    }
                }
            }
            // The library dropped or replaced what this note was playing: it stops rather than read freed memory.
            if (loaded != playingStyle)
            {
                zone = nullptr;
                finished = true;
            }

            // A balance, not a pan: the recording is stereo already.
            const float pan = juce::jlimit(-1.0f, 1.0f, params.pan);
            const float gainLeft = pan > 0.0f ? 1.0f - pan : 1.0f, gainRight = pan < 0.0f ? 1.0f + pan : 1.0f;

            if (finished || zone == nullptr)
            {
                for (int s = 0; s < numSamples; ++s)
                    emit(left, right, s, 0.0f, 0.0f, 0.0f, params);
                return;
            }

            const double rootHz = 440.0 * std::exp2((static_cast<double>(zone->root) - 69.0) / 12.0);
            const double step = juce::jlimit(1.0 / 64.0, 32.0, hz / rootHz) * zone->rate / sampleRate;
            const int cutoff = SincKernels::cutoffFor(step);
            const bool looping = params.p[loop] >= 0.5f && zone->loopEnd > 0;
            const double loopFrames = static_cast<double>(zone->loopEnd - zone->loopStart);
            const double fadeFrames = static_cast<double>(zone->loopFade);
            const double frames = static_cast<double>(zone->frames);
            const float* data = zone->data;

            std::array<float, SincKernels::points> w {};
            auto read = [&](double at, float& outLeft, float& outRight) noexcept
            {
                const int index = static_cast<int>(at);
                kernels->weights(cutoff, static_cast<float>(at - index), w.data());
                const float* frame = data + 2 * (index - (SincKernels::points / 2 - 1));
                float sumLeft = 0.0f, sumRight = 0.0f;
                for (int k = 0; k < SincKernels::points; ++k)
                {
                    sumLeft += frame[2 * k] * w[static_cast<std::size_t>(k)];
                    sumRight += frame[2 * k + 1] * w[static_cast<std::size_t>(k)];
                }
                outLeft = sumLeft;
                outRight = sumRight;
            };

            for (int s = 0; s < numSamples; ++s)
            {
                float valueLeft = 0.0f, valueRight = 0.0f;
                if (! finished)
                {
                    read(position, valueLeft, valueRight);
                    if (looping)
                    {
                        const double intoFade = position - (static_cast<double>(zone->loopEnd) - fadeFrames);
                        if (fadeFrames > 0.0 && intoFade > 0.0)
                        {
                            float earlierLeft = 0.0f, earlierRight = 0.0f;
                            read(position - loopFrames, earlierLeft, earlierRight);
                            const float x = static_cast<float>(std::min(1.0, intoFade / fadeFrames)) * juce::MathConstants<float>::halfPi;
                            valueLeft = valueLeft * std::cos(x) + earlierLeft * std::sin(x);
                            valueRight = valueRight * std::cos(x) + earlierRight * std::sin(x);
                        }
                        position += step;
                        if (position >= static_cast<double>(zone->loopEnd))
                            position -= loopFrames;
                    }
                    else
                    {
                        position += step;
                        if (position >= frames)
                            finished = true;
                    }
                }
                emit(left, right, s, valueLeft * gainLeft, valueRight * gainRight, 0.5f * (valueLeft + valueRight), params);
            }
        }

        double sampleRate = 96000.0;
        const SampleBank* bank = nullptr;
        const SincKernels* kernels = nullptr;
        double position = 0.0; // in source frames
        bool startPending = false;
        bool finished = true;
        float noteVelocity = 1.0f;
        const LoadedStyle* playingStyle = nullptr; // the style in memory this note started on
        const SampleZone* zone = nullptr;
        std::array<float, 4> ringLeft {}, ringRight {}, ringMono {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        GeneratorLink input {};
    };

    static_assert(Module<SampleGenerator>);
}
