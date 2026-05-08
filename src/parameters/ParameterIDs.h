#pragma once

namespace coolsynth::parameters::ids
{
    inline constexpr int versionHint = 1;

    inline constexpr char waveform[] = "waveform";
    inline constexpr char ampAttackMs[] = "ampAttackMs";
    inline constexpr char ampDecayMs[] = "ampDecayMs";
    inline constexpr char ampSustain[] = "ampSustain";
    inline constexpr char ampReleaseMs[] = "ampReleaseMs";
    inline constexpr char filterCutoffHz[] = "filterCutoffHz";
    inline constexpr char filterResonance[] = "filterResonance";
    inline constexpr char delayTimeMs[] = "delayTimeMs";
    inline constexpr char delayFeedback[] = "delayFeedback";
    inline constexpr char delayMix[] = "delayMix";
    inline constexpr char masterGainDb[] = "masterGainDb";
}

namespace coolsynth::parameters
{
    enum class WaveformChoice : int
    {
        sine = 0,
        square = 1,
        saw = 2,
    };
}
