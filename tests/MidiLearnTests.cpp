#include <array>
#include <memory>
#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "standalone/SettingsStore.h"
#include "synth/GlobalDelay.h"
#include "parameters/ParameterIDs.h"
#include "TestSampleLibrary.h"

#include <iostream>

using namespace coolsynth::midi;

namespace
{
    /** Puts an effect type into master slot f<slot> and sets the values given, in the type's units. */
    void setMasterSlot(juce::AudioProcessorValueTreeState& state, int slot, int type,
                       std::initializer_list<std::pair<int, float>> units = {})
    {
        const auto prefix = "f" + juce::String(slot);
        if (auto* typeParameter = state.getParameter(prefix + ".type"))
            typeParameter->setValueNotifyingHost(typeParameter->convertTo0to1(static_cast<float>(type)));
        const auto& d = coolsynth::synth::v3::insertDescriptor(type);
        for (const auto& [index, value] : units)
            if (auto* parameter = state.getParameter(prefix + ".p" + juce::String(index + 1)))
                parameter->setValueNotifyingHost(coolsynth::synth::v3::unitsToNormalised(d.params[static_cast<std::size_t>(index)], value));
    }
}

class StdoutLogger : public juce::Logger
{
protected:
    void logMessage(const juce::String& message) override
    {
        std::cout << message.toRawUTF8() << std::endl;
    }
};

class ProcessorSilenceTests final : public juce::UnitTest
{
public:
    ProcessorSilenceTests() : juce::UnitTest("Processor silence and timing", "CoolSynth") {}

    void runTest() override
    {
        beginTest("the_apps_settings_forget_v2s_midi_keys_and_keep_the_audio_device");
        {
            // D212: V2's MIDI input, MIDI learn and controller profiles are gone; what they kept goes with them.
            juce::PropertySet props;
            props.setValue("midiInputIdentifier", "minilab3-id");
            props.setValue("midiLearnMappings", "<MIDI_LEARN_MAPPINGS/>");
            props.setValue("controllerProfileId", "arturia.minilab3.arturia-mode.v1");
            props.setValue("showCcLabels", false);
            props.setValue("velocityCurve", 2);
            auto audioSetup = std::make_unique<juce::XmlElement>("DEVICESETUP");
            audioSetup->setAttribute("deviceType", "ASIO");
            audioSetup->setAttribute("audioOutputDeviceName", "UMC ASIO Driver");
            audioSetup->setAttribute("audioDeviceRate", 44100.0);
            audioSetup->setAttribute("audioDeviceBufferSize", 128);
            props.setValue("audioSetup", audioSetup.get());

            const coolsynth::standalone::StandaloneSettingsStore store(props);
            for (const char* key : { "midiInputIdentifier", "midiLearnMappings", "controllerProfileId", "showCcLabels" })
                expect(! props.containsKey(key), key);
            expectEquals(store.getVelocityCurve(), 2, "the app's own choices stay");
            const auto audio = store.loadPersistedAudioSelection();
            expect(audio.has_value() && audio->outputDeviceName == "UMC ASIO Driver" && audio->bufferSizeSamples == 128);
        }

        beginTest("plugin_process_block_treats_all_sound_off_as_immediate_engine_silence");
        {
            SynthAudioProcessor processor;
            processor.prepareToPlay(48000.0, 128);

            juce::AudioBuffer<float> buffer(2, 128);
            juce::MidiBuffer noteOn;
            noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            processor.processBlock(buffer, noteOn);

            juce::MidiBuffer allSoundOff;
            allSoundOff.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            buffer.clear();
            processor.processBlock(buffer, allSoundOff);

            float maxAbs = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    maxAbs = juce::jmax(maxAbs, std::abs(buffer.getSample(channel, sample)));
            }

            expectWithinAbsoluteError(maxAbs, 0.0f, 1.0e-6f);
        }

