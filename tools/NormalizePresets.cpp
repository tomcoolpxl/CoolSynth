// Offline LUFS normalizer for factory presets.
//
// Renders each factory preset through the real plugin signal path, measures
// integrated K-weighted loudness (ITU-R BS.1770 with gating) plus sample peak,
// and prints the masterGainDb correction needed to land at -18 LUFS without
// exceeding -3 dBFS peak. Writes a CSV; with --apply it also writes each
// corrected master line back into the bank files in resources/presets.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "parameters/ParameterIDs.h"
#include "synth/v3/Modulation.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

namespace
{
    constexpr double kSampleRate         = 48000.0;
    constexpr int    kBlockSize          = 256;
    constexpr double kRenderSeconds      = 4.0;
    constexpr double kBaseTargetLufs     = -18.0;
    constexpr double kPeakCeilingDb      = -3.0;

    // K-weighted LUFS undercounts perceived loudness for high-crest material
    // (sustained sine/triangle-ish patches, breathy flutes, sparse plucks) compared
    // to dense low-crest material (saws, square leads). Adding a crest-factor-aware
    // boost lets sparse patches sit a few dB hotter than the base LUFS target so
    // they feel comparable to dense leads. Dense material (crest <= 3 dB) is
    // unchanged; high-crest material gets up to +4 dB extra, still capped by the
    // peak ceiling.
    constexpr double kCrestBoostKneeDb   = 3.0;  // crest factor below which no boost applies
    constexpr double kCrestBoostSlope    = 0.4;  // dB of boost per dB of crest factor above the knee
    constexpr double kCrestBoostMaxDb    = 4.0;  // maximum boost

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

    const coolsynth::presets::PresetEntry* findEntry(const coolsynth::presets::FactoryPreset& preset, juce::StringRef id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return &entry;
        return nullptr;
    }

    float lookupMasterGainDb(const coolsynth::presets::FactoryPreset& preset)
    {
        const auto* entry = findEntry(preset, coolsynth::parameters::ids::masterGainDb);
        return entry != nullptr ? entry->text.getFloatValue() : -6.0f; // the parameter's default
    }

    /** True unless the preset sets part A to mono, legato or unison. */
    bool lookupPolyMode(const coolsynth::presets::FactoryPreset& preset)
    {
        const auto* entry = findEntry(preset, "pA.play.mode");
        return entry == nullptr || entry->text.trim().equalsIgnoreCase("poly");
    }

    bool lookupArpEnabled(const coolsynth::presets::FactoryPreset& preset)
    {
        const auto* entry = findEntry(preset, coolsynth::parameters::ids::arpEnabled);
        return entry != nullptr && entry->text.trim().equalsIgnoreCase("on");
    }

    /** True if one key already plays a chord (chord memory on). */
    bool lookupChordMemory(const coolsynth::presets::FactoryPreset& preset)
    {
        const auto* entry = findEntry(preset, "pA.chord.type");
        return entry != nullptr && ! entry->text.trim().equalsIgnoreCase("off");
    }

