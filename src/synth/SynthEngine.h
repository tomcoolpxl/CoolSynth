#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthVoice.h"
#include "SynthParameters.h"

namespace coolsynth::synth
{
    /**
     * Custom synthesizer that implements release-first then oldest-active voice stealing.
     */
    class ReleaseFirstSynthesiser final : public juce::Synthesiser
    {
    protected:
        juce::SynthesiserVoice* findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                                int midiChannel,
                                                int midiNoteNumber) const override;
    };

    inline constexpr int defaultVoiceCount = 8;
    inline constexpr double masterGainRampSeconds = 0.02;

    class SynthEngine final
    {
    public:
        SynthEngine();

        void prepare(double sampleRate, int samplesPerBlock, int outputChannelCount);
        void releaseResources() noexcept;
        void render(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiMessages,
                    const BlockRenderParameters& parameters);
        void panic() noexcept;

    private:
        void prepareVoices(double sampleRate, int samplesPerBlock);
        void pushEnvelopeParametersToVoices(const EnvelopeParameters& parameters) noexcept;
        void applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept;

        ReleaseFirstSynthesiser synthesiser;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainLinear;
        int outputChannels = 0;
        bool prepared = false;
    };
}
