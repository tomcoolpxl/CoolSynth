#include "PatchState.h"

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
        if (version != patchFormatVersion)
            return { PatchStateError::unsupportedFormatVersion, "Unsupported CoolSynth patch version.", nullptr };

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
