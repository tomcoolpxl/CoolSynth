#include "PatchState.h"

#include "StateMigration.h"

namespace coolsynth::presets
{
    std::unique_ptr<juce::XmlElement> createWrappedPatchXml(const juce::XmlElement& parameterStateXml,
                                                            juce::StringRef stateType)
    {
        auto patchXml = std::make_unique<juce::XmlElement>(patchRootTag);
        patchXml->setAttribute("formatVersion", patchFormatVersion);
        patchXml->setAttribute("product", "CoolSynth");
        patchXml->setAttribute("stateType", stateType);
        patchXml->addChildElement(new juce::XmlElement(parameterStateXml));
        return patchXml;
    }

    PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                         juce::StringRef expectedStateType)
    {
        if (!patchXml.hasTagName(patchRootTag))
            return { PatchStateError::invalidRootTag, "Not a CoolSynth patch file.", nullptr };

        const int version = patchXml.getIntAttribute("formatVersion", 0);
        if (version < patchVersionWithFixedEffects || version > patchFormatVersion)
            return { PatchStateError::unsupportedFormatVersion, "Unsupported CoolSynth patch version.", nullptr };

        if (patchXml.getStringAttribute("stateType") != expectedStateType)
            return { PatchStateError::unexpectedStateType, "Patch file contains incompatible parameter state.", nullptr };

        const juce::XmlElement* matchingChild = nullptr;

        for (auto* child : patchXml.getChildIterator())
        {
            if (!child->hasTagName(expectedStateType))
                continue;

            if (matchingChild != nullptr)
                return { PatchStateError::ambiguousParameterState, "Patch file contains multiple parameter-state payloads.", nullptr };

            matchingChild = child;
        }

        if (matchingChild == nullptr)
            return { PatchStateError::missingParameterState, "Patch file does not contain the expected parameter state.", nullptr };

        PatchLoadResult result;
        result.parameterStateXml.reset(new juce::XmlElement(*matchingChild));
        if (version == patchVersionWithFixedEffects)
            migrateFixedMasterEffects(*result.parameterStateXml); // its fixed master effects move into the slots
        if (version <= patchVersionWithOldLayerModes)
            migrateLayerModes(*result.parameterStateXml); // the layer modes of before D190
        if (version <= patchVersionWithGlobalArp)
            migrateLayerArps(*result.parameterStateXml); // one arp and three slots per layer before D194
        if (version <= patchVersionBeforeSlices)
            migrateSampleSlices(*result.parameterStateXml); // a Sample slot's slice values at their defaults (D207)
        if (version <= patchVersionBeforeBrownNoise)
            migrateNoiseTypes(*result.parameterStateXml); // a Noise slot's Type over five choices (D221)
        if (version <= patchVersionBeforeSvfModes)
            migrateSvfModes(*result.parameterStateXml); // an SVF slot's Mode over seven choices (D233)
        result.migrated = version != patchFormatVersion;
        return result;
    }

    PatchIoResult writePatchFile(const juce::File& destination,
                                 const juce::XmlElement& patchXml)
    {
        juce::TemporaryFile tempFile(destination);

        if (auto stream = tempFile.getFile().createOutputStream())
        {
            patchXml.writeTo(*stream, {});
            stream->flush();
            stream.reset();

            if (tempFile.overwriteTargetFileWithTemporary())
                return {};
        }

        return { PatchStateError::fileWriteFailed, "Failed to write patch file." };
    }

    PatchLoadResult readPatchFile(const juce::File& source,
                                  juce::StringRef expectedStateType)
    {
        auto xml = juce::XmlDocument::parse(source);
        if (xml == nullptr)
            return { PatchStateError::invalidXmlDocument, "Failed to parse patch XML.", nullptr };

        return parseWrappedPatchXml(*xml, expectedStateType);
    }
}
