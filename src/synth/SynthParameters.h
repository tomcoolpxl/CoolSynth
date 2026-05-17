#pragma once

#include <atomic>
#include <cstdint>
#include "parameters/ParameterIDs.h"

namespace coolsynth::synth
{
    inline constexpr int defaultVoiceCount = 8;
    inline constexpr double masterGainRampSeconds = 0.004;
    inline constexpr int maxEngineEventsPerBlock = 1024;
    // LFO updates every N samples with linear interpolation (~0.7 ms at 44.1 kHz — inaudible)
    inline constexpr int lfoSubRateSamples = 32;

    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct OscillatorParametersV2
    {
        coolsynth::parameters::OscillatorWaveShape waveShape =
            coolsynth::parameters::OscillatorWaveShape::saw;
        int octaveIndex = 2;
        float fineCents = 0.0f;
        float level = 0.75f;
        float pulseWidth = 0.5f;
        bool syncEnabled = false;
        bool lowFrequencyMode = false;
    };

    struct MixerParametersV2
    {
        float noiseLevel = 0.0f;
    };

    struct FilterParametersV2
    {
        float cutoffHz = 10000.0f;
        float resonanceNormalized = 0.1f;
        float envelopeAmount = 0.0f;
        coolsynth::parameters::FilterKeyTrackingMode keyTracking =
            coolsynth::parameters::FilterKeyTrackingMode::half;
    };

    struct LfoParametersV2
    {
        float rateHz = 5.0f;
        coolsynth::parameters::LfoWaveShape waveShape =
            coolsynth::parameters::LfoWaveShape::triangle;
        float oscPitchDepth = 0.0f;
        float pulseWidthDepth = 0.0f;
        float filterCutoffDepth = 0.0f;
        float modWheelDepth = 1.0f;
    };

    struct PolyModParametersV2
    {
        float oscBToOscPitch = 0.0f;
        float envToOscPitch = 0.0f;
        float oscBToPulseWidth = 0.0f;
        float envToPulseWidth = 0.0f;
        float oscBToFilterCutoff = 0.0f;
        float envToFilterCutoff = 0.0f;
    };

    struct PerformanceParametersV2
    {
        float glideTimeSeconds = 0.0f;
        coolsynth::parameters::PlayModeChoice playMode =
            coolsynth::parameters::PlayModeChoice::poly;
        coolsynth::parameters::KeyPriorityChoice keyPriority =
            coolsynth::parameters::KeyPriorityChoice::last;
        float pitchBendRangeSemitones = 2.0f;
        float vintageAmount = 0.0f;
        float panSpread = 0.0f;
        float velocityToAmp = 1.0f;
        float velocityToFilter = 0.0f;
    };

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

    struct DelayParametersV2
    {
        bool enabled = false;
        float timeMs = 250.0f;
        float feedback = 0.25f;
        float mix = 0.0f;
    };

    struct ReverbParametersV2
    {
        bool enabled = false;
        float size = 0.4f;
        float damping = 0.5f;
        float mix = 0.2f;
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

    struct BlockRenderParametersV2
    {
        OscillatorParametersV2 oscA;
        OscillatorParametersV2 oscB;
        MixerParametersV2 mixer;
        FilterParametersV2 filter;
        EnvelopeParameters filterEnvelope;
        EnvelopeParameters ampEnvelope;
        LfoParametersV2 lfo;
        PolyModParametersV2 polyMod;
        PerformanceParametersV2 performance;
        ArpParametersV2 arp;
        DriveParametersV2 drive;
        PhaserParametersV2 phaser;
        ChorusParametersV2 chorus;
        DelayParametersV2 delay;
        ReverbParametersV2 reverb;
        CompressorParametersV2 compressor;
        float masterGainLinear = 1.0f;
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

