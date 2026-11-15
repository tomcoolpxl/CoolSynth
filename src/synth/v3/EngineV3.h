#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SoundUnit.h"

/*
    V3 engine (DESIGN_V3.md sections 2, 7 and 8; decision D158): sound units, summed, then a soft
    clipper. A sound unit (SoundUnit.h) is a whole sound: two parts, their insert slots, the master
    effect slots F1..F6, the mix and the master gain.

    A preset change while notes still sound works as on a Korg Kronos or a Yamaha Montage: the notes
    held, on the keys or the pedal, go on with the sound they started with, its effects, modulators
    and mix included, and only new notes take the new preset. The engine keeps four units: the one
    being played, up to two earlier sounds still ringing, and one fading out. An earlier sound
    hears only what ends or bends its notes: the release of a key it took, the sustain pedal, the
    pitch bend, All Notes Off; the mod wheel, pressure and expression reach only the sound being
    played. Five seconds after its last key and the pedal are up, an earlier sound fades out over
    half a second; so does the oldest when a third earlier sound would ring, or when the processor
    is short of time. A sound silent for a quarter of a second ends at once. All Sound Off ends
    every earlier sound.
*/
namespace coolsynth::synth::v3
{
    class EngineV3 final
    {
    public:
        static constexpr double masterGainRampSeconds = SoundUnit::masterGainRampSeconds;
        static constexpr int sharedVoices = SoundUnit::sharedVoices;
        static constexpr int guaranteedVoices = SoundUnit::guaranteedVoices;
        static constexpr float silentLevelDb = SoundUnit::silentLevelDb;

        static constexpr int unitCount = 4;            // the sound played, two earlier ones, one fading out
        static constexpr int earlierSoundsKept = 2;    // like the Montage (the user's choice, D158)
        static constexpr double holdSeconds = 5.0;     // after an earlier sound's keys and pedal are up (the Kronos default)
        static constexpr double fadeSeconds = 0.5;     // its fade, and the oldest's when room or time runs out
        static constexpr double quietSeconds = 0.25;   // silent this long: it ends at once
        static constexpr float quietLevel = 1.0e-5f;   // -100 dB
        static constexpr float overloadShare = 0.75f;  // of the block's time: the oldest earlier sound gives way

        using StageTiming = v3::StageTiming;

        EngineV3();

        void prepare(double sampleRate, int maxBlockSize, int outputChannels);
        void releaseResources() noexcept {}
        void render(juce::AudioBuffer<float>& buffer,
                    std::span<const EngineMidiEvent> events,
                    const EngineSettings& settings,
                    const EngineTransportInfo& transport = {}) noexcept;
        void panic() noexcept;

        /** The share of its time the last block took, 0 to 1 or more: an earlier sound gives way when it is high. */
        void setLoad(float shareOfBlockTime) noexcept { load = shareOfBlockTime; }

        [[nodiscard]] int getLatencySamples() const noexcept { return units[0]->latencySamples(); }

        // ---- For the flight recorder's tripwires (diagnostics/FlightRecorder.h), audio thread ----------------
        /** The slowest stage of the last block. */
        [[nodiscard]] const StageTiming& slowestStage() const noexcept { return slowest; }
        [[nodiscard]] int activeVoiceCount() const noexcept
        {
            int voices = 0;
            for (const auto& unit : units)
                voices += unit->activeVoiceCount();
            return voices;
        }
        /** Samples since the last call that went into the output's soft clipper (above its knee), then zero. */
        [[nodiscard]] int consumeClippedSamples() noexcept { return std::exchange(clippedSamples, 0); }
        /** The chord a part of the sound being played just learned, packed (see packLearnedChord), or 0. Reading clears it. */
        [[nodiscard]] std::uint32_t consumeLearnedChord(int partIndex) noexcept { return units[static_cast<std::size_t>(current)]->consumeLearnedChord(partIndex); }

        /** Earlier sounds ringing now (0 to earlierSoundsKept, plus one fading). */
        [[nodiscard]] int earlierSoundsRinging() const noexcept;
        /** The sound count (PartSettings::soundGeneration) of the oldest sound still sounding: what an older sound
            used, such as the user's files, may be let go. */
        [[nodiscard]] std::uint32_t oldestSoundingGeneration() const noexcept;

