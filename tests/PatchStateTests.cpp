#include <juce_core/juce_core.h>

#include "plugin/SynthAudioProcessor.h"
#include "presets/PatchState.h"

class PatchStateTests final : public juce::UnitTest
{
public:
    PatchStateTests() : juce::UnitTest("PatchState", "CoolSynth") {}

    void runTest() override
    {
        beginTest("init_patch_resets_all_automatable_parameters_to_defaults");
        {
            SynthAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            apvts.getParameter("delayMix")->setValueNotifyingHost(1.0f);
            apvts.getParameter("masterGainDb")->setValueNotifyingHost(1.0f);

            processor.resetAutomatableParametersToDefaults();

            expectWithinAbsoluteError(apvts.getParameter("delayMix")->getValue(),
                                      apvts.getParameter("delayMix")->getDefaultValue(),
                                      0.0001f);
        }

        beginTest("patch_round_trip_restores_parameter_values_in_fresh_processor");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            auto stateXml = source.createParameterStateXml();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      source.getParameterStateTypeName());
            auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml,
                                                                   target.getParameterStateTypeName());

            expect(parsed.succeeded());
            expect(parsed.parameterStateXml != nullptr);
            expect(target.applyParameterStateXml(*parsed.parameterStateXml));
        }

        beginTest("patch_wrapper_contains_expected_root_and_single_apvts_child");
        {
            SynthAudioProcessor processor;
            auto stateXml = processor.createParameterStateXml();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      processor.getParameterStateTypeName());

            expect(patchXml->hasTagName(coolsynth::presets::patchRootTag));
            expectEquals(patchXml->getNumChildElements(), 1);
            expect(patchXml->getChildElement(0)->hasTagName(processor.getParameterStateTypeName()));
        }

        beginTest("patch_loader_rejects_wrong_root_or_missing_state_child");
        {
            juce::XmlElement wrongRoot("WRONG_ROOT");
            auto parsed1 = coolsynth::presets::parseWrappedPatchXml(wrongRoot, "CoolSynthState");
            expect(!parsed1.succeeded());
            expect(parsed1.error == coolsynth::presets::PatchStateError::invalidRootTag);

            juce::XmlElement goodRoot(coolsynth::presets::patchRootTag);
            goodRoot.setAttribute("formatVersion", 1);
            auto parsed2 = coolsynth::presets::parseWrappedPatchXml(goodRoot, "CoolSynthState");
            expect(!parsed2.succeeded());
            expect(parsed2.error == coolsynth::presets::PatchStateError::missingParameterState);
        }

        beginTest("patch_xml_excludes_standalone_and_midi_learn_keys");
        {
            SynthAudioProcessor processor;
            auto stateXml = processor.createParameterStateXml();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      processor.getParameterStateTypeName());
            auto xmlString = patchXml->toString();

            expect(!xmlString.contains("audioSetup"));
            expect(!xmlString.contains("midiInputIdentifier"));
            expect(!xmlString.contains("midiInputName"));
            expect(!xmlString.contains("midiLearnMappings"));
        }
    }
};

static PatchStateTests patchStateTests;
