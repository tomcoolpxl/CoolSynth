#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include "parameters/ParameterIDs.h"

/*
    Engine-facing types shared by the processor, the arpeggiator and the effect rack: the MIDI
    event model, the arpeggiator and effect settings, and the raw value pointers of the
    parameters outside a V3 part. The part's own settings live in synth/v3.
*/
namespace coolsynth::synth
{
    inline constexpr int maxEngineEventsPerBlock = 1024;

    struct ArpParametersV2
    {
        bool enabled = false;
        float internalTempoBpm = 120.0f;
        coolsynth::parameters::ArpRateChoice rate =
            coolsynth::parameters::ArpRateChoice::sixteenth;
        coolsynth::parameters::ArpPatternChoice pattern =
            coolsynth::parameters::ArpPatternChoice::up;
        int octaveRange = 1;
        float gateLength = 0.5f;
        float swingAmount = 0.0f;
        float chance = 1.0f;
        coolsynth::parameters::ArpRatchetChoice ratchetCount =
            coolsynth::parameters::ArpRatchetChoice::off;
        float ratchetChance = 0.0f;
        coolsynth::parameters::ArpAccentEveryChoice accentEvery =
            coolsynth::parameters::ArpAccentEveryChoice::off;
        float accentAmount = 0.0f;
        coolsynth::parameters::ArpRhythmChoice rhythm =
            coolsynth::parameters::ArpRhythmChoice::straight;
        int euclideanPulses = 4;
        int euclideanSteps = 8;
        int euclideanRotation = 0;
        bool latch = false;
        bool newPatterns = false; // random choices go on at a defined start instead of repeating (D222)
    };

    struct DriveParametersV2
    {
        bool enabled = false;
        float amount = 0.0f;
        float mix = 1.0f;
    };

    struct ChorusParametersV2
    {
        bool enabled = false;
        float rateHz = 0.6f;
        float depth = 0.4f;
        float mix = 0.3f;
    };

    /** Note values a synced delay can take, in quarter notes. */
    struct DelayDivision
    {
        const char* name;
        double beats;
    };

