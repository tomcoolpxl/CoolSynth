#include "EngineV3.h"

#include <algorithm>

namespace coolsynth::synth::v3
{
    EngineV3::EngineV3() : part(std::make_unique<Part>()) {}

    void EngineV3::prepare(double sampleRate, int maxBlockSize, int)
    {
        const int block = std::max(1, maxBlockSize);
        part->prepare(sampleRate, block, 0x5EEDu);
        fxRack.prepare(sampleRate, block, 2);
        work.setSize(2, block, false, true, true);
        masterGain.reset(sampleRate, masterGainRampSeconds);
        masterGain.setCurrentAndTargetValue(1.0f);
        masterPrimed = false;
        prepared = true;
    }

    void EngineV3::panic() noexcept
    {
        part->reset();
        fxRack.clear();
        work.clear();
    }

    void EngineV3::render(juce::AudioBuffer<float>& buffer,
                          std::span<const EngineMidiEvent> events,
                          const EngineSettings& settings,
                          const EngineTransportInfo& transport) noexcept
    {
        const int n = buffer.getNumSamples();
        if (! prepared || n <= 0)
        {
            buffer.clear();
            return;
        }

        // Rare: a host block longer than prepared. The work buffer grows once; the part splits on its own.
        if (work.getNumSamples() < n)
            work.setSize(2, n, false, false, true);

        juce::AudioBuffer<float> stereo(work.getArrayOfWritePointers(), 2, n);
        part->render(stereo.getWritePointer(0), stereo.getWritePointer(1), n, events, settings.part, transport);
        if (part->consumeSoundOff())
            fxRack.clear();

        fxRack.process(stereo, settings.drive, settings.phaser, settings.chorus, settings.delay, settings.reverb, settings.compressor);

        // The first block after prepare starts at the setting; ramping from unity would make the
        // first notes after loading louder than the patch.
        if (! masterPrimed)
        {
            masterGain.setCurrentAndTargetValue(std::max(0.0f, settings.masterGainLinear));
            masterPrimed = true;
        }
        masterGain.setTargetValue(std::max(0.0f, settings.masterGainLinear));
        float* l = stereo.getWritePointer(0);
        float* r = stereo.getWritePointer(1);
        for (int i = 0; i < n; ++i)
        {
            const float g = masterGain.getNextValue();
            l[i] = softClip(l[i] * g);
            r[i] = softClip(r[i] * g);
        }

        const int channels = buffer.getNumChannels();
        if (channels == 1)
        {
            float* out = buffer.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (l[i] + r[i]);
            return;
        }
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, stereo, std::min(ch, 1), 0, n);
    }
}
