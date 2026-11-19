#pragma once

#include <memory>

#include <juce_core/juce_core.h>

namespace coolsynth::presets
{
    inline constexpr char patchRootTag[] = "COOLSYNTH_PATCH";
    // 8: V3 parameter contract (older patches are rejected); 9: the master effect slots (an 8 is carried over, D135)
    // 10: the layer modes Stack, Split, Velocity (D190; a 9 or 8 is carried over)
    // 11: an arp and six effect slots per layer (D194; a 10, 9 or 8 is carried over)
    // 12: the Layers page's balance, detune and spread (D196; an 11 gets them at their defaults)
    inline constexpr int patchFormatVersion = 14; // 14: Brown noise (D221)
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
        bool migrated = false; // an older version, converted: the processor fills what it still lacks with defaults
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
