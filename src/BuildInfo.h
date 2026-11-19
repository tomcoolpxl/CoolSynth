#pragma once

#include <juce_core/juce_core.h>

namespace coolsynth::build
{
    juce::String getBuildIdentity();
    /** The version the build was made as (CMakeLists.txt's project version, "0.3.0"): Preferences shows it. */
    juce::String getVersion();
}
