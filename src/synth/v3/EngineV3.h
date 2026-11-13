#pragma once

#include <memory>
#include <span>

#include <juce_audio_basics/juce_audio_basics.h>

#include "synth/GlobalFxRack.h"
#include "Part.h"

/*
    V3 engine (DESIGN_V3.md section 2): one part for now (two in phase 7.2), then the effect
    rack, then the master gain and a soft clipper. The rack is the existing drive, phaser,
    chorus, delay, plate and compressor chain until effects become slot types in phase 5.
*/
namespace coolsynth::synth::v3
{
    struct EngineSettings
    {
        PartSettings part {};
        coolsynth::synth::DriveParametersV2 drive {};
        coolsynth::synth::PhaserParametersV2 phaser {};
        coolsynth::synth::ChorusParametersV2 chorus {};
        coolsynth::synth::DelayParametersV2 delay {};
        coolsynth::synth::ReverbParametersV2 reverb {};
        coolsynth::synth::CompressorParametersV2 compressor {};
        float masterGainLinear = 1.0f;
    };

    class EngineV3 final
    {
    public:
        static constexpr double masterGainRampSeconds = 0.02;

        EngineV3();

        void prepare(double sampleRate, int maxBlockSize, int outputChannels);
        void releaseResources() noexcept {}
        void render(juce::AudioBuffer<float>& buffer,
                    std::span<const EngineMidiEvent> events,
                    const EngineSettings& settings,
                    const EngineTransportInfo& transport = {}) noexcept;
        void panic() noexcept;

        [[nodiscard]] int getLatencySamples() const noexcept { return part->latencySamples(); }
        [[nodiscard]] Part& partForTesting() noexcept { return *part; }

        /** Above 0.8 the output bends into a tanh knee and never passes 1. */
        static float softClip(float x) noexcept
        {
            constexpr float threshold = 0.8f;
            const float magnitude = std::abs(x);
            if (magnitude <= threshold)
                return x;
            return std::copysign(threshold + (1.0f - threshold) * std::tanh((magnitude - threshold) / (1.0f - threshold)), x);
        }

    private:
        std::unique_ptr<Part> part; // large: a pool of voices with their slot arenas
        coolsynth::synth::GlobalFxRack fxRack;
        juce::AudioBuffer<float> work; // the stereo part output the rack runs on
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;
        bool prepared = false;
        bool masterPrimed = false;
    };
}
