#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"

namespace coolsynth::synth
{
    /**
     * Global delay effect stage, voiced like a tape or bucket-brigade echo.
     *
     * Own delay line with Hermite-interpolated fractional reads. The delay
     * time follows the knob through a slew, so a time change glides the pitch
     * of the repeats instead of clicking or jumping. The feedback path runs
     * through a soft clip, a one-pole low-pass and a one-pole high-pass, so
     * every repeat comes back a little darker and thinner than the last.
     *
     * Tape mode echoes each channel in place. Ping-pong sums the input to
     * mono into the left line and cross-couples the lines through the same
     * feedback path, so the echoes alternate left, right, left at the delay
     * time, each bounce scaled by the feedback. Changing mode clears the lines.
     */
    class GlobalDelay final
    {
    public:
        static constexpr float maxFeedback = 0.85f;

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void reset() noexcept;
        void clear() noexcept;
        /** The next block starts at the settings instead of gliding to them. */
        void rearm() noexcept { primed = false; }
        void process(juce::AudioBuffer<float>& buffer,
                     const DelayParametersV2& parameters) noexcept;

    private:
        static constexpr float minDelayTimeMs = 1.0f;
        static constexpr float maxDelayTimeMs = 4000.0f; // a synced bar at 60 BPM
        static constexpr double timeSlewSeconds = 0.08;
        static constexpr double parameterSmoothingSeconds = 0.02;
        static constexpr float feedbackLowPassHz = 5500.0f;
        static constexpr float feedbackHighPassHz = 110.0f;

        struct Channel
        {
            std::vector<float> buffer;
            int writeIndex = 0;
            float lowPassState = 0.0f;
            float highPassState = 0.0f;
            float highPassPreviousInput = 0.0f;
        };

        float clampDelayMs(float timeMs) const noexcept;
        float delayMsToSamples(float timeMs) const noexcept;
        float readHermite(const Channel& channel, float delaySamples) const noexcept;
        float feedbackFilter(Channel& channel, float wet) const noexcept;
        void updateTargets(const DelayParametersV2& parameters) noexcept;

        std::vector<Channel> channels;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoother;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
        double currentSampleRate = 0.0;
        int bufferLength = 0;
        float currentDelaySamples = 0.0f;
        float targetDelaySamples = 0.0f;
        float timeSlewCoefficient = 0.0f;
        float lowPassCoefficient = 0.0f;
        float highPassCoefficient = 0.0f;
        bool prepared = false;
        bool pingPongActive = false;
        bool primed = false;
    };
}
