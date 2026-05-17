// Offline LUFS normalizer for factory presets.
//
// Renders each factory preset through the real plugin signal path, measures
// integrated K-weighted loudness (ITU-R BS.1770 with gating) plus sample peak,
// and prints the masterGainDb correction needed to land at -18 LUFS without
// exceeding -3 dBFS peak. Output is a CSV that an external script can apply
// back into FactoryPresets.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

namespace
{
    constexpr double kSampleRate     = 48000.0;
    constexpr int    kBlockSize      = 256;
    constexpr double kRenderSeconds  = 4.0;
    constexpr double kTargetLufs     = -18.0;
    constexpr double kPeakCeilingDb  = -3.0;

    // ITU-R BS.1770-4 K-weighting biquads, tuned for 48 kHz sample rate.
    // Stage 1: high-shelf (~+4 dB above 1.68 kHz). Stage 2: high-pass (~38 Hz).
    struct Biquad
    {
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        double process(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0; }
    };

    Biquad makeKWeightStage1()
    {
        Biquad b;
        b.b0 =  1.53512485958697;
        b.b1 = -2.69169618940638;
        b.b2 =  1.19839281085285;
        b.a1 = -1.69065929318241;
        b.a2 =  0.73248077421585;
        return b;
    }

    Biquad makeKWeightStage2()
    {
        Biquad b;
        b.b0 =  1.0;
        b.b1 = -2.0;
        b.b2 =  1.0;
        b.a1 = -1.99004745483398;
        b.a2 =  0.99007225036621;
        return b;
    }

    struct LoudnessResult
    {
        double integratedLufs = -120.0;
        double peakDb         = -120.0;
    };

