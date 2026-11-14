#include <array>
#include <memory>
#include <cmath>
#include <juce_core/juce_core.h>
#include "midi/MidiLearn.h"
#include "midi/ControllerProfile.h"
#include "midi/MidiMappingEngine.h"
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

class MidiLearnTests final : public juce::UnitTest
{
public:
    MidiLearnTests() : juce::UnitTest("MidiLearn", "CoolSynth") {}

    void runTest() override
    {
        beginTest("learn_manager_accepts_part_and_fixed_parameters_across_continuous_and_discrete_controls");
        {
            MidiLearnManager manager;
            expect(manager.isLearnableParameter("pA.p1.p1"));
            expect(manager.isLearnableParameter(coolsynth::parameters::ids::masterGainDb));
            expect(manager.isLearnableParameter("pA.g1.type"));
            expect(manager.isLearnableParameter(coolsynth::parameters::ids::arpEnabled));
            expect(manager.isLearnableParameter("pA.play.mode"));
            expect(!manager.isLearnableParameter("invalid_id"));
        }

        beginTest("learn_manager_rejects_note_events_without_mutating_bindings");
        {
            MidiLearnManager manager;
            manager.beginLearning("pA.p1.p1", "Cutoff");
            
            ControllerMidiEvent noteOnEvent{ ControllerMidiEventType::noteOn, 1, 60, 100 };
            auto outcome = manager.handleIncomingEvent(noteOnEvent);
            
            expectEquals((int)outcome.result, (int)MidiLearnCaptureResult::rejectedNonCc);
            expect(manager.getSession().armed); // Stays armed
            expectEquals((int)manager.getBindings().size(), 0);
        }

        beginTest("learn_manager_rejects_reserved_performance_ccs_without_mutating_bindings");
        {
            MidiLearnManager manager;
            manager.beginLearning("pA.p1.p1", "Cutoff");

            auto modWheelOutcome = manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 1, 100 });
            expectEquals((int)modWheelOutcome.result, (int)MidiLearnCaptureResult::ignored);
            expect(manager.getSession().armed);
            expectEquals((int)manager.getBindings().size(), 0);

