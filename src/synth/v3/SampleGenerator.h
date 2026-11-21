#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "SincKernels.h"

#include "InstrumentCatalogue.h"
#include "Module.h"
#include "SampleLibrary.h"
#include "Samples.h"
#include "UserLibrary.h"

/*
    V3 sample generator (docs/design/DESIGN_V3.md section 3.1; docs/design/REQUIREMENTS_V3.md: one-shot or loop, start,
    loop points, pitch tracking on or off, factory samples; decisions D83 and D96 to D98).

    It plays one of two kinds of source, chosen by Sample:

    The five generated samples (Samples.h). C4 is the sample's own speed; with the slot's key
    tracking off every key plays it there. With Loop off the sample plays once to its end and
    fades out over its last 10 ms; with Loop on it runs to Loop End and continues from Loop
    Start, and over the last part of the loop (Loop Fade) what comes before Loop Start is faded in
    under what is going out. Reverse plays it backwards. The read is a 4-point Hermite one between
    two band levels.

    A sampled instrument from the catalogue (InstrumentCatalogue.h), in the Style chosen. Every
    note picks the recording mapped to its key and velocity (the mapping is the one the
    recordings' author publishes) and the next of several takes, and plays it in stereo at the
    speed that gives the note, at the level its velocity gives it on that instrument: the square
    of the velocity for most, none at all for an organ or a harpsichord. Loop on keeps
    a sustaining style going on the loop its recording carries, crossfaded; Loop off, and every
    struck or plucked style, lets the recording ring out. Start skips into the recording. The
    read is a 16-point windowed-sinc one (Kaiser, 256 phases, interpolated), with a narrower
    kernel when a note lies so far above its recording that it would alias. The instrument's
    audio comes from the sample library, which loads it in the background: until it is in
    memory the generator is silent, and a note never starts in the middle of a recording.

    Or the user's own sample when the slot names a file (UserLibrary.h, decision D137), played as
    an instrument of one recording over every key: at its root note's speed on its root key, its
    loop when the file has one and Loop is on, its level following velocity squared. Silent while
    the file loads.

    Slices (D207): with Slices at Equal or Attacks the user's file is cut into pieces, one per key
    from First Key up: Count equal pieces, or a cut at every attack the loader found (Sensitivity
    1 keeps them all, 0 only the strongest), each cut 2 ms before its attack and at least 20 ms
    after the last. A slot that names a folder is a kit: one file per key, in name order. A slice
    or a kit's file plays at its own speed on every key (the slot's octave, fine tuning, bend and
    pitch modulation still move it), once, to its end, even after the key is let go: the voice
    waits for it before it releases (holdsRelease). Reverse plays it from its end; Start skips
    into it. It fades in over 1 ms and out over its last 4 ms, so no cut clicks. A key outside the
    pieces is silent.

    Output is two samples late like every generator; a retriggered voice starts again, what was sounding fading out
    under the new start over 5 ms while the new start rises over 2 ms (captureTail), so a repeated key never clicks.
*/
namespace coolsynth::synth::v3
{
    class SampleGenerator final
    {
    public:
        enum Param : int { sample = 0, start, loop, loopStart, loopEnd, fade, reverse, style, slices, count, sense, firstKey, paramCount };
        static constexpr int fewestSlices = 2, mostSlices = 64;  // Equal
        static constexpr int mostAttackSlices = 128;              // Attacks
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

        /** "2|3|..|64": the equal slice counts. */
        static const std::string& countNames()
        {
            static const std::string names = []
            {
                std::string s;
                for (int n = fewestSlices; n <= mostSlices; ++n)
                    s += (s.empty() ? "" : "|") + std::to_string(n);
                return s;
            }();
            return names;
        }

