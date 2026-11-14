#include <cmath>
#include <juce_core/juce_core.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "presets/PatchState.h"

class AudioProcessorXmlHarness final : public juce::AudioProcessor
{
public:
    static void writeXmlToBinary(const juce::XmlElement& xml, juce::MemoryBlock& destData)
    {
        copyXmlToBinary(xml, destData);
    }

    static std::unique_ptr<juce::XmlElement> readXmlFromBinary(const void* data, int sizeInBytes)
    {
        return getXmlFromBinary(data, sizeInBytes);
    }

    const juce::String getName() const override { return "AudioProcessorXmlHarness"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

class PatchStateTests final : public juce::UnitTest
{
public:
    PatchStateTests() : juce::UnitTest("PatchState", "CoolSynth") {}

    void runTest() override
    {
        beginTest("parameter_layout_registers_every_expected_id_exactly_once");
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

            // each part: 3 generators x 21, 3 processors x 13, 3 envelopes x 13, 4 LFOs x 9, 24 matrix slots x 5,
            // global envelope 12, step 16 + 16 + 4, 4 macros, chaos 1, 5 voice, 8 play, 8 chord, 10 mix, 3 inserts x 13;
            // then the part mode
            constexpr int onePart = 3 * 21 + 3 * 13 + 3 * 13 + 4 * 9 + 24 * 5 + 12 + 36 + 4 + 1 + 5 + 8 + 8 + 10 + 3 * 13;
            constexpr int partParameterCount = 2 * onePart + 1;
            expectEquals(registeredIds.size(), static_cast<int>(coolsynth::parameters::allParameterIds.size()) + partParameterCount);

            for (const auto* expectedId : coolsynth::parameters::allParameterIds)
                expect(registeredIds.contains(expectedId));
            for (const auto* partId : { "pA.g1.type", "pA.g3.amount", "pA.p1.p8", "pA.env3.oneShot", "pA.lfo2.oneShot", "pA.mod24.curve", "pA.mod1.src", "pA.genv.trigger", "pA.step.slide16", "pA.macro4", "pA.chaos.rate", "pA.glfo1.division", "pA.voice.wheelToLfo1", "pA.voice.drift", "pA.play.glideLegato" })
                expect(registeredIds.contains(partId), juce::String("missing ") + partId);
        }

        beginTest("init_patch_resets_all_automatable_parameters_to_defaults");
        {
            SynthAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            apvts.getParameter("delayMix")->setValueNotifyingHost(1.0f);
            apvts.getParameter("masterGainDb")->setValueNotifyingHost(1.0f);
            apvts.getParameter("pA.g2.fine")->setValueNotifyingHost(0.0f);
            apvts.getParameter("pA.voice.env2ToP1")->setValueNotifyingHost(0.0f);

            processor.resetAutomatableParametersToDefaults();

            expectWithinAbsoluteError(apvts.getParameter("delayMix")->getValue(),
                                      apvts.getParameter("delayMix")->getDefaultValue(),
                                      0.0001f);
            // P1 defaults to the ladder: its cutoff parameter stores 3200 Hz in normalised form.
            expectWithinAbsoluteError(apvts.getRawParameterValue("pA.p1.p1")->load(),
                                      std::log(3200.0f / 20.0f) / std::log(1000.0f),
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue("pA.voice.env2ToP1")->load(),
                                      0.2f,
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue("pA.g2.fine")->load(),
                                      0.0f,
                                      0.0001f);
            expectWithinAbsoluteError(apvts.getRawParameterValue("pA.env1.sustain")->load(),
                                      1.0f,
                                      0.0001f);
        }

        beginTest("patch_round_trip_restores_parameter_values_in_fresh_processor");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            auto& sourceState = source.getValueTreeState();
            sourceState.getParameter("pA.g1.type")->setValueNotifyingHost(0.0f);
            sourceState.getParameter("pA.voice.env2ToP1")->setValueNotifyingHost(0.75f);
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
            expectWithinAbsoluteError(target.getValueTreeState().getParameter("pA.g1.type")->getValue(),
                                      0.0f,
                                      0.0001f);
            expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue("pA.voice.env2ToP1")->load(),
                                      0.5f, // normalised 0.75 on a -1..1 range
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
            goodRoot.setAttribute("formatVersion", coolsynth::presets::patchFormatVersion);
            goodRoot.setAttribute("stateType", "CoolSynthState");
            auto parsed2 = coolsynth::presets::parseWrappedPatchXml(goodRoot, "CoolSynthState");
            expect(!parsed2.succeeded());
            expect(parsed2.error == coolsynth::presets::PatchStateError::missingParameterState);
        }

        beginTest("patch_loader_rejects_legacy_version_and_wrong_state_type");
        {
            SynthAudioProcessor processor;
            auto stateXml = processor.createParameterStateXml();
            expect(stateXml != nullptr);

            if (stateXml != nullptr)
            {
                auto legacyPatchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                                processor.getParameterStateTypeName());
                legacyPatchXml->setAttribute("formatVersion", coolsynth::presets::patchFormatVersion - 1);
                auto parsedLegacy = coolsynth::presets::parseWrappedPatchXml(*legacyPatchXml,
                                                                             processor.getParameterStateTypeName());
                expect(!parsedLegacy.succeeded());
                expect(parsedLegacy.error == coolsynth::presets::PatchStateError::unsupportedFormatVersion);

                auto wrongStateTypePatchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                                        processor.getParameterStateTypeName());
                wrongStateTypePatchXml->setAttribute("stateType", "LegacyCoolSynthState");
                auto parsedWrongStateType = coolsynth::presets::parseWrappedPatchXml(*wrongStateTypePatchXml,
                                                                                     processor.getParameterStateTypeName());
                expect(!parsedWrongStateType.succeeded());
                expect(parsedWrongStateType.error == coolsynth::presets::PatchStateError::unexpectedStateType);
            }
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

