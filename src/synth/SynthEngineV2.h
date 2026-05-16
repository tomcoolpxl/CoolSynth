#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Arpeggiator.h"
#include "GlobalFxRack.h"
#include "SynthParameters.h"
#include "SynthVoice.h"

namespace coolsynth::synth
{
    struct VoiceDebugState
    {
        bool active = false;
        bool keyDown = false;
        bool sustained = false;
        bool releasing = false;
        int noteNumber = -1;
        uint64_t startOrder = 0;
    };

    inline constexpr int maxHeldNotes = 16;

    class SynthEngineV2 final
    {
    public:
        explicit SynthEngineV2(int voiceCount = defaultVoiceCount);

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    std::span<const EngineMidiEvent> midiEvents,
                    const BlockRenderParametersV2& parameters,
                    const EngineTransportInfo& transport = {});
        void panic() noexcept;

        [[nodiscard]] int getActiveVoiceCountForTesting() const noexcept;
        void copyVoiceStatesForTesting(std::span<VoiceDebugState> destination) const noexcept;
        [[nodiscard]] int getArpHeldNoteCountForTesting() const noexcept
        {
            return arpeggiator.getHeldNoteCountForTesting();
        }
        [[nodiscard]] int getArpLatchedNoteCountForTesting() const noexcept
        {
            return arpeggiator.getLatchedNoteCountForTesting();
        }
        [[nodiscard]] int getArpRingingNoteCountForTesting() const noexcept
        {
            return arpeggiator.getRingingNoteCountForTesting();
        }
        [[nodiscard]] int getLastPlayedNoteForTesting() const noexcept
        {
            return lastPlayedNote;
        }

    private:
        struct VoiceSlot
        {
            SynthVoice voice;
            int noteNumber = -1;
            bool keyDown = false;
            bool sustained = false;
            uint64_t startOrder = 0;
        };

        struct HeldNote
        {
            int noteNumber = -1;
            float velocity = 0.0f;
            uint64_t order = 0;
        };

        void prepareVoices(double sampleRate, int samplesPerBlock);
        void applyVoiceParametersForSpan(const BlockRenderParametersV2& parameters,
                                         float lfoPhase) noexcept;
        void renderVoiceSpan(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples,
                             const BlockRenderParametersV2& parameters,
                             float lfoPhase) noexcept;
        void handleEvent(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleNoteOn(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleNoteOff(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handlePitchBend(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleModWheel(const EngineMidiEvent& event) noexcept;
        void handleSustainPedal(const EngineMidiEvent& event) noexcept;
        void handleAllNotesOff() noexcept;
        void handleAllSoundOff() noexcept;
        void handleResetControllers() noexcept;
        void releaseSustainedVoices() noexcept;

        void allocatePolyNote(int noteNumber,
                              float velocity,
                              const BlockRenderParametersV2& parameters) noexcept;
        void allocateMonoNote(int noteNumber,
                              float velocity,
                              const BlockRenderParametersV2& parameters) noexcept;
        void allocateUnisonNote(int noteNumber,
                                float velocity,
                                const BlockRenderParametersV2& parameters) noexcept;
        void releaseMonoOrUnisonVoices() noexcept;
        void retriggerMonoFromHeldNotes(const BlockRenderParametersV2& parameters) noexcept;

        int findVoiceIndexToAllocate() const noexcept;
        void clearVoiceSlot(VoiceSlot& slot) noexcept;
        void applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept;

        void addHeldNote(int noteNumber, float velocity) noexcept;
        void removeHeldNote(int noteNumber) noexcept;
        void clearHeldNotes() noexcept;
        int pickHeldNoteByPriority(coolsynth::parameters::KeyPriorityChoice priority) const noexcept;

        void assignVoicePanForIndex(VoiceSlot& slot,
                                    int voiceIndex,
                                    int totalVoices,
                                    float panSpread) noexcept;
        void assignVoiceVintageForIndex(VoiceSlot& slot,
                                        int voiceIndex,
                                        float vintageAmount) noexcept;

        static float computeLfoPhaseIncrementPerSample(float rateHz, double sampleRate) noexcept;

        std::vector<VoiceSlot> voices;
        Arpeggiator arpeggiator;
        GlobalFxRack globalFxRack;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainLinear;
        std::array<HeldNote, maxHeldNotes> heldNotes {};
        int heldNoteCount = 0;
        uint64_t nextHeldOrder = 0;
        int lastPlayedNote = -1;
        coolsynth::parameters::PlayModeChoice currentPlayMode = coolsynth::parameters::PlayModeChoice::poly;
        double currentSampleRate = 0.0;
        int outputChannels = 0;
        uint64_t nextVoiceStartOrder = 0;
        float pitchBendSemitones = 0.0f;
        float modWheelValue = 0.0f;
        float globalLfoPhase = 0.0f;
        bool sustainPedalDown = false;
        bool prepared = false;
    };
}
