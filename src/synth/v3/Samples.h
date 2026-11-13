#pragma once

#include <vector>

/*
    V3 factory samples (REQUIREMENTS_V3.md section 3.1: "factory samples embedded in the
    binary"). The set is made by the program itself the first time it is needed, so there is no
    recording to license or to ship:

        Glass    a bowed wine glass: a nearly pure tone with its slow beating and a breath of friction
        Choir    six detuned voices through vowel formants that drift from "ah" to "oo" and back
        Strings  seven detuned saws, softened, each wandering a little in pitch
        Air      wind: noise through two band-passes that move on their own
        Harp     a plucked C major add9 arpeggio, every string left ringing

    Every sample is four seconds of mono at 48 kHz, tuned so that it plays at its own speed on
    C4, levelled to the same loudness, and closed into a seamless loop (its last half second is
    folded into its first), so a loop over the whole sample and a grain that crosses its end
    never click. Each exists at three band limits (20, 10 and 5 kHz), so transposing it up does
    not alias. A sample's data has one guard frame before and three after, so a 4-point read at
    any frame needs no wrapping.
*/
namespace coolsynth::synth::v3
{
    class SampleBank final
    {
    public:
        static constexpr int sampleCount = 5;
        static constexpr int levels = 3;
        static constexpr int length = 192000;
        static constexpr double sourceRate = 48000.0;
        static constexpr double rootHz = 261.6255653005986; // C4
        static constexpr int stride = length + 4;
        /** The names of the samples, as a descriptor's options. */
        static constexpr const char* optionNames = "Glass|Choir|Strings|Air|Harp";

        enum class Sample : int { glass = 0, choir, strings, air, harp };

        /** Built on first call (thread-safe); call it once off the audio thread to warm it. */
        static const SampleBank& instance();

        /** Points at frame 0 of a sample at a band level; frames -1 .. length + 2 are readable. */
        [[nodiscard]] const float* data(int sample, int level) const noexcept;

        /** The band level to read at a playback ratio (source frames per output frame at twice the
            source rate count as 1): what folds back from the higher levels then lands above the
            audible band, where the voice's decimator removes it. */
        [[nodiscard]] static int levelFor(double pitchRatio) noexcept
        {
            return pitchRatio <= 3.0 ? 0 : (pitchRatio <= 6.0 ? 1 : 2);
        }

    private:
        SampleBank();
        std::vector<float> storage;
    };
}