        // The sound being played.
        [[nodiscard]] Part& partForTesting(int index = 0) noexcept { return units[static_cast<std::size_t>(current)]->part(index); }
        [[nodiscard]] const InsertSlot& insertForTesting(int part, int slot) const noexcept { return units[static_cast<std::size_t>(current)]->insert(part, slot); }
        [[nodiscard]] const InsertSlot& masterSlotForTesting(int slot) const noexcept { return units[static_cast<std::size_t>(current)]->masterSlot(slot); }
        /** Any unit, 0 to unitCount - 1. */
        [[nodiscard]] SoundUnit& unitForTesting(int index) noexcept { return *units[static_cast<std::size_t>(index)]; }
        [[nodiscard]] int currentUnitForTesting() const noexcept { return current; }

        /** True while a sampled instrument that a playing part names is still being read into memory (one that is
            not installed does not count). Asking is what starts the read. Audio thread, never waits. */
        [[nodiscard]] static bool sampledInstrumentPending(const EngineSettings& settings) noexcept;

        [[nodiscard]] static int voicesWanted(const PartSettings& settings) noexcept { return SoundUnit::voicesWanted(settings); }
        [[nodiscard]] static std::array<int, partCount> voiceLimits(const std::array<int, partCount>& wanted) noexcept
        {
            return SoundUnit::voiceLimits(wanted);
        }

        /** Above 0.8 the output bends into a tanh knee and never passes 1. */
        static float softClip(float x) noexcept
        {
            constexpr float threshold = 0.8f;
            const float magnitude = std::abs(x);
            if (magnitude <= threshold)
                return x;
            return std::copysign(threshold + (1.0f - threshold) * std::tanh((magnitude - threshold) / (1.0f - threshold)), x);
        }

    private:
        enum class Phase : std::uint8_t { idle, playing, ringing, fading };

        struct UnitState
        {
            Phase phase = Phase::idle;
            std::uint32_t generation = 0; // the sound count it plays
            std::uint64_t order = 0;      // when it became an earlier sound: the oldest gives way first
            int keysHeld = 0;             // keys it took that are still down
            bool pedalDown = false;
            double releasedSeconds = 0.0; // since its keys and pedal came up
            double quietSeconds = 0.0;    // since it last made a sound
            float fade = 1.0f;
            // an earlier sound's settings, as they were when the preset changed (on the heap: tests keep engines on the stack)
            std::unique_ptr<EngineSettings> settings = std::make_unique<EngineSettings>();
        };

        /** One block no longer than the prepared size. */
        void renderBlock(juce::AudioBuffer<float>& buffer, std::span<const EngineMidiEvent> events,
                         const EngineSettings& settings, const EngineTransportInfo& transport) noexcept;
        void followSoundChange(const EngineSettings& settings) noexcept;
        void routeEvents(std::span<const EngineMidiEvent> events) noexcept;
        void pushEvent(int unit, const EngineMidiEvent& event) noexcept;
        void startFading(int unit) noexcept;
        void endUnit(int unit) noexcept;
        [[nodiscard]] int oldestRinging() const noexcept;

        std::array<std::unique_ptr<SoundUnit>, unitCount> units;
        std::array<UnitState, unitCount> state {};
        std::array<std::vector<EngineMidiEvent>, unitCount> unitEvents;
        std::array<int, unitCount> unitEventCount {};
        std::array<std::int8_t, 128> keyOwner {}; // per key: the unit that took its note-on, or -1
        int current = 0;
        std::uint64_t changes = 0;
        std::uint32_t generation = 0;
        bool generationPrimed = false;
        std::unique_ptr<EngineSettings> lastSettings = std::make_unique<EngineSettings>();   // the last block's
        std::unique_ptr<EngineSettings> compareScratch = std::make_unique<EngineSettings>(); // a change compared with it

        // Where the pedal, the bend and the wheels are: a unit that starts playing a new sound is told.
        struct HeldController
        {
            EngineMidiEvent event {};
            bool seen = false;
        };
        static constexpr std::size_t controllerSustain = 0, controllerBend = 1, controllerWheel = 2,
                                     controllerPressure = 3, controllerExpression = 4;
        std::array<HeldController, 5> controllers {};
        bool controllersToCurrent = false;

        std::vector<EngineMidiEvent> pieceEvents; // a longer host block's events, one piece at a time
        juce::AudioBuffer<float> sum;             // every unit added, before the soft clipper
        bool prepared = false;
        float load = 0.0f;
        StageTiming slowest;      // flight recorder: the last block's slowest stage
        int clippedSamples = 0;   // flight recorder: samples into the soft clipper's knee since last asked
        double hostRate = 48000.0;
    };
}
