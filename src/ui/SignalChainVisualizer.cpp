#include "SignalChainVisualizer.h"
#include <cmath>

namespace coolsynth::ui
{
    namespace
    {
        float renderIdealSample(float phase, coolsynth::parameters::OscillatorWaveShape shape, float pw)
        {
            switch (shape)
            {
                case coolsynth::parameters::OscillatorWaveShape::pulse:
                    return (phase < pw) ? 0.5f : -0.5f;
                case coolsynth::parameters::OscillatorWaveShape::triangle:
                    return (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
                case coolsynth::parameters::OscillatorWaveShape::saw:
                    return 2.0f * phase - 1.0f;
                case coolsynth::parameters::OscillatorWaveShape::sine:
                    return std::sin(phase * juce::MathConstants<float>::twoPi);
            }
            return 0.0f;
        }
    }

    SignalChainVisualizer::SignalChainVisualizer(juce::AudioProcessorValueTreeState& apvts)
        : state(apvts)
    {
        // Larger buffer (2048) to see bass note cycles clearly
        outputBuffer.resize(1024, 0.0f);
        startTimerHz(30);
    }

    SignalChainVisualizer::~SignalChainVisualizer()
    {
        stopTimer();
    }

    void SignalChainVisualizer::pushSamples(const float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            int idx = writeIndex.load();
            fifo[idx] = samples[i];
            writeIndex.store((idx + 1) % fifoSize);
        }
    }

    void SignalChainVisualizer::paint(juce::Graphics& g)
    {
        auto area = getLocalBounds();
        auto panes = area.reduced(2);
        
        const int paneW = 120;
        const int gap = 10;
        
        auto sourceArea = panes.removeFromLeft(paneW);
        panes.removeFromLeft(gap);
        auto filterArea = panes.removeFromLeft(paneW);
        panes.removeFromLeft(gap);
        auto outputArea = panes.removeFromLeft(paneW);

        drawWaveform(g, sourceArea, sourcePath, palette::textPrimary, "SOURCE");
        drawWaveform(g, filterArea, filterPath, palette::ledGreen, "FILTER");
        drawWaveform(g, outputArea, outputPath, palette::learnYellow, "OUTPUT");
    }

    void SignalChainVisualizer::resized()
    {
        updateIdealWaveforms();
    }

    void SignalChainVisualizer::timerCallback()
    {
        frameCount++;
        updateIdealWaveforms();

        // Update Output Path
        outputPath.clear();
        
        int currentWrite = writeIndex.load();
        int readStart = (currentWrite - outputBuffer.size() * 3 + fifoSize) % fifoSize;
        
        int triggerIdx = -1;
        float maxAbsVal = 0.01f;
        for (int i = 0; i < (int)outputBuffer.size() * 2; ++i)
        {
            int idx1 = (readStart + i) % fifoSize;
            int idx2 = (idx1 + 1) % fifoSize;
            float val = std::abs(fifo[idx1]);
            if (val > maxAbsVal) maxAbsVal = val;

            if (triggerIdx == -1 && fifo[idx1] <= 0.0f && fifo[idx2] > 0.0f)
            {
                triggerIdx = idx2;
            }
        }

        if (triggerIdx == -1) triggerIdx = readStart;

        // Dynamic Zoom: Auto-scale if signal is very quiet, but cap it
        float zoom = 1.0f / std::max(0.1f, maxAbsVal);
        zoom = std::min(zoom, 10.0f); // Don't zoom forever on noise

        const float h = getHeight();
        const float halfH = h * 0.5f;
        const float w = 120.0f;
        const float stepX = w / static_cast<float>(outputBuffer.size());

        for (int i = 0; i < (int)outputBuffer.size(); ++i)
        {
            float val = fifo[(triggerIdx + i) % fifoSize];
            float x = i * stepX;
            // Apply dynamic zoom + base scaling
            float y = halfH - val * halfH * 0.8f * zoom;
            
            if (i == 0) outputPath.startNewSubPath(x, y);
            else outputPath.lineTo(x, y);
        }

        repaint();
    }

