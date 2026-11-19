#include "ControllerMap.h"

#include <algorithm>

namespace coolsynth::midi
{
    namespace
    {
        struct PlaceInfo
        {
            const char* key;
            const char* name;
        };

        constexpr PlaceInfo placeInfo[] {
            { "mainTurn", "Main knob" },
            { "mainLeft", "Main knob, one step left" },
            { "mainRight", "Main knob, one step right" },
            { "mainPress", "Main knob press" },
            { "back", "Back" },
            { "shift", "Shift" },
            { "knob1", "Knob 1" },
            { "knob2", "Knob 2" },
            { "knob3", "Knob 3" },
            { "knob4", "Knob 4" },
            { "knob5", "Knob 5 (pages)" },
            { "knob6", "Knob 6 (Glide)" },
            { "knob7", "Knob 7 (Brightness)" },
            { "knob8", "Knob 8 (Space)" },
            { "fader1", "Fader 1 (layer A)" },
            { "fader2", "Fader 2 (layer B)" },
            { "fader3", "Fader 3 (effects)" },
            { "fader4", "Fader 4 (master)" },
            { "pad1", "Pad 1 (Home)" },
            { "pad2", "Pad 2 (Sound)" },
            { "pad3", "Pad 3 (Shape)" },
            { "pad4", "Pad 4 (Motion)" },
            { "pad5", "Pad 5 (Stop)" },
            { "pad6", "Pad 6 (Play)" },
            { "pad7", "Pad 7 (Record)" },
            { "pad8", "Pad 8 (Effects)" },
        };
        static_assert(std::size(placeInfo) == static_cast<std::size_t>(placeCount));

        const PlaceInfo& info(Place place) { return placeInfo[static_cast<std::size_t>(place)]; }

        bool sameControl(const ControlId& link, const ControlId& heard) noexcept
        {
            // A ready profile may leave the channel open (0): its maker's channel is not known for sure.
            return link.kind == heard.kind && link.number == heard.number && (link.channel == 0 || link.channel == heard.channel);
        }

        juce::var controlToVar(const ControlId& control)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("kind", control.kind == ControlId::Kind::note ? "note" : "cc");
            object->setProperty("channel", control.channel);
            object->setProperty("number", control.number);
            return object;
        }

        std::optional<ControlId> controlFromVar(const juce::var& value)
        {
            auto* object = value.getDynamicObject();
            if (object == nullptr)
                return std::nullopt;
            ControlId control;
            control.kind = object->getProperty("kind").toString() == "note" ? ControlId::Kind::note : ControlId::Kind::cc;
            control.channel = static_cast<int>(object->getProperty("channel"));
            control.number = static_cast<int>(object->getProperty("number"));
            if (control.channel < 0 || control.channel > 16 || control.number < 0 || control.number > 127 || isReserved(control))
                return std::nullopt;
            return control;
        }

