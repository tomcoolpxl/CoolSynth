#include "SynthVoice.h"
#include "SynthSound.h"

namespace coolsynth::synth
{
    SynthVoice::SynthVoice()
    {
        oscillator.initialise([](float x) { return std::sin(x); });
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
