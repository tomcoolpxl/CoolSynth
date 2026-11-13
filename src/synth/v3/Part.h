#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "synth/Arpeggiator.h"
#include "synth/HalfBand2x.h"
#include "ChordMemory.h"
#include "GlobalModulators.h"
#include "Voice.h"

/*
    V3 part (DESIGN_V3.md sections 6 and 7): one patch playing on a pool of voices.

    The note layer runs in this order: chord memory (one key plays a chord, see ChordMemory.h),
    the arpeggiator (reused unchanged, with the rule that a note-on is absorbed before steps are
    generated and a note-off after, so a tap shorter than a block still fires), then the play
    mode, then the allocator. Voices render at twice the host
    rate into one bus with a fixed headroom, so a held note never changes level when others join;
    the bus is decimated once per part.

    Allocation in poly mode: a key that is still sounding takes its own voice again, so repeated
    notes never stack; otherwise the least recently used silent voice, then the quietest released
    voice, then the quietest voice held only by the pedal, then the oldest held voice. Voices
    above the polyphony limit finish what they play and are not used again. Mono and legato play
    one voice from a note stack with last, low or high priority and return to a held key on
    release; legato keeps the envelopes running across overlapping keys. Unison stacks 2 to 8
    voices on one note, spread in pitch and across the stereo field. Glide can be limited to
    overlapping keys.

    The global LFOs, the step modulator, the chaos source and the smoothed macros run whether or
    not anything sounds, one value per voice sub-block; the LFOs and the step modulator restart
    at the first note after silence. The global envelope's gate follows the keys as played,
    before chord memory and the arpeggiator: it opens with the first key of a phrase (or every key) and closes when
    the last key and the pedal are up and no latched arp notes remain. Synced, its times are
    written as at 120 BPM and scale with the tempo. Tempo is the host's when it reports one, otherwise
    the arpeggiator's internal tempo, so the arp and synced LFOs always agree.
*/
namespace coolsynth::synth::v3
{
    using coolsynth::synth::Arpeggiator;
    using coolsynth::synth::ArpParametersV2;
    using coolsynth::synth::EngineMidiEvent;
    using coolsynth::synth::EngineMidiEventType;
    using coolsynth::synth::EngineTransportInfo;

    enum class PlayMode : int { poly = 0, mono, legato, unison };
    enum class KeyPriority : int { last = 0, low, high };
    /** When the global envelope starts: at the first key of a phrase (none other held), or at every key. */
    enum class GenvTrigger : int { firstNote = 0, everyNote };
    inline constexpr int macroCount = 4;

    struct PartSettings
    {
        VoiceSettings voice {};
        PlayMode playMode = PlayMode::poly;
        KeyPriority keyPriority = KeyPriority::last;
        int polyphony = 16;
        int unisonVoices = 4;
        float unisonDetuneCents = 15.0f; // outermost voice; the others are spread evenly inside
        float unisonSpread = 0.7f;       // pan of the outermost voices
        float bendRangeSemitones = 2.0f;
        bool glideLegatoOnly = false;
        std::array<LfoSettings, globalLfoCount> globalLfos {}; // GLFO1, GLFO2: one phase for every voice
        Envelope::Parameters globalEnvelope {};  // GENV; times in seconds, as at 120 BPM when synced
        bool globalEnvelopeSync = false;
        GenvTrigger globalEnvelopeTrigger = GenvTrigger::firstNote;
        StepSettings step {};
        std::array<float, macroCount> macros {};
        float chaosRateHz = 0.5f;
        ChordSettings chord {};
        ArpParametersV2 arp {};
    };

    class Part final
    {
    public:
        static constexpr int maxVoices = 16;
        static constexpr int maxUnisonVoices = 8;
        static constexpr int oversampleFactor = 2;
        static constexpr float voiceHeadroom = 0.5f;

        /** Message thread. Allocates the voice bus for blocks up to maxBlockSize; longer blocks are split. */
        void prepare(double hostSampleRate, int maxBlockSize, std::uint32_t seed);

        /** Panic: silence, empty note stack, controllers at rest. */
        void reset() noexcept;

        /** Writes (not adds) the part's host-rate output. Event offsets are relative to this call. */
        void render(float* left,
                    float* right,
                    int numSamples,
                    std::span<const EngineMidiEvent> events,
                    const PartSettings& settings,
                    const EngineTransportInfo& transport) noexcept;

        /** True once after an All Sound Off, so the engine clears its effect tails too. */
        [[nodiscard]] bool consumeSoundOff() noexcept
        {
            const bool requested = soundOffPending;
            soundOffPending = false;
            return requested;
        }

        /** True once after the first note after silence: where a free-running pump starts its bar. */
        [[nodiscard]] bool consumePhraseStart() noexcept
        {
            const bool started = phraseStartPending;
            phraseStartPending = false;
            return started;
        }

        /** The chord just learned, packed (see packLearnedChord), or 0. Audio thread; reading clears it. */
        [[nodiscard]] std::uint32_t consumeLearnedChord() noexcept { return chordMemory.consumeLearned(); }

        /** Host samples between a note event and its sound: decimator plus generator delay. */
        [[nodiscard]] int latencySamples() const noexcept { return decimatorLatency + generatorLatency; }

