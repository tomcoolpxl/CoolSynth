#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "UiPalette.h"
#include "parameters/ParameterIDs.h"

namespace coolsynth::ui
{
    /**
     * A 5-pane Visual Laboratory showing Signal Flow:
     * [MODS] -> [SOURCE] -> [FILTER] -> [SPECTRA] -> [REALITY]
     */
    class SignalChainVisualizer final : public juce::Component,
                                        private juce::Timer
    {
    public:
        SignalChainVisualizer(juce::AudioProcessorValueTreeState& apvts);
        ~SignalChainVisualizer() override;

        void pushSamples(const float* samples, int numSamples) noexcept;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;

        void updateIdealWaveforms();
        void updateSpectralData();

        void drawWaveform(juce::Graphics& g, 
                          juce::Rectangle<int> area, 
                          const juce::Path& path, 
                          juce::Colour colour, 
                          const juce::String& label);

        void drawModulationPane(juce::Graphics& g, juce::Rectangle<int> area);

        juce::AudioProcessorValueTreeState& state;

        // Waveform data
        juce::Path sourcePath;
        juce::Path filterPath;
        juce::Path realityPath;
        juce::Path spectraPath;

        // Modulation micro-paths
        juce::Path lfoPath;
        juce::Path filterEnvPath;
        juce::Path ampEnvPath;

        // FFT for Spectra pane
        static constexpr int fftOrder = 10;
        static constexpr int fftSize = 1 << fftOrder;
        juce::dsp::FFT forwardFFT { fftOrder };
        juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };

        float fftData[fftSize * 2] {};
        float scopeData[fftSize] {};
        std::atomic<bool> nextFFTBlockReady { false };

        // FIFO for real-time output
        static constexpr int fifoSize = 4096;
        float fifo[fifoSize] {};
        std::atomic<int> writeIndex { 0 };

        // Local buffer for drawing output
        std::vector<float> outputBuffer;

        uint32_t frameCount { 0 };
        juce::Random random;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainVisualizer)
    };
}
