#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>

#include "parameters/v3/SlotParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/BankFile.h"
#include "presets/FactoryPresets.h"

namespace
{
    using coolsynth::presets::BankPreset;
    using coolsynth::presets::parseBankFile;

    juce::String entryText(const BankPreset& preset, const juce::String& id)
    {
        for (const auto& entry : preset.entries)
            if (entry.parameterId == id)
                return entry.text;
        return "<missing>";
    }

    float rawValue(SynthAudioProcessor& processor, const juce::String& id)
    {
        return processor.getValueTreeState().getRawParameterValue(id)->load();
    }
}

class V3BankTests final : public juce::UnitTest
{
public:
    V3BankTests() : juce::UnitTest("V3 bank files", "V3") {}

    void runTest() override
    {
        beginTest("parse_maps_slots_envelopes_and_effects_to_parameter_ids");
        {
            const auto parsed = parseBankFile(R"(
# a comment line
[Reese]
room = Main Stage
g1 = VA shape=saw oct=-1 fine=+6 level=0.8   # trailing comment
p1 = Ladder cutoff=800 res=0.2 mode=lp12
env1 = attack=5 decay=200 sustain=1 release=120 loop=off
voice = env2ToP1=0.1 glide=15
play = mode=legato priority=low
arp = on rate=1/16 pattern=up/down
drive = on amount=0.15 mix=0.8
master = -6
)", "test.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            expectEquals(static_cast<int>(parsed.presets.size()), 1);
            const auto& preset = parsed.presets.front();
            expect(preset.name == "Reese");
            expect(preset.category == "Main Stage");
            expect(preset.sourceFile == "test.csbank");
            expect(entryText(preset, "pA.g1.type") == "VA");
            expect(entryText(preset, "pA.g1.p1") == "saw");     // shape is the first VA parameter
            expect(entryText(preset, "pA.g1.oct") == "-1");
            expect(entryText(preset, "pA.g1.fine") == "+6");
            expect(entryText(preset, "pA.p1.type") == "Ladder");
            expect(entryText(preset, "pA.p1.p1") == "800");
            expect(entryText(preset, "pA.p1.p5") == "lp12");    // mode is the fifth ladder parameter
            expect(entryText(preset, "pA.env1.attack") == "5");
            expect(entryText(preset, "pA.env1.loop") == "off");
            expect(entryText(preset, "pA.voice.glide") == "15");
            expect(entryText(preset, "pA.play.priority") == "low");
            expect(entryText(preset, "arpEnabled") == "on");
            expect(entryText(preset, "arpPattern") == "up/down");
            expect(entryText(preset, "driveAmount") == "0.15");
            expect(entryText(preset, "masterGainDb") == "-6");
            expect(preset.entries.front().parameterId == "pA.g1.type"); // the type comes before its parameters
        }

        beginTest("parse_reports_unknown_settings_with_file_and_line");
        {
            const auto parsed = parseBankFile(R"(room = Nowhere
[Broken]
g1 = Wavetable position=0.5
g2 = VA wobble=3
p1 = Ladder cutoff
envelope = attack=5
delay = maybe time=300
)", "broken.csbank");
            for (const auto& e : parsed.errors)
                logMessage(e);
            expectEquals(parsed.errors.size(), 6);
            expect(parsed.errors[0].startsWith("broken.csbank:1:"));
            expect(parsed.errors[1].contains(":3:") && parsed.errors[1].contains("Wavetable"));
            expect(parsed.errors[2].contains(":4:") && parsed.errors[2].contains("wobble"));
            expect(parsed.errors[3].contains(":5:"));
            expect(parsed.errors[4].contains(":6:") && parsed.errors[4].contains("envelope"));
            expect(parsed.errors[5].contains(":7:") && parsed.errors[5].contains("maybe"));
        }

        beginTest("apply_loads_type_defaults_and_reads_values_through_the_parameters");
        {
            const auto parsed = parseBankFile(R"([Layered]
room = Workshop
g2 = VA level=0.5
p1 = Ladder mode=hp12 cutoff=140 res=0.25
play = mode=legato voices=6
arp = on rate=1/8T
master = -7.5
)", "layered.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));

            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            expect(problems.isEmpty(), problems.joinIntoString("\n"));

