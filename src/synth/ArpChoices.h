#pragma once

/*
    The arpeggiator's choices, as the engine plays them. They keep the namespace they had in parameters/ParameterIDs.h,
    which includes this header, so every use reads as before; the engine needs no parameter ID to use them (D265).
*/
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
}
