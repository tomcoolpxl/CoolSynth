// The built VST3, hosted as a DAW hosts it (D228): found in its bundle, made, played, its state saved and given to a
// second one, which must sound the same bit for bit; bounced twice the same; its panel opened. The user's request
// after the DAW round of 2026-11-20: checks a program can make are not the player's to make.

#include <cmath>
#include <cstdlib>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "presets/FactoryPresets.h"
#include "synth/v3/SampleLibrary.h"

namespace
{
    /** The process's environment, which the plugin's own copy of the code reads too. */
    void setEnvironment(const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s(name, value.toRawUTF8());
       #else
        setenv(name, value.toRawUTF8(), 1);
       #endif
    }

    /** A chord held 0.4 s and let go, 0.8 s in all, as a host's bounce plays it. */
    juce::AudioBuffer<float> render(juce::AudioPluginInstance& plugin)
    {
        constexpr int block = 256, blocks = 188;
        juce::AudioBuffer<float> out(2, block * blocks);
        juce::AudioBuffer<float> buffer(juce::jmax(2, plugin.getTotalNumOutputChannels()), block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (int note : { 48, 55, 60, 64 })
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
            if (b == 94)
                for (int note : { 48, 55, 60, 64 })
                    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
            buffer.clear();
            plugin.processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom(ch, b * block, buffer, ch, 0, block);
        }
        return out;
    }

    float largestDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                worst = juce::jmax(worst, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
        return worst;
    }
}

class HostedPluginTests final : public juce::UnitTest
{
public:
    HostedPluginTests() : juce::UnitTest("Hosted VST3", "Plugin") {}

    void runTest() override
    {
        const auto path = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_VST3", COOLSYNTH_VST3_BUNDLE);
        beginTest("the built VST3 is found in its bundle");
        if (! juce::File(path).exists())
        {
            expect(false, "no VST3 at " + path + " (build CoolSynth_VST3)");
            return;
        }
        // The plugin's copy of the code: off the player's MiniLab and logs, on the tests' small sample library.
        setEnvironment("COOLSYNTH_TEST_HOST", "1");
        setEnvironment("COOLSYNTH_LIBRARY", coolsynth::synth::v3::SampleLibrary::instance().root().getFullPathName());

        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile(types, path);
        expectEquals(types.size(), 1, "one plugin in the bundle");
        if (types.isEmpty())
            return;
        const auto& type = *types[0];
        logMessage(type.name + " " + type.version + " by " + type.manufacturerName + ", " + type.pluginFormatName
                   + (type.isInstrument ? ", an instrument" : ", not an instrument") + ", category " + type.category);
        expect(type.isInstrument, "hosts list it as an instrument");
        expectEquals(type.name, juce::String("CoolSynth"));

        auto make = [&]
        {
            juce::String error;
            auto plugin = format.createInstanceFromDescription(type, 48000.0, 256, error);
            expect(plugin != nullptr, "made: " + error);
            if (plugin != nullptr)
            {
                plugin->enableAllBuses();
                plugin->setNonRealtime(true); // a bounce: it waits for its sounds, and no block is cut short
                plugin->prepareToPlay(48000.0, 256);
            }
            return plugin;
        };

        beginTest("made, played, a parameter moved by the host");
        auto first = make();
        if (first == nullptr)
            return;
        expectGreaterOrEqual(first->getTotalNumOutputChannels(), 2, "stereo out");
        const auto plain = render(*first);
        logMessage("Reese's chord: loudest " + juce::String(juce::Decibels::gainToDecibels(plain.getMagnitude(0, plain.getNumSamples())), 1) + " dBFS");
        expectGreaterThan(plain.getMagnitude(0, plain.getNumSamples()), 0.001f, "it plays");
        // The first parameter is layer A's first macro (D216), named after the start sound's macro (D248).
        juce::String reeseMacro;
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            if (coolsynth::presets::getFactoryPreset(i).name == "Reese")
                reeseMacro = coolsynth::presets::getFactoryPreset(i).macroNames[0][0];
        expect(reeseMacro.isNotEmpty(), "Reese names its macros");
        auto* macro = first->getParameters().isEmpty() ? nullptr : first->getParameters()[0];
        expect(macro != nullptr && macro->getName(64) == "A1 " + reeseMacro,
               "the host sees 'A1 " + reeseMacro + "', not '" + (macro != nullptr ? macro->getName(64) : juce::String()) + "'");
        if (macro != nullptr)
            macro->setValueNotifyingHost(0.8f);

        beginTest("bounced twice the same, and its state given to a second one sounds the same");
        first->reset();
        const auto once = render(*first);
        first->reset();
        const auto twice = render(*first);
        expectEquals(largestDifference(once, twice), 0.0f, "two bounces null");
        expectGreaterThan(largestDifference(once, plain), 0.0f, "the moved macro changed the sound");
        juce::MemoryBlock state;
        first->getStateInformation(state);
        logMessage("its state: " + juce::String(static_cast<int>(state.getSize())) + " bytes");
        auto second = make();
        if (second == nullptr)
            return;
        second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        second->reset();
        const auto reopened = render(*second);
        expectEquals(largestDifference(once, reopened), 0.0f, "the reopened one sounds the same, bit for bit");

        beginTest("its panel opens in the host");
        if (std::unique_ptr<juce::AudioProcessorEditor> editor { first->createEditorIfNeeded() }; editor != nullptr)
        {
            logMessage("panel " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            expect(editor->getWidth() > 400 && editor->getHeight() > 300, "a panel of a real size");
            const auto picture = editor->createComponentSnapshot(editor->getLocalBounds(), true, 0.5f);
            expect(picture.isValid());
        }
        else
            expect(false, "no panel");
        first->releaseResources();
        second->releaseResources();
    }
};

static HostedPluginTests hostedPluginTests;