        /** "C-1|C#-1|..|G9": every key by name, middle C as C4. */
        static const std::string& keyNames()
        {
            static const std::string names = []
            {
                static constexpr const char* letters[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                std::string s;
                for (int key = 0; key < 128; ++key)
                    s += (key == 0 ? "" : "|") + std::string(letters[key % 12]) + std::to_string(key / 12 - 1);
                return s;
            }();
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
                   { "style",   "Style",      0.0f, static_cast<float>(maxInstrumentStyles - 1), 0.0f, Skew::linear, Unit::choice, false, "1|2|3|4" },
                   // The user's file cut into slices, one per key (D207): the fourth page.
                   { "slices",   "Slices",      0.0f, 2.0f,  0.0f,  Skew::linear, Unit::choice,  false, "Off|Equal|Attacks" },
                   { "count",    "Count",       0.0f, static_cast<float>(mostSlices - fewestSlices), static_cast<float>(16 - fewestSlices),
                     Skew::linear, Unit::choice, false, countNames().c_str() },
                   { "sense",    "Sensitivity", 0.0f, 1.0f,  0.5f,  Skew::linear, Unit::percent, false },
                   { "firstkey", "First Key",   0.0f, 127.0f, 48.0f, Skew::linear, Unit::choice,  false, keyNames().c_str() } }},
                paramCount };
            return d;
        }

        /**
            Where slice `index` of the user's sound lies (D207), in the recording's frames: Equal cuts it into `count`
            pieces; Attacks (mode 2) cuts 2 ms before each of the strongest attacks, as many of them as `sensitivity`
            says (at least one; D225, by rank, as Ableton's Simpler and Logic's Quick Sampler do), at least 20 ms after the
            last cut. False when there is no such slice.
        */
        static bool sliceRange(const LoadedStyle& style, const SampleZone& zone, int mode, int count, float sensitivity,
                               int index, int& from, int& to) noexcept
        {
            const int frames = zone.frames;
            if (index < 0 || frames <= 1)
                return false;
            if (mode == 1)
            {
                const int pieces = juce::jlimit(fewestSlices, mostSlices, count);
                if (index >= pieces)
                    return false;
                from = static_cast<int>(static_cast<long long>(frames) * index / pieces);
                to = static_cast<int>(static_cast<long long>(frames) * (index + 1) / pieces);
                return to > from;
            }
            const int keep = std::max(1, static_cast<int>(std::lround(juce::jlimit(0.0f, 1.0f, sensitivity) * static_cast<float>(style.attacks.size()))));
            const int preRoll = static_cast<int>(std::lround(0.002 * zone.rate));
            const int apart = static_cast<int>(std::lround(0.020 * zone.rate));
            int start = 0, n = 0;
            for (const auto& attack : style.attacks)
            {
                if (attack.rank >= keep)
                    continue;
                const int cut = std::max(0, attack.frame - preRoll);
                if (cut - start < apart || frames - cut < apart)
                    continue;
                if (n == index)
                {
                    from = start;
                    to = cut;
                    return true;
                }
                start = cut;
                if (++n >= mostAttackSlices - 1)
                    break;
            }
            if (n != index)
                return false;
            from = start;
            to = frames;
            return to > from;
        }

        static constexpr double tailSeconds = 0.005; // a strike on a sounding recording fades the old out over 5 ms
        static constexpr double riseSeconds = 0.002; // and the new start rises over 2 ms, as a fresh note's attack
        static constexpr int tailCapacity = 1024;     // 5 ms up to 192 kHz

        static std::size_t memoryFloats(double, int maxSubBlock)
        {
            return static_cast<std::size_t>(std::max(1, maxSubBlock)) + 2 * static_cast<std::size_t>(tailCapacity);
        }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &SampleBank::instance();
            kernels = &SincKernels::instance();
            signalTrack = memory.take(static_cast<std::size_t>(std::max(1, maxSubBlock)));
            tailLeft = memory.take(static_cast<std::size_t>(tailCapacity));
            tailRight = memory.take(static_cast<std::size_t>(tailCapacity));
            reset();
        }

        SampleGenerator() = default;
        SampleGenerator(const SampleGenerator&) = default;
        SampleGenerator& operator=(const SampleGenerator&) = default;
        ~SampleGenerator() { hold(nullptr); }

        void reset() noexcept
        {
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            position = 0.0;
            startPending = false;
            finished = true;
            hold(nullptr);
            playingStyle = nullptr;
            zone = nullptr;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            ringMono.fill(0.0f);
            cursor = 0;
            input = {};
            slicing = false;
            tailLength = 0;
            tailIndex = 0;
            riseLength = 0;
            riseIndex = 0;
            capturing = false;
        }

        void noteOn(const NoteContext& context) noexcept
        {
            startPending = true;
            noteVelocity = context.velocity;
            noteNumber = context.noteNumber;
        }

        /** Another key on the same voice (a retrigger): the next start picks that key's slice (D207). */
        void noteChanged(int newNote) noexcept { noteNumber = newNote; }

        /** A slice or a kit's file plays to its end after the key is let go (D207): the voice waits while this is true. */
        [[nodiscard]] bool holdsRelease() const noexcept { return (slicing && ! finished) || (startPending && armed); }

        /** The same voice again: the sample starts over. */
        void strike() noexcept { startPending = true; }

        /** A one-shot recording that has played to its end, with no start waiting: silence until the next note, so
            a released voice need not ring out its envelope (TODO 10). A loop never ends. */
        [[nodiscard]] bool silentUntilNextNote() const noexcept { return finished && ! startPending; }

        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), nullptr }; }

        [[nodiscard]] double positionForTesting() const noexcept { return position; }
        [[nodiscard]] const SampleZone* zoneForTesting() const noexcept { return zone; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const int which = std::max(0, static_cast<int>(std::lround(params.p[sample])));
            if (params.userSound >= 0)
            {
                const LoadedStyle* user = UserLibrary::instance().sample(params.userSound);
                armed = std::lround(params.p[slices]) > 0 || (user != nullptr && user->kit);
                renderStyle(left, right, numSamples, params, user);
            }
            else
            {
                armed = false;
                if (which >= firstInstrument)
                    renderInstrument(left, right, numSamples, params, which - firstInstrument);
                else
                    renderGenerated(left, right, numSamples, params, std::min(which, SampleBank::sampleCount - 1));
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
        /** Hands one stereo sample out two samples late, with the previous generator's ring or AM on it.
            `valueMono` is what the next generator's combine input gets: the signal before panning. */
        void emit(float* left, float* right, int s, float valueLeft, float valueRight, float valueMono, const SlotParams& params) noexcept
        {
            if (capturing)
            {
                // Reading ahead for captureTail: the plain values, nothing delayed or combined.
                left[s] = valueLeft;
                right[s] = valueRight;
                return;
            }
            if (riseIndex < riseLength)
            {
                // The new start after a strike rises as a fresh note's 2 ms attack would: the voice's envelope is open.
                const float x = (static_cast<float>(riseIndex++) + 0.5f) / static_cast<float>(riseLength);
                const float h = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * x);
                valueLeft *= h;
                valueRight *= h;
                valueMono *= h;
            }
            if (tailIndex < tailLength)
            {
                // What was sounding before a strike, fading out under the new start (a raised cosine).
                const float x = (static_cast<float>(tailIndex) + 0.5f) / static_cast<float>(tailLength);
                const float g = 0.5f + 0.5f * std::cos(juce::MathConstants<float>::pi * x);
                const auto i = static_cast<std::size_t>(tailIndex++);
                valueLeft += tailLeft[i] * g;
                valueRight += tailRight[i] * g;
                valueMono += 0.5f * (tailLeft[i] + tailRight[i]) * g;
            }
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
                if (! finished && zone == nullptr)
                    captureTail(params); // a strike while it sounds: what was playing fades out under the new start
                position = juce::jlimit(0.0, frames - 1.0, static_cast<double>(params.p[start]) * frames);
                finished = false;
                startPending = false;
                zone = nullptr;
                hold(nullptr);
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

        /** The next few milliseconds of what is sounding, read ahead before a strike starts the recording over, so emit()
            fades it out under the new start instead of cutting it (repeated keys clicked: the user's listening pass,
            2026-11-21). The playing state is left as it was. */
        void captureTail(const SlotParams& params) noexcept
        {
            tailLength = 0;
            tailIndex = 0;
            const int frames = std::min(tailCapacity, std::max(1, static_cast<int>(std::lround(tailSeconds * sampleRate))));
            if (tailLeft.size() < static_cast<std::size_t>(frames) || tailRight.size() < static_cast<std::size_t>(frames))
                return; // not prepared
            const double savedPosition = position, savedPlayed = played;
            const bool savedFinished = finished;
            capturing = true;
            const bool savedStart = startPending;
            startPending = false;
            if (zone != nullptr)
            {
                const double hz = std::max(1.0, static_cast<double>(params.frequencyHz));
                const float pan = juce::jlimit(-1.0f, 1.0f, params.pan);
                const float gainLeft = velocityLevel * (pan > 0.0f ? 1.0f - pan : 1.0f), gainRight = velocityLevel * (pan < 0.0f ? 1.0f + pan : 1.0f);
                switch (zone->format)
                {
                    case SampleFormat::int16:
                        renderZone(tailLeft.data(), tailRight.data(), frames, params, hz, gainLeft, gainRight, Int16Numbers { static_cast<const std::int16_t*>(zone->data) });
                        break;
                    case SampleFormat::int24:
                        renderZone(tailLeft.data(), tailRight.data(), frames, params, hz, gainLeft, gainRight, Int24Numbers { static_cast<const std::uint8_t*>(zone->data) });
                        break;
                    case SampleFormat::float32:
                    default:
                        renderZone(tailLeft.data(), tailRight.data(), frames, params, hz, gainLeft, gainRight, FloatNumbers { static_cast<const float*>(zone->data) });
                        break;
                }
            }
            else
            {
                renderGenerated(tailLeft.data(), tailRight.data(), frames, params, std::min(std::max(0, static_cast<int>(std::lround(params.p[sample]))),
                                                                                         SampleBank::sampleCount - 1));
            }
            capturing = false;
            startPending = savedStart;
            position = savedPosition;
            played = savedPlayed;
            finished = savedFinished;
            tailLength = frames;
            riseLength = std::max(1, static_cast<int>(std::lround(riseSeconds * sampleRate)));
            riseIndex = 0;
        }

        void renderInstrument(float* left, float* right, int numSamples, const SlotParams& params, int instrument) noexcept
        {
            // A note takes its style when it starts and keeps it to its end, whatever the slot is set to meanwhile; its
            // count among the style's users keeps that memory from being reused (D155, D158).
            const LoadedStyle* playing = playingStyle;
            if (startPending)
            {
                const int styleIndex = juce::jlimit(0, maxInstrumentStyles - 1, static_cast<int>(std::lround(params.p[style])));
                playing = SampleLibrary::instance().request(instrument, styleIndex);
            }
            renderStyle(left, right, numSamples, params, playing, true);
        }

        /** A style in memory (an instrument's, or the user's one recording), or silence while there is none. An
            instrument's (`held`) is the note's own until it ends; the user's file is the one the slot names now. */
        void renderStyle(float* left, float* right, int numSamples, const SlotParams& params, const LoadedStyle* loaded, bool held = false) noexcept
        {
            const double hz = std::max(1.0, static_cast<double>(params.frequencyHz));

            if (startPending)
            {
                // A strike while the recording sounds: it fades out under the new start, read while its memory is
                // still this note's (an instrument's held style, or the user's file the slot still names).
                if (! finished && zone != nullptr && playingStyle != nullptr
                    && (held ? heldStyle.style == playingStyle : loaded == playingStyle))
                    captureTail(params);
                startPending = false;
                hold(held ? loaded : nullptr);
                playingStyle = loaded;
                finished = true;
                zone = nullptr;
                slicing = false;
                const int sliceMode = held ? 0 : juce::jlimit(0, 2, static_cast<int>(std::lround(params.p[slices])));
                if (loaded != nullptr && ! held && (loaded->kit || sliceMode > 0))
                {
                    // A slice of the user's file, or a kit's file, for this key (D207).
                    velocityLevel = loaded->velocityGain(noteVelocity);
                    const int index = noteNumber - juce::jlimit(0, 127, static_cast<int>(std::lround(params.p[firstKey])));
                    int from = 0, to = 0;
                    const SampleZone* chosen = nullptr;
                    if (loaded->kit)
                    {
                        if (index >= 0 && index < static_cast<int>(loaded->zones.size()))
                        {
                            chosen = &loaded->zones[static_cast<std::size_t>(index)];
                            to = chosen->frames;
                        }
                    }
                    else if (! loaded->zones.empty()
                             && sliceRange(*loaded, loaded->zones.front(), sliceMode,
                                           static_cast<int>(std::lround(params.p[count])) + fewestSlices, params.p[sense], index, from, to))
                        chosen = &loaded->zones.front();
                    if (chosen != nullptr && to - from > 1)
                    {
                        zone = chosen;
                        slicing = true;
                        backwards = params.p[reverse] >= 0.5f;
                        sliceFrom = static_cast<double>(from);
                        sliceTo = static_cast<double>(to);
                        const double skip = juce::jlimit(0.0, 1.0, static_cast<double>(params.p[start])) * (sliceTo - sliceFrom - 1.0);
                        position = backwards ? sliceTo - 1.0 - skip : sliceFrom + skip;
                        played = 0.0;
                        finished = false;
                    }
                }
                else if (loaded != nullptr)
                {
                    const float midi = static_cast<float>(69.0 + 12.0 * std::log2(hz / 440.0));
                    // The next take: the part's own count (no global, D162 rule 3); a generator alone counts for itself.
                    std::uint32_t& turn = params.takeTurn != nullptr ? *params.takeTurn : ownTurn;
                    zone = loaded->pick(midi, noteVelocity, turn++);
                    velocityLevel = loaded->velocityGain(noteVelocity);
                    if (zone != nullptr && zone->frames > 0)
                    {
                        position = juce::jlimit(0.0, static_cast<double>(zone->frames - 1), static_cast<double>(params.p[start]) * zone->frames);
                        finished = false;
                    }
                }
            }
            // The user's file was replaced or let go under this note: it stops rather than read freed memory.
            if (loaded != playingStyle)
            {
                zone = nullptr;
                finished = true;
            }

            // A balance, not a pan: the recording is stereo already.
            const float pan = juce::jlimit(-1.0f, 1.0f, params.pan);
            // The instrument's own dynamics: its recordings are levelled for a square law of velocity (an organ's for none).
            const float gainLeft = velocityLevel * (pan > 0.0f ? 1.0f - pan : 1.0f), gainRight = velocityLevel * (pan < 0.0f ? 1.0f + pan : 1.0f);

            if (finished || zone == nullptr)
            {
                hold(nullptr);
                for (int s = 0; s < numSamples; ++s)
                    emit(left, right, s, 0.0f, 0.0f, 0.0f, params);
                return;
            }

            // The recording as it is held (D155): 16-bit, packed 24-bit or float numbers, mono or stereo.
            switch (zone->format)
            {
                case SampleFormat::int16:
                    renderZone(left, right, numSamples, params, hz, gainLeft, gainRight, Int16Numbers { static_cast<const std::int16_t*>(zone->data) });
                    break;
                case SampleFormat::int24:
                    renderZone(left, right, numSamples, params, hz, gainLeft, gainRight, Int24Numbers { static_cast<const std::uint8_t*>(zone->data) });
                    break;
                case SampleFormat::float32:
                default:
                    renderZone(left, right, numSamples, params, hz, gainLeft, gainRight, FloatNumbers { static_cast<const float*>(zone->data) });
                    break;
            }
            if (finished)
                hold(nullptr); // the note has ended: its instrument's memory may be reused
        }

        struct FloatNumbers
        {
            const float* d;
            float operator()(std::ptrdiff_t i) const noexcept { return d[i]; }
        };
        struct Int16Numbers
        {
            const std::int16_t* d;
            float operator()(std::ptrdiff_t i) const noexcept { return static_cast<float>(d[i]); }
        };
        struct Int24Numbers
        {
            const std::uint8_t* d;
            float operator()(std::ptrdiff_t i) const noexcept
            {
                const std::uint8_t* p = d + i * 3;
                return static_cast<float>(static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) | (static_cast<std::uint32_t>(p[1]) << 16)
                                                                    | (static_cast<std::uint32_t>(p[2]) << 24)) >> 8);
            }
        };

        /** Counts this note among its style's users while it plays (D155): memory with users is never reused. */
        void hold(const LoadedStyle* style) noexcept
        {
            if (heldStyle.style == style)
                return;
            if (heldStyle.style != nullptr)
                heldStyle.style->users.fetch_sub(1, std::memory_order_acq_rel);
            heldStyle.style = style;
            if (style != nullptr)
                style->users.fetch_add(1, std::memory_order_acq_rel);
        }

        template <typename Numbers>
        void renderZone(float* left, float* right, int numSamples, const SlotParams& params, double hz,
                        float gainLeft, float gainRight, const Numbers& numbers) noexcept
        {
            // The pitch the recording sounds at: its note, and the cents the manifest says it is off it. A slice sounds at
            // its own speed on every key: only what moves the note's pitch (octave, fine, bend, glide, modulation) moves it.
            const double rootHz = slicing ? 440.0 * std::exp2((static_cast<double>(noteNumber) - 69.0) / 12.0)
                                          : 440.0 * std::exp2((static_cast<double>(zone->root) - 69.0 + 0.01 * static_cast<double>(zone->tuneCents)) / 12.0);
            const double step = juce::jlimit(1.0 / 64.0, 32.0, hz / rootHz) * zone->rate / sampleRate;
            const int cutoff = SincKernels::cutoffFor(step);
            const bool looping = params.p[loop] >= 0.5f && zone->loopEnd > 0;
            const double loopFrames = static_cast<double>(zone->loopEnd - zone->loopStart);
            const double fadeFrames = static_cast<double>(zone->loopFade);
            const double frames = static_cast<double>(zone->frames);
            const int channels = zone->channels;
            const float scale = zone->scale;

            std::array<float, SincKernels::points> w {};
            auto read = [&](double at, float& outLeft, float& outRight) noexcept
            {
                const int index = static_cast<int>(at);
                kernels->weights(cutoff, static_cast<float>(at - index), w.data());
                const std::ptrdiff_t first = static_cast<std::ptrdiff_t>(index - (SincKernels::points / 2 - 1)) * channels;
                float sumLeft = 0.0f, sumRight = 0.0f;
                if (channels == 2)
                    for (int k = 0; k < SincKernels::points; ++k)
                    {
                        sumLeft += numbers(first + 2 * k) * w[static_cast<std::size_t>(k)];
                        sumRight += numbers(first + 2 * k + 1) * w[static_cast<std::size_t>(k)];
                    }
                else
                {
                    for (int k = 0; k < SincKernels::points; ++k)
                        sumLeft += numbers(first + k) * w[static_cast<std::size_t>(k)];
                    sumRight = sumLeft;
                }
                outLeft = sumLeft * scale;
                outRight = sumRight * scale;
            };

            // A slice's fades: in over 1 ms, out over its last 4 ms (at most a third of it), in the recording's frames.
            const double fadeIn = std::max(1.0, 0.001 * zone->rate);
            const double fadeOut = std::max(1.0, std::min(0.004 * zone->rate, (sliceTo - sliceFrom) / 3.0));

            for (int s = 0; s < numSamples; ++s)
            {
                float valueLeft = 0.0f, valueRight = 0.0f;
                if (! finished && slicing)
                {
                    read(position, valueLeft, valueRight);
                    const double remaining = backwards ? position - sliceFrom : sliceTo - position;
                    const float gain = static_cast<float>(std::min(1.0, played / fadeIn) * std::clamp(remaining / fadeOut, 0.0, 1.0));
                    valueLeft *= gain;
                    valueRight *= gain;
                    played += step;
                    position += backwards ? -step : step;
                    if (backwards ? position < sliceFrom : position >= sliceTo)
                        finished = true;
                }
                else if (! finished)
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
                emit(left, right, s, valueLeft * gainLeft, valueRight * gainRight, 0.5f * velocityLevel * (valueLeft + valueRight), params);
            }
        }

        double sampleRate = 96000.0;
        const SampleBank* bank = nullptr;
        const SincKernels* kernels = nullptr;
        double position = 0.0; // in source frames
        bool startPending = false;
        bool finished = true;
        float noteVelocity = 1.0f;
        int noteNumber = 60;
        float velocityLevel = 1.0f; // the level this note's velocity gives it on a sampled instrument
        // A slice or a kit's file playing (D207): its frames, which way, how far it has come.
        bool slicing = false, backwards = false;
        bool armed = false; // the slot is set to slices or names a kit: a note about to start will be a slice
        double sliceFrom = 0.0, sliceTo = 0.0, played = 0.0;
        const LoadedStyle* playingStyle = nullptr; // the style in memory this note started on
        /** The style this note counts itself a user of. A copy of the generator holds nothing of its own. */
        struct Held
        {
            const LoadedStyle* style = nullptr;
            Held() = default;
            Held(const Held&) noexcept {}
            Held& operator=(const Held&) noexcept { return *this; }
        };
        Held heldStyle;
        std::uint32_t ownTurn = 0; // the take count when no part gives one (a generator tested on its own)
        const SampleZone* zone = nullptr;
        std::array<float, 4> ringLeft {}, ringRight {}, ringMono {};
        unsigned int cursor = 0;
        std::span<float> signalTrack;
        GeneratorLink input {};
        // A strike's fade (captureTail): what was sounding, read ahead, and how far emit() has faded it out.
        std::span<float> tailLeft, tailRight;
        int tailLength = 0, tailIndex = 0;
        int riseLength = 0, riseIndex = 0;
        bool capturing = false;
    };

    static_assert(Module<SampleGenerator>);
}
