#pragma once

#include <atomic>

namespace coolsynth::synth
{
    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct BlockRenderParameters
    {
        EnvelopeParameters ampEnvelope;
        float masterGainLinear = 1.0f;
    };

    struct ParameterValuePointers
    {
        std::atomic<float>* ampAttackMs = nullptr;
        std::atomic<float>* ampDecayMs = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampReleaseMs = nullptr;
        std::atomic<float>* masterGainDb = nullptr;
    };
}
