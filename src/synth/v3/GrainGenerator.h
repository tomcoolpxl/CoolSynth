#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"
#include "SampleGenerator.h"
#include "Samples.h"
#include "UserLibrary.h"

/*
    V3 grain generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: source, density, size,
    pitch spray, position scan, freeze).

    A cloud of up to twelve grains read from one of the factory samples (Samples.h), or from the
    user's own sample when the slot names a file (UserLibrary.h, decision D137: its first minute,
    its root note's pitch at its own speed; silent while it loads). Density is
    how many grains start each second and Size how long each lasts; every grain is a short piece
    of the sample under a smooth window, read at the note's pitch (C4 is the sample's own), and
    placed somewhere in the stereo field (Spread). Position is where in the sample the grains
    come from. Scan moves that spot through the sample on its own, at that many times the
    sample's speed: 1 plays the sample through in grains, 0 freezes the cloud on one spot,
    negative values run backwards. Spray scatters each grain's start around the spot (up to a
    second either way) and loosens the grains' timing; at 0 the grains fall on a strict grid,
    which at high densities is heard as a tone of its own. Pitch Spray detunes each grain at
    random, a few cents at small settings and up to an octave either way at full.

    The level follows the square root of the overlap, so thin and thick clouds are about as
    loud. A new note starts its scan at Position; grains already sounding in a retriggered voice
    finish. The output is stereo and two samples late like every generator; the next generator's
    combine input gets the sum of both sides.
*/
namespace coolsynth::synth::v3
{
    class GrainGenerator final
    {
    public:
        enum Param : int { sample = 0, position, scan, size, density, spray, pitchSpray, spread, paramCount };
        static constexpr int maxGrains = 12;
        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double sprayFrames = SampleBank::sourceRate; // a second either way at full Spray
        static constexpr double pitchSpraySemitones = 12.0;

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Grain", 3,
                {{ { "sample",  "Sample",      0.0f,  static_cast<float>(SampleBank::sampleCount - 1), 0.0f, Skew::linear, Unit::choice, false, SampleBank::optionNames },
                   { "pos",     "Position",    0.0f,  1.0f,   0.2f,   Skew::linear,      Unit::percent,      true },
                   { "scan",    "Scan",        -2.0f, 2.0f,   0.25f,  Skew::linear,      Unit::generic,      true },
                   { "size",    "Size",        10.0f, 500.0f, 120.0f, Skew::logarithmic, Unit::milliseconds, true },
                   { "density", "Density",     1.0f,  80.0f,  20.0f,  Skew::logarithmic, Unit::generic,      true },
                   { "spray",   "Spray",       0.0f,  1.0f,   0.3f,   Skew::linear,      Unit::percent,      true },
                   { "pitch",   "Pitch Spray", 0.0f,  1.0f,   0.0f,   Skew::linear,      Unit::percent,      true },
                   { "spread",  "Spread",      0.0f,  1.0f,   0.7f,   Skew::linear,      Unit::percent,      true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return 3u * static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &SampleBank::instance();
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            sumLeft = memory.take(n);
            sumRight = memory.take(n);
            static std::atomic<std::uint32_t> instances { 0 };
            rng = (instances.fetch_add(1, std::memory_order_relaxed) + 1u) * 2654435761u ^ 0x6A09E667u;
            if (rng == 0)
                rng = 1;
            reset();
        }

        void reset() noexcept
        {
            for (auto& g : grains)
                g.active = false;
            grainSource = nullptr;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            untilNext = 0.0;
            scanFrames = 0.0;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            cursor = 0;
            input = {};
        }

        void noteOn(const NoteContext&) noexcept
        {
            scanFrames = 0.0;
            untilNext = 0.0;
        }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        [[nodiscard]] double scanFramesForTesting() const noexcept { return scanFrames; }
        [[nodiscard]] int activeGrainsForTesting() const noexcept
        {
            return static_cast<int>(std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.active; }));
        }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            // The user's own sample when the slot names one. Grains still reading a source that was swapped or
            // dropped end at once rather than read freed memory.
            const UserGrainSource* user = params.userSound >= 0 ? UserLibrary::instance().grains(params.userSound) : nullptr;
            if (user != grainSource)
            {
                for (auto& g : grains)
                    g.active = false;
                grainSource = user;
            }
            const bool silent = params.userSound >= 0 && user == nullptr;
            const double frames = user != nullptr ? static_cast<double>(user->length) : static_cast<double>(SampleBank::length);
            const double sourceRate = user != nullptr ? user->rate : SampleBank::sourceRate;
            const double rootHz = user != nullptr ? user->rootHz : SampleBank::rootHz;

            const int n = std::min(numSamples, static_cast<int>(sumLeft.size()));
            const int which = juce::jlimit(0, SampleBank::sampleCount - 1, static_cast<int>(std::lround(params.p[sample])));
            const double ratio = juce::jlimit(1.0 / 64.0, 16.0, static_cast<double>(params.frequencyHz) / rootHz);
            const double framesPerSample = sourceRate / sampleRate;

