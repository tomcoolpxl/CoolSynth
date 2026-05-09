#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthParameters.h"

namespace coolsynth::synth
{
    /**
     * A single synthesizer voice responsible for rendering one selectable waveform note.
     */
    class SynthVoice final : public juce::SynthesiserVoice
    {
    public:
        SynthVoice();

        void prepare(const juce::dsp::ProcessSpec& spec);
        void setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept;
        void setNextFilterParameters(const FilterParameters& parameters) noexcept;
        void setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept;

        bool canPlaySound(juce::SynthesiserSound* sound) override;
        void startNote(int midiNoteNumber,
                       float velocity,
                       juce::SynthesiserSound* sound,
                       int currentPitchWheelPosition) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int newPitchWheelValue) override;
        void controllerMoved(int controllerNumber, int newControllerValue) override;
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples) override;

    private:
        juce::ADSR::Parameters makeJuceEnvelopeParameters() const noexcept;
        static float renderWaveSample(float phase,
                                      coolsynth::parameters::WaveformChoice waveform) noexcept;
        static float mapNormalizedResonanceToQ(float normalized) noexcept;
        float clampCutoffToPreparedRange(float cutoffHz) const noexcept;
        void primeFilterForCurrentTargets() noexcept;
        void resetVoiceState() noexcept;

        juce::dsp::Oscillator<float> oscillator;
        juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHzSmoother;
        juce::ADSR ampEnvelope;
        EnvelopeParameters nextEnvelopeParameters;
        FilterParameters nextFilterParameters;
        float lastAppliedResonanceQ = 0.0f;
        coolsynth::parameters::WaveformChoice currentWaveform =
            coolsynth::parameters::WaveformChoice::saw;

        double currentSampleRate = 0.0;
        float currentFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
    };
}
