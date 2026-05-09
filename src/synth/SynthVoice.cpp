#include "SynthVoice.h"
#include "SynthSound.h"

namespace coolsynth::synth
{
    SynthVoice::SynthVoice()
    {
        oscillator.initialise([this](float phase)
        {
            return renderWaveSample(phase, currentWaveform);
        });
    }

    void SynthVoice::prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        oscillator.prepare(spec);
        ampEnvelope.setSampleRate(spec.sampleRate);
    }

    void SynthVoice::setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept
    {
        nextEnvelopeParameters = parameters;
    }

    bool SynthVoice::canPlaySound(juce::SynthesiserSound* sound)
    {
        return dynamic_cast<SynthSound*>(sound) != nullptr;
    }

    void SynthVoice::startNote(int midiNoteNumber,
                               float velocity,
                               juce::SynthesiserSound* /*sound*/,
                               int /*currentPitchWheelPosition*/)
    {
        currentFrequencyHz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        oscillator.setFrequency(currentFrequencyHz);
        oscillator.reset();

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());
        ampEnvelope.noteOn();

        velocityGain = velocity;
    }

    void SynthVoice::stopNote(float /*velocity*/, bool allowTailOff)
    {
        if (allowTailOff)
        {
            ampEnvelope.noteOff();
        }
        else
        {
            ampEnvelope.reset();
            clearCurrentNote();
        }
    }

    void SynthVoice::pitchWheelMoved(int /*newPitchWheelValue*/) {}

    void SynthVoice::controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) {}

    void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     int startSample,
                                     int numSamples)
    {
        if (!isVoiceActive())
            return;

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());

        // Note: JUCE's dsp::Oscillator process expects a ProcessContext.
        // For simplicity in Phase 4, we use the sample-by-sample approach as requested.
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float envValue = ampEnvelope.getNextSample();
            const float oscValue = oscillator.processSample(0.0f);
            const float sampleValue = oscValue * envValue * velocityGain;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            {
                outputBuffer.addSample(channel, startSample + sample, sampleValue);
            }

            if (!ampEnvelope.isActive())
            {
                clearCurrentNote();
                oscillator.reset();
                break;
            }
        }
    }

    void SynthVoice::setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept
    {
        currentWaveform = waveform;
    }

    float SynthVoice::renderWaveSample(float phase,
                                       coolsynth::parameters::WaveformChoice waveform) noexcept
    {
        switch (waveform)
        {
            case coolsynth::parameters::WaveformChoice::sine:
                return std::sin(phase);

            case coolsynth::parameters::WaveformChoice::square:
                return phase < 0.0f ? -1.0f : 1.0f;

            case coolsynth::parameters::WaveformChoice::saw:
                return juce::jmap(phase,
                                  -juce::MathConstants<float>::pi,
                                  juce::MathConstants<float>::pi,
                                  -1.0f,
                                  1.0f);
        }

        return std::sin(phase);
    }

    juce::ADSR::Parameters SynthVoice::makeJuceEnvelopeParameters() const noexcept
    {
        juce::ADSR::Parameters p;
        p.attack = nextEnvelopeParameters.attackSeconds;
        p.decay = nextEnvelopeParameters.decaySeconds;
        p.sustain = nextEnvelopeParameters.sustainLevel;
        p.release = nextEnvelopeParameters.releaseSeconds;
        return p;
    }
}
