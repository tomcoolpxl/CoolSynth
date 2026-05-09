#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "synth/SynthEngine.h"
#include "midi/MidiMappingEngine.h"

class SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SynthAudioProcessor();
    ~SynthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CoolSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::unique_ptr<juce::XmlElement> createParameterStateXml();
    bool applyParameterStateXml(const juce::XmlElement& stateXml);
    void resetAutomatableParametersToDefaults();
    juce::String getParameterStateTypeName() const;

    void setLearnedMidiBindings(std::span<const coolsynth::midi::LearnedCcBinding> bindings);
    void clearLearnedMidiBinding(juce::StringRef parameterId);

    void handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event);
    void requestPanic() noexcept;

    APVTS& getValueTreeState() noexcept { return parameters; }
    const APVTS& getValueTreeState() const noexcept { return parameters; }

private:
    void applyMappedAction(const coolsynth::midi::MappedAction& action);
    void applyNormalizedParameterValue(juce::RangedAudioParameter& parameter,
                                      float normalizedValue);
    void applyParameterChange(const coolsynth::midi::MappedParameterChange& change);
    void applyMappedCommand(coolsynth::midi::MappedCommand command) noexcept;

    coolsynth::synth::BlockRenderParameters makeBlockRenderParameters() const noexcept;
    static coolsynth::synth::ParameterValuePointers bindParameterPointers(APVTS& state);

    APVTS parameters;
    coolsynth::midi::MidiMappingEngine midiMappingEngine;
    coolsynth::synth::ParameterValuePointers parameterValues;
    coolsynth::synth::SynthEngine synthEngine;
    std::atomic<bool> panicRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
