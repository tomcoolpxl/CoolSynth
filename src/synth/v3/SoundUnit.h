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
    One whole sound (decision D158): the two parts with their voices, their three insert slots each,
    the six master effect slots F1..F6, the mix and the master gain, everything a preset sets. The
    engine (EngineV3.h) keeps four: the sound being played and, after a preset change, up to two
    earlier ones that ring on with everything they had, and one fading out. Each renders on its own
    and is added into the engine's sum before the soft clipper.

    Parts. Each part is a whole patch with its own note layer and voices. The part mode says
    which of them the keys reach: A only, Layer (both on every key), Split (each inside its key
    range) or B only; with both playing, each also has a velocity range. A part's Transpose moves
    the keys it is sent, and the unit remembers what it sent for every key, so a release always
    ends the note that key started, whatever changed meanwhile. A part that the mode switches off
    lets go of its notes and rings out. Everything that is not a note reaches both parts.

    Voices. With both parts playing they share sixteen voices: each may use what its patch asks
    for (its polyphony, one voice for mono and legato, its unison count), is sure of eight, and
    gets more when the other asks for fewer.

    Inserts. Each part runs through three insert slots of its own before the mix (Inserts.h):
    any of the twelve effects, one per slot, so two parts can be treated differently.

    Mix. Each part has a level, a balance and an effects share: that share of the part passes
    through the master slots, the rest joins after them, before the master gain.

    Tempo. Synced delays and pumps, in the master slots and in the inserts, follow the host's tempo and
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
        std::array<InsertSettings, masterSlotCount> master {}; // F1..F6, in chain order
        float masterGainLinear = 1.0f;
        float space = 0.0f; // -1..1, knob 8 (D176): every reverb, delay and room, wetter (to twice) or drier (to half)
    };

    /** For the flight recorder's tripwires: the slowest stage of a block. */
    struct StageTiming
    {
        int code = 0;              // diag::Stage: part A/B voices, inserts, master F1..F6
        float milliseconds = 0.0f;
    };

    class SoundUnit final
    {
    public:
        static constexpr double masterGainRampSeconds = 0.02;
        static constexpr int sharedVoices = Part::maxVoices;
        static constexpr int guaranteedVoices = sharedVoices / partCount;
        static constexpr float silentLevelDb = -60.0f; // a part level at or below this is off

        SoundUnit();

        /** Message thread. Every buffer for blocks up to maxBlockSize; `seed` sets the voices' randomness apart. */
        void prepare(double sampleRate, int maxBlockSize, std::uint32_t seed);
        /** Silent and idle at once: voices, held keys, effect tails, the mix's and the master gain's ramps. */
        void panic() noexcept;

        /**
            One block of at most the prepared size, with its events, played with `settings`, added into the sum: after
            the unit's own master gain, times a fade that moves from `fadeFrom` to `fadeTo` across the block. The
            stage times go into `slowest`.
        */
        void render(float* sumLeft, float* sumRight, int numSamples, std::span<const EngineMidiEvent> events,
                    const EngineSettings& settings, const EngineTransportInfo& transport,
                    float fadeFrom, float fadeTo, StageTiming& slowest) noexcept;

        /** A voice is still playing. */
        [[nodiscard]] bool isSounding() const noexcept { return parts[0]->isSounding() || parts[1]->isSounding(); }
        /** The largest sample of the last block, after the master gain and before the fade. */
        [[nodiscard]] float lastPeak() const noexcept { return peak; }
        [[nodiscard]] int latencySamples() const noexcept { return parts[0]->latencySamples(); }
        [[nodiscard]] int activeVoiceCount() const noexcept
        {
            return parts[0]->activeVoiceCountForTesting() + parts[1]->activeVoiceCountForTesting();
        }
        [[nodiscard]] std::uint32_t consumeLearnedChord(int partIndex) noexcept
        {
            return parts[static_cast<std::size_t>(std::clamp(partIndex, 0, partCount - 1))]->consumeLearnedChord();
        }
        [[nodiscard]] Part& part(int index) noexcept { return *parts[static_cast<std::size_t>(index)]; }
        [[nodiscard]] const InsertSlot& insert(int partIndex, int slot) const noexcept
        {
            return inserts[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(slot)];
        }
        [[nodiscard]] const InsertSlot& masterSlot(int slot) const noexcept { return (*masterSlots)[static_cast<std::size_t>(slot)]; }

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

        // F1..F6, on the heap: each slot carries a rack, a grain buffer and a room, too much for a processor kept on a stack
        std::unique_ptr<std::array<InsertSlot, masterSlotCount>> masterSlots = std::make_unique<std::array<InsertSlot, masterSlotCount>>();
        juce::AudioBuffer<float> work;       // the stereo bus the master slots run on
        std::array<juce::AudioBuffer<float>, partCount> partBuffers; // each part's voices, then through its inserts
        std::array<std::array<InsertSlot, insertSlotCount>, partCount> inserts;
        juce::AudioBuffer<float> dryBus;     // what goes past the master slots
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;
        bool masterPrimed = false;
        float peak = 0.0f;
        double hostRate = 48000.0;
        double freeBeats = 0.0; // the beat count while no host position drives it
    };
}
