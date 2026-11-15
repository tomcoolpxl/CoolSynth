// Offline loudness levelling for the factory presets (decision D153).
//
// Renders each factory preset through the real plugin signal path and gives every preset one loudness:
// the loudest 400 ms of its test chord (BS.1770 momentary loudness, K-weighted), so a struck piano chord
// and a held pad are judged by how loud they sound when played, not by an average over the decay.
// Two limits keep chords clean: the test chord (velocity 100) peaks at least 6 dB under the output's
// soft clipper, and a chord at full velocity anywhere in the preset's range stays under the clipper.
// Writes a CSV; with --apply it also writes each corrected master line back into the bank files in
// resources/presets. --target <LUFS> tries another common loudness.

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
#include "synth/v3/InstrumentCatalogue.h"

namespace
{
    constexpr double kSampleRate         = 48000.0;
    constexpr int    kBlockSize          = 256;
    constexpr double kRenderSeconds      = 4.0;
    double           kTargetLufs         = -20.0; // the one loudness: the test chord's loudest 400 ms (--target); D153

    // The output's soft clipper bends from 0.8 (-1.9 dBFS). The test chord, four notes at velocity 100, peaks
    // 6 dB under that (room for more notes and harder playing); a four-note chord at full velocity anywhere
    // in the preset's range stays under it.
    constexpr double kClipperKneeDb      = -1.94;
    constexpr double kChordCeilingDb     = kClipperKneeDb - 6.0;
    constexpr double kRangeCeilingDb     = kClipperKneeDb - 0.06;