    void SignalChainVisualizer::updateIdealWaveforms()
    {
        using namespace coolsynth::parameters;
        
        const auto waveA = static_cast<OscillatorWaveShape>(static_cast<int>(state.getRawParameterValue(ids::oscAWave)->load()));
        const auto pwA = state.getRawParameterValue(ids::oscAPulseWidth)->load();
        const auto levelA = state.getRawParameterValue(ids::oscALevel)->load();

        const auto waveB = static_cast<OscillatorWaveShape>(static_cast<int>(state.getRawParameterValue(ids::oscBWave)->load()));
        const auto pwB = state.getRawParameterValue(ids::oscBPulseWidth)->load();
        const auto levelB = state.getRawParameterValue(ids::oscBLevel)->load();
        const auto detuneB = state.getRawParameterValue(ids::oscBFineCents)->load() / 100.0f;

        const auto noiseLevel = state.getRawParameterValue(ids::noiseLevel)->load();

        const auto cutoff = state.getRawParameterValue(ids::filterCutoffHz)->load();
        const auto res = state.getRawParameterValue(ids::filterResonance)->load();

        sourcePath.clear();
        filterPath.clear();

        const int numPoints = 256;
        const float numCycles = 2.0f;
        const float w = 120.0f;
        const float h = getHeight();
        const float halfH = h * 0.5f;
        const float stepX = w / static_cast<float>(numPoints);

        float drift = std::sin(frameCount * 0.05f) * 0.01f;

        float lastFiltered = 0.0f;
        float alpha = std::clamp(cutoff / 4000.0f, 0.01f, 1.0f);

        for (int i = 0; i < numPoints; ++i)
        {
            float normX = static_cast<float>(i) / static_cast<float>(numPoints);
            float phase = std::fmod(normX * numCycles + drift, 1.0f);
            
            float sA = renderIdealSample(phase, waveA, pwA);
            float phaseB = std::fmod(phase * (1.0f + detuneB * 0.05f), 1.0f);
            float sB = renderIdealSample(phaseB, waveB, pwB);
            
            // Add random noise component
            float noiseSample = (random.nextFloat() * 2.0f - 1.0f);

            // Match SynthVoice Mixed/Normalized logic
            float mixed = sA * levelA + sB * levelB + noiseSample * noiseLevel;
            float totalLevel = levelA + levelB + noiseLevel;
            float overloadDrive = 1.0f + std::max(0.0f, totalLevel - 1.0f) * 1.75f;
            float normalized = mixed * (totalLevel > 0.0f ? 1.0f / std::max(1.0f, totalLevel) : 0.0f);
            float sourceVal = std::tanh(normalized * overloadDrive);

            float x = i * stepX;
            
            // Source Path
            float yS = halfH - sourceVal * halfH * 0.8f;
            if (i == 0) sourcePath.startNewSubPath(x, yS);
            else sourcePath.lineTo(x, yS);

            // Filter Sim
            float filtered = lastFiltered + alpha * (sourceVal - lastFiltered);
            float resRinging = std::sin(phase * 15.0f) * res * 0.2f * (1.0f - alpha);
            lastFiltered = filtered;

            float yF = halfH - (filtered + resRinging) * halfH * 0.8f;
            if (i == 0) filterPath.startNewSubPath(x, yF);
            else filterPath.lineTo(x, yF);
        }
    }

    void SignalChainVisualizer::drawWaveform(juce::Graphics& g, 
                                              juce::Rectangle<int> area, 
                                              const juce::Path& path, 
                                              juce::Colour colour, 
                                              const juce::String& label)
    {
        g.saveState();
        
        // Background of pane
        g.setColour(palette::panelBlack);
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(palette::panelStroke.withAlpha(0.6f));
        g.drawRoundedRectangle(area.toFloat(), 3.0f, 1.0f);

        g.reduceClipRegion(area);
        
        // Label
        g.setColour(palette::textSecondary);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, area.withTrimmedBottom(2).removeFromBottom(10), juce::Justification::centred);

        // Waveform - Thicker stroke for visibility
        g.setColour(colour);
        auto p = path;
        p.applyTransform(juce::AffineTransform::translation(static_cast<float>(area.getX()), 
                                                            static_cast<float>(area.getY())));
        g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.restoreState();
    }
}
