#include <cmath>
#include <juce_core/juce_core.h>

#include "parameters/ParameterIDs.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "presets/PatchState.h"
#include "midi/LearnedBindingsXml.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/SampleGenerator.h"

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
            // global envelope 12, step 16 + 16 + 4, 4 macros, chaos 1, 7 voice, 9 play, 8 chord, 9 mix, 6 inserts x 13 (D194),
            // 8 effect routes x 5 (D179);
            // then the layer mode with its velocity split and fade (D190), and the six master effect slots x 13
            constexpr int onePart = 3 * 21 + 3 * 13 + 3 * 13 + 4 * 9 + 24 * 5 + 12 + 36 + 4 + 1 + 7 + 9 + 8 + 9 + 6 * 13 + 8 * 5;
            constexpr int partParameterCount = 2 * onePart + 6; // mode, velocity split and fade; balance, detune, spread (D196)
            constexpr int masterSlotParameterCount = 6 * 13;
            expectEquals(registeredIds.size(),
                         static_cast<int>(coolsynth::parameters::allParameterIds.size()) + partParameterCount + masterSlotParameterCount);
            for (const auto* slotId : { "f1.type", "f1.p1", "f6.type", "f6.p12" })
                expect(registeredIds.contains(slotId), juce::String("missing ") + slotId);

            for (const auto* expectedId : coolsynth::parameters::allParameterIds)
                expect(registeredIds.contains(expectedId));
            for (const auto* partId : { "pA.g1.type", "pA.g3.amount", "pA.p1.p8", "pA.env3.oneShot", "pA.lfo2.oneShot", "pA.mod24.curve", "pA.mod1.src", "pA.genv.trigger", "pA.step.slide16", "pA.macro4", "pA.chaos.rate", "pA.glfo1.division", "pA.voice.wheelToLfo1", "pA.voice.drift", "pA.play.glideLegato" })
                expect(registeredIds.contains(partId), juce::String("missing ") + partId);
        }

        beginTest("init_patch_resets_all_automatable_parameters_to_defaults");
        {
            SynthAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            apvts.getParameter("f1.type")->setValueNotifyingHost(1.0f);
            apvts.getParameter("masterGainDb")->setValueNotifyingHost(1.0f);
            apvts.getParameter("pA.g2.fine")->setValueNotifyingHost(0.0f);
            apvts.getParameter("pA.voice.env2ToP1")->setValueNotifyingHost(0.0f);

            processor.resetAutomatableParametersToDefaults();

            expectWithinAbsoluteError(apvts.getParameter("f1.type")->getValue(),
                                      apvts.getParameter("f1.type")->getDefaultValue(),
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
            sourceState.getParameter("f2.p3")->setValueNotifyingHost(0.35f);

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
            expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue("f2.p3")->load(),
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
                legacyPatchXml->setAttribute("formatVersion", 7); // before the V3 contract; 8 is carried over (D135)
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
            sourceState.getParameter("f1.p3")->setValueNotifyingHost(0.65f);
            sourceState.getParameter("f1.type")->setValueNotifyingHost(1.0f);

            auto stateXml = source.createParameterStateXml();
            expect(stateXml != nullptr);

            stateXml->setAttribute("midiInputIdentifier", "should-not-load");
            auto* standaloneOnlyChild = stateXml->createNewChildElement("STANDALONE_ONLY");
            standaloneOnlyChild->setAttribute("value", "bad");

            expect(target.applyParameterStateXml(*stateXml));

            auto& targetState = target.getValueTreeState();
            expectWithinAbsoluteError(targetState.getRawParameterValue("f1.p3")->load(),
                                      0.65f,
                                      0.0001f);
            expectWithinAbsoluteError(targetState.getParameter("f1.type")->getValue(),
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
            sourceState.getParameter("f1.p3")->setValueNotifyingHost(0.42f);

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
                expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue("f1.p3")->load(),
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

            source.getValueTreeState().getParameter("f1.p3")->setValueNotifyingHost(0.61f);
            target.getValueTreeState().getParameter("f1.p3")->setValueNotifyingHost(0.12f);

            auto bareStateXml = source.createParameterStateXml();
            expect(bareStateXml != nullptr);

            if (bareStateXml != nullptr)
            {
                juce::MemoryBlock bareStateData;
                AudioProcessorXmlHarness::writeXmlToBinary(*bareStateXml, bareStateData);
                target.setStateInformation(bareStateData.getData(), static_cast<int> (bareStateData.getSize()));

                expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue("f1.p3")->load(),
                                          0.12f,
                                          0.0001f);

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

                    expectWithinAbsoluteError(target.getValueTreeState().getRawParameterValue("f1.p3")->load(),
                                              0.12f,
                                              0.0001f);
                }
            }
        }
    }
};

