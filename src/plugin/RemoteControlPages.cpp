#include "RemoteControlPages.h"

#include <algorithm>

#include "interface/InterfaceModel.h"
#include "parameters/ParameterIDs.h"
#include "parameters/v3/PartParameters.h"

namespace coolsynth::plugin
{
    namespace
    {
        using coolsynth::ui::InterfaceModel;
        namespace pv = coolsynth::parameters::v3;

        juce::String pageName(const coolsynth::ui::Slot& slot, const coolsynth::ui::Page& page)
        {
            return page.name.isEmpty() || page.name == slot.label ? slot.label : slot.label + " " + page.name;
        }
    }

    std::vector<RemotePage> remotePages(const InterfaceModel& model)
    {
        std::vector<RemotePage> pages;
        const auto part = model.part() == 0 ? pv::Part::a : pv::Part::b;
        namespace ids = coolsynth::parameters::ids;
        const std::array<juce::String, 4> fixed { pv::voiceParameterId(part, "glide"), ids::brightness, ids::space, ids::volumeMaster };

        auto add = [&pages, &fixed](const juce::String& section, const juce::String& name, const coolsynth::ui::Page& page)
        {
            RemotePage remote;
            remote.section = section;
            remote.name = name;
            for (std::size_t knob = 0; knob < 4; ++knob)
                remote.parameterIds[knob] = page.ids[knob];
            for (std::size_t knob = 0; knob < 4; ++knob)
                remote.parameterIds[4 + knob] = fixed[knob];
            // An id from the page's place on the panel, so a host keeps its place when the list is rebuilt; another
            // page that happens to hash alike takes the next free number.
            auto id = static_cast<std::uint32_t>(juce::String(section + "/" + name).hashCode()) & 0x7fffffffu;
            for (bool taken = true; taken;)
            {
                taken = std::any_of(pages.begin(), pages.end(), [id](const RemotePage& p) { return p.id == id; });
                if (taken)
                    id = (id + 1) & 0x7fffffffu;
            }
            remote.id = id;
            pages.push_back(std::move(remote));
        };

        const auto live = model.livePageNames();
        for (int i = 0; i < live.size(); ++i)
            add("Home", live[i], model.livePageAt(i));
        for (int section = 0; section < InterfaceModel::sectionCount; ++section)
            for (const auto& slot : model.slotsOf(section))
                if (! slot.empty)
                    for (const auto& page : slot.pages)
                        add(InterfaceModel::sectionName(section), pageName(slot, page), page);
        return pages;
    }

    int openRemotePage(const InterfaceModel& model, const std::vector<RemotePage>& pages)
    {
        juce::String section, name;
        if (model.view() == InterfaceModel::View::home)
        {
            const auto live = model.livePageNames();
            if (model.livePage() < 0 || model.livePage() >= live.size())
                return -1;
            section = "Home";
            name = live[model.livePage()];
        }
        else
        {
            section = InterfaceModel::sectionName(model.section());
            name = pageName(model.currentSlot(), model.currentPage());
        }
        for (std::size_t i = 0; i < pages.size(); ++i)
            if (pages[i].section == section && pages[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    bool samePages(const std::vector<RemotePage>& a, const std::vector<RemotePage>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].parameterIds != b[i].parameterIds)
                return false;
        return true;
    }
}
