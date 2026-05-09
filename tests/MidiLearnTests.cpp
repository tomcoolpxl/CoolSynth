#include <juce_core/juce_core.h>
#include "midi/MidiLearn.h"
#include "midi/MidiMappingEngine.h"
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