static PatchStateTests patchStateTests;

/**
    A state or a patch saved while the master section was the fixed chain of ten (decision D135): the effects that
    were on move into the master slots in the old chain's order, with their values. (A state's learned knobs are not
    read since D211: the controller setup is the computer's.) Its own class and heap processors: the test runner's
    stack frame is small in Debug.
*/
class MasterEffectMigrationTests final : public juce::UnitTest
{
public:
    MasterEffectMigrationTests() : juce::UnitTest("Master effect migration", "CoolSynth") {}

    void runTest() override
    {
        namespace types = coolsynth::synth::v3::insertType;
        using coolsynth::synth::v3::insertDescriptor;
        using coolsynth::synth::v3::descriptorToUnits;

        // A parameter state as version 9 wrote it: no master slots, the fixed effects' own parameters instead.
        auto oldParameterState = [](juce::XmlElement state)
        {
            for (int i = state.getNumChildElements(); --i >= 0;)
                if (state.getChildElement(i)->getStringAttribute("id").startsWith("f"))
                    state.removeChildElement(state.getChildElement(i), true);
            auto add = [&](const char* id, double value)
            {
                auto* child = state.createNewChildElement("PARAM");
                child->setAttribute("id", id);
                child->setAttribute("value", value);
            };
            add("driveEnabled", 0.0); add("driveAmount", 0.5); add("driveMix", 1.0);
            add("reverbEnabled", 1.0); add("reverbSize", 0.75); add("reverbDamping", 0.45); add("reverbMix", 0.24);
            add("delayEnabled", 1.0); add("delayTimeMs", 375.0); add("delayFeedback", 0.25); add("delayMix", 0.12);
            add("delaySync", 0.0); add("delayDivision", 5.0); add("delayMode", 1.0);
            add("chorusEnabled", 0.0); add("chorusRateHz", 0.6); add("chorusDepth", 0.4); add("chorusMix", 0.3);
            return state;
        };

        auto slotType = [](SynthAudioProcessor& p, int slot)
        {
            return static_cast<int>(std::lround(p.getValueTreeState().getRawParameterValue("f" + juce::String(slot) + ".type")->load()));
        };
        auto slotUnits = [](SynthAudioProcessor& p, int slot, int type, int index)
        {
            const auto& d = insertDescriptor(type).params[static_cast<std::size_t>(index)];
            return descriptorToUnits(d, p.getValueTreeState().getRawParameterValue("f" + juce::String(slot) + ".p" + juce::String(index + 1))->load());
        };
        auto expectCarriedOver = [&](SynthAudioProcessor& p)
        {
            expectEquals(slotType(p, 1), static_cast<int>(types::delay), "the delay first, as in the old chain");
            expectWithinAbsoluteError(slotUnits(p, 1, types::delay, 0), 375.0f, 0.01f);
            expectWithinAbsoluteError(slotUnits(p, 1, types::delay, 2), 0.12f, 1.0e-4f);
            expectWithinAbsoluteError(slotUnits(p, 1, types::delay, 5), 1.0f, 1.0e-4f, "ping-pong");
            expectEquals(slotType(p, 2), static_cast<int>(types::reverb));
            expectWithinAbsoluteError(slotUnits(p, 2, types::reverb, 0), 0.75f, 1.0e-4f);
            expectWithinAbsoluteError(slotUnits(p, 2, types::reverb, 2), 0.24f, 1.0e-4f);
            expectWithinAbsoluteError(slotUnits(p, 2, types::reverb, 5), 0.5f, 1.0e-4f, "a value the state lacks takes the old default");
            for (int slot = 3; slot <= 6; ++slot)
                expectEquals(slotType(p, slot), static_cast<int>(types::off), "the drive and chorus were off");
        };

        beginTest("a_state_saved_with_the_fixed_effects_is_carried_into_the_slots");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            juce::MemoryBlock saved;
            source->getStateInformation(saved);
            auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
            expect(root != nullptr);
            root->setAttribute("formatVersion", 9);
            auto* parameters = root->getChildByName(source->getParameterStateTypeName());
            expect(parameters != nullptr);
            const auto old = oldParameterState(*parameters);
            root->replaceChildElement(parameters, new juce::XmlElement(old));

            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary(*root, oldState);
            auto target = std::make_unique<SynthAudioProcessor>();
            target->setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
            expectCarriedOver(*target);
        }

