#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace coolsynth::midi
{
    enum class Minilab3ControlCategory : uint8_t
    {
        keyboard,
        knob,
        fader,
        pad,
        encoder,
    };

    enum class Minilab3Disposition : uint8_t
    {
        requiredForPhase7,
        deferred,
        unsupported,
    };

    enum class Minilab3ValueMode : uint8_t
    {
        none,
        absolute,
        relative,
        velocity,
    };

    enum class VerifiedMidiMessageKind : uint8_t
    {
        note,
        controlChange,
        other,
    };

    struct MidiValueRange
    {
        int min = -1;
        int max = -1;
    };

    struct VerifiedMidiSignature
    {
        VerifiedMidiMessageKind kind = VerifiedMidiMessageKind::controlChange;
        uint8_t channel = 1;
        MidiValueRange primaryData {};
        MidiValueRange secondaryData {};
        Minilab3ValueMode valueMode = Minilab3ValueMode::none;
        bool emitsReleaseEvent = false;
        bool requiresRawByteInspection = false;
    };

    struct Minilab3ControlDefinition
    {
        std::string_view controlId;
        std::string_view displayName;
        Minilab3ControlCategory category = Minilab3ControlCategory::knob;
        std::string_view preferredTarget;
        Minilab3Disposition disposition = Minilab3Disposition::deferred;
        VerifiedMidiSignature signature {};
        std::string_view notes;
    };

    std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept;
    const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept;
    bool validateMinilab3Profile() noexcept;
}
