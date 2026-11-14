#pragma once

#include <memory>

#include <juce_core/juce_core.h>

namespace coolsynth::presets
{
    inline constexpr char patchRootTag[] = "COOLSYNTH_PATCH";
    // 8: V3 parameter contract (older patches are rejected); 9: the master effect slots (an 8 is carried over, D135)
    inline constexpr int patchFormatVersion = 9;
    inline constexpr char defaultPatchExtension[] = ".cspatch";

    enum class PatchStateError : uint8_t
    {
        none,
        fileReadFailed,
        fileWriteFailed,
        invalidXmlDocument,
        invalidRootTag,
        unsupportedFormatVersion,
        missingParameterState,
        ambiguousParameterState,
        unexpectedStateType,
        applyFailed,
    };

    struct PatchIoResult
    {
        PatchStateError error = PatchStateError::none;
        juce::String message;

        bool succeeded() const noexcept
        {
            return error == PatchStateError::none;
        }
    };

    struct PatchLoadResult : PatchIoResult
    {
        std::unique_ptr<juce::XmlElement> parameterStateXml;
    };

    std::unique_ptr<juce::XmlElement> createWrappedPatchXml(const juce::XmlElement& parameterStateXml,
                                                            juce::StringRef stateType);

    PatchLoadResult parseWrappedPatchXml(const juce::XmlElement& patchXml,
                                         juce::StringRef expectedStateType);

    PatchIoResult writePatchFile(const juce::File& destination,
                                 const juce::XmlElement& patchXml);

    PatchLoadResult readPatchFile(const juce::File& source,
                                  juce::StringRef expectedStateType);
}