    // Every render plays at this master gain, far under the clipper, so peaks and loudness are measured
    // without its bend; the preset's own master gain is added back afterwards (master gain is the last
    // linear stage before the clipper).
    constexpr float  kProbeMasterDb      = -40.0f;

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
        double momentaryMaxLufs = -120.0; // the loudest 400 ms block
        bool sampled = false;             // the preset plays a sampled instrument
    };

    /** Shifts every level of a measurement by `db` (the render's gain was linear). */
    LoudnessResult lifted(LoudnessResult r, double db)
    {
        r.integratedLufs += db;
        r.peakDb += db;
        r.momentaryMaxLufs += db;
        return r;
    }

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

        const double rawPeakDb = 20.0 * std::log10(std::max(rawPeak, 1e-12));
        if (blockMs.empty())
            return { -120.0, rawPeakDb };
        const double loudestBlock = *std::max_element(blockMs.begin(), blockMs.end());
        const double momentaryMax = -0.691 + 10.0 * std::log10(std::max(loudestBlock, 1e-30));

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
            return { -120.0, rawPeakDb, momentaryMax };

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
        return { lufs, rawPeakDb, momentaryMax };
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

    /** Plays the processor at kProbeMasterDb; returns the dB to add to what it measures to get the preset's own level. */
    double setProbeMaster(SynthAudioProcessor& processor, const coolsynth::presets::FactoryPreset& preset)
    {
        auto* master = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::masterGainDb);
        master->setValueNotifyingHost(master->convertTo0to1(kProbeMasterDb));
        return static_cast<double>(lookupMasterGainDb(preset) - kProbeMasterDb);
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

    /** `style` from 0 switches the sampled instrument in the first generator slot to another of its styles. */
    LoudnessResult renderAndMeasure(const coolsynth::presets::FactoryPreset& preset, int style = -1)
    {
        SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset);
        if (style >= 0)
            if (auto* styleParameter = processor.getValueTreeState().getParameter("pA.g1.p8"))
                styleParameter->setValueNotifyingHost(styleParameter->getValueForText(juce::String(style + 1))); // shown from 1
        const double lift = setProbeMaster(processor, preset);
        processor.loadSampledInstrumentsNow(); // a sampled instrument is read in before the first note
        int stimulusRoot = 60, lowestRecorded = 0, highestRecorded = 0;
        const bool sampled = processor.sampledInstrumentRange(lowestRecorded, highestRecorded);
        if (sampled)
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

        auto measured = lifted(measureLoudness(collected), lift);
        measured.sampled = sampled;
        return measured;
    }

    /**
        The highest peak, at the preset's own master gain and before the output's clipper, of the test
        stimulus at full velocity (127) played at six places spread over the preset's range: a sampled
        instrument's recorded notes (a chord's top note stays inside them), else C2 to C6. Each place is held
        1.5 seconds (plus a swell's rise) and released for one. `worstRoot` is the lowest note where it peaked.
    */
    double worstPeakOverRange(const coolsynth::presets::FactoryPreset& preset, int& worstRoot)
    {
        SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset);
        const double lift = setProbeMaster(processor, preset);
        processor.loadSampledInstrumentsNow();

        const bool chord = lookupPolyMode(preset) && ! lookupArpEnabled(preset) && ! lookupChordMemory(preset);
        const std::vector<int> intervals = chord ? std::vector<int> { 0, 3, 7, 12 } : std::vector<int> { 0 };
        int lowest = 36, highest = 96;
        if (! processor.sampledInstrumentRange(lowest, highest))
        {
            lowest = 36;
            highest = 96;
        }
        const int from = lowest, to = std::max(lowest, highest - intervals.back());
        std::vector<int> roots;
        for (int k = 0; k < 6; ++k)
        {
            const int root = from + (to - from) * k / 5;
            if (roots.empty() || roots.back() != root)
                roots.push_back(root);
        }

        const int holdSamples = static_cast<int>((1.5 + globalEnvelopeRiseSeconds(processor.getValueTreeState())) * kSampleRate);
        const int segmentSamples = holdSamples + static_cast<int>(1.0 * kSampleRate);
        juce::AudioBuffer<float> block(2, kBlockSize);
        double worst = 0.0;
        worstRoot = roots.front();
        for (const int root : roots)
        {
            double peak = 0.0;
            for (int rendered = 0; rendered < segmentSamples; rendered += kBlockSize)
            {
                const int n = std::min(kBlockSize, segmentSamples - rendered);
                juce::MidiBuffer midi;
                for (const int interval : intervals)
                {
                    if (rendered == 0)
                        midi.addEvent(juce::MidiMessage::noteOn(1, root + interval, static_cast<juce::uint8>(127)), 0);
                    if (rendered <= holdSamples && holdSamples < rendered + n)
                        midi.addEvent(juce::MidiMessage::noteOff(1, root + interval), holdSamples - rendered);
                }
                block.setSize(2, n, false, false, true);
                block.clear();
                processor.processBlock(block, midi);
                for (int ch = 0; ch < 2; ++ch)
                    peak = std::max(peak, static_cast<double>(block.getMagnitude(ch, 0, n)));
            }
            if (peak > worst)
            {
                worst = peak;
                worstRoot = root;
            }
        }
        processor.releaseResources();
        return 20.0 * std::log10(std::max(worst, 1e-12)) + lift;
    }
}

namespace
{
    /**
        The change of master gain a measurement asks for, and which rule set it: "loudness" (the preset reaches
        the common loudness), "chord" (the test chord's 6 dB of room) or "range" (a full-velocity chord
        somewhere in its range would reach the clipper). `worstPeakDb` is kNoRangeLimit when not measured.
    */
    double correctionFor(const LoudnessResult& loudness, double worstPeakDb, std::string& limitedBy)
    {
        const double loudnessDelta = kTargetLufs - loudness.momentaryMaxLufs;
        const double chordDelta = kChordCeilingDb - loudness.peakDb;
        const double rangeDelta = kRangeCeilingDb - worstPeakDb;
        const double correction = std::min({ loudnessDelta, chordDelta, rangeDelta });
        limitedBy = correction == loudnessDelta ? "loudness" : (correction == chordDelta ? "chord" : "range");
        return correction;
    }

