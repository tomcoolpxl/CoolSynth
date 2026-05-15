#include "SynthEngineV2.h"

#include <cmath>
#include <limits>

namespace coolsynth::synth
{
    namespace
    {
        constexpr float maxVintageDriftCents = 25.0f;

        float computeVoicePan(int voiceIndex, int totalVoices, float panSpread) noexcept
        {
            if (totalVoices <= 1)
                return 0.0f;

            const float position = (static_cast<float>(voiceIndex) / static_cast<float>(totalVoices - 1)) * 2.0f - 1.0f;
            const float spread = juce::jlimit(0.0f, 1.0f, panSpread);
            return juce::jlimit(-1.0f, 1.0f, position * spread);
        }

        float computeVoicePanLeft(int voiceIndex, int totalVoices, float panSpread) noexcept
        {
            const float pan = computeVoicePan(voiceIndex, totalVoices, panSpread);
            return 1.0f - juce::jmax(0.0f, pan);
        }

        float computeVoicePanRight(int voiceIndex, int totalVoices, float panSpread) noexcept
        {
            const float pan = computeVoicePan(voiceIndex, totalVoices, panSpread);
            return 1.0f - juce::jmax(0.0f, -pan);
        }

        float deterministicVoiceCents(int voiceIndex) noexcept
        {
            constexpr uint32_t mixer = 0x9e3779b9u;
            uint32_t state = static_cast<uint32_t>(voiceIndex + 1) * mixer;
            state ^= state >> 16;
            state *= 0x85ebca6bu;
            state ^= state >> 13;
            state *= 0xc2b2ae35u;
            state ^= state >> 16;
            const float normalized = static_cast<float>(state & 0xffffffu) / static_cast<float>(0xffffffu);
            return (normalized * 2.0f) - 1.0f;
        }
    }

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
        globalFxRack.prepare(sampleRate, samplesPerBlock, outputChannelCount);
        arpeggiator.prepare(sampleRate);
        panic();
        prepared = true;
    }

    void SynthEngineV2::releaseResources() noexcept
    {
        panic();
        globalFxRack.reset();
        prepared = false;
    }

    void SynthEngineV2::render(juce::AudioBuffer<float>& outputBuffer,
                               std::span<const EngineMidiEvent> midiEvents,
                               const BlockRenderParametersV2& parameters,
                               const EngineTransportInfo& transport)
    {
        if (! prepared)
            return;

        currentPlayMode = parameters.performance.playMode;

        const float lfoPhaseIncrement = computeLfoPhaseIncrementPerSample(parameters.lfo.rateHz, currentSampleRate);

        const int blockSamples = outputBuffer.getNumSamples();
        const bool arpEnabled = parameters.arp.enabled;

        arpeggiator.setParameters(parameters.arp);
        arpeggiator.setTransportInfo(transport);

        // Phase 1: when arp is enabled, absorb incoming note events into the arp
        // held/latched trackers BEFORE generating its events for this block.
        // Non-note control events still propagate to the engine normally.
        if (arpEnabled)
        {
            for (const auto& event : midiEvents)
            {
                switch (event.type)
                {
                    case EngineMidiEventType::noteOn:
                        arpeggiator.onNoteOn(static_cast<int>(event.noteNumber),
                                             juce::jlimit(0.0f, 1.0f, event.value));
                        break;
                    case EngineMidiEventType::noteOff:
                        arpeggiator.onNoteOff(static_cast<int>(event.noteNumber));
                        break;
                    case EngineMidiEventType::allNotesOff:
                    case EngineMidiEventType::allSoundOff:
                    case EngineMidiEventType::resetControllers:
                        arpeggiator.onAllNotesOff();
                        break;
                    default:
                        break;
                }
            }
        }

        std::array<EngineMidiEvent, maxArpEventsPerBlock> arpEventBuffer {};
        const int arpEventCount = arpeggiator.generateEventsForBlock(blockSamples,
                                                                     currentSampleRate,
                                                                     arpEventBuffer.data(),
                                                                     static_cast<int>(arpEventBuffer.size()));

        int renderedSamples = 0;
        size_t midiIndex = 0;
        int arpIndex = 0;
        const size_t midiCount = midiEvents.size();

        while (midiIndex < midiCount || arpIndex < arpEventCount)
        {
            const int midiOffset = midiIndex < midiCount
                ? juce::jlimit(0, blockSamples, midiEvents[midiIndex].sampleOffset)
                : std::numeric_limits<int>::max();
            const int arpOffset = arpIndex < arpEventCount
                ? juce::jlimit(0, blockSamples, arpEventBuffer[static_cast<size_t>(arpIndex)].sampleOffset)
                : std::numeric_limits<int>::max();

            const int nextOffset = juce::jmin(midiOffset, arpOffset);
            if (nextOffset >= blockSamples)
                break;

            if (nextOffset > renderedSamples)
            {
                const int spanSamples = nextOffset - renderedSamples;
                renderVoiceSpan(outputBuffer, renderedSamples, spanSamples, parameters, globalLfoPhase);
                globalLfoPhase += lfoPhaseIncrement * static_cast<float>(spanSamples);
                globalLfoPhase -= std::floor(globalLfoPhase);
                renderedSamples = nextOffset;
            }

            // Process arp events at this offset first so a step-aligned
            // noteOff fires before a coincident noteOn keeps the voice
            // cleanly retriggered.
            if (arpOffset == nextOffset && arpIndex < arpEventCount)
            {
                handleEvent(arpEventBuffer[static_cast<size_t>(arpIndex)], parameters);
                ++arpIndex;
            }

            if (midiOffset == nextOffset && midiIndex < midiCount)
            {
                const auto& midiEvent = midiEvents[midiIndex++];
                const bool isNote = midiEvent.type == EngineMidiEventType::noteOn
                                    || midiEvent.type == EngineMidiEventType::noteOff;

                if (arpEnabled && isNote)
                {
                    // The arp already consumed this note above; the allocator
                    // must not also see it, otherwise notes would double up.
                    continue;
                }

                handleEvent(midiEvent, parameters);
            }
        }

        if (renderedSamples < blockSamples)
        {
            const int spanSamples = blockSamples - renderedSamples;
            renderVoiceSpan(outputBuffer, renderedSamples, spanSamples, parameters, globalLfoPhase);
            globalLfoPhase += lfoPhaseIncrement * static_cast<float>(spanSamples);
            globalLfoPhase -= std::floor(globalLfoPhase);
        }

        globalFxRack.process(outputBuffer,
                             parameters.drive,
                             parameters.chorus,
                             parameters.delay,
                             parameters.reverb);
        applyMasterGain(outputBuffer, parameters.masterGainLinear);
    }

    void SynthEngineV2::panic() noexcept
    {
        for (auto& slot : voices)
        {
            slot.voice.forceStop();
            clearVoiceSlot(slot);
        }

        globalFxRack.clear();
        clearHeldNotes();
        arpeggiator.panic();
        nextVoiceStartOrder = 0;
        pitchBendSemitones = 0.0f;
        modWheelValue = 0.0f;
        globalLfoPhase = 0.0f;
        lastPlayedNote = -1;
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

    void SynthEngineV2::applyVoiceParametersForSpan(const BlockRenderParametersV2& parameters,
                                                    float lfoPhase) noexcept
    {
        for (auto& slot : voices)
        {
            slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
            slot.voice.setOutputLevel(1.0f);
            slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
            slot.voice.setNextFilterEnvelopeParameters(parameters.filterEnvelope);
            slot.voice.setNextFilterParameters(parameters.filter);
            slot.voice.setNextModulationParameters(parameters.lfo, parameters.polyMod, parameters.performance);
            slot.voice.setGlobalLfoState(lfoPhase, modWheelValue);
            slot.voice.setPitchBendSemitones(pitchBendSemitones);
        }
    }

    void SynthEngineV2::renderVoiceSpan(juce::AudioBuffer<float>& outputBuffer,
                                        int startSample,
                                        int numSamples,
                                        const BlockRenderParametersV2& parameters,
                                        float lfoPhase) noexcept
    {
        if (numSamples <= 0)
            return;

        applyVoiceParametersForSpan(parameters, lfoPhase);

        for (auto& slot : voices)
        {
            if (! slot.voice.isActive())
            {
                clearVoiceSlot(slot);
                continue;
            }

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
            case EngineMidiEventType::noteOff:       handleNoteOff(event, parameters); break;
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
        const int noteNumber = static_cast<int>(event.noteNumber);
        const float velocity = juce::jlimit(0.0f, 1.0f, event.value);

        addHeldNote(noteNumber, velocity);

        switch (parameters.performance.playMode)
        {
            case coolsynth::parameters::PlayModeChoice::mono:
                allocateMonoNote(noteNumber, velocity, parameters);
                break;
            case coolsynth::parameters::PlayModeChoice::unison:
                allocateUnisonNote(noteNumber, velocity, parameters);
                break;
            case coolsynth::parameters::PlayModeChoice::poly:
            default:
                allocatePolyNote(noteNumber, velocity, parameters);
                break;
        }

        lastPlayedNote = noteNumber;
    }

    void SynthEngineV2::handleNoteOff(const EngineMidiEvent& event,
                                      const BlockRenderParametersV2& parameters) noexcept
    {
        const int noteNumber = static_cast<int>(event.noteNumber);
        removeHeldNote(noteNumber);

        const auto playMode = parameters.performance.playMode;

        if (playMode == coolsynth::parameters::PlayModeChoice::mono
            || playMode == coolsynth::parameters::PlayModeChoice::unison)
        {
            bool wasSoundingNote = false;
            for (const auto& slot : voices)
            {
                if (slot.voice.isActive() && slot.keyDown && slot.noteNumber == noteNumber)
                {
                    wasSoundingNote = true;
                    break;
                }
            }

            if (! wasSoundingNote)
                return;

            if (heldNoteCount > 0 && playMode == coolsynth::parameters::PlayModeChoice::mono)
            {
                retriggerMonoFromHeldNotes(parameters);
                return;
            }

            if (heldNoteCount > 0 && playMode == coolsynth::parameters::PlayModeChoice::unison)
            {
                const int priorityIndex = pickHeldNoteByPriority(parameters.performance.keyPriority);
                if (priorityIndex >= 0)
                {
                    const auto held = heldNotes[static_cast<size_t>(priorityIndex)];
                    allocateUnisonNote(held.noteNumber, held.velocity, parameters);
                    lastPlayedNote = held.noteNumber;
                    return;
                }
            }

            for (auto& slot : voices)
            {
                if (! slot.voice.isActive())
                    continue;

                slot.keyDown = false;

                if (sustainPedalDown && ! event.fromArp)
                    slot.sustained = true;
                else
                    slot.voice.stopNote(0.0f, true);
            }
            return;
        }

        const int voiceIndex = findVoiceIndexForNoteOff(noteNumber);
        if (voiceIndex < 0)
            return;

        auto& slot = voices[static_cast<size_t>(voiceIndex)];
        slot.keyDown = false;

        if (sustainPedalDown && ! event.fromArp)
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
        clearHeldNotes();
        arpeggiator.onAllNotesOff();

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
        arpeggiator.onAllNotesOff();
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

    void SynthEngineV2::allocatePolyNote(int noteNumber,
                                         float velocity,
                                         const BlockRenderParametersV2& parameters) noexcept
    {
        const int voiceIndex = findVoiceIndexToAllocate();
        auto& slot = voices[static_cast<size_t>(voiceIndex)];

        if (slot.voice.isActive())
            slot.voice.forceStop();

        slot.noteNumber = noteNumber;
        slot.keyDown = true;
        slot.sustained = false;
        slot.startOrder = ++nextVoiceStartOrder;
        slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
        slot.voice.setOutputLevel(1.0f);
        slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
        slot.voice.setNextFilterEnvelopeParameters(parameters.filterEnvelope);
        slot.voice.setNextFilterParameters(parameters.filter);
        slot.voice.setNextModulationParameters(parameters.lfo, parameters.polyMod, parameters.performance);
        slot.voice.setGlobalLfoState(globalLfoPhase, modWheelValue);
        assignVoicePanForIndex(slot, voiceIndex, static_cast<int>(voices.size()), parameters.performance.panSpread);
        assignVoiceVintageForIndex(slot, voiceIndex, parameters.performance.vintageAmount);
        slot.voice.startNote(slot.noteNumber, velocity);
        slot.voice.setGlideFromNote(lastPlayedNote, parameters.performance.glideTimeSeconds);
        slot.voice.setPitchBendSemitones(pitchBendSemitones);
    }

    void SynthEngineV2::allocateMonoNote(int noteNumber,
                                         float velocity,
                                         const BlockRenderParametersV2& parameters) noexcept
    {
        const int voiceIndex = 0;
        auto& slot = voices[static_cast<size_t>(voiceIndex)];

        for (size_t i = 1; i < voices.size(); ++i)
        {
            auto& other = voices[i];
            if (other.voice.isActive())
                other.voice.forceStop();
            clearVoiceSlot(other);
        }

        const int glideFromNote = (slot.voice.isActive() && slot.noteNumber >= 0) ? slot.noteNumber : lastPlayedNote;

        slot.noteNumber = noteNumber;
        slot.keyDown = true;
        slot.sustained = false;
        slot.startOrder = ++nextVoiceStartOrder;
        slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
        slot.voice.setOutputLevel(1.0f);
        slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
        slot.voice.setNextFilterEnvelopeParameters(parameters.filterEnvelope);
        slot.voice.setNextFilterParameters(parameters.filter);
        slot.voice.setNextModulationParameters(parameters.lfo, parameters.polyMod, parameters.performance);
        slot.voice.setGlobalLfoState(globalLfoPhase, modWheelValue);
        slot.voice.setPan(1.0f, 1.0f);
        slot.voice.setVintageDriftCents(0.0f);
        slot.voice.startNote(noteNumber, velocity);
        slot.voice.setGlideFromNote(glideFromNote, parameters.performance.glideTimeSeconds);
        slot.voice.setPitchBendSemitones(pitchBendSemitones);
    }

    void SynthEngineV2::allocateUnisonNote(int noteNumber,
                                           float velocity,
                                           const BlockRenderParametersV2& parameters) noexcept
    {
        const int totalVoices = static_cast<int>(voices.size());

        for (int i = 0; i < totalVoices; ++i)
        {
            auto& slot = voices[static_cast<size_t>(i)];
            const int glideFromNote = (slot.voice.isActive() && slot.noteNumber >= 0) ? slot.noteNumber : lastPlayedNote;
            const bool wasActive = slot.voice.isActive();

            if (! wasActive)
                slot.voice.forceStop();

            slot.noteNumber = noteNumber;
            slot.keyDown = true;
            slot.sustained = false;
            slot.startOrder = ++nextVoiceStartOrder;
            slot.voice.setNextVoiceSourceParameters(parameters.oscA, parameters.oscB, parameters.mixer);
            slot.voice.setOutputLevel(1.0f / std::sqrt(static_cast<float>(juce::jmax(1, totalVoices))));
            slot.voice.setNextEnvelopeParameters(parameters.ampEnvelope);
            slot.voice.setNextFilterEnvelopeParameters(parameters.filterEnvelope);
            slot.voice.setNextFilterParameters(parameters.filter);
            slot.voice.setNextModulationParameters(parameters.lfo, parameters.polyMod, parameters.performance);
            slot.voice.setGlobalLfoState(globalLfoPhase, modWheelValue);
            assignVoicePanForIndex(slot, i, totalVoices, parameters.performance.panSpread);
            const float unisonVintage = parameters.performance.vintageAmount;
            assignVoiceVintageForIndex(slot, i, unisonVintage);
            slot.voice.startNote(noteNumber, velocity);
            slot.voice.setGlideFromNote(glideFromNote, parameters.performance.glideTimeSeconds);
            slot.voice.setPitchBendSemitones(pitchBendSemitones);
        }
    }

    void SynthEngineV2::retriggerMonoFromHeldNotes(const BlockRenderParametersV2& parameters) noexcept
    {
        const int priorityIndex = pickHeldNoteByPriority(parameters.performance.keyPriority);
        if (priorityIndex < 0)
            return;

        const auto held = heldNotes[static_cast<size_t>(priorityIndex)];
        allocateMonoNote(held.noteNumber, held.velocity, parameters);
        lastPlayedNote = held.noteNumber;
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
        int activeCount = 0;
        for (const auto& slot : voices)
            if (slot.voice.isActive())
                ++activeCount;
        const float headroom = 1.0f / std::sqrt(static_cast<float>(juce::jmax(1, activeCount)));
        masterGainLinear.setTargetValue(targetLinearGain * headroom);
        masterGainLinear.applyGain(outputBuffer, outputBuffer.getNumSamples());
    }

    void SynthEngineV2::addHeldNote(int noteNumber, float velocity) noexcept
    {
        removeHeldNote(noteNumber);

        if (heldNoteCount >= maxHeldNotes)
        {
            for (int i = 0; i < heldNoteCount - 1; ++i)
                heldNotes[static_cast<size_t>(i)] = heldNotes[static_cast<size_t>(i + 1)];
            --heldNoteCount;
        }

        heldNotes[static_cast<size_t>(heldNoteCount)] = HeldNote { noteNumber, velocity, ++nextHeldOrder };
        ++heldNoteCount;
    }

    void SynthEngineV2::removeHeldNote(int noteNumber) noexcept
    {
        int writeIndex = 0;
        for (int i = 0; i < heldNoteCount; ++i)
        {
            if (heldNotes[static_cast<size_t>(i)].noteNumber == noteNumber)
                continue;

            heldNotes[static_cast<size_t>(writeIndex++)] = heldNotes[static_cast<size_t>(i)];
        }
        heldNoteCount = writeIndex;
    }

    void SynthEngineV2::clearHeldNotes() noexcept
    {
        heldNoteCount = 0;
        nextHeldOrder = 0;
    }

    int SynthEngineV2::pickHeldNoteByPriority(coolsynth::parameters::KeyPriorityChoice priority) const noexcept
    {
        if (heldNoteCount <= 0)
            return -1;

        int chosen = 0;
        for (int i = 1; i < heldNoteCount; ++i)
        {
            const auto& candidate = heldNotes[static_cast<size_t>(i)];
            const auto& current = heldNotes[static_cast<size_t>(chosen)];

            switch (priority)
            {
                case coolsynth::parameters::KeyPriorityChoice::low:
                    if (candidate.noteNumber < current.noteNumber)
                        chosen = i;
                    break;
                case coolsynth::parameters::KeyPriorityChoice::high:
                    if (candidate.noteNumber > current.noteNumber)
                        chosen = i;
                    break;
                case coolsynth::parameters::KeyPriorityChoice::last:
                default:
                    if (candidate.order > current.order)
                        chosen = i;
                    break;
            }
        }

        return chosen;
    }

    void SynthEngineV2::assignVoicePanForIndex(VoiceSlot& slot,
                                               int voiceIndex,
                                               int totalVoices,
                                               float panSpread) noexcept
    {
        const float left = computeVoicePanLeft(voiceIndex, totalVoices, panSpread);
        const float right = computeVoicePanRight(voiceIndex, totalVoices, panSpread);
        slot.voice.setPan(left, right);
    }

    void SynthEngineV2::assignVoiceVintageForIndex(VoiceSlot& slot,
                                                   int voiceIndex,
                                                   float vintageAmount) noexcept
    {
        const float amount = juce::jlimit(0.0f, 1.0f, vintageAmount);
        const float cents = deterministicVoiceCents(voiceIndex) * amount * maxVintageDriftCents;
        slot.voice.setVintageDriftCents(cents);
    }

    float SynthEngineV2::computeLfoPhaseIncrementPerSample(float rateHz, double sampleRate) noexcept
    {
        const float sr = static_cast<float>(juce::jmax(1.0, sampleRate));
        return juce::jmax(0.0f, rateHz) / sr;
    }
}
