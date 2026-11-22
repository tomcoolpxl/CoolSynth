#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <variant>

#include <memory>

#include "ChipGenerator.h"
#include "Envelope.h"
#include "LiveBuffer.h"
#include "Fm4Generator.h"
#include "FmGenerator.h"
#include "GrainGenerator.h"
#include "KickGenerator.h"
#include "LadderProcessor.h"
#include "Lfo.h"
#include "ModMatrix.h"
#include "ModalGenerator.h"
#include "Modulation.h"
#include "Module.h"
#include "NoiseGenerator.h"
#include "OffModules.h"
#include "ResonatorProcessors.h"
#include "SampleGenerator.h"
#include "ShaperProcessors.h"
#include "StringGenerator.h"
#include "SvfProcessor.h"
#include "Tb303Generator.h"
#include "Tr808DrumsGenerator.h"
#include "Tr808MetalGenerator.h"
#include "Tr808SnareClapGenerator.h"
#include "VaGenerator.h"
#include "WavetableGenerator.h"

/*
    V3 voice (docs/design/DESIGN_V3.md section 4).

    Three generator slots render at unit level, each into its own buffer; a generator with a
    combine mode receives the previous generator's link (its mono signal and wrap track), so a
    modulator can be silent in the mix and still drive the next slot. The mixer applies each
    slot's level with a per-sample ramp. Three processor slots run in series on the stereo mix,
    then the amplifier envelope (per sample) and, while a stolen voice hands over, a short fade.

    LFO1 and LFO2 restart at every note the voice starts or retriggers (legato keeps them
    running) and only advance while the voice sounds, so with retrigger off a voice picks up its
    LFO where its previous note left it. Global sources that move inside a block arrive as one
    frame per sub-block.

    Once per sub-block, after every source has its value, the matrix adds its offsets: into each
    slot's parameters, the voice pitch and pan, the LFO rates (used from the next sub-block) and
    the VCA, whose gain ramps across the sub-block.

    Pitch lives in semitones throughout: note, constant-time glide, bend, a slow per-voice drift,
    and per-slot offsets meet in one place and become a frequency per generator. Generators with
    key tracking off sit at C4 plus their own offset.

    The voice owns its own handover: a start on a sounding voice fades the old note out over
    1.5 ms and then starts the new one from silence with every module reset, so the allocator
    never has to know about clicks. Every note begins inside render(), after the block's patch
    has been read: smoothed values snap to it, a waiting type change applies at once, and the
    envelopes start with the patch's own times, so a note never carries the previous preset.
*/
namespace coolsynth::synth::v3
{
    using GeneratorTypes = std::variant<OffGenerator, VaGenerator, NoiseGenerator, FmGenerator, Fm4Generator, WavetableGenerator,
                                        StringGenerator, ModalGenerator, SampleGenerator, GrainGenerator, ChipGenerator,
                                        KickGenerator, Tb303Generator, Tr808DrumsGenerator, Tr808MetalGenerator,
                                        Tr808SnareClapGenerator>; // new types only at the end
    using ProcessorTypes = std::variant<OffProcessor, LadderProcessor, SvfProcessor, CrusherProcessor, FolderProcessor, DriveProcessor,
                                        FormantProcessor, CombProcessor>; // new types only at the end
    using GeneratorSlot = Slot<GeneratorTypes>;
    using ProcessorSlot = Slot<ProcessorTypes>;