    constexpr double kNoRangeLimit = -1000.0; // worstPeakDb when the range is not measured

    /** --styles: for every preset that plays a sampled instrument, how much louder or quieter each of the
        instrument's other styles would have to be to need the same master gain as its first. The library builder
        takes these as the styles' trims (the catalogue's trim= option), so one preset level suits every style. */
    int reportStyleTrims()
    {
        namespace sv3 = coolsynth::synth::v3;
        const auto& catalogue = sv3::InstrumentCatalogue::instance();
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
        {
            const auto& preset = coolsynth::presets::getFactoryPreset(i);
            const auto* type = findEntry(preset, "pA.g1.type");
            const auto* sample = findEntry(preset, "pA.g1.p1");
            const int instrument = type != nullptr && sample != nullptr && type->text.equalsIgnoreCase("Sample")
                                       ? catalogue.find(sample->text.toStdString()) : -1;
            if (instrument < 0)
                continue;
            const auto& styles = catalogue.at(instrument).styles;
            std::string limitedBy;
            const double first = correctionFor(renderAndMeasure(preset, 0), kNoRangeLimit, limitedBy);
            for (int s = 1; s < static_cast<int>(styles.size()); ++s)
            {
                const double other = correctionFor(renderAndMeasure(preset, s), kNoRangeLimit, limitedBy);
                std::cout << "trim | " << catalogue.at(instrument).name << " | " << styles[static_cast<std::size_t>(s)].name << " | "
                          << std::fixed << std::setprecision(1) << (other - first) << "\n" << std::flush;
            }
        }
        return 0;
    }
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (int a = 1; a < argc; ++a)
        if (juce::String(argv[a]) == "--styles")
            return reportStyleTrims();

    // --apply [bank directory]: write the corrected master gains into the bank files. --target <LUFS>.
    bool apply = false;
    auto bankDirectory = juce::File::getCurrentWorkingDirectory().getChildFile("../resources/presets");
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg(argv[a]);
        if (arg == "--apply")
            apply = true;
        else if (arg == "--target" && a + 1 < argc)
            kTargetLufs = juce::String(argv[++a]).getDoubleValue();
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
    std::cout << "Levelling " << presetCount << " factory presets to " << kTargetLufs
              << " LUFS (loudest 400 ms of the test chord); test chord peak at most " << kChordCeilingDb
              << " dBFS, full-velocity chords over the range at most " << kRangeCeilingDb << " dBFS.\n\n";

    std::ostringstream csv;
    csv << "preset,old_master_db,new_master_db,delta_db,integrated_lufs,momentary_max_lufs,chord_peak_db,"
           "range_peak_db,range_peak_root,loudest_possible_lufs,limited_by\n";

