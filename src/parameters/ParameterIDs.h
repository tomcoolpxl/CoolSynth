#pragma once

#include <array>
#include <string_view>
#include <utility>

/*
    IDs of the parameters outside a V3 part's slot paths: the two layers' arpeggiators (D194), the tempo and the output. A part's own
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
    inline constexpr int parameterVersionHintV7 = 7;

    // The arpeggiator: one per layer since D194, each layer's own, as Korg's wavestate gives each layer one. These
    // name layer A's ("pA.arpEnabled"); arpOfLayer() gives layer B's. Before D194 one arp ("arpEnabled") served both.
    inline constexpr char arpEnabled[] = "pA.arpEnabled";
    inline constexpr char arpRateDivision[] = "pA.arpRateDivision";
    inline constexpr char arpPattern[] = "pA.arpPattern";
    inline constexpr char arpOctaveRange[] = "pA.arpOctaveRange";
    inline constexpr char arpGate[] = "pA.arpGate";
    inline constexpr char arpSwing[] = "pA.arpSwing";
    inline constexpr char arpChance[] = "pA.arpChance";
    inline constexpr char arpRatchetCount[] = "pA.arpRatchetCount";
    inline constexpr char arpRatchetChance[] = "pA.arpRatchetChance";
    inline constexpr char arpAccentEvery[] = "pA.arpAccentEvery";
    inline constexpr char arpAccentAmount[] = "pA.arpAccentAmount";
    inline constexpr char arpRhythm[] = "pA.arpRhythm";
    inline constexpr char arpEuclideanPulses[] = "pA.arpEuclideanPulses";
    inline constexpr char arpEuclideanSteps[] = "pA.arpEuclideanSteps";
    inline constexpr char arpEuclideanRotation[] = "pA.arpEuclideanRotation";
    inline constexpr char arpLatch[] = "pA.arpLatch";
    // Off, random choices repeat from play to play; on, each play draws new ones (D222).
    inline constexpr char arpNewPatterns[] = "pA.arpNewPatterns";
    namespace layerB
    {
        inline constexpr char arpEnabled[] = "pB.arpEnabled";
        inline constexpr char arpRateDivision[] = "pB.arpRateDivision";
        inline constexpr char arpPattern[] = "pB.arpPattern";
        inline constexpr char arpOctaveRange[] = "pB.arpOctaveRange";
        inline constexpr char arpGate[] = "pB.arpGate";
        inline constexpr char arpSwing[] = "pB.arpSwing";
        inline constexpr char arpChance[] = "pB.arpChance";
        inline constexpr char arpRatchetCount[] = "pB.arpRatchetCount";
        inline constexpr char arpRatchetChance[] = "pB.arpRatchetChance";
        inline constexpr char arpAccentEvery[] = "pB.arpAccentEvery";
        inline constexpr char arpAccentAmount[] = "pB.arpAccentAmount";
        inline constexpr char arpRhythm[] = "pB.arpRhythm";
        inline constexpr char arpEuclideanPulses[] = "pB.arpEuclideanPulses";
        inline constexpr char arpEuclideanSteps[] = "pB.arpEuclideanSteps";
        inline constexpr char arpEuclideanRotation[] = "pB.arpEuclideanRotation";
        inline constexpr char arpLatch[] = "pB.arpLatch";
        inline constexpr char arpNewPatterns[] = "pB.arpNewPatterns";
    }
    // The tempo while no host plays, one for the sound: the arps and the synced effects follow it.
    inline constexpr char arpInternalTempoBpm[] = "arpInternalTempoBpm";

    inline constexpr std::array<std::pair<const char*, const char*>, 17> arpLayerIds {{
        { arpEnabled, layerB::arpEnabled },
        { arpRateDivision, layerB::arpRateDivision },
        { arpPattern, layerB::arpPattern },
        { arpOctaveRange, layerB::arpOctaveRange },
        { arpGate, layerB::arpGate },
        { arpSwing, layerB::arpSwing },
        { arpChance, layerB::arpChance },
        { arpRatchetCount, layerB::arpRatchetCount },
        { arpRatchetChance, layerB::arpRatchetChance },
        { arpAccentEvery, layerB::arpAccentEvery },
        { arpAccentAmount, layerB::arpAccentAmount },
        { arpRhythm, layerB::arpRhythm },
        { arpEuclideanPulses, layerB::arpEuclideanPulses },
        { arpEuclideanSteps, layerB::arpEuclideanSteps },
        { arpEuclideanRotation, layerB::arpEuclideanRotation },
        { arpLatch, layerB::arpLatch },
        { arpNewPatterns, layerB::arpNewPatterns }
    }};

    /** Layer `layer`'s (0 A, 1 B) arp parameter, given layer A's ID. */
    [[nodiscard]] inline const char* arpOfLayer(int layer, const char* layerAId) noexcept
    {
        if (layer == 0)
            return layerAId;
        for (const auto& [a, b] : arpLayerIds)
            if (std::string_view(a) == layerAId)
                return b;
        return layerAId;
    }

    inline constexpr char masterGainDb[] = "masterGainDb";
    inline constexpr char brightness[] = "brightness"; // knob 7: every filter of the sound, -1..1 (D176)
    inline constexpr char space[] = "space";           // knob 8: every reverb, delay and room, -1..1 (D176)
    // The four faders (D184): volumes over the sound as designed, 0..1, 1 at the top (as designed). Kept when the
    // sound changes: a preset never sets them.
    inline constexpr char volumeA[] = "volumeA";
    inline constexpr char volumeB[] = "volumeB";
    inline constexpr char volumeFx[] = "volumeFx";
    inline constexpr char volumeMaster[] = "volumeMaster";

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
        ids::arpNewPatterns,
        ids::layerB::arpEnabled,
        ids::layerB::arpRateDivision,
        ids::layerB::arpPattern,
        ids::layerB::arpOctaveRange,
        ids::layerB::arpGate,
        ids::layerB::arpSwing,
        ids::layerB::arpChance,
        ids::layerB::arpRatchetCount,
        ids::layerB::arpRatchetChance,
        ids::layerB::arpAccentEvery,
        ids::layerB::arpAccentAmount,
        ids::layerB::arpRhythm,
        ids::layerB::arpEuclideanPulses,
        ids::layerB::arpEuclideanSteps,
        ids::layerB::arpEuclideanRotation,
        ids::layerB::arpLatch,
        ids::layerB::arpNewPatterns,
        ids::arpInternalTempoBpm,
        ids::masterGainDb,
        ids::brightness,
        ids::space,
        ids::volumeA,
        ids::volumeB,
        ids::volumeFx,
        ids::volumeMaster,
    });

    /** The fixed parameters MIDI learn accepts; part parameters are accepted by their slot-path prefix. */
    inline constexpr auto learnableParameterIds = allParameterIds;
}