        beginTest("processor_reset_silences_allocator_path_and_clears_the_master_effects");
        {
            SynthAudioProcessor processor;
            processor.prepareToPlay(48000.0, 128);

            auto& state = processor.getValueTreeState();
            namespace types = coolsynth::synth::v3::insertType;
            setMasterSlot(state, 1, types::drive, { { 0, 1.0f }, { 1, 1.0f } });
            setMasterSlot(state, 2, types::chorus, { { 1, 1.0f }, { 2, 1.0f } });
            setMasterSlot(state, 3, types::delay, { { 0, 250.0f }, { 1, 0.85f }, { 2, 1.0f } });
            setMasterSlot(state, 4, types::reverb, { { 0, 1.0f }, { 2, 1.0f } });

            juce::AudioBuffer<float> buffer(2, 128);
            juce::MidiBuffer noteOn;
            noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            processor.processBlock(buffer, noteOn);
            juce::MidiBuffer emptyMidi;
            buffer.clear();
            processor.processBlock(buffer, emptyMidi);

            processor.reset();

            buffer.clear();
            processor.processBlock(buffer, emptyMidi);

            float maxAbs = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    maxAbs = juce::jmax(maxAbs, std::abs(buffer.getSample(channel, sample)));
            }

            expectWithinAbsoluteError(maxAbs, 0.0f, 1.0e-6f);
        }

        beginTest("processor_reports_the_longest_tail_of_its_master_effects");
        {
            SynthAudioProcessor processor;
            auto& state = processor.getValueTreeState();
            namespace types = coolsynth::synth::v3::insertType;

            const auto expectTailNear = [&](double expected, const char* what)
            {
                expectWithinAbsoluteError(processor.getTailLengthSeconds(), expected, 1.0e-4, what);
            };

            expectTailNear(0.0, "no effects");
            setMasterSlot(state, 1, types::delay, { { 0, 250.0f }, { 1, 0.85f }, { 2, 0.0f } });
            expectTailNear(0.0, "a delay mixed out");
            setMasterSlot(state, 1, types::delay, { { 1, 0.0f }, { 2, 1.0f } });
            expectTailNear(0.5, "one echo and the pad");
            setMasterSlot(state, 1, types::delay, { { 1, 0.85f } });
            {
                // Repeats until -60 dB, plus the pad; the time read back as the slot holds it.
                const auto& d = coolsynth::synth::v3::insertDescriptor(types::delay);
                const double delaySeconds = coolsynth::synth::v3::descriptorToUnits(d.params[0], state.getRawParameterValue("f1.p1")->load()) / 1000.0;
                const double feedback = coolsynth::synth::v3::descriptorToUnits(d.params[1], state.getRawParameterValue("f1.p2")->load());
                expectTailNear(delaySeconds * (std::log(0.001) / std::log(feedback)) + 0.25, "repeats");
            }
            setMasterSlot(state, 1, coolsynth::synth::v3::insertType::off);
            setMasterSlot(state, 6, types::reverb, { { 2, 1.0f } });
            expectTailNear(12.0, "a reverb in the last slot");
        }

        beginTest("processor_process_block_routes_note_events_through_the_allocator");
        {
            SynthAudioProcessor processor;
            processor.prepareToPlay(48000.0, 128);

            juce::AudioBuffer<float> buffer(2, 128);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 48);

            processor.processBlock(buffer, midi);

            float firstHalfMax = 0.0f;
            float secondHalfMax = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < 48; ++sample)
                    firstHalfMax = juce::jmax(firstHalfMax, std::abs(buffer.getSample(channel, sample)));

                for (int sample = 48; sample < buffer.getNumSamples(); ++sample)
                    secondHalfMax = juce::jmax(secondHalfMax, std::abs(buffer.getSample(channel, sample)));
            }

            expectWithinAbsoluteError(firstHalfMax, 0.0f, 1.0e-6f);
            expect(secondHalfMax > 1.0e-4f);
        }

    }
};

static ProcessorSilenceTests processorSilenceTests;

// Editor UX regressions (resizing, keyboard focus, undo, patch identity). Kept in
// their own runTest() frame: MSVC Debug reserves a stack slot for every
// block-scoped local of a function, and processors and editors are large.
class ProcessorUxTests final : public juce::UnitTest
{
public:
    ProcessorUxTests() : juce::UnitTest("ProcessorUx", "CoolSynth") {}