        Encoding encodingFromKey(const juce::String& key)
        {
            for (auto e : { Encoding::absolute, Encoding::binaryOffset, Encoding::twosComplement, Encoding::signBit, Encoding::offsetGap })
                if (encodingKey(e) == key)
                    return e;
            return Encoding::absolute;
        }
    }

    juce::String ControlId::describe() const
    {
        return (kind == Kind::note ? "note " : "CC ") + juce::String(number)
             + (channel == 0 ? juce::String(", any channel") : ", channel " + juce::String(channel));
    }

    juce::String placeName(Place place) { return info(place).name; }
    juce::String placeKey(Place place) { return info(place).key; }

    std::optional<Place> placeFromKey(const juce::String& key)
    {
        for (int i = 0; i < placeCount; ++i)
            if (key == placeInfo[static_cast<std::size_t>(i)].key)
                return static_cast<Place>(i);
        return std::nullopt;
    }

    bool isButtonPlace(Place place) noexcept
    {
        switch (place)
        {
            case Place::mainLeft: case Place::mainRight: case Place::mainPress: case Place::back: case Place::shift:
                return true;
            default:
                return place >= Place::pad1 && place <= Place::pad8;
        }
    }

    const PlaceLink* ControllerMap::placeFor(const ControlId& control) const noexcept
    {
        for (const auto& link : places)
            if (sameControl(link.control, control))
                return &link;
        return nullptr;
    }

    const SettingLink* ControllerMap::settingFor(const ControlId& control) const noexcept
    {
        for (const auto& link : settings)
            if (sameControl(link.control, control))
                return &link;
        return nullptr;
    }

    const PlaceLink* ControllerMap::linkOf(Place place) const noexcept
    {
        for (const auto& link : places)
            if (link.place == place)
                return &link;
        return nullptr;
    }

    const SettingLink* ControllerMap::linkOf(const juce::String& parameterId) const noexcept
    {
        for (const auto& link : settings)
            if (link.parameterId == parameterId)
                return &link;
        return nullptr;
    }

    namespace
    {
        void letGo(ControllerMap& map, const ControlId& control)
        {
            // Either way round: a link left open on its channel and one heard on a channel are the same control.
            auto clash = [&control](const ControlId& other)
            {
                return sameControl(other, control) || sameControl(control, other);
            };
            map.places.erase(std::remove_if(map.places.begin(), map.places.end(), [&](const PlaceLink& l) { return clash(l.control); }),
                             map.places.end());
            map.settings.erase(std::remove_if(map.settings.begin(), map.settings.end(), [&](const SettingLink& l) { return clash(l.control); }),
                               map.settings.end());
        }
    }

    void ControllerMap::link(Place place, ControlId control, Encoding encoding)
    {
        if (! control.isValid() && control.channel != 0)
            return;
        if (isReserved(control))
            return;
        forget(place);
        letGo(*this, control);
        places.push_back({ place, control, encoding });
    }

    void ControllerMap::alsoLink(Place place, ControlId control, Encoding encoding)
    {
        if ((! control.isValid() && control.channel != 0) || isReserved(control))
            return;
        letGo(*this, control);
        places.push_back({ place, control, encoding });
    }

    void ControllerMap::link(const juce::String& parameterId, ControlId control, Encoding encoding)
    {
        if (parameterId.isEmpty() || isReserved(control))
            return;
        forget(parameterId);
        letGo(*this, control);
        settings.push_back({ parameterId, control, encoding });
    }

    void ControllerMap::forget(Place place)
    {
        places.erase(std::remove_if(places.begin(), places.end(), [place](const PlaceLink& l) { return l.place == place; }), places.end());
    }

    void ControllerMap::forget(const juce::String& parameterId)
    {
        settings.erase(std::remove_if(settings.begin(), settings.end(), [&](const SettingLink& l) { return l.parameterId == parameterId; }),
                       settings.end());
    }

    juce::var ControllerMap::toVar() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("version", 1);
        root->setProperty("name", name);
        root->setProperty("profile", profileId);
        juce::Array<juce::var> placeArray;
        for (const auto& link : places)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("place", placeKey(link.place));
            object->setProperty("control", controlToVar(link.control));
            object->setProperty("encoding", encodingKey(link.encoding));
            placeArray.add(object);
        }
        root->setProperty("places", placeArray);
        juce::Array<juce::var> settingArray;
        for (const auto& link : settings)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty("parameter", link.parameterId);
            object->setProperty("control", controlToVar(link.control));
            object->setProperty("encoding", encodingKey(link.encoding));
            settingArray.add(object);
        }
        root->setProperty("settings", settingArray);
        return root;
    }

    ControllerMap ControllerMap::fromVar(const juce::var& value)
    {
        ControllerMap map;
        auto* root = value.getDynamicObject();
        if (root == nullptr)
            return map;
        map.name = root->getProperty("name").toString();
        map.profileId = root->getProperty("profile").toString();
        if (auto* array = root->getProperty("places").getArray())
            for (const auto& item : *array)
                if (auto* object = item.getDynamicObject())
                    if (const auto place = placeFromKey(object->getProperty("place").toString()))
                        if (const auto control = controlFromVar(object->getProperty("control")))
                            map.alsoLink(*place, *control, encodingFromKey(object->getProperty("encoding").toString())); // a place may have several (D216)
        if (auto* array = root->getProperty("settings").getArray())
            for (const auto& item : *array)
                if (auto* object = item.getDynamicObject())
                    if (const auto control = controlFromVar(object->getProperty("control")))
                        map.link(object->getProperty("parameter").toString(), *control,
                                 encodingFromKey(object->getProperty("encoding").toString()));
        return map;
    }

    bool isReserved(const ControlId& control) noexcept
    {
        if (control.kind != ControlId::Kind::cc)
            return false;
        switch (control.number)
        {
            case 0: case 32:                      // bank select
            case 1:                               // mod wheel
            case 6: case 38: case 98: case 99: case 100: case 101: // data entry, NRPN and RPN
            case 64:                              // sustain
                return true;
            default:
                return control.number >= 120;     // all sound off, reset, local, all notes off, the modes
        }
    }

    int relativeSteps(Encoding encoding, int value) noexcept
    {
        switch (encoding)
        {
            case Encoding::binaryOffset:   return value == 0 ? 0 : value - 64; // some send a 0 between turns
            case Encoding::twosComplement: return value < 64 ? value : value - 128;
            case Encoding::signBit:        return (value & 64) != 0 ? -(value & 63) : (value & 63);
            case Encoding::offsetGap:      return value == 0 ? 0 : (value >= 64 ? value - 64 : value - 63);
            case Encoding::absolute:
            default:                       return 0;
        }
    }

    Encoding guessEncoding(const std::vector<int>& values) noexcept
    {
        // A position never repeats: a controller sends a value only when it changes. Only at an end does an endless
        // knob counting 0-127 send its end again (the MiniLab's do, D200). Turns repeat all the time: 65, 65, 65.
        bool repeats = false;
        for (std::size_t i = 1; i < values.size(); ++i)
            repeats = repeats || (values[i] == values[i - 1] && values[i] != 0 && values[i] != 127);
        if (! repeats || values.empty())
            return Encoding::absolute;

        auto all = [&values](auto&& test) { return std::all_of(values.begin(), values.end(), test); };
        auto any = [&values](auto&& test) { return std::any_of(values.begin(), values.end(), test); };
        const bool low = any([](int v) { return v >= 1 && v <= 31; });
        // Turned right first, as asked: 65 says 64 +/- steps; 1 says one of the other two, which the left turn tells.
        if (low && all([](int v) { return v == 0 || (v >= 1 && v <= 31) || (v >= 97 && v <= 127); }))
            return Encoding::twosComplement;
        if (low && all([](int v) { return v == 0 || (v >= 1 && v <= 31) || (v >= 65 && v <= 95); }))
            return Encoding::signBit;
        if (low && values.front() >= 1 && values.front() <= 31)
            return Encoding::twosComplement; // turned right only: the commoner of the two
        if (all([](int v) { return v == 0 || v >= 97; }))
            return Encoding::twosComplement; // turned left only: 127, 126 are -1, -2
        return Encoding::binaryOffset;
    }

    juce::String encodingKey(Encoding encoding)
    {
        switch (encoding)
        {
            case Encoding::binaryOffset:   return "binaryOffset";
            case Encoding::twosComplement: return "twosComplement";
            case Encoding::signBit:        return "signBit";
            case Encoding::offsetGap:      return "offsetGap";
            case Encoding::absolute:
            default:                       return "absolute";
        }
    }

    juce::String encodingName(Encoding encoding)
    {
        switch (encoding)
        {
            case Encoding::binaryOffset:   return "endless knob";
            case Encoding::twosComplement: return "endless knob";
            case Encoding::signBit:        return "endless knob";
            case Encoding::offsetGap:      return "endless knob";
            case Encoding::absolute:
            default:                       return "knob or fader";
        }
    }

    // ---- Ready profiles ----------------------------------------------------------------------------------------
    // From the makers' manuals and setup guides, checked on 2026-11-18 (D211). A channel of 0 accepts any channel where
    // the factory channel is not stated for sure. Every profile is a start: the guided setup changes any of it.

    namespace
    {
        ControlId cc(int number, int channel = 0) { return { ControlId::Kind::cc, channel, number }; }
        ControlId note(int number, int channel) { return { ControlId::Kind::note, channel, number }; }

        void knobs(ControllerMap& map, std::initializer_list<int> numbers, Encoding encoding = Encoding::absolute, int channel = 0)
        {
            int knob = 1;
            for (const int n : numbers)
                map.link(knobPlace(knob++), cc(n, channel), encoding);
        }

        void faders(ControllerMap& map, std::initializer_list<int> numbers, int channel = 0)
        {
            int fader = 0;
            for (const int n : numbers)
                map.link(static_cast<Place>(static_cast<int>(Place::fader1) + fader++), cc(n, channel), Encoding::absolute);
        }

        void padNotes(ControllerMap& map, int firstNote, int channel)
        {
            for (int pad = 0; pad < 8; ++pad)
                map.link(static_cast<Place>(static_cast<int>(Place::pad1) + pad), note(firstNote + pad, channel), Encoding::absolute);
        }

        std::vector<ReadyProfile> makeProfiles()
        {
            std::vector<ReadyProfile> profiles;
            auto add = [&profiles](const char* id, const char* name, juce::StringArray ports, const char* note)
            {
                ReadyProfile p;
                p.id = id;
                p.name = name;
                p.portNames = std::move(ports);
                p.note = note;
                p.map.name = name;
                p.map.profileId = id;
                profiles.push_back(std::move(p));
                return &profiles.back().map;
            };

            // Akai MPK mini mk3, program 1: knobs CC 70-77; pads bank A notes 36-43 on channel 10.
            auto* mpk = add("akai.mpkmini3", "Akai MPK mini mk3", { "MPK mini 3" },
                            "Works as the keyboard comes. It has no knob for browsing: use the mouse, or pick one with Set up step by step.");
            knobs(*mpk, { 70, 71, 72, 73, 74, 75, 76, 77 });
            padNotes(*mpk, 36, 10);

            // Novation Launchkey Mini MK3 and MK4, as they start: the knobs in Custom mode send CC 21-28.
            auto* lk3 = add("novation.launchkeymini3", "Novation Launchkey Mini MK3", { "Launchkey Mini MK3", "Launchkey Mini MIDI" },
                            "Its knobs work as the keyboard comes. For its pads and a browsing knob, use Set up step by step.");
            knobs(*lk3, { 21, 22, 23, 24, 25, 26, 27, 28 });
            auto* lk4 = add("novation.launchkeymini4", "Novation Launchkey Mini MK4", { "Launchkey Mini MK4" },
                            "Its knobs work as the keyboard comes. For its pads and a browsing knob, use Set up step by step.");
            knobs(*lk4, { 21, 22, 23, 24, 25, 26, 27, 28 });

            // Arturia KeyLab Essential mk3, program User 1: knobs and faders; pads bank A notes 36-43 on channel 10.
            auto* kle = add("arturia.keylabessential3", "Arturia KeyLab Essential mk3", { "KL Essential" },
                            "On the keyboard, choose the User 1 program with the Prog button. For a browsing knob, use Set up step by step.");
            knobs(*kle, { 74, 71, 76, 77, 93, 18, 19, 16 });
            faders(*kle, { 73, 75, 79, 72 });
            padNotes(*kle, 36, 10);

            // Arturia MiniLab 3 in its Arturia program (D211; tested on the device in V2): without CoolSynth's own
            // driver, which talks to it in its DAW program and lights its screen and pads (D184).
            auto* ml3 = add("arturia.minilab3", "Arturia MiniLab 3", { "Minilab3", "MiniLab 3" },
                            "For a MiniLab 3 with the MiniLab 3 driver switched Off, in its Arturia or its DAW program: no screen and no pad lights.");
            knobs(*ml3, { 74, 71, 76, 77, 93, 18, 19, 16 });
            faders(*ml3, { 82, 83, 85, 17 });
            ml3->link(Place::mainTurn, cc(114), Encoding::binaryOffset);
            padNotes(*ml3, 36, 10);
            // The same MiniLab in its DAW program, where CoolSynth's driver leaves it: no message switches its program
            // (only Shift + pad 3 does), so both programs' numbers are linked (D216; docs/minilab3-screen.md).
            int knob = 1;
            for (const int n : { 86, 87, 89, 90, 110, 111, 116, 117 })
                ml3->alsoLink(knobPlace(knob++), cc(n), Encoding::absolute);
            int fader = 0;
            for (const int n : { 14, 15, 30, 31 })
                ml3->alsoLink(static_cast<Place>(static_cast<int>(Place::fader1) + fader++), cc(n), Encoding::absolute);
            ml3->alsoLink(Place::mainTurn, cc(28), Encoding::offsetGap);  // 65 one click right, 62 one left
            ml3->alsoLink(Place::mainTurn, cc(29), Encoding::offsetGap);  // turned with Shift held
            ml3->alsoLink(Place::mainPress, cc(118), Encoding::absolute);
            ml3->alsoLink(Place::mainPress, cc(119), Encoding::absolute); // pressed with Shift held
            ml3->alsoLink(Place::shift, cc(27), Encoding::absolute);

            // Native Instruments Komplete Kontrol M32 and A series in MIDI mode (Shift + Plug-in): knobs CC 14-21.
            auto* nik = add("ni.kompletekontrol", "NI Komplete Kontrol M32 / A series", { "KOMPLETE KONTROL M32", "KOMPLETE KONTROL A" },
                            "On the keyboard, press Shift + Plug-in for MIDI mode. For a browsing knob, use Set up step by step.");
            knobs(*nik, { 14, 15, 16, 17, 18, 19, 20, 21 });
            return profiles;
        }
    }

    const std::vector<ReadyProfile>& readyProfiles()
    {
        static const std::vector<ReadyProfile> profiles = makeProfiles();
        return profiles;
    }

    const ReadyProfile* readyProfile(const juce::String& id)
    {
        for (const auto& p : readyProfiles())
            if (p.id == id)
                return &p;
        return nullptr;
    }

    const ReadyProfile* readyProfileForPorts(const juce::StringArray& inputNames)
    {
        for (const auto& input : inputNames)
            for (const auto& p : readyProfiles())
                for (const auto& port : p.portNames)
                    if (input.containsIgnoreCase(port))
                        return &p;
        return nullptr;
    }
}
