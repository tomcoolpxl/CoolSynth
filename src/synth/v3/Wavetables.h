#pragma once

#include <array>
#include <vector>

/*
    V3 factory wavetables (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: a factory table set,
    band-limited playback).

    Four tables of 16 frames, each frame one cycle of 2048 samples, built once from their
    harmonic spectra at first use:

        Classic  sine, triangle, saw, square, blended spectrum by spectrum (fundamental in phase)
        Digital  a hard-synced saw whose slave ratio climbs from 1 to 8
        Vocal    a saw through the formants of a, e, i, o, u
        Chip     32-step, 4-bit waves: pulse widths from 1/8 to 1/2, the NES triangle, a stepped
                 saw and four SCC-style waves

    Every frame exists at 11 band limits, one per octave (all harmonics up to 1023 at level 0,
    one harmonic at level 10), made by clearing the spectrum above the limit and transforming
    back. Each frame is scaled so its full-band version peaks at 1; its band-limited versions
    keep that scale. A frame's samples are stored with one guard sample before and two after,
    so a 4-point read never wraps.
*/
namespace coolsynth::synth::v3
{
    class WavetableBank final
    {
    public:
        static constexpr int tableSize = 2048;
        static constexpr int frames = 16;
        static constexpr int levels = 11;
        static constexpr int tableCount = 4;
        static constexpr int stride = tableSize + 3;

        enum class Table : int { classic = 0, digital, vocal, chip };

        /** Built on first call (thread-safe); call it once off the audio thread to warm it. */
        static const WavetableBank& instance();

        /** Points at sample 0 of a frame; samples -1 .. tableSize + 1 are readable. */
        [[nodiscard]] const float* frame(int table, int frameIndex, int level) const noexcept;

        /** The band limit for a frequency at a sample rate: harmonics stay below 0.75 fs, so what
            folds back lands above fs / 4, where the voice's decimator removes it. */
        [[nodiscard]] static int levelFor(double frequencyHz, double sampleRate) noexcept;

    private:
        WavetableBank();
        std::vector<float> data;
    };
}