    // BS.1770 gated integrated loudness: 400 ms blocks with 75% overlap,
    // absolute gate at -70 LUFS, then relative gate at -10 LU below the
    // absolute-gated mean.
    LoudnessResult measureLoudness(const juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        std::vector<Biquad> s1(numChannels);
        std::vector<Biquad> s2(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            s1[ch] = makeKWeightStage1();
            s2[ch] = makeKWeightStage2();
        }

        std::vector<std::vector<double>> kSquared(numChannels);
        for (auto& v : kSquared) v.resize(numSamples);

        double rawPeak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                const double x = static_cast<double>(src[s]);
                rawPeak = std::max(rawPeak, std::abs(x));
                const double y = s2[ch].process(s1[ch].process(x));
                kSquared[ch][s] = y * y;
            }
        }

        const int blockSize = static_cast<int>(std::round(0.400 * kSampleRate));
        const int hopSize   = blockSize / 4; // 75% overlap
        std::vector<double> blockMs;
        blockMs.reserve(static_cast<size_t>(numSamples / hopSize + 1));

        for (int start = 0; start + blockSize <= numSamples; start += hopSize)
        {
            double sum = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto& sq = kSquared[ch];
                for (int s = start; s < start + blockSize; ++s)
                    sum += sq[s];
            }
            blockMs.push_back(sum / static_cast<double>(blockSize));
        }

        if (blockMs.empty())
            return { -120.0, juce::Decibels::gainToDecibels(static_cast<float>(rawPeak), -120.0f) };

        const double absoluteGateMs = std::pow(10.0, (-70.0 + 0.691) / 10.0);
        double sumAbs = 0.0;
        int    countAbs = 0;
        for (double ms : blockMs)
        {
            if (ms > absoluteGateMs)
            {
                sumAbs += ms;
                ++countAbs;
            }
        }

        if (countAbs == 0)
            return { -120.0, juce::Decibels::gainToDecibels(static_cast<float>(rawPeak), -120.0f) };

        const double absoluteMean = sumAbs / countAbs;
        const double relativeGateMs = absoluteMean * std::pow(10.0, -10.0 / 10.0);

        double sumRel = 0.0;
        int    countRel = 0;
        for (double ms : blockMs)
        {
            if (ms > absoluteGateMs && ms > relativeGateMs)
            {
                sumRel += ms;
                ++countRel;
            }
        }

        const double finalMs = countRel > 0 ? sumRel / countRel : absoluteMean;
        const double lufs = -0.691 + 10.0 * std::log10(std::max(finalMs, 1e-30));
        const double peakDb = juce::Decibels::gainToDecibels(static_cast<float>(rawPeak), -120.0f);
        return { lufs, peakDb };
    }

    float lookupMasterGainDb(const coolsynth::presets::FactoryPreset& preset)
    {
        namespace ids = coolsynth::parameters::ids;
        for (int i = 0; i < preset.valueCount; ++i)
            if (juce::StringRef(preset.values[i].parameterId) == juce::StringRef(ids::masterGainDb))
                return preset.values[i].value;
        return -12.0f;
    }

    coolsynth::parameters::PlayModeChoice lookupPlayMode(const coolsynth::presets::FactoryPreset& preset)
    {
        namespace ids = coolsynth::parameters::ids;
        for (int i = 0; i < preset.valueCount; ++i)
            if (juce::StringRef(preset.values[i].parameterId) == juce::StringRef(ids::playMode))
                return static_cast<coolsynth::parameters::PlayModeChoice>(static_cast<int>(preset.values[i].value));
        return coolsynth::parameters::PlayModeChoice::poly;
    }

    bool lookupArpEnabled(const coolsynth::presets::FactoryPreset& preset)
    {
        namespace ids = coolsynth::parameters::ids;
        for (int i = 0; i < preset.valueCount; ++i)
            if (juce::StringRef(preset.values[i].parameterId) == juce::StringRef(ids::arpEnabled))
                return preset.values[i].value >= 0.5f;
        return false;
    }

    juce::MidiBuffer buildStimulus(const coolsynth::presets::FactoryPreset& preset, bool firstBlock)
    {
        juce::MidiBuffer midi;
        if (! firstBlock)
            return midi;

        const auto mode = lookupPlayMode(preset);
        const bool isArp = lookupArpEnabled(preset);

        // Mono/unison: single sustained note (60 = C4).
        // Poly without arp: 4-note chord (C, Eb, G, C+1oct) — covers thirds and fifths.
        // Poly with arp: single held note; arp generates the steps internally.
        const juce::uint8 velocity = 100;
        if (mode == coolsynth::parameters::PlayModeChoice::poly && ! isArp)
        {
            for (int note : { 60, 63, 67, 72 })
                midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        }
        else
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, velocity), 0);
        }
        return midi;
    }

    LoudnessResult renderAndMeasure(const coolsynth::presets::FactoryPreset& preset)
    {
        SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset);

        const int totalSamples = static_cast<int>(kRenderSeconds * kSampleRate);
        juce::AudioBuffer<float> collected(2, totalSamples);
        collected.clear();

        juce::AudioBuffer<float> block(2, kBlockSize);

        int rendered = 0;
        bool firstBlock = true;
        while (rendered < totalSamples)
        {
            const int n = std::min(kBlockSize, totalSamples - rendered);
            block.setSize(2, n, false, false, true);
            block.clear();

            auto midi = buildStimulus(preset, firstBlock);
            firstBlock = false;

            juce::AudioBuffer<float> shortBlock(block.getArrayOfWritePointers(), 2, n);
            processor.processBlock(shortBlock, midi);

            for (int ch = 0; ch < 2; ++ch)
                collected.copyFrom(ch, rendered, shortBlock, ch, 0, n);
            rendered += n;
        }

        processor.releaseResources();

        return measureLoudness(collected);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const int presetCount = coolsynth::presets::getFactoryPresetCount();
    std::cout << "Normalizing " << presetCount << " factory presets to "
              << kTargetLufs << " LUFS (peak ceiling " << kPeakCeilingDb << " dBFS).\n\n";

    std::ostringstream csv;
    csv << "preset,old_master_db,new_master_db,delta_db,lufs,peak_db,limited_by\n";

    int adjusted = 0;
    for (int i = 0; i < presetCount; ++i)
    {
        const auto& preset = coolsynth::presets::getFactoryPreset(i);
        const float oldMasterDb = lookupMasterGainDb(preset);

        const auto loudness = renderAndMeasure(preset);

        double lufsDelta = kTargetLufs - loudness.integratedLufs;
        double peakDelta = kPeakCeilingDb - loudness.peakDb;
        std::string limitedBy = "lufs";
        double correction = lufsDelta;
        if (peakDelta < correction)
        {
            correction = peakDelta;
            limitedBy = "peak";
        }

        double newMasterDb = oldMasterDb + correction;
        // Round to 0.5 dB so the written value reads cleanly in source.
        newMasterDb = std::round(newMasterDb * 2.0) / 2.0;
        newMasterDb = std::clamp(newMasterDb, -36.0, 6.0);
        const double appliedDelta = newMasterDb - oldMasterDb;

        std::cout << std::left << std::setw(28) << preset.name
                  << "  master " << std::setw(7) << std::right << std::fixed << std::setprecision(1)
                  << oldMasterDb << " -> " << std::setw(6) << newMasterDb << " dB"
                  << "  (LUFS " << std::setw(7) << std::setprecision(2) << loudness.integratedLufs
                  << ", peak " << std::setw(6) << loudness.peakDb << " dB"
                  << ", limit=" << limitedBy << ")\n";

        csv << "\"" << preset.name << "\","
            << oldMasterDb << ","
            << newMasterDb << ","
            << appliedDelta << ","
            << loudness.integratedLufs << ","
            << loudness.peakDb << ","
            << limitedBy << "\n";

        if (std::abs(appliedDelta) > 0.25)
            ++adjusted;
    }

    const auto csvPath = juce::File::getCurrentWorkingDirectory().getChildFile("preset_loudness.csv");
    csvPath.replaceWithText(csv.str());

    std::cout << "\nWrote " << csvPath.getFullPathName() << "\n";
    std::cout << adjusted << " of " << presetCount << " presets need a non-trivial adjustment.\n";
    return 0;
}
