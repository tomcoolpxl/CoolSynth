#include "SynthVoice.h"

#include <cmath>

namespace coolsynth::synth
{
    SynthVoice::SynthVoice()
    {
        lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    void SynthVoice::prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        lowPassFilter.prepare(spec);
        ampEnvelope.setSampleRate(spec.sampleRate);
        cutoffHzSmoother.reset(spec.sampleRate, 0.02);
        resonanceQSmoother.reset(spec.sampleRate, 0.02);
        noteStartRampMinSamples = juce::jlimit(8, 64, static_cast<int>(std::round(spec.sampleRate * 0.00035)));
        forceStop();
    }

    void SynthVoice::setRandomSeed(uint32_t seed) noexcept
    {
        baseRandomSeed = seed == 0 ? 0x12345678u : seed;
        randomState = baseRandomSeed;
    }

    void SynthVoice::setNextVoiceSourceParameters(const OscillatorParametersV2& oscA,
                                                  const OscillatorParametersV2& oscB,
                                                  const MixerParametersV2& mixer) noexcept
    {
        nextOscillatorAParameters = oscA;
        nextOscillatorBParameters = oscB;
        nextMixerParameters = mixer;
    }

    void SynthVoice::setNextEnvelopeParameters(const EnvelopeParameters& parameters) noexcept
    {
        nextEnvelopeParameters = parameters;
    }

    void SynthVoice::setNextFilterEnvelopeParameters(const EnvelopeParameters& parameters) noexcept
    {
        nextFilterEnvelopeParameters = parameters;
    }

    void SynthVoice::setNextFilterParameters(const FilterParametersV2& parameters) noexcept
    {
        nextFilterParameters = parameters;
    }

    void SynthVoice::setWaveform(coolsynth::parameters::WaveformChoice waveform) noexcept
    {
        switch (waveform)
        {
            case coolsynth::parameters::WaveformChoice::sine:
                nextOscillatorAParameters.waveShape = coolsynth::parameters::OscillatorWaveShape::triangle;
                break;

            case coolsynth::parameters::WaveformChoice::square:
                nextOscillatorAParameters.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
                break;

            case coolsynth::parameters::WaveformChoice::saw:
            default:
                nextOscillatorAParameters.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
                break;
        }

        nextOscillatorAParameters.level = 1.0f;
        nextOscillatorBParameters.level = 0.0f;
        nextMixerParameters.noiseLevel = 0.0f;
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
        resetVoiceState();

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());
        ampEnvelope.noteOn();

        velocityGain = juce::jlimit(0.0f, 1.0f, velocity);
        noteStartRampTotalSamples = computeNoteStartRampSamples();
        noteStartRampSamplesRemaining = noteStartRampTotalSamples;
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

        const auto bendRatio = std::pow(2.0f, currentPitchBendSemitones / 12.0f);
        oscAState.frequencyHz = computeOscillatorFrequencyHz(nextOscillatorAParameters) * bendRatio;
        oscAState.pulseWidth = juce::jlimit(0.05f, 0.95f, nextOscillatorAParameters.pulseWidth);
        oscAState.waveShape = nextOscillatorAParameters.waveShape;

        oscBState.frequencyHz = computeOscillatorFrequencyHz(nextOscillatorBParameters);
        if (! nextOscillatorBParameters.lowFrequencyMode)
            oscBState.frequencyHz *= bendRatio;

        oscBState.pulseWidth = juce::jlimit(0.05f, 0.95f, nextOscillatorBParameters.pulseWidth);
        oscBState.waveShape = nextOscillatorBParameters.waveShape;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            lowPassFilter.setCutoffFrequency(cutoffHzSmoother.getNextValue());
            lowPassFilter.setResonance(resonanceQSmoother.getNextValue());

            const float oscValue = renderMixedVoiceSample();
            const float filteredValue = lowPassFilter.processSample(0, oscValue);
            const float envValue = ampEnvelope.getNextSample();
            const float sampleValue = filteredValue * envValue * consumeNoteStartRamp() * velocityGain * outputLevel;

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

    float SynthVoice::computeOscillatorFrequencyHz(const OscillatorParametersV2& parameters) const noexcept
    {
        const auto octaveOffsetSemitones = static_cast<float>((parameters.octaveIndex - 2) * 12);
        const auto fineTuneSemitones = parameters.fineCents / 100.0f;

        if (parameters.lowFrequencyMode)
        {
            const auto lowFrequencyRatio = std::pow(2.0f, octaveOffsetSemitones / 12.0f);
            return juce::jlimit(0.1f,
                                20.0f,
                                1.0f * lowFrequencyRatio * std::pow(2.0f, fineTuneSemitones / 12.0f));
        }

        return baseFrequencyHz * std::pow(2.0f, (octaveOffsetSemitones + fineTuneSemitones) / 12.0f);
    }

    int SynthVoice::computeNoteStartRampSamples() const noexcept
    {
        const auto oscALevel = juce::jlimit(0.0f, 1.0f, nextOscillatorAParameters.level);
        const auto oscBLevel = juce::jlimit(0.0f, 1.0f, nextOscillatorBParameters.level);
        const auto noiseLevel = juce::jlimit(0.0f, 1.0f, nextMixerParameters.noiseLevel);
        const auto totalLevel = oscALevel + oscBLevel + noiseLevel;

        float extraMilliseconds = 0.0f;
        if (nextOscillatorAParameters.waveShape == coolsynth::parameters::OscillatorWaveShape::pulse)
            extraMilliseconds += 0.8f * oscALevel;

        if (nextOscillatorBParameters.waveShape == coolsynth::parameters::OscillatorWaveShape::pulse)
            extraMilliseconds += 0.8f * oscBLevel;

        if (nextOscillatorAParameters.syncEnabled)
            extraMilliseconds += 0.5f * juce::jmax(oscALevel, oscBLevel);

        extraMilliseconds += 1.2f * noiseLevel;
        extraMilliseconds += 0.4f * juce::jmax(0.0f, totalLevel - 1.0f);

        const auto extraSamples = static_cast<int>(std::round((extraMilliseconds * 0.001f)
                                                              * static_cast<float>(currentSampleRate)));
        return juce::jlimit(noteStartRampMinSamples, 192, noteStartRampMinSamples + extraSamples);
    }

