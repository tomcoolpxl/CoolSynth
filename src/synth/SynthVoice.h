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
        void setNextFilterParameters(const FilterParametersV2& parameters) noexcept;
        void setNextFilterEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;
        void setNextModulationParameters(const LfoParametersV2& lfo,
                                         const PolyModParametersV2& polyMod,
                                         const PerformanceParametersV2& performance) noexcept;
        void setGlobalLfoState(float phase, float modWheel) noexcept;
        void setPan(float panLeft, float panRight) noexcept;
        void setVintageDriftCents(float cents) noexcept;
        void setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept;
        void setOutputLevel(float level) noexcept;
        void setPitchBendSemitones(float semitones) noexcept;
        void setGlideFromNote(int fromMidiNoteNumber, float glideTimeSeconds) noexcept;

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
        [[nodiscard]] float getVintageDriftCentsForTesting() const noexcept { return vintageDriftCents; }
        [[nodiscard]] float getCurrentGlideLog2ForTesting() const noexcept { return glideOffsetLog2; }

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
        juce::ADSR::Parameters makeJuceFilterEnvelopeParameters() const noexcept;
        float computeOscillatorFrequencyHz(const OscillatorParametersV2& parameters) const noexcept;
        int computeNoteStartRampSamples() const noexcept;
        float renderOscillatorSample(OscillatorState& oscillator,
                                     bool forcePhaseReset) noexcept;
        float consumeNoteStartRamp() noexcept;
        float nextNoiseSample() noexcept;
        uint32_t advanceRandomState() noexcept;
        static float renderWaveSample(float phase,
                                      coolsynth::parameters::OscillatorWaveShape waveShape,
                                      float pulseWidth) noexcept;
        static coolsynth::parameters::OscillatorWaveShape lfoShapeToOscShape(
            coolsynth::parameters::LfoWaveShape shape) noexcept;
        static float mapNormalizedResonanceToQ(float normalized) noexcept;
        float clampCutoffToPreparedRange(float cutoffHz) const noexcept;
        void primeFilterForCurrentTargets() noexcept;
        void resetVoiceState() noexcept;

        juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHzSmoother;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> resonanceQSmoother;
        juce::ADSR ampEnvelope;
        juce::ADSR filterEnvelope;
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
        EnvelopeParameters nextFilterEnvelopeParameters;
        FilterParametersV2 nextFilterParameters;
        LfoParametersV2 nextLfoParameters;
        PolyModParametersV2 nextPolyModParameters;
        PerformanceParametersV2 nextPerformanceParameters;
        OscillatorState oscAState;
        OscillatorState oscBState;

        double currentSampleRate = 0.0;
        float baseFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
        float outputLevel = 1.0f;
        float currentPitchBendSemitones = 0.0f;
        float blockStartGlobalLfoPhase = 0.0f;
        float currentModWheelValue = 0.0f;
        float panLeftVolume = 1.0f;
        float panRightVolume = 1.0f;
        float vintageDriftCents = 0.0f;
        float glideOffsetLog2 = 0.0f;
        float glideStepLog2PerSample = 0.0f;
        uint32_t baseRandomSeed = 0x12345678u;
        uint32_t randomState = 0x12345678u;
        float pinkB0 = 0.0f;
        float pinkB1 = 0.0f;
        float pinkB2 = 0.0f;
        int noteStartRampSamplesRemaining = 0;
        int noteStartRampMinSamples = 1;
        int noteStartRampTotalSamples = 1;
        int currentMidiNoteNumber = -1;
        bool active = false;
        bool releasing = false;
    };
}
