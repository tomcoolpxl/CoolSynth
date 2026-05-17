#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthParameters.h"

namespace coolsynth::synth
{
    // 4-stage all-pass phaser with a sine LFO. Stereo channels run with a 90 deg
    // LFO phase offset to give the classic widening swirl. Feedback is fixed.
    class Phaser final
    {
    public:
        static constexpr int numStages = 4;
        static constexpr float feedbackAmount = 0.35f;
        static constexpr float notchMinHz = 80.0f;
        static constexpr float notchMaxHz = 5000.0f;
        static constexpr float maxWetMix = 0.5f;

        void prepare(double newSampleRate, int samplesPerBlock, int outputChannelCount);
        void reset() noexcept;
        void process(juce::AudioBuffer<float>& buffer, const PhaserParametersV2& parameters) noexcept;

    private:
        struct AllPassState
        {
            float x1 = 0.0f;
            float y1 = 0.0f;
        };

        struct ChannelState
        {
            std::array<AllPassState, numStages> stages {};
            float feedback = 0.0f;
            float lfoPhase = 0.0f;
        };

        std::vector<ChannelState> channels;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMixSmoothed;
        double sampleRate = 44100.0;
        bool prepared = false;
        bool wasAudible = false;
    };
}