    /** Sets, or adds after the header, the master line of one preset in a bank file's text. */
    juce::String setMasterLine(const juce::String& text, const juce::String& presetName, double masterDb)
    {
        auto lines = juce::StringArray::fromLines(text);
        const auto value = "master = " + juce::String(masterDb, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        const int header = lines.indexOf("[" + presetName + "]");
        if (header < 0)
            return text;
        int insertAt = header + 1;
        for (int i = header + 1; i < lines.size() && ! lines[i].trim().startsWithChar('['); ++i)
        {
            const auto key = lines[i].upToFirstOccurrenceOf("=", false, false).trim();
            if (key == "master")
            {
                lines.set(i, value);
                return lines.joinIntoString("\n");
            }
            if (lines[i].trim().isNotEmpty())
                insertAt = i + 1;
        }
        lines.insert(insertAt, value);
        return lines.joinIntoString("\n");
    }

    /**
        Seconds to render before measuring: the global envelope's rise when a matrix slot uses it,
        or a slow amplifier attack (over half a second), whichever is longer; else 0. A swell or a
        riser is judged once it has arrived.
    */
    double globalEnvelopeRiseSeconds(juce::AudioProcessorValueTreeState& state)
    {
        auto value = [&](const juce::String& id) { auto* p = state.getRawParameterValue(id); return p != nullptr ? p->load() : 0.0f; };
        const double ampRise = (value("pA.env1.delay") + value("pA.env1.attack") + value("pA.env1.hold")) / 1000.0;
        const double ampPreroll = ampRise > 0.5 ? std::min(10.0, ampRise) : 0.0;
        constexpr float genvSource = static_cast<float>(coolsynth::synth::v3::ModSource::genv);
        bool used = false;
        for (int slot = 1; slot <= 24; ++slot)
        {
            const juce::String prefix = "pA.mod" + juce::String(slot) + ".";
            used = used || std::abs(value(prefix + "src") - genvSource) < 0.5f || std::abs(value(prefix + "via") - genvSource) < 0.5f;
        }
        if (! used)
            return ampPreroll;
        double ms = value("pA.genv.delay") + value("pA.genv.attack") + value("pA.genv.hold") + value("pA.genv.decay");
        if (value("pA.genv.sync") >= 0.5f)
            ms *= 120.0 / std::max(20.0f, value("arpInternalTempoBpm"));
        return std::max(ampPreroll, std::min(40.0, ms / 1000.0));
    }

    /** `root` is the chord's lowest note: 60 (C4), or for a sampled instrument six below the middle of the notes
        it was recorded at, so a flute is not judged below its lowest note nor a double bass at its top. */
    juce::MidiBuffer buildStimulus(const coolsynth::presets::FactoryPreset& preset, bool firstBlock, int root)
    {
        juce::MidiBuffer midi;
        if (! firstBlock)
            return midi;

        const bool isPoly = lookupPolyMode(preset);
        const bool isArp = lookupArpEnabled(preset);

        // Mono/unison: single sustained note.
        // Poly without arp: 4-note chord (root, minor third, fifth, octave) — covers thirds and fifths.
        // Poly with arp: single held note; arp generates the steps internally.
        // Poly with chord memory: single held note; the key plays the chord.
        const juce::uint8 velocity = 100;
        if (isPoly && ! isArp && ! lookupChordMemory(preset))
        {
            for (int interval : { 0, 3, 7, 12 })
                midi.addEvent(juce::MidiMessage::noteOn(1, root + interval, velocity), 0);
        }
        else
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, root, velocity), 0);
        }
        return midi;
    }

    LoudnessResult renderAndMeasure(const coolsynth::presets::FactoryPreset& preset)
    {
        SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset);
        processor.loadSampledInstrumentsNow(); // a sampled instrument is read in before the first note
        int stimulusRoot = 60, lowestRecorded = 0, highestRecorded = 0;
        if (processor.sampledInstrumentRange(lowestRecorded, highestRecorded))
            stimulusRoot = (lowestRecorded + highestRecorded) / 2 - 6;

        // A preset that routes the global envelope is measured once that envelope has risen
        // (a riser is judged at its top, not at its quiet start).
        const int prerollSamples = static_cast<int>(globalEnvelopeRiseSeconds(processor.getValueTreeState()) * kSampleRate);
        const int measuredSamples = static_cast<int>(kRenderSeconds * kSampleRate);
        const int totalSamples = prerollSamples + measuredSamples;
        juce::AudioBuffer<float> collected(2, measuredSamples);
        collected.clear();

        juce::AudioBuffer<float> block(2, kBlockSize);

        int rendered = 0;
        bool firstBlock = true;
        while (rendered < totalSamples)
        {
            const int n = std::min(kBlockSize, totalSamples - rendered);
            block.setSize(2, n, false, false, true);
            block.clear();

            auto midi = buildStimulus(preset, firstBlock, stimulusRoot);
            firstBlock = false;

            juce::AudioBuffer<float> shortBlock(block.getArrayOfWritePointers(), 2, n);
            processor.processBlock(shortBlock, midi);

            for (int i = 0; i < n; ++i)
            {
                const int at = rendered + i - prerollSamples;
                if (at >= 0)
                    for (int ch = 0; ch < 2; ++ch)
                        collected.setSample(ch, at, shortBlock.getSample(ch, i));
            }
            rendered += n;
        }

        processor.releaseResources();

        return measureLoudness(collected);
    }
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // --apply [bank directory]: write the corrected master gains into the bank files.
    bool apply = false;
    auto bankDirectory = juce::File::getCurrentWorkingDirectory().getChildFile("../resources/presets");
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg(argv[a]);
        if (arg == "--apply")
            apply = true;
        else
            bankDirectory = juce::File::getCurrentWorkingDirectory().getChildFile(arg);
    }
    std::vector<std::tuple<juce::String, juce::String, double>> corrections; // file, preset, master dB

    if (! coolsynth::presets::getFactoryBankErrors().isEmpty())
    {
        std::cout << "The bank has errors:\n" << coolsynth::presets::getFactoryBankErrors().joinIntoString("\n") << "\n";
        return 1;
    }

    const int presetCount = coolsynth::presets::getFactoryPresetCount();
    std::cout << "Normalizing " << presetCount << " factory presets to "
              << kBaseTargetLufs << " LUFS base (peak ceiling " << kPeakCeilingDb << " dBFS, "
              << "crest-factor boost up to +" << kCrestBoostMaxDb << " dB).\n\n";

    std::ostringstream csv;
    csv << "preset,old_master_db,new_master_db,delta_db,lufs,peak_db,crest_db,target_lufs,limited_by\n";

    int adjusted = 0;
    for (int i = 0; i < presetCount; ++i)
    {
        const auto& preset = coolsynth::presets::getFactoryPreset(i);
        const float oldMasterDb = lookupMasterGainDb(preset);

        const auto loudness = renderAndMeasure(preset);

        const double crestFactor = loudness.peakDb - loudness.integratedLufs;
        const double crestBoost  = std::clamp((crestFactor - kCrestBoostKneeDb) * kCrestBoostSlope,
                                              0.0, kCrestBoostMaxDb);
        const double targetLufs  = kBaseTargetLufs + crestBoost;

        double lufsDelta = targetLufs - loudness.integratedLufs;
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
                  << ", crest " << std::setw(5) << crestFactor << " dB"
                  << ", tgt " << std::setw(6) << targetLufs << " LUFS"
                  << ", limit=" << limitedBy << ")\n";

        csv << "\"" << preset.name << "\","
            << oldMasterDb << ","
            << newMasterDb << ","
            << appliedDelta << ","
            << loudness.integratedLufs << ","
            << loudness.peakDb << ","
            << crestFactor << ","
            << targetLufs << ","
            << limitedBy << "\n";

        if (std::abs(appliedDelta) > 0.25)
            ++adjusted;
        corrections.emplace_back(preset.sourceFile, preset.name, newMasterDb);
    }

    const auto csvPath = juce::File::getCurrentWorkingDirectory().getChildFile("preset_loudness.csv");
    csvPath.replaceWithText(csv.str());

    std::cout << "\nWrote " << csvPath.getFullPathName() << "\n";
    std::cout << adjusted << " of " << presetCount << " presets need a non-trivial adjustment.\n";

    if (apply)
    {
        std::map<juce::String, juce::String> files;
        for (const auto& [file, name, db] : corrections)
        {
            auto source = bankDirectory.getChildFile(file);
            if (! source.existsAsFile())
            {
                std::cout << "Missing bank file " << source.getFullPathName() << "\n";
                return 1;
            }
            if (files.find(file) == files.end())
                files[file] = source.loadFileAsString();
            files[file] = setMasterLine(files[file], name, db);
        }
        for (const auto& [file, text] : files)
        {
            bankDirectory.getChildFile(file).replaceWithText(text.endsWith("\n") ? text : text + "\n");
            std::cout << "Updated " << bankDirectory.getChildFile(file).getFullPathName() << "\n";
        }
    }
    return 0;
}
