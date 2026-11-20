#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "ChipWaves.h"
#include "DspMath.h"
#include "Module.h"
#include "Wavetables.h"

/*
    The Chip generator (docs/design/CHIP_GENERATOR.md, D230): the sound chips of the NES, the Game Boy, the Commodore
    64 (SID 6581 and 8580), the AY-3-8910 and YM2149, and Konami's SCC in one slot, with the frame tables chip
    musicians drive them with. A synthesizer's take on the chips, not an emulator: every wave plays on every chip, and
    the chip brings its quirks: its pulse widths, its triangle's steps, its noise register and the pitches that
    register can play, its volume steps and their curve, its frame rate and its output filters.

    Fixed shapes are read from band-limited tables (ChipWaves.h) with a 4-point Hermite read at the band limit the
    pitch needs. The pulse and the noise registers are drawn as steps, each band-limited with the 4-point BLEP of the
    VA and Noise generators; so is every jump the frame tables make (a volume step, a drum's switch from noise to
    tone, a duty change, a hard restart). Raw turns all of that off and reads the full-band tables: aliasing as on a
    cheap emulator. The output runs generatorLatencySamples late like every generator, mono as the chips were.

    The sequences run at the frame rate from the note's start, a step every Speed frames; Decay is the chip's own
    volume envelope (the NES's clocked at four times the frame rate, the Game Boy's at 64 Hz, the SID's its decay
    stage with its sixteen rates and exponential steps, the others a step every few frames); Sweep is a pitch glide in
    the chip's sweep clock (the NES's twice the frame rate, the Game Boy's 128 Hz). Combine works as on the VA: Sync
    resets the phase at the generator before's wraps, Ring and AM multiply, FM moves the phase.
*/
namespace coolsynth::synth::v3
{
    class ChipGenerator final
    {
    public:
        enum Param : int { chip = 0, wave, duty, table, sequence, speed, depth, decay, sweep, frame, output, raw, paramCount };
        enum class Model : int { nes = 0, gameBoy, sid6581, sid8580, ay, ym, scc };
        enum class Wave : int { pulse = 0, triangle, saw, noise, noiseShort, triSaw, triPulse, sawPulse, triSawPulse, table, buzzSaw, buzzTriangle };
        enum class Sequence : int { off = 0, kick, snare, hat, tom, arpMajor, arpMinor, arpOctave, arpFifth, arpSeventh,
                                    dutySweep, pluck, swell, blip, vibrato, hardRestart };
        /** The noise shift registers: the NES's 15 bits (also the Game Boy's long mode), its 93-step short mode, the
            Game Boy's 7-bit mode, the AY's 17 bits, the SID's 23 with its 8-bit output. */
        enum class Register : int { nesLong = 0, nesShort, gameBoyShort, ay17, sid23 };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr int modelCount = 7;
        static constexpr int waveCount = 12;
        static constexpr int sequenceCount = 16;
        static constexpr float pulseLevel = 0.6f;     // a 50 % pulse swings +-0.6: about a saw's loudness
        static constexpr float noiseLevel = 0.5f;     // the 1-bit registers swing +-0.5, as the Noise generator's
        static constexpr float sidNoiseLevel = 0.87f; // the SID's 8-bit noise, as loud as the 1-bit ones
        static constexpr double externalFmCycles = 0.6;
        static constexpr double noiseStepsPerCycle = 93.0; // the register's clock per note period: Short plays the note

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "Chip", 1,
                {{ { "chip",   "Chip",     0.0f, 6.0f,  0.0f, Skew::linear, Unit::choice,  false, "NES|Game Boy|SID 6581|SID 8580|AY|YM|SCC" },
                   { "wave",   "Wave",     0.0f, 11.0f, 0.0f, Skew::linear, Unit::choice,  false,
                     "Pulse|Triangle|Saw|Noise|Noise Short|Tri+Saw|Tri+Pulse|Saw+Pulse|Tri+Saw+Pulse|Table|Buzz Saw|Buzz Triangle" },
                   { "duty",   "Duty",     0.0f, 1.0f,  0.5f, Skew::linear, Unit::percent, true },
                   { "table",  "Table",    0.0f, 9.0f,  0.0f, Skew::linear, Unit::choice,  false,
                     "Sine|Triangle|Saw|Square|Organ|Bass|Brass|Strings|Bell|Voice" },
                   { "seq",    "Sequence", 0.0f, 15.0f, 0.0f, Skew::linear, Unit::choice,  false,
                     "Off|Kick|Snare|Hat|Tom|Arp Major|Arp Minor|Arp Octave|Arp Fifth|Arp Seventh|Duty Sweep|Pluck|Swell|Blip|Vibrato|Hard Restart" },
                   { "speed",  "Speed",    1.0f, 16.0f, 1.0f, Skew::linear, Unit::choice,  false },
                   { "depth",  "Depth",    0.0f, 1.0f,  0.3f, Skew::linear, Unit::percent, true },
                   { "decay",  "Decay",    0.0f, 16.0f, 0.0f, Skew::linear, Unit::choice,  false, "Off|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16" },
                   { "sweep",  "Sweep",   -1.0f, 1.0f,  0.0f, Skew::linear, Unit::percent, true },
                   { "frame",  "Frame",    0.0f, 2.0f,  0.0f, Skew::linear, Unit::choice,  false, "Chip|50 Hz|60 Hz" },
                   { "output", "Output",   0.0f, 1.0f,  0.0f, Skew::linear, Unit::choice,  false, "Chip|Clean" },
                   { "raw",    "Raw",      0.0f, 1.0f,  0.0f, Skew::linear, Unit::choice,  false, "Off|On" } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return 2 * static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            bank = &ChipWaveBank::instance();
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            wrapTrack = memory.take(n);
            // Its own register start from the slot's seed (no global count: D162 rule 3).
            registerStart = ((memory.seed() + 1u) * 2654435761u) & 0x7FFFu;
            if (registerStart == 0)
                registerStart = 1;
            shift = registerStart;
            reset();
        }

        /** Back to the register it was prepared with: a defined start (D222). A note's reset keeps it running. */
        void reseed() noexcept { shift = registerStart; }

        void reset() noexcept
        {
            phase = 0.0;
            effectivePhase = 0.0;
            noisePhase = 0.0;
            accumulator.fill(0.0f);
            wrapRing.fill(-1.0f);
            cursor = 0;
            highPass = lowPass = 0.0f;
            primed = false;
            gain = 0.0f;
            notePending = true;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            input = {};
        }

        /** The frame tables, the chip's envelope and its sweep start again from the note's start. */
        void noteOn(const NoteContext&) noexcept { notePending = true; }
        /** A retrigger restarts them too, as a tracker's new note restarts its instrument. */
        void strike() noexcept { notePending = true; }
        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), wrapTrack.data() }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            readParameters(params);
            if (notePending)
            {
                startNote();
                notePending = false;
            }

