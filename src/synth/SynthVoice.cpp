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
        filterEnvelope.setSampleRate(spec.sampleRate);
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

    void SynthVoice::setNextModulationParameters(const LfoParametersV2& lfo,
                                                 const PolyModParametersV2& polyMod,
                                                 const PerformanceParametersV2& performance) noexcept
    {
        nextLfoParameters = lfo;
        nextPolyModParameters = polyMod;
        nextPerformanceParameters = performance;
    }

    void SynthVoice::setGlobalLfoState(float phase, float modWheel) noexcept
    {
        blockStartGlobalLfoPhase = phase - std::floor(phase);
        currentModWheelValue = juce::jlimit(0.0f, 1.0f, modWheel);
    }

    void SynthVoice::setPan(float panLeft, float panRight) noexcept
    {
        panLeftVolume = juce::jlimit(0.0f, 1.5f, panLeft);
        panRightVolume = juce::jlimit(0.0f, 1.5f, panRight);
    }

    void SynthVoice::setVintageDriftCents(float cents) noexcept
    {
        vintageDriftCents = juce::jlimit(-50.0f, 50.0f, cents);
    }

    void SynthVoice::setGlideFromNote(int fromMidiNoteNumber, float glideTimeSeconds) noexcept
    {
        if (currentMidiNoteNumber < 0 || fromMidiNoteNumber < 0)
        {
            glideOffsetLog2 = 0.0f;
            glideStepLog2PerSample = 0.0f;
            return;
        }

        const float deltaSemitones = static_cast<float>(fromMidiNoteNumber - currentMidiNoteNumber);
        glideOffsetLog2 = deltaSemitones / 12.0f;

        if (glideTimeSeconds <= 0.0001f || currentSampleRate <= 0.0)
        {
            glideOffsetLog2 = 0.0f;
            glideStepLog2PerSample = 0.0f;
            return;
        }

        const float glideSamples = glideTimeSeconds * static_cast<float>(currentSampleRate);
        glideStepLog2PerSample = std::abs(glideOffsetLog2) / juce::jmax(1.0f, glideSamples);
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
                nextOscillatorAParameters.waveShape = coolsynth::parameters::OscillatorWaveShape::sine;
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
        glideOffsetLog2 = 0.0f;
        glideStepLog2PerSample = 0.0f;
        resetVoiceState();

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());
        ampEnvelope.noteOn();

        filterEnvelope.setParameters(makeJuceFilterEnvelopeParameters());
        filterEnvelope.noteOn();

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
            filterEnvelope.noteOff();
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
        filterEnvelope.setParameters(makeJuceFilterEnvelopeParameters());

        const auto baseBendRatio = std::pow(2.0f, currentPitchBendSemitones / 12.0f);
        const auto vintageRatio = std::pow(2.0f, vintageDriftCents / 1200.0f);

        float keyboardTrackingAmount = 0.0f;
        if (nextFilterParameters.keyTracking == coolsynth::parameters::FilterKeyTrackingMode::half)
            keyboardTrackingAmount = 0.5f;
        else if (nextFilterParameters.keyTracking == coolsynth::parameters::FilterKeyTrackingMode::full)
            keyboardTrackingAmount = 1.0f;

        const float keyTrackingRatio = std::pow(2.0f,
                                                (static_cast<float>(currentMidiNoteNumber) - 60.0f)
                                                    * keyboardTrackingAmount / 12.0f);

        const float sampleRateFloat = static_cast<float>(juce::jmax(1.0, currentSampleRate));
        const float lfoPhaseIncrement = nextLfoParameters.rateHz / sampleRateFloat;
        const float lfoModWheelDepth = (1.0f - nextLfoParameters.modWheelDepth)
                                       + (nextLfoParameters.modWheelDepth * currentModWheelValue);

        const auto nextQ = mapNormalizedResonanceToQ(nextFilterParameters.resonanceNormalized);
        resonanceQSmoother.setTargetValue(nextQ);

        const auto baseCutoffHz = clampCutoffToPreparedRange(nextFilterParameters.cutoffHz
                                                              * keyTrackingRatio);
        cutoffHzSmoother.setTargetValue(baseCutoffHz);

        const auto lfoOscShape = lfoShapeToOscShape(nextLfoParameters.waveShape);

        const float baseOscAFreqRaw = computeOscillatorFrequencyHz(nextOscillatorAParameters);
        const float baseOscBFreqRaw = computeOscillatorFrequencyHz(nextOscillatorBParameters);

        float lfoPhase = blockStartGlobalLfoPhase;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            if (glideStepLog2PerSample > 0.0f)
            {
                if (glideOffsetLog2 > 0.0f)
                    glideOffsetLog2 = juce::jmax(0.0f, glideOffsetLog2 - glideStepLog2PerSample);
                else
                    glideOffsetLog2 = juce::jmin(0.0f, glideOffsetLog2 + glideStepLog2PerSample);
            }

            const float glideRatio = std::pow(2.0f, glideOffsetLog2);

            const float envValue = ampEnvelope.getNextSample();
            const float filterEnvValue = filterEnvelope.getNextSample();

            const float lfoBaseSample = renderWaveSample(lfoPhase, lfoOscShape, 0.5f);
            const float lfoSample = lfoBaseSample * lfoModWheelDepth;

            lfoPhase += lfoPhaseIncrement;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            const float lfoPitchSemitones = lfoSample * nextLfoParameters.oscPitchDepth * 12.0f;
            const float lfoPitchRatio = std::pow(2.0f, lfoPitchSemitones / 12.0f);

            const float commonPitchRatio = baseBendRatio * vintageRatio * glideRatio;

            oscBState.frequencyHz = baseOscBFreqRaw * lfoPitchRatio;
            if (! nextOscillatorBParameters.lowFrequencyMode)
                oscBState.frequencyHz *= commonPitchRatio;

            oscBState.pulseWidth = juce::jlimit(0.05f, 0.95f, nextOscillatorBParameters.pulseWidth);
            oscBState.waveShape = nextOscillatorBParameters.waveShape;

            const auto oscBWrapped = oscBState.phase
                                     + juce::jlimit(0.0f, 0.5f, oscBState.frequencyHz / sampleRateFloat) >= 1.0f;
            const float oscBSample = renderOscillatorSample(oscBState, false);

            const float polyModOscB = oscBSample;
            const float polyModEnv = filterEnvValue;

            const float polyModOscAPitchSemis = (polyModOscB * nextPolyModParameters.oscBToOscPitch
                                                  + polyModEnv * nextPolyModParameters.envToOscPitch) * 24.0f;
            const float oscATotalPitchSemis = lfoPitchSemitones + polyModOscAPitchSemis;
            const float oscAPitchRatio = std::pow(2.0f, oscATotalPitchSemis / 12.0f);

            oscAState.frequencyHz = baseOscAFreqRaw * oscAPitchRatio * commonPitchRatio;

            const float polyModOscAPw = (polyModOscB * nextPolyModParameters.oscBToPulseWidth
                                          + polyModEnv * nextPolyModParameters.envToPulseWidth) * 0.5f;
            const float lfoOscAPw = lfoSample * nextLfoParameters.pulseWidthDepth * 0.5f;
            oscAState.pulseWidth = juce::jlimit(0.05f,
                                                0.95f,
                                                nextOscillatorAParameters.pulseWidth + polyModOscAPw + lfoOscAPw);
            oscAState.waveShape = nextOscillatorAParameters.waveShape;

            const float oscASample = renderOscillatorSample(oscAState,
                                                            nextOscillatorAParameters.syncEnabled && oscBWrapped);

            const auto oscALevel = juce::jlimit(0.0f, 1.0f, nextOscillatorAParameters.level);
            const auto oscBLevel = juce::jlimit(0.0f, 1.0f, nextOscillatorBParameters.level);
            const auto noiseLevel = juce::jlimit(0.0f, 1.0f, nextMixerParameters.noiseLevel);
            const auto noiseSample = nextNoiseSample();

            const auto mixed = oscASample * oscALevel
                               + oscBSample * oscBLevel
                               + noiseSample * noiseLevel;
            const auto totalLevel = oscALevel + oscBLevel + noiseLevel;
            const auto overloadDrive = 1.0f + juce::jmax(0.0f, totalLevel - 1.0f) * 1.75f;
            const auto normalized = mixed * (totalLevel > 0.0f ? 1.0f / juce::jmax(1.0f, totalLevel) : 0.0f);
            const float oscValue = std::tanh(normalized * overloadDrive);

            const float polyModCutoffSemis = (polyModOscB * nextPolyModParameters.oscBToFilterCutoff
                                               + polyModEnv * nextPolyModParameters.envToFilterCutoff) * 60.0f;
            const float lfoCutoffSemis = lfoSample * nextLfoParameters.filterCutoffDepth * 60.0f;
            const float envCutoffSemis = filterEnvValue * nextFilterParameters.envelopeAmount * 84.0f;
            const float velFilterSemis = nextPerformanceParameters.velocityToFilter * velocityGain * 24.0f;
            const float cutoffModRatio = std::pow(2.0f,
                                                  (envCutoffSemis + lfoCutoffSemis + polyModCutoffSemis
                                                   + velFilterSemis) / 12.0f);

            const float baseCutoffSmoothed = cutoffHzSmoother.getNextValue();
            lowPassFilter.setCutoffFrequency(clampCutoffToPreparedRange(baseCutoffSmoothed * cutoffModRatio));
            lowPassFilter.setResonance(resonanceQSmoother.getNextValue());

            const float filteredValue = lowPassFilter.processSample(0, oscValue);

            const float ampVelocityMult = 1.0f - nextPerformanceParameters.velocityToAmp
                                          + (velocityGain * nextPerformanceParameters.velocityToAmp);
            const float sampleValue = filteredValue * envValue * consumeNoteStartRamp()
                                      * ampVelocityMult * outputLevel;

            if (outputBuffer.getNumChannels() == 1)
            {
                outputBuffer.addSample(0, startSample + sample, sampleValue);
            }
            else
            {
                outputBuffer.addSample(0, startSample + sample, sampleValue * panLeftVolume);
                outputBuffer.addSample(1, startSample + sample, sampleValue * panRightVolume);
            }

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
        filterEnvelope.reset();
        resetVoiceState();
        currentMidiNoteNumber = -1;
        active = false;
        releasing = false;
        velocityGain = 0.0f;
        baseFrequencyHz = 0.0f;
        glideOffsetLog2 = 0.0f;
        glideStepLog2PerSample = 0.0f;
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
        const auto white = juce::jmap(static_cast<float>(randomBits & 0x00ffffffu),
                                      0.0f,
                                      static_cast<float>(0x00ffffffu),
                                      -1.0f,
                                      1.0f);

        pinkB0 = 0.99765f * pinkB0 + white * 0.0990460f;
        pinkB1 = 0.96300f * pinkB1 + white * 0.2965164f;
        pinkB2 = 0.57000f * pinkB2 + white * 1.0526913f;

        const auto pink = pinkB0 + pinkB1 + pinkB2 + white * 0.1848f;
        return juce::jlimit(-1.0f, 1.0f, pink * 0.2f);
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

            case coolsynth::parameters::OscillatorWaveShape::sine:
                return std::sin(phase * juce::MathConstants<float>::twoPi);

            case coolsynth::parameters::OscillatorWaveShape::saw:
            default:
                return juce::jmap(phase, 0.0f, 1.0f, -1.0f, 1.0f);
        }
    }

    coolsynth::parameters::OscillatorWaveShape SynthVoice::lfoShapeToOscShape(
        coolsynth::parameters::LfoWaveShape shape) noexcept
    {
        switch (shape)
        {
            case coolsynth::parameters::LfoWaveShape::saw:
                return coolsynth::parameters::OscillatorWaveShape::saw;
            case coolsynth::parameters::LfoWaveShape::square:
                return coolsynth::parameters::OscillatorWaveShape::pulse;
            case coolsynth::parameters::LfoWaveShape::triangle:
                return coolsynth::parameters::OscillatorWaveShape::triangle;
            case coolsynth::parameters::LfoWaveShape::sine:
            default:
                return coolsynth::parameters::OscillatorWaveShape::sine;
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
