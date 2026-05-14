#pragma once

#include <atomic>
#include <cstdint>
#include "parameters/ParameterIDs.h"

namespace coolsynth::synth
{
    inline constexpr int defaultVoiceCount = 8;
    inline constexpr double masterGainRampSeconds = 0.02;
    inline constexpr int maxEngineEventsPerBlock = 1024;

    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct FilterParameters
    {
        float cutoffHz = 10000.0f;
        float resonanceNormalized = 0.1f;
    };

    struct DelayParameters
    {
        float timeMs = 250.0f;
        float feedback = 0.25f;
        float mix = 0.0f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        FilterParameters filter;
        DelayParameters delay;
        coolsynth::parameters::WaveformChoice waveform = coolsynth::parameters::WaveformChoice::saw;
        float masterGainLinear = 1.0f;
    };

    struct ParameterValuePointers
    {
        std::atomic<float>* waveform = nullptr;
        std::atomic<float>* ampAttackMs = nullptr;
        std::atomic<float>* ampDecayMs = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampReleaseMs = nullptr;
        std::atomic<float>* filterCutoffHz = nullptr;
        std::atomic<float>* filterResonance = nullptr;
        std::atomic<float>* delayTimeMs = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayMix = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
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
        ChorusParametersV2 chorus;
        DelayParametersV2 delay;
        ReverbParametersV2 reverb;
        float masterGainLinear = 1.0f;
    };

    enum class EngineMidiEventType : uint8_t
    {
        noteOn,
        noteOff,
        pitchBend,
        modWheel,
        sustainPedal,
    };

    struct EngineMidiEvent
    {
        EngineMidiEventType type = EngineMidiEventType::noteOn;
        int sampleOffset = 0;
        uint8_t noteNumber = 0;
        float value = 0.0f;
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
    };
}
