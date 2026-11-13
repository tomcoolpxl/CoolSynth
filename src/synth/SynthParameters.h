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
    };

    struct EngineMidiEvent
    {
        EngineMidiEventType type = EngineMidiEventType::noteOn;
        int sampleOffset = 0;
        uint8_t noteNumber = 0;
        float value = 0.0f;
        bool fromArp = false;
    };

    /** Raw values of the parameters outside a part, resolved by ID once. */
    struct GlobalParameterPointers
    {
        std::atomic<float>* arpEnabled = nullptr;
        std::atomic<float>* arpInternalTempoBpm = nullptr;
        std::atomic<float>* arpRateDivision = nullptr;
        std::atomic<float>* arpPattern = nullptr;
        std::atomic<float>* arpOctaveRange = nullptr;
        std::atomic<float>* arpGate = nullptr;
        std::atomic<float>* arpSwing = nullptr;
        std::atomic<float>* arpChance = nullptr;
        std::atomic<float>* arpRatchetCount = nullptr;
        std::atomic<float>* arpRatchetChance = nullptr;
        std::atomic<float>* arpAccentEvery = nullptr;
        std::atomic<float>* arpAccentAmount = nullptr;
        std::atomic<float>* arpRhythm = nullptr;
        std::atomic<float>* arpEuclideanPulses = nullptr;
        std::atomic<float>* arpEuclideanSteps = nullptr;
        std::atomic<float>* arpEuclideanRotation = nullptr;
        std::atomic<float>* arpLatch = nullptr;
        std::atomic<float>* driveEnabled = nullptr;
        std::atomic<float>* driveAmount = nullptr;
        std::atomic<float>* driveMix = nullptr;
        std::atomic<float>* chorusEnabled = nullptr;
        std::atomic<float>* chorusRateHz = nullptr;
        std::atomic<float>* chorusDepth = nullptr;
        std::atomic<float>* chorusMix = nullptr;
        std::atomic<float>* delayEnabled = nullptr;
        std::atomic<float>* delayTimeMs = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayMix = nullptr;
        std::atomic<float>* delaySync = nullptr;
        std::atomic<float>* delayDivision = nullptr;
        std::atomic<float>* delayMode = nullptr;
        std::atomic<float>* reverbEnabled = nullptr;
        std::atomic<float>* reverbSize = nullptr;
        std::atomic<float>* reverbDamping = nullptr;
        std::atomic<float>* reverbMix = nullptr;
        std::atomic<float>* reverbPreDelay = nullptr;
        std::atomic<float>* reverbLowCut = nullptr;
        std::atomic<float>* reverbModulation = nullptr;
        std::atomic<float>* reverbShimmer = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
        std::atomic<float>* phaserEnabled = nullptr;
        std::atomic<float>* phaserRateHz = nullptr;
        std::atomic<float>* phaserDepth = nullptr;
        std::atomic<float>* compressorEnabled = nullptr;
        std::atomic<float>* compressorAmount = nullptr;
        std::atomic<float>* compressorMix = nullptr;
    };

    using GlobalParamMemberPtr = std::atomic<float>* GlobalParameterPointers::*;

    inline const std::array<std::pair<const char*, GlobalParamMemberPtr>,
                            coolsynth::parameters::allParameterIds.size()> kGlobalParamPtrBindings = {{
        { coolsynth::parameters::ids::arpEnabled,           &GlobalParameterPointers::arpEnabled           },
        { coolsynth::parameters::ids::arpInternalTempoBpm,  &GlobalParameterPointers::arpInternalTempoBpm  },
        { coolsynth::parameters::ids::arpRateDivision,      &GlobalParameterPointers::arpRateDivision      },
        { coolsynth::parameters::ids::arpPattern,           &GlobalParameterPointers::arpPattern           },
        { coolsynth::parameters::ids::arpOctaveRange,       &GlobalParameterPointers::arpOctaveRange       },
        { coolsynth::parameters::ids::arpGate,              &GlobalParameterPointers::arpGate              },
        { coolsynth::parameters::ids::arpSwing,             &GlobalParameterPointers::arpSwing             },
        { coolsynth::parameters::ids::arpChance,            &GlobalParameterPointers::arpChance            },
        { coolsynth::parameters::ids::arpRatchetCount,      &GlobalParameterPointers::arpRatchetCount      },
        { coolsynth::parameters::ids::arpRatchetChance,     &GlobalParameterPointers::arpRatchetChance     },
        { coolsynth::parameters::ids::arpAccentEvery,       &GlobalParameterPointers::arpAccentEvery       },
        { coolsynth::parameters::ids::arpAccentAmount,      &GlobalParameterPointers::arpAccentAmount      },
        { coolsynth::parameters::ids::arpRhythm,            &GlobalParameterPointers::arpRhythm            },
        { coolsynth::parameters::ids::arpEuclideanPulses,   &GlobalParameterPointers::arpEuclideanPulses   },
        { coolsynth::parameters::ids::arpEuclideanSteps,    &GlobalParameterPointers::arpEuclideanSteps    },
        { coolsynth::parameters::ids::arpEuclideanRotation, &GlobalParameterPointers::arpEuclideanRotation },
        { coolsynth::parameters::ids::arpLatch,             &GlobalParameterPointers::arpLatch             },
        { coolsynth::parameters::ids::driveEnabled,         &GlobalParameterPointers::driveEnabled         },
        { coolsynth::parameters::ids::driveAmount,          &GlobalParameterPointers::driveAmount          },
        { coolsynth::parameters::ids::driveMix,             &GlobalParameterPointers::driveMix             },
        { coolsynth::parameters::ids::chorusEnabled,        &GlobalParameterPointers::chorusEnabled        },
        { coolsynth::parameters::ids::chorusRateHz,         &GlobalParameterPointers::chorusRateHz         },
        { coolsynth::parameters::ids::chorusDepth,          &GlobalParameterPointers::chorusDepth          },
        { coolsynth::parameters::ids::chorusMix,            &GlobalParameterPointers::chorusMix            },
        { coolsynth::parameters::ids::delayEnabled,         &GlobalParameterPointers::delayEnabled         },
        { coolsynth::parameters::ids::delayTimeMs,          &GlobalParameterPointers::delayTimeMs          },
        { coolsynth::parameters::ids::delayFeedback,        &GlobalParameterPointers::delayFeedback        },
        { coolsynth::parameters::ids::delayMix,             &GlobalParameterPointers::delayMix             },
        { coolsynth::parameters::ids::delaySync,            &GlobalParameterPointers::delaySync            },
        { coolsynth::parameters::ids::delayDivision,        &GlobalParameterPointers::delayDivision        },
        { coolsynth::parameters::ids::delayMode,            &GlobalParameterPointers::delayMode            },
        { coolsynth::parameters::ids::reverbEnabled,        &GlobalParameterPointers::reverbEnabled        },
        { coolsynth::parameters::ids::reverbSize,           &GlobalParameterPointers::reverbSize           },
        { coolsynth::parameters::ids::reverbDamping,        &GlobalParameterPointers::reverbDamping        },
        { coolsynth::parameters::ids::reverbMix,            &GlobalParameterPointers::reverbMix            },
        { coolsynth::parameters::ids::reverbPreDelay,       &GlobalParameterPointers::reverbPreDelay       },
        { coolsynth::parameters::ids::reverbLowCut,         &GlobalParameterPointers::reverbLowCut         },
        { coolsynth::parameters::ids::reverbModulation,     &GlobalParameterPointers::reverbModulation     },
        { coolsynth::parameters::ids::reverbShimmer,        &GlobalParameterPointers::reverbShimmer        },
        { coolsynth::parameters::ids::masterGainDb,         &GlobalParameterPointers::masterGainDb         },
        { coolsynth::parameters::ids::phaserEnabled,        &GlobalParameterPointers::phaserEnabled        },
        { coolsynth::parameters::ids::phaserRateHz,         &GlobalParameterPointers::phaserRateHz         },
        { coolsynth::parameters::ids::phaserDepth,          &GlobalParameterPointers::phaserDepth          },
        { coolsynth::parameters::ids::compressorEnabled,    &GlobalParameterPointers::compressorEnabled    },
        { coolsynth::parameters::ids::compressorAmount,     &GlobalParameterPointers::compressorAmount     },
        { coolsynth::parameters::ids::compressorMix,        &GlobalParameterPointers::compressorMix        },
    }};
}