        beginTest("patch_loader_rejects_incomplete_or_partial_overlap_parameter_state");
        {
            SynthAudioProcessor processor;
            auto fullStateXml = processor.createParameterStateXml();
            expect(fullStateXml != nullptr);

            if (fullStateXml != nullptr)
            {
                auto incompleteStateXml = std::make_unique<juce::XmlElement>(*fullStateXml);
                incompleteStateXml->removeChildElement(incompleteStateXml->getChildElement(0), true);
                expect(!processor.applyParameterStateXml(*incompleteStateXml));
            }

            juce::XmlElement partialOverlapState(processor.getParameterStateTypeName());
            auto* child = partialOverlapState.createNewChildElement("PARAM");
            child->setAttribute("id", "pA.p1.p1");
            child->setAttribute("value", 250.0f);

            expect(!processor.applyParameterStateXml(partialOverlapState));
            expectWithinAbsoluteError(processor.getValueTreeState().getRawParameterValue("pA.p1.p1")->load(),
                                      std::log(3200.0f / 20.0f) / std::log(1000.0f), // the ladder default, normalised
                                      0.1f);
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

        beginTest("factory_bank_reads_cleanly_and_every_preset_applies_without_problems");
        {
            const auto& errors = coolsynth::presets::getFactoryBankErrors();
            expect(errors.isEmpty(), errors.joinIntoString("\n"));
            expect(coolsynth::presets::getFactoryPresetCount() >= 14);
            expect(coolsynth::presets::getFactoryPreset(0).category == "Workshop");

            SynthAudioProcessor processor;
            juce::StringArray names;
            for (int index = 0; index < coolsynth::presets::getFactoryPresetCount(); ++index)
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(index);
                expect(preset.category.isNotEmpty(), preset.name + " has no room");
                // A name is unique within its room: a room may hold a model of an instrument another room has recorded.
                const auto fullName = preset.category + ": " + preset.name;
                expect(! names.contains(fullName), "two presets are called " + fullName);
                names.add(fullName);
                const auto problems = coolsynth::presets::applyFactoryPreset(processor.getValueTreeState(), preset);
                expect(problems.isEmpty(), problems.joinIntoString("\n"));
            }
        }

