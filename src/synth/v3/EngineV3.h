#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "synth/GlobalFxRack.h"
#include "Inserts.h"
#include "Part.h"

/*
    V3 engine (DESIGN_V3.md sections 2 and 7): two parts, then the effect rack, then the master
    gain and a soft clipper. The rack is the existing drive, lo-fi, phaser, flanger, chorus,
    delay, plate, compressor, EQ and pump chain.

    Parts. Each part is a whole patch with its own note layer and voices. The part mode says
    which of them the keys reach: A only, Layer (both on every key), Split (each inside its key
    range) or B only; with both playing, each also has a velocity range. A part's Transpose moves
    the keys it is sent, and the engine remembers what it sent for every key, so a release always
    ends the note that key started, whatever changed meanwhile. A part that the mode switches off
    lets go of its notes and rings out. Everything that is not a note reaches both parts.

    Voices. With both parts playing they share sixteen voices: each may use what its patch asks
    for (its polyphony, one voice for mono and legato, its unison count), is sure of eight, and
    gets more when the other asks for fewer.

    Inserts. Each part runs through three insert slots of its own before the mix (Inserts.h):
    any of the rack's ten effects, one per slot, so two parts can be treated differently.

    Mix. Each part has a level, a balance and an effects share: that share of the part passes
    through the rack, the rest joins after it, before the master gain. A sub layer set to 0
    stays out of the chorus and the plate.

    Tempo. Synced delays and pumps, in the rack and in the inserts, follow the host's tempo and
    song position while it plays; otherwise the arp's tempo and a beat count that starts again
    with the first note after silence.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int partCount = 2;

    struct EngineSettings
    {
        std::array<PartSettings, partCount> parts {};
        std::array<PartMix, partCount> mix {};
        std::array<std::array<InsertSettings, insertSlotCount>, partCount> inserts {};
        PartMode partMode = PartMode::aOnly;
        coolsynth::synth::DriveParametersV2 drive {};
        coolsynth::synth::PhaserParametersV2 phaser {};
        coolsynth::synth::ChorusParametersV2 chorus {};
        coolsynth::synth::DelayParametersV2 delay {};
        coolsynth::synth::ReverbParametersV2 reverb {};
        coolsynth::synth::LoFiParameters lofi {};
        coolsynth::synth::FlangerParameters flanger {};
        coolsynth::synth::EqParameters eq {};
        coolsynth::synth::PumpParameters pump {};
        coolsynth::synth::CompressorParametersV2 compressor {};
        float masterGainLinear = 1.0f;
    };

    class EngineV3 final
    {
    public:
        static constexpr double masterGainRampSeconds = 0.02;
        static constexpr int sharedVoices = Part::maxVoices;
        static constexpr int guaranteedVoices = sharedVoices / partCount;
        static constexpr float silentLevelDb = -60.0f; // a part level at or below this is off

        EngineV3();

        void prepare(double sampleRate, int maxBlockSize, int outputChannels);
        void releaseResources() noexcept {}
        void render(juce::AudioBuffer<float>& buffer,
                    std::span<const EngineMidiEvent> events,
                    const EngineSettings& settings,
                    const EngineTransportInfo& transport = {}) noexcept;
        void panic() noexcept;

        [[nodiscard]] int getLatencySamples() const noexcept { return parts[0]->latencySamples(); }
        /** The chord a part just learned, packed (see packLearnedChord), or 0. Audio thread; reading clears it. */
        [[nodiscard]] std::uint32_t consumeLearnedChord(int partIndex) noexcept
        {
            return parts[static_cast<std::size_t>(std::clamp(partIndex, 0, partCount - 1))]->consumeLearnedChord();
        }
        [[nodiscard]] Part& partForTesting(int index = 0) noexcept { return *parts[static_cast<std::size_t>(index)]; }
        [[nodiscard]] const InsertSlot& insertForTesting(int part, int slot) const noexcept
        {
            return inserts[static_cast<std::size_t>(part)][static_cast<std::size_t>(slot)];
        }

        /** The voices a part's patch asks for: its polyphony, one for mono and legato, its unison count. */
        [[nodiscard]] static int voicesWanted(const PartSettings& settings) noexcept
        {
            switch (settings.playMode)
            {
                case PlayMode::mono:
                case PlayMode::legato: return 1;
                case PlayMode::unison: return std::clamp(settings.unisonVoices, 2, Part::maxUnisonVoices);
                case PlayMode::poly:
                default:               return std::clamp(settings.polyphony, 1, Part::maxVoices);
            }
        }

        /** With both parts playing: what each may use of the sixteen. Each is sure of eight. */
        [[nodiscard]] static std::array<int, partCount> voiceLimits(const std::array<int, partCount>& wanted) noexcept
        {
            return { std::min(wanted[0], std::max(guaranteedVoices, sharedVoices - wanted[1])),
                     std::min(wanted[1], std::max(guaranteedVoices, sharedVoices - wanted[0])) };
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
        /** One part's gains for a block: into the effects and past them, left and right. */
        struct MixGains
        {
            float wetLeft = 0.0f, wetRight = 0.0f, dryLeft = 0.0f, dryRight = 0.0f;
            [[nodiscard]] bool anyDry() const noexcept { return dryLeft != 0.0f || dryRight != 0.0f; }
        };

        [[nodiscard]] static MixGains gainsFor(const PartMix& mix) noexcept;
        void routeEvents(std::span<const EngineMidiEvent> events, const EngineSettings& settings,
                         const std::array<bool, partCount>& active) noexcept;
        void pushEvent(int partIndex, const EngineMidiEvent& event) noexcept;
        void pushNote(int partIndex, EngineMidiEventType type, int note, float value, int offset) noexcept;

        std::array<std::unique_ptr<Part>, partCount> parts; // large: each a pool of voices with their slot arenas
        std::array<PartSettings, partCount> resolved {};   // the patch as this block plays it: voices, arp, tuning
        std::array<std::array<std::int8_t, 128>, partCount> sentNote {}; // per played key: the note the part was sent, or -1
        std::array<bool, partCount> wasActive { true, false };
        std::array<std::vector<EngineMidiEvent>, partCount> partEvents;
        std::array<int, partCount> partEventCount {};
        std::array<MixGains, partCount> lastGains {};
        bool mixPrimed = false;

        coolsynth::synth::GlobalFxRack fxRack;
        juce::AudioBuffer<float> work;       // the stereo bus the rack runs on
        std::array<juce::AudioBuffer<float>, partCount> partBuffers; // each part's voices, then through its inserts
        std::array<std::array<InsertSlot, insertSlotCount>, partCount> inserts;
        juce::AudioBuffer<float> dryBus;     // what goes past the rack
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;
        bool prepared = false;
        bool masterPrimed = false;
        double hostRate = 48000.0;
        double freeBeats = 0.0; // the beat count while no host position drives it
    };
}