    struct ParameterValuePointersV2
    {
        std::atomic<float>* oscAWave = nullptr;
        std::atomic<float>* oscAOctave = nullptr;
        std::atomic<float>* oscAFineCents = nullptr;
        std::atomic<float>* oscALevel = nullptr;
        std::atomic<float>* oscAPulseWidth = nullptr;
        std::atomic<float>* oscASyncEnabled = nullptr;
        std::atomic<float>* oscBWave = nullptr;
        std::atomic<float>* oscBOctave = nullptr;
        std::atomic<float>* oscBFineCents = nullptr;
        std::atomic<float>* oscBLevel = nullptr;
        std::atomic<float>* oscBPulseWidth = nullptr;
        std::atomic<float>* oscBLowFrequencyMode = nullptr;
        std::atomic<float>* noiseLevel = nullptr;
        std::atomic<float>* filterCutoffHz = nullptr;
        std::atomic<float>* filterResonance = nullptr;
        std::atomic<float>* filterEnvAmount = nullptr;
        std::atomic<float>* filterKeyTracking = nullptr;
        std::atomic<float>* filterAttackMs = nullptr;
        std::atomic<float>* filterDecayMs = nullptr;
        std::atomic<float>* filterSustain = nullptr;
        std::atomic<float>* filterReleaseMs = nullptr;
        std::atomic<float>* ampAttackMs = nullptr;
        std::atomic<float>* ampDecayMs = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampReleaseMs = nullptr;
        std::atomic<float>* lfoRateHz = nullptr;
        std::atomic<float>* lfoWave = nullptr;
        std::atomic<float>* lfoToOscPitch = nullptr;
        std::atomic<float>* lfoToPulseWidth = nullptr;
        std::atomic<float>* lfoToFilterCutoff = nullptr;
        std::atomic<float>* modWheelToLfoDepth = nullptr;
        std::atomic<float>* polyModOscBToOscPitch = nullptr;
        std::atomic<float>* polyModEnvToOscPitch = nullptr;
        std::atomic<float>* polyModOscBToPulseWidth = nullptr;
        std::atomic<float>* polyModEnvToPulseWidth = nullptr;
        std::atomic<float>* polyModOscBToFilterCutoff = nullptr;
        std::atomic<float>* polyModEnvToFilterCutoff = nullptr;
        std::atomic<float>* glideTimeMs = nullptr;
        std::atomic<float>* playMode = nullptr;
        std::atomic<float>* keyPriority = nullptr;
        std::atomic<float>* pitchBendRangeSemitones = nullptr;
        std::atomic<float>* vintageAmount = nullptr;
        std::atomic<float>* panSpread = nullptr;
        std::atomic<float>* velocityToAmp = nullptr;
        std::atomic<float>* velocityToFilter = nullptr;
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
        std::atomic<float>* reverbEnabled = nullptr;
        std::atomic<float>* reverbSize = nullptr;
        std::atomic<float>* reverbDamping = nullptr;
        std::atomic<float>* reverbMix = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
        // --- v3 additions ---
        std::atomic<float>* timbre = nullptr;
        std::atomic<float>* excite = nullptr;
        std::atomic<float>* phaserEnabled = nullptr;
        std::atomic<float>* phaserRateHz = nullptr;
        std::atomic<float>* phaserDepth = nullptr;
        std::atomic<float>* compressorEnabled = nullptr;
        std::atomic<float>* compressorAmount = nullptr;
        std::atomic<float>* compressorMix = nullptr;
    };

    using ParamMemberPtr = std::atomic<float>* ParameterValuePointersV2::*;

