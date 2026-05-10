#include "ControllerProfile.h"

#include <algorithm>

#include <BinaryData.h>

namespace coolsynth::midi
{
    namespace
    {
        juce::String toLower(juce::String text)
        {
            return text.toLowerCase();
        }

        ControllerMessageKind parseMessageKind(juce::StringRef text) noexcept
        {
            const juce::String value(text);
            if (value == "noteOn")        return ControllerMessageKind::noteOn;
            if (value == "noteOff")       return ControllerMessageKind::noteOff;
            if (value == "controlChange") return ControllerMessageKind::controlChange;
            return ControllerMessageKind::other;
        }

        ControllerValueMode parseValueMode(juce::StringRef text) noexcept
        {
            const juce::String value(text);
            if (value == "threeStepAbsolute")   return ControllerValueMode::threeStepAbsolute;
            if (value == "relativeBinaryOffset") return ControllerValueMode::relativeBinaryOffset;
            if (value == "noteGate")            return ControllerValueMode::noteGate;
            return ControllerValueMode::absolute7;
        }

        ControllerTargetKind parseTargetKind(juce::StringRef text) noexcept
        {
            if (juce::String(text) == "command")
                return ControllerTargetKind::command;

            return ControllerTargetKind::parameter;
        }

        ControllerCommandId parseCommand(juce::StringRef text) noexcept
        {
            if (juce::String(text) == "panic")
                return ControllerCommandId::panic;

            return ControllerCommandId::none;
        }

        uint8_t parseChannel(const juce::var& value) noexcept
        {
            if (value.isString() && value.toString() == "omni")
                return 0;

            return static_cast<uint8_t>(juce::jlimit(0, 16, static_cast<int>(value)));
        }

        ControllerBinding parseBinding(const juce::var& bindingVar)
        {
            ControllerBinding binding;
            if (auto* object = bindingVar.getDynamicObject())
            {
                binding.bindingId = object->getProperty("bindingId").toString();
                binding.displayName = object->getProperty("displayName").toString();
                binding.enabled = static_cast<bool>(object->getProperty("enabled"));
                if (!object->hasProperty("enabled"))
                    binding.enabled = true;

                const auto signatureVar = object->getProperty("signature");
                if (auto* signatureObject = signatureVar.getDynamicObject())
                {
                    binding.signature.kind = parseMessageKind(signatureObject->getProperty("kind").toString());
                    binding.signature.channel = parseChannel(signatureObject->getProperty("channel"));
                    binding.signature.data1 = static_cast<uint8_t>(juce::jlimit(0, 127, static_cast<int>(signatureObject->getProperty("data1"))));
                }

                binding.valueMode = parseValueMode(object->getProperty("valueMode").toString());

                const auto targetVar = object->getProperty("target");
                if (auto* targetObject = targetVar.getDynamicObject())
                {
                    binding.target.kind = parseTargetKind(targetObject->getProperty("kind").toString());
                    binding.target.parameterId = targetObject->getProperty("parameterId").toString();
                    binding.target.command = parseCommand(targetObject->getProperty("command").toString());
                }
            }

            return binding;
        }

        ControllerProfile parseProfileJson(const juce::String& jsonText)
        {
            ControllerProfile profile;
            const auto root = juce::JSON::parse(jsonText);
            if (auto* object = root.getDynamicObject())
            {
                profile.profileId = object->getProperty("profileId").toString();
                profile.displayName = object->getProperty("displayName").toString();

                if (auto* deviceHints = object->getProperty("deviceHints").getDynamicObject())
                {
                    if (auto* names = deviceHints->getProperty("nameContains").getArray())
                        for (const auto& name : *names)
                            profile.deviceNameContains.add(name.toString());

                    if (auto* identifiers = deviceHints->getProperty("identifierContains").getArray())
                        for (const auto& identifier : *identifiers)
                            profile.deviceIdentifierContains.add(identifier.toString());
                }

                if (auto* bindingsArray = object->getProperty("bindings").getArray())
                {
                    for (const auto& bindingVar : *bindingsArray)
                    {
                        auto binding = parseBinding(bindingVar);
                        if (binding.isValid())
                            profile.bindings.push_back(std::move(binding));
                    }
                }
            }

            return profile;
        }

        bool containsCaseInsensitive(const juce::StringArray& patterns, juce::StringRef haystack)
        {
            const auto lowercaseHaystack = toLower(juce::String(haystack));

            for (const auto& pattern : patterns)
                if (lowercaseHaystack.contains(toLower(pattern)))
                    return true;

            return false;
        }
    }

    const ControllerProfileRegistry& ControllerProfileRegistry::get() noexcept
    {
        static ControllerProfileRegistry registry;
        return registry;
    }

    ControllerProfileRegistry::ControllerProfileRegistry()
    {
        const auto* minilabJson = BinaryData::minilab3_arturia_mode_json;
        const auto minilabSize = BinaryData::minilab3_arturia_mode_jsonSize;

        auto profile = parseProfileJson(juce::String::fromUTF8(minilabJson, minilabSize));
        if (profile.isValid())
            profiles.push_back(std::move(profile));
    }

    const ControllerProfile* ControllerProfileRegistry::findProfileById(juce::StringRef profileId) const noexcept
    {
        for (const auto& profile : profiles)
            if (profile.profileId == profileId)
                return &profile;

        return nullptr;
    }

    juce::String ControllerProfileRegistry::findBestProfileIdForDevice(const juce::MidiDeviceInfo& device) const
    {
        if (device.name.isEmpty() && device.identifier.isEmpty())
            return {};

        const auto exactIdentifier = toLower(device.identifier);
        const auto exactName = toLower(device.name);

        for (const auto& profile : profiles)
        {
            for (const auto& identifier : profile.deviceIdentifierContains)
                if (toLower(identifier) == exactIdentifier)
                    return profile.profileId;
        }

        for (const auto& profile : profiles)
        {
            for (const auto& name : profile.deviceNameContains)
                if (toLower(name) == exactName)
                    return profile.profileId;
        }

        for (const auto& profile : profiles)
        {
            if (containsCaseInsensitive(profile.deviceIdentifierContains, device.identifier)
                || containsCaseInsensitive(profile.deviceNameContains, device.name))
            {
                return profile.profileId;
            }
        }

        return {};
    }
}