            expectEquals(static_cast<int>(rawValue(*processor, "pA.g2.type")), 1);              // VA
            expectWithinAbsoluteError(rawValue(*processor, "pA.g2.p1"), 2.0f / 3.0f, 1.0e-5f);  // VA's default shape: saw, not the empty slot's 0
            expectWithinAbsoluteError(rawValue(*processor, "pA.g2.level"), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p2"), 0.25f, 1.0e-6f);        // res=0.25: a bare fraction, not a percent
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p5"), 1.0f, 1.0e-6f);          // HP12 is the last mode
            expectWithinAbsoluteError(rawValue(*processor, "pA.p1.p1"), std::log(140.0f / 20.0f) / std::log(1000.0f), 1.0e-4f);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.mode")), 2);            // Legato
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.voices")), 6);
            expectEquals(static_cast<int>(rawValue(*processor, "arpEnabled")), 1);
            expectEquals(static_cast<int>(rawValue(*processor, "arpRateDivision")), 2);         // 1/8T
            expectWithinAbsoluteError(rawValue(*processor, "masterGainDb"), -7.5f, 1.0e-3f);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.g1.type")), 1);              // untouched: back to its default
        }

        beginTest("apply_reports_values_it_cannot_read");
        {
            const auto parsed = parseBankFile(R"([Wrong]
g1 = VA shape=wobble
p1 = Ladder cutoff=50000
play = mode=sideways
env1 = loop=maybe attack=abc
)", "wrong.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            auto processor = std::make_unique<SynthAudioProcessor>();
            const auto problems = coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), parsed.presets.front());
            for (const auto& p : problems)
                logMessage(p);
            expectEquals(problems.size(), 5);
        }

        beginTest("the_bank_is_the_processors_program_list");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            expectEquals(processor->getNumPrograms(), coolsynth::presets::getFactoryPresetCount());
            int moogBass = -1;
            for (int i = 0; i < processor->getNumPrograms(); ++i)
                if (coolsynth::presets::getFactoryPreset(i).name == "Moog Bass")
                    moogBass = i;
            expect(moogBass >= 0);
            expect(processor->getProgramName(moogBass) == "Studio 1982: Moog Bass");
            processor->setCurrentProgram(moogBass);
            expectEquals(processor->getCurrentProgram(), moogBass);
            expectEquals(static_cast<int>(rawValue(*processor, "pA.play.mode")), 2); // legato
            expect(processor->getPatchName() == "Moog Bass");
        }

        beginTest("audition_editor_builds_and_lays_out");
        {
            auto processor = std::make_unique<SynthAudioProcessor>();
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                expect(editor != nullptr);
                expect(editor->getWidth() >= 560 && editor->getHeight() > 100);
                editor->setSize(900, 700);
                expect(editor->getNumChildComponents() >= 2); // the preset menu, the sliders, and the resize corner
            }
        }

        beginTest("every_factory_preset_sounds_and_stays_bounded");
        {
            for (int index = 0; index < coolsynth::presets::getFactoryPresetCount(); ++index)
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(index);
                auto processor = std::make_unique<SynthAudioProcessor>();
                processor->prepareToPlay(48000.0, 256);
                coolsynth::presets::applyFactoryPreset(processor->getValueTreeState(), preset);

                juce::AudioBuffer<float> buffer(2, 256);
                double sum = 0.0;
                float peak = 0.0f;
                bool finite = true;
                for (int block = 0; block < 188; ++block) // about one second
                {
                    juce::MidiBuffer midi;
                    if (block == 0)
                        for (int note : { 48, 55, 60 })
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
                    buffer.clear();
                    processor->processBlock(buffer, midi);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        for (int s = 0; s < 256; ++s)
                        {
                            const float v = buffer.getSample(ch, s);
                            finite = finite && std::isfinite(v);
                            peak = std::max(peak, std::abs(v));
                            sum += static_cast<double>(v) * static_cast<double>(v);
                        }
                    }
                }
                const float rms = static_cast<float>(std::sqrt(sum / (188.0 * 256.0 * 2.0)));
                expect(finite, preset.name + " produced a non-finite sample");
                expect(rms > 1.0e-3f, preset.name + " is silent: rms " + juce::String(rms, 6));
                expect(peak <= 1.0f, preset.name + " peaks at " + juce::String(peak, 3));
            }
        }
    }
};

static V3BankTests v3BankTests;
