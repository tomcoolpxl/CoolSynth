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
            expect(!manager.isLearnableParameter(coolsynth::parameters::ids::waveform));
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

        beginTest("processor_applies_factory_profile_cc_mappings");
        {
            SynthAudioProcessor processor;
            auto& state = processor.getValueTreeState();
            auto* waveform = state.getParameter(coolsynth::parameters::ids::waveform);
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
