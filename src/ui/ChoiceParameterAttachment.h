#pragma once

#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace coolsynth::ui
{
    // Wires a choice control that exposes `onSelectionChanged(int)` and
    // `setSelectedValue(int)` (SegmentedChoiceGroup) to a choice parameter through a
    // juce::ParameterAttachment owned by `attachment`. Shared by the main editor and
    // the arp advanced overlay so the glue exists once.
    template <typename ChoiceControl>
    void attachChoiceControl(ChoiceControl& control,
                             std::unique_ptr<juce::ParameterAttachment>& attachment,
                             juce::RangedAudioParameter* parameter)
    {
        control.onSelectionChanged = [&attachment](int value)
        {
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture(static_cast<float>(value));
        };

        if (parameter == nullptr)
            return;

        attachment = std::make_unique<juce::ParameterAttachment>(*parameter,
                                                                 [&control](float value)
                                                                 {
                                                                     control.setSelectedValue(static_cast<int>(std::lround(value)));
                                                                 });
        attachment->sendInitialUpdate();
    }
}
