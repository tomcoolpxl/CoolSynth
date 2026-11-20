#pragma once

#include <vector>

/*
    The Chip generator's fixed shapes (docs/design/CHIP_GENERATOR.md), band-limited as the factory wavetables are
    (Wavetables.h): each one cycle of 2048 samples, built once from its spectrum at 11 band limits, one per octave,
    with one guard sample before and two after. The mean is left out, as in every table: a chip's offset only thumps
    when the voice's envelope opens and closes.

        triangle4        the 32-step, 16-level triangle of the NES and the Game Boy's wave channel
        triangleNes      the same through the NES's mixer curve (nesdev.org, APU Mixer)
        triangleSmooth   the SID's 12-bit triangle
        saw4, saw8       32-step saws of 4 and 8 bits (the Game Boy's and the SCC's wave memory)
        sawSmooth        the SID's 12-bit saw
        buzzSaw, buzzTriangle   the AY's envelope run at the note, through its logarithmic volume steps:
                         16 steps on the AY, 32 on the YM2149 (the DAC tables of Peter Sovietov's Ayumi)
        triSaw           the SID's combined triangle and saw, its waveforms' bits ANDed (a model brighter than the chip)
        triPulse, sawPulse, triSawPulse   the combined waves with the pulse, at 16 widths from 1/32 to 31/32
        table4, table8   the named 32-step waves, at 4 bits and at the SCC's 8

    The fixed shapes peak at 1. The combined waves keep the level the AND leaves them, so a narrow pulse in one is
    quieter, as on the chip.
*/
namespace coolsynth::synth::v3
{
    class ChipWaveBank final
    {
    public:
        static constexpr int tableSize = 2048;
        static constexpr int levels = 11;
        static constexpr int stride = tableSize + 3;
        static constexpr int widths = 16;
        static constexpr int tableWaves = 10;

        enum Frame : int
        {
            triangle4 = 0,
            triangleNes,
            triangleSmooth,
            saw4,
            saw8,
            sawSmooth,
            buzzSawAy,
            buzzSawYm,
            buzzTriangleAy,
            buzzTriangleYm,
            triSaw,
            triPulse,
            sawPulse = triPulse + widths,
            triSawPulse = sawPulse + widths,
            table4 = triSawPulse + widths,
            table8 = table4 + tableWaves,
            frameCount = table8 + tableWaves
        };

        /** Built on first call (thread-safe); Part warms it off the audio thread. */
        static const ChipWaveBank& instance();

        /** Points at sample 0 of a frame; samples -1 .. tableSize + 1 are readable. */
        [[nodiscard]] const float* frame(int index, int level) const noexcept;

        /** The AY's and the YM's volume DACs, 32 entries from 0 to 1 (Ayumi); a 4-bit volume v reads entry 2v + 1. */
        static const float* ayLevels() noexcept;
        static const float* ymLevels() noexcept;

    private:
        ChipWaveBank();
        std::vector<float> data;
    };
}
