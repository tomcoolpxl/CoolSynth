#pragma once

#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthParameters.h"

namespace coolsynth::synth
{
    class SynthVoice final
    {
    public:
        SynthVoice();

        void prepare(const juce::dsp::ProcessSpec& spec);
        void setRandomSeed(uint32_t seed) noexcept;
        void setNextVoiceSourceParameters(const OscillatorParametersV2& oscA,
                                          const OscillatorParametersV2& oscB,
                                          const MixerParametersV2& mixer) noexcept;
        void setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;
        void setNextFilterParameters(const FilterParameters& parameters) noexcept;
        void setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept;
        void setOutputLevel(float level) noexcept;
        void setPitchBendSemitones(float semitones) noexcept;

        void startNote(int midiNoteNumber,
                       float velocity,
                       juce::SynthesiserSound* sound = nullptr,
                       int currentPitchWheelPosition = 0);
        void stopNote(float velocity, bool allowTailOff);
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples);
        void forceStop() noexcept;

        [[nodiscard]] bool isActive() const noexcept { return active; }
        [[nodiscard]] bool isReleasing() const noexcept { return releasing; }
        [[nodiscard]] int getCurrentMidiNoteNumber() const noexcept { return currentMidiNoteNumber; }

    private:
        struct OscillatorState
        {
            float phase = 0.0f;
            float frequencyHz = 0.0f;
            float pulseWidth = 0.5f;
            coolsynth::parameters::OscillatorWaveShape waveShape =
                coolsynth::parameters::OscillatorWaveShape::saw;
        };

        juce::ADSR::Parameters makeJuceEnvelopeParameters() const noexcept;
        float computeOscillatorFrequencyHz(const OscillatorParametersV2& parameters) const noexcept;
        int computeNoteStartRampSamples() const noexcept;
        float renderOscillatorSample(OscillatorState& oscillator,
                                     bool forcePhaseReset) noexcept;
        float renderMixedVoiceSample() noexcept;
        float consumeNoteStartRamp() noexcept;
        float nextNoiseSample() noexcept;
        uint32_t advanceRandomState() noexcept;
        static float renderWaveSample(float phase,
                                      coolsynth::parameters::OscillatorWaveShape waveShape,
                                      float pulseWidth) noexcept;
        static float mapNormalizedResonanceToQ(float normalized) noexcept;
        float clampCutoffToPreparedRange(float cutoffHz) const noexcept;
        void primeFilterForCurrentTargets() noexcept;
        void resetVoiceState() noexcept;

        juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHzSmoother;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> resonanceQSmoother;
        juce::ADSR ampEnvelope;
        OscillatorParametersV2 nextOscillatorAParameters {
            coolsynth::parameters::OscillatorWaveShape::saw,
            2,
            0.0f,
            1.0f,
            0.5f,
            false,
            false,
        };
        OscillatorParametersV2 nextOscillatorBParameters {
            coolsynth::parameters::OscillatorWaveShape::saw,
            2,
            0.0f,
            0.0f,
            0.5f,
            false,
            false,
        };
        MixerParametersV2 nextMixerParameters;
        EnvelopeParameters nextEnvelopeParameters;
        FilterParameters nextFilterParameters;
        OscillatorState oscAState;
        OscillatorState oscBState;

        double currentSampleRate = 0.0;
        float baseFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
        float outputLevel = 1.0f;
        float currentPitchBendSemitones = 0.0f;
        uint32_t baseRandomSeed = 0x12345678u;
        uint32_t randomState = 0x12345678u;
        int noteStartRampSamplesRemaining = 0;
        int noteStartRampMinSamples = 1;
        int noteStartRampTotalSamples = 1;
        int currentMidiNoteNumber = -1;
        bool active = false;
        bool releasing = false;
    };
}
