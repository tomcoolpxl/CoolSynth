#pragma once

#include <functional>
#include <vector>

#include "McpServer.h"

class SynthAudioProcessor;
namespace juce
{
    class Component;
}

namespace coolsynth::mcp
{
    /** The port the app's MCP server listens on, on 127.0.0.1 (D225). */
    inline constexpr int defaultPort = 47613;

    /**
        CoolSynth's tools (D225), the user's four: read and set the sound (slots, values as the screen shows them,
        presets, a slot's own file), play notes and measure what comes out, a picture of the window, the flight log.
        `window` gives the component to picture, or null; `runner` runs a job on the message thread (the play tool
        waits between its notes on the server's thread).
    */
    [[nodiscard]] std::vector<Tool> coolSynthTools(SynthAudioProcessor& synth, std::function<juce::Component*()> window, Server::Runner runner);
}