        beginTest("a_patch_saved_with_the_fixed_effects_is_carried_into_the_slots");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(oldParameterState(*source->createParameterStateXml()),
                                                                      source->getParameterStateTypeName());
            patchXml->setAttribute("formatVersion", 8);
            auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml, source->getParameterStateTypeName());
            expect(parsed.succeeded(), parsed.message);
            auto target = std::make_unique<SynthAudioProcessor>();
            expect(target->applyParameterStateXml(*parsed.parameterStateXml));
            expectCarriedOver(*target);
        }

        beginTest("a_state_from_before_version_9_is_still_refused");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*source->createParameterStateXml(), source->getParameterStateTypeName());
            patchXml->setAttribute("formatVersion", 7);
            expect(! coolsynth::presets::parseWrappedPatchXml(*patchXml, source->getParameterStateTypeName()).succeeded());
        }
    }
};

static MasterEffectMigrationTests masterEffectMigrationTests;

/** The layer modes before D190 (A, Layer, Split, B) carried into Stack, Split, Velocity. Heap processors. */
class LayerModeMigrationTests final : public juce::UnitTest
{
public:
    LayerModeMigrationTests() : juce::UnitTest("Layer mode migration", "Presets") {}

    void runTest() override
    {
        // A parameter state as an old version wrote it: both layers with a generator and a processor, the old mode.
        auto oldState = [](SynthAudioProcessor& source, int oldMode)
        {
            auto state = source.createParameterStateXml();
            auto set = [&](const juce::String& id, double value)
            {
                for (auto* child : state->getChildWithTagNameIterator("PARAM"))
                    if (child->getStringAttribute("id") == id)
                    {
                        child->setAttribute("value", value);
                        return;
                    }
                auto* child = state->createNewChildElement("PARAM");
                child->setAttribute("id", id);
                child->setAttribute("value", value);
            };
            const double va = source.getValueTreeState().getRawParameterValue("pA.g1.type")->load();
            const double filter = source.getValueTreeState().getRawParameterValue("pA.p1.type")->load();
            set("pB.g1.type", va);
            set("pB.p1.type", filter);
            set("part.mode", oldMode);
            return state;
        };
        auto value = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };

        struct Case { int oldMode; int newMode; bool aPlays; bool bPlays; const char* name; };
        const Case cases[] { { 0, 0, true, false, "A: a stack with B emptied" },
                             { 1, 0, true, true, "Layer: a stack" },
                             { 2, 1, true, true, "Split stays split" },
                             { 3, 0, false, true, "B: a stack with A emptied" } };

        beginTest("a_state_with_the_old_layer_modes_plays_as_before");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            for (const auto& c : cases)
            {
                juce::MemoryBlock saved;
                source->getStateInformation(saved);
                auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
                expect(root != nullptr);
                root->setAttribute("formatVersion", 10);
                auto* parameters = root->getChildByName(source->getParameterStateTypeName());
                expect(parameters != nullptr);
                root->replaceChildElement(parameters, oldState(*source, c.oldMode).release());
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary(*root, old);

                auto target = std::make_unique<SynthAudioProcessor>();
                target->setStateInformation(old.getData(), static_cast<int>(old.getSize()));
                expectEquals(static_cast<int>(value(*target, "part.mode")), c.newMode, c.name);
                expect((value(*target, "pA.g1.type") != 0.0f) == c.aPlays, c.name);
                expect((value(*target, "pA.p1.type") != 0.0f) == c.aPlays, c.name);
                expect((value(*target, "pB.g1.type") != 0.0f) == c.bPlays, c.name);
                expect((value(*target, "pB.p1.type") != 0.0f) == c.bPlays, c.name);
            }
        }

        beginTest("a_saved_sound_with_the_old_layer_modes_plays_as_before");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            for (const auto& c : cases)
            {
                auto patchXml = coolsynth::presets::createWrappedPatchXml(*oldState(*source, c.oldMode), source->getParameterStateTypeName());
                patchXml->setAttribute("formatVersion", 9);
                auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml, source->getParameterStateTypeName());
                expect(parsed.succeeded(), parsed.message);
                auto target = std::make_unique<SynthAudioProcessor>();
                expect(target->applyParameterStateXml(*parsed.parameterStateXml));
                expectEquals(static_cast<int>(value(*target, "part.mode")), c.newMode, c.name);
                expect((value(*target, "pA.g1.type") != 0.0f) == c.aPlays, c.name);
                expect((value(*target, "pB.g1.type") != 0.0f) == c.bPlays, c.name);
            }
        }

        beginTest("a_state_of_this_version_is_left_as_it_is");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto state = oldState(*source, 2); // Velocity now, not the old Split
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*state, source->getParameterStateTypeName());
            auto parsed = coolsynth::presets::parseWrappedPatchXml(*patchXml, source->getParameterStateTypeName());
            expect(parsed.succeeded(), parsed.message);
            auto target = std::make_unique<SynthAudioProcessor>();
            expect(target->applyParameterStateXml(*parsed.parameterStateXml));
            expectEquals(static_cast<int>(value(*target, "part.mode")), 2);
            expect(value(*target, "pB.g1.type") != 0.0f);
        }
    }
};

