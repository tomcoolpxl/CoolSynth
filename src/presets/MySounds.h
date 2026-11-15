#pragma once

#include <juce_core/juce_core.h>

/*
    The player's own sounds (decision D180): one .cspatch file per sound in Documents\CoolSynth\My Sounds,
    the sound's name the file's name. The browser shows them as the room "My Sounds", first in Home.
    Message thread only.
*/
namespace coolsynth::presets
{
    class MySounds final
    {
    public:
        /** Documents\CoolSynth\My Sounds, or the folder a test set. Not created until a sound is saved. */
        [[nodiscard]] static juce::File folder();
        /** Tests: keep the sounds somewhere else (an empty file goes back to the real folder). */
        static void setFolderForTesting(const juce::File& folder);

        /** The saved sounds, by name, A to Z. */
        [[nodiscard]] static juce::Array<juce::File> list();
        /** A name a file may have: what Windows forbids taken out, at most 60 letters; empty if nothing is left. */
        [[nodiscard]] static juce::String cleanName(const juce::String& name);
        /** The file a sound of this (clean) name lives in. */
        [[nodiscard]] static juce::File fileFor(const juce::String& name);
    };
}
