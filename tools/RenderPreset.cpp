// Renders a preset playing given notes to a WAV file, through the real plugin signal path.
//
//   CoolSynthRender --preset "Marimba" --notes "60:0:1.5,64:0.5:1.5:80" --seconds 6 --out marimba.wav
//                   [--bank some.csbank] [--style N] [--no-fx [--keep-inserts]] [--tune CENTS]
//                   [--cc time:number:value,...] [--list]
//
// --preset  a preset's name, or "Room: Name" where two rooms have a preset of that name
// --notes   comma-separated note:start:length[:velocity], times in seconds, velocity 1..127 (100)
// --bank    take the preset from this bank file instead of the built-in bank
// --style   the Nth style, from 0, of the sampled instrument in the first generator slot
// --no-fx   switch every effect off, to hear the voices alone
// --keep-inserts  with --no-fx: leave the parts' own effect slots on, for a sound whose character is in one of them
// --tune    play this many cents sharp (a reference recording's instrument is rarely at 440 Hz); a held pitch bend
// --cc      controller moves, time in seconds, value 0..127: 0.7:1:127 pushes the modulation wheel up at 0.7 s, as a
//           player brings in a vibrato
// --list    print the built-in preset names and exit
//
// Used to put a preset next to a reference recording, and a change next to what it replaces.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 256;

    struct NoteEvent
    {
        std::int64_t sample = 0;
        bool on = false;
        int note = 60;
        int velocity = 100;
    };

    /** "60:0:1.5:100,64:0.5:1.5" into on and off events, in time order (offs before ons at one sample). */
    std::vector<NoteEvent> parseNotes(const juce::String& text)
    {
        std::vector<NoteEvent> events;
        for (const auto& item : juce::StringArray::fromTokens(text, ",", ""))
        {
            const auto fields = juce::StringArray::fromTokens(item.trim(), ":", "");
            if (fields.size() < 3)
                continue;
            const int note = juce::jlimit(0, 127, fields[0].getIntValue());
            const double start = std::max(0.0, fields[1].getDoubleValue());
            const double length = std::max(0.001, fields[2].getDoubleValue());
            const int velocity = fields.size() > 3 ? juce::jlimit(1, 127, fields[3].getIntValue()) : 100;
            events.push_back({ static_cast<std::int64_t>(std::llround(start * kSampleRate)), true, note, velocity });
            events.push_back({ static_cast<std::int64_t>(std::llround((start + length) * kSampleRate)), false, note, 0 });
        }
        std::stable_sort(events.begin(), events.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            return a.sample != b.sample ? a.sample < b.sample : (! a.on && b.on);
        });
        return events;
    }

    bool writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);
        if (! out)
            return false;
        const std::uint32_t channels = static_cast<std::uint32_t>(audio.getNumChannels());
        const std::uint32_t frames = static_cast<std::uint32_t>(audio.getNumSamples());
        const std::uint32_t dataBytes = frames * channels * 3u;
        auto put32 = [&](std::uint32_t v) { const char b[4] { static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24) }; out.write(b, 4); };
        auto put16 = [&](std::uint32_t v) { const char b[2] { static_cast<char>(v), static_cast<char>(v >> 8) }; out.write(b, 2); };
        out.write("RIFF", 4);
        put32(36u + dataBytes);
        out.write("WAVEfmt ", 8);
        put32(16u);
        put16(1u);
        put16(channels);
        put32(static_cast<std::uint32_t>(kSampleRate));
        put32(static_cast<std::uint32_t>(kSampleRate) * channels * 3u);
        put16(channels * 3u);
        put16(24u);
        out.write("data", 4);
        put32(dataBytes);
        for (std::uint32_t i = 0; i < frames; ++i)
            for (std::uint32_t ch = 0; ch < channels; ++ch)
            {
                const double v = juce::jlimit(-1.0, 1.0, static_cast<double>(audio.getSample(static_cast<int>(ch), static_cast<int>(i))));
                const auto s = static_cast<std::int32_t>(std::lround(v * 8388607.0));
                const char b[3] { static_cast<char>(s), static_cast<char>(s >> 8), static_cast<char>(s >> 16) };
                out.write(b, 3);
            }
        return static_cast<bool>(out);
    }
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::StringArray args(argv + 1, argc - 1);
    auto value = [&](const char* flag, const juce::String& fallback = {})
    {
        const int i = args.indexOf(flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };

    if (args.contains("--list"))
    {
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            std::cout << coolsynth::presets::getFactoryPreset(i).category << ": " << coolsynth::presets::getFactoryPreset(i).name << "\n";
        return 0;
    }

    const auto presetName = value("--preset");
    const auto outPath = value("--out");
    const double seconds = juce::jlimit(0.1, 600.0, value("--seconds", "4").getDoubleValue());
    const auto events = parseNotes(value("--notes", "60:0:2"));
    if (presetName.isEmpty() || outPath.isEmpty() || events.empty())
    {
        std::cerr << "usage: CoolSynthRender --preset NAME --notes note:start:length[:velocity],... --seconds N --out FILE [--bank FILE] [--no-fx]\n";
        return 2;
    }

    coolsynth::presets::FactoryPreset preset;
    bool found = false;
    if (const auto bankPath = value("--bank"); bankPath.isNotEmpty())
    {
        const juce::File bankFile(juce::File::getCurrentWorkingDirectory().getChildFile(bankPath));
        const auto parsed = coolsynth::presets::parseBankFile(bankFile.loadFileAsString(), bankFile.getFileName());
        for (const auto& error : parsed.errors)
            std::cerr << error << "\n";
        for (const auto& candidate : parsed.presets)
            if (candidate.name.equalsIgnoreCase(presetName))
            {
                preset = candidate;
                found = true;
            }
    }
    else
    {
        // "Name" takes the first preset of that name; "Room: Name" tells two rooms' presets of one name apart.
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount() && ! found; ++i)
        {
            const auto& candidate = coolsynth::presets::getFactoryPreset(i);
            if (candidate.name.equalsIgnoreCase(presetName) || (candidate.category + ": " + candidate.name).equalsIgnoreCase(presetName))
            {
                preset = candidate;
                found = true;
            }
        }
    }
    if (! found)
    {
        std::cerr << "no preset named '" << presetName << "'\n";
        return 1;
    }

    SynthAudioProcessor processor;
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    for (const auto& problem : coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset))
        std::cerr << problem << "\n";
    // --style N: the Nth style (from 0) of the sampled instrument in the first generator slot.
    if (const auto styleText = value("--style"); styleText.isNotEmpty())
        if (auto* styleParameter = processor.getValueTreeState().getParameter("pA.g1.p8"))
            styleParameter->setValueNotifyingHost(styleParameter->getValueForText(juce::String(styleText.getIntValue() + 1))); // the parameter shows styles from 1
    if (! processor.loadSampledInstrumentsNow())
        std::cerr << "a sampled instrument of this preset is not installed: it will be silent\n";
    if (! processor.loadUserSoundsNow())
        std::cerr << "a file of the user's this preset names is missing or unreadable: its slot will be silent\n";

    if (args.contains("--no-fx"))
    {
        // The master effect slots: their first type is Off.
        for (int slot = 1; slot <= coolsynth::parameters::v3::masterEffectSlots; ++slot)
            if (auto* parameter = processor.getValueTreeState().getParameter("f" + juce::String(slot) + ".type"))
                parameter->setValueNotifyingHost(0.0f);
        // The parts' insert slots too (their first type is Off), unless they are part of the sound under test.
        if (! args.contains("--keep-inserts"))
            for (const char* part : { "pA", "pB" })
                for (int slot = 1; slot <= 3; ++slot)
                    if (auto* parameter = processor.getValueTreeState().getParameter(juce::String(part) + ".i" + juce::String(slot) + ".type"))
                        parameter->setValueNotifyingHost(0.0f);
    }

    // --tune CENTS: a pitch bend held for the whole clip, within the preset's bend range.
    int bend = 8192;
    if (const auto tuneText = value("--tune"); tuneText.isNotEmpty())
    {
        float range = 2.0f;
        if (const auto* rangeValue = processor.getValueTreeState().getRawParameterValue("pA.play.bendRange"))
            range = rangeValue->load();
        if (range > 0.0f)
            bend = juce::jlimit(0, 16383, 8192 + static_cast<int>(std::lround(tuneText.getFloatValue() / 100.0f / range * 8191.0f)));
        else
            std::cerr << "--tune needs a bend range above zero in the preset\n";
    }

    // --cc time:number:value,...: controller moves, in time order with the notes.
    struct ControllerMove
    {
        std::int64_t sample = 0;
        int number = 1;
        int value = 0;
    };
    std::vector<ControllerMove> moves;
    for (const auto& item : juce::StringArray::fromTokens(value("--cc"), ",", ""))
    {
        const auto fields = juce::StringArray::fromTokens(item, ":", "");
        if (fields.size() == 3)
            moves.push_back({ static_cast<std::int64_t>(std::llround(fields[0].getDoubleValue() * kSampleRate)), fields[1].getIntValue(),
                              juce::jlimit(0, 127, fields[2].getIntValue()) });
    }
    std::sort(moves.begin(), moves.end(), [](const ControllerMove& a, const ControllerMove& b) { return a.sample < b.sample; });
    std::size_t nextMove = 0;

    const int total = static_cast<int>(std::llround(seconds * kSampleRate));
    juce::AudioBuffer<float> rendered(2, total);
    rendered.clear();
    juce::AudioBuffer<float> block(2, kBlockSize);
    std::size_t next = 0;
    for (int start = 0; start < total; start += kBlockSize)
    {
        const int n = std::min(kBlockSize, total - start);
        juce::MidiBuffer midi;
        if (start == 0 && bend != 8192)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, bend), 0);
        while (nextMove < moves.size() && moves[nextMove].sample < start + n)
        {
            const auto& move = moves[nextMove++];
            midi.addEvent(juce::MidiMessage::controllerEvent(1, move.number, move.value),
                          static_cast<int>(std::max<std::int64_t>(0, move.sample - start)));
        }
        while (next < events.size() && events[next].sample < start + n)
        {
            const auto& e = events[next++];
            const int offset = static_cast<int>(std::max<std::int64_t>(0, e.sample - start));
            midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, static_cast<juce::uint8>(e.velocity)) : juce::MidiMessage::noteOff(1, e.note), offset);
        }
        juce::AudioBuffer<float> span(block.getArrayOfWritePointers(), 2, n);
        span.clear();
        processor.processBlock(span, midi);
        for (int ch = 0; ch < 2; ++ch)
            rendered.copyFrom(ch, start, span, ch, 0, n);
    }
    processor.releaseResources();

    const juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(outPath));
    outFile.getParentDirectory().createDirectory();
    if (! writeWav24(outFile, rendered))
    {
        std::cerr << "cannot write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    std::cout << preset.name << " -> " << outFile.getFullPathName() << " (" << seconds << " s, peak "
              << juce::Decibels::gainToDecibels(rendered.getMagnitude(0, total), -120.0f) << " dB)\n";
    return 0;
}
