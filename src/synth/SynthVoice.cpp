#include "SynthVoice.h"

#include <cmath>

namespace
{
    constexpr float kPreFilterOverloadDriveSlope   = 1.75f;  // soft-clip slope when total osc level > 1.0
    constexpr float kPinkNoiseKelletNormalisation  = 0.2f;   // Kellet filter normalisation to ~0 dBFS pink
    constexpr float kPolyModCutoffRangeSemitones   = 60.0f;  // poly-mod and LFO → filter cutoff: ±5 octaves
    constexpr float kFilterEnvCutoffRangeSemitones = 84.0f;  // filter envelope → cutoff: ±7 octaves
    constexpr float kPolyModOscPitchRangeSemitones = 24.0f;  // poly-mod osc-B → pitch: ±2 octaves
    constexpr float kLfoPitchRangeSemitones        = 12.0f;  // LFO → pitch: ±1 octave
    constexpr float kVelocityFilterRangeSemitones  = 24.0f;  // velocity → filter: ±2 octaves
    constexpr float kFilterQMin                    = 0.7071067811865476f; // 1/√2, Butterworth minimum Q
    constexpr float kFilterQMax                    = 25.0f;  // maximum resonance Q

    // Standard 2-point PolyBLEP correction. t in [0,1), dt = phase increment per sample.
    static float polyBlep(float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    // Bandlimited wave renderer using PolyBLEP for saw and pulse discontinuities
    // and a leaky integrator for triangle. triState persists between samples.
    static float renderBandlimitedWaveSample(
        float phase, float dt,
        coolsynth::parameters::OscillatorWaveShape shape,
        float pulseWidth, float& triState) noexcept
    {
        using WS = coolsynth::parameters::OscillatorWaveShape;
        switch (shape)
        {
            case WS::saw:
            {
                float y = 2.0f * phase - 1.0f;
                y -= polyBlep(phase, dt);
                return y;
            }
            case WS::pulse:
            {
                const float pw = juce::jlimit(0.05f, 0.95f, pulseWidth);
                float y = phase < pw ? (1.0f - pw) : -pw;
                y += polyBlep(phase, dt);
                y -= polyBlep(std::fmod(phase - pw + 1.0f, 1.0f), dt);
                return y;
            }
            case WS::triangle:
            {
                // Leaky integrator of bandlimited square; leak factor prevents DC drift.
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(phase, dt);
                sq -= polyBlep(std::fmod(phase + 0.5f, 1.0f), dt);
                triState = 4.0f * dt * sq + (1.0f - 4.0f * dt) * triState;
                return triState * 0.8f;
            }
            case WS::sine:
            default:
                return std::sin(phase * juce::MathConstants<float>::twoPi);
        }
    }
} // namespace

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
        samplesSinceStart = 0;
        active = true;
        releasing = false;
        pendingStop = false;
    }

    void SynthVoice::stopNote(float, bool allowTailOff)
    {
        if (! active)
            return;

        if (allowTailOff)
        {
            const int minAudibleSamples = static_cast<int>(std::round(currentSampleRate * 0.005));
            if (samplesSinceStart < minAudibleSamples)
            {
                pendingStop = true;
                return;
            }

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

        if (pendingStop)
        {
            const int minAudibleSamples = static_cast<int>(std::round(currentSampleRate * 0.005));
            if (samplesSinceStart >= minAudibleSamples)
            {
                ampEnvelope.noteOff();
                filterEnvelope.noteOff();
                releasing = true;
                pendingStop = false;
            }
        }

        ampEnvelope.setParameters(makeJuceEnvelopeParameters());
        filterEnvelope.setParameters(makeJuceFilterEnvelopeParameters());

        // C2: exp2 is faster than pow(2, x) on all major platforms
        const float voicePitchCarrierRatio = std::exp2(currentPitchBendSemitones / 12.0f)
                                            * std::exp2(vintageDriftCents / 1200.0f);

        // C1: initialize glide ratio as running multiplicative state (avoids per-sample pow)
        float currentGlideRatio = std::exp2(glideOffsetLog2);
        const float glideStepRatioPerSample = std::exp2(glideStepLog2PerSample);
        const float glideStepRatioInverse = (glideStepRatioPerSample > 0.0f)
                                           ? 1.0f / glideStepRatioPerSample : 1.0f;

        float keyboardTrackingAmount = 0.0f;
        if (nextFilterParameters.keyTracking == coolsynth::parameters::FilterKeyTrackingMode::half)
            keyboardTrackingAmount = 0.5f;
        else if (nextFilterParameters.keyTracking == coolsynth::parameters::FilterKeyTrackingMode::full)
            keyboardTrackingAmount = 1.0f;

        const float keyTrackingRatio = std::exp2((static_cast<float>(currentMidiNoteNumber) - 60.0f)
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

        // C3: sub-rate LFO — evaluate wave every lfoSubRateSamples, linearly interpolate between
        float lfoValue = renderNaiveWaveSample(lfoPhase, lfoOscShape, 0.5f) * lfoModWheelDepth;
        float lfoInterpStep = 0.0f;
        int   lfoSubRateCounter = lfoSubRateSamples;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // C1: multiplicative glide update — avoids per-sample pow(2, glideOffsetLog2)
            if (glideStepLog2PerSample > 0.0f)
            {
                if (glideOffsetLog2 > 0.0f)
                {
                    glideOffsetLog2 = juce::jmax(0.0f, glideOffsetLog2 - glideStepLog2PerSample);
                    currentGlideRatio = (glideOffsetLog2 > 0.0f) ? currentGlideRatio * glideStepRatioInverse : 1.0f;
                }
                else if (glideOffsetLog2 < 0.0f)
                {
                    glideOffsetLog2 = juce::jmin(0.0f, glideOffsetLog2 + glideStepLog2PerSample);
                    currentGlideRatio = (glideOffsetLog2 < 0.0f) ? currentGlideRatio * glideStepRatioPerSample : 1.0f;
                }
            }

            if (pendingStop)
            {
                const int minAudibleSamples = static_cast<int>(std::round(currentSampleRate * 0.005));
                if (samplesSinceStart >= minAudibleSamples)
                {
                    ampEnvelope.noteOff();
                    filterEnvelope.noteOff();
                    releasing = true;
                    pendingStop = false;
                }
            }

            const float envValue = ampEnvelope.getNextSample();
            const float filterEnvValue = filterEnvelope.getNextSample();

            // C3: use interpolated LFO value; advance phase and recompute only every sub-rate period
            const float lfoSample = lfoValue;
            lfoValue += lfoInterpStep;
            if (--lfoSubRateCounter == 0)
            {
                lfoPhase += lfoPhaseIncrement * static_cast<float>(lfoSubRateSamples);
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;
                const float nextLfoValue = renderNaiveWaveSample(lfoPhase, lfoOscShape, 0.5f) * lfoModWheelDepth;
                lfoInterpStep     = (nextLfoValue - lfoValue) / static_cast<float>(lfoSubRateSamples);
                lfoSubRateCounter = lfoSubRateSamples;
            }

            const float lfoPitchSemitones = lfoSample * nextLfoParameters.oscPitchDepth * kLfoPitchRangeSemitones;
            // C2: exp2 instead of pow; C1: one exp2 per oscillator (osc A folds in polyMod below)
            const float oscBPitchRatio = std::exp2(lfoPitchSemitones / 12.0f);

            const float totalPitchCarrierRatio = voicePitchCarrierRatio * currentGlideRatio;

            oscBState.frequencyHz = baseOscBFreqRaw * oscBPitchRatio;
            if (! nextOscillatorBParameters.lowFrequencyMode)
                oscBState.frequencyHz *= totalPitchCarrierRatio;

            oscBState.phaseIncrement = juce::jlimit(0.0f, 0.5f, oscBState.frequencyHz / sampleRateFloat);
            oscBState.pulseWidth = juce::jlimit(0.05f, 0.95f, nextOscillatorBParameters.pulseWidth);
            oscBState.waveShape = nextOscillatorBParameters.waveShape;

            const bool oscBWrapped = oscBState.phase + oscBState.phaseIncrement >= 1.0f;
            const float bWrapFrac = (oscBWrapped && oscBState.phaseIncrement > 0.0f)
                ? (oscBState.phase + oscBState.phaseIncrement - 1.0f) / oscBState.phaseIncrement
                : 0.0f;
            const float oscBSample = renderOscillatorSample(oscBState, false);

            const float polyModOscB = oscBSample;
            const float polyModEnv = filterEnvValue;

            const float polyModOscAPitchSemis = (polyModOscB * nextPolyModParameters.oscBToOscPitch
                                                  + polyModEnv * nextPolyModParameters.envToOscPitch) * kPolyModOscPitchRangeSemitones;
            // C1: fold LFO + polyMod semitones together before the single exp2
            const float oscAPitchRatio = std::exp2((lfoPitchSemitones + polyModOscAPitchSemis) / 12.0f);

            oscAState.frequencyHz = baseOscAFreqRaw * oscAPitchRatio * totalPitchCarrierRatio;
            oscAState.phaseIncrement = juce::jlimit(0.0f, 0.5f, oscAState.frequencyHz / sampleRateFloat);

            const float polyModOscAPw = (polyModOscB * nextPolyModParameters.oscBToPulseWidth
                                          + polyModEnv * nextPolyModParameters.envToPulseWidth) * 0.5f;
            const float lfoOscAPw = lfoSample * nextLfoParameters.pulseWidthDepth * 0.5f;
            oscAState.pulseWidth = juce::jlimit(0.05f,
                                                0.95f,
                                                nextOscillatorAParameters.pulseWidth + polyModOscAPw + lfoOscAPw);
            oscAState.waveShape = nextOscillatorAParameters.waveShape;

            const float phaseBeforeAReset = oscAState.phase;
            float oscASample = renderOscillatorSample(oscAState,
                                                      nextOscillatorAParameters.syncEnabled && oscBWrapped);

            // B2: smooth the sync discontinuity — PolyBLEP correction for the hard-reset step.
            if (nextOscillatorAParameters.syncEnabled && oscBWrapped && oscAState.phaseIncrement > 0.0f)
            {
                const float before = renderNaiveWaveSample(phaseBeforeAReset,
                                                           oscAState.waveShape, oscAState.pulseWidth);
                const float after  = renderNaiveWaveSample(0.0f,
                                                           oscAState.waveShape, oscAState.pulseWidth);
                oscASample += polyBlep(bWrapFrac, oscAState.phaseIncrement) * (after - before);
            }

            const auto oscALevel = juce::jlimit(0.0f, 1.0f, nextOscillatorAParameters.level);
            const auto oscBLevel = juce::jlimit(0.0f, 1.0f, nextOscillatorBParameters.level);
            const auto noiseLevel = juce::jlimit(0.0f, 1.0f, nextMixerParameters.noiseLevel);
            const auto noiseSample = nextNoiseSample();

            const auto mixed = oscASample * oscALevel
                               + oscBSample * oscBLevel
                               + noiseSample * noiseLevel;
            const auto totalLevel = oscALevel + oscBLevel + noiseLevel;
            const auto overloadDrive = 1.0f + juce::jmax(0.0f, totalLevel - 1.0f) * kPreFilterOverloadDriveSlope;
            const auto normalized = mixed * (totalLevel > 0.0f ? 1.0f / juce::jmax(1.0f, totalLevel) : 0.0f);
            const float oscValue = std::tanh(normalized * overloadDrive);

            const float polyModCutoffSemis = (polyModOscB * nextPolyModParameters.oscBToFilterCutoff
                                               + polyModEnv * nextPolyModParameters.envToFilterCutoff) * kPolyModCutoffRangeSemitones;
            const float lfoCutoffSemis = lfoSample * nextLfoParameters.filterCutoffDepth * kPolyModCutoffRangeSemitones;
            const float envCutoffSemis = filterEnvValue * nextFilterParameters.envelopeAmount * kFilterEnvCutoffRangeSemitones;
            const float velFilterSemis = nextPerformanceParameters.velocityToFilter * velocityGain * kVelocityFilterRangeSemitones;
            const float cutoffModSumSemis = juce::jlimit(-120.0f, 120.0f,
                envCutoffSemis + lfoCutoffSemis + polyModCutoffSemis + velFilterSemis);
            const float cutoffModRatio = std::exp2(cutoffModSumSemis / 12.0f);

            const float baseCutoffSmoothed = cutoffHzSmoother.getNextValue();
            lowPassFilter.setCutoffFrequency(clampCutoffToPreparedRange(baseCutoffSmoothed * cutoffModRatio));
            const float currentQ = resonanceQSmoother.getNextValue();
            lowPassFilter.setResonance(currentQ);
            const float filterInputGain = juce::jmin(1.0f, 1.0f / std::sqrt(currentQ));
            const float filteredValue = lowPassFilter.processSample(0, oscValue * filterInputGain);

            const float ampVelocityMult = 1.0f - nextPerformanceParameters.velocityToAmp
                                          + (velocityGain * nextPerformanceParameters.velocityToAmp);
            const float sampleValue = filteredValue * envValue * consumeNoteStartRamp()
                                      * ampVelocityMult * outputLevel;

            samplesSinceStart++;

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
        pendingStop = false;
        samplesSinceStart = 0;
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
            return juce::jlimit(0.1f,
                                20.0f,
                                std::exp2((octaveOffsetSemitones + fineTuneSemitones) / 12.0f));

        return baseFrequencyHz * std::exp2((octaveOffsetSemitones + fineTuneSemitones) / 12.0f);
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

        const float sample = renderBandlimitedWaveSample(
            oscillator.phase, oscillator.phaseIncrement,
            oscillator.waveShape, oscillator.pulseWidth, oscillator.triState);

        oscillator.phase += oscillator.phaseIncrement;
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
        return juce::jlimit(-1.0f, 1.0f, pink * kPinkNoiseKelletNormalisation);
    }

    uint32_t SynthVoice::advanceRandomState() noexcept
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return randomState;
    }

    float SynthVoice::renderNaiveWaveSample(float phase,
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
        const float r = juce::jlimit(0.0f, 1.0f, normalized);
        return kFilterQMin + (kFilterQMax - kFilterQMin) * (r * r * r);
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
        oscAState.triState = 0.0f;
        oscBState.triState = 0.0f;
        oscAState.phaseIncrement = 0.0f;
        oscBState.phaseIncrement = 0.0f;
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
