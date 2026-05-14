#pragma once

#include <span>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "GlobalDelay.h"
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

    class SynthEngineV2 final
    {
    public:
        explicit SynthEngineV2(int voiceCount = defaultVoiceCount);

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    std::span<const EngineMidiEvent> midiEvents,
                    const BlockRenderParametersV2& parameters);
        void panic() noexcept;

        [[nodiscard]] int getActiveVoiceCountForTesting() const noexcept;
        void copyVoiceStatesForTesting(std::span<VoiceDebugState> destination) const noexcept;

    private:
        struct VoiceSlot
        {
            SynthVoice voice;
            int noteNumber = -1;
            bool keyDown = false;
            bool sustained = false;
            uint64_t startOrder = 0;
        };

        void prepareVoices(double sampleRate, int samplesPerBlock);
        void applyVoiceParameters(const BlockRenderParametersV2& parameters) noexcept;
        void renderVoiceSpan(juce::AudioBuffer<float>& outputBuffer,
                             int startSample,
                             int numSamples) noexcept;
        void handleEvent(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleNoteOn(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleNoteOff(const EngineMidiEvent& event) noexcept;
        void handlePitchBend(const EngineMidiEvent& event, const BlockRenderParametersV2& parameters) noexcept;
        void handleModWheel(const EngineMidiEvent& event) noexcept;
        void handleSustainPedal(const EngineMidiEvent& event) noexcept;
        void handleAllNotesOff() noexcept;
        void handleAllSoundOff() noexcept;
        void handleResetControllers() noexcept;
        void releaseSustainedVoices() noexcept;
        int findVoiceIndexToAllocate() const noexcept;
        int findVoiceIndexForNoteOff(int midiNoteNumber) const noexcept;
        void clearVoiceSlot(VoiceSlot& slot) noexcept;
        void applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept;

        static DelayParameters mapDelayParameters(const DelayParametersV2& parameters) noexcept;
        static coolsynth::parameters::WaveformChoice chooseWaveform(const BlockRenderParametersV2& parameters) noexcept;
        static float chooseOutputLevel(const BlockRenderParametersV2& parameters) noexcept;

        std::vector<VoiceSlot> voices;
        GlobalDelay globalDelay;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainLinear;
        double currentSampleRate = 0.0;
        int outputChannels = 0;
        uint64_t nextVoiceStartOrder = 0;
        float pitchBendSemitones = 0.0f;
        float modWheelValue = 0.0f;
        bool sustainPedalDown = false;
        bool prepared = false;
    };
}
