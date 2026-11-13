#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BankFile.h"

namespace coolsynth::presets
{
    using FactoryPreset = BankPreset;

    /** The embedded bank, in file-name order; never empty. */
    int getFactoryPresetCount() noexcept;
    const FactoryPreset& getFactoryPreset(int index) noexcept;

    /** Problems found while reading the embedded bank files; empty when the bank is clean. */
    const juce::StringArray& getFactoryBankErrors() noexcept;

    /**
        Returns every parameter to its default, then applies the preset's entries in order. Naming
        a slot type also loads that type's defaults into the slot's generic parameters. Returns the
        entries it could not apply (unknown parameter, value it could not read, out of range).
    */
    juce::StringArray applyFactoryPreset(juce::AudioProcessorValueTreeState& apvts, const FactoryPreset& preset);
}
