#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Compressor.h"
#include "GlobalDelay.h"
#include "HalfBand2x.h"
#include "LoFi.h"
#include "ModernFx.h"
#include "Phaser.h"
#include "PlateReverb.h"
#include "StereoChorus.h"
#include "SynthParameters.h"

namespace coolsynth::synth
{
    /** The stages added on the V3 engine, in one bundle so the rack's signature stays put. */
    struct ExtraFxParameters
    {
        LoFiParameters lofi {};
        FlangerParameters flanger {};
        EqParameters eq {};
        PumpParameters pump {};
    };

    /**
        Chain: drive, lo-fi, phaser, flanger, chorus, delay, reverb, compressor, EQ, pump.
    */
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
                     const ReverbParametersV2& reverb,
                     const CompressorParametersV2& compressor,
                     const ExtraFxParameters& extra = ExtraFxParameters {}) noexcept;

        [[nodiscard]] static double estimateTailLengthSeconds(const DelayParametersV2& delay,
                                                              const ReverbParametersV2& reverb) noexcept;

    private:
        void processChunk(juce::AudioBuffer<float>& buffer,
                          const DriveParametersV2& drive,
                          const PhaserParametersV2& phaser,
                          const ChorusParametersV2& chorus,
                          const DelayParametersV2& delay,
                          const ReverbParametersV2& reverb,
                          const CompressorParametersV2& compressor,
                          const ExtraFxParameters& extra) noexcept;
        void processDrive(juce::AudioBuffer<float>& buffer, const DriveParametersV2& parameters) noexcept;
        void processChorus(juce::AudioBuffer<float>& buffer, const ChorusParametersV2& parameters) noexcept;
        void processDelay(juce::AudioBuffer<float>& buffer, const DelayParametersV2& parameters) noexcept;
        void processReverb(juce::AudioBuffer<float>& buffer, const ReverbParametersV2& parameters) noexcept;

        GlobalDelay delay;
        Phaser phaser;
        Compressor compressor;
        StereoChorus chorus;
        PlateReverb reverb;
        LoFi lofi;
        Flanger flanger;
        MasterEq eq;
        Pump pump;
        std::vector<HalfBandUpsampler2x> driveUpsamplers;  // drive shapes at 2x so its harmonics do not alias
        std::vector<HalfBandDecimator2x> driveDecimators;
        std::vector<float> driveToneStates;                 // one-pole low-pass on the shaped signal
        double currentSampleRate = 48000.0;
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
