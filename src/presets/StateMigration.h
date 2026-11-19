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

    /** The format versions with the old layer modes (D190): A, Layer, Split, B. */
    inline constexpr int processorStateVersionWithOldLayerModes = 10;
    inline constexpr int patchVersionWithOldLayerModes = 9;

    /** The format versions with one arpeggiator for both layers and three effect slots per layer (before D194). */
    inline constexpr int processorStateVersionWithGlobalArp = 11;
    inline constexpr int patchVersionWithGlobalArp = 10;

    /**
        Rewrites a parameter state from before D194 in place: the one arp ("arpEnabled", ...) becomes each layer's
        own ("pA.arpEnabled", "pB.arpEnabled", ...), on for a layer only if it was on and that layer followed it
        (its "mix.arp", gone); effect routes aimed at a master slot aim at it still, past the three new insert
        slots. The new slots and anything else the state lacks are filled with defaults by the processor. Returns
        the old arp IDs and their layer A IDs, so MIDI learn follows.
    */
    std::map<juce::String, juce::String> migrateLayerArps(juce::XmlElement& parameterState);

    /**
        Rewrites a parameter state from before D190 in place: the modes were A, Layer, Split, B and are Stack,
        Split, Velocity. A and B become a stack with the layer they left out emptied (its slot types Off, so it is
        left out as before), Layer a stack, Split stays split. The sound is the same.
    */
    void migrateLayerModes(juce::XmlElement& parameterState);

    /** The format versions before a Sample slot used its ninth to twelfth values (slices, D207). */
    inline constexpr int processorStateVersionBeforeSlices = 13;
    inline constexpr int patchVersionBeforeSlices = 12;

    /**
        Rewrites a parameter state from before D207 in place: a Sample slot's Count, Sensitivity and First Key were
        unused and saved as 0 (2 slices, nothing kept, C-1); they take the type's defaults (16, 50 %, C3), so turning
        Slices on later starts where a new slot does. Slices itself, 0, is Off: the sound is the same.
    */
    void migrateSampleSlices(juce::XmlElement& parameterState);

    /** The format versions before the Noise generator had a fifth type, Brown (D221). */
    inline constexpr int processorStateVersionBeforeBrownNoise = 14;
    inline constexpr int patchVersionBeforeBrownNoise = 13;

    /**
        Rewrites a parameter state from before D221 in place: a Noise slot's Type ran over four choices (White, Pink,
        Band, Chip) and runs over five now, Brown added at the end; its stored value is rescaled, so each slot keeps
        its kind. The sound is the same.
    */
    void migrateNoiseTypes(juce::XmlElement& parameterState);
}