        [[nodiscard]] int activeVoiceCountForTesting() const noexcept;
        [[nodiscard]] const Voice& voiceForTesting(int index) const noexcept { return voices[static_cast<std::size_t>(index)]; }
        [[nodiscard]] int voiceNoteForTesting(int index) const noexcept { return state[static_cast<std::size_t>(index)].note; }
        [[nodiscard]] bool voiceKeyDownForTesting(int index) const noexcept { return state[static_cast<std::size_t>(index)].keyDown; }
        [[nodiscard]] float bendSemitonesForTesting() const noexcept { return globals.bendSemitones; }
        [[nodiscard]] int heldNoteCountForTesting() const noexcept { return heldCount; }
        [[nodiscard]] int lastPlayedNoteForTesting() const noexcept { return lastNote; }
        [[nodiscard]] const ChordMemory& chordMemoryForTesting() const noexcept { return chordMemory; }
        [[nodiscard]] int arpHeldNoteCountForTesting() const noexcept { return arpeggiator.getHeldNoteCountForTesting(); }
        [[nodiscard]] int arpLatchedNoteCountForTesting() const noexcept { return arpeggiator.getLatchedNoteCountForTesting(); }
        [[nodiscard]] int arpRingingNoteCountForTesting() const noexcept { return arpeggiator.getRingingNoteCountForTesting(); }
        [[nodiscard]] const Lfo& globalLfoForTesting(int index) const noexcept { return globalLfos[static_cast<std::size_t>(index)]; }
        [[nodiscard]] double tempoForTesting() const noexcept { return chunkClock.bpm; }
        [[nodiscard]] const Envelope& globalEnvelopeForTesting() const noexcept { return globalEnvelope; }
        [[nodiscard]] const StepModulator& stepForTesting() const noexcept { return stepModulator; }
        [[nodiscard]] const GlobalFrame& lastFrameForTesting() const noexcept { return frames[static_cast<std::size_t>(std::max(0, globals.frameCount - 1))]; }

    private:
        struct VoiceState
        {
            int note = -1;
            bool keyDown = false;
            bool sustained = false;  // key up, held by the pedal
            std::uint64_t order = 0; // when it last started a note
        };

        struct HeldNote
        {
            int note = -1;
            float velocity = 0.0f;
            std::uint64_t order = 0;
        };

        static constexpr int maxHeldNotes = 32;

        void renderChunk(float* left, float* right, int n, std::span<const EngineMidiEvent> events, int offsetBase,
                         const PartSettings& settings, const EngineTransportInfo& transport) noexcept;
        void renderSpan(int start, int count, const PartSettings& settings) noexcept;
        void handleEvent(const EngineMidiEvent& event, const PartSettings& settings) noexcept;
        void updateGlobals(const PartSettings& settings) noexcept;
        [[nodiscard]] bool anyVoiceActive() const noexcept;
        void trackKeys(const EngineMidiEvent& event, const PartSettings& settings) noexcept;
        void closeGlobalEnvelopeIfUnheld(const PartSettings& settings) noexcept;

        void polyNoteOn(int note, float velocity, bool fromArp, const PartSettings& settings) noexcept;
        void polyNoteOff(int note, float releaseVelocity) noexcept;
        void monoNoteOn(int note, float velocity, bool fromArp, const PartSettings& settings) noexcept;
        void monoNoteOff(int note, float releaseVelocity, const PartSettings& settings) noexcept;
        void playMono(int note, float velocity, bool overlapping, bool fromArp, const PartSettings& settings) noexcept;
        [[nodiscard]] int pickVoice(int limit) const noexcept;
        [[nodiscard]] int glideSource(const PartSettings& settings, bool overlapping) const noexcept;

        void addHeld(int note, float velocity) noexcept;
        void removeHeld(int note) noexcept;
        [[nodiscard]] int priorityNote(KeyPriority priority) const noexcept;
        [[nodiscard]] float heldVelocity(int note) const noexcept;

        void releaseVoice(int index, float releaseVelocity) noexcept;
        void releaseAll() noexcept;
        void releaseSustained() noexcept;

        std::array<Voice, maxVoices> voices;
        std::array<VoiceState, maxVoices> state {};
        std::array<HeldNote, maxHeldNotes> held {};
        int heldCount = 0;

        std::vector<float> busLeft, busRight;
        HalfBandDecimator2x decimatorLeft, decimatorRight;
        ChordMemory chordMemory;
        std::vector<EngineMidiEvent> chordEvents;     // one chunk's events after chord memory
        std::vector<std::uint16_t> chordInputsBefore; // per chord event: the played events read by then
        Arpeggiator arpeggiator;
        VoiceGlobals globals;
        std::array<Lfo, globalLfoCount> globalLfos;
        std::vector<GlobalFrame> frames;
        LfoClock chunkClock {};
        double ppqPerHostSample = 0.0;
        Envelope globalEnvelope;
        Envelope::Parameters appliedGlobalEnvelope {};
        StepModulator stepModulator;
        ChaosSource chaos;
        std::array<float, macroCount> macroValues {};
        bool macrosPrimed = false;
        std::array<bool, 128> keyIsDown {};
        int keysDown = 0;

        std::uint64_t counter = 0;
        int lastNote = -1;
        int monoNote = -1;
        int monoVoiceCount = 0;
        PlayMode currentMode = PlayMode::poly;
        bool sustainDown = false;
        bool soundOffPending = false;
        bool phraseStartPending = false;
        bool previousArpEnabled = false;
        bool prepared = false;
        float bendNormalised = 0.0f;
        float wheel = 0.0f;
        double hostRate = 48000.0;
        int maxBlock = 512;
        int decimatorLatency = 0;
        int generatorLatency = 1;
    };
}
