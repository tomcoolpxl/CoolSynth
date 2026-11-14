#pragma once

#include <vector>

#include <juce_core/juce_core.h>

/*
    Factory bank files (resources/presets/*.csbank): plain text, one file per room.

        # comment
        [Reese]
        room = Main Stage
        g1 = VA shape=saw oct=-1 fine=+6 level=0.8
        p1 = Ladder cutoff=800 res=0.2 key=0.3
        env1 = attack=5 decay=200 sustain=1 release=120
        voice = env2ToP1=0.1 glide=15
        play = mode=legato
        f1 = Drive amount=0.15 mix=0.8
        f2 = Reverb size=0.6 mix=0.25
        master = -6

    g1..g3, p1..p3, the part's insert slots i1..i3 and the master effect slots f1..f6 name the
    slot type first, then its parameters by descriptor key (generators also take level, pan, oct,
    semi, fine, key, combine, amount). The master slots run in slot order (decision D135).
    env1..env3, voice and play take the part parameters by name. arp takes on or off first, then
    its settings. Values are written as the parameter shows them: units for numbers, names for
    choices, on and off for switches. Anything not written keeps its default, and naming a slot
    type loads that type's defaults.

    The parser only maps the structure onto parameter IDs; the parameters themselves read the
    values when a preset is applied, so a value means what the generic editor would show.
*/
namespace coolsynth::presets
{
    /** One setting of a preset: a parameter ID and its value as text. */
    struct PresetEntry
    {
        juce::String parameterId;
        juce::String text;
    };

    struct BankPreset
    {
        juce::String name;
        juce::String category;   // the room
        juce::String sourceFile; // bank file name, for the loudness tool
        std::vector<PresetEntry> entries;
        juce::String section;    // a family inside the room ("family = Strings"), or empty
    };

    struct BankParseResult
    {
        std::vector<BankPreset> presets;
        juce::StringArray errors; // "file:line: problem"
    };

    [[nodiscard]] BankParseResult parseBankFile(const juce::String& text, const juce::String& sourceFile);
}
