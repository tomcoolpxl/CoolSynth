#pragma once

#include <juce_core/juce_core.h>

namespace coolsynth
{
    // Whether a path read from a saved project or file names a place on this computer, so juce::File can take it.
    // On Windows, JUCE 9.0.5's File::isAbsolutePath also accepts "/Users/...", which a project saved on a Mac holds:
    // juce::File would then assert and guess the drive. So Windows needs a drive ("C:\") or a share ("\\server\").
    inline bool isFullPathHere(const juce::String& path)
    {
       #if JUCE_WINDOWS
        const auto isSlash = [](juce::juce_wchar c) { return c == '\\' || c == '/'; };
        if (path.length() >= 3 && juce::CharacterFunctions::isLetter(path[0]) && path[1] == ':' && isSlash(path[2]))
            return true;
        return path.length() >= 3 && isSlash(path[0]) && isSlash(path[1]) && ! isSlash(path[2]);
       #else
        return juce::File::isAbsolutePath(path);
       #endif
    }
}