    float SynthVoice::renderOscillatorSample(OscillatorState& oscillator,
                                             bool forcePhaseReset) noexcept
    {
        if (forcePhaseReset)
            oscillator.phase = 0.0f;

        const auto sample = renderWaveSample(oscillator.phase, oscillator.waveShape, oscillator.pulseWidth);
        if (currentSampleRate <= 0.0)
            return sample;

        const auto phaseIncrement = juce::jlimit(0.0f,
                                                 0.5f,
                                                 oscillator.frequencyHz / static_cast<float>(currentSampleRate));
        oscillator.phase += phaseIncrement;
        oscillator.phase -= std::floor(oscillator.phase);
        return sample;
    }

    float SynthVoice::renderMixedVoiceSample() noexcept
    {
        const auto oscALevel = juce::jlimit(0.0f, 1.0f, nextOscillatorAParameters.level);
        const auto oscBLevel = juce::jlimit(0.0f, 1.0f, nextOscillatorBParameters.level);
        const auto noiseLevel = juce::jlimit(0.0f, 1.0f, nextMixerParameters.noiseLevel);

        const auto oscBWrapped = oscBState.phase + juce::jlimit(0.0f,
                                                                0.5f,
                                                                oscBState.frequencyHz / static_cast<float>(currentSampleRate)) >= 1.0f;
        const auto oscBSample = renderOscillatorSample(oscBState, false);
        const auto oscASample = renderOscillatorSample(oscAState, nextOscillatorAParameters.syncEnabled && oscBWrapped);
        const auto noiseSample = nextNoiseSample();

        const auto mixed = oscASample * oscALevel
                         + oscBSample * oscBLevel
                         + noiseSample * noiseLevel;

        const auto totalLevel = oscALevel + oscBLevel + noiseLevel;
        const auto overloadDrive = 1.0f + juce::jmax(0.0f, totalLevel - 1.0f) * 1.75f;
        const auto normalized = mixed * (totalLevel > 0.0f ? 1.0f / juce::jmax(1.0f, totalLevel) : 0.0f);
        return std::tanh(normalized * overloadDrive);
    }

    float SynthVoice::consumeNoteStartRamp() noexcept
    {
        if (noteStartRampSamplesRemaining <= 0)
            return 1.0f;

        const auto totalSamples = juce::jmax(noteStartRampMinSamples, noteStartRampTotalSamples);
        const auto linear = 1.0f - static_cast<float>(noteStartRampSamplesRemaining)
                                  / static_cast<float>(totalSamples);
        --noteStartRampSamplesRemaining;
        const auto clamped = juce::jlimit(0.0f, 1.0f, linear);
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    float SynthVoice::nextNoiseSample() noexcept
    {
        const auto randomBits = advanceRandomState();
        return juce::jmap(static_cast<float>(randomBits & 0x00ffffffu),
                          0.0f,
                          static_cast<float>(0x00ffffffu),
                          -1.0f,
                          1.0f);
    }

    uint32_t SynthVoice::advanceRandomState() noexcept
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return randomState;
    }

    float SynthVoice::renderWaveSample(float phase,
                                       coolsynth::parameters::OscillatorWaveShape waveShape,
                                       float pulseWidth) noexcept
    {
        switch (waveShape)
        {
            case coolsynth::parameters::OscillatorWaveShape::pulse:
            {
                const auto clampedPulseWidth = juce::jlimit(0.05f, 0.95f, pulseWidth);
                return phase < clampedPulseWidth ? (1.0f - clampedPulseWidth) : -clampedPulseWidth;
            }

            case coolsynth::parameters::OscillatorWaveShape::triangle:
                return phase < 0.5f
                    ? juce::jmap(phase, 0.0f, 0.5f, -1.0f, 1.0f)
                    : juce::jmap(phase, 0.5f, 1.0f, 1.0f, -1.0f);

            case coolsynth::parameters::OscillatorWaveShape::saw:
            default:
                return juce::jmap(phase, 0.0f, 1.0f, -1.0f, 1.0f);
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
        oscAState.phase = juce::jmap(static_cast<float>(advanceRandomState() & 0xffffu),
                                     0.0f,
                                     65535.0f,
                                     0.0f,
                                     1.0f);
        oscBState.phase = juce::jmap(static_cast<float>(advanceRandomState() & 0xffffu),
                                     0.0f,
                                     65535.0f,
                                     0.0f,
                                     1.0f);
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

    juce::ADSR::Parameters SynthVoice::makeJuceFilterEnvelopeParameters() const noexcept
    {
        juce::ADSR::Parameters p;
        p.attack = nextFilterEnvelopeParameters.attackSeconds;
        p.decay = nextFilterEnvelopeParameters.decaySeconds;
        p.sustain = nextFilterEnvelopeParameters.sustainLevel;
        p.release = nextFilterEnvelopeParameters.releaseSeconds;
        return p;
    }
}
