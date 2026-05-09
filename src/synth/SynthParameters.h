#pragma once

#include <atomic>
#include "parameters/ParameterIDs.h"

namespace coolsynth::synth
{
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
}