    int adjusted = 0;
    std::vector<std::pair<double, juce::String>> loudestPossible; // how loud each preset can be under both limits
    // Every preset measured at once, on all cores but two (D164): each its own processor; only the sample memory is
    // shared (D162). The results are printed and written in bank order afterwards.
    struct Measured
    {
        LoudnessResult loudness;
        double worstPeakDb = 0.0;
        int worstRoot = 0;
    };
    std::vector<Measured> measured(static_cast<std::size_t>(presetCount));
    {
        juce::ThreadPool pool(juce::ThreadPoolOptions {}.withNumberOfThreads(std::max(1, juce::SystemStats::getNumCpus() - 2)));
        for (int i = 0; i < presetCount; ++i)
            pool.addJob([i, &measured]
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(i);
                // Five takes: a preset that plays random notes (a random arp, chance, a random walk) measured up to 5 dB
                // apart from one 4-second take to the next. The median loudness, the highest chord peak.
                std::vector<LoudnessResult> takes;
                for (int take = 0; take < 5; ++take)
                    takes.push_back(renderAndMeasure(preset));
                std::sort(takes.begin(), takes.end(), [](const auto& x, const auto& y) { return x.momentaryMaxLufs < y.momentaryMaxLufs; });
                auto& m = measured[static_cast<std::size_t>(i)];
                m.loudness = takes[2];
                for (const auto& t : takes)
                    m.loudness.peakDb = std::max(m.loudness.peakDb, t.peakDb);
                m.worstPeakDb = worstPeakOverRange(preset, m.worstRoot);
            });
        while (pool.getNumJobs() > 0)
            juce::Thread::sleep(20);
    }

    for (int i = 0; i < presetCount; ++i)
    {
        const auto& preset = coolsynth::presets::getFactoryPreset(i);
        const float oldMasterDb = lookupMasterGainDb(preset);
        const auto& loudness = measured[static_cast<std::size_t>(i)].loudness;
        const int worstRoot = measured[static_cast<std::size_t>(i)].worstRoot;
        const double worstPeakDb = measured[static_cast<std::size_t>(i)].worstPeakDb;

        std::string limitedBy;
        const double correction = correctionFor(loudness, worstPeakDb, limitedBy);
        const double possible = loudness.momentaryMaxLufs + std::min(kChordCeilingDb - loudness.peakDb, kRangeCeilingDb - worstPeakDb);
        loudestPossible.emplace_back(possible, preset.name);

        double newMasterDb = oldMasterDb + correction;
        // Round to 0.5 dB so the written value reads cleanly in source.
        newMasterDb = std::round(newMasterDb * 2.0) / 2.0;
        newMasterDb = std::clamp(newMasterDb, -36.0, 6.0);
        const double appliedDelta = newMasterDb - oldMasterDb;

        std::cout << std::left << std::setw(28) << preset.name
                  << "  master " << std::setw(7) << std::right << std::fixed << std::setprecision(1)
                  << oldMasterDb << " -> " << std::setw(6) << newMasterDb << " dB"
                  << "  (loudest " << std::setw(6) << std::setprecision(1) << loudness.momentaryMaxLufs
                  << " LUFS, chord peak " << std::setw(5) << loudness.peakDb
                  << ", range peak " << std::setw(5) << worstPeakDb << " at " << worstRoot
                  << ", could be " << std::setw(6) << possible << ", limit=" << limitedBy << ")\n" << std::flush;

        csv << "\"" << preset.name << "\","
            << oldMasterDb << ","
            << newMasterDb << ","
            << appliedDelta << ","
            << loudness.integratedLufs << ","
            << loudness.momentaryMaxLufs << ","
            << loudness.peakDb << ","
            << worstPeakDb << ","
            << worstRoot << ","
            << possible << ","
            << limitedBy << "\n";

        if (std::abs(appliedDelta) > 0.25)
            ++adjusted;
        corrections.emplace_back(preset.sourceFile, preset.name, newMasterDb);
    }

    const auto csvPath = juce::File::getCurrentWorkingDirectory().getChildFile("preset_loudness.csv");
    csvPath.replaceWithText(csv.str());

    std::cout << "\nWrote " << csvPath.getFullPathName() << "\n";
    std::cout << adjusted << " of " << presetCount << " presets need a non-trivial adjustment.\n";

    // How many presets can reach each common loudness, and the ones that hold it lowest.
    std::sort(loudestPossible.begin(), loudestPossible.end());
    std::cout << "\nCommon loudness and how many presets reach it:\n";
    for (int target = -12; target >= -24; --target)
    {
        const auto reach = std::count_if(loudestPossible.begin(), loudestPossible.end(),
                                         [&](const auto& p) { return p.first >= target; });
        std::cout << "  " << target << " LUFS: " << reach << " of " << presetCount << "\n";
    }
    std::cout << "Lowest ceilings:\n";
    for (std::size_t k = 0; k < std::min<std::size_t>(15, loudestPossible.size()); ++k)
        std::cout << "  " << std::setw(6) << std::setprecision(1) << loudestPossible[k].first << "  " << loudestPossible[k].second << "\n";

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