    inline const std::array<std::pair<const char*, ParamMemberPtr>,
        coolsynth::parameters::allParameterIds.size()> kParamPtrBindings = {{
        { coolsynth::parameters::ids::oscAWave,                 &ParameterValuePointersV2::oscAWave                 },
        { coolsynth::parameters::ids::oscAOctave,               &ParameterValuePointersV2::oscAOctave               },
        { coolsynth::parameters::ids::oscAFineCents,            &ParameterValuePointersV2::oscAFineCents            },
        { coolsynth::parameters::ids::oscALevel,                &ParameterValuePointersV2::oscALevel                },
        { coolsynth::parameters::ids::oscAPulseWidth,           &ParameterValuePointersV2::oscAPulseWidth           },
        { coolsynth::parameters::ids::oscASyncEnabled,          &ParameterValuePointersV2::oscASyncEnabled          },
        { coolsynth::parameters::ids::oscBWave,                 &ParameterValuePointersV2::oscBWave                 },
        { coolsynth::parameters::ids::oscBOctave,               &ParameterValuePointersV2::oscBOctave               },
        { coolsynth::parameters::ids::oscBFineCents,            &ParameterValuePointersV2::oscBFineCents            },
        { coolsynth::parameters::ids::oscBLevel,                &ParameterValuePointersV2::oscBLevel                },
        { coolsynth::parameters::ids::oscBPulseWidth,           &ParameterValuePointersV2::oscBPulseWidth           },
        { coolsynth::parameters::ids::oscBLowFrequencyMode,     &ParameterValuePointersV2::oscBLowFrequencyMode     },
        { coolsynth::parameters::ids::noiseLevel,               &ParameterValuePointersV2::noiseLevel               },
        { coolsynth::parameters::ids::filterCutoffHz,           &ParameterValuePointersV2::filterCutoffHz           },
        { coolsynth::parameters::ids::filterResonance,          &ParameterValuePointersV2::filterResonance          },
        { coolsynth::parameters::ids::filterEnvAmount,          &ParameterValuePointersV2::filterEnvAmount          },
        { coolsynth::parameters::ids::filterKeyTracking,        &ParameterValuePointersV2::filterKeyTracking        },
        { coolsynth::parameters::ids::filterAttackMs,           &ParameterValuePointersV2::filterAttackMs           },
        { coolsynth::parameters::ids::filterDecayMs,            &ParameterValuePointersV2::filterDecayMs            },
        { coolsynth::parameters::ids::filterSustain,            &ParameterValuePointersV2::filterSustain            },
        { coolsynth::parameters::ids::filterReleaseMs,          &ParameterValuePointersV2::filterReleaseMs          },
        { coolsynth::parameters::ids::ampAttackMs,              &ParameterValuePointersV2::ampAttackMs              },
        { coolsynth::parameters::ids::ampDecayMs,               &ParameterValuePointersV2::ampDecayMs               },
        { coolsynth::parameters::ids::ampSustain,               &ParameterValuePointersV2::ampSustain               },
        { coolsynth::parameters::ids::ampReleaseMs,             &ParameterValuePointersV2::ampReleaseMs             },
        { coolsynth::parameters::ids::lfoRateHz,                &ParameterValuePointersV2::lfoRateHz                },
        { coolsynth::parameters::ids::lfoWave,                  &ParameterValuePointersV2::lfoWave                  },
        { coolsynth::parameters::ids::lfoToOscPitch,            &ParameterValuePointersV2::lfoToOscPitch            },
        { coolsynth::parameters::ids::lfoToPulseWidth,          &ParameterValuePointersV2::lfoToPulseWidth          },
        { coolsynth::parameters::ids::lfoToFilterCutoff,        &ParameterValuePointersV2::lfoToFilterCutoff        },
        { coolsynth::parameters::ids::modWheelToLfoDepth,       &ParameterValuePointersV2::modWheelToLfoDepth       },
        { coolsynth::parameters::ids::polyModOscBToOscPitch,    &ParameterValuePointersV2::polyModOscBToOscPitch    },
        { coolsynth::parameters::ids::polyModEnvToOscPitch,     &ParameterValuePointersV2::polyModEnvToOscPitch     },
        { coolsynth::parameters::ids::polyModOscBToPulseWidth,  &ParameterValuePointersV2::polyModOscBToPulseWidth  },
        { coolsynth::parameters::ids::polyModEnvToPulseWidth,   &ParameterValuePointersV2::polyModEnvToPulseWidth   },
        { coolsynth::parameters::ids::polyModOscBToFilterCutoff,&ParameterValuePointersV2::polyModOscBToFilterCutoff},
        { coolsynth::parameters::ids::polyModEnvToFilterCutoff, &ParameterValuePointersV2::polyModEnvToFilterCutoff },
        { coolsynth::parameters::ids::glideTimeMs,              &ParameterValuePointersV2::glideTimeMs              },
        { coolsynth::parameters::ids::playMode,                 &ParameterValuePointersV2::playMode                 },
        { coolsynth::parameters::ids::keyPriority,              &ParameterValuePointersV2::keyPriority              },
        { coolsynth::parameters::ids::pitchBendRangeSemitones,  &ParameterValuePointersV2::pitchBendRangeSemitones  },
        { coolsynth::parameters::ids::vintageAmount,            &ParameterValuePointersV2::vintageAmount            },
        { coolsynth::parameters::ids::panSpread,                &ParameterValuePointersV2::panSpread                },
        { coolsynth::parameters::ids::velocityToAmp,            &ParameterValuePointersV2::velocityToAmp            },
        { coolsynth::parameters::ids::velocityToFilter,         &ParameterValuePointersV2::velocityToFilter         },
        { coolsynth::parameters::ids::arpEnabled,               &ParameterValuePointersV2::arpEnabled               },
        { coolsynth::parameters::ids::arpInternalTempoBpm,      &ParameterValuePointersV2::arpInternalTempoBpm      },
        { coolsynth::parameters::ids::arpRateDivision,          &ParameterValuePointersV2::arpRateDivision          },
        { coolsynth::parameters::ids::arpPattern,               &ParameterValuePointersV2::arpPattern               },
        { coolsynth::parameters::ids::arpOctaveRange,           &ParameterValuePointersV2::arpOctaveRange           },
        { coolsynth::parameters::ids::arpGate,                  &ParameterValuePointersV2::arpGate                  },
        { coolsynth::parameters::ids::arpSwing,                 &ParameterValuePointersV2::arpSwing                 },
        { coolsynth::parameters::ids::arpChance,                &ParameterValuePointersV2::arpChance                },
        { coolsynth::parameters::ids::arpRatchetCount,          &ParameterValuePointersV2::arpRatchetCount          },
        { coolsynth::parameters::ids::arpRatchetChance,         &ParameterValuePointersV2::arpRatchetChance         },
        { coolsynth::parameters::ids::arpAccentEvery,           &ParameterValuePointersV2::arpAccentEvery           },
        { coolsynth::parameters::ids::arpAccentAmount,          &ParameterValuePointersV2::arpAccentAmount          },
        { coolsynth::parameters::ids::arpLatch,                 &ParameterValuePointersV2::arpLatch                 },
        { coolsynth::parameters::ids::driveEnabled,             &ParameterValuePointersV2::driveEnabled             },
        { coolsynth::parameters::ids::driveAmount,              &ParameterValuePointersV2::driveAmount              },
        { coolsynth::parameters::ids::driveMix,                 &ParameterValuePointersV2::driveMix                 },
        { coolsynth::parameters::ids::chorusEnabled,            &ParameterValuePointersV2::chorusEnabled            },
        { coolsynth::parameters::ids::chorusRateHz,             &ParameterValuePointersV2::chorusRateHz             },
        { coolsynth::parameters::ids::chorusDepth,              &ParameterValuePointersV2::chorusDepth              },
        { coolsynth::parameters::ids::chorusMix,                &ParameterValuePointersV2::chorusMix                },
        { coolsynth::parameters::ids::delayEnabled,             &ParameterValuePointersV2::delayEnabled             },
        { coolsynth::parameters::ids::delayTimeMs,              &ParameterValuePointersV2::delayTimeMs              },
        { coolsynth::parameters::ids::delayFeedback,            &ParameterValuePointersV2::delayFeedback            },
        { coolsynth::parameters::ids::delayMix,                 &ParameterValuePointersV2::delayMix                 },
        { coolsynth::parameters::ids::reverbEnabled,            &ParameterValuePointersV2::reverbEnabled            },
        { coolsynth::parameters::ids::reverbSize,               &ParameterValuePointersV2::reverbSize               },
        { coolsynth::parameters::ids::reverbDamping,            &ParameterValuePointersV2::reverbDamping            },
        { coolsynth::parameters::ids::reverbMix,                &ParameterValuePointersV2::reverbMix                },
        { coolsynth::parameters::ids::masterGainDb,             &ParameterValuePointersV2::masterGainDb             },
        // --- v3 additions ---
        { coolsynth::parameters::ids::timbre,                   &ParameterValuePointersV2::timbre                   },
        { coolsynth::parameters::ids::excite,                   &ParameterValuePointersV2::excite                   },
        { coolsynth::parameters::ids::phaserEnabled,            &ParameterValuePointersV2::phaserEnabled            },
        { coolsynth::parameters::ids::phaserRateHz,             &ParameterValuePointersV2::phaserRateHz             },
        { coolsynth::parameters::ids::phaserDepth,              &ParameterValuePointersV2::phaserDepth              },
        { coolsynth::parameters::ids::compressorEnabled,        &ParameterValuePointersV2::compressorEnabled        },
        { coolsynth::parameters::ids::compressorAmount,         &ParameterValuePointersV2::compressorAmount         },
        { coolsynth::parameters::ids::compressorMix,            &ParameterValuePointersV2::compressorMix            },
    }};

    static_assert(kParamPtrBindings.size() == coolsynth::parameters::allParameterIds.size(),
                  "kParamPtrBindings must have one entry per parameter in allParameterIds");
}
