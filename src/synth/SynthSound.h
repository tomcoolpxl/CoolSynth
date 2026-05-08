#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace coolsynth::synth
{
    /**
     * Trivial sound object that accepts all notes and channels.
     */
    class SynthSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int /*midiNoteNumber*/) override { return true; }
        bool appliesToChannel(int /*midiChannel*/) override { return true; }
    };
}
