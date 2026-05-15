#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UiPalette.h"
#include "parameters/ParameterIDs.h"

namespace coolsynth::ui
{
    /**
     * A triple-pane visualizer showing Source, Filter, and Output waveforms.
     */
    class SignalChainVisualizer final : public juce::Component,
                                        private juce::Timer
    {
    public:
        SignalChainVisualizer(juce::AudioProcessorValueTreeState& apvts);
        ~SignalChainVisualizer() override;

        /** Pushes new audio samples from the processor into the visualizer FIFO. */
        void pushSamples(const float* samples, int numSamples) noexcept;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;

        /** Recalculates the idealized source and filtered waveforms. */
        void updateIdealWaveforms();

        /** Draws a waveform within a specific area. */
        void drawWaveform(juce::Graphics& g, 
                          juce::Rectangle<int> area, 
                          const juce::Path& path, 
                          juce::Colour colour, 
                          const juce::String& label);

        juce::AudioProcessorValueTreeState& state;

        // Waveform data
        juce::Path sourcePath;
        juce::Path filterPath;
        juce::Path outputPath;

        // FIFO for real-time output
        static constexpr int fifoSize = 2048;
        float fifo[fifoSize] {};
        std::atomic<int> writeIndex { 0 };
        std::atomic<int> readIndex { 0 };

        // Local buffer for drawing output
        std::vector<float> outputBuffer;

        uint32_t frameCount { 0 };
        juce::Random random;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainVisualizer)
    };
}
