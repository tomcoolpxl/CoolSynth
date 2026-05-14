#include "SynthVoice.h"

#include <cmath>

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
        resonanceQSmoother.reset(spec.sampleRate, 0.02);
        forceStop();
    }

    void SynthVoice::setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept
    {
        nextEnvelopeParameters = parameters;
    }

    void SynthVoice::setNextFilterParameters(const FilterParameters& parameters) noexcept
    {
        nextFilterParameters = parameters;
    }

    void SynthVoice::setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept
    {
        currentWaveform = waveform;
    }

    void SynthVoice::setOutputLevel(float level) noexcept
    {
        outputLevel = juce::jlimit(0.0f, 1.0f, level);
    }

    void SynthVoice::setPitchBendSemitones(float semitones) noexcept
    {
        currentPitchBendSemitones = semitones;
    }

    void SynthVoice::startNote(int midiNoteNumber,
                               float velocity,
                               juce::SynthesiserSound*,
                               int)
    {
        currentMidiNoteNumber = midiNoteNumber;
        baseFrequencyHz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        oscillator.reset();

        resetVoiceState();

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());
        ampEnvelope.noteOn();

        velocityGain = juce::jlimit(0.0f, 1.0f, velocity);
        active = true;
        releasing = false;
    }

    void SynthVoice::stopNote(float, bool allowTailOff)
    {
        if (! active)
            return;

        if (allowTailOff)
        {
            ampEnvelope.noteOff();
            releasing = true;
            return;
        }

        forceStop();
    }

    void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     int startSample,
                                     int numSamples)
    {
        if (! active || numSamples <= 0)
            return;

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());

        const auto clampedCutoffHz = clampCutoffToPreparedRange(nextFilterParameters.cutoffHz);
        cutoffHzSmoother.setTargetValue(clampedCutoffHz);

        const auto nextQ = mapNormalizedResonanceToQ(nextFilterParameters.resonanceNormalized);
        resonanceQSmoother.setTargetValue(nextQ);

        const auto pitchRatio = std::pow(2.0f, currentPitchBendSemitones / 12.0f);
        oscillator.setFrequency(baseFrequencyHz * pitchRatio, false);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            lowPassFilter.setCutoffFrequency(cutoffHzSmoother.getNextValue());
            lowPassFilter.setResonance(resonanceQSmoother.getNextValue());

            const float oscValue = oscillator.processSample(0.0f);
            const float filteredValue = lowPassFilter.processSample(0, oscValue);
            const float envValue = ampEnvelope.getNextSample();
            const float sampleValue = filteredValue * envValue * velocityGain * outputLevel;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample(channel, startSample + sample, sampleValue);

            if (! ampEnvelope.isActive())
            {
                forceStop();
                break;
            }
        }

        lowPassFilter.snapToZero();
    }

    void SynthVoice::forceStop() noexcept
    {
        ampEnvelope.reset();
        resetVoiceState();
        currentMidiNoteNumber = -1;
        active = false;
        releasing = false;
        velocityGain = 0.0f;
        baseFrequencyHz = 0.0f;
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
            default:
                return juce::jmap(phase,
                                  -juce::MathConstants<float>::pi,
                                  juce::MathConstants<float>::pi,
                                  -1.0f,
                                  1.0f);
        }
    }

    float SynthVoice::mapNormalizedResonanceToQ(float normalized) noexcept
    {
        const float qMin = 1.0f / std::sqrt(2.0f);
        const float qMax = 25.0f;
        const float r = juce::jlimit(0.0f, 1.0f, normalized);
        return qMin + (qMax - qMin) * (r * r * r);
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
        resonanceQSmoother.setCurrentAndTargetValue(q);
        lowPassFilter.setResonance(q);
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
