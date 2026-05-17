#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "GlobalDelay.h"
#include "Phaser.h"
#include "SynthParameters.h"

namespace coolsynth::synth
{
    class GlobalFxRack final
    {
    public:
        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void reset() noexcept;
        void clear() noexcept;
        void process(juce::AudioBuffer<float>& buffer,
                     const DriveParametersV2& drive,
                     const PhaserParametersV2& phaser,
                     const ChorusParametersV2& chorus,
                     const DelayParametersV2& delay,
                     const ReverbParametersV2& reverb) noexcept;

        [[nodiscard]] static double estimateTailLengthSeconds(const DelayParametersV2& delay,
                                                              const ReverbParametersV2& reverb) noexcept;

    private:
        void processDrive(juce::AudioBuffer<float>& buffer, const DriveParametersV2& parameters) noexcept;
        void processChorus(juce::AudioBuffer<float>& buffer, const ChorusParametersV2& parameters) noexcept;
        void processDelay(juce::AudioBuffer<float>& buffer, const DelayParametersV2& parameters) noexcept;
        void processReverb(juce::AudioBuffer<float>& buffer, const ReverbParametersV2& parameters) noexcept;

        GlobalDelay delay;
        Phaser phaser;
        juce::dsp::Chorus<float> chorus;
        juce::dsp::Reverb reverb;
        juce::Reverb::Parameters reverbParameters;
        bool chorusWasAudible = false;
        bool delayWasAudible = false;
        bool reverbWasAudible = false;
        bool prepared = false;

        // C5: per-sample FX mix smoothers — eliminates zipper noise under automation
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveMixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> chorusMixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbMixSmoothed;
        juce::AudioBuffer<float> dryBuffer;   // pre-sized scratch for dry-signal preservation
        std::vector<float> mixRampScratch;    // pre-sized per-sample mix ramp (avoids per-channel smoother advance)
    };
}
