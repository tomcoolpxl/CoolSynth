#include "Arpeggiator.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace coolsynth::synth
{
    namespace
    {
        constexpr float minGateFraction = 0.05f;
        constexpr float maxGateFraction = 0.95f;
        constexpr float maxSwingFraction = 0.75f;
        constexpr float ppqEpsilon = 1.0e-9f;
        constexpr int stepsPerBar = 4;

        bool usesPlayOrder(coolsynth::parameters::ArpPatternChoice pattern) noexcept
        {
            return pattern == coolsynth::parameters::ArpPatternChoice::asPlayed;
        }

        int convergeIndex(int stepIndex, int length) noexcept
        {
            if (length <= 1)
                return 0;

            const int pair = stepIndex / 2;
            const bool flip = (stepIndex & 1) != 0;
            return flip ? (length - 1 - pair) : pair;
        }

        int divergeIndex(int stepIndex, int length) noexcept
        {
            if (length <= 1)
                return 0;

            const int mid = length / 2;
            const int pair = stepIndex / 2;
            const bool flip = (stepIndex & 1) != 0;
            const int index = flip ? (mid - 1 - pair) : (mid + pair);
            return juce::jlimit(0, length - 1, index);
        }

        bool useHostSync(const EngineTransportInfo& info) noexcept
        {
            return info.hostHasTempo && info.hostHasPpq && info.hostBpm > 0.0;
        }

        int positiveModulo(int value, int modulus) noexcept
        {
            if (modulus <= 0)
                return 0;

            const int remainder = value % modulus;
            return remainder < 0 ? (remainder + modulus) : remainder;
        }

        void insertSortedEvent(EngineMidiEvent* outEvents,
                               int& outEventCount,
                               const EngineMidiEvent& event,
                               int maxEvents) noexcept
        {
            if (outEventCount >= maxEvents)
                return;

            int insertIndex = outEventCount;
            while (insertIndex > 0
                   && outEvents[insertIndex - 1].sampleOffset > event.sampleOffset)
            {
                outEvents[insertIndex] = outEvents[insertIndex - 1];
                --insertIndex;
            }
            outEvents[insertIndex] = event;
            ++outEventCount;
        }

        int buildBjorklundSequence(int level,
                                   const std::array<int, 16>& counts,
                                   const std::array<int, 16>& remainders,
                                   std::array<bool, 16>& pattern,
                                   int writeIndex) noexcept
        {
            if (level == -1)
            {
                pattern[static_cast<size_t>(writeIndex++)] = false;
                return writeIndex;
            }

            if (level == -2)
            {
                pattern[static_cast<size_t>(writeIndex++)] = true;
                return writeIndex;
            }

            for (int i = 0; i < counts[static_cast<size_t>(level)]; ++i)
                writeIndex = buildBjorklundSequence(level - 1, counts, remainders, pattern, writeIndex);

            if (remainders[static_cast<size_t>(level)] != 0)
                writeIndex = buildBjorklundSequence(level - 2, counts, remainders, pattern, writeIndex);

            return writeIndex;
        }

        std::array<bool, 16> makeEuclideanCycle(int pulses, int steps, int rotation) noexcept
        {
            std::array<bool, 16> cycle {};

            const int clampedSteps = juce::jlimit(1, 16, steps);
            const int clampedPulses = juce::jlimit(0, clampedSteps, pulses);

            if (clampedPulses == 0)
                return cycle;

            if (clampedPulses >= clampedSteps)
            {
                for (int index = 0; index < clampedSteps; ++index)
                    cycle[static_cast<size_t>(index)] = true;
                return cycle;
            }

            std::array<int, 16> counts {};
            std::array<int, 16> remainders {};

            remainders[0] = clampedPulses;
            int divisor = clampedSteps - clampedPulses;
            int level = 0;

            while (remainders[static_cast<size_t>(level)] > 1)
            {
                counts[static_cast<size_t>(level)] =
                    divisor / remainders[static_cast<size_t>(level)];
                remainders[static_cast<size_t>(level + 1)] =
                    divisor % remainders[static_cast<size_t>(level)];
                divisor = remainders[static_cast<size_t>(level)];
                ++level;
            }

            counts[static_cast<size_t>(level)] = divisor;

            std::array<bool, 16> rawPattern {};
            const int generatedLength =
                buildBjorklundSequence(level, counts, remainders, rawPattern, 0);

            const int rotationOffset = positiveModulo(rotation, clampedSteps);
            const int firstPulseIndex =
                generatedLength > 0
                    ? [&rawPattern, generatedLength]()
                    {
                        for (int index = 0; index < generatedLength; ++index)
                        {
                            if (rawPattern[static_cast<size_t>(index)])
                                return index;
                        }
                        return 0;
                    }()
                    : 0;

            for (int index = 0; index < clampedSteps; ++index)
            {
                const int sourceIndex =
                    (index + firstPulseIndex + rotationOffset) % clampedSteps;
                cycle[static_cast<size_t>(index)] = rawPattern[static_cast<size_t>(sourceIndex)];
            }

            return cycle;
        }
    }

    Arpeggiator::Arpeggiator() noexcept = default;

    void Arpeggiator::prepare(double sampleRate) noexcept
    {
        preparedSampleRate = juce::jmax(1.0, sampleRate);
        rng.setSeed(static_cast<int64_t>(juce::Time::getHighResolutionTicks()));
        panic();
    }

    void Arpeggiator::panic() noexcept
    {
        heldNoteCount = 0;
        nextHeldOrder = 0;
        latchedNoteCount = 0;
        nextLatchedOrder = 0;
        ringingNoteCount = 0;
        samplesUntilNextStep = 0.0f;
        internalClockArmed = false;
        patternStepCounter = 0;
        emittedStepCounter = 0;
        randomWalkIndex = 0;
        euclideanPosition = 0;
        updateEuclideanCycle();
        previousHostIsPlaying = false;
        previousEnabled = false;
    }

    void Arpeggiator::setSeedForTesting(uint64_t seed) noexcept
    {
        rng.setSeed(static_cast<int64_t>(seed));
        resetPatternWalkState();
    }

    void Arpeggiator::setParameters(const ArpParametersV2& parameters) noexcept
    {
        const bool wasLatch = currentParameters.latch;
        const bool nowLatch = parameters.latch;
        const auto previousPattern = currentParameters.pattern;
        const auto previousRhythm = currentParameters.rhythm;
        const auto previousEuclideanPulses = currentParameters.euclideanPulses;
        const auto previousEuclideanSteps = currentParameters.euclideanSteps;
        const auto previousEuclideanRotation = currentParameters.euclideanRotation;

        if (! wasLatch && nowLatch)
            copyHeldIntoLatched();
        else if (wasLatch && ! nowLatch)
            clearLatched();

        currentParameters = parameters;

        if (previousPattern != currentParameters.pattern)
            resetPatternWalkState();

        if (previousRhythm != currentParameters.rhythm
            || previousEuclideanPulses != currentParameters.euclideanPulses
            || previousEuclideanSteps != currentParameters.euclideanSteps
            || previousEuclideanRotation != currentParameters.euclideanRotation)
        {
            updateEuclideanCycle();
        }
    }

    void Arpeggiator::setTransportInfo(const EngineTransportInfo& transport) noexcept
    {
        currentTransport = transport;
    }

    void Arpeggiator::onNoteOn(int noteNumber, float velocity) noexcept
    {
        const bool clampedNote = noteNumber >= 0 && noteNumber <= 127;
        if (! clampedNote)
            return;

        if (currentParameters.latch && heldNoteCount == 0)
            clearLatched();

        addNoteToHeld(noteNumber, velocity);

        if (currentParameters.latch)
            addNoteToLatched(noteNumber, velocity);
    }

    void Arpeggiator::onNoteOff(int noteNumber) noexcept
    {
        removeNoteFromHeld(noteNumber);
    }

    void Arpeggiator::onAllNotesOff() noexcept
    {
        // Soft reset: release held set, but preserve latched memory.
        clearHeld();
    }

    int Arpeggiator::generateEventsForBlock(int blockSamples,
                                            double currentSampleRate,
                                            EngineMidiEvent* outEvents,
                                            int maxEvents) noexcept
    {
        if (outEvents == nullptr || maxEvents <= 0 || blockSamples <= 0)
            return 0;

        if (currentSampleRate > 0.0)
            preparedSampleRate = currentSampleRate;

        int outEventCount = 0;

        const bool nowEnabled = currentParameters.enabled;
        const bool enabledTransitionedOn = ! previousEnabled && nowEnabled;
        const bool enabledTransitionedOff = previousEnabled && ! nowEnabled;

        if (enabledTransitionedOff)
        {
            emitNoteOffsForAllRinging(outEvents, outEventCount, 0, maxEvents);
            samplesUntilNextStep = 0.0f;
            internalClockArmed = false;
        }
        else if (enabledTransitionedOn)
        {
            resetPatternWalkState();
            euclideanPosition = 0;
        }

        previousEnabled = nowEnabled;

        // Always advance pending gate-offs even if arp is now disabled, so
        // any straggler events get flushed in this block.
        if (! nowEnabled)
        {
            popRingingForBlock(outEvents, outEventCount, blockSamples, maxEvents);
            previousHostIsPlaying = false;
            return outEventCount;
        }

        const bool hostSync = useHostSync(currentTransport);
        const bool hostStopped = hostSync && ! currentTransport.hostIsPlaying;
        const bool hostTransportTransitionedOff =
            hostSync && previousHostIsPlaying && ! currentTransport.hostIsPlaying;

        if (hostTransportTransitionedOff)
        {
            emitNoteOffsForAllRinging(outEvents, outEventCount, 0, maxEvents);
        }

        previousHostIsPlaying = hostSync ? currentTransport.hostIsPlaying : false;

        // Always drain pending gate-offs for the block first.
        popRingingForBlock(outEvents, outEventCount, blockSamples, maxEvents);

        if (hostStopped)
            return outEventCount;

        const int stepsPerBeat = computeStepsPerBeat();
        if (stepsPerBeat <= 0)
            return outEventCount;

        const double bpm = hostSync ? currentTransport.hostBpm : juce::jlimit(20.0, 400.0, static_cast<double>(currentParameters.internalTempoBpm));
        if (bpm <= 0.0)
            return outEventCount;

        const float stepLengthSamples = static_cast<float>(
            preparedSampleRate * 60.0 / bpm / static_cast<double>(stepsPerBeat));

        if (stepLengthSamples < 1.0f)
            return outEventCount;

        const float gateFraction = juce::jlimit(minGateFraction,
                                                maxGateFraction,
                                                currentParameters.gateLength);
        const float swingAmount = juce::jlimit(0.0f,
                                               maxSwingFraction,
                                               currentParameters.swingAmount);
        const float chance = juce::jlimit(0.0f, 1.0f, currentParameters.chance);
        const float ratchetChance = juce::jlimit(0.0f, 1.0f, currentParameters.ratchetChance);
        const float accentAmount = juce::jlimit(0.0f, 1.0f, currentParameters.accentAmount);
        const int gateLengthSamples = juce::jmax(1, static_cast<int>(stepLengthSamples * gateFraction));

        // Compute when the first step fires inside this block.
        float firstStepOffset = 0.0f;
        int hostStepIndex = 0;

        if (hostSync)
        {
            const double stepLengthPpq = 1.0 / static_cast<double>(stepsPerBeat);
            const double ppqAtStart = currentTransport.hostPpqAtBlockStart;
            const double rawIndex = ppqAtStart / stepLengthPpq;
            double nextStepIndex = std::floor(rawIndex);
            if (rawIndex - nextStepIndex > ppqEpsilon)
                nextStepIndex = std::floor(rawIndex) + 1.0;

            const double nextStepPpq = nextStepIndex * stepLengthPpq;
            const double samplesPerPpq = preparedSampleRate * 60.0 / bpm;
            const double offset = (nextStepPpq - ppqAtStart) * samplesPerPpq;
            firstStepOffset = static_cast<float>(juce::jmax(0.0, offset));
            hostStepIndex = juce::jmax(0, static_cast<int>(nextStepIndex));
            // Refresh internal clock so that if host sync drops out next block,
            // the internal clock continues smoothly.
            samplesUntilNextStep = firstStepOffset;
            internalClockArmed = true;
        }
        else
        {
            if (! internalClockArmed)
            {
                samplesUntilNextStep = 0.0f;
                internalClockArmed = true;
            }
            firstStepOffset = samplesUntilNextStep;
        }

        float stepOffset = firstStepOffset;

        while (stepOffset < static_cast<float>(blockSamples) && outEventCount < maxEvents)
        {
            const bool emitsThisRhythmStep = shouldEmitEuclideanStep(hostStepIndex);
            if (hostSync)
                ++hostStepIndex;

            if (! emitsThisRhythmStep)
            {
                stepOffset += stepLengthSamples;
                continue;
            }

            const bool swingThisStep = (patternStepCounter & 1) != 0;
            const float swingDelaySamples = swingThisStep
                ? (swingAmount * (stepLengthSamples * 0.5f))
                : 0.0f;
            const int eventOffset = juce::jlimit(0,
                                                 juce::jmax(0, blockSamples - 1),
                                                 static_cast<int>(stepOffset + swingDelaySamples));

            if (currentParameters.pattern == coolsynth::parameters::ArpPatternChoice::chord)
            {
                std::array<HeldEntry, maxArpHeldNotes> ordered {};
                const int workingSetSize = buildOrderedWorkingSet(ordered);

                if (workingSetSize > 0)
                {
                    const int octaveRange = juce::jlimit(1, 3, currentParameters.octaveRange);
                    const int octaveShift = patternStepCounter % octaveRange;
                    ++patternStepCounter;

                    std::array<int, maxArpHeldNotes> chordNotes {};
                    std::array<float, maxArpHeldNotes> chordVelocities {};
                    int validNoteCount = 0;

                    for (int index = 0; index < workingSetSize; ++index)
                    {
                        const auto& entry = ordered[static_cast<size_t>(index)];
                        const int shiftedNote = entry.noteNumber + (12 * octaveShift);
                        if (shiftedNote < 0 || shiftedNote > 127)
                            continue;

                        chordNotes[static_cast<size_t>(validNoteCount)] = shiftedNote;
                        chordVelocities[static_cast<size_t>(validNoteCount)] = entry.velocity;
                        ++validNoteCount;
                    }

                    if (validNoteCount > 0)
                    {
                        const bool stepPassedChance = chance >= 1.0f || rng.nextFloat() < chance;
                        if (! stepPassedChance)
                        {
                            stepOffset += stepLengthSamples;
                            continue;
                        }

                        const int ratchetCount = resolveRatchetCount();
                        const bool ratchetActive = ratchetCount > 1
                            && (ratchetChance >= 1.0f || rng.nextFloat() < ratchetChance);
                        const int stepRatchetCount = ratchetActive ? ratchetCount : 1;
                        const int accentEvery = resolveAccentEvery();
                        const bool accentThisStep = accentEvery > 0
                            && (emittedStepCounter % accentEvery) == 0;
                        const float velocityScale = accentThisStep
                            ? (1.0f + accentAmount)
                            : 1.0f;

                        if (emitStepNotes(chordNotes,
                                          chordVelocities,
                                          validNoteCount,
                                          eventOffset,
                                          gateLengthSamples,
                                          stepRatchetCount,
                                          velocityScale,
                                          outEvents,
                                          outEventCount,
                                          blockSamples,
                                          maxEvents))
                        {
                            ++emittedStepCounter;
                        }
                    }
                }
            }
            else
            {
                float velocity = 0.0f;
                const int note = pickNextPatternNote(velocity);

                if (note >= 0)
                {
                    const bool stepPassedChance = chance >= 1.0f || rng.nextFloat() < chance;
                    if (! stepPassedChance)
                    {
                        stepOffset += stepLengthSamples;
                        continue;
                    }

                    std::array<int, maxArpHeldNotes> stepNotes {};
                    std::array<float, maxArpHeldNotes> stepVelocities {};
                    stepNotes[0] = note;
                    stepVelocities[0] = velocity;

                    const int ratchetCount = resolveRatchetCount();
                    const bool ratchetActive = ratchetCount > 1
                        && (ratchetChance >= 1.0f || rng.nextFloat() < ratchetChance);
                    const int stepRatchetCount = ratchetActive ? ratchetCount : 1;
                    const int accentEvery = resolveAccentEvery();
                    const bool accentThisStep = accentEvery > 0
                        && (emittedStepCounter % accentEvery) == 0;
                    const float velocityScale = accentThisStep
                        ? (1.0f + accentAmount)
                        : 1.0f;

                    if (emitStepNotes(stepNotes,
                                      stepVelocities,
                                      1,
                                      eventOffset,
                                      gateLengthSamples,
                                      stepRatchetCount,
                                      velocityScale,
                                      outEvents,
                                      outEventCount,
                                      blockSamples,
                                      maxEvents))
                    {
                        ++emittedStepCounter;
                    }
                }
            }

            stepOffset += stepLengthSamples;
        }

        // Carry remaining step countdown into next block.
        samplesUntilNextStep = stepOffset - static_cast<float>(blockSamples);
        if (samplesUntilNextStep < 0.0f)
            samplesUntilNextStep = 0.0f;

        return outEventCount;
    }

    void Arpeggiator::addNoteToHeld(int noteNumber, float velocity) noexcept
    {
        for (int i = 0; i < heldNoteCount; ++i)
        {
            if (heldNotes[static_cast<size_t>(i)].noteNumber == noteNumber)
            {
                heldNotes[static_cast<size_t>(i)].velocity = velocity;
                heldNotes[static_cast<size_t>(i)].order = ++nextHeldOrder;
                resetPatternWalkState();
                return;
            }
        }

        if (heldNoteCount >= maxArpHeldNotes)
        {
            // Drop oldest entry to make room.
            for (int i = 0; i < heldNoteCount - 1; ++i)
                heldNotes[static_cast<size_t>(i)] = heldNotes[static_cast<size_t>(i + 1)];
            --heldNoteCount;
        }

        heldNotes[static_cast<size_t>(heldNoteCount)] =
            HeldEntry { noteNumber, velocity, ++nextHeldOrder };
        ++heldNoteCount;
        resetPatternWalkState();
    }

    void Arpeggiator::removeNoteFromHeld(int noteNumber) noexcept
    {
        int writeIndex = 0;
        for (int i = 0; i < heldNoteCount; ++i)
        {
            if (heldNotes[static_cast<size_t>(i)].noteNumber == noteNumber)
                continue;
            heldNotes[static_cast<size_t>(writeIndex++)] = heldNotes[static_cast<size_t>(i)];
        }
        if (writeIndex != heldNoteCount)
            resetPatternWalkState();
        heldNoteCount = writeIndex;
    }

    void Arpeggiator::addNoteToLatched(int noteNumber, float velocity) noexcept
    {
        for (int i = 0; i < latchedNoteCount; ++i)
        {
            if (latchedNotes[static_cast<size_t>(i)].noteNumber == noteNumber)
            {
                latchedNotes[static_cast<size_t>(i)].velocity = velocity;
                latchedNotes[static_cast<size_t>(i)].order = ++nextLatchedOrder;
                resetPatternWalkState();
                return;
            }
        }

        if (latchedNoteCount >= maxArpLatchedNotes)
        {
            for (int i = 0; i < latchedNoteCount - 1; ++i)
                latchedNotes[static_cast<size_t>(i)] = latchedNotes[static_cast<size_t>(i + 1)];
            --latchedNoteCount;
        }

        latchedNotes[static_cast<size_t>(latchedNoteCount)] =
            HeldEntry { noteNumber, velocity, ++nextLatchedOrder };
        ++latchedNoteCount;
        resetPatternWalkState();
    }

    void Arpeggiator::clearHeld() noexcept
    {
        if (heldNoteCount > 0)
            resetPatternWalkState();
        heldNoteCount = 0;
        nextHeldOrder = 0;
    }

    void Arpeggiator::clearLatched() noexcept
    {
        if (latchedNoteCount > 0)
            resetPatternWalkState();
        latchedNoteCount = 0;
        nextLatchedOrder = 0;
    }

    void Arpeggiator::copyHeldIntoLatched() noexcept
    {
        clearLatched();
        for (int i = 0; i < heldNoteCount; ++i)
            addNoteToLatched(heldNotes[static_cast<size_t>(i)].noteNumber,
                             heldNotes[static_cast<size_t>(i)].velocity);
    }

    void Arpeggiator::emitNoteOffsForAllRinging(EngineMidiEvent* outEvents,
                                                int& outEventCount,
                                                int sampleOffset,
                                                int maxEvents) noexcept
    {
        int writeIndex = 0;
        for (int i = 0; i < ringingNoteCount; ++i)
        {
            if (outEventCount >= maxEvents)
            {
                // Cannot emit more events this block; save the rest for next time.
                ringingNotes[static_cast<size_t>(writeIndex++)] = ringingNotes[static_cast<size_t>(i)];
                continue;
            }

            EngineMidiEvent noteOff {};
            noteOff.type = EngineMidiEventType::noteOff;
            noteOff.sampleOffset = sampleOffset;
            noteOff.noteNumber = static_cast<uint8_t>(
                ringingNotes[static_cast<size_t>(i)].noteNumber);
            noteOff.fromArp = true;
            insertSortedEvent(outEvents, outEventCount, noteOff, maxEvents);
        }
        ringingNoteCount = writeIndex;
    }

    void Arpeggiator::scheduleRingingNote(int noteNumber, int samplesUntilGateOff) noexcept
    {
        if (ringingNoteCount >= maxArpRingingNotes)
        {
            // Drop oldest entry (FIFO).
            for (int i = 0; i < ringingNoteCount - 1; ++i)
                ringingNotes[static_cast<size_t>(i)] = ringingNotes[static_cast<size_t>(i + 1)];
            --ringingNoteCount;
        }

        ringingNotes[static_cast<size_t>(ringingNoteCount)] =
            RingingEntry { noteNumber, juce::jmax(0, samplesUntilGateOff) };
        ++ringingNoteCount;
    }

    int Arpeggiator::popRingingForBlock(EngineMidiEvent* outEvents,
                                        int& outEventCount,
                                        int blockSamples,
                                        int maxEvents) noexcept
    {
        int emittedCount = 0;
        int writeIndex = 0;
        for (int i = 0; i < ringingNoteCount; ++i)
        {
            auto& entry = ringingNotes[static_cast<size_t>(i)];
            if (entry.samplesUntilGateOff < blockSamples && outEventCount < maxEvents)
            {
                EngineMidiEvent noteOff {};
                noteOff.type = EngineMidiEventType::noteOff;
                noteOff.sampleOffset = juce::jmax(0, entry.samplesUntilGateOff);
                noteOff.noteNumber = static_cast<uint8_t>(entry.noteNumber);
                noteOff.fromArp = true;
                insertSortedEvent(outEvents, outEventCount, noteOff, maxEvents);
                ++emittedCount;
            }
            else
            {
                entry.samplesUntilGateOff = juce::jmax(0, entry.samplesUntilGateOff - blockSamples);
                ringingNotes[static_cast<size_t>(writeIndex++)] = entry;
            }
        }
        ringingNoteCount = writeIndex;
        return emittedCount;
    }

    int Arpeggiator::computeStepsPerBeat() const noexcept
    {
        switch (currentParameters.rate)
        {
            case coolsynth::parameters::ArpRateChoice::quarter:          return 1;
            case coolsynth::parameters::ArpRateChoice::eighth:           return 2;
            case coolsynth::parameters::ArpRateChoice::eighthTriplet:    return 3;
            case coolsynth::parameters::ArpRateChoice::sixteenth:        return 4;
            case coolsynth::parameters::ArpRateChoice::sixteenthTriplet: return 6;
            case coolsynth::parameters::ArpRateChoice::thirtySecond:     return 8;
        }
        return 4;
    }

    float Arpeggiator::computeInternalStepLengthSamples(double sampleRate) const noexcept
    {
        const auto bpm = juce::jlimit(20.0, 400.0, static_cast<double>(currentParameters.internalTempoBpm));
        const auto stepsPerBeat = juce::jmax(1, computeStepsPerBeat());
        return static_cast<float>(sampleRate * 60.0 / bpm / static_cast<double>(stepsPerBeat));
    }

    int Arpeggiator::buildOrderedWorkingSet(std::array<HeldEntry, maxArpHeldNotes>& ordered) const noexcept
    {
        const HeldEntry* workingSet = nullptr;
        int workingSetSize = 0;

        if (currentParameters.latch && latchedNoteCount > 0)
        {
            workingSet = latchedNotes.data();
            workingSetSize = latchedNoteCount;
        }
        else if (heldNoteCount > 0)
        {
            workingSet = heldNotes.data();
            workingSetSize = heldNoteCount;
        }

        if (workingSet == nullptr || workingSetSize <= 0)
            return 0;

        for (int i = 0; i < workingSetSize; ++i)
            ordered[static_cast<size_t>(i)] = workingSet[static_cast<size_t>(i)];

        if (usesPlayOrder(currentParameters.pattern))
        {
            std::sort(ordered.data(),
                      ordered.data() + workingSetSize,
                      [](const HeldEntry& a, const HeldEntry& b) noexcept
                      {
                          return a.order < b.order;
                      });
        }
        else
        {
            std::sort(ordered.data(),
                      ordered.data() + workingSetSize,
                      [](const HeldEntry& a, const HeldEntry& b) noexcept
                      {
                          return a.noteNumber < b.noteNumber;
                      });
        }

        return workingSetSize;
    }

    void Arpeggiator::resetPatternWalkState() noexcept
    {
        emittedStepCounter = 0;
        randomWalkIndex = 0;
    }

    void Arpeggiator::updateEuclideanCycle() noexcept
    {
        euclideanStepCount = juce::jlimit(1, 16, currentParameters.euclideanSteps);
        euclideanCycle = makeEuclideanCycle(currentParameters.euclideanPulses,
                                            euclideanStepCount,
                                            currentParameters.euclideanRotation);
        euclideanPosition = positiveModulo(euclideanPosition, euclideanStepCount);
    }

    bool Arpeggiator::shouldEmitEuclideanStep(int hostStepIndex) noexcept
    {
        if (currentParameters.rhythm != coolsynth::parameters::ArpRhythmChoice::euclidean)
            return true;

        if (euclideanStepCount <= 0)
            return false;

        int cycleIndex = 0;
        if (useHostSync(currentTransport))
        {
            const int slotsPerBar = juce::jmax(1, computeStepsPerBeat() * stepsPerBar);
            const int slotInBar = positiveModulo(hostStepIndex, slotsPerBar);
            cycleIndex = slotInBar % euclideanStepCount;
        }
        else
        {
            cycleIndex = euclideanPosition;
            euclideanPosition = (euclideanPosition + 1) % euclideanStepCount;
        }

        return euclideanCycle[static_cast<size_t>(cycleIndex)];
    }

    int Arpeggiator::resolveRatchetCount() const noexcept
    {
        switch (currentParameters.ratchetCount)
        {
            case coolsynth::parameters::ArpRatchetChoice::x2: return 2;
            case coolsynth::parameters::ArpRatchetChoice::x3: return 3;
            case coolsynth::parameters::ArpRatchetChoice::x4: return 4;
            case coolsynth::parameters::ArpRatchetChoice::off:
            default: return 1;
        }
    }

    int Arpeggiator::resolveAccentEvery() const noexcept
    {
        switch (currentParameters.accentEvery)
        {
            case coolsynth::parameters::ArpAccentEveryChoice::every2: return 2;
            case coolsynth::parameters::ArpAccentEveryChoice::every3: return 3;
            case coolsynth::parameters::ArpAccentEveryChoice::every4: return 4;
            case coolsynth::parameters::ArpAccentEveryChoice::off:
            default: return 0;
        }
    }

    bool Arpeggiator::emitStepNotes(const std::array<int, maxArpHeldNotes>& notes,
                                    const std::array<float, maxArpHeldNotes>& velocities,
                                    int noteCount,
                                    int eventOffset,
                                    int gateLengthSamples,
                                    int ratchetCount,
                                    float velocityScale,
                                    EngineMidiEvent* outEvents,
                                    int& outEventCount,
                                    int blockSamples,
                                    int maxEvents) noexcept
    {
        if (noteCount <= 0 || ratchetCount <= 0)
            return false;

        const int clampedRatchetCount = juce::jmax(1, ratchetCount);
        const int subHitLengthSamples = juce::jmax(1, gateLengthSamples / clampedRatchetCount);

        int requiredEventCount = 0;
        int requiredRingingCount = 0;

        for (int hitIndex = 0; hitIndex < clampedRatchetCount; ++hitIndex)
        {
            const int subHitOffset = eventOffset
                + static_cast<int>((static_cast<int64_t>(hitIndex) * gateLengthSamples)
                                   / clampedRatchetCount);
            const int absoluteGateOffSample = subHitOffset + subHitLengthSamples;
            const bool gateFitsInBlock = absoluteGateOffSample < blockSamples;

            requiredEventCount += noteCount * (gateFitsInBlock ? 2 : 1);
            if (! gateFitsInBlock)
                requiredRingingCount += noteCount;
        }

        if ((outEventCount + requiredEventCount) > maxEvents
            || (ringingNoteCount + requiredRingingCount) > maxArpRingingNotes)
        {
            return false;
        }

        for (int hitIndex = 0; hitIndex < clampedRatchetCount; ++hitIndex)
        {
            const int subHitOffset = eventOffset
                + static_cast<int>((static_cast<int64_t>(hitIndex) * gateLengthSamples)
                                   / clampedRatchetCount);
            const int absoluteGateOffSample = subHitOffset + subHitLengthSamples;

            for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
            {
                EngineMidiEvent noteOn {};
                noteOn.type = EngineMidiEventType::noteOn;
                noteOn.sampleOffset = subHitOffset;
                noteOn.noteNumber = static_cast<uint8_t>(notes[static_cast<size_t>(noteIndex)]);
                noteOn.value = juce::jlimit(0.0f,
                                            1.0f,
                                            velocities[static_cast<size_t>(noteIndex)] * velocityScale);
                noteOn.fromArp = true;
                insertSortedEvent(outEvents, outEventCount, noteOn, maxEvents);
            }

            for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
            {
                const int noteNumber = notes[static_cast<size_t>(noteIndex)];
                if (absoluteGateOffSample < blockSamples)
                {
                    EngineMidiEvent noteOff {};
                    noteOff.type = EngineMidiEventType::noteOff;
                    noteOff.sampleOffset = absoluteGateOffSample;
                    noteOff.noteNumber = static_cast<uint8_t>(noteNumber);
                    noteOff.fromArp = true;
                    insertSortedEvent(outEvents, outEventCount, noteOff, maxEvents);
                }
                else
                {
                    scheduleRingingNote(noteNumber,
                                        juce::jmax(0,
                                                   absoluteGateOffSample - blockSamples));
                }
            }
        }

        return true;
    }

    int Arpeggiator::pickNextPatternNote(float& outVelocity) noexcept
    {
        std::array<HeldEntry, maxArpHeldNotes> ordered {};
        const int workingSetSize = buildOrderedWorkingSet(ordered);
        if (workingSetSize <= 0)
            return -1;

        const auto pattern = currentParameters.pattern;
        const int octaveRange = juce::jlimit(1, 3, currentParameters.octaveRange);

        const int notesPerOctaveSweep = workingSetSize;
        const int totalSweepLength = notesPerOctaveSweep * octaveRange;
        if (totalSweepLength <= 0)
            return -1;

        int linearIndex = 0;
        int octaveShift = 0;

        switch (pattern)
        {
            case coolsynth::parameters::ArpPatternChoice::up:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                octaveShift = stepIndex / notesPerOctaveSweep;
                linearIndex = stepIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::down:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                octaveShift = (octaveRange - 1) - (stepIndex / notesPerOctaveSweep);
                const int inOctaveIndex = stepIndex % notesPerOctaveSweep;
                linearIndex = (notesPerOctaveSweep - 1) - inOctaveIndex;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::upDown:
            {
                // Length without endpoint repeats: 2 * total - 2 when total > 1,
                // else 1.
                const int reflectedLength = totalSweepLength > 1
                    ? (2 * totalSweepLength - 2)
                    : 1;
                const int reflectIndex = patternStepCounter % reflectedLength;
                int sweepIndex = reflectIndex;
                if (reflectIndex >= totalSweepLength)
                    sweepIndex = (2 * totalSweepLength - 2) - reflectIndex;

                octaveShift = sweepIndex / notesPerOctaveSweep;
                linearIndex = sweepIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::asPlayed:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                octaveShift = stepIndex / notesPerOctaveSweep;
                linearIndex = stepIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::converge:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                const int sweepIndex = convergeIndex(stepIndex, totalSweepLength);
                octaveShift = sweepIndex / notesPerOctaveSweep;
                linearIndex = sweepIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::diverge:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                const int sweepIndex = divergeIndex(stepIndex, totalSweepLength);
                octaveShift = sweepIndex / notesPerOctaveSweep;
                linearIndex = sweepIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::inside:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                octaveShift = stepIndex / notesPerOctaveSweep;
                linearIndex = convergeIndex(stepIndex % notesPerOctaveSweep,
                                            notesPerOctaveSweep);
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::outside:
            {
                const int stepIndex = patternStepCounter % totalSweepLength;
                octaveShift = stepIndex / notesPerOctaveSweep;
                linearIndex = (notesPerOctaveSweep - 1)
                    - convergeIndex(stepIndex % notesPerOctaveSweep,
                                    notesPerOctaveSweep);
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::random:
            {
                octaveShift = octaveRange > 1 ? rng.nextInt(octaveRange) : 0;
                linearIndex = rng.nextInt(notesPerOctaveSweep);
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::randomWalk:
            {
                if (totalSweepLength <= 1)
                {
                    randomWalkIndex = 0;
                }
                else
                {
                    const float roll = rng.nextFloat();
                    int nextIndex = randomWalkIndex;

                    if (roll < 0.50f)
                        ++nextIndex;
                    else if (roll < 0.75f)
                        --nextIndex;

                    if (nextIndex < 0)
                        nextIndex = 1;
                    else if (nextIndex >= totalSweepLength)
                        nextIndex = totalSweepLength - 2;

                    randomWalkIndex = juce::jlimit(0, totalSweepLength - 1, nextIndex);
                }

                octaveShift = randomWalkIndex / notesPerOctaveSweep;
                linearIndex = randomWalkIndex % notesPerOctaveSweep;
                break;
            }
            case coolsynth::parameters::ArpPatternChoice::chord:
                return -1;
        }

        ++patternStepCounter;

        const auto& entry = ordered[static_cast<size_t>(juce::jlimit(0, notesPerOctaveSweep - 1, linearIndex))];
        const int shiftedNote = entry.noteNumber + 12 * octaveShift;
        if (shiftedNote < 0 || shiftedNote > 127)
            return -1;

        outVelocity = entry.velocity;
        return shiftedNote;
    }
}
