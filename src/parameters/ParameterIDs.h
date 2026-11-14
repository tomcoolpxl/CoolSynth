#pragma once

#include <array>

/*
    IDs of the parameters outside a V3 part: the arpeggiator and the output. A part's own
    parameters (generator and processor slots, envelopes, voice and play settings) and the master
    effect slots F1..F6 ("f1.type", "f1.p1") are built from slot paths in parameters/v3 and are not
    listed here.
*/
namespace coolsynth::parameters::ids
{
    inline constexpr int parameterVersionHint = 2;
    inline constexpr int parameterVersionHintV3 = 3;
    inline constexpr int parameterVersionHintV4 = 4;
    inline constexpr int parameterVersionHintV5 = 5;
    inline constexpr int parameterVersionHintV6 = 6;

    inline constexpr char arpEnabled[] = "arpEnabled";
    inline constexpr char arpInternalTempoBpm[] = "arpInternalTempoBpm";
    inline constexpr char arpRateDivision[] = "arpRateDivision";
    inline constexpr char arpPattern[] = "arpPattern";
    inline constexpr char arpOctaveRange[] = "arpOctaveRange";
    inline constexpr char arpGate[] = "arpGate";
    inline constexpr char arpSwing[] = "arpSwing";
    inline constexpr char arpChance[] = "arpChance";
    inline constexpr char arpRatchetCount[] = "arpRatchetCount";
    inline constexpr char arpRatchetChance[] = "arpRatchetChance";
    inline constexpr char arpAccentEvery[] = "arpAccentEvery";
    inline constexpr char arpAccentAmount[] = "arpAccentAmount";
    inline constexpr char arpRhythm[] = "arpRhythm";
    inline constexpr char arpEuclideanPulses[] = "arpEuclideanPulses";
    inline constexpr char arpEuclideanSteps[] = "arpEuclideanSteps";
    inline constexpr char arpEuclideanRotation[] = "arpEuclideanRotation";
    inline constexpr char arpLatch[] = "arpLatch";

    inline constexpr char masterGainDb[] = "masterGainDb";

}

namespace coolsynth::parameters
{
    enum class ArpRateChoice : int
    {
        quarter = 0,
        eighth = 1,
        eighthTriplet = 2,
        sixteenth = 3,
        sixteenthTriplet = 4,
        thirtySecond = 5,
    };

    enum class ArpPatternChoice : int
    {
        up = 0,
        down = 1,
        upDown = 2,
        asPlayed = 3,
        converge = 4,
        diverge = 5,
        inside = 6,
        outside = 7,
        random = 8,
        randomWalk = 9,
        chord = 10,
    };

    enum class ArpRatchetChoice : int
    {
        off = 0,
        x2 = 1,
        x3 = 2,
        x4 = 3,
    };

    enum class ArpRhythmChoice : int
    {
        straight = 0,
        euclidean = 1,
    };

    enum class ArpAccentEveryChoice : int
    {
        off = 0,
        every2 = 1,
        every3 = 2,
        every4 = 3,
    };

    inline constexpr auto allParameterIds = std::to_array<const char*>({
        ids::arpEnabled,
        ids::arpInternalTempoBpm,
        ids::arpRateDivision,
        ids::arpPattern,
        ids::arpOctaveRange,
        ids::arpGate,
        ids::arpSwing,
        ids::arpChance,
        ids::arpRatchetCount,
        ids::arpRatchetChance,
        ids::arpAccentEvery,
        ids::arpAccentAmount,
        ids::arpRhythm,
        ids::arpEuclideanPulses,
        ids::arpEuclideanSteps,
        ids::arpEuclideanRotation,
        ids::arpLatch,
        ids::masterGainDb,
    });

    /** The fixed parameters MIDI learn accepts; part parameters are accepted by their slot-path prefix. */
    inline constexpr auto learnableParameterIds = allParameterIds;
}