            const double fs = sampleRate;
            const double frameHz = frameRate();
            frameStep = frameHz / fs;
            decayStep = decayClockHz(frameHz) / fs;
            sweepStep = sweepClockHz(frameHz) / fs;
            sweepPerTick = static_cast<float>(600.0 * std::pow(static_cast<double>(sweepAmount), 3.0) / sweepClockHz(frameHz));
            baseHz = std::max(1.0f, params.frequencyHz);

            // The block's own settings, and the jump they make from the last sample's.
            const float before = gain * valueNow();
            refresh();
            float blockJump = primed && ! rawMode ? gain * valueNow() - before : 0.0f;
            primed = true;
            setFilters();

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);
            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useSync = params.combine == Combine::sync && input.wraps != nullptr;
            const bool useFm = params.combine == Combine::fm && input.signal != nullptr && amount > 0.0f;
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            const double fmDepth = externalFmCycles * static_cast<double>(amount * amount);

            for (int s = 0; s < numSamples; ++s)
            {
                const unsigned int now = cursor & 3u;
                if (s == 0 && blockJump != 0.0f)
                    addStep(0.5f, blockJump);

                // The tone's phase, with its sync reset and its edges.
                const double offset = useFm ? fmDepth * static_cast<double>(input.signal[s]) : 0.0;
                const float syncTau = useSync ? input.wraps[s] : -1.0f;
                float wrapTau = -1.0f;
                const double from = effectivePhase;
                if (syncTau > 0.0f)
                {
                    const double reached = phase + dt * static_cast<double>(syncTau);
                    if (state.kind == Wave::pulse && ! rawMode)
                        pulseEdges(from, reached + offset - from, 0.0f, syncTau);
                    const float beforeReset = gain * toneValue(reached + offset);
                    phase = dt * (1.0 - static_cast<double>(syncTau));
                    if (isTone(state.kind) && ! rawMode)
                        addStep(syncTau, gain * toneValue(offset) - beforeReset);
                    if (state.kind == Wave::pulse && ! rawMode)
                        pulseEdges(offset, phase, syncTau, 1.0f);
                    wrapTau = syncTau;
                }
                else
                {
                    const double next = phase + dt;
                    if (next >= 1.0 && dt > 0.0)
                        wrapTau = static_cast<float>(juce::jlimit(1.0e-6, 1.0, (1.0 - phase) / dt));
                    phase = next - std::floor(next);
                    if (state.kind == Wave::pulse && ! rawMode)
                    {
                        double move = phase + offset - from;
                        move -= std::round(move); // the shortest way round: FM can move the phase back
                        if (! useFm)
                            move = dt;
                        pulseEdges(from, move, 0.0f, 1.0f);
                    }
                }
                effectivePhase = phase + offset;
                effectivePhase -= std::floor(effectivePhase);

                if (isNoise(state.kind))
                    runNoise();

                // The frame tables, the envelope and the sweep, each at its own clock.
                if (const float tau = tick(frameClock, frameStep); tau > 0.0f)
                    applyEvent(tau, [this] { nextFrame(); });
                if (sid())
                    runSidDecay();
                else if (const float tau = tick(decayClock, decayStep); tau > 0.0f && decayRate > 0)
                    applyEvent(tau, [this] { nextDecayTick(); });
                if (const float tau = tick(sweepClock, sweepStep); tau > 0.0f && sweepPerTick != 0.0f)
                {
                    sweepOffset = juce::jlimit(-96.0f, 96.0f, sweepOffset + sweepPerTick);
                    setPitch();
                }

                accumulator[now] += gain * valueNow();
                wrapRing[now] = wrapTau;

                // The sample two steps back can no longer change: hand it out.
                const unsigned int out = (cursor + 2u) & 3u;
                float value = accumulator[out];
                const float outWrap = wrapRing[out];
                accumulator[out] = 0.0f;
                ++cursor;

                if (filtered)
                {
                    if (highPassCoefficient > 0.0f)
                        value -= onePole(highPass, value, highPassCoefficient);
                    if (lowPassCoefficient > 0.0f)
                        value = onePole(lowPass, value, lowPassCoefficient);
                }
                if (useRing || useAm)
                {
                    const float m = input.signal[s];
                    value *= useRing ? 1.0f - amount + amount * m : 1.0f - 0.5f * amount * (1.0f - m);
                }
                left[s] = value * gainLeft;
                right[s] = value * gainRight;
                signalTrack[static_cast<std::size_t>(s)] = value;
                wrapTrack[static_cast<std::size_t>(s)] = outWrap;
            }
        }

        /** Steps a noise register once as the generator does. */
        static std::uint32_t stepRegister(std::uint32_t v, Register r) noexcept
        {
            switch (r)
            {
                case Register::nesLong:
                {
                    const std::uint32_t feedback = (v ^ (v >> 1u)) & 1u;
                    return ((v >> 1u) | (feedback << 14u)) & 0x7FFFu;
                }
                case Register::nesShort:
                {
                    const std::uint32_t feedback = (v ^ (v >> 6u)) & 1u;
                    return ((v >> 1u) | (feedback << 14u)) & 0x7FFFu;
                }
                case Register::gameBoyShort:
                {
                    // gbdev.io Pan Docs, Audio details: in short mode the feedback is also written to bit 7 (bit 6 once
                    // shifted), which makes it a 7-bit register.
                    const std::uint32_t feedback = (v ^ (v >> 1u)) & 1u;
                    const std::uint32_t shifted = ((v >> 1u) | (feedback << 14u)) & 0x7FFFu;
                    return (shifted & ~(1u << 6u)) | (feedback << 6u);
                }
                case Register::ay17:
                {
                    // Ayumi: bit 0 xor bit 3 into bit 16.
                    const std::uint32_t feedback = (v ^ (v >> 3u)) & 1u;
                    return ((v >> 1u) | (feedback << 16u)) & 0x1FFFFu;
                }
                case Register::sid23:
                {
                    // reSID: bit 22 xor bit 17 into bit 0.
                    const std::uint32_t feedback = ((v >> 22u) ^ (v >> 17u)) & 1u;
                    return ((v << 1u) | feedback) & 0x7FFFFFu;
                }
            }
            return v;
        }

        /** A register's output, -1 .. 1. */
        static float registerOutput(std::uint32_t v, Register r) noexcept
        {
            if (r != Register::sid23)
                return (v & 1u) != 0 ? -1.0f : 1.0f;
            // The SID reads eight of its bits: 22, 20, 16, 13, 11, 7, 4 and 2 (reSID's wave.h).
            const std::uint32_t byte = (((v >> 22u) & 1u) << 7u) | (((v >> 20u) & 1u) << 6u) | (((v >> 16u) & 1u) << 5u)
                                       | (((v >> 13u) & 1u) << 4u) | (((v >> 11u) & 1u) << 3u) | (((v >> 7u) & 1u) << 2u)
                                       | (((v >> 4u) & 1u) << 1u) | ((v >> 2u) & 1u);
            return static_cast<float>(byte) / 127.5f - 1.0f;
        }

        /** The pulse width the chip can play nearest the knob's. */
        static float quantiseDuty(Model m, float d) noexcept
        {
            d = juce::jlimit(0.0f, 1.0f, d);
            switch (m)
            {
                case Model::nes:
                case Model::gameBoy:
                {
                    constexpr std::array<float, 4> widths { 0.125f, 0.25f, 0.5f, 0.75f };
                    float best = widths[0];
                    for (const float w : widths)
                        if (std::abs(w - d) < std::abs(best - d))
                            best = w;
                    return best;
                }
                case Model::ay:
                case Model::ym:
                    return 0.5f; // squares only
                case Model::scc:
                    return juce::jlimit(1.0f / 32.0f, 31.0f / 32.0f, std::round(d * 32.0f) / 32.0f); // its 32-step memory
                case Model::sid6581:
                case Model::sid8580:
                    break;
            }
            return juce::jlimit(0.01f, 0.99f, std::round(d * 4096.0f) / 4096.0f); // the SID's 12 bits
        }

        /** The register clock nearest the wanted one that the chip can make. */
        static double noiseClock(Model m, Register r, double wanted) noexcept
        {
            wanted = std::max(1.0, wanted);
            switch (m)
            {
                case Model::nes:
                {
                    // nesdev.org, APU Noise: the NTSC periods in CPU cycles.
                    constexpr std::array<double, 16> periods { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
                    double best = 1789773.0 / periods[0];
                    for (const double p : periods)
                        if (std::abs(std::log(1789773.0 / p / wanted)) < std::abs(std::log(best / wanted)))
                            best = 1789773.0 / p;
                    return best;
                }
                case Model::gameBoy:
                {
                    // 262144 / (divider * 2^shift) Hz, the divider 0 counting as a half.
                    double best = 524288.0;
                    for (int shiftBits = 0; shiftBits <= 13; ++shiftBits)
                        for (int divider = 0; divider <= 7; ++divider)
                        {
                            const double hz = 262144.0 / ((divider == 0 ? 0.5 : divider) * std::exp2(shiftBits));
                            if (std::abs(std::log(hz / wanted)) < std::abs(std::log(best / wanted)))
                                best = hz;
                        }
                    return best;
                }
                case Model::ay:
                case Model::ym:
                case Model::scc:
                {
                    // fclock / (16 N), N 1..31: the ZX Spectrum 128's AY, the Atari ST's YM, the MSX's PSG for the SCC.
                    const double clock = m == Model::ay ? 1773400.0 : (m == Model::ym ? 2000000.0 : 1789773.0);
                    const double n = juce::jlimit(1.0, 31.0, std::round(clock / 16.0 / wanted));
                    return clock / 16.0 / n;
                }
                case Model::sid6581:
                case Model::sid8580:
                    break;
            }
            juce::ignoreUnused(r);
            return wanted; // the SID's noise follows its oscillator: any pitch
        }

        /** The register a chip uses for a noise wave. */
        static Register registerFor(Model m, Wave w) noexcept
        {
            if (w == Wave::noiseShort)
                return m == Model::gameBoy ? Register::gameBoyShort : Register::nesShort;
            switch (m)
            {
                case Model::nes:
                case Model::gameBoy: return Register::nesLong;
                case Model::sid6581:
                case Model::sid8580: return Register::sid23;
                case Model::ay:
                case Model::ym:
                case Model::scc: return Register::ay17;
            }
            return Register::nesLong;
        }

        /** For tests: the volume (0..15, the SID's 0..255) and the sequence step it is at. */
        [[nodiscard]] int volumeForTesting() const noexcept { return sid() ? sidLevel : decayLevel; }
        [[nodiscard]] int stepForTesting() const noexcept { return step; }
        [[nodiscard]] float gainForTesting() const noexcept { return gain; }

    private:
        struct State
        {
            Wave kind = Wave::pulse;  // what sounds now: the slot's wave, or a drum's noise frame
            float duty = 0.5f;        // as the chip plays it
            int frameA = 0, frameB = 0;
            float blend = 0.0f;
            float scale = 1.0f;       // the 6581's combined waves are weaker than the 8580's
        };

        static bool isNoise(Wave w) noexcept { return w == Wave::noise || w == Wave::noiseShort; }
        static bool isTone(Wave w) noexcept { return ! isNoise(w); }
        [[nodiscard]] bool sid() const noexcept { return model == Model::sid6581 || model == Model::sid8580; }

        void readParameters(const SlotParams& params) noexcept
        {
            model = static_cast<Model>(juce::jlimit(0, modelCount - 1, static_cast<int>(std::lround(params.p[chip]))));
            slotWave = static_cast<Wave>(juce::jlimit(0, waveCount - 1, static_cast<int>(std::lround(params.p[wave]))));
            dutyKnob = juce::jlimit(0.0f, 1.0f, params.p[duty]);
            tableIndex = juce::jlimit(0, ChipWaveBank::tableWaves - 1, static_cast<int>(std::lround(params.p[table])));
            sequenceType = static_cast<Sequence>(juce::jlimit(0, sequenceCount - 1, static_cast<int>(std::lround(params.p[sequence]))));
            speedFrames = juce::jlimit(1, 16, static_cast<int>(std::lround(params.p[speed])));
            depthAmount = juce::jlimit(0.0f, 1.0f, params.p[depth]);
            decayRate = juce::jlimit(0, 16, static_cast<int>(std::lround(params.p[decay])));
            sweepAmount = juce::jlimit(-1.0f, 1.0f, params.p[sweep]);
            frameMode = juce::jlimit(0, 2, static_cast<int>(std::lround(params.p[frame])));
            chipOutput = std::lround(params.p[output]) == 0;
            rawMode = std::lround(params.p[raw]) != 0;
        }

        void startNote() noexcept
        {
            step = 0;
            framesInStep = 0;
            frameClock = decayClock = sweepClock = 0.0;
            decayLevel = 15;
            decayCount = 0;
            sidLevel = 255;
            sidCounter = 0.0;
            sweepOffset = 0.0f;
        }

        // The frame rates (Frame: the chip's own, 50 or 60 Hz): NTSC NES 60.0988, the Game Boy 59.7275, the PAL C64
        // 50.1245, the ZX Spectrum 128 50.021, the Atari ST 50.0, an NTSC MSX 59.922.
        [[nodiscard]] double frameRate() const noexcept
        {
            if (frameMode == 1)
                return 50.0;
            if (frameMode == 2)
                return 60.0;
            switch (model)
            {
                case Model::nes: return 60.0988;
                case Model::gameBoy: return 59.7275;
                case Model::sid6581:
                case Model::sid8580: return 50.1245;
                case Model::ay: return 50.021;
                case Model::ym: return 50.0;
                case Model::scc: return 59.922;
            }
            return 60.0;
        }

        [[nodiscard]] double decayClockHz(double frameHz) const noexcept
        {
            if (model == Model::nes)
                return 4.0 * frameHz; // the quarter frames
            if (model == Model::gameBoy)
                return 64.0;
            return frameHz;
        }

        [[nodiscard]] double sweepClockHz(double frameHz) const noexcept
        {
            if (model == Model::nes)
                return 2.0 * frameHz; // the half frames
            if (model == Model::gameBoy)
                return 128.0;
            return frameHz;
        }

        static float tick(double& clock, double stepPerSample) noexcept
        {
            clock += stepPerSample;
            if (clock < 1.0 || stepPerSample <= 0.0)
                return -1.0f;
            clock -= std::floor(clock);
            return static_cast<float>(juce::jlimit(1.0e-6, 1.0, 1.0 - clock / stepPerSample));
        }

        /** A change at time tau in this sample: the jump it makes is band-limited (unless Raw). */
        template <typename Change>
        void applyEvent(float tau, Change&& change) noexcept
        {
            const float before = gain * valueNow();
            change();
            const float after = gain * valueNow();
            if (! rawMode && after != before)
                addStep(tau, after - before);
        }

        void nextFrame() noexcept
        {
            if (++framesInStep < speedFrames)
                return;
            framesInStep = 0;
            ++step;
            refresh();
            if (stepState(step).resetPhase)
            {
                phase = 0.0;
                effectivePhase = 0.0;
            }
        }

        void nextDecayTick() noexcept
        {
            if (++decayCount < decayRate)
                return;
            decayCount = 0;
            if (decayLevel > 0)
                --decayLevel;
            refreshGain();
        }

        /** The SID's decay stage (reSID's envelope.cc): a step down every rate period, longer at the low levels
            (its exponential counter), at the PAL C64's clock. */
        void runSidDecay() noexcept
        {
            if (decayRate <= 0 || sidLevel <= 0)
                return;
            constexpr std::array<double, 16> ratePeriods { 9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251 };
            sidCounter += 985248.0 / sampleRate;
            bool changed = false;
            for (int guard = 0; guard < 8 && sidLevel > 0; ++guard)
            {
                const int exponential = sidLevel > 93 ? 1 : sidLevel > 54 ? 2 : sidLevel > 26 ? 4 : sidLevel > 14 ? 8 : sidLevel > 6 ? 16 : 30;
                const double period = ratePeriods[static_cast<std::size_t>(decayRate - 1)] * exponential;
                if (sidCounter < period)
                    break;
                sidCounter -= period;
                --sidLevel;
                changed = true;
            }
            if (changed)
                refreshGain(); // a 256th at a time: below a click
        }

        struct StepState
        {
            Wave kind = Wave::pulse;
            float pitch = 0.0f;   // semitones over the note
            int volume = 15;
            float dutyOffset = 0.0f;
            bool resetPhase = false;
        };

        /** What the sequence plays at a step (each step Speed frames long). */
        [[nodiscard]] StepState stepState(int k) const noexcept
        {
            StepState st;
            st.kind = slotWave;
            const Wave noiseFrame = isNoise(slotWave) ? slotWave : Wave::noise;
            const float drop = depthAmount * 48.0f;
            switch (sequenceType)
            {
                case Sequence::off:
                    break;
                case Sequence::kick:
                    if (k == 0)
                    {
                        st.kind = noiseFrame;
                        st.pitch = 24.0f;
                    }
                    else
                    {
                        st.pitch = drop * std::max(0.0f, 1.0f - static_cast<float>(k - 1) / 7.0f);
                        st.volume = k == 1 ? 15 : std::max(0, 15 - 2 * (k - 1));
                    }
                    break;
                case Sequence::snare:
                    if (k == 1)
                    {
                        st.pitch = 0.5f * drop;
                        st.volume = 14;
                    }
                    else
                    {
                        st.kind = noiseFrame;
                        st.pitch = 36.0f;
                        st.volume = k == 0 ? 15 : std::max(0, 12 - (k - 2));
                    }
                    break;
                case Sequence::hat:
                {
                    const int length = 3 + static_cast<int>(std::lround(depthAmount * 12.0f));
                    st.kind = noiseFrame;
                    st.pitch = 48.0f;
                    st.volume = std::max(0, static_cast<int>(std::lround(12.0f * (1.0f - static_cast<float>(k) / static_cast<float>(length)))));
                    break;
                }
                case Sequence::tom:
                    if (k == 0)
                    {
                        st.kind = noiseFrame;
                        st.pitch = 24.0f;
                    }
                    else
                    {
                        st.pitch = drop * std::max(0.0f, 1.0f - static_cast<float>(k - 1) / 14.0f);
                        st.volume = std::max(0, 15 - (k - 1));
                    }
                    break;
                case Sequence::arpMajor:
                case Sequence::arpMinor:
                case Sequence::arpOctave:
                case Sequence::arpFifth:
                case Sequence::arpSeventh:
                {
                    static constexpr std::array<std::array<int, 4>, 5> chords { { { 0, 4, 7, 0 }, { 0, 3, 7, 0 }, { 0, 12, 0, 0 },
                                                                                  { 0, 7, 0, 0 }, { 0, 4, 7, 10 } } };
                    static constexpr std::array<int, 5> sizes { 3, 3, 2, 2, 4 };
                    const auto c = static_cast<std::size_t>(static_cast<int>(sequenceType) - static_cast<int>(Sequence::arpMajor));
                    const int n = sizes[c];
                    const int octaves = 1 + static_cast<int>(depthAmount * 2.999f);
                    const int i = k % (n * octaves);
                    st.pitch = static_cast<float>(chords[c][static_cast<std::size_t>(i % n)] + 12 * (i / n));
                    break;
                }
                case Sequence::dutySweep:
                {
                    const int u = k % 16;
                    const float triangle = u < 8 ? static_cast<float>(u) / 8.0f : static_cast<float>(16 - u) / 8.0f;
                    st.dutyOffset = depthAmount * 0.45f * (2.0f * triangle - 1.0f);
                    break;
                }
                case Sequence::pluck:
                    st.volume = std::max(static_cast<int>(std::lround(15.0f * (1.0f - depthAmount))), 15 - k);
                    break;
                case Sequence::swell:
                    st.volume = std::min(15, static_cast<int>(std::lround(15.0f * (1.0f - depthAmount))) + k);
                    break;
                case Sequence::blip:
                    st.pitch = k == 0 ? std::round(depthAmount * 24.0f) : 0.0f;
                    break;
                case Sequence::vibrato:
                    if (k >= 12)
                    {
                        static constexpr std::array<float, 8> shape { 0.0f, 0.7f, 1.0f, 0.7f, 0.0f, -0.7f, -1.0f, -0.7f };
                        st.pitch = depthAmount * 2.0f * shape[static_cast<std::size_t>((k - 12) % 8)];
                    }
                    break;
                case Sequence::hardRestart:
                    st.volume = k == 0 ? 0 : 15;
                    st.resetPhase = k == 1;
                    break;
            }
            return st;
        }

        /** The state, the volume and the pitch from the knobs, the sequence's step and the envelope. */
        void refresh() noexcept
        {
            const StepState st = stepState(step);
            stepVolume = st.volume;
            stepPitch = st.pitch;
            state.kind = st.kind;
            state.duty = quantiseDuty(model, dutyKnob + st.dutyOffset);
            state.scale = 1.0f;
            state.blend = 0.0f;
            switch (state.kind)
            {
                case Wave::triangle:
                    state.frameA = model == Model::nes ? (chipOutput ? ChipWaveBank::triangleNes : ChipWaveBank::triangle4)
                                                       : (sid() ? ChipWaveBank::triangleSmooth : ChipWaveBank::triangle4);
                    break;
                case Wave::saw:
                    state.frameA = sid() ? ChipWaveBank::sawSmooth : (model == Model::scc ? ChipWaveBank::saw8 : ChipWaveBank::saw4);
                    break;
                case Wave::triSaw:
                    state.frameA = ChipWaveBank::triSaw;
                    break;
                case Wave::triPulse:
                case Wave::sawPulse:
                case Wave::triSawPulse:
                {
                    const int base = state.kind == Wave::triPulse ? ChipWaveBank::triPulse
                                     : state.kind == Wave::sawPulse ? ChipWaveBank::sawPulse : ChipWaveBank::triSawPulse;
                    const float position = juce::jlimit(0.0f, static_cast<float>(ChipWaveBank::widths - 1),
                                                        state.duty * static_cast<float>(ChipWaveBank::widths) - 0.5f);
                    const int lower = std::min(ChipWaveBank::widths - 2, static_cast<int>(position));
                    state.frameA = base + lower;
                    state.blend = position - static_cast<float>(lower);
                    break;
                }
                case Wave::table:
                    state.frameA = (model == Model::scc ? ChipWaveBank::table8 : ChipWaveBank::table4) + tableIndex;
                    break;
                case Wave::buzzSaw:
                    state.frameA = model == Model::ym ? ChipWaveBank::buzzSawYm : ChipWaveBank::buzzSawAy;
                    break;
                case Wave::buzzTriangle:
                    state.frameA = model == Model::ym ? ChipWaveBank::buzzTriangleYm : ChipWaveBank::buzzTriangleAy;
                    break;
                case Wave::pulse:
                case Wave::noise:
                case Wave::noiseShort:
                    break;
            }
            state.frameB = state.frameA + 1;
            if (model == Model::sid6581 && (state.kind == Wave::triSaw || state.kind == Wave::triPulse || state.kind == Wave::sawPulse
                                            || state.kind == Wave::triSawPulse))
                state.scale = 0.5f;
            if (isNoise(state.kind))
            {
                const Register r = registerFor(model, state.kind);
                if (r != noiseRegister)
                {
                    noiseRegister = r;
                    const std::uint32_t mask = r == Register::sid23 ? 0x7FFFFFu : (r == Register::ay17 ? 0x1FFFFu : 0x7FFFu);
                    if ((shift & mask) == 0)
                        shift = 1;
                    noiseValue = registerOutput(shift, r) * (r == Register::sid23 ? sidNoiseLevel : noiseLevel);
                }
            }
            refreshGain();
            setPitch();
        }

        void refreshGain() noexcept
        {
            if (sid())
            {
                gain = (static_cast<float>(sidLevel) / 255.0f) * (static_cast<float>(stepVolume) / 15.0f);
                return;
            }
            const int v = juce::jlimit(0, 15, static_cast<int>(std::lround(static_cast<float>(decayLevel * stepVolume) / 15.0f)));
            gain = levelOf(v);
        }

        /** A 4-bit volume as the chip plays it: the NES's triangle has none, its other channels go through its mixer's
            curve, the AY and the YM through their logarithmic DACs (Output: Chip); every other a straight line. */
        [[nodiscard]] float levelOf(int v) const noexcept
        {
            if (v <= 0)
                return 0.0f;
            if (model == Model::nes && state.kind == Wave::triangle)
                return 1.0f;
            if (! chipOutput)
                return static_cast<float>(v) / 15.0f;
            switch (model)
            {
                case Model::nes:
                    if (isNoise(state.kind))
                        return static_cast<float>((159.79 / (12241.0 / v + 100.0)) / (159.79 / (12241.0 / 15.0 + 100.0)));
                    return static_cast<float>((95.88 / (8128.0 / v + 100.0)) / (95.88 / (8128.0 / 15.0 + 100.0)));
                case Model::ay: return ChipWaveBank::ayLevels()[2 * v + 1];
                case Model::ym: return ChipWaveBank::ymLevels()[2 * v + 1];
                case Model::gameBoy:
                case Model::sid6581:
                case Model::sid8580:
                case Model::scc:
                    break;
            }
            return static_cast<float>(v) / 15.0f;
        }

        void setPitch() noexcept
        {
            const double hz = juce::jlimit(1.0, 0.45 * sampleRate, static_cast<double>(baseHz) * std::exp2((stepPitch + sweepOffset) / 12.0));
            dt = hz / sampleRate;
            bandLevel = rawMode ? 0 : WavetableBank::levelFor(hz, sampleRate);
            if (isNoise(state.kind))
            {
                const double perCycle = noiseRegister == Register::gameBoyShort ? 127.0 : noiseStepsPerCycle;
                noiseStep = std::min(8.0, noiseClock(model, noiseRegister, hz * perCycle) / sampleRate);
            }
        }

        void setFilters() noexcept
        {
            // Output: Chip. The NES: 90 Hz high-pass, 14 kHz low-pass (nesdev.org, APU Mixer). The Game Boy: its
            // capacitor's 0.999958 a clock at 4.19 MHz, about 28 Hz (Pan Docs). The C64: reSID's external filter,
            // 1/RC = 100 and 100000 rad/s.
            double hp = 0.0, lp = 0.0;
            if (chipOutput)
                switch (model)
                {
                    case Model::nes: hp = 90.0; lp = 14000.0; break;
                    case Model::gameBoy: hp = -std::log(0.999958) * 4194304.0 / juce::MathConstants<double>::twoPi; break;
                    case Model::sid6581:
                    case Model::sid8580: hp = 100.0 / juce::MathConstants<double>::twoPi; lp = 100000.0 / juce::MathConstants<double>::twoPi; break;
                    case Model::ay:
                    case Model::ym:
                    case Model::scc: break;
                }
            highPassCoefficient = hp > 0.0 ? coefficient(hp) : 0.0f;
            lowPassCoefficient = lp > 0.0 && lp < 0.45 * sampleRate ? coefficient(lp) : 0.0f;
            filtered = highPassCoefficient > 0.0f || lowPassCoefficient > 0.0f;
        }

        [[nodiscard]] float coefficient(double hz) const noexcept
        {
            const double g = std::tan(juce::MathConstants<double>::pi * hz / sampleRate);
            return static_cast<float>(g / (1.0 + g));
        }

        /** Trapezoidal one-pole low-pass; coefficient is g / (1 + g). */
        static float onePole(float& z, float x, float c) noexcept
        {
            const float v = (x - z) * c;
            const float y = v + z;
            z = y + v;
            return y;
        }

        /** The wave as it sounds at the current phase, before the volume. */
        [[nodiscard]] float valueNow() const noexcept
        {
            return isNoise(state.kind) ? noiseValue : toneValue(effectivePhase);
        }

        [[nodiscard]] float toneValue(double at) const noexcept
        {
            at -= std::floor(at);
            if (state.kind == Wave::pulse)
                return at < static_cast<double>(state.duty) ? 2.0f * pulseLevel * (1.0f - state.duty) : -2.0f * pulseLevel * state.duty;
            if (isNoise(state.kind))
                return noiseValue;
            float v = hermite(bank->frame(state.frameA, bandLevel), at);
            if (state.blend > 0.0f)
                v += (hermite(bank->frame(state.frameB, bandLevel), at) - v) * state.blend;
            return v * state.scale;
        }

        static float hermite(const float* f, double at) noexcept
        {
            const double x = at * ChipWaveBank::tableSize;
            const int i = std::min(ChipWaveBank::tableSize - 1, static_cast<int>(x));
            const float t = static_cast<float>(x - i);
            const float xm1 = f[i - 1], x0 = f[i], x1 = f[i + 1], x2 = f[i + 2];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }

        /** The pulse's edges as its phase moves by `move` from `from`, over the part of the sample from tau0 to tau1. */
        void pulseEdges(double from, double move, float tau0, float tau1) noexcept
        {
            if (move == 0.0)
                return;
            const float jump = 2.0f * pulseLevel * gain;
            edgesThrough(from, move, jump, tau0, tau1);                                       // the cycle's start: low to high
            edgesThrough(from - static_cast<double>(state.duty), move, -jump, tau0, tau1);    // the width: high to low
        }

        void edgesThrough(double start, double move, float jumpForward, float tau0, float tau1) noexcept
        {
            const float span = tau1 - tau0;
            if (move > 0.0)
            {
                for (double k = std::floor(start) + 1.0; k <= start + move; k += 1.0)
                    addStep(juce::jlimit(1.0e-6f, 1.0f, tau0 + span * static_cast<float>((k - start) / move)), jumpForward);
            }
            else
            {
                for (double k = std::floor(start); k > start + move; k -= 1.0)
                    addStep(juce::jlimit(1.0e-6f, 1.0f, tau0 + span * static_cast<float>((start - k) / -move)), -jumpForward);
            }
        }

        void runNoise() noexcept
        {
            const float scale = noiseRegister == Register::sid23 ? sidNoiseLevel : noiseLevel;
            float level = noiseValue;
            noisePhase += noiseStep;
            while (noisePhase >= 1.0)
            {
                noisePhase -= 1.0;
                shift = stepRegister(shift, noiseRegister);
                const float next = registerOutput(shift, noiseRegister) * scale;
                if (next != level)
                {
                    if (! rawMode)
                        addStep(static_cast<float>(juce::jlimit(1.0e-6, 1.0, 1.0 - noisePhase / noiseStep)), (next - level) * gain);
                    level = next;
                }
            }
            noiseValue = level;
        }

        void addStep(float tau, float jump) noexcept
        {
            for (unsigned int j = 0; j < 4u; ++j)
            {
                const float x = static_cast<float>(static_cast<int>(j) - 1) - tau;
                accumulator[(cursor + 2u + j) & 3u] += jump * dsp::blepResidual(x);
            }
        }

        const ChipWaveBank* bank = nullptr;
        double sampleRate = 96000.0;

        // The knobs, read each block.
        Model model = Model::nes;
        Wave slotWave = Wave::pulse;
        float dutyKnob = 0.5f, depthAmount = 0.3f, sweepAmount = 0.0f;
        int tableIndex = 0, speedFrames = 1, decayRate = 0, frameMode = 0;
        Sequence sequenceType = Sequence::off;
        bool chipOutput = true, rawMode = false;

        // What sounds.
        State state;
        float gain = 0.0f;
        int stepVolume = 15;
        float stepPitch = 0.0f, sweepOffset = 0.0f, sweepPerTick = 0.0f, baseHz = 440.0f;
        double phase = 0.0, effectivePhase = 0.0, dt = 0.0;
        int bandLevel = 0;
        bool primed = false, notePending = true;

        // The frame engine.
        double frameClock = 0.0, decayClock = 0.0, sweepClock = 0.0;
        double frameStep = 0.0, decayStep = 0.0, sweepStep = 0.0;
        int step = 0, framesInStep = 0, decayLevel = 15, decayCount = 0, sidLevel = 255;
        double sidCounter = 0.0;

        // The noise register.
        Register noiseRegister = Register::nesLong;
        std::uint32_t shift = 1, registerStart = 1;
        double noisePhase = 0.0, noiseStep = 0.0;
        float noiseValue = noiseLevel;

        // The output.
        std::array<float, 4> accumulator {}, wrapRing {};
        unsigned int cursor = 0;
        float highPass = 0.0f, lowPass = 0.0f;
        float highPassCoefficient = 0.0f, lowPassCoefficient = 0.0f;
        bool filtered = false;
        std::span<float> signalTrack, wrapTrack;
        GeneratorLink input {};
    };

    static_assert(Module<ChipGenerator>);
}
