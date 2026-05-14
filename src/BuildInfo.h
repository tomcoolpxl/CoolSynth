#pragma once

#include <juce_core/juce_core.h>

#ifndef COOLSYNTH_BUILD_VERSION
#define COOLSYNTH_BUILD_VERSION "0.0.0"
#endif

namespace coolsynth::build
{
    inline juce::String getBuildIdentity()
    {
        return "Build: v" COOLSYNTH_BUILD_VERSION " | " __DATE__ " " __TIME__;
    }
}
