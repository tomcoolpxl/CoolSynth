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

        lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    void SynthVoice::prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        oscillator.prepare(spec);
        lowPassFilter.prepare(spec);
        ampEnvelope.setSampleRate(spec.sampleRate);
        cutoffHzSmoother.reset(spec.sampleRate, 0.02);
    }

    void SynthVoice::setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept
    {
        nextEnvelopeParameters = parameters;
    }

    void SynthVoice::setNextFilterParameters(const FilterParameters& parameters) noexcept
    {
        nextFilterParameters = parameters;
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

        resetVoiceState();

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
            resetVoiceState();
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

        const auto clampedCutoffHz = clampCutoffToPreparedRange(nextFilterParameters.cutoffHz);
        cutoffHzSmoother.setTargetValue(clampedCutoffHz);

        const auto nextQ = mapNormalizedResonanceToQ(nextFilterParameters.resonanceNormalized);
        if (nextQ != lastAppliedResonanceQ)
        {
            lowPassFilter.setResonance(nextQ);
            lastAppliedResonanceQ = nextQ;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            lowPassFilter.setCutoffFrequency(cutoffHzSmoother.getNextValue());

            const float oscValue = oscillator.processSample(0.0f);
            const float filteredValue = lowPassFilter.processSample(0, oscValue);
            const float envValue = ampEnvelope.getNextSample();
            const float sampleValue = filteredValue * envValue * velocityGain;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            {
                outputBuffer.addSample(channel, startSample + sample, sampleValue);
            }

            if (!ampEnvelope.isActive())
            {
                resetVoiceState();
                clearCurrentNote();
                break;
            }
        }

        lowPassFilter.snapToZero();
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

    float SynthVoice::mapNormalizedResonanceToQ(float normalized) noexcept
    {
        const float qMin = 1.0f / std::sqrt(2.0f);
        const float qMax = 8.0f;
        const float r = juce::jlimit(0.0f, 1.0f, normalized);
        return qMin + (qMax - qMin) * (r * r);
    }

    float SynthVoice::clampCutoffToPreparedRange(float cutoffHz) const noexcept
    {
        const float minCutoffHz = 20.0f;
        const float maxCutoffHz = std::min(20000.0f, static_cast<float>(0.45 * currentSampleRate));
        return juce::jlimit(minCutoffHz, maxCutoffHz, cutoffHz);
    }

    void SynthVoice::primeFilterForCurrentTargets() noexcept
    {
        const auto clampedCutoffHz = clampCutoffToPreparedRange(nextFilterParameters.cutoffHz);
        cutoffHzSmoother.setCurrentAndTargetValue(clampedCutoffHz);

        const auto q = mapNormalizedResonanceToQ(nextFilterParameters.resonanceNormalized);
        lowPassFilter.setResonance(q);
        lastAppliedResonanceQ = q;
    }

    void SynthVoice::resetVoiceState() noexcept
    {
        lowPassFilter.reset();
        primeFilterForCurrentTargets();
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
