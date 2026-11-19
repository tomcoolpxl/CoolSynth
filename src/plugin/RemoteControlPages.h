#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

namespace coolsynth::ui
{
    class InterfaceModel;
}

/*
    The panel's pages as CLAP remote control pages (D216, TODO C5): a controller's eight knobs follow the page the panel
    shows, in the hosts that read the pages (Bitwig, Reaper, FL Studio, Studio One). Each page holds the panel page's
    knobs 1-4, then the knobs that are the same on every page: Glide, Brightness, Space and, in the page knob's place,
    the master volume, as the first eight parameters do (D216). The pages are those of the layer the panel edits;
    they change when it changes layer or a slot changes type. Message thread.
*/
namespace coolsynth::plugin
{
    struct RemotePage
    {
        juce::String section;                     // "Home", "Sound", "Shape", "Motion", "Effects"
        juce::String name;                        // "Macros", "Gen 1 Osc"
        std::uint32_t id = 0;                     // stable for as long as the page exists
        std::array<juce::String, 8> parameterIds; // empty: no parameter on that knob
    };

    /** Every page of the panel, in the panel's order: Home's live pages, then each section's slots and their pages. */
    [[nodiscard]] std::vector<RemotePage> remotePages(const coolsynth::ui::InterfaceModel& model);

    /** The page the panel shows now, as an index into `pages`, or -1. */
    [[nodiscard]] int openRemotePage(const coolsynth::ui::InterfaceModel& model, const std::vector<RemotePage>& pages);

    /** Two lists the host would see the same: the same pages with the same parameters. */
    [[nodiscard]] bool samePages(const std::vector<RemotePage>& a, const std::vector<RemotePage>& b);
}