    inline constexpr std::array<DelayDivision, 14> delayDivisions { {
        { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16.", 0.375 }, { "1/8T", 1.0 / 3.0 },
        { "1/8", 0.5 },    { "1/8.", 0.75 },       { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4.", 1.5 },
        { "1/2T", 4.0 / 3.0 }, { "1/2", 2.0 },     { "1/2.", 3.0 },  { "1 Bar", 4.0 },
    } };
    inline constexpr int delayDefaultDivision = 5; // 1/8

    /** The length of a delay division at a tempo, in milliseconds. */
    [[nodiscard]] inline float delayDivisionMs(int division, double bpm) noexcept
    {
        const auto index = static_cast<std::size_t>(division < 0 ? 0 : (division >= static_cast<int>(delayDivisions.size())
                                                                             ? static_cast<int>(delayDivisions.size()) - 1 : division));
        return static_cast<float>(delayDivisions[index].beats * 60000.0 / (bpm > 1.0 ? bpm : 1.0));
    }

    struct DelayParametersV2
    {
        bool enabled = false;
        float timeMs = 250.0f; // what the delay plays; the engine sets it from the division when synced
        float feedback = 0.25f;
        float mix = 0.0f;
        bool sync = false;
        int division = delayDefaultDivision;
        bool pingPong = false; // off: tape (both channels echo in place, darkening)
    };

    struct ReverbParametersV2
    {
        bool enabled = false;
        float size = 0.4f;
        float damping = 0.5f;
        float mix = 0.2f;
        float preDelayMs = 0.0f;
        float lowCutHz = 20.0f;
        float modulation = 0.5f;
        float shimmer = 0.0f;
    };

    struct PhaserParametersV2
    {
        bool enabled = false;
        float rateHz = 0.5f;
        float depth = 0.6f;
    };

    struct CompressorParametersV2
    {
        bool enabled = false;
        float amount = 0.3f;
        float mix = 1.0f;
    };

    enum class EngineMidiEventType : uint8_t
    {
        noteOn,
        noteOff,
        pitchBend,
        modWheel,
        sustainPedal,
        allNotesOff,
        allSoundOff,
        resetControllers,
        channelPressure, // the whole keyboard's pressure (channel aftertouch), value 0..1
        polyPressure,    // one key's pressure (polyphonic aftertouch), noteNumber and value 0..1
        expression,      // the expression pedal (CC 11), value 0..1
    };

    struct EngineMidiEvent
    {
        EngineMidiEventType type = EngineMidiEventType::noteOn;
        int sampleOffset = 0;
        uint8_t noteNumber = 0;
        float value = 0.0f;
        bool fromArp = false;
        float gain = 1.0f; // a note-on's share in a velocity split between the layers (D190)
    };

    /** A fader's position (0 bottom, 1 top) as a gain (D184): the cube, an audio taper: the top as designed (0 dB),
        half way about -18 dB, a quarter about -36 dB, the bottom off. */
    [[nodiscard]] inline float faderGain(float position) noexcept
    {
        const float x = position < 0.0f ? 0.0f : (position > 1.0f ? 1.0f : position);
        return x * x * x;
    }

    /** One layer's arpeggiator (D194: each layer has its own). */
    struct ArpParameterPointers
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* rateDivision = nullptr;
        std::atomic<float>* pattern = nullptr;
        std::atomic<float>* octaveRange = nullptr;
        std::atomic<float>* gate = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* chance = nullptr;
        std::atomic<float>* ratchetCount = nullptr;
        std::atomic<float>* ratchetChance = nullptr;
        std::atomic<float>* accentEvery = nullptr;
        std::atomic<float>* accentAmount = nullptr;
        std::atomic<float>* rhythm = nullptr;
        std::atomic<float>* euclideanPulses = nullptr;
        std::atomic<float>* euclideanSteps = nullptr;
        std::atomic<float>* euclideanRotation = nullptr;
        std::atomic<float>* latch = nullptr;
        std::atomic<float>* newPatterns = nullptr;
    };

    /** Raw values of the parameters outside a part, resolved by ID once. */
    struct GlobalParameterPointers
    {
        std::array<ArpParameterPointers, 2> arps {}; // layer A, layer B
        std::atomic<float>* arpInternalTempoBpm = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
        std::atomic<float>* brightness = nullptr;
        std::atomic<float>* space = nullptr;
        std::atomic<float>* volumeA = nullptr;
        std::atomic<float>* volumeB = nullptr;
        std::atomic<float>* volumeFx = nullptr;
        std::atomic<float>* volumeMaster = nullptr;
        std::atomic<float>* masterBoost = nullptr; // dB, D225
    };

    using GlobalParamMemberPtr = std::atomic<float>* GlobalParameterPointers::*;

    inline const std::array<std::pair<const char*, GlobalParamMemberPtr>,
                            9> kGlobalParamPtrBindings = {{
        { coolsynth::parameters::ids::arpInternalTempoBpm,  &GlobalParameterPointers::arpInternalTempoBpm  },
        { coolsynth::parameters::ids::masterGainDb,         &GlobalParameterPointers::masterGainDb         },
        { coolsynth::parameters::ids::brightness,           &GlobalParameterPointers::brightness           },
        { coolsynth::parameters::ids::space,                &GlobalParameterPointers::space                },
        { coolsynth::parameters::ids::volumeA,              &GlobalParameterPointers::volumeA              },
        { coolsynth::parameters::ids::volumeB,              &GlobalParameterPointers::volumeB              },
        { coolsynth::parameters::ids::volumeFx,             &GlobalParameterPointers::volumeFx             },
        { coolsynth::parameters::ids::volumeMaster,         &GlobalParameterPointers::volumeMaster         },
        { coolsynth::parameters::ids::masterBoost,          &GlobalParameterPointers::masterBoost          },
    }};
    /** Binds layer `layer`'s arp pointers (0 A, 1 B), by the IDs of ParameterIDs.h. */
    template <typename State>
    inline ArpParameterPointers bindArpPointers(State& state, int layer)
    {
        namespace ids = coolsynth::parameters::ids;
        const auto get = [&](const char* layerAId) { return state.getRawParameterValue(ids::arpOfLayer(layer, layerAId)); };
        ArpParameterPointers p;
        p.enabled = get(ids::arpEnabled);
        p.rateDivision = get(ids::arpRateDivision);
        p.pattern = get(ids::arpPattern);
        p.octaveRange = get(ids::arpOctaveRange);
        p.gate = get(ids::arpGate);
        p.swing = get(ids::arpSwing);
        p.chance = get(ids::arpChance);
        p.ratchetCount = get(ids::arpRatchetCount);
        p.ratchetChance = get(ids::arpRatchetChance);
        p.accentEvery = get(ids::arpAccentEvery);
        p.accentAmount = get(ids::arpAccentAmount);
        p.rhythm = get(ids::arpRhythm);
        p.euclideanPulses = get(ids::arpEuclideanPulses);
        p.euclideanSteps = get(ids::arpEuclideanSteps);
        p.euclideanRotation = get(ids::arpEuclideanRotation);
        p.latch = get(ids::arpLatch);
        p.newPatterns = get(ids::arpNewPatterns);
        return p;
    }
}
