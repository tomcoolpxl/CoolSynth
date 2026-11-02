#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

#include "midi/MidiLearn.h"

namespace coolsynth::midi
{
    // Shared XML schema for learned CC bindings, used by both the plugin state
    // (LEARNED_MIDI_MAPPINGS) and the standalone settings store (MIDI_LEARN_MAPPINGS):
    //
    //   <rootTag version="1">
    //     <MAPPING parameterId="..." channel="1..16" controller="0..127"/>
    //   </rootTag>

    inline constexpr int learnedBindingsXmlVersion = 1;
    inline constexpr char learnedBindingXmlTag[] = "MAPPING";
    inline constexpr char learnedBindingParameterIdAttribute[] = "parameterId";
    inline constexpr char learnedBindingChannelAttribute[] = "channel";
    inline constexpr char learnedBindingControllerAttribute[] = "controller";

    inline std::unique_ptr<juce::XmlElement> createLearnedBindingsXml(juce::StringRef rootTag,
                                                                      std::span<const LearnedCcBinding> bindings)
    {
        auto xml = std::make_unique<juce::XmlElement>(rootTag);
        xml->setAttribute("version", learnedBindingsXmlVersion);

        for (const auto& binding : bindings)
        {
            if (! binding.isValid())
                continue;

            auto* child = xml->createNewChildElement(learnedBindingXmlTag);
            child->setAttribute(learnedBindingParameterIdAttribute, binding.parameterId);
            child->setAttribute(learnedBindingChannelAttribute, static_cast<int> (binding.cc.channel));
            child->setAttribute(learnedBindingControllerAttribute, static_cast<int> (binding.cc.controllerNumber));
        }

        return xml;
    }

    // Parses the MAPPING children of `parent`. Channel and controller are validated
    // as integers before narrowing, so an out-of-range value such as channel 257
    // is rejected instead of wrapping into the valid range.
    inline std::vector<LearnedCcBinding> parseLearnedBindingsXml(const juce::XmlElement& parent)
    {
        std::vector<LearnedCcBinding> bindings;

        for (const auto* child : parent.getChildIterator())
        {
            if (! child->hasTagName(learnedBindingXmlTag))
                continue;

            const auto parameterId = child->getStringAttribute(learnedBindingParameterIdAttribute);
            const int channel = child->getIntAttribute(learnedBindingChannelAttribute, 0);
            const int controller = child->getIntAttribute(learnedBindingControllerAttribute, -1);

            if (parameterId.isEmpty() || channel < 1 || channel > 16 || controller < 0 || controller > 127)
                continue;

            bindings.push_back({ parameterId,
                                 { static_cast<uint8_t> (channel), static_cast<uint8_t> (controller) } });
        }

        return bindings;
    }
}
