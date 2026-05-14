#pragma once

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
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> resonanceQSmoother;
        juce::ADSR ampEnvelope;
        EnvelopeParameters nextEnvelopeParameters;
        FilterParameters nextFilterParameters;
        coolsynth::parameters::WaveformChoice currentWaveform =
            coolsynth::parameters::WaveformChoice::saw;

        double currentSampleRate = 0.0;
        float baseFrequencyHz = 0.0f;
        float velocityGain = 0.0f;
        float outputLevel = 1.0f;
        float currentPitchBendSemitones = 0.0f;
        int currentMidiNoteNumber = -1;
        bool active = false;
        bool releasing = false;
    };
}
