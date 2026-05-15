#include <juce_core/juce_core.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/PatchState.h"

class PatchStateTests final : public juce::UnitTest
{
public:
    PatchStateTests() : juce::UnitTest("PatchState", "CoolSynth") {}

    void runTest() override
    {
        beginTest("v2_parameter_layout_registers_every_expected_id_exactly_once");
        {
            SynthAudioProcessor processor;
            juce::StringArray registeredIds;

            for (auto* parameterBase : processor.getParameters())
            {
                auto* parameter = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameterBase);
                expect(parameter != nullptr);

                if (parameter == nullptr)
                    continue;

                expect(!registeredIds.contains(parameter->paramID));
                registeredIds.add(parameter->paramID);
            }

            expectEquals(registeredIds.size(), static_cast<int>(coolsynth::parameters::allParameterIds.size()));

            for (const auto* expectedId : coolsynth::parameters::allParameterIds)
                expect(registeredIds.contains(expectedId));
        }

        beginTest("init_patch_resets_all_automatable_parameters_to_defaults");
        {
            SynthAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            apvts.getParameter("delayMix")->setValueNotifyingHost(1.0f);
            apvts.getParameter("masterGainDb")->setValueNotifyingHost(1.0f);
            apvts.getParameter(coolsynth::parameters::ids::oscBFineCents)->setValueNotifyingHost(0.0f);
            apvts.getParameter(coolsynth::parameters::ids::filterEnvAmount)->setValueNotifyingHost(0.0f);

            processor.resetAutomatableParametersToDefaults();

            expectWithinAbsoluteError(apvts.getParameter("delayMix")->getValue(),
                                      apvts.getParameter("delayMix")->getDefaultValue(),
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue(coolsynth::parameters::ids::filterCutoffHz)->load(),
                                      3200.0f,
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue(coolsynth::parameters::ids::filterEnvAmount)->load(),
                                      0.35f,
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue(coolsynth::parameters::ids::oscBFineCents)->load(),
                                      4.0f,
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue(coolsynth::parameters::ids::ampSustain)->load(),
                                      0.72f,
                                      0.0001f);
        }

        beginTest("patch_round_trip_restores_parameter_values_in_fresh_processor");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            auto& sourceState = source.getValueTreeState();
            sourceState.getParameter(coolsynth::parameters::ids::oscAWave)->setValueNotifyingHost(0.0f);
            sourceState.getParameter(coolsynth::parameters::ids::filterEnvAmount)->setValueNotifyingHost(0.75f);
            sourceState.getParameter(coolsynth::parameters::ids::arpEnabled)->setValueNotifyingHost(1.0f);
            sourceState.getParameter(coolsynth::parameters::ids::reverbMix)->setValueNotifyingHost(0.35f);

            auto stateXml = source.createParameterStateXml();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      source.getParameterStateTypeName());
            auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml,
                                                                   target.getParameterStateTypeName());

            expect(parsed.succeeded());
            expect(parsed.parameterStateXml != nullptr);
            expect(target.applyParameterStateXml(*parsed.parameterStateXml));
            expectWithinAbsoluteError(target.getValueTreeState().getParameter(coolsynth::parameters::ids::oscAWave)->getValue(),
                                      0.0f,
                                      0.0001f);
            expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue(coolsynth::parameters::ids::filterEnvAmount)->load(),
                                      0.75f,
                                      0.0001f);
            expectWithinAbsoluteError(target.getValueTreeState().getParameter(coolsynth::parameters::ids::arpEnabled)->getValue(),
                                      1.0f,
                                      0.0001f);
            expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue(coolsynth::parameters::ids::reverbMix)->load(),
                                      0.35f,
                                      0.0001f);
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

        beginTest("patch_loader_sanitizes_extra_root_properties_and_unknown_children");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            auto& sourceState = source.getValueTreeState();
            sourceState.getParameter(coolsynth::parameters::ids::delayMix)->setValueNotifyingHost(0.65f);
            sourceState.getParameter(coolsynth::parameters::ids::driveEnabled)->setValueNotifyingHost(1.0f);

            auto stateXml = source.createParameterStateXml();
            expect(stateXml != nullptr);

            stateXml->setAttribute("midiInputIdentifier", "should-not-load");
            auto* standaloneOnlyChild = stateXml->createNewChildElement("STANDALONE_ONLY");
            standaloneOnlyChild->setAttribute("value", "bad");

            expect(target.applyParameterStateXml(*stateXml));

            auto& targetState = target.getValueTreeState();
            expectWithinAbsoluteError(targetState.getRawParameterValue(coolsynth::parameters::ids::delayMix)->load(),
                                      0.65f,
                                      0.0001f);
            expectWithinAbsoluteError(targetState.getParameter(coolsynth::parameters::ids::driveEnabled)->getValue(),
                                      1.0f,
                                      0.0001f);

            auto sanitizedXml = target.createParameterStateXml();
            auto xmlString = sanitizedXml->toString();
            expect(!xmlString.contains("midiInputIdentifier"));
            expect(!xmlString.contains("STANDALONE_ONLY"));
        }

        beginTest("patch_loader_rejects_duplicate_parameter_ids");
        {
            SynthAudioProcessor processor;
            auto stateXml = processor.createParameterStateXml();
            expect(stateXml != nullptr);

            auto* firstChild = stateXml->getChildElement(0);
            expect(firstChild != nullptr);

            if (firstChild != nullptr)
            {
                auto* duplicateChild = new juce::XmlElement(*firstChild);
                duplicateChild->setAttribute("value", 0.99f);
                stateXml->addChildElement(duplicateChild);
            }

            expect(!processor.applyParameterStateXml(*stateXml));
        }

        beginTest("patch_file_write_and_read_round_trip_succeeds");
        {
            SynthAudioProcessor source;
            auto& sourceState = source.getValueTreeState();
            sourceState.getParameter(coolsynth::parameters::ids::arpEnabled)->setValueNotifyingHost(1.0f);
            sourceState.getParameter(coolsynth::parameters::ids::delayMix)->setValueNotifyingHost(0.42f);

            auto stateXml = source.createParameterStateXml();
            expect(stateXml != nullptr);

            auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                      source.getParameterStateTypeName());
            expect(patchXml != nullptr);

            auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("CoolSynthPatchStateTests");
            tempDir.createDirectory();
            auto patchFile = tempDir.getChildFile("round-trip-test" + juce::String(coolsynth::presets::defaultPatchExtension));
            patchFile.deleteFile();

            auto writeResult = coolsynth::presets::writePatchFile(patchFile, *patchXml);
            expect(writeResult.succeeded());
            expect(patchFile.existsAsFile());

            auto readResult = coolsynth::presets::readPatchFile(patchFile,
                                                                source.getParameterStateTypeName());
            expect(readResult.succeeded());
            expect(readResult.parameterStateXml != nullptr);

            if (readResult.parameterStateXml != nullptr)
            {
                SynthAudioProcessor target;
                expect(target.applyParameterStateXml(*readResult.parameterStateXml));
                expectWithinAbsoluteError(target.getValueTreeState().getParameter(coolsynth::parameters::ids::arpEnabled)->getValue(),
                                          1.0f,
                                          0.0001f);
                expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue(coolsynth::parameters::ids::delayMix)->load(),
                                          0.42f,
                                          0.0001f);
            }

            patchFile.deleteFile();
            tempDir.deleteRecursively();
        }
    }
};

static PatchStateTests patchStateTests;