        beginTest("factory_presets_round_trip_through_wrapped_patch_state");
        {
            const auto compareStates = [this](const SynthAudioProcessor& source,
                                              const SynthAudioProcessor& target)
            {
                for (const auto* parameterId : coolsynth::parameters::allParameterIds)
                {
                    const auto* sourceValue = source.getValueTreeState().getRawParameterValue(parameterId);
                    const auto* targetValue = target.getValueTreeState().getRawParameterValue(parameterId);
                    expect(sourceValue != nullptr);
                    expect(targetValue != nullptr);

                    if (sourceValue != nullptr && targetValue != nullptr)
                    {
                        expectWithinAbsoluteError(sourceValue->load(),
                                                  targetValue->load(),
                                                  0.01f);
                    }
                }
            };

            for (int presetIndex = 0; presetIndex < coolsynth::presets::getFactoryPresetCount(); ++presetIndex)
            {
                SynthAudioProcessor source;
                SynthAudioProcessor target;

                coolsynth::presets::applyFactoryPreset(source.getValueTreeState(),
                                                       coolsynth::presets::getFactoryPreset(presetIndex));

                auto stateXml = source.createParameterStateXml();
                expect(stateXml != nullptr);

                if (stateXml == nullptr)
                    continue;

                auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                                          source.getParameterStateTypeName());
                auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml,
                                                                       target.getParameterStateTypeName());
                expect(parsed.succeeded());
                expect(parsed.parameterStateXml != nullptr);

                if (parsed.parameterStateXml == nullptr)
                    continue;

                expect(target.applyParameterStateXml(*parsed.parameterStateXml));
                compareStates(source, target);
            }
        }

        beginTest("processor_state_loader_rejects_unwrapped_or_legacy_wrapped_state");
        {
            SynthAudioProcessor source;
            SynthAudioProcessor target;

            source.getValueTreeState().getParameter(coolsynth::parameters::ids::delayMix)->setValueNotifyingHost(0.61f);
            target.getValueTreeState().getParameter(coolsynth::parameters::ids::delayMix)->setValueNotifyingHost(0.12f);
            const std::array<coolsynth::midi::LearnedCcBinding, 1> targetBindings {{
                { "pA.g1.type", { 3, 70 } }
            }};
            target.setLearnedMidiBindings(targetBindings);

            auto bareStateXml = source.createParameterStateXml();
            expect(bareStateXml != nullptr);

            if (bareStateXml != nullptr)
            {
                juce::MemoryBlock bareStateData;
                AudioProcessorXmlHarness::writeXmlToBinary(*bareStateXml, bareStateData);
                target.setStateInformation(bareStateData.getData(), static_cast<int> (bareStateData.getSize()));

                expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue(coolsynth::parameters::ids::delayMix)->load(),
                                          0.12f,
                                          0.0001f);
                expectEquals(static_cast<int> (target.getLearnedMidiBindings().size()), 1);

                juce::MemoryBlock wrappedStateData;
                source.getStateInformation(wrappedStateData);
                auto wrappedStateXml = AudioProcessorXmlHarness::readXmlFromBinary(wrappedStateData.getData(),
                                                                                   static_cast<int> (wrappedStateData.getSize()));
                expect(wrappedStateXml != nullptr);

                if (wrappedStateXml != nullptr)
                {
                    wrappedStateXml->setAttribute("formatVersion", 1);

                    juce::MemoryBlock legacyWrappedStateData;
                    AudioProcessorXmlHarness::writeXmlToBinary(*wrappedStateXml, legacyWrappedStateData);
                    target.setStateInformation(legacyWrappedStateData.getData(),
                                               static_cast<int> (legacyWrappedStateData.getSize()));

                    expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue(coolsynth::parameters::ids::delayMix)->load(),
                                              0.12f,
                                              0.0001f);
                    expectEquals(static_cast<int> (target.getLearnedMidiBindings().size()), 1);
                }
            }
        }
    }
};

static PatchStateTests patchStateTests;