static LayerModeMigrationTests layerModeMigrationTests;

/** Before D194 one arp served both layers and each layer had three effect slots. Heap processors. */
class LayerArpMigrationTests final : public juce::UnitTest
{
public:
    LayerArpMigrationTests() : juce::UnitTest("Layer arp migration", "Presets") {}

    void runTest() override
    {
        // A version 11 state as it was written: the global arp IDs, each layer's follow flag, three slots, routes.
        auto oldState = [](SynthAudioProcessor& source, bool bFollows)
        {
            auto state = source.createParameterStateXml();
            std::vector<juce::XmlElement*> drop;
            for (auto* child : state->getChildWithTagNameIterator("PARAM"))
            {
                const auto id = child->getStringAttribute("id");
                if (id.contains(".arp") || id.matchesWildcard("p?.i4.*", true) || id.matchesWildcard("p?.i5.*", true) || id.matchesWildcard("p?.i6.*", true))
                    drop.push_back(child);
            }
            for (auto* child : drop)
                state->removeChildElement(child, true);
            auto add = [&](const juce::String& id, double value)
            {
                auto* child = state->createNewChildElement("PARAM");
                child->setAttribute("id", id);
                child->setAttribute("value", value);
            };
            add("arpEnabled", 1.0);
            add("arpRateDivision", 1.0); // 1/8
            add("arpGate", 0.8);
            add("pA.mix.arp", 1.0);
            add("pB.mix.arp", bFollows ? 1.0 : 0.0);
            for (auto* child : state->getChildWithTagNameIterator("PARAM"))
                if (child->getStringAttribute("id") == "pA.fx1.dst")
                    child->setAttribute("value", 1 + 3 * 12 + 2); // F1's third setting, as numbered with three insert slots
            return state;
        };
        auto value = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };

        beginTest("an_old_state_gives_each_layer_the_arp_it_followed");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            for (const bool bFollows : { true, false })
            {
                juce::MemoryBlock saved;
                source->getStateInformation(saved);
                auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
                root->setAttribute("formatVersion", 11);
                auto* parameters = root->getChildByName(source->getParameterStateTypeName());
                root->replaceChildElement(parameters, oldState(*source, bFollows).release());
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary(*root, old);

                auto target = std::make_unique<SynthAudioProcessor>();
                target->setStateInformation(old.getData(), static_cast<int>(old.getSize()));
                expectEquals(value(*target, "pA.arpEnabled"), 1.0f, "the state loaded, with layer A's arp on");
                expectEquals(value(*target, "pB.arpEnabled"), bFollows ? 1.0f : 0.0f);
                expectEquals(value(*target, "pA.arpRateDivision"), 1.0f);
                expectWithinAbsoluteError(value(*target, "pB.arpGate"), 0.8f, 1.0e-6f);
                expectEquals(value(*target, "pA.fx1.dst"), static_cast<float>(1 + 6 * 12 + 2), "the route still aims at F1's third setting");
                expectEquals(value(*target, "pA.i6.type"), 0.0f, "a new slot, Off");
            }
        }

        beginTest("an_old_saved_sound_loads_with_the_new_settings_at_their_defaults");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*oldState(*source, false), source->getParameterStateTypeName());
            patchXml->setAttribute("formatVersion", 10);
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("coolsynth-d194-old.cspatch");
            expect(coolsynth::presets::writePatchFile(file, *patchXml).succeeded());
            auto target = std::make_unique<SynthAudioProcessor>();
            expect(target->loadMySound(file), "an older saved sound loads");
            expectEquals(value(*target, "pB.arpEnabled"), 0.0f);
            expectEquals(value(*target, "pA.arpEnabled"), 1.0f);
            file.deleteFile();
        }
    }
};