    void runTest() override
    {
        beginTest("processor_undo_and_redo_follow_gestures_and_bulk_edits");
        {
            auto processorHolder = std::make_unique<SynthAudioProcessor>();
            auto& processor = *processorHolder;
            auto& state = processor.getValueTreeState();
            auto* cutoff = state.getParameter("pA.p1.p1");
            auto* resonance = state.getParameter("pA.p1.p2");
            expect(cutoff != nullptr && resonance != nullptr);
            if (cutoff == nullptr || resonance == nullptr)
                return;

            expect(! processor.canUndo());
            expect(! processor.isPatchDirty());
            expectEquals(processor.getPatchName(), juce::String("Init"));

            // A gesture that changes nothing leaves no undo step.
            cutoff->beginChangeGesture();
            cutoff->endChangeGesture();
            expect(! processor.canUndo());

            // One UI gesture is one undo step and marks the patch dirty.
            const float cutoffBefore = cutoff->getValue();
            cutoff->beginChangeGesture();
            cutoff->setValueNotifyingHost(0.25f);
            cutoff->setValueNotifyingHost(0.3f);
            cutoff->endChangeGesture();
            expect(processor.canUndo());
            expect(processor.isPatchDirty());
            expectEquals(processor.getUndoLabel(), cutoff->getName(32));

            expect(processor.undo());
            expectWithinAbsoluteError(cutoff->getValue(), cutoffBefore, 0.0001f);
            expect(! processor.isPatchDirty());
            expect(processor.canRedo());

            expect(processor.redo());
            expectWithinAbsoluteError(cutoff->getValue(), 0.3f, 0.0001f);
            expect(processor.isPatchDirty());

            // A bulk edit (init, preset, load) is one step regardless of how many
            // parameters it touches, and the identity set afterwards is restored
            // together with the values.
            resonance->beginChangeGesture();
            resonance->setValueNotifyingHost(0.9f);
            resonance->endChangeGesture();

            processor.performUndoableEdit("Init patch", [&] { processor.resetAutomatableParametersToDefaults(); });
            processor.setPatchIdentity("Init", false);
            expectWithinAbsoluteError(cutoff->getValue(), cutoff->getDefaultValue(), 0.0001f);
            expectWithinAbsoluteError(resonance->getValue(), resonance->getDefaultValue(), 0.0001f);
            expectEquals(processor.getUndoLabel(), juce::String("Init patch"));
            expect(! processor.isPatchDirty());

            expect(processor.undo());
            expectWithinAbsoluteError(cutoff->getValue(), 0.3f, 0.0001f);
            expectWithinAbsoluteError(resonance->getValue(), 0.9f, 0.0001f);
            expect(processor.isPatchDirty());

            // A new edit after undo discards the redo branch.
            cutoff->beginChangeGesture();
            cutoff->setValueNotifyingHost(0.6f);
            cutoff->endChangeGesture();
            expect(! processor.canRedo());
        }

        beginTest("processor_state_round_trips_patch_identity");
        {
            auto sourceHolder = std::make_unique<SynthAudioProcessor>();
            auto& source = *sourceHolder;
            source.setPatchIdentity("Glass Pad", true);

            juce::MemoryBlock stateData;
            source.getStateInformation(stateData);

            auto targetHolder = std::make_unique<SynthAudioProcessor>();
            auto& target = *targetHolder;
            target.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
            expectEquals(target.getPatchName(), juce::String("Glass Pad"));
            expect(target.isPatchDirty());
            expect(! target.canUndo());
        }

    }
};

static ProcessorUxTests processorUxTests;

int main()
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::ScopedJuceInitialiser_GUI initialiser;
    // A crash prints where it happened (the thread's stack, with names when the .pdb is beside the runner).
    juce::SystemStats::setApplicationCrashHandler([](void*)
    {
        std::fputs(("\nCRASH\n" + juce::SystemStats::getStackBacktrace() + "\n").toRawUTF8(), stderr);
        std::fflush(stderr);
    });
    // No test reads the real sampled-instrument library: a small stand-in is written and used instead.
    coolsynth::testing::installTestSampleLibrary();
    juce::UnitTestRunner runner;
    // COOLSYNTH_TEST_ONLY=part of a test's name runs only the tests whose names contain it.
    if (const auto only = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_TEST_ONLY", {}); only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName().containsIgnoreCase(only))
                chosen.add(test);
        runner.runTests(chosen);
    }
    else
        runner.runAllTests();
    coolsynth::testing::removeTestSampleLibrary();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (auto* result = runner.getResult(i))
            failures += result->failures;
    }
    
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