    namespace generatorType
    {
        inline constexpr int off = 0;
        inline constexpr int va = 1;
        inline constexpr int noise = 2;
        inline constexpr int fm2 = 3;
        inline constexpr int fm4 = 4;
        inline constexpr int wavetable = 5;
        inline constexpr int string = 6;
        inline constexpr int modal = 7;
        inline constexpr int sample = 8;
        inline constexpr int grain = 9;
        inline constexpr int chip = 10; // D230
        inline constexpr int kick = 11; // D232
        inline constexpr int tb303 = 12; // D242
        inline constexpr int drums808 = 13; // D252
        inline constexpr int metal808 = 14; // D254
        inline constexpr int snareClap808 = 15; // D256
    }
    namespace processorType
    {
        inline constexpr int off = 0;
        inline constexpr int ladder = 1;
        inline constexpr int svf = 2;
        inline constexpr int crusher = 3;
        inline constexpr int folder = 4;
        inline constexpr int drive = 5;
        inline constexpr int formant = 6;
        inline constexpr int comb = 7;
    }
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::va, GeneratorTypes>, VaGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::noise, GeneratorTypes>, NoiseGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::fm2, GeneratorTypes>, FmGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::fm4, GeneratorTypes>, Fm4Generator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::wavetable, GeneratorTypes>, WavetableGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::string, GeneratorTypes>, StringGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::modal, GeneratorTypes>, ModalGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::sample, GeneratorTypes>, SampleGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::grain, GeneratorTypes>, GrainGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::chip, GeneratorTypes>, ChipGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::kick, GeneratorTypes>, KickGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::tb303, GeneratorTypes>, Tb303Generator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::drums808, GeneratorTypes>, Tr808DrumsGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::metal808, GeneratorTypes>, Tr808MetalGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<generatorType::snareClap808, GeneratorTypes>, Tr808SnareClapGenerator>);
    static_assert(std::is_same_v<std::variant_alternative_t<processorType::ladder, ProcessorTypes>, LadderProcessor>);
    static_assert(std::is_same_v<std::variant_alternative_t<processorType::svf, ProcessorTypes>, SvfProcessor>);
    static_assert(std::is_same_v<std::variant_alternative_t<processorType::folder, ProcessorTypes>, FolderProcessor>);
    static_assert(std::is_same_v<std::variant_alternative_t<processorType::drive, ProcessorTypes>, DriveProcessor>);
    static_assert(std::is_same_v<std::variant_alternative_t<processorType::comb, ProcessorTypes>, CombProcessor>);

    inline constexpr int generatorSlotCount = 3;
    inline constexpr int processorSlotCount = 3;
    inline constexpr int envelopeCount = 3;
    inline constexpr int localLfoCount = 2;
    inline constexpr int globalLfoCount = 2;

    /**
        Global sources that move inside a block. The part computes one frame of them for every
        voice sub-block of a render call, so a global LFO is as smooth as a local one.
    */
    inline constexpr std::array<ModSource, 9> trackedGlobalSources { ModSource::glfo1, ModSource::glfo2, ModSource::genv,
                                                                     ModSource::step, ModSource::chaos, ModSource::macro1,
                                                                     ModSource::macro2, ModSource::macro3, ModSource::macro4 };

    struct GlobalFrame
    {
        std::array<float, trackedGlobalSources.size()> values {};
        float brightness = 0.0f; // knob 7, smoothed by the part as the macros are (D176, D261)
    };

    /** One generator slot as the patch sets it. Generic values are normalised 0..1. */
    struct GeneratorSettings
    {
        int type = generatorType::off;
        std::array<float, maxSlotParameters> p {};
        float level = 0.8f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f; // octave, semitone and fine together
        bool keyTrack = true;
        Combine combine = Combine::off;
        float combineAmount = 0.5f;
        int userSound = -1; // the user's own sound this slot plays (a UserLibrary handle), or -1 for the slot's own choice
    };

    /** One processor slot as the patch sets it. Generic values are normalised 0..1. */
    struct ProcessorSettings
    {
        int type = processorType::off;
        std::array<float, maxSlotParameters> p {};
    };

    /** Everything a voice reads from the patch, once per block. */
    struct VoiceSettings
    {
        std::array<GeneratorSettings, generatorSlotCount> generators {};
        std::array<ProcessorSettings, processorSlotCount> processors {};
        std::array<Envelope::Parameters, envelopeCount> envelopes {}; // 1 drives the amplifier
        float env2ToP1 = 0.0f;     // -1..1, normalised offset on the first parameter of P1 (a filter's cutoff)
        float velocityToP1 = 0.0f; // -1..1, same destination, times the note velocity
        float brightness = 0.0f;   // normalised offset on every Ladder and SVF cutoff: knob 7, Brightness (D176)
        float glideSeconds = 0.0f; // constant time, whatever the interval (or the time for an octave: glideByRate)
        bool glideByRate = false;      // the glide runs at a constant rate: an octave takes glideSeconds, a fifth 7/12 of it
        float glideDownFactor = 1.0f;  // a downward glide takes this many times as long as an upward one
        float driftCents = 0.0f;   // depth of the slow per-voice pitch drift
        std::array<LfoSettings, localLfoCount> lfos {};
        float wheelToLfo1 = 0.0f;  // 0..1: how much LFO1's depth follows the mod wheel
        MatrixSettings matrix {};
    };

    /** Part-wide state the voice reads each block. */
    struct VoiceGlobals
    {
        float bendSemitones = 0.0f;
        float channelPressure = 0.0f; // 0..1: the whole keyboard's pressure; a voice smooths it with its key's own
        float expression = 0.0f;      // 0..1: the expression pedal; each voice smooths it
        ModSources sources {}; // global sources; the voice adds its local ones on top
        LfoClock clock {};     // tempo, and song position at the start of the render call
        double ppqPerSample = 0.0;           // song position per voice sample
        const GlobalFrame* frames = nullptr; // one per sub-block of the render call, or none
        int frameCount = 0;
        std::uint32_t* takeTurn = nullptr;   // the part's count of takes played: each part its own, no global (D162)
    };

    struct NoteStart
    {
        int note = 60;
        float velocity = 1.0f;
        int glideFrom = -1;        // a note to glide from, or -1
        float pan = 0.0f;          // voice pan, added to every generator's pan
        float detuneCents = 0.0f;  // whole-voice detune, for unison stacks
        int unisonIndex = 0;
        int unisonCount = 1;
        bool fromArp = false;
        float gain = 1.0f;         // the note's share in a velocity split between the layers (D190)
    };

    class Voice final
    {
    public:
        static constexpr int maxSubBlock = 32;
        static constexpr double stealFadeSeconds = 0.005; // the 2-5 ms floor for a fast kill without a click (D170)
        static constexpr float minAmpAttackSeconds = 0.0005f;
        static constexpr float minAmpReleaseSeconds = 0.002f;

        /** Message thread. sampleRate is the voice rate (twice the host rate in the engine). */
        void prepare(double sampleRate, std::uint32_t seed);
        /** Its random numbers, its drift and its slots' back to where prepare left them: a defined start (D222). */
        void reseed() noexcept;
        /** Its LFOs' random state as it is now is the start a reseed returns to: once the part's preparation is done. */
        void keepAsStart() noexcept
        {
            for (auto& l : lfos)
                l.keepAsStart();
        }

        /** Silent and idle at once. */
        void reset() noexcept;

        /** A fresh note: from silence, every module restarted; a sounding voice fades out first. */
        void start(const NoteStart& note) noexcept;
        /**
            The same voice again: envelopes restart from their current level and oscillators run on.
            With glideFrom >= 0 the pitch glides from where it is; otherwise it jumps.
        */
        void retrigger(const NoteStart& note) noexcept;
        /** Pitch only, envelopes carry on; glides like retrigger(). */
        void legato(const NoteStart& note) noexcept;
        /**
            The kit play mode (D252): another instrument struck in the voice that holds the kit. A silent voice starts,
            a sounding one is retriggered; a strike while a note waits to start (two hits in one block) waits with it.
        */
        void strikeKit(const NoteStart& note) noexcept;
        void release(float releaseVelocity = 0.5f) noexcept;
        /** Out over the steal fade and then free, the live emergency's way (D222): a note that is sounding and
            not already fading. */
        void fadeOut() noexcept;
        [[nodiscard]] bool isFadingOut() const noexcept { return fadingOut; }
        /** A kit voice (D258): the level of the quietest instrument ringing in any of its generators, -1 if none. */
        [[nodiscard]] float quietestInstrumentLevel() const noexcept;
        /** A kit voice: that instrument fades out, the voice and its other instruments ring on (D258). */
        bool shedQuietestInstrument() noexcept;
        void kill() noexcept { reset(); }
        /** Let go and waiting for a slice to end before releasing (D207). */
        [[nodiscard]] bool isReleaseWaiting() const noexcept { return releaseWaiting; }
        /** The pressure on this voice's own key (polyphonic aftertouch), 0..1. A new key starts at 0. */
        void setPolyPressure(float value) noexcept { polyPressure = std::clamp(value, 0.0f, 1.0f); }

        /** Key pressure and the expression pedal reach the matrix smoothed over about this long. */
        static constexpr float pressureSmoothingSeconds = 0.01f;

        /** Adds the voice into left and right. */
        void render(float* left, float* right, int numSamples, const VoiceSettings& settings, const VoiceGlobals& globals) noexcept;

        [[nodiscard]] bool isActive() const noexcept;
        [[nodiscard]] bool isReleased() const noexcept { return released; }
        [[nodiscard]] bool isStealing() const noexcept { return stealRemaining > 0; }
        [[nodiscard]] int currentNote() const noexcept { return pending ? pendingNote.note : note.note; }
        [[nodiscard]] float ampLevel() const noexcept { return envelopes[0].getValue(); }

        [[nodiscard]] const ModSources& sourcesForTesting() const noexcept { return sources; }
        [[nodiscard]] float pitchForTesting() const noexcept { return currentPitch; }
        [[nodiscard]] float driftForTesting() const noexcept { return driftValue; }
        [[nodiscard]] const MatrixOffsets& matrixOffsetsForTesting() const noexcept { return matrixOffsets; }
        [[nodiscard]] const Lfo& lfoForTesting(int index) const noexcept { return lfos[static_cast<std::size_t>(index)]; }
        [[nodiscard]] int generatorTypeForTesting(int slot) const noexcept
        {
            return generators[static_cast<std::size_t>(slot)].slot.typeIndex();
        }

    private:
        struct GeneratorState
        {
            GeneratorSlot slot;
            SlotBase base;
            float mixedLevel = 0.0f;
            std::array<float, maxSubBlock> left {}, right {};
        };

        struct ProcessorState
        {
            ProcessorSlot slot;
            SlotBase base;
        };

        void applySettings(const VoiceSettings& settings) noexcept;
        void snapToSettings() noexcept;
        void restartModules() noexcept;
        void releaseNow() noexcept;
        void beginNote(const NoteStart& start, const ModSources& base) noexcept;
        void startPendingNote(const VoiceGlobals& globals) noexcept;
        /** Each envelope's time factor for a note, from the matrix's envelope-time slots. */
        [[nodiscard]] std::array<float, envelopeCount> envelopeTimeFactors(const ModSources& base, const NoteStart& start, float random) const noexcept;
        void startEnvelopes(const std::array<float, envelopeCount>& timeFactors) noexcept;
        void retarget(int noteNumber, float detuneCents, bool glide) noexcept;
        void renderSubBlock(float* left, float* right, int offset, int n, const VoiceSettings& settings, const VoiceGlobals& globals) noexcept;
        void advancePitch(int n, const VoiceSettings& settings) noexcept;
        void advanceDrift(int n) noexcept;
        float nextRandom() noexcept; // -1..1

        std::array<GeneratorState, generatorSlotCount> generators;
        std::array<ProcessorState, processorSlotCount> processors;
        std::array<Envelope, envelopeCount> envelopes;
        std::array<Lfo, localLfoCount> lfos;
        bool lfoRestartPending = false;
        MatrixOffsets matrixOffsets {};
        std::array<MatrixSlot, matrixSlotCount> noteSlots {}; // the matrix slots read at note start
        int noteSlotCount = 0;
        std::array<float, localLfoCount> lfoRateScale { 1.0f, 1.0f }; // from the previous sub-block's matrix
        float vcaGain = 1.0f;
        bool vcaSnap = true;
        std::array<Envelope::Parameters, envelopeCount> appliedEnvelopes {};
        std::array<float, maxSubBlock> ampBuffer {}, mixLeft {}, mixRight {};
        ModSources sources;

        NoteStart note, pendingNote;
        /** The kit's strikes that came while a note waited to start, played right after it starts (D252). */
        static constexpr int maxQueuedStrikes = 8;
        std::array<NoteStart, maxQueuedStrikes> queuedStrikes {};
        int queuedStrikeCount = 0;
        bool pending = false;
        bool pendingReleased = false;
        bool released = false;
        bool releaseWaiting = false; // let go, but a slice still plays to its end first (D207)
        bool prepared = false;
        int stealRemaining = 0;
        bool fadingOut = false; // the steal fade ends the voice instead of starting a waiting note
        int envelopeHold = 0; // samples the envelopes wait at a fresh note, matching the generators' latency
        int stealTotal = 1;
        // A released voice whose recordings have ended ends once it has been this quiet this long (TODO 10).
        int silentSamples = 0;
        float lastSubBlockPeak = 0.0f;
        [[nodiscard]] bool canEndEarly(const VoiceSettings& settings) const noexcept;

        float currentPitch = 60.0f;
        float targetPitch = 60.0f;
        float glideSpan = 0.0f;
        float releaseVelocity = 0.0f;
        float noteRandom = 0.0f;
        float polyPressure = 0.0f;     // the key's own pressure, as last sent
        float pressureValue = 0.0f;    // the aftertouch source: the larger of the two pressures, smoothed
        float expressionValue = 0.0f;  // the expression source, smoothed
        bool pressurePrimed = false;   // a fresh note takes both values as they are, without a ramp
        float driftStage = 0.0f;
        float driftValue = 0.0f;
        float driftTarget = 0.0f;
        int driftSamplesToNext = 0;
        std::uint32_t rng = 1;
        std::uint32_t rngStart = 1; // what prepare seeded rng with (D222)
        float sampleRate = 96000.0f;
        // The last second of the generator before a Grain slot set to Live (decision D139); on the heap, a voice is
        // kept in an array. Only the first such slot gets it.
        std::unique_ptr<LiveBuffer> liveBuffer;
    };
}
