#pragma once

#include <cstdint>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

namespace coolsynth::midi
{
    enum class ControllerMessageKind : uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        other,
    };

    enum class ControllerValueMode : uint8_t
    {
        absolute7,
        threeStepAbsolute,
        relativeBinaryOffset,
        noteGate,
    };

    enum class ControllerCommandId : uint8_t
    {
        none,
        panic,
    };

    enum class ControllerTargetKind : uint8_t
    {
        parameter,
        command,
    };

    struct ControllerMessageSignature
    {
        ControllerMessageKind kind = ControllerMessageKind::other;
        uint8_t channel = 0; // 0 means omni
        uint8_t data1 = 0;

        bool isValid() const noexcept
        {
            return kind != ControllerMessageKind::other;
        }
    };

    struct ControllerTarget
    {
        ControllerTargetKind kind = ControllerTargetKind::parameter;
        juce::String parameterId;
        ControllerCommandId command = ControllerCommandId::none;

        bool isValid() const noexcept
        {
            switch (kind)
            {
                case ControllerTargetKind::parameter: return parameterId.isNotEmpty();
                case ControllerTargetKind::command:   return command != ControllerCommandId::none;
            }

            return false;
        }
    };

    struct ControllerBinding
    {
        juce::String bindingId;
        juce::String displayName;
        ControllerMessageSignature signature;
        ControllerValueMode valueMode = ControllerValueMode::absolute7;
        ControllerTarget target;
        bool enabled = true;

        bool isValid() const noexcept
        {
            return bindingId.isNotEmpty() && signature.isValid() && target.isValid();
        }
    };

    struct ControllerProfile
    {
        juce::String profileId;
        juce::String displayName;
        juce::StringArray deviceNameContains;
        juce::StringArray deviceIdentifierContains;
        std::vector<ControllerBinding> bindings;

        bool isValid() const noexcept
        {
            return profileId.isNotEmpty() && displayName.isNotEmpty() && !bindings.empty();
        }
    };

    class ControllerProfileRegistry final
    {
    public:
        static const ControllerProfileRegistry& get() noexcept;

        const std::vector<ControllerProfile>& getProfiles() const noexcept { return profiles; }
        const ControllerProfile* findProfileById(juce::StringRef profileId) const noexcept;
        juce::String findBestProfileIdForDevice(const juce::MidiDeviceInfo& device) const;

    private:
        ControllerProfileRegistry();

        std::vector<ControllerProfile> profiles;
    };
}
