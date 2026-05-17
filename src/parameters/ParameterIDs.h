#pragma once

#include <array>

namespace coolsynth::parameters::ids
{
    inline constexpr int parameterVersionHint = 2;
    inline constexpr int parameterVersionHintV3 = 3;
    inline constexpr int parameterVersionHintV4 = 4;
    inline constexpr int parameterVersionHintV5 = 5;

    inline constexpr char oscAWave[] = "oscAWave";
    inline constexpr char oscAOctave[] = "oscAOctave";
    inline constexpr char oscAFineCents[] = "oscAFineCents";
    inline constexpr char oscALevel[] = "oscALevel";
    inline constexpr char oscAPulseWidth[] = "oscAPulseWidth";
    inline constexpr char oscASyncEnabled[] = "oscASyncEnabled";

    inline constexpr char oscBWave[] = "oscBWave";
    inline constexpr char oscBOctave[] = "oscBOctave";
    inline constexpr char oscBFineCents[] = "oscBFineCents";
    inline constexpr char oscBLevel[] = "oscBLevel";
    inline constexpr char oscBPulseWidth[] = "oscBPulseWidth";
    inline constexpr char oscBLowFrequencyMode[] = "oscBLowFrequencyMode";

    inline constexpr char noiseLevel[] = "noiseLevel";

    inline constexpr char filterCutoffHz[] = "filterCutoffHz";
    inline constexpr char filterResonance[] = "filterResonance";
    inline constexpr char filterEnvAmount[] = "filterEnvAmount";
    inline constexpr char filterKeyTracking[] = "filterKeyTracking";

    inline constexpr char filterAttackMs[] = "filterAttackMs";
    inline constexpr char filterDecayMs[] = "filterDecayMs";
    inline constexpr char filterSustain[] = "filterSustain";
    inline constexpr char filterReleaseMs[] = "filterReleaseMs";

    inline constexpr char ampAttackMs[] = "ampAttackMs";
    inline constexpr char ampDecayMs[] = "ampDecayMs";
    inline constexpr char ampSustain[] = "ampSustain";
    inline constexpr char ampReleaseMs[] = "ampReleaseMs";

    inline constexpr char lfoRateHz[] = "lfoRateHz";
    inline constexpr char lfoWave[] = "lfoWave";
    inline constexpr char lfoToOscPitch[] = "lfoToOscPitch";
    inline constexpr char lfoToPulseWidth[] = "lfoToPulseWidth";
    inline constexpr char lfoToFilterCutoff[] = "lfoToFilterCutoff";
    inline constexpr char modWheelToLfoDepth[] = "modWheelToLfoDepth";

    inline constexpr char polyModOscBToOscPitch[] = "polyModOscBToOscPitch";
    inline constexpr char polyModEnvToOscPitch[] = "polyModEnvToOscPitch";
    inline constexpr char polyModOscBToPulseWidth[] = "polyModOscBToPulseWidth";
    inline constexpr char polyModEnvToPulseWidth[] = "polyModEnvToPulseWidth";
    inline constexpr char polyModOscBToFilterCutoff[] = "polyModOscBToFilterCutoff";
    inline constexpr char polyModEnvToFilterCutoff[] = "polyModEnvToFilterCutoff";

    inline constexpr char glideTimeMs[] = "glideTimeMs";
    inline constexpr char playMode[] = "playMode";
    inline constexpr char keyPriority[] = "keyPriority";
    inline constexpr char pitchBendRangeSemitones[] = "pitchBendRangeSemitones";
    inline constexpr char vintageAmount[] = "vintageAmount";
    inline constexpr char panSpread[] = "panSpread";
    inline constexpr char velocityToAmp[] = "velocityToAmp";
    inline constexpr char velocityToFilter[] = "velocityToFilter";

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

    inline constexpr char reverbEnabled[] = "reverbEnabled";
    inline constexpr char reverbSize[] = "reverbSize";
    inline constexpr char reverbDamping[] = "reverbDamping";
    inline constexpr char reverbMix[] = "reverbMix";

    inline constexpr char masterGainDb[] = "masterGainDb";

    // --- v3 additions (Phase C–E: macros, phaser, compressor) ---
    inline constexpr char timbre[]            = "timbre";
    inline constexpr char excite[]            = "excite";

    inline constexpr char phaserEnabled[]     = "phaserEnabled";
    inline constexpr char phaserRateHz[]      = "phaserRateHz";
    inline constexpr char phaserDepth[]       = "phaserDepth";

    inline constexpr char compressorEnabled[] = "compressorEnabled";
    inline constexpr char compressorAmount[]  = "compressorAmount";
    inline constexpr char compressorMix[]     = "compressorMix";
}

namespace coolsynth::parameters
{
    enum class OscillatorWaveShape : int
    {
        pulse = 0,
        triangle = 1,
        saw = 2,
        sine = 3,
    };

    enum class FilterKeyTrackingMode : int
    {
        off = 0,
        half = 1,
        full = 2,
    };

    enum class LfoWaveShape : int
    {
        saw = 0,
        triangle = 1,
        square = 2,
        sine = 3,
    };

    enum class PlayModeChoice : int
    {
        poly = 0,
        mono = 1,
        unison = 2,
    };

    enum class KeyPriorityChoice : int
    {
        last = 0,
        low = 1,
        high = 2,
    };

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

    enum class ArpAccentEveryChoice : int
    {
        off = 0,
        every2 = 1,
        every3 = 2,
        every4 = 3,
    };

    inline constexpr auto allParameterIds = std::to_array<const char*>({
        ids::oscAWave,
        ids::oscAOctave,
        ids::oscAFineCents,
        ids::oscALevel,
        ids::oscAPulseWidth,
        ids::oscASyncEnabled,
        ids::oscBWave,
        ids::oscBOctave,
        ids::oscBFineCents,
        ids::oscBLevel,
        ids::oscBPulseWidth,
        ids::oscBLowFrequencyMode,
        ids::noiseLevel,
        ids::filterCutoffHz,
        ids::filterResonance,
        ids::filterEnvAmount,
        ids::filterKeyTracking,
        ids::filterAttackMs,
        ids::filterDecayMs,
        ids::filterSustain,
        ids::filterReleaseMs,
        ids::ampAttackMs,
        ids::ampDecayMs,
        ids::ampSustain,
        ids::ampReleaseMs,
        ids::lfoRateHz,
        ids::lfoWave,
        ids::lfoToOscPitch,
        ids::lfoToPulseWidth,
        ids::lfoToFilterCutoff,
        ids::modWheelToLfoDepth,
        ids::polyModOscBToOscPitch,
        ids::polyModEnvToOscPitch,
        ids::polyModOscBToPulseWidth,
        ids::polyModEnvToPulseWidth,
        ids::polyModOscBToFilterCutoff,
        ids::polyModEnvToFilterCutoff,
        ids::glideTimeMs,
        ids::playMode,
        ids::keyPriority,
        ids::pitchBendRangeSemitones,
        ids::vintageAmount,
        ids::panSpread,
        ids::velocityToAmp,
        ids::velocityToFilter,
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
        ids::reverbEnabled,
        ids::reverbSize,
        ids::reverbDamping,
        ids::reverbMix,
        ids::masterGainDb,
        // --- v3 additions below; v2EraParameterCount marks the boundary for state migration ---
        ids::timbre,
        ids::excite,
        ids::phaserEnabled,
        ids::phaserRateHz,
        ids::phaserDepth,
        ids::compressorEnabled,
        ids::compressorAmount,
        ids::compressorMix,
    });

    inline constexpr auto learnableParameterIds = allParameterIds;
}
