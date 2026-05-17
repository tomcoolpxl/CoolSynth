#pragma once

#include <array>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "SynthParameters.h"

namespace coolsynth::synth
{
    struct EngineTransportInfo
    {
        bool hostHasTempo = false;
        bool hostHasPpq = false;
        bool hostIsPlaying = false;
        double hostBpm = 120.0;
        double hostPpqAtBlockStart = 0.0;
    };

    inline constexpr int maxArpHeldNotes = 16;
    inline constexpr int maxArpLatchedNotes = 16;
    inline constexpr int maxArpRingingNotes = 32;
    inline constexpr int maxArpEventsPerBlock = 64;

    class Arpeggiator final
    {
    public:
        Arpeggiator() noexcept;

        void prepare(double sampleRate) noexcept;
        void panic() noexcept;
        void setSeedForTesting(uint64_t seed) noexcept;

        void setParameters(const ArpParametersV2& parameters) noexcept;
        void setTransportInfo(const EngineTransportInfo& transport) noexcept;

        void onNoteOn(int noteNumber, float velocity) noexcept;
        void onNoteOff(int noteNumber) noexcept;
        void onAllNotesOff() noexcept;

        [[nodiscard]] bool isEnabled() const noexcept { return currentParameters.enabled; }

        // Writes up to maxEvents events into outEvents (already sorted by sampleOffset)
        // and advances internal clock by blockSamples. Returns the number written.
        int generateEventsForBlock(int blockSamples,
                                   double currentSampleRate,
                                   EngineMidiEvent* outEvents,
                                   int maxEvents) noexcept;

        // Test accessors
        [[nodiscard]] int getHeldNoteCountForTesting() const noexcept { return heldNoteCount; }
        [[nodiscard]] int getLatchedNoteCountForTesting() const noexcept { return latchedNoteCount; }
        [[nodiscard]] int getRingingNoteCountForTesting() const noexcept { return ringingNoteCount; }

    private:
        struct HeldEntry
        {
            int noteNumber = -1;
            float velocity = 0.0f;
            uint64_t order = 0;
        };

        struct RingingEntry
        {
            int noteNumber = -1;
            int samplesUntilGateOff = 0;
        };

        void addNoteToHeld(int noteNumber, float velocity) noexcept;
        void removeNoteFromHeld(int noteNumber) noexcept;
        void addNoteToLatched(int noteNumber, float velocity) noexcept;
        void clearHeld() noexcept;
        void clearLatched() noexcept;
        void copyHeldIntoLatched() noexcept;

        void emitNoteOffsForAllRinging(EngineMidiEvent* outEvents,
                                       int& outEventCount,
                                       int sampleOffset,
                                       int maxEvents) noexcept;
        void scheduleRingingNote(int noteNumber, int samplesUntilGateOff) noexcept;
        int popRingingForBlock(EngineMidiEvent* outEvents,
                               int& outEventCount,
                               int blockSamples,
                               int maxEvents) noexcept;

        int computeStepsPerBeat() const noexcept;
        float computeInternalStepLengthSamples(double sampleRate) const noexcept;
        int buildOrderedWorkingSet(std::array<HeldEntry, maxArpHeldNotes>& ordered) const noexcept;
        void resetPatternWalkState() noexcept;

        // Returns next pattern note number with octave shift applied, or -1 if working set empty.
        // Advances patternStepCounter on success.
        int pickNextPatternNote(float& outVelocity) noexcept;

        ArpParametersV2 currentParameters {};
        EngineTransportInfo currentTransport {};

        std::array<HeldEntry, maxArpHeldNotes> heldNotes {};
        int heldNoteCount = 0;
        uint64_t nextHeldOrder = 0;

        std::array<HeldEntry, maxArpLatchedNotes> latchedNotes {};
        int latchedNoteCount = 0;
        uint64_t nextLatchedOrder = 0;

        std::array<RingingEntry, maxArpRingingNotes> ringingNotes {};
        int ringingNoteCount = 0;

        double preparedSampleRate = 48000.0;

        // Internal-rate clock state (samples until next step).
        float samplesUntilNextStep = 0.0f;
        bool internalClockArmed = false;

        // Host-sync state from previous block, used to detect transitions.
        bool previousHostIsPlaying = false;
        bool previousEnabled = false;

        // Pattern walker state.
        int patternStepCounter = 0;
        int randomWalkIndex = 0;
        juce::Random rng;
    };
}