            const double sizeSamples = std::max(32.0, 0.001 * juce::jlimit(10.0, 500.0, static_cast<double>(params.p[size])) * sampleRate);
            const double interval = sampleRate / juce::jlimit(1.0, 80.0, static_cast<double>(params.p[density]));
            const double sprayAmount = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[spray]));
            const double pitchAmount = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[pitchSpray]));
            const float spreadAmount = juce::jlimit(0.0f, 1.0f, params.p[spread]);
            const double spot = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[position])) * frames;

            // Overlapping grains add in power, and the window's mean square is 0.41: the level follows the
            // root of the overlap. A thin cloud's single grains are left at the sample's own level.
            const double overlap = std::min(static_cast<double>(maxGrains), sizeSamples / interval);
            const float level = static_cast<float>(std::min(1.0, 0.9 / std::sqrt(0.41 * overlap)));

            std::fill_n(sumLeft.begin(), n, 0.0f);
            std::fill_n(sumRight.begin(), n, 0.0f);

            // Start this block's grains, each at its own sample.
            while (untilNext < static_cast<double>(n))
            {
                const int offset = std::max(0, static_cast<int>(untilNext));
                if (Grain* g = silent ? nullptr : freeGrain())
                {
                    const double detune = pitchAmount * pitchAmount * pitchSpraySemitones * nextBipolar();
                    const double grainRatio = ratio * std::exp2(detune / 12.0);
                    const double scatter = sprayAmount * sprayAmount * sprayFrames * (sourceRate / SampleBank::sourceRate) * nextBipolar();
                    double from = spot + scanFrames + static_cast<double>(offset) * params.p[scan] * framesPerSample + scatter;
                    from -= frames * std::floor(from / frames);

                    g->active = true;
                    g->offset = offset;
                    g->frame = from;
                    g->step = grainRatio * framesPerSample;
                    g->data = user != nullptr ? user->data(SampleBank::levelFor(grainRatio)) : bank->data(which, SampleBank::levelFor(grainRatio));
                    g->phase = 0.0f;
                    g->phaseStep = static_cast<float>(1.0 / sizeSamples);
                    const float pan = juce::jlimit(-1.0f, 1.0f, params.pan + spreadAmount * static_cast<float>(nextBipolar()));
                    const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    g->gainLeft = level * std::cos(angle);
                    g->gainRight = level * std::sin(angle);
                }
                // Spray loosens the grid: the next grain comes up to half an interval early or late.
                untilNext += std::max(1.0, interval * (1.0 + 0.5 * sprayAmount * nextBipolar()));
            }
            untilNext -= static_cast<double>(n);

            for (auto& g : grains)
            {
                if (! g.active)
                    continue;
                for (int s = g.offset; s < n; ++s)
                {
                    const int index = static_cast<int>(g.frame);
                    const float v = SampleGenerator::hermite(g.data + index, static_cast<float>(g.frame - index));
                    // A smooth bump, close to a Hann window and far cheaper: (4 x (1 - x)) squared.
                    const float bump = 4.0f * g.phase * (1.0f - g.phase);
                    const float windowed = v * bump * bump;
                    sumLeft[static_cast<std::size_t>(s)] += windowed * g.gainLeft;
                    sumRight[static_cast<std::size_t>(s)] += windowed * g.gainRight;

                    g.frame += g.step;
                    if (g.frame >= frames)
                        g.frame -= frames;
                    g.phase += g.phaseStep;
                    if (g.phase >= 1.0f)
                    {
                        g.active = false;
                        break;
                    }
                }
                g.offset = 0;
            }

            scanFrames += static_cast<double>(n) * juce::jlimit(-2.0, 2.0, static_cast<double>(params.p[scan])) * framesPerSample;
            scanFrames -= frames * std::floor(scanFrames / frames);

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            for (int s = 0; s < numSamples; ++s)
            {
                const unsigned int now = cursor & 3u;
                ringLeft[now] = s < n ? sumLeft[static_cast<std::size_t>(s)] : 0.0f;
                ringRight[now] = s < n ? sumRight[static_cast<std::size_t>(s)] : 0.0f;
                const unsigned int delayed = (cursor + 2u) & 3u;
                float outLeft = ringLeft[delayed], outRight = ringRight[delayed];
                ++cursor;

                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    const float k = useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                    outLeft *= k;
                    outRight *= k;
                }
                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[static_cast<std::size_t>(s)] = 0.7071f * (outLeft + outRight);
            }
        }

    private:
        struct Grain
        {
            bool active = false;
            int offset = 0;        // where in the current block it begins
            double frame = 0.0;    // in source frames
            double step = 0.0;
            const float* data = nullptr;
            float phase = 0.0f, phaseStep = 0.0f; // through its window
            float gainLeft = 0.0f, gainRight = 0.0f;
        };

        Grain* freeGrain() noexcept
        {
            for (auto& g : grains)
                if (! g.active)
                    return &g;
            return nullptr;
        }

        /** Uniform in -1..1. */
        double nextBipolar() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<double>(rng >> 8) * (2.0 / 16777216.0) - 1.0;
        }

        double sampleRate = 96000.0;
        const SampleBank* bank = nullptr;
        const UserGrainSource* grainSource = nullptr; // the user's sample the grains read, or none (the factory's)
        std::array<Grain, maxGrains> grains {};
        double untilNext = 0.0;  // output samples until the next grain starts
        double scanFrames = 0.0; // how far Scan has moved the spot
        std::uint32_t rng = 1;
        std::array<float, 4> ringLeft {}, ringRight {};
        unsigned int cursor = 0;
        std::span<float> signalTrack, sumLeft, sumRight;
        GeneratorLink input {};
    };

    static_assert(Module<GrainGenerator>);
}
