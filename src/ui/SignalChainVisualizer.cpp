#include "SignalChainVisualizer.h"
#include <cmath>
#include <algorithm>

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

        juce::Path createAdsrPath(float a, float d, float s, float r, float w, float h)
        {
            juce::Path p;
            p.startNewSubPath(0.0f, h);
            
            const float total = a + d + 0.5f + r; // 0.5 is for sustain hold visual
            const float scale = w / total;
            
            float x = a * scale;
            p.lineTo(x, 0.0f);
            
            x += d * scale;
            p.lineTo(x, h * (1.0f - s));
            
            x += 0.5f * scale;
            p.lineTo(x, h * (1.0f - s));
            
            x += r * scale;
            p.lineTo(x, h);
            
            return p;
        }
    }

    SignalChainVisualizer::SignalChainVisualizer(juce::AudioProcessorValueTreeState& apvts)
        : state(apvts)
    {
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
            const float val = samples[i];
            
            // Scope FIFO
            int idx = writeIndex.load();
            fifo[idx] = val;
            writeIndex.store((idx + 1) % fifoSize);

            // FFT buffer (simple non-overlapping for now)
            static int fftWriteIdx = 0;
            if (!nextFFTBlockReady.load())
            {
                fftData[fftWriteIdx++] = val;
                if (fftWriteIdx >= fftSize)
                {
                    fftWriteIdx = 0;
                    nextFFTBlockReady.store(true);
                }
            }
        }
    }

    void SignalChainVisualizer::paint(juce::Graphics& g)
    {
        auto area = getLocalBounds().reduced(2);
        
        // 5 Panes: MODS, SOURCE, FILTER, SPECTRA, REALITY
        const int gap = 8;
        const int paneW = (area.getWidth() - (gap * 4)) / 5;
        
        auto modsArea = area.removeFromLeft(paneW);
        area.removeFromLeft(gap);
        auto sourceArea = area.removeFromLeft(paneW);
        area.removeFromLeft(gap);
        auto filterArea = area.removeFromLeft(paneW);
        area.removeFromLeft(gap);
        auto realityArea = area.removeFromLeft(paneW);
        area.removeFromLeft(gap);
        auto spectraArea = area;

        drawModulationPane(g, modsArea);
        drawWaveform(g, sourceArea, sourcePath, palette::textPrimary, "SOURCE");
        drawWaveform(g, filterArea, filterPath, palette::ledGreen, "FILTER");
        drawWaveform(g, realityArea, realityPath, palette::learnYellow, "REALITY");
        drawWaveform(g, spectraArea, spectraPath, palette::ledGreen, "SPECTRA");
    }

    void SignalChainVisualizer::resized()
    {
        updateIdealWaveforms();
    }

    void SignalChainVisualizer::timerCallback()
    {
        frameCount++;
        updateIdealWaveforms();
        updateSpectralData();

        // Update Reality Path
        realityPath.clear();
        
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

        float zoom = 1.0f / std::max(0.1f, maxAbsVal);
        zoom = std::min(zoom, 10.0f);

        const float halfH = getHeight() * 0.5f;
        const float w = (getWidth() - 40) / 5.0f; // Approximate
        const float stepX = w / static_cast<float>(outputBuffer.size());

        for (int i = 0; i < (int)outputBuffer.size(); ++i)
        {
            float val = fifo[(triggerIdx + i) % fifoSize];
            float x = i * stepX;
            float y = halfH - val * halfH * 0.8f * zoom;
            
            if (i == 0) realityPath.startNewSubPath(x, y);
            else realityPath.lineTo(x, y);
        }

        repaint();
    }

    void SignalChainVisualizer::updateSpectralData()
    {
        if (nextFFTBlockReady.load())
        {
            window.multiplyWithWindowingTable(fftData, fftSize);
            forwardFFT.performFrequencyOnlyForwardTransform(fftData);

            spectraPath.clear();
            const float w = (getWidth() - 40) / 5.0f;
            const float h = getHeight();
            const int numBins = fftSize / 2;
            const float stepX = w / std::log10(static_cast<float>(numBins));

            for (int i = 1; i < numBins; ++i)
            {
                // Logarithmic frequency scale for better visual balance
                float x = std::log10(static_cast<float>(i)) * stepX;
                float level = juce::Decibels::gainToDecibels(fftData[i]);
                // Scale dB to visual height (roughly -60dB to 0dB range)
                float y = juce::jlimit(0.0f, h, h * (1.0f - (level + 60.0f) / 60.0f));

                if (i == 1) spectraPath.startNewSubPath(x, y);
                else spectraPath.lineTo(x, y);
            }

            nextFFTBlockReady.store(false);
        }
    }

    void SignalChainVisualizer::updateIdealWaveforms()
    {
        using namespace coolsynth::parameters;
        
        // Osc A/B
        const auto waveA = static_cast<OscillatorWaveShape>(static_cast<int>(state.getRawParameterValue(ids::oscAWave)->load()));
        const auto pwA = state.getRawParameterValue(ids::oscAPulseWidth)->load();
        const auto levelA = state.getRawParameterValue(ids::oscALevel)->load();
        const auto waveB = static_cast<OscillatorWaveShape>(static_cast<int>(state.getRawParameterValue(ids::oscBWave)->load()));
        const auto pwB = state.getRawParameterValue(ids::oscBPulseWidth)->load();
        const auto levelB = state.getRawParameterValue(ids::oscBLevel)->load();
        const auto detuneB = state.getRawParameterValue(ids::oscBFineCents)->load() / 100.0f;
        const auto noiseLevel = state.getRawParameterValue(ids::noiseLevel)->load();

        // Filter
        const auto cutoff = state.getRawParameterValue(ids::filterCutoffHz)->load();
        const auto res = state.getRawParameterValue(ids::filterResonance)->load();

        // Mods
        const auto lfoWave = static_cast<LfoWaveShape>(static_cast<int>(state.getRawParameterValue(ids::lfoWave)->load()));
        
        const auto fA = state.getRawParameterValue(ids::filterAttackMs)->load() / 1000.0f;
        const auto fD = state.getRawParameterValue(ids::filterDecayMs)->load() / 1000.0f;
        const auto fS = state.getRawParameterValue(ids::filterSustain)->load();
        const auto fR = state.getRawParameterValue(ids::filterReleaseMs)->load() / 1000.0f;

        const auto aA = state.getRawParameterValue(ids::ampAttackMs)->load() / 1000.0f;
        const auto aD = state.getRawParameterValue(ids::ampDecayMs)->load() / 1000.0f;
        const auto aS = state.getRawParameterValue(ids::ampSustain)->load();
        const auto aR = state.getRawParameterValue(ids::ampReleaseMs)->load() / 1000.0f;

        sourcePath.clear();
        filterPath.clear();
        lfoPath.clear();

        const int numPoints = 256;
        const float numCycles = 2.0f;
        const float w = std::max(1.0f, (static_cast<float>(getWidth()) - 4.0f - 32.0f) / 5.0f);
        const float h = getHeight();
        const float halfH = h * 0.5f;
        const float stepX = w / static_cast<float>(numPoints);

        float drift = std::sin(frameCount * 0.05f) * 0.01f;
        float lfoAnim = std::sin(frameCount * 0.1f) * 0.02f;

        // Draw LFO Preview
        auto lfoOscShape = OscillatorWaveShape::sine;
        if (lfoWave == LfoWaveShape::saw) lfoOscShape = OscillatorWaveShape::saw;
        else if (lfoWave == LfoWaveShape::square) lfoOscShape = OscillatorWaveShape::pulse;
        else if (lfoWave == LfoWaveShape::triangle) lfoOscShape = OscillatorWaveShape::triangle;

        for (int i = 0; i < numPoints; ++i)
        {
            float normX = static_cast<float>(i) / static_cast<float>(numPoints);
            float lfoPhase = std::fmod(normX + lfoAnim, 1.0f);
            float lfoVal = renderIdealSample(lfoPhase, lfoOscShape, 0.5f);
            float lx = i * stepX;
            float ly = halfH - lfoVal * halfH * 0.7f;
            if (i == 0) lfoPath.startNewSubPath(lx, ly);
            else lfoPath.lineTo(lx, ly);
        }

        // Draw Env Previews
        filterEnvPath = createAdsrPath(fA, fD, fS, fR, w, h * 0.7f);
        ampEnvPath = createAdsrPath(aA, aD, aS, aR, w, h * 0.7f);

        // Draw Source/Filter Waves
        float lastFiltered = 0.0f;
        float alpha = std::clamp(cutoff / 4000.0f, 0.01f, 1.0f);

        for (int i = 0; i < numPoints; ++i)
        {
            float normX = static_cast<float>(i) / static_cast<float>(numPoints);
            float phase = std::fmod(normX * numCycles + drift, 1.0f);
            
            float sA = renderIdealSample(phase, waveA, pwA);
            float phaseB = std::fmod(phase * (1.0f + detuneB * 0.05f), 1.0f);
            float sB = renderIdealSample(phaseB, waveB, pwB);
            float noiseSample = (random.nextFloat() * 2.0f - 1.0f);

            float mixed = sA * levelA + sB * levelB + noiseSample * noiseLevel;
            float totalLevel = levelA + levelB + noiseLevel;
            float overloadDrive = 1.0f + std::max(0.0f, totalLevel - 1.0f) * 1.75f;
            float normalized = mixed * (totalLevel > 0.0f ? 1.0f / std::max(1.0f, totalLevel) : 0.0f);
            float sourceVal = std::tanh(normalized * overloadDrive);

            float x = i * stepX;
            float yS = halfH - sourceVal * halfH * 0.8f;
            if (i == 0) sourcePath.startNewSubPath(x, yS);
            else sourcePath.lineTo(x, yS);

            float filtered = lastFiltered + alpha * (sourceVal - lastFiltered);
            float resRinging = std::sin(phase * 15.0f) * res * 0.2f * (1.0f - alpha);
            lastFiltered = filtered;
            float yF = halfH - (filtered + resRinging) * halfH * 0.8f;
            if (i == 0) filterPath.startNewSubPath(x, yF);
            else filterPath.lineTo(x, yF);
        }
    }

    void SignalChainVisualizer::drawModulationPane(juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.saveState();
        g.setColour(palette::panelBlack);
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(palette::panelStroke.withAlpha(0.6f));
        g.drawRoundedRectangle(area.toFloat(), 3.0f, 1.0f);

        auto inner = area.reduced(4, 2);
        
        // Split into 3 micro-rows
        auto lfoArea = inner.removeFromTop(inner.getHeight() / 3);
        auto fenvArea = inner.removeFromTop(inner.getHeight() / 2);
        auto aenvArea = inner;

        auto drawMicro = [&](juce::Rectangle<int> r, const juce::Path& p, juce::Colour c, const juce::String& lbl)
        {
            g.setColour(palette::textSecondary.withAlpha(0.6f));
            g.setFont(7.0f);
            g.drawText(lbl, r.removeFromLeft(20), juce::Justification::centredLeft);
            
            g.setColour(c);
            auto pScaled = p;
            auto bounds = pScaled.getBounds();
            pScaled.applyTransform(juce::AffineTransform::scale(r.getWidth() / std::max(1.0f, bounds.getWidth()),
                                                                r.getHeight() / std::max(1.0f, bounds.getHeight())));
            pScaled.applyTransform(juce::AffineTransform::translation(static_cast<float>(r.getX()), 
                                                                      static_cast<float>(r.getY())));
            g.strokePath(pScaled, juce::PathStrokeType(1.0f));
        };

        drawMicro(lfoArea, lfoPath, palette::learnYellow, "LFO");
        drawMicro(fenvArea, filterEnvPath, palette::ledGreen, "FLT");
        drawMicro(aenvArea, ampEnvPath, palette::textPrimary, "AMP");

        g.setColour(palette::textSecondary);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("MODS", area.withTrimmedBottom(2).removeFromBottom(10), juce::Justification::centred);

        g.restoreState();
    }

    void SignalChainVisualizer::drawWaveform(juce::Graphics& g, 
                                              juce::Rectangle<int> area, 
                                              const juce::Path& path, 
                                              juce::Colour colour, 
                                              const juce::String& label)
    {
        g.saveState();
        g.setColour(palette::panelBlack);
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(palette::panelStroke.withAlpha(0.6f));
        g.drawRoundedRectangle(area.toFloat(), 3.0f, 1.0f);

        g.reduceClipRegion(area);
        
        g.setColour(palette::textSecondary);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, area.withTrimmedBottom(2).removeFromBottom(10), juce::Justification::centred);

        g.setColour(colour);
        auto p = path;
        p.applyTransform(juce::AffineTransform::translation(static_cast<float>(area.getX()), 
                                                            static_cast<float>(area.getY())));
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.restoreState();
    }
}