static LayerArpMigrationTests layerArpMigrationTests;


class SampleSliceMigrationTests final : public juce::UnitTest
{
public:
    SampleSliceMigrationTests() : juce::UnitTest("Sample slice migration", "Presets") {}

    void runTest() override
    {
        namespace sv3 = coolsynth::synth::v3;
        const auto& d = sv3::SampleGenerator::descriptor();
        auto defaultOf = [&](int index) { return sv3::unitsToNormalised(d.params[static_cast<std::size_t>(index)], d.params[static_cast<std::size_t>(index)].def); };
        auto value = [](SynthAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue(id)->load(); };

        // A state with a Sample slot whose ninth to twelfth values were unused, saved as 0 (before D207).
        auto saved = [&](int version)
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto& state = source->getValueTreeState();
            auto* type = state.getParameter("pA.g2.type");
            type->setValueNotifyingHost(type->getValueForText("Sample"));
            for (int n = 9; n <= 12; ++n)
                state.getParameter("pA.g2.p" + juce::String(n))->setValueNotifyingHost(0.0f);
            juce::MemoryBlock block;
            source->getStateInformation(block);
            auto root = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
            root->setAttribute("formatVersion", version);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary(*root, old);
            return old;
        };

        beginTest("an_old_sample_slot_gets_the_slice_defaults_and_stays_unsliced");
        {
            const auto old = saved(13);
            auto target = std::make_unique<SynthAudioProcessor>();
            target->setStateInformation(old.getData(), static_cast<int>(old.getSize()));
            expectEquals(value(*target, "pA.g2.p9"), 0.0f, "Slices stays Off");
            expectWithinAbsoluteError(value(*target, "pA.g2.p10"), defaultOf(sv3::SampleGenerator::count), 1.0e-6f, "Count 16");
            expectWithinAbsoluteError(value(*target, "pA.g2.p11"), defaultOf(sv3::SampleGenerator::sense), 1.0e-6f, "Sensitivity 50 %");
            expectWithinAbsoluteError(value(*target, "pA.g2.p12"), defaultOf(sv3::SampleGenerator::firstKey), 1.0e-6f, "First Key C3");
        }

        beginTest("a_current_state_keeps_its_values");
        {
            const auto current = saved(14);
            auto target = std::make_unique<SynthAudioProcessor>();
            target->setStateInformation(current.getData(), static_cast<int>(current.getSize()));
            expectEquals(value(*target, "pA.g2.p12"), 0.0f, "First Key C-1, as saved");
        }

        beginTest("an_old_saved_sound_is_migrated_too");
        {
            auto source = std::make_unique<SynthAudioProcessor>();
            auto& state = source->getValueTreeState();
            auto* type = state.getParameter("pA.g1.type");
            type->setValueNotifyingHost(type->getValueForText("Sample"));
            for (int n = 9; n <= 12; ++n)
                state.getParameter("pA.g1.p" + juce::String(n))->setValueNotifyingHost(0.0f);
            auto patchXml = coolsynth::presets::createWrappedPatchXml(*source->createParameterStateXml(), source->getParameterStateTypeName());
            patchXml->setAttribute("formatVersion", 12);
            const auto result = coolsynth::presets::parseWrappedPatchXml(*patchXml, source->getParameterStateTypeName());
            expect(result.parameterStateXml != nullptr);
            if (result.parameterStateXml != nullptr)
                for (auto* child : result.parameterStateXml->getChildWithTagNameIterator("PARAM"))
                    if (child->getStringAttribute("id") == "pA.g1.p12")
                        expectWithinAbsoluteError(static_cast<float>(child->getDoubleAttribute("value")), defaultOf(sv3::SampleGenerator::firstKey), 1.0e-6f);
            expect(result.migrated, "an older patch says it was migrated");
        }
    }
};

static SampleSliceMigrationTests sampleSliceMigrationTests;
