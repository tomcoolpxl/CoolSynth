#pragma once

#include <map>

#include <juce_core/juce_core.h>

/*
    Saved states and patches from before the master effect slots (decision D135).

    The master section was a fixed chain of ten effects, each with parameters of its own
    (driveEnabled, driveAmount, ..., reverbMix). It is now six slots, F1..F6, that take any effect.
    A state saved before is carried over, not refused: every effect that was switched on takes the
    next free slot in the old chain's order (drive, lo-fi, phaser, flanger, chorus, delay, reverb,
    compressor, EQ, pump) with its values, so the patch sounds as it did. Six slots hold every
    combination a patch could switch on in practice; a seventh effect and beyond would be dropped
    (no factory preset uses more than four).
*/
namespace coolsynth::presets
{
    /** The format versions that still hold the fixed effects: the processor state's and the patch file's. */
    inline constexpr int processorStateVersionWithFixedEffects = 9;
    inline constexpr int patchVersionWithFixedEffects = 8;

    /**
        Rewrites a parameter state (the value tree's XML: PARAM children with "id" and "value") in place:
        removes the fixed effects' parameters and adds every master slot parameter. Returns, for each
        old effect parameter that has a place now, its new ID ("reverbMix" -> "f2.p3"), so a learned
        MIDI binding can follow it; the others (the on switches, the values of effects that were off)
        have none.
    */
    std::map<juce::String, juce::String> migrateFixedMasterEffects(juce::XmlElement& parameterState);
}