            auto sustainOutcome = manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 64, 127 });
            expectEquals((int)sustainOutcome.result, (int)MidiLearnCaptureResult::ignored);
            expect(manager.getSession().armed);
            expectEquals((int)manager.getBindings().size(), 0);

            auto allNotesOffOutcome = manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 123, 0 });
            expectEquals((int)allNotesOffOutcome.result, (int)MidiLearnCaptureResult::ignored);
            expect(manager.getSession().armed);
            expectEquals((int)manager.getBindings().size(), 0);
        }

        beginTest("learn_manager_replaces_existing_binding_by_parameter");
        {
            MidiLearnManager manager;
            manager.beginLearning("pA.p1.p1", "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 });
            
            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].cc.controllerNumber == 74);

            manager.beginLearning("pA.p1.p1", "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 75, 64 }); // Re-learn
            
            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].cc.controllerNumber == 75);
        }

        beginTest("learn_manager_replaces_existing_binding_by_cc_signature");
        {
            MidiLearnManager manager;
            manager.beginLearning("pA.p1.p1", "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 });

            manager.beginLearning("pA.p1.p2", "Resonance");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 }); // Same CC

            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].parameterId == "pA.p1.p2");
        }

        beginTest("settings_store_round_trips_learned_mapping_xml");
        {
            juce::PropertySet props;
            coolsynth::standalone::StandaloneSettingsStore store(props);
            
            std::vector<LearnedCcBinding> bindings = {
                { "pA.p1.p1", { 1, 74 } },
                { "pA.g1.type", { 2, 83 } }
            };
            store.saveLearnedMidiMappings(bindings);
            
            auto loaded = store.loadLearnedMidiMappings();
            expectEquals((int)loaded.size(), 2);
            expect(loaded[0].parameterId == "pA.p1.p1");
            expect(loaded[1].parameterId == "pA.g1.type");
            expect(loaded[1].cc.channel == 2);
            expect(loaded[1].cc.controllerNumber == 83);
        }

        beginTest("settings_store_drops_invalid_or_unknown_mapping_entries");
        {
            juce::PropertySet props;
            auto xml = std::make_unique<juce::XmlElement>("MIDI_LEARN_MAPPINGS");
            
            auto* valid = xml->createNewChildElement("MAPPING");
            valid->setAttribute("parameterId", "pA.p1.p1");
            valid->setAttribute("channel", 1);
            valid->setAttribute("controller", 74);

            auto* invalidParam = xml->createNewChildElement("MAPPING");
            invalidParam->setAttribute("parameterId", "unknownParam");
            invalidParam->setAttribute("channel", 1);
            invalidParam->setAttribute("controller", 75);

            auto* invalidCC = xml->createNewChildElement("MAPPING");
            invalidCC->setAttribute("parameterId", "pA.p1.p2");
            invalidCC->setAttribute("channel", 1);
            invalidCC->setAttribute("controller", 200); // Invalid CC

            props.setValue("midiLearnMappings", xml.get());

            coolsynth::standalone::StandaloneSettingsStore store(props);
            auto loaded = store.loadLearnedMidiMappings();
            
            MidiLearnManager manager;
            manager.replaceBindings(loaded);
            auto normalized = manager.getBindings();
            
            // Should only keep the valid one
            expectEquals((int)normalized.size(), 1);
            expect(normalized[0].parameterId == "pA.p1.p1");
        }

        beginTest("controller_profile_registry_loads_builtin_minilab_profile");
        {
            const auto& registry = coolsynth::midi::ControllerProfileRegistry::get();
            const auto* profile = registry.findProfileById("arturia.minilab3.arturia-mode.v1");

            expect(profile != nullptr);
            expect(profile != nullptr && profile->displayName == "MiniLab 3 / Arturia Mode");
            expectEquals(registry.findBestProfileIdForDevice({ "MiniLab 3 MIDI", "test-id" }),
                         juce::String("arturia.minilab3.arturia-mode.v1"));
        }

        beginTest("settings_store_round_trips_controller_profile_selection");
        {
            juce::PropertySet props;
            coolsynth::standalone::StandaloneSettingsStore store(props);

            store.savePersistedControllerProfileSelection({});
            auto autoSelection = store.loadPersistedControllerProfileSelection();
            expectEquals((int) autoSelection.mode,
                         (int) coolsynth::standalone::ControllerProfileSelectionMode::autoDetect);
            expect(autoSelection.profileId.isEmpty());

            store.savePersistedControllerProfileSelection(
                { coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile,
                  "arturia.minilab3.arturia-mode.v1" });
            auto explicitSelection = store.loadPersistedControllerProfileSelection();
            expectEquals((int) explicitSelection.mode,
                         (int) coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile);
            expect(explicitSelection.profileId == "arturia.minilab3.arturia-mode.v1");

            store.savePersistedControllerProfileSelection(
                { coolsynth::standalone::ControllerProfileSelectionMode::none, {} });
            auto noneSelection = store.loadPersistedControllerProfileSelection();
            expectEquals((int) noneSelection.mode,
                         (int) coolsynth::standalone::ControllerProfileSelectionMode::none);
            expect(noneSelection.profileId.isEmpty());
        }

        beginTest("settings_store_clears_all_standalone_midi_state");
        {
            juce::PropertySet props;
            coolsynth::standalone::StandaloneSettingsStore store(props);

            juce::MidiDeviceInfo midiDevice;
            midiDevice.identifier = "minilab3-id";
            midiDevice.name = "MiniLab 3 MIDI";
            store.savePersistedMidiInputSelection(midiDevice);
            store.savePersistedControllerProfileSelection(
                { coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile,
                  "arturia.minilab3.arturia-mode.v1" });
            store.setShowCcLabels(false);

            std::vector<LearnedCcBinding> bindings = {
                { "pA.p1.p1", { 1, 74 } }
            };
            store.saveLearnedMidiMappings(bindings);

            auto audioSetup = std::make_unique<juce::XmlElement>("DEVICESETUP");
            audioSetup->setAttribute("deviceType", "Windows Audio");
            audioSetup->setAttribute("audioOutputDeviceName", "Speakers");
            audioSetup->setAttribute("audioDeviceRate", 48000.0);
            audioSetup->setAttribute("audioDeviceBufferSize", 480);
            auto* midiInput = audioSetup->createNewChildElement("MIDIINPUT");
            midiInput->setAttribute("name", "MiniLab 3 MIDI");
            midiInput->setAttribute("identifier", "minilab3-id");
            props.setValue("audioSetup", audioSetup.get());

            store.clearStandaloneMidiState();

            expect(!props.containsKey("midiInputIdentifier"));
            expect(!props.containsKey("midiInputName"));
            expect(!props.containsKey("midiLearnMappings"));
            expect(!props.containsKey("controllerProfileSelectionMode"));
            expect(!props.containsKey("controllerProfileId"));
            expect(!props.containsKey("showCcLabels"));

            expect(!store.loadPersistedMidiInputSelection().has_value());
            expectEquals((int) store.loadLearnedMidiMappings().size(), 0);

            const auto selection = store.loadPersistedControllerProfileSelection();
            expectEquals((int) selection.mode,
                         (int) coolsynth::standalone::ControllerProfileSelectionMode::autoDetect);
            expect(selection.profileId.isEmpty());
            expect(store.getShowCcLabels());

            auto cleanedAudioSetup = props.getXmlValue("audioSetup");
            expect(cleanedAudioSetup != nullptr);
            expect(cleanedAudioSetup != nullptr && cleanedAudioSetup->hasTagName("DEVICESETUP"));
            expect(cleanedAudioSetup != nullptr && cleanedAudioSetup->getChildByName("MIDIINPUT") == nullptr);
        }

        beginTest("processor_applies_factory_profile_cc_mappings");
        {
            SynthAudioProcessor processor;
            auto& state = processor.getValueTreeState();
            auto* waveform = state.getParameter("pA.g1.p1"); // G1 shape, a continuous morph
            auto* cutoff = state.getParameter("pA.p1.p1");
            auto* oscALevel = state.getParameter("pA.g1.level");
            auto* oscBLevel = state.getParameter("pA.g2.level");

            expect(waveform != nullptr);
            expect(cutoff != nullptr);
            expect(oscALevel != nullptr);
            expect(oscBLevel != nullptr);
            expect(processor.setActiveControllerProfile("arturia.minilab3.arturia-mode.v1"));

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 114, 63 });
            expectWithinAbsoluteError(waveform->getValue(), 2.0f / 3.0f - 1.0f / 64.0f, 0.001f); // one detent down from saw

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 74, 0 });
            expectWithinAbsoluteError(cutoff->getValue(), 0.0f, 0.001f);

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 74, 127 });
            expectWithinAbsoluteError(cutoff->getValue(), 1.0f, 0.001f);

            const auto initialOscALevel = oscALevel != nullptr ? oscALevel->getValue() : 0.0f;
            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 82, 64 });
            expectWithinAbsoluteError(oscALevel->getValue(), initialOscALevel, 0.001f);
            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 82, 127 });
            expectWithinAbsoluteError(oscALevel->getValue(), 1.0f, 0.001f);

            const auto initialOscBLevel = oscBLevel != nullptr ? oscBLevel->getValue() : 0.0f;
            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 83, 32 });
            expectWithinAbsoluteError(oscBLevel->getValue(), initialOscBLevel, 0.001f);
            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 83, 0 });
            expectWithinAbsoluteError(oscBLevel->getValue(), 0.0f, 0.001f);
        }

        beginTest("learned_bindings_shadow_factory_profile_signatures");
        {
            SynthAudioProcessor processor;
            auto& state = processor.getValueTreeState();
            auto* cutoff = state.getParameter("pA.p1.p1");
            auto* oscBLevel = state.getParameter("pA.g2.level");

            expect(cutoff != nullptr);
            expect(oscBLevel != nullptr);
            expect(processor.setActiveControllerProfile("arturia.minilab3.arturia-mode.v1"));

            const auto initialOscBLevel = oscBLevel != nullptr ? oscBLevel->getValue() : 0.0f;

            const std::array<LearnedCcBinding, 1> bindings {{
                { "pA.p1.p1", { 1, 83 } }
            }};
            processor.setLearnedMidiBindings(bindings);

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 83, 127 });

            expectWithinAbsoluteError(cutoff->getValue(), 1.0f, 0.001f);
            expectWithinAbsoluteError(oscBLevel->getValue(), initialOscBLevel, 1.0e-6f);
        }

        beginTest("plugin_process_block_applies_learned_cc_mappings_via_async_handoff");
        {
            SynthAudioProcessor processor;
            const std::array<LearnedCcBinding, 1> bindings {{
                { "pA.p1.p1", { 1, 74 } }
            }};
            processor.setLearnedMidiBindings(bindings);

            juce::AudioBuffer<float> buffer(2, 32);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 0);

            processor.processBlock(buffer, midi);
            processor.flushMappedControllerEventsSync();

            auto* cutoff = processor.getValueTreeState().getParameter("pA.p1.p1");
            expect(cutoff != nullptr);
            expectWithinAbsoluteError(cutoff->getValue(), 1.0f, 0.001f);

            std::array<ControllerMidiEvent, 4> drained {};
            const auto numDrained = processor.drainPendingPluginControllerEvents(drained.data(),
                                                                                 static_cast<int>(drained.size()));
            expectEquals(numDrained, 1);
            if (numDrained == 1)
            {
                expectEquals((int) drained[0].type, (int) ControllerMidiEventType::controlChange);
                expectEquals((int) drained[0].channel, 1);
                expectEquals((int) drained[0].data1, 74);
                expectEquals((int) drained[0].data2, 127);
            }
        }

        beginTest("plugin_controller_event_queue_survives_fifo_wraparound");
        {
            SynthAudioProcessor processor;
            juce::AudioBuffer<float> buffer(2, 8);

            for (int value = 0; value < 96; ++value)
            {
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, value), 0);
                processor.processBlock(buffer, midi);
            }

            std::array<ControllerMidiEvent, 80> drainedFirst {};
            const auto firstCount = processor.drainPendingPluginControllerEvents(drainedFirst.data(),
                                                                                 static_cast<int>(drainedFirst.size()));
            expectEquals(firstCount, 80);

            for (int value = 0; value < 64; ++value)
            {
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, value), 0);
                processor.processBlock(buffer, midi);
            }

            std::array<ControllerMidiEvent, 80> drainedSecond {};
            const auto secondCount = processor.drainPendingPluginControllerEvents(drainedSecond.data(),
                                                                                  static_cast<int>(drainedSecond.size()));
            expectEquals(secondCount, 80);

            if (secondCount == 80)
            {
                for (int i = 0; i < 16; ++i)
                    expectEquals((int) drainedSecond[static_cast<size_t>(i)].data2, 80 + i);

                for (int i = 0; i < 64; ++i)
                    expectEquals((int) drainedSecond[static_cast<size_t>(16 + i)].data2, i);
            }
        }

        beginTest("plugin_process_block_ignores_reserved_host_safety_cc_mappings");
        {
            SynthAudioProcessor processor;
            const std::array<LearnedCcBinding, 1> bindings {{
                { "pA.p1.p1", { 1, 123 } }
            }};
            processor.setLearnedMidiBindings(bindings);

            auto* cutoff = processor.getValueTreeState().getParameter("pA.p1.p1");
            expect(cutoff != nullptr);
            const auto initialValue = cutoff != nullptr ? cutoff->getValue() : 0.0f;

            juce::AudioBuffer<float> buffer(2, 32);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);

            processor.processBlock(buffer, midi);
            juce::Thread::sleep(20);

            if (cutoff != nullptr)
                expectWithinAbsoluteError(cutoff->getValue(), initialValue, 1.0e-6f);
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

        beginTest("plugin_state_round_trip_restores_learned_midi_bindings");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            const std::array<LearnedCcBinding, 2> bindings {{
                { "pA.p1.p1", { 1, 74 } },
                { "pA.g1.type", { 2, 83 } }
            }};
            source.setLearnedMidiBindings(bindings);

            juce::MemoryBlock state;
            source.getStateInformation(state);
            target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

            const auto restored = target.getLearnedMidiBindings();
            expectEquals((int) restored.size(), 2);

            if (restored.size() == 2)
            {
                expect(restored[0].parameterId == "pA.p1.p1");
                expectEquals((int) restored[0].cc.channel, 1);
                expectEquals((int) restored[0].cc.controllerNumber, 74);
                expect(restored[1].parameterId == "pA.g1.type");
                expectEquals((int) restored[1].cc.channel, 2);
                expectEquals((int) restored[1].cc.controllerNumber, 83);
            }
        }

    }
};

static MidiLearnTests midiLearnTests;

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
    // No test reads the real sampled-instrument library: a small stand-in is written and used instead.
    coolsynth::testing::installTestSampleLibrary();
    juce::UnitTestRunner runner;
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
