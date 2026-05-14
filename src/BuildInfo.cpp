#include "BuildInfo.h"
#include "GeneratedBuildInfo.h"

namespace coolsynth::build
{
    juce::String getBuildIdentity()
    {
        return DYNAMIC_BUILD_IDENTITY;
    }
}
