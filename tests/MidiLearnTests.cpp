#include <array>
#include <juce_core/juce_core.h>
#include "midi/MidiLearn.h"
#include "midi/ControllerProfile.h"
#include "midi/MidiMappingEngine.h"
#include "plugin/SynthAudioProcessor.h"
#include "standalone/SettingsStore.h"
#include "parameters/ParameterIDs.h"

#include <iostream>

using namespace coolsynth::midi;

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
        beginTest("learn_manager_accepts_only_the_ten_continuous_parameter_ids");
        {
            MidiLearnManager manager;
            expect(manager.isLearnableParameter(coolsynth::parameters::ids::filterCutoffHz));
            expect(manager.isLearnableParameter(coolsynth::parameters::ids::masterGainDb));
            expect(!manager.isLearnableParameter(coolsynth::parameters::ids::oscAWave));
            expect(!manager.isLearnableParameter("invalid_id"));
        }

        beginTest("learn_manager_rejects_note_events_without_mutating_bindings");
        {
            MidiLearnManager manager;
            manager.beginLearning(coolsynth::parameters::ids::filterCutoffHz, "Cutoff");
            
            ControllerMidiEvent noteOnEvent{ ControllerMidiEventType::noteOn, 1, 60, 100 };
            auto outcome = manager.handleIncomingEvent(noteOnEvent);
            
            expectEquals((int)outcome.result, (int)MidiLearnCaptureResult::rejectedNonCc);
            expect(manager.getSession().armed); // Stays armed
            expectEquals((int)manager.getBindings().size(), 0);
        }

        beginTest("learn_manager_rejects_reserved_performance_ccs_without_mutating_bindings");
        {
            MidiLearnManager manager;
            manager.beginLearning(coolsynth::parameters::ids::filterCutoffHz, "Cutoff");

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
            manager.beginLearning(coolsynth::parameters::ids::filterCutoffHz, "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 });
            
            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].cc.controllerNumber == 74);

            manager.beginLearning(coolsynth::parameters::ids::filterCutoffHz, "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 75, 64 }); // Re-learn
            
            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].cc.controllerNumber == 75);
        }

        beginTest("learn_manager_replaces_existing_binding_by_cc_signature");
        {
            MidiLearnManager manager;
            manager.beginLearning(coolsynth::parameters::ids::filterCutoffHz, "Cutoff");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 });

            manager.beginLearning(coolsynth::parameters::ids::filterResonance, "Resonance");
            manager.handleIncomingEvent({ ControllerMidiEventType::controlChange, 1, 74, 64 }); // Same CC

            expectEquals((int)manager.getBindings().size(), 1);
            expect(manager.getBindings()[0].parameterId == coolsynth::parameters::ids::filterResonance);
        }

        beginTest("settings_store_round_trips_learned_mapping_xml");
        {
            juce::PropertySet props;
            coolsynth::standalone::StandaloneSettingsStore store(props);
            
            std::vector<LearnedCcBinding> bindings = {
                { coolsynth::parameters::ids::filterCutoffHz, { 1, 74 } },
                { coolsynth::parameters::ids::delayMix, { 2, 83 } }
            };
            store.saveLearnedMidiMappings(bindings);
            
            auto loaded = store.loadLearnedMidiMappings();
            expectEquals((int)loaded.size(), 2);
            expect(loaded[0].parameterId == coolsynth::parameters::ids::filterCutoffHz);
            expect(loaded[1].parameterId == coolsynth::parameters::ids::delayMix);
            expect(loaded[1].cc.channel == 2);
            expect(loaded[1].cc.controllerNumber == 83);
        }

        beginTest("settings_store_drops_invalid_or_unknown_mapping_entries");
        {
            juce::PropertySet props;
            auto xml = std::make_unique<juce::XmlElement>("MIDI_LEARN_MAPPINGS");
            
            auto* valid = xml->createNewChildElement("MAPPING");
            valid->setAttribute("parameterId", coolsynth::parameters::ids::filterCutoffHz);
            valid->setAttribute("channel", 1);
            valid->setAttribute("controller", 74);

            auto* invalidParam = xml->createNewChildElement("MAPPING");
            invalidParam->setAttribute("parameterId", "unknownParam");
            invalidParam->setAttribute("channel", 1);
            invalidParam->setAttribute("controller", 75);

            auto* invalidCC = xml->createNewChildElement("MAPPING");
            invalidCC->setAttribute("parameterId", coolsynth::parameters::ids::filterResonance);
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
            expect(normalized[0].parameterId == coolsynth::parameters::ids::filterCutoffHz);
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
                { coolsynth::parameters::ids::filterCutoffHz, { 1, 74 } }
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
            auto* waveform = state.getParameter(coolsynth::parameters::ids::oscAWave);
            auto* cutoff = state.getParameter(coolsynth::parameters::ids::filterCutoffHz);

            expect(waveform != nullptr);
            expect(cutoff != nullptr);
            expect(processor.setActiveControllerProfile("arturia.minilab3.arturia-mode.v1"));

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 114, 63 });
            expectWithinAbsoluteError(waveform->getValue(), 0.5f, 0.001f);

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 74, 0 });
            expectWithinAbsoluteError(cutoff->getValue(), 0.0f, 0.001f);

            processor.handleStandaloneControllerEvent({ ControllerMidiEventType::controlChange, 1, 74, 127 });
            expectWithinAbsoluteError(cutoff->getValue(), 1.0f, 0.001f);
        }

        beginTest("plugin_process_block_applies_learned_cc_mappings_via_async_handoff");
        {
            SynthAudioProcessor processor;
            const std::array<LearnedCcBinding, 1> bindings {{
                { coolsynth::parameters::ids::filterCutoffHz, { 1, 74 } }
            }};
            processor.setLearnedMidiBindings(bindings);

            juce::AudioBuffer<float> buffer(2, 32);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 0);

            processor.processBlock(buffer, midi);
            juce::Thread::sleep(20);

            auto* cutoff = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::filterCutoffHz);
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
                { coolsynth::parameters::ids::filterCutoffHz, { 1, 123 } }
            }};
            processor.setLearnedMidiBindings(bindings);

            auto* cutoff = processor.getValueTreeState().getParameter(coolsynth::parameters::ids::filterCutoffHz);
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

        beginTest("processor_reset_silences_allocator_path_and_clears_full_fx_rack");
        {
            SynthAudioProcessor processor;
            processor.prepareToPlay(48000.0, 128);

            auto& state = processor.getValueTreeState();
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::driveEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* amount = state.getParameter(coolsynth::parameters::ids::driveAmount))
                amount->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::driveMix))
                mix->setValueNotifyingHost(1.0f);
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::chorusEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* depth = state.getParameter(coolsynth::parameters::ids::chorusDepth))
                depth->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::chorusMix))
                mix->setValueNotifyingHost(1.0f);
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::delayEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* feedback = state.getParameter(coolsynth::parameters::ids::delayFeedback))
                feedback->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::delayMix))
                mix->setValueNotifyingHost(1.0f);
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::reverbEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* size = state.getParameter(coolsynth::parameters::ids::reverbSize))
                size->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::reverbMix))
                mix->setValueNotifyingHost(1.0f);

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

        beginTest("processor_reports_tail_for_audible_delay_or_reverb_paths");
        {
            SynthAudioProcessor processor;
            auto& state = processor.getValueTreeState();

            const auto expectTailNear = [&](double expected)
            {
                expectWithinAbsoluteError(processor.getTailLengthSeconds(), expected, 1.0e-6);
            };

            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::delayEnabled))
                enabled->setValueNotifyingHost(0.0f);
            if (auto* feedback = state.getParameter(coolsynth::parameters::ids::delayFeedback))
                feedback->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::delayMix))
                mix->setValueNotifyingHost(1.0f);
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::reverbEnabled))
                enabled->setValueNotifyingHost(0.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::reverbMix))
                mix->setValueNotifyingHost(0.0f);
            expectTailNear(0.0);

            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::delayEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::delayMix))
                mix->setValueNotifyingHost(0.0f);
            expectTailNear(0.0);

            if (auto* mix = state.getParameter(coolsynth::parameters::ids::delayMix))
                mix->setValueNotifyingHost(1.0f);
            if (auto* feedback = state.getParameter(coolsynth::parameters::ids::delayFeedback))
                feedback->setValueNotifyingHost(0.0f);
            expectTailNear(0.5);

            if (auto* feedback = state.getParameter(coolsynth::parameters::ids::delayFeedback))
                feedback->setValueNotifyingHost(1.0f);
            expectTailNear(48.0);

            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::delayEnabled))
                enabled->setValueNotifyingHost(0.0f);
            if (auto* enabled = state.getParameter(coolsynth::parameters::ids::reverbEnabled))
                enabled->setValueNotifyingHost(1.0f);
            if (auto* mix = state.getParameter(coolsynth::parameters::ids::reverbMix))
                mix->setValueNotifyingHost(1.0f);
            expectTailNear(12.0);
        }

        beginTest("processor_process_block_routes_note_events_through_the_v2_allocator");
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
                { coolsynth::parameters::ids::filterCutoffHz, { 1, 74 } },
                { coolsynth::parameters::ids::delayMix, { 2, 83 } }
            }};
            source.setLearnedMidiBindings(bindings);

            juce::MemoryBlock state;
            source.getStateInformation(state);
            target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

            const auto restored = target.getLearnedMidiBindings();
            expectEquals((int) restored.size(), 2);

            if (restored.size() == 2)
            {
                expect(restored[0].parameterId == coolsynth::parameters::ids::filterCutoffHz);
                expectEquals((int) restored[0].cc.channel, 1);
                expectEquals((int) restored[0].cc.controllerNumber, 74);
                expect(restored[1].parameterId == coolsynth::parameters::ids::delayMix);
                expectEquals((int) restored[1].cc.channel, 2);
                expectEquals((int) restored[1].cc.controllerNumber, 83);
            }
        }
    }
};

static MidiLearnTests midiLearnTests;

int main()
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::UnitTestRunner runner;
    runner.runAllTests();
    
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (auto* result = runner.getResult(i))
            failures += result->failures;
    }
    
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
