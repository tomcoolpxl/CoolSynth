#include "SynthEngineV2.h"

#include <limits>

namespace coolsynth::synth
{
    SynthEngineV2::SynthEngineV2(int voiceCount)
        : voices(static_cast<size_t>(juce::jmax(1, voiceCount)))
    {
    }

    void SynthEngineV2::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        currentSampleRate = sampleRate;
        outputChannels = outputChannelCount;
        masterGainLinear.reset(sampleRate, masterGainRampSeconds);
        prepareVoices(sampleRate, samplesPerBlock);
        globalDelay.prepare(sampleRate, samplesPerBlock, outputChannelCount);
        panic();
        prepared = true;
    }

    void SynthEngineV2::releaseResources() noexcept
    {
        panic();
        globalDelay.reset();
        prepared = false;
    }

    void SynthEngineV2::render(juce::AudioBuffer<float>& outputBuffer,
                               std::span<const EngineMidiEvent> midiEvents,
                               const BlockRenderParametersV2& parameters)
    {
        if (! prepared)
            return;

        applyVoiceParameters(parameters);

        int renderedSamples = 0;
        const int blockSamples = outputBuffer.getNumSamples();

        for (const auto& event : midiEvents)
        {
            const int eventOffset = juce::jlimit(0, blockSamples, event.sampleOffset);
            if (eventOffset > renderedSamples)
            {
                renderVoiceSpan(outputBuffer, renderedSamples, eventOffset - renderedSamples);
                renderedSamples = eventOffset;
            }

            handleEvent(event, parameters);
        }

        if (renderedSamples < blockSamples)
            renderVoiceSpan(outputBuffer, renderedSamples, blockSamples - renderedSamples);

        globalDelay.process(outputBuffer, mapDelayParameters(parameters.delay));
        applyMasterGain(outputBuffer, parameters.masterGainLinear);
    }

    void SynthEngineV2::panic() noexcept
    {
        for (auto& slot : voices)
        {
            slot.voice.forceStop();
            clearVoiceSlot(slot);
        }

        globalDelay.clear();
        nextVoiceStartOrder = 0;
        pitchBendSemitones = 0.0f;
        modWheelValue = 0.0f;
        sustainPedalDown = false;
    }

    int SynthEngineV2::getActiveVoiceCountForTesting() const noexcept
    {
        int activeCount = 0;

        for (const auto& slot : voices)
            if (slot.voice.isActive())
                ++activeCount;

        return activeCount;
    }

    void SynthEngineV2::copyVoiceStatesForTesting(std::span<VoiceDebugState> destination) const noexcept
    {
        const auto count = juce::jmin(static_cast<int>(voices.size()), static_cast<int>(destination.size()));

        for (int i = 0; i < count; ++i)
        {
            const auto& source = voices[static_cast<size_t>(i)];
            auto& target = destination[static_cast<size_t>(i)];
            target.active = source.voice.isActive();
            target.keyDown = source.keyDown;
            target.sustained = source.sustained;
            target.releasing = source.voice.isReleasing();
            target.noteNumber = source.noteNumber;
            target.startOrder = source.startOrder;
        }
    }

    void SynthEngineV2::prepareVoices(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1;

        for (int i = 0; i < static_cast<int>(voices.size()); ++i)
        {
            auto& slot = voices[static_cast<size_t>(i)];
            slot.voice.prepare(spec);
            slot.voice.setRandomSeed(0x9e3779b9u ^ static_cast<uint32_t>((i + 1) * 0x85ebca6bu));
        }
    }

    void SynthEngineV2::applyVoiceParameters(const BlockRenderParametersV2& parameters) noexcept
    {
        for (auto& slot : voices)
        {
            slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
            slot.voice.setOutputLevel(1.0f);
            slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
            slot.voice.setNextFilterParameters({ parameters.filter.cutoffHz, parameters.filter.resonanceNormalized });
        }
    }

    void SynthEngineV2::renderVoiceSpan(juce::AudioBuffer<float>& outputBuffer,
                                        int startSample,
                                        int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        for (auto& slot : voices)
        {
            if (! slot.voice.isActive())
            {
                clearVoiceSlot(slot);
                continue;
            }

            slot.voice.setPitchBendSemitones(pitchBendSemitones);
            slot.voice.renderNextBlock(outputBuffer, startSample, numSamples);

            if (! slot.voice.isActive())
                clearVoiceSlot(slot);
        }
    }

    void SynthEngineV2::handleEvent(const EngineMidiEvent& event,
                                    const BlockRenderParametersV2& parameters) noexcept
    {
        switch (event.type)
        {
            case EngineMidiEventType::noteOn:        handleNoteOn(event, parameters); break;
            case EngineMidiEventType::noteOff:       handleNoteOff(event); break;
            case EngineMidiEventType::pitchBend:     handlePitchBend(event, parameters); break;
            case EngineMidiEventType::modWheel:      handleModWheel(event); break;
            case EngineMidiEventType::sustainPedal:  handleSustainPedal(event); break;
            case EngineMidiEventType::allNotesOff:   handleAllNotesOff(); break;
            case EngineMidiEventType::allSoundOff:   handleAllSoundOff(); break;
            case EngineMidiEventType::resetControllers: handleResetControllers(); break;
        }
    }

    void SynthEngineV2::handleNoteOn(const EngineMidiEvent& event,
                                     const BlockRenderParametersV2& parameters) noexcept
    {
        const int voiceIndex = findVoiceIndexToAllocate();
        auto& slot = voices[static_cast<size_t>(voiceIndex)];

        if (slot.voice.isActive())
            slot.voice.forceStop();

        slot.noteNumber = static_cast<int>(event.noteNumber);
        slot.keyDown = true;
        slot.sustained = false;
        slot.startOrder = ++nextVoiceStartOrder;
        slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
        slot.voice.setOutputLevel(1.0f);
        slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
        slot.voice.setNextFilterParameters({ parameters.filter.cutoffHz, parameters.filter.resonanceNormalized });
        slot.voice.startNote(slot.noteNumber, juce::jlimit(0.0f, 1.0f, event.value));
        slot.voice.setPitchBendSemitones(pitchBendSemitones);
    }

    void SynthEngineV2::handleNoteOff(const EngineMidiEvent& event) noexcept
    {
        const int voiceIndex = findVoiceIndexForNoteOff(static_cast<int>(event.noteNumber));
        if (voiceIndex < 0)
            return;

        auto& slot = voices[static_cast<size_t>(voiceIndex)];
        slot.keyDown = false;

        if (sustainPedalDown)
        {
            slot.sustained = true;
            return;
        }

        slot.voice.stopNote(0.0f, true);
    }

    void SynthEngineV2::handlePitchBend(const EngineMidiEvent& event,
                                        const BlockRenderParametersV2& parameters) noexcept
    {
        const auto normalized = juce::jlimit(-1.0f, 1.0f, event.value);
        pitchBendSemitones = normalized * juce::jlimit(1.0f, 24.0f, parameters.performance.pitchBendRangeSemitones);
    }

    void SynthEngineV2::handleModWheel(const EngineMidiEvent& event) noexcept
    {
        modWheelValue = juce::jlimit(0.0f, 1.0f, event.value);
        juce::ignoreUnused(modWheelValue);
    }

    void SynthEngineV2::handleSustainPedal(const EngineMidiEvent& event) noexcept
    {
        const bool newSustainState = event.value >= 0.5f;
        if (sustainPedalDown == newSustainState)
            return;

        sustainPedalDown = newSustainState;
        if (! sustainPedalDown)
            releaseSustainedVoices();
    }

    void SynthEngineV2::handleAllNotesOff() noexcept
    {
        for (auto& slot : voices)
        {
            if (! slot.voice.isActive())
            {
                clearVoiceSlot(slot);
                continue;
            }

            slot.keyDown = false;
            slot.sustained = false;
            slot.voice.stopNote(0.0f, true);
        }
    }

    void SynthEngineV2::handleAllSoundOff() noexcept
    {
        panic();
    }

    void SynthEngineV2::handleResetControllers() noexcept
    {
        pitchBendSemitones = 0.0f;
        modWheelValue = 0.0f;
        sustainPedalDown = false;
        releaseSustainedVoices();
    }

    void SynthEngineV2::releaseSustainedVoices() noexcept
    {
        for (auto& slot : voices)
        {
            if (! slot.sustained || slot.keyDown)
                continue;

            slot.sustained = false;
            slot.voice.stopNote(0.0f, true);
        }
    }

    int SynthEngineV2::findVoiceIndexToAllocate() const noexcept
    {
        int oldestReleasedVoice = -1;
        uint64_t oldestReleasedOrder = std::numeric_limits<uint64_t>::max();
        int oldestHeldVoice = -1;
        uint64_t oldestHeldOrder = std::numeric_limits<uint64_t>::max();

        for (int i = 0; i < static_cast<int>(voices.size()); ++i)
        {
            const auto& slot = voices[static_cast<size_t>(i)];

            if (! slot.voice.isActive())
                return i;

            if (! slot.keyDown)
            {
                if (slot.startOrder < oldestReleasedOrder)
                {
                    oldestReleasedOrder = slot.startOrder;
                    oldestReleasedVoice = i;
                }

                continue;
            }

            if (slot.startOrder < oldestHeldOrder)
            {
                oldestHeldOrder = slot.startOrder;
                oldestHeldVoice = i;
            }
        }

        if (oldestReleasedVoice >= 0)
            return oldestReleasedVoice;

        return juce::jmax(0, oldestHeldVoice);
    }

    int SynthEngineV2::findVoiceIndexForNoteOff(int midiNoteNumber) const noexcept
    {
        int matchedVoice = -1;
        uint64_t oldestMatchOrder = std::numeric_limits<uint64_t>::max();

        for (int i = 0; i < static_cast<int>(voices.size()); ++i)
        {
            const auto& slot = voices[static_cast<size_t>(i)];
            if (! slot.keyDown || slot.noteNumber != midiNoteNumber)
                continue;

            if (slot.startOrder < oldestMatchOrder)
            {
                oldestMatchOrder = slot.startOrder;
                matchedVoice = i;
            }
        }

        return matchedVoice;
    }

    void SynthEngineV2::clearVoiceSlot(VoiceSlot& slot) noexcept
    {
        slot.noteNumber = -1;
        slot.keyDown = false;
        slot.sustained = false;
        slot.startOrder = 0;
    }

    void SynthEngineV2::applyMasterGain(juce::AudioBuffer<float>& outputBuffer,
                                        float targetLinearGain) noexcept
    {
        masterGainLinear.setTargetValue(targetLinearGain);
        masterGainLinear.applyGain(outputBuffer, outputBuffer.getNumSamples());
    }

    DelayParameters SynthEngineV2::mapDelayParameters(const DelayParametersV2& parameters) noexcept
    {
        DelayParameters delay;
        delay.timeMs = parameters.timeMs;
        delay.feedback = parameters.enabled ? parameters.feedback : 0.0f;
        delay.mix = parameters.enabled ? parameters.mix : 0.0f;
        return delay;
    }
}
