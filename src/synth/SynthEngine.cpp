#include "SynthEngine.h"
#include "SynthSound.h"

#include <algorithm>
#include <vector>

namespace coolsynth::synth
{
    // --- ReleaseFirstSynthesiser ---

    juce::SynthesiserVoice* ReleaseFirstSynthesiser::findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                                                    int /*midiChannel*/,
                                                                    int /*midiNoteNumber*/) const
    {
        std::vector<juce::SynthesiserVoice*> candidateVoices;
        for (auto* voice : voices)
        {
            if (voice->canPlaySound(soundToPlay))
                candidateVoices.push_back(voice);
        }

        if (candidateVoices.empty())
            return nullptr;

        // Sort by age: oldest first
        std::sort(candidateVoices.begin(), candidateVoices.end(), [](auto* a, auto* b) {
            return a->wasStartedBefore(*b);
        });

        // Pass 1: oldest voice that is in release
        for (auto* voice : candidateVoices)
        {
            if (voice->isPlayingButReleased())
                return voice;
        }

        // Pass 2: oldest active voice
        return candidateVoices.front();
    }

    // --- SynthEngine ---

    SynthEngine::SynthEngine()
    {
        synthesiser.setNoteStealingEnabled(true);
        synthesiser.addSound(new SynthSound());

        for (int i = 0; i < defaultVoiceCount; ++i)
        {
            auto* voice = new SynthVoice();
            synthVoices.push_back(voice);
            synthesiser.addVoice(voice);
        }
    }

    void SynthEngine::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        outputChannels = outputChannelCount;
        synthesiser.setCurrentPlaybackSampleRate(sampleRate);
        masterGainLinear.reset(sampleRate, masterGainRampSeconds);
        
        prepareVoices(sampleRate, samplesPerBlock);
        globalDelay.prepare(sampleRate, samplesPerBlock, outputChannelCount);
        
        prepared = true;
    }

    void SynthEngine::releaseResources() noexcept
    {
        globalDelay.reset();
        prepared = false;
    }

    void SynthEngine::render(juce::AudioBuffer<float>& outputBuffer,
                            juce::MidiBuffer& midiMessages,
                            const BlockRenderParameters& parameters)
    {
        if (!prepared)
            return;

        pushEnvelopeParametersToVoices(parameters.ampEnvelope);
        pushFilterParametersToVoices(parameters.filter);
        pushWaveformToVoices(parameters.waveform);
        
        synthesiser.renderNextBlock(outputBuffer, midiMessages, 0, outputBuffer.getNumSamples());
        
        globalDelay.process(outputBuffer, parameters.delay);
        
        applyMasterGain(outputBuffer, parameters.masterGainLinear);
    }

    void SynthEngine::panic() noexcept
    {
        synthesiser.allNotesOff(0, false);
    }

    void SynthEngine::prepareVoices(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1; // Voices render mono

        for (auto* voice : synthVoices)
        {
            voice->prepare(spec);
        }
    }

    void SynthEngine::pushEnvelopeParametersToVoices(const EnvelopeParameters& parameters) noexcept
    {
        for (auto* voice : synthVoices)
        {
            voice->setNextEnvelopeParameters(parameters);
        }
    }

    void SynthEngine::pushFilterParametersToVoices(const FilterParameters& parameters) noexcept
    {
        for (auto* voice : synthVoices)
        {
            voice->setNextFilterParameters(parameters);
        }
    }

    void SynthEngine::pushWaveformToVoices(coolsynth::parameters::WaveformChoice waveform) noexcept
    {
        for (auto* voice : synthVoices)
        {
            voice->setWaveform(waveform);
        }
    }

    void SynthEngine::applyMasterGain(juce::AudioBuffer<float>& outputBuffer, float targetLinearGain) noexcept
    {
        masterGainLinear.setTargetValue(targetLinearGain);
        masterGainLinear.applyGain(outputBuffer, outputBuffer.getNumSamples());
    }
}
