#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "UiPalette.h"
#include "parameters/ParameterIDs.h"
#include "plugin/ProcessorScopeFifo.h"

namespace coolsynth::ui
{
    /**
     * A 7-pane Visual Laboratory showing Signal Flow with labels beneath each pane:
     * [LFO] [FLT ENV] [AMP ENV] [SOURCE] [FILTER] [REALITY] [SPECTRA]
     */
    class SignalChainVisualizer final : public juce::Component,
                                        private juce::Timer
    {
    public:
        SignalChainVisualizer(juce::AudioProcessorValueTreeState& apvts,
                              coolsynth::plugin::ProcessorScopeFifo& fifo);
        ~SignalChainVisualizer() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;

        void pullFromProcessor();
        void updateIdealWaveforms();

        void drawWaveform(juce::Graphics& g,
                          juce::Rectangle<int> area,
                          const juce::Path& path,
                          juce::Colour colour,
                          const juce::String& label);

        [[nodiscard]] float paneWidth() const noexcept;
        [[nodiscard]] float paneWaveHeight() const noexcept;

        juce::AudioProcessorValueTreeState& state;
        coolsynth::plugin::ProcessorScopeFifo& scopeFifo;

        // Waveform data
        juce::Path sourcePath;
        juce::Path filterPath;
        juce::Path realityPath;
        juce::Path spectraPath;

        // Modulation micro-paths
        juce::Path lfoPath;
        juce::Path filterEnvPath;
        juce::Path ampEnvPath;

        // FFT for Spectra pane — all UI-thread-only, no atomics or statics
        static constexpr int fftOrder = 10;
        static constexpr int fftSize = 1 << fftOrder;
        juce::dsp::FFT forwardFFT { fftOrder };
        juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };

        std::vector<float> fftScratch;
        int fftWriteIdx = 0;

        // Buffer pulled from the processor FIFO each timer tick
        std::vector<float> outputBuffer;

        uint32_t frameCount { 0 };
        juce::Random random;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainVisualizer)
    };
}
