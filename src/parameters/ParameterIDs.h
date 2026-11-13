#pragma once

#include <array>

/*
    IDs of the parameters outside a V3 part: the arpeggiator, the effect rack and the output.
    A part's own parameters (generator and processor slots, envelopes, voice and play settings)
    are built from slot paths in parameters/v3 and are not listed here.
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

    inline constexpr char driveEnabled[] = "driveEnabled";
    inline constexpr char driveAmount[] = "driveAmount";
    inline constexpr char driveMix[] = "driveMix";

    inline constexpr char chorusEnabled[] = "chorusEnabled";
    inline constexpr char chorusRateHz[] = "chorusRateHz";
    inline constexpr char chorusDepth[] = "chorusDepth";
    inline constexpr char chorusMix[] = "chorusMix";

    inline constexpr char delayEnabled[] = "delayEnabled";
    inline constexpr char delayTimeMs[] = "delayTimeMs";
    inline constexpr char delayFeedback[] = "delayFeedback";
    inline constexpr char delayMix[] = "delayMix";
    inline constexpr char delaySync[] = "delaySync";
    inline constexpr char delayDivision[] = "delayDivision";
    inline constexpr char delayMode[] = "delayMode";

    inline constexpr char lofiEnabled[] = "lofiEnabled";
    inline constexpr char lofiWow[] = "lofiWow";
    inline constexpr char lofiFlutter[] = "lofiFlutter";
    inline constexpr char lofiHiss[] = "lofiHiss";
    inline constexpr char lofiCrackle[] = "lofiCrackle";
    inline constexpr char lofiBandwidth[] = "lofiBandwidth";
    inline constexpr char lofiBits[] = "lofiBits";
    inline constexpr char lofiRate[] = "lofiRate";
    inline constexpr char lofiMix[] = "lofiMix";

    inline constexpr char reverbEnabled[] = "reverbEnabled";
    inline constexpr char reverbSize[] = "reverbSize";
    inline constexpr char reverbDamping[] = "reverbDamping";
    inline constexpr char reverbMix[] = "reverbMix";
    inline constexpr char reverbPreDelay[] = "reverbPreDelay";
    inline constexpr char reverbLowCut[] = "reverbLowCut";
    inline constexpr char reverbModulation[] = "reverbModulation";
    inline constexpr char reverbShimmer[] = "reverbShimmer";

    inline constexpr char masterGainDb[] = "masterGainDb";

    inline constexpr char phaserEnabled[]     = "phaserEnabled";
    inline constexpr char phaserRateHz[]      = "phaserRateHz";
    inline constexpr char phaserDepth[]       = "phaserDepth";

    inline constexpr char compressorEnabled[] = "compressorEnabled";
    inline constexpr char compressorAmount[]  = "compressorAmount";
    inline constexpr char compressorMix[]     = "compressorMix";
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
        ids::driveEnabled,
        ids::driveAmount,
        ids::driveMix,
        ids::chorusEnabled,
        ids::chorusRateHz,
        ids::chorusDepth,
        ids::chorusMix,
        ids::delayEnabled,
        ids::delayTimeMs,
        ids::delayFeedback,
        ids::delayMix,
        ids::delaySync,
        ids::delayDivision,
        ids::delayMode,
        ids::lofiEnabled,
        ids::lofiWow,
        ids::lofiFlutter,
        ids::lofiHiss,
        ids::lofiCrackle,
        ids::lofiBandwidth,
        ids::lofiBits,
        ids::lofiRate,
        ids::lofiMix,
        ids::reverbEnabled,
        ids::reverbSize,
        ids::reverbDamping,
        ids::reverbMix,
        ids::reverbPreDelay,
        ids::reverbLowCut,
        ids::reverbModulation,
        ids::reverbShimmer,
        ids::masterGainDb,
        ids::phaserEnabled,
        ids::phaserRateHz,
        ids::phaserDepth,
        ids::compressorEnabled,
        ids::compressorAmount,
        ids::compressorMix,
    });

    /** The fixed parameters MIDI learn accepts; part parameters are accepted by their slot-path prefix. */
    inline constexpr auto learnableParameterIds = allParameterIds;
}
