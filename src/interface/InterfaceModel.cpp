#include "InterfaceModel.h"

#include <algorithm>
#include <cmath>

#include "midi/ControllerSetup.h"
#include "parameters/ParameterIDs.h"
#include "parameters/v3/PartParameters.h"
#include "parameters/v3/SlotParameters.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "presets/MySounds.h"
#include "synth/v3/SampleLibrary.h"
#include "synth/v3/UserLibrary.h"

namespace coolsynth::ui
{
    namespace pv = coolsynth::parameters::v3;

    namespace
    {
        const std::array<const char*, 4> macroRoles { "Tone", "Character", "Time", "Movement" };

        int clampInt(int value, int low, int high) noexcept { return juce::jlimit(low, high, value); }

        juce::String withoutPrefix(juce::String name)
        {
            // "Env 1 Attack" -> "Attack": the slot is shown elsewhere
            for (const auto* prefix : { "Env 1 ", "Env 2 ", "Env 3 ", "LFO 1 ", "LFO 2 " })
                if (name.startsWith(prefix))
                    return name.substring(static_cast<int>(std::strlen(prefix)));
            return name;
        }

        /** How many positions a parameter has when it is a list (choice, switch, whole numbers, a slot value with
            option names); 0 when it is continuous. */
        int optionCount(const juce::RangedAudioParameter& parameter)
        {
            if (auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(&parameter))
                return choice->choices.size();
            if (dynamic_cast<const juce::AudioParameterBool*>(&parameter) != nullptr)
                return 2;
            if (auto* integer = dynamic_cast<const juce::AudioParameterInt*>(&parameter))
                return integer->getRange().getLength() + 1;
            if (auto* generic = dynamic_cast<const pv::GenericSlotParameter*>(&parameter))
            {
                // the ID ends in ".p<n>": the n-th value of the slot's current type
                const int n = parameter.getParameterID().fromLastOccurrenceOf(".p", false, false).getIntValue();
                const auto& descriptor = generic->currentDescriptor();
                if (n >= 1 && n <= descriptor.usedParams)
                    if (const char* options = descriptor.params[static_cast<std::size_t>(n - 1)].options)
                        return juce::StringArray::fromTokens(options, "|", {}).size();
            }
            return 0;
        }

        /** The slot value's descriptor when a parameter is a generic slot value in semitones, else null. */
        const coolsynth::synth::v3::ParameterDescriptor* semitoneDescriptor(const juce::RangedAudioParameter& parameter)
        {
            if (auto* generic = dynamic_cast<const pv::GenericSlotParameter*>(&parameter))
            {
                const int n = parameter.getParameterID().fromLastOccurrenceOf(".p", false, false).getIntValue();
                const auto& descriptor = generic->currentDescriptor();
                if (n >= 1 && n <= descriptor.usedParams)
                {
                    const auto& d = descriptor.params[static_cast<std::size_t>(n - 1)];
                    if (d.unit == coolsynth::synth::v3::Unit::semitones && d.skew == coolsynth::synth::v3::Skew::linear)
                        return &d;
                }
            }
            return nullptr;
        }
    }

    juce::Colour miniLabColour(Tint tint) noexcept
    {
        // What the MiniLab 3's pad LEDs need to show each colour, by the user's eye on the device (D184): its LEDs
        // are bluer and pinker than a screen. Home is what Play was sent before (it lit teal); Play a plain green,
        // Stop a white with its green full and less red and blue (white lit blue, a warm white pink), Sound a redder
        // orange (orange lit light pink).
        switch (tint)
        {
            case Tint::sound:  return juce::Colour(0xffff5000);
            case Tint::home:   return juce::Colour(0xff4aff8e); // the teal Play was sent, at the LEDs' full brightness
            case Tint::shape:  return juce::Colour(0xff1060ff); // a clear blue, apart from Home's teal and Motion's purple
            case Tint::stop:   return juce::Colour(0xffd7ffbe); // white lit blue, a warm white pink: green full
            case Tint::play:   return juce::Colour(0xff10ff00);
            case Tint::partB:  return juce::Colour(0xffffc000); // Home while part B is on: yellow (D185)
            default:           return tintColour(tint); // Motion, Effects and Record light as they should
        }
    }

    juce::Colour tintColour(Tint tint) noexcept
    {
        switch (tint)
        {
            case Tint::sound:   return juce::Colour(0xfff2a33a);
            case Tint::shape:   return juce::Colour(0xff3b8bff); // a clear blue, not dark (D184; was cyan #35b8e0)
            case Tint::motion:  return juce::Colour(0xff9b7bff);
            case Tint::effects: return juce::Colour(0xffec5fa6);
            case Tint::perform: return juce::Colour(0xffa4d23f);
            case Tint::home:    return juce::Colour(0xff23c4b5);
            case Tint::stop:    return juce::Colour(0xffffffff);
            case Tint::play:    return juce::Colour(0xff3ccf73);
            case Tint::record:  return juce::Colour(0xffff0000); // pure red: the pad's LED shows #f0524f as pink
            case Tint::partB:   return juce::Colour(0xfff2c94c); // Home while part B is on: a warm yellow (D185)
        }
        return juce::Colours::grey;
    }

    juce::StringArray enginePillsOf(const std::function<juce::String(const juce::String& id, const juce::String& otherwise)>& entry)
    {
        const auto mode = entry(pv::partModeParameterId, "A");
        juce::StringArray parts;
        if (mode != "B")
            parts.add("pA");
        if (mode == "B" || mode == "Stack" || mode == "Split")
            parts.add("pB");

        juce::StringArray pills;
        bool noise = false;
        for (const auto& part : parts)
            for (int g = 1; g <= 3; ++g)
            {
                const auto type = entry(part + ".g" + juce::String(g) + ".type", g == 1 ? "VA" : "Off");
                juce::String pill;
                if (type == "VA")
                    pill = "VA";
                else if (type == "Sample")
                    pill = "Sample";
                else if (type == "FM2" || type == "FM4")
                    pill = "FM";
                else if (type == "Wavetable")
                    pill = "WT";
                else if (type == "Grain")
                    pill = "Grain";
                else if (type == "String" || type == "Modal")
                    pill = "Physical";
                else if (type == "Noise")
                    noise = true;
                if (pill.isNotEmpty() && pills.size() < 3)
                    pills.addIfNotAlreadyThere(pill);
            }
        if (pills.isEmpty() && noise)
            pills.add("Noise");
        // No generator at all: the sound is a filter ringing on its own (Siren's ladder at full resonance).
        if (pills.isEmpty())
            for (const auto& part : parts)
                for (int p = 1; p <= 3 && pills.isEmpty(); ++p)
                    if (entry(part + ".p" + juce::String(p) + ".type", p == 1 ? "Ladder" : "Off") != "Off")
                        pills.add("Filter");
        return pills;
    }

    juce::StringArray enginePills(int factoryPreset)
    {
        // Read from the preset's own entries; what a preset leaves out is the parameter's default: g1 VA, g2 and g3
        // Off, part A alone.
        static const std::vector<juce::StringArray> cache = []
        {
            std::vector<juce::StringArray> all;
            for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
            {
                const auto& preset = coolsynth::presets::getFactoryPreset(i);
                all.push_back(enginePillsOf([&](const juce::String& id, const juce::String& otherwise)
                {
                    juce::String text = otherwise;
                    for (const auto& e : preset.entries)
                        if (e.parameterId == id)
                            text = e.text; // the last one counts, as when it is applied
                    return text;
                }));
            }
            return all;
        }();
        return factoryPreset >= 0 && factoryPreset < static_cast<int>(cache.size()) ? cache[static_cast<std::size_t>(factoryPreset)]
                                                                                    : juce::StringArray();
    }

    InterfaceModel::InterfaceModel(SynthAudioProcessor& processor)
        : synth(processor), state(processor.getValueTreeState())
    {
        refreshMySounds();
        browser.category = categoryOfPreset(playingPreset());
        browser.row = 1;
        openHome(); // CoolSynth opens in Home, on the sound playing (D186)
    }

    // ---- The bank ---------------------------------------------------------------------------

    void InterfaceModel::refreshMySounds()
    {
        // The rooms: My Sounds first when it holds anything (D180), then the factory's in bank order.
        mySoundFiles.clear();
        for (const auto& file : coolsynth::presets::MySounds::list())
            mySoundFiles.push_back(file);
        bank.clear();
        if (! mySoundFiles.empty())
        {
            bank.push_back({ "My Sounds", {} });
            for (int k = 0; k < static_cast<int>(mySoundFiles.size()); ++k)
                bank.back().presets.push_back(mySoundBase + k);
        }
        for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
        {
            const auto& preset = coolsynth::presets::getFactoryPreset(i);
            if (bank.empty() || bank.back().name != preset.category)
                bank.push_back({ preset.category, {} });
            bank.back().presets.push_back(i);
        }
        browser.category = clampInt(browser.category, -1, static_cast<int>(bank.size()) - 1);
        touched();
    }

    int InterfaceModel::categoryOfPreset(int preset) const noexcept
    {
        for (int c = 0; c < static_cast<int>(bank.size()); ++c)
            for (int p : bank[static_cast<std::size_t>(c)].presets)
                if (p == preset)
                    return c;
        return 0;
    }

    int InterfaceModel::playingPreset() const
    {
        // The sound the edited layer plays (D194). A saved sound has its own id past the factory's (D180).
        const auto& sound = synth.layerSound(currentPart);
        if (const auto mine = sound.mySound; mine != juce::File())
        {
            for (std::size_t k = 0; k < mySoundFiles.size(); ++k)
                if (mySoundFiles[k] == mine)
                    return mySoundBase + static_cast<int>(k);
            return -1;
        }
        return sound.program; // -1: none, or a sound the bank does not have (D193)
    }

    juce::String InterfaceModel::soundName(int sound) const
    {
        if (sound >= mySoundBase)
        {
            const auto k = static_cast<std::size_t>(sound - mySoundBase);
            return k < mySoundFiles.size() ? mySoundFiles[k].getFileNameWithoutExtension() : juce::String();
        }
        return coolsynth::presets::getFactoryPreset(sound).name;
    }

    juce::StringArray InterfaceModel::soundPills(int sound) const
    {
        if (sound >= mySoundBase)
        {
            const auto k = static_cast<std::size_t>(sound - mySoundBase);
            return k < mySoundFiles.size() ? SynthAudioProcessor::mySoundPills(mySoundFiles[k]) : juce::StringArray();
        }
        auto pills = enginePills(sound);
        if (SynthAudioProcessor::isCombination(sound))
            pills.insert(0, "A+B"); // a combination of both layers: it loads whole (D194)
        return pills;
    }

    juce::String InterfaceModel::playingPresetName() const { return synth.getPatchName(); }

    void InterfaceModel::loadPreset(int preset)
    {
        synth.keepEditForRecall(); // an edited sound stays reachable, first in the list of rooms (D225)
        if (preset >= mySoundBase)
        {
            const auto k = static_cast<std::size_t>(preset - mySoundBase);
            if (k < mySoundFiles.size())
                synth.loadMySoundIntoLayer(mySoundFiles[k], currentPart);
        }
        else
            synth.loadSoundIntoLayer(preset, currentPart); // into the layer Home is on; a combination whole (D194)
    }

    // ---- Hold and the octave, for the screen's keys when no MiniLab is connected (D184) -----------------------

    void InterfaceModel::resetView()
    {
        currentPart = 0;
        currentLivePage = 0;
        openHome();
        touched();
    }

    void InterfaceModel::togglePart()
    {
        currentPart = 1 - currentPart;
        touched();
    }

    void InterfaceModel::toggleHold()
    {
        hold = ! hold;
        synth.setScreenHold(hold);
        touched();
    }

    void InterfaceModel::shiftOctave(int steps)
    {
        octave = clampInt(octave + steps, -maxOctaveShift, maxOctaveShift);
        touched();
    }

    int InterfaceModel::keyboardKeys() const noexcept
    {
        return hardware ? 25 : coolsynth::midi::ControllerSetup::instance().keyboardKeys();
    }

    int InterfaceModel::homeLowKey(int keys) noexcept
    {
        if (keys >= 88)
            return 21;
        return keys >= 49 ? 36 : 48;
    }

    int InterfaceModel::keyboardLowKey() const noexcept
    {
        const int keys = keyboardKeys();
        return homeLowKey(keys) + (keys == keyboardOctaveKeys ? 12 * keyboardOctave : 0);
    }

    void InterfaceModel::keyPlayed(int note) noexcept
    {
        const int keys = keyboardKeys();
        if (keys == 0)
            return;
        if (keys != keyboardOctaveKeys)
        {
            keyboardOctaveKeys = keys;
            keyboardOctave = 0;
        }
        const int home = homeLowKey(keys), span = keys - 1;
        const int low = home + 12 * keyboardOctave;
        int octaveNow = keyboardOctave;
        if (note < low)
            octaveNow = static_cast<int>(std::floor(static_cast<double>(note - home) / 12.0));
        else if (note > low + span)
            octaveNow = static_cast<int>(std::ceil(static_cast<double>(note - home - span) / 12.0));
        // Four octaves each way, as the MiniLab shifts, and never past MIDI's 128 notes.
        octaveNow = clampInt(octaveNow, juce::jmax(-4, -(home / 12)), juce::jmin(4, (127 - home - span) / 12));
        if (octaveNow != keyboardOctave)
        {
            keyboardOctave = octaveNow;
            touched();
        }
    }

    void InterfaceModel::setHardwareConnected(bool connected)
    {
        if (connected == hardware)
            return;
        hardware = connected;
        // The MiniLab has its own Hold and octave: the screen's let go of theirs.
        if (hardware && hold)
            toggleHold();
        octave = 0;
        touched();
    }

    // ---- Sections and slots -------------------------------------------------------------------

    juce::String InterfaceModel::sectionName(int section)
    {
        static const juce::StringArray names { "Sound", "Shape", "Motion", "Effects" };
        return names[clampInt(section, 0, sectionCount - 1)];
    }

    Tint InterfaceModel::sectionTint(int section) noexcept
    {
        static constexpr std::array<Tint, sectionCount> tints { Tint::sound, Tint::shape, Tint::motion, Tint::effects };
        return tints[static_cast<std::size_t>(clampInt(section, 0, sectionCount - 1))];
    }

    std::vector<SectionRow> InterfaceModel::rowsOf(int section)
    {
        const juce::String arrow = juce::String(juce::CharPointer_UTF8("\xe2\x86\x92"));
        switch (section)
        {
            case 0: return { { "mixed", { 0, 1, 2 }, "+", arrow + " Shape" } };
            case 1: return { { "in a row", { 0, 1, 2 }, arrow, arrow + " Effects" } };
            case 2: return { { "envelopes", { 0, 1, 2 }, "", "" }, { "LFOs", { 3, 4 }, "", "" } };
            // the layer's six slots, then the chain at the end, the whole sound's (D194)
            default: return { { "layer", { 0, 1, 2 }, arrow, "" }, { "", { 3, 4, 5 }, arrow, "" },
                              { "master", { 6, 7, 8 }, arrow, "" }, { "", { 9, 10, 11 }, arrow, "" } };
        }
    }

    bool InterfaceModel::currentLayerEmpty() const { return layerEmpty(currentPart); }

    juce::String InterfaceModel::layerSoundName(int layer) const
    {
        const auto& name = synth.layerSound(layer).name; // each layer's own sound (D194)
        return name.isNotEmpty() ? name : synth.getPatchName();
    }

    juce::String InterfaceModel::layerModeText() const
    {
        namespace pv3 = coolsynth::parameters::v3;
        auto* mode = parameter(pv3::partModeParameterId);
        if (mode == nullptr)
            return {};
        auto text = mode->getCurrentValueAsText().toUpperCase();
        if (text == "VELOCITY")
            if (auto* split = parameter(pv3::velocitySplitParameterId))
                text << " " << split->getCurrentValueAsText();
        return text;
    }

    bool InterfaceModel::layerEmpty(int layer) const
    {
        const juce::String prefix = layer == 0 ? "pA" : "pB";
        for (int s = 1; s <= 3; ++s)
            for (const char* kind : { ".g", ".p" })
                if (auto* type = synth.getValueTreeState().getRawParameterValue(prefix + kind + juce::String(s) + ".type");
                    type != nullptr && juce::roundToInt(type->load()) != 0)
                    return false;
        return true;
    }

    juce::RangedAudioParameter* InterfaceModel::parameter(const juce::String& id) const
    {
        return id.isEmpty() ? nullptr : state.getParameter(id);
    }

    int InterfaceModel::openSlotOf(int layer, int section) const noexcept
    {
        if (currentView != View::section || currentSection != section || currentPart != layer)
            return -1;
        return slotCursor[static_cast<std::size_t>(clampInt(section, 0, sectionCount - 1))];
    }

    std::vector<Slot> InterfaceModel::slotsOf(int section, int layer) const
    {
        const auto part = layer == 0 ? pv::Part::a : pv::Part::b;
        std::vector<Slot> slots;

        auto typedSlot = [&](pv::SlotPath path, const juce::String& code, const juce::String& label)
        {
            Slot slot;
            slot.code = code;
            slot.label = label;
            slot.typeId = pv::typeParameterId(path);
            auto* type = dynamic_cast<juce::AudioParameterChoice*>(parameter(slot.typeId));
            slot.typeName = type != nullptr ? type->getCurrentChoiceName() : juce::String("?");
            slot.empty = type == nullptr || type->getIndex() == 0;
            if (! slot.empty)
            {
                if (auto* first = dynamic_cast<pv::GenericSlotParameter*>(parameter(pv::genericParameterId(path, 1))))
                {
                    const auto& descriptor = first->currentDescriptor();
                    for (int start = 0; start < descriptor.usedParams; start += 4)
                    {
                        Page page;
                        page.name = descriptor.params[static_cast<std::size_t>(start)].name;
                        for (int k = 0; k < 4 && start + k < descriptor.usedParams; ++k)
                            page.ids[static_cast<std::size_t>(k)] = pv::genericParameterId(path, start + k + 1);
                        slot.pages.push_back(page);
                    }
                }
                if (path.kind == pv::SlotKind::generator)
                {
                    slot.pages.push_back({ "Mix",
                                           { pv::fixedParameterId(path, "level"), pv::fixedParameterId(path, "pan"),
                                             pv::fixedParameterId(path, "oct"), pv::fixedParameterId(path, "fine") },
                                           { "Level", "Pan", "Octave", "Fine" } });
                    if (path.index > 1)
                        slot.pages.push_back({ "Link",
                                               { pv::fixedParameterId(path, "combine"), pv::fixedParameterId(path, "amount"), {}, {} },
                                               { "Combine", "Amount", {}, {} } });
                }
            }
            slots.push_back(std::move(slot));
        };

        switch (section)
        {
            case 0:
                for (int i = 1; i <= pv::partGeneratorSlots; ++i)
                    typedSlot({ part, pv::SlotKind::generator, i }, "G" + juce::String(i), "Gen " + juce::String(i));
                break;
            case 1:
                for (int i = 1; i <= pv::partProcessorSlots; ++i)
                    typedSlot({ part, pv::SlotKind::processor, i }, "P" + juce::String(i), "Shaper " + juce::String(i));
                break;
            case 2:
            {
                static const juce::StringArray envelopeNames { "Amp env", "Filter env", "Mod env" };
                for (int i = 1; i <= pv::partEnvelopes; ++i)
                {
                    auto id = [&](const char* field) { return pv::envelopeParameterId(part, i, field); };
                    slots.push_back({ "E" + juce::String(i), envelopeNames[i - 1], {}, "Env", false,
                                      { { "Envelope", { id("attack"), id("decay"), id("sustain"), id("release") },
                                                      { "Attack", "Decay", "Sustain", "Release" } },
                                        { "Timing", { id("delay"), id("hold"), id("velocity"), id("keyTime") },
                                                    { "Delay", "Hold", "Velocity", "Key time" } },
                                        { "Curves", { id("attackCurve"), id("decayCurve"), id("releaseCurve"), id("loop") },
                                                    { "Attack curve", "Decay curve", "Release curve", "Loop" } } } });
                }
                for (int i = 1; i <= pv::partLocalLfos; ++i)
                {
                    const juce::String lfo = "lfo" + juce::String(i);
                    auto id = [&](const char* field) { return pv::lfoParameterId(part, lfo, field); };
                    slots.push_back({ "L" + juce::String(i), "LFO " + juce::String(i), {}, "LFO", false,
                                      { { "LFO", { id("shape"), id("rate"), id("delay"), id("fade") },
                                                 { "Shape", "Rate", "Delay", "Fade" } },
                                        { "Sync", { id("sync"), id("division"), id("phase"), id("retrigger") },
                                                  { "Sync", "Division", "Phase", "Retrigger" } } } });
                }
                break;
            }
            default:
                for (int i = 1; i <= pv::partInsertSlots; ++i)
                    typedSlot({ part, pv::SlotKind::insert, i }, "I" + juce::String(i), "Insert " + juce::String(i));
                for (int i = 1; i <= pv::masterEffectSlots; ++i)
                    typedSlot({ part, pv::SlotKind::master, i }, "F" + juce::String(i), "Master " + juce::String(i));
                break;
        }
        return slots;
    }

    juce::String InterfaceModel::slotKey(int section, int slot) const
    {
        return juce::String(currentPart) + "." + juce::String(section) + "." + juce::String(slot);
    }

    int InterfaceModel::pageIndexOf(int section, int slot) const
    {
        const auto found = pageOfSlot.find(slotKey(section, slot));
        return found == pageOfSlot.end() ? 0 : found->second;
    }

    bool InterfaceModel::editingMaster() const
    {
        return currentView == View::section && currentSlot().code.startsWith("F");
    }

    Slot InterfaceModel::currentSlot() const
    {
        const auto slots = slotsOf(currentSection);
        return slots[static_cast<std::size_t>(clampInt(slotCursor[static_cast<std::size_t>(currentSection)], 0,
                                                       static_cast<int>(slots.size()) - 1))];
    }

    juce::StringArray InterfaceModel::livePageNames() const { return { "Macros", "Arp", "Groove", "Chord", "Voice", "Layers" }; }

    juce::StringArray InterfaceModel::layerActionNames(int layer) const
    {
        layer = clampInt(layer, 0, 1);
        const juce::String x = layer == 0 ? "A" : "B", y = layer == 0 ? "B" : "A";
        const bool solo = synth.soloLayer() == layer;
        return { "Copy " + x + " to " + y, "Copy " + y + " to " + x, "Swap A and B", "Double " + x + " onto " + y,
                 x + " an octave lower on " + y, x + " an octave higher on " + y,
                 solo ? "Solo " + x + ": on" : "Solo " + x, "Empty layer " + y };
    }

    void InterfaceModel::doLayerAction(int action, int layer)
    {
        const int x = clampInt(layer, 0, 1), y = 1 - x;
        switch (action)
        {
            case 0: synth.copyLayer(x, y); break;
            case 1: synth.copyLayer(y, x); break;
            case 2: synth.swapLayers(); break;
            case 3: synth.doubleLayer(x); break;
            case 4: synth.octaveLayer(x, -12); break;
            case 5: synth.octaveLayer(x, 12); break;
            case 6: synth.setSoloLayer(synth.soloLayer() == x ? -1 : x); break;
            case 7: synth.emptyLayer(y); break;
            default: break;
        }
    }

    Page InterfaceModel::livePageAt(int livePage) const
    {
        const auto part = currentPart == 0 ? pv::Part::a : pv::Part::b;
        namespace ids = coolsynth::parameters::ids;
        {
            switch (livePage)
            {
                case 0:
                {
                    // An empty layer's macros do nothing: Home says so and the knobs show a dash, no names (D192).
                    if (currentLayerEmpty())
                        return { "Layer " + juce::String(currentPart == 0 ? "A" : "B") + " is empty", {}, {} };
                    // The playing sound's own names where it has them (D177; a saved sound keeps them, D180), else the roles.
                    std::array<juce::String, 4> labels { macroRoles[0], macroRoles[1], macroRoles[2], macroRoles[3] };
                    const auto names = synth.macroNames(currentPart);
                    for (std::size_t m = 0; m < labels.size(); ++m)
                        if (names[m].isNotEmpty())
                            labels[m] = names[m];
                    return { "Macros", { pv::macroParameterId(part, 1), pv::macroParameterId(part, 2),
                                         pv::macroParameterId(part, 3), pv::macroParameterId(part, 4) },
                             labels };
                }
                // the layer's own arp (D194)
                case 1: return { "Arp", { ids::arpOfLayer(currentPart, ids::arpEnabled), ids::arpOfLayer(currentPart, ids::arpRateDivision),
                                          ids::arpOfLayer(currentPart, ids::arpPattern), ids::arpOfLayer(currentPart, ids::arpOctaveRange) },
                                 { "Arp", "Rate", "Pattern", "Octaves" } };
                case 2: return { "Groove", { ids::arpOfLayer(currentPart, ids::arpGate), ids::arpOfLayer(currentPart, ids::arpSwing),
                                             ids::arpOfLayer(currentPart, ids::arpChance), ids::arpOfLayer(currentPart, ids::arpLatch) },
                                 { "Gate", "Swing", "Chance", "Latch" } };
                case 3: return { "Chord", { pv::chordParameterId(part, "type"), pv::chordParameterId(part, "strum"),
                                            pv::chordParameterId(part, "learn"), {} },
                                 { "Chord", "Strum", "Learn", {} } };
                case 4: return { "Voice", { pv::playParameterId(part, "mode"), pv::playParameterId(part, "voices"),
                                            pv::voiceParameterId(part, "glide"), pv::playParameterId(part, "bendRange") },
                                 { "Mode", "Voices", "Glide", "Bend range" } };
                default: // the Layers page (D196): between the layers, and the other layer's transposition
                {
                    const auto other = currentPart == 0 ? pv::Part::b : pv::Part::a;
                    return { "Layers", { pv::layerBalanceParameterId, pv::layerDetuneParameterId, pv::mixParameterId(other, "transpose"),
                                         pv::layerSpreadParameterId },
                             { "Balance", "Detune", currentPart == 0 ? "B Transpose" : "A Transpose", "Spread" } };
                }
            }
        }
    }

    Page InterfaceModel::currentPage() const
    {
        if (currentView == View::home)
            return livePageAt(currentLivePage);
        const auto slot = currentSlot();
        if (slot.pages.empty())
            return { "empty", {}, {} };
        const int page = clampInt(pageIndexOf(currentSection, slotCursor[static_cast<std::size_t>(currentSection)]), 0,
                                  static_cast<int>(slot.pages.size()) - 1);
        return slot.pages[static_cast<std::size_t>(page)];
    }

    Tint InterfaceModel::pageTint() const noexcept
    {
        if (currentView == View::home) // Home belongs to the layer it is on: teal for A, yellow for B (D194)
            return currentLivePage == 0 ? (currentPart == 0 ? Tint::home : Tint::partB) : Tint::perform;
        return sectionTint(currentSection);
    }

    // ---- Knobs ----------------------------------------------------------------------------------

    KnobView InterfaceModel::knob(int knobNumber) const
    {
        KnobView view;
        juce::RangedAudioParameter* p = nullptr;
        juce::String label;
        if (knobNumber >= 1 && knobNumber <= 4)
        {
            const auto page = currentPage();
            const auto k = static_cast<std::size_t>(knobNumber - 1);
            p = parameter(page.ids[k]);
            label = page.labels[k];
            view.role = currentView == View::home && currentLivePage == 0
                            ? (page.ids[k].isNotEmpty() ? juce::String(macroRoles[k]) : juce::String())
                            : "knob " + juce::String(knobNumber);
            if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(p); generic != nullptr && ! generic->isUsedByCurrentType())
                p = nullptr;
        }
        else if (knobNumber == 6)
        {
            p = parameter(pv::voiceParameterId(currentPart == 0 ? pv::Part::a : pv::Part::b, "glide"));
            label = "Glide";
            view.role = "always";
        }
        else if (knobNumber == 7 || knobNumber == 8)
        {
            // Every filter of the sound together; every reverb, delay and room (D176).
            p = parameter(knobNumber == 7 ? coolsynth::parameters::ids::brightness : coolsynth::parameters::ids::space);
            label = knobNumber == 7 ? "Brightness" : "Space";
            view.role = "always";
        }

        if (p == nullptr)
        {
            view.name = juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")); // em dash
            return view;
        }
        view.name = label.isNotEmpty() ? label : withoutPrefix(p->getName(32));
        view.value = p->getCurrentValueAsText();
        view.normalised = p->getValue();
        view.active = true;

        // A setting that does nothing at the moment shows dimmed, still turnable, with the reason (D201).
        auto idle = [&view](const char* why)
        {
            view.idle = true;
            view.why = why;
        };
        if (currentView == View::home && ! typeList.open && knobNumber <= 4)
        {
            const bool arpOn = [this]
            {
                auto* on = parameter(coolsynth::parameters::ids::arpOfLayer(currentPart, coolsynth::parameters::ids::arpEnabled));
                return on != nullptr && on->getValue() >= 0.5f;
            }();
            if (currentLivePage == layersPage)
            {
                const bool bothSound = synth.soloLayer() < 0 && ! layerEmpty(0) && ! layerEmpty(1);
                if (knobNumber == 3 && layerEmpty(1 - currentPart))
                    idle(currentPart == 0 ? "B empty" : "A empty");
                else if (knobNumber != 3 && ! bothSound)
                    idle(synth.soloLayer() >= 0 ? "solo" : "both layers");
            }
            else if ((currentLivePage == 1 && knobNumber >= 2) || currentLivePage == 2)
            {
                if (! arpOn)
                    idle("arp off");
            }
        }
        if (knobNumber == 6 && layerEmpty(currentPart))
            idle("layer empty");

        int part = 0, slot = 0;
        const bool source = sampleSourceOf(p->getParameterID(), part, slot);
        const int instruments = std::max(2, optionCount(*p));
        if (pendingChoice.has_value() && pendingChoice->id == p->getParameterID())
        {
            view.value = source && pendingChoice->index >= instruments ? userEntryText(pendingChoice->index - instruments)
                                                                      : p->getText(pendingChoice->value, 64);
            if (const int entry = pendingChoice->index - instruments;
                source && entry >= 0 && entry < static_cast<int>(userEntries.size()) && userEntries[static_cast<std::size_t>(entry)].name.isNotEmpty())
                describeUserFile(userEntries[static_cast<std::size_t>(entry)].name, userEntries[static_cast<std::size_t>(entry)].kit, view.name, view.value);
            view.normalised = pendingChoice->value;
            view.pending = true;
        }
        else if (source && synth.getUserFile(part, slot).isNotEmpty())
        {
            // The user's own file or kit (D207), by its name.
            const auto file = synth.getUserFile(part, slot);
            const bool kit = coolsynth::synth::v3::UserLibrary::instance().folder().getChildFile(file).isDirectory();
            describeUserFile(file, kit, view.name, view.value);
            view.normalised = 1.0f;
        }
        return view;
    }

    void InterfaceModel::setNormalised(juce::RangedAudioParameter& p, float value)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
        p.endChangeGesture();
    }

    void InterfaceModel::stepParameter(juce::RangedAudioParameter& p, int steps, bool fine)
    {
        if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(&p); generic != nullptr && ! generic->isUsedByCurrentType())
            return;
        const int options = optionCount(p); // lists step one position per detent
        float value = p.getValue();
        if (options >= 2)
            value += static_cast<float>(steps) / static_cast<float>(options - 1);
        else if (const auto* semitone = semitoneDescriptor(p))
        {
            // A shift in semitones steps by whole semitones, a tenth with Shift (D205): +7 and +12 are a turn away.
            const float step = fine ? 0.1f : 1.0f;
            const float units = semitone->min + value * (semitone->max - semitone->min);
            const float moved = (std::round(units / step) + static_cast<float>(steps)) * step;
            value = (std::clamp(moved, semitone->min, semitone->max) - semitone->min) / (semitone->max - semitone->min);
        }
        else
            value += static_cast<float>(steps) * (fine ? 0.001f : 0.01f);
        setNormalised(p, value);
    }

    void InterfaceModel::turnKnob(int knobNumber, int steps, bool fine)
    {
        if (steps == 0)
            return;
        juce::RangedAudioParameter* p = nullptr;
        if (knobNumber >= 1 && knobNumber <= 4)
        {
            if (typeList.open)
                return;
            const auto page = currentPage();
            p = parameter(page.ids[static_cast<std::size_t>(knobNumber - 1)]);
        }
        else if (knobNumber == 6)
            p = parameter(pv::voiceParameterId(currentPart == 0 ? pv::Part::a : pv::Part::b, "glide"));
        else if (knobNumber == 7 || knobNumber == 8)
            p = parameter(knobNumber == 7 ? coolsynth::parameters::ids::brightness : coolsynth::parameters::ids::space);
        if (p == nullptr)
            return;
        if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(p); generic != nullptr && ! generic->isUsedByCurrentType())
            return;
        if (knobNumber <= 4 && loadsInstrument(p->getParameterID()))
        {
            // Browsing instruments reads nothing in: the knob shows the choice, a press loads it (D156). A Sample slot's
            // source goes on past the instruments into the user's own files and kits (D207).
            const int instruments = std::max(2, optionCount(*p));
            int part = 0, slot = 0;
            const bool source = sampleSourceOf(p->getParameterID(), part, slot);
            const bool browsing = pendingChoice.has_value() && pendingChoice->id == p->getParameterID();
            if (source && ! browsing)
                refreshUserEntries();
            const int total = instruments + (source ? static_cast<int>(userEntries.size()) : 0);
            const int loaded = source ? loadedSourceIndex(*p, part, slot) : juce::roundToInt(p->getValue() * static_cast<float>(instruments - 1));
            const int index = clampInt((browsing ? pendingChoice->index : loaded) + steps, 0, total - 1);
            const float value = index < instruments ? static_cast<float>(index) / static_cast<float>(instruments - 1) : p->getValue();
            if (index == loaded)
                pendingChoice.reset(); // back on what is loaded
            else
                pendingChoice = PendingChoice { p->getParameterID(), knobNumber, value, index };
        }
        else
            stepParameter(*p, steps, fine);
        const auto view = knob(knobNumber);
        raisePopup(view.name, view.value, view.normalised, false, knobNumber);
        touched();
    }

    void InterfaceModel::turnKnobFromDevice(int knobNumber, int counts, bool fine)
    {
        if (counts == 0 || knobNumber < 1 || knobNumber > 8)
            return;
        // What the knob moves, and how many counts make a step of it.
        juce::String target;
        int perStep = 1;
        if (knobNumber == 5)
        {
            target = "pages";
            perStep = countsPerListStep;
        }
        else
        {
            juce::RangedAudioParameter* p = nullptr;
            if (knobNumber <= 4 && ! typeList.open)
                p = parameter(currentPage().ids[static_cast<std::size_t>(knobNumber - 1)]);
            else if (knobNumber == 6)
                p = parameter(pv::voiceParameterId(currentPart == 0 ? pv::Part::a : pv::Part::b, "glide"));
            else if (knobNumber == 7 || knobNumber == 8)
                p = parameter(knobNumber == 7 ? coolsynth::parameters::ids::brightness : coolsynth::parameters::ids::space);
            if (p != nullptr)
            {
                target = p->getParameterID();
                const int options = optionCount(*p);
                perStep = options == 2 ? countsPerSwitchStep : (options > 2 ? countsPerListStep : 1);
                if (options == 0 && ! fine && semitoneDescriptor(*p) != nullptr)
                    perStep = countsPerListStep; // whole semitones step as a list does (D205)
            }
        }
        if (perStep == 1)
        {
            turnKnob(knobNumber, counts, fine);
            return;
        }
        auto& held = deviceCounts[static_cast<std::size_t>(knobNumber)];
        auto& last = deviceTargets[static_cast<std::size_t>(knobNumber)];
        if (target != last || (held != 0 && (held > 0) != (counts > 0)))
            held = 0; // another setting, or the other way: start afresh
        last = target;
        held += counts;
        const int steps = held / perStep;
        held -= steps * perStep;
        if (steps == 0)
            return;
        if (knobNumber == 5)
            turnPageKnob(steps);
        else
            turnKnob(knobNumber, steps, fine);
    }

    void InterfaceModel::pressKnob(int knobNumber)
    {
        if (pendingChoice.has_value() && pendingChoice->knob == knobNumber)
        {
            commitPendingChoice();
            touched();
        }
    }

    bool InterfaceModel::loadsInstrument(const juce::String& parameterId) const
    {
        // a Sample slot's instrument (p1) and style (p8)
        if (! (parameterId.endsWith(".p1") || parameterId.endsWith(".p8")))
            return false;
        auto* type = parameter(parameterId.upToLastOccurrenceOf(".p", false, false) + ".type");
        return type != nullptr && type->getCurrentValueAsText().equalsIgnoreCase("Sample");
    }

    void InterfaceModel::commitPendingChoice()
    {
        if (! pendingChoice.has_value())
            return;
        if (auto* p = parameter(pendingChoice->id))
        {
            int part = 0, slot = 0;
            const bool source = sampleSourceOf(pendingChoice->id, part, slot);
            const int instruments = std::max(2, optionCount(*p));
            if (source && pendingChoice->index >= instruments)
            {
                // One of the user's files or kits (D207); the "none yet" line chooses nothing.
                const int entry = pendingChoice->index - instruments;
                if (entry < static_cast<int>(userEntries.size()) && userEntries[static_cast<std::size_t>(entry)].name.isNotEmpty())
                    synth.setUserFile(part, slot, userEntries[static_cast<std::size_t>(entry)].name);
            }
            else
            {
                setNormalised(*p, pendingChoice->value);
                if (source && synth.getUserFile(part, slot).isNotEmpty())
                    synth.setUserFile(part, slot, {}); // an instrument again: the file is let go
            }
        }
        pendingChoice.reset();
    }

    bool InterfaceModel::sampleSourceOf(const juce::String& parameterId, int& part, int& slot) const
    {
        // "pA.g1.p1": layer A's first generator, its source
        if (! parameterId.endsWith(".p1") || ! loadsInstrument(parameterId))
            return false;
        part = parameterId.startsWith("pB.") ? 1 : 0;
        slot = parameterId.fromFirstOccurrenceOf(".g", false, false).getIntValue();
        return slot >= 1 && slot <= pv::partGeneratorSlots;
    }

    void InterfaceModel::refreshUserEntries(bool fromController)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (fromController && userEntriesReadAt != 0 && now - userEntriesReadAt < 1000)
            return;
        userEntriesReadAt = now == 0 ? 1 : now;
        userEntries.clear();
        auto folder = coolsynth::synth::v3::UserLibrary::instance().folder();
        folder.createDirectory(); // there is always a place to put one's files (D207)
        userFolderPath = folder.getFullPathName();
        const auto files = folder.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
        juce::StringArray kits;
        for (const auto& file : files)
        {
            userEntries.push_back({ file.getRelativePathFrom(folder).replaceCharacter('\\', '/'), false });
            const auto parent = file.getParentDirectory();
            if (parent != folder)
                kits.addIfNotAlreadyThere(parent.getRelativePathFrom(folder).replaceCharacter('\\', '/'));
        }
        for (const auto& kit : kits)
            userEntries.push_back({ kit, true });
        std::sort(userEntries.begin(), userEntries.end(), [](const UserEntry& a, const UserEntry& b)
        {
            const int order = a.name.compareNatural(b.name);
            return order != 0 ? order < 0 : a.kit && ! b.kit; // a folder before the files in it
        });
        constexpr std::size_t most = 2000;
        if (userEntries.size() > most)
            userEntries.resize(most);
        if (userEntries.empty())
            userEntries.push_back({ {}, false });
    }

    int InterfaceModel::loadedSourceIndex(const juce::RangedAudioParameter& source, int part, int slot) const
    {
        const int instruments = std::max(2, optionCount(source));
        const auto file = synth.getUserFile(part, slot);
        if (file.isNotEmpty())
            for (std::size_t i = 0; i < userEntries.size(); ++i)
                if (userEntries[i].name == file)
                    return instruments + static_cast<int>(i);
        return juce::roundToInt(source.getValue() * static_cast<float>(instruments - 1));
    }

    void InterfaceModel::describeUserFile(const juce::String& file, bool kit, juce::String& where, juce::String& name) const
    {
        if (kit)
        {
            int files = 0;
            for (const auto& e : userEntries)
                if (! e.kit && e.name.startsWith(file + "/") && ! e.name.fromFirstOccurrenceOf(file + "/", false, false).containsChar('/'))
                    ++files;
            where = "Kit: " + file;
            name = files == 1 ? juce::String("1 file") : juce::String(files) + " files";
            return;
        }
        const auto folder = file.containsChar('/') ? file.upToLastOccurrenceOf("/", false, false) : juce::String();
        where = folder.isEmpty() ? juce::String("Your files") : folder + "/";
        const auto own = file.fromLastOccurrenceOf("/", false, false);
        name = own.containsChar('.') ? own.upToLastOccurrenceOf(".", false, false) : own;
    }

    juce::String InterfaceModel::userEntryText(int entry) const
    {
        if (entry < 0 || entry >= static_cast<int>(userEntries.size()))
            return {};
        const auto& e = userEntries[static_cast<std::size_t>(entry)];
        if (e.name.isEmpty())
            return "No files yet: put WAV files in " + userFolderPath;
        return e.kit ? e.name + "/ (kit)" : e.name;
    }

    void InterfaceModel::raisePopup(const juce::String& name, const juce::String& value, float normalised, bool fader, int control)
    {
        lastPopup.name = name;
        lastPopup.value = value;
        lastPopup.normalised = normalised;
        lastPopup.fader = fader;
        lastPopup.pad = false;
        lastPopup.control = control;
        ++lastPopup.serial;
    }

    // ---- Faders ---------------------------------------------------------------------------------

    std::vector<juce::String> InterfaceModel::faderParameterIds(int fader) const
    {
        switch (fader)
        {
            // Volumes over the sound as designed (D184), not the sound's own levels.
            case 0: return { coolsynth::parameters::ids::volumeA };
            case 1: return { coolsynth::parameters::ids::volumeB };
            case 2: return { coolsynth::parameters::ids::volumeFx };
            default: return { coolsynth::parameters::ids::volumeMaster };
        }
    }

    juce::String InterfaceModel::faderName(int fader) const
    {
        static const juce::StringArray names { "Layer A", "Layer B", "Effects", "Master" };
        return names[clampInt(fader, 0, 3)];
    }

    float InterfaceModel::faderValue(int fader) const
    {
        if (auto* p = parameter(faderParameterIds(fader).front()))
            return p->getValue();
        return 0.0f;
    }

    float InterfaceModel::masterBoostDb() const
    {
        if (auto* p = parameter(coolsynth::parameters::ids::masterBoost))
            return p->convertFrom0to1(p->getValue());
        return 0.0f;
    }

    void InterfaceModel::setMasterBoost(float db)
    {
        if (auto* p = parameter(coolsynth::parameters::ids::masterBoost))
            setNormalised(*p, p->convertTo0to1(juce::jlimit(0.0f, 12.0f, db)));
        raisePopup("Master boost", "+" + juce::String(masterBoostDb(), 1) + " dB", masterBoostDb() / 12.0f, true, 3);
        touched();
    }

    void InterfaceModel::moveFader(int fader, float value01, bool pickup)
    {
        fader = clampInt(fader, 0, 3);
        // The Master below its top takes the boost away (D225): a boost lives only above the top, so what the faders
        // show is what plays, the MiniLab's fader too.
        if (fader == 3 && value01 < 0.999f && masterBoostDb() > 0.0f)
            if (auto* p = parameter(coolsynth::parameters::ids::masterBoost))
                setNormalised(*p, 0.0f);
        const auto f = static_cast<std::size_t>(fader);
        // The faders are volumes the player keeps across sounds (D184): a position means the same whatever plays,
        // so the MiniLab's faders are taken as they are, with no catching up.
        juce::ignoreUnused(pickup, f);
        for (const auto& id : faderParameterIds(fader))
            if (auto* p = parameter(id))
                setNormalised(*p, value01);
        juce::String text;
        if (auto* p = parameter(faderParameterIds(fader).front()))
            text = p->getCurrentValueAsText();
        raisePopup(faderName(fader), text, value01, true, fader);
        touched();
    }

    // ---- Any controller (D211) ----------------------------------------------------------------------

    float InterfaceModel::valueScaled(float position, float h0, float h1) noexcept
    {
        // Ableton's "Value Scaling": the knob's move, scaled by the room the value has left that way to the room the
        // knob has. Where they agree it is the knob's move itself; at an end they agree from then on.
        if (h1 > h0 && h0 < 1.0f)
            return juce::jlimit(0.0f, 1.0f, position + (h1 - h0) * (1.0f - position) / (1.0f - h0));
        if (h1 < h0 && h0 > 0.0f)
            return juce::jlimit(0.0f, 1.0f, position - (h0 - h1) * position / h0);
        return position;
    }

    void InterfaceModel::scaleList(ScaledPlace& place, const juce::String& target, int index, int count, float h0, float h1,
                                   const std::function<void(int)>& turn)
    {
        if (count < 2)
            return;
        float position = static_cast<float>(index) / static_cast<float>(count - 1);
        if (place.target == target && place.seenIndex == index)
            position = place.position; // still where this knob left it: go on from its finer place
        position = valueScaled(position, h0, h1);
        const int wanted = clampInt(juce::roundToInt(position * static_cast<float>(count - 1)), 0, count - 1);
        place.target = target;
        place.position = position;
        place.seenIndex = wanted;
        place.seenValue = -1.0f;
        if (wanted != index)
            turn(wanted - index);
    }

    bool InterfaceModel::scaleNumber(ScaledPlace& place, juce::RangedAudioParameter& p, float h0, float h1)
    {
        const auto id = p.getParameterID();
        float position = p.getValue();
        if (place.target == id && std::abs(position - place.seenValue) < 1.0e-5f)
            position = place.position;
        position = valueScaled(position, h0, h1);
        setNormalised(p, position);
        place.target = id;
        place.position = position;
        place.seenValue = p.getValue();
        place.seenIndex = -1;
        return true;
    }

    juce::RangedAudioParameter* InterfaceModel::knobParameter(int knobNumber) const
    {
        juce::RangedAudioParameter* p = nullptr;
        if (knobNumber >= 1 && knobNumber <= 4)
        {
            if (! typeList.open)
                p = parameter(currentPage().ids[static_cast<std::size_t>(knobNumber - 1)]);
        }
        else if (knobNumber == 6)
            p = parameter(pv::voiceParameterId(currentPart == 0 ? pv::Part::a : pv::Part::b, "glide"));
        else if (knobNumber == 7 || knobNumber == 8)
            p = parameter(knobNumber == 7 ? coolsynth::parameters::ids::brightness : coolsynth::parameters::ids::space);
        if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(p); generic != nullptr && ! generic->isUsedByCurrentType())
            return nullptr;
        return p;
    }

    juce::String InterfaceModel::settingName(const juce::String& parameterId) const
    {
        for (int k : { 1, 2, 3, 4 })
            if (auto* p = knobParameter(k); p != nullptr && p->getParameterID() == parameterId)
                return knob(k).name; // the page's own label when it has one
        if (auto* p = parameter(parameterId))
            return withoutPrefix(p->getName(32));
        return parameterId;
    }

    void InterfaceModel::moveKnobFromController(int knobNumber, int from, int to)
    {
        if (knobNumber < 1 || knobNumber > 8 || from == to || from < 0)
            return;
        const float h0 = static_cast<float>(from) / 127.0f, h1 = static_cast<float>(to) / 127.0f;
        auto& place = scaledKnobs[static_cast<std::size_t>(knobNumber)];
        if (knobNumber == 5)
        {
            if (typeList.open)
                return;
            const bool home = currentView == View::home;
            const int count = home ? livePageCount : static_cast<int>(currentSlot().pages.size());
            const int page = home ? currentLivePage : pageIndexOf(currentSection, slotCursor[static_cast<std::size_t>(currentSection)]);
            scaleList(place, "pages", page, count, h0, h1, [this](int steps) { turnPageKnob(steps); });
            return;
        }
        auto* p = knobParameter(knobNumber);
        if (p == nullptr)
            return;
        const auto id = p->getParameterID();
        auto turn = [this, knobNumber](int steps) { turnKnob(knobNumber, steps, false); };
        if (knobNumber <= 4 && loadsInstrument(id))
        {
            // The instruments and the user's files, as the knob browses them (D156, D207): a press loads the choice.
            const int instruments = std::max(2, optionCount(*p));
            int part = 0, slot = 0;
            const bool source = sampleSourceOf(id, part, slot);
            const bool browsing = pendingChoice.has_value() && pendingChoice->id == id;
            if (source && ! browsing)
                refreshUserEntries(true);
            const int total = instruments + (source ? static_cast<int>(userEntries.size()) : 0);
            const int loaded = source ? loadedSourceIndex(*p, part, slot) : juce::roundToInt(p->getValue() * static_cast<float>(instruments - 1));
            scaleList(place, id, browsing ? pendingChoice->index : loaded, total, h0, h1, turn);
            return;
        }
        if (const int options = optionCount(*p); options >= 2)
        {
            scaleList(place, id, juce::roundToInt(p->getValue() * static_cast<float>(options - 1)), options, h0, h1, turn);
            return;
        }
        if (const auto* semitone = semitoneDescriptor(*p))
        {
            // whole semitones, as the MiniLab's knobs step them (D205)
            const float units = semitone->min + p->getValue() * (semitone->max - semitone->min);
            const int count = juce::roundToInt(semitone->max - semitone->min) + 1;
            scaleList(place, id, juce::roundToInt(units - semitone->min), count, h0, h1, turn);
            return;
        }
        scaleNumber(place, *p, h0, h1);
        const auto view = knob(knobNumber);
        raisePopup(view.name, view.value, view.normalised, false, knobNumber);
        touched();
    }

    void InterfaceModel::moveSettingFromController(const juce::String& parameterId, int from, int to)
    {
        auto* p = parameter(parameterId);
        if (p == nullptr || from == to || from < 0)
            return;
        if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(p); generic != nullptr && ! generic->isUsedByCurrentType())
            return;
        const float h0 = static_cast<float>(from) / 127.0f, h1 = static_cast<float>(to) / 127.0f;
        auto& place = scaledSettings[parameterId];
        auto turn = [this, p](int steps) { stepParameter(*p, steps, false); };
        if (const int options = optionCount(*p); options >= 2)
            scaleList(place, parameterId, juce::roundToInt(p->getValue() * static_cast<float>(options - 1)), options, h0, h1, turn);
        else if (const auto* semitone = semitoneDescriptor(*p))
        {
            const float units = semitone->min + p->getValue() * (semitone->max - semitone->min);
            scaleList(place, parameterId, juce::roundToInt(units - semitone->min),
                      juce::roundToInt(semitone->max - semitone->min) + 1, h0, h1, turn);
        }
        else
            scaleNumber(place, *p, h0, h1);
        raisePopup(settingName(parameterId), p->getCurrentValueAsText(), p->getValue(), false, 0);
        touched();
    }

    void InterfaceModel::turnSettingFromController(const juce::String& parameterId, int steps, bool fine)
    {
        auto* p = parameter(parameterId);
        if (p == nullptr || steps == 0)
            return;
        stepParameter(*p, steps, fine);
        raisePopup(settingName(parameterId), p->getCurrentValueAsText(), p->getValue(), false, 0);
        touched();
    }

    // ---- Navigation -------------------------------------------------------------------------------

    void InterfaceModel::openHome()
    {
        cancelPendingChoice();
        refreshMySounds(); // sounds saved, or copied into the folder, since
        currentView = View::home;
        typeList.open = false;
        browser.inCategory = true;
        browser.category = categoryOfPreset(playingPreset());
        const auto& presets = bank[static_cast<std::size_t>(browser.category)].presets;
        const auto at = std::find(presets.begin(), presets.end(), playingPreset());
        browser.row = at == presets.end() ? 1 : static_cast<int>(at - presets.begin()) + 1;
    }

    void InterfaceModel::openSection(int section)
    {
        cancelPendingChoice();
        typeList.open = false;
        if (currentView == View::section && currentSection == section)
        {
            // the lit pad pressed again: the next slot
            const int count = static_cast<int>(slotsOf(section).size());
            auto& cursor = slotCursor[static_cast<std::size_t>(section)];
            cursor = (cursor + 1) % count;
            return;
        }
        currentView = View::section;
        currentSection = section;
    }

    juce::StringArray InterfaceModel::typeNames() const
    {
        juce::StringArray names;
        if (auto* type = dynamic_cast<juce::AudioParameterChoice*>(parameter(currentSlot().typeId)))
            names = type->choices;
        return names;
    }

    void InterfaceModel::openTypeList()
    {
        const auto slot = currentSlot();
        auto* type = dynamic_cast<juce::AudioParameterChoice*>(parameter(slot.typeId));
        if (type == nullptr)
            return; // Motion's slots have one type
        cancelPendingChoice();
        typeList.open = true;
        typeList.row = type->getIndex() + 1;
    }

    void InterfaceModel::chooseType(int typeIndex)
    {
        const auto slot = currentSlot();
        auto* type = dynamic_cast<juce::AudioParameterChoice*>(parameter(slot.typeId));
        if (type == nullptr)
            return;
        if (typeIndex == type->getIndex())
            return;
        const auto prefix = slot.typeId.upToLastOccurrenceOf(".", false, false);
        synth.performUndoableEdit("Type " + slot.code, [&]
        {
            setNormalised(*type, type->convertTo0to1(static_cast<float>(typeIndex)));
            // the new type starts from its own defaults, as a preset would set it
            for (int i = 1; i <= pv::genericParameterCount; ++i)
                if (auto* generic = dynamic_cast<pv::GenericSlotParameter*>(parameter(prefix + ".p" + juce::String(i))))
                    setNormalised(*generic, generic->currentTypeDefault());
        });
        pageOfSlot[slotKey(currentSection, slotCursor[static_cast<std::size_t>(currentSection)])] = 0;
    }

    void InterfaceModel::turnMain(int steps)
    {
        if (steps == 0)
            return;
        mainTurns += steps;
        cancelPendingChoice();
        if (typeList.open)
            typeList.row = clampInt(typeList.row + steps, 0, typeNames().size());
        else if (currentView == View::home)
        {
            if (currentLivePage == layersPage)
                layerAction = clampInt(layerAction + steps, 0, layerActionNames().size() - 1);
            else if (! browser.inCategory) // -1: the "Empty layer" row (D194); -2, above it, an edit to go back to (D225)
                browser.category = clampInt(browser.category + steps, synth.hasEditRecall() ? recallRow : -1, static_cast<int>(bank.size()) - 1);
            else
                browser.row = clampInt(browser.row + steps, 0,
                                       static_cast<int>(bank[static_cast<std::size_t>(browser.category)].presets.size()));
        }
        else
        {
            const int count = static_cast<int>(slotsOf(currentSection).size());
            auto& cursor = slotCursor[static_cast<std::size_t>(currentSection)];
            cursor = clampInt(cursor + steps, 0, count - 1);
        }
        touched();
    }

    void InterfaceModel::pressMain()
    {
        if (typeList.open)
        {
            if (typeList.row > 0)
                chooseType(typeList.row - 1);
            typeList.open = false; // ‹ Back keeps the type
        }
        else if (pendingChoice.has_value())
            commitPendingChoice(); // the instrument a knob shows is read in now (D156)
        else if (currentView == View::home)
        {
            if (currentLivePage == layersPage)
                doLayerAction(layerAction); // (D196)
            else if (! browser.inCategory && browser.category == recallRow)
            {
                synth.restoreEditRecall(); // the edit back, as it was (D225)
                browser.category = 0;
            }
            else if (! browser.inCategory && browser.category < 0)
            {
                synth.keepEditForRecall();
                synth.emptyLayer(currentPart); // "Empty layer A/B" (D194)
            }
            else if (! browser.inCategory)
            {
                browser.inCategory = true;
                const auto& presets = bank[static_cast<std::size_t>(browser.category)].presets;
                const auto at = std::find(presets.begin(), presets.end(), playingPreset());
                browser.row = at == presets.end() ? 1 : static_cast<int>(at - presets.begin()) + 1;
            }
            else if (browser.row == 0)
                browser.inCategory = false; // ‹ Back to categories
            else
            {
                const int preset = bank[static_cast<std::size_t>(browser.category)].presets[static_cast<std::size_t>(browser.row - 1)];
                if (preset != playingPreset())
                    loadPreset(preset);
            }
        }
        else
            openTypeList();
        touched();
    }

    void InterfaceModel::turnPageKnob(int steps)
    {
        if (steps == 0 || typeList.open)
            return;
        pageTurns += steps;
        cancelPendingChoice();
        if (currentView == View::home)
            currentLivePage = clampInt(currentLivePage + steps, 0, livePageCount - 1);
        else
        {
            const auto slot = currentSlot();
            if (slot.pages.empty())
                return;
            const int cursor = slotCursor[static_cast<std::size_t>(currentSection)];
            auto& page = pageOfSlot[slotKey(currentSection, cursor)];
            page = clampInt(page + steps, 0, static_cast<int>(slot.pages.size()) - 1);
        }
        raisePopup("Page", currentPage().name, 0.0f, false, 5);
        touched();
    }

    int InterfaceModel::sectionOfPad(int pad) noexcept
    {
        // The pads, left to right (D184): Home, Sound, Shape, Motion, the recorder's three, Effects.
        if (pad >= 1 && pad <= 3)
            return pad - 1;
        return pad == 7 ? 3 : -1;
    }

    int InterfaceModel::padOfSection(int section) noexcept
    {
        return section >= 0 && section < 3 ? section + 1 : 7;
    }

    void InterfaceModel::pressPad(int pad, bool shift)
    {
        // No Shift on the pads (D185): Shift + pads 1-3 are the MiniLab's own (program, pad bank). Home pressed again
        // while Home is open switches the part, as a lit section pad pressed again moves to the next slot.
        juce::ignoreUnused(shift);
        if (const int section = sectionOfPad(pad); section >= 0)
            openSection(section);
        else if (pad == homePad)
        {
            if (currentView == View::home && ! typeList.open)
                togglePart();
            else
                openHome();
        }
        else if (pad == stopPad)
            synth.requestPanic(); // every sound ends at once, its tails too
        // What the pad put you in, briefly on the MiniLab's screen (D186): the pad's name and the part you edit; the
        // recorder's pads, that they come later.
        if (pad >= 0 && pad < 8)
        {
            lastPopup.name = pad == homePad ? juce::String("Home") : padLabel(pad, false); // the part goes on line 2
            lastPopup.value = pad == stopPad ? juce::String("All sound off")
                              : (pad == 5 || pad == 6 ? juce::String("comes later") : "Layer " + juce::String(currentPart == 0 ? "A" : "B"));
            lastPopup.normalised = 1.0f;
            lastPopup.fader = false;
            lastPopup.pad = true;
            lastPopup.control = 100 + pad; // not a knob: the screen's knobs leave it alone
            ++lastPopup.serial;
        }
        // pads 5-7 (4-6 from 0) are kept for the recorder
        touched();
    }

    // ---- The mouse on the screen ---------------------------------------------------------------------

    void InterfaceModel::clickListRow(int row)
    {
        const auto view = list();
        if (row < 0 || row >= view.rows.size())
            return;
        if (typeList.open)
            typeList.row = row;
        else if (currentView == View::home)
        {
            if (currentLivePage == layersPage)
                layerAction = row;
            else if (browser.inCategory)
                browser.row = row;
            else
                browser.category = row - 1 - (synth.hasEditRecall() ? 1 : 0); // "Empty layer" first (D194), an edit before it (D225)
        }
        else
            return;
        pressMain();
    }

    void InterfaceModel::clickSlot(int slot)
    {
        if (currentView != View::section)
            return;
        cancelPendingChoice();
        const int target = clampInt(slot, 0, static_cast<int>(slotsOf(currentSection).size()) - 1);
        auto& cursor = slotCursor[static_cast<std::size_t>(currentSection)];
        // The slot already chosen, clicked again: its type list, as a press of the main knob (D181).
        if (target == cursor && ! typeList.open)
        {
            openTypeList();
            touched();
            return;
        }
        typeList.open = false;
        cursor = target;
        touched();
    }

    void InterfaceModel::openSlot(int layer, int section, int slot)
    {
        layer = clampInt(layer, 0, 1);
        section = clampInt(section, 0, sectionCount - 1);
        if (currentView == View::section && currentSection == section && currentPart == layer)
        {
            clickSlot(slot); // already there: a slot clicked again opens its type list
            return;
        }
        cancelPendingChoice();
        typeList.open = false;
        currentPart = layer;
        currentView = View::section;
        currentSection = section;
        slotCursor[static_cast<std::size_t>(section)] = clampInt(slot, 0, static_cast<int>(slotsOf(section).size()) - 1);
        touched();
    }

    void InterfaceModel::back()
    {
        // One level up, for a mouse or a computer keyboard (D181): the type list closes, a sound list goes back to
        // the rooms, a section goes Home.
        if (pendingChoice.has_value())
            cancelPendingChoice();
        else if (typeList.open)
            typeList.open = false;
        else if (currentView == View::home)
        {
            if (browser.inCategory)
                browser.inCategory = false;
        }
        else
            openHome();
        touched();
    }

    void InterfaceModel::clickTrail(int item)
    {
        // Each part of the trail does what it names (D181).
        const auto items = trail();
        if (item < 0 || item >= static_cast<int>(items.size()))
            return;
        const auto& clicked = items[static_cast<std::size_t>(item)];
        const bool cursor = clicked.mark == juce::String(juce::CharPointer_UTF8("\xe2\x97\x8e"));
        const bool page = clicked.mark == "5";
        if (cursor)
        {
            pressMain(); // the row or slot the main knob is on: open, load, choose
            return;
        }
        if (page)
        {
            // the next page, round again after the last
            const int count = currentView == View::home && ! typeList.open ? livePageCount : static_cast<int>(currentSlot().pages.size());
            const int now = currentView == View::home && ! typeList.open ? currentLivePage
                                                                          : pageIndexOf(currentSection, slotIndex(currentSection));
            if (count > 1)
                clickPage((now + 1) % count);
            return;
        }
        if (currentView == View::home && ! typeList.open)
        {
            cancelPendingChoice();
            if (item == 0)
                browser.inCategory = false; // Categories
            else
                browser.inCategory = true;  // the room the list is in
            touched();
            return;
        }
        // In a section: the sound's name goes Home, the section's name closes the type list.
        if (item == 0)
            openHome();
        else
        {
            cancelPendingChoice();
            typeList.open = false;
        }
        touched();
    }

    void InterfaceModel::clickPage(int page)
    {
        if (typeList.open)
            return;
        cancelPendingChoice();
        if (currentView == View::home)
            currentLivePage = clampInt(page, 0, livePageCount - 1);
        else
        {
            const auto slot = currentSlot();
            if (slot.pages.empty())
                return;
            pageOfSlot[slotKey(currentSection, slotCursor[static_cast<std::size_t>(currentSection)])]
                = clampInt(page, 0, static_cast<int>(slot.pages.size()) - 1);
        }
        touched();
    }

    // ---- What to show -----------------------------------------------------------------------------

    ListView InterfaceModel::list() const
    {
        ListView view;
        if (typeList.open)
        {
            const auto slot = currentSlot();
            view.title = "TYPE " + slot.code;
            view.rows.add(juce::String(juce::CharPointer_UTF8("\xe2\x80\xb9 Back")));
            for (const auto& name : typeNames())
                view.rows.add(name == "Off" ? juce::String("Empty") : name);
            view.cursor = typeList.row;
            if (auto* type = dynamic_cast<juce::AudioParameterChoice*>(parameter(slot.typeId)))
                view.used = type->getIndex() + 1;
            view.firstRowIsBack = true;
            return view;
        }
        if (currentView != View::home)
            return view;
        if (currentLivePage == layersPage) // the main knob picks a layer action (D196)
        {
            view.title = "LAYERS";
            view.rows = layerActionNames();
            view.cursor = clampInt(layerAction, 0, view.rows.size() - 1);
            return view;
        }
        if (! browser.inCategory)
        {
            view.title = "CATEGORIES";
            // The edited sound another replaced, to go back to (D225), then a way to clear the layer (D194).
            const int extra = synth.hasEditRecall() ? 1 : 0;
            if (extra != 0)
                view.rows.add(juce::String(juce::CharPointer_UTF8("\xe2\x86\xba Back to your edit: ")) + synth.editRecallName());
            view.rows.add("Empty layer " + juce::String(currentPart == 0 ? "A" : "B")); // first, to clear the layer (D194)
            for (const auto& category : bank)
                view.rows.add(category.name);
            view.cursor = juce::jlimit(0, view.rows.size() - 1, browser.category + 1 + extra);
            view.used = layerEmpty(currentPart) ? extra : categoryOfPreset(playingPreset()) + 1 + extra;
            return view;
        }
        const auto& category = bank[static_cast<std::size_t>(browser.category)];
        view.title = category.name.toUpperCase();
        view.rows.add(juce::String(juce::CharPointer_UTF8("\xe2\x80\xb9 Back to categories")));
        view.pills.emplace_back();
        for (int preset : category.presets)
        {
            view.rows.add(soundName(preset));
            view.pills.push_back(soundPills(preset));
            if (preset == playingPreset())
                view.used = view.rows.size() - 1;
        }
        view.cursor = browser.row;
        view.firstRowIsBack = true;
        return view;
    }

    std::vector<TrailItem> InterfaceModel::trail() const
    {
        std::vector<TrailItem> items;
        if (currentView == View::home && ! typeList.open)
        {
            const auto l = list();
            if (currentLivePage == layersPage)
            {
                items.push_back({ "Layers", {}, false });
                items.push_back({ l.rows[l.cursor], juce::String(juce::CharPointer_UTF8("\xe2\x97\x8e")), true });
                return items;
            }
            items.push_back({ "Categories", {}, false });
            if (browser.inCategory)
                items.push_back({ bank[static_cast<std::size_t>(browser.category)].name, {}, false });
            items.push_back({ l.rows[l.cursor], juce::String(juce::CharPointer_UTF8("\xe2\x97\x8e")), true });
            items.push_back({ livePageNames()[currentLivePage], "5", false });
            return items;
        }
        const auto slot = currentSlot();
        items.push_back({ playingPresetName(), {}, false });
        items.push_back({ sectionName(currentSection), {}, false });
        items.push_back({ slot.code + " " + (slot.empty ? juce::String("empty") : slot.typeName),
                          juce::String(juce::CharPointer_UTF8("\xe2\x97\x8e")), true });
        items.push_back({ slot.empty ? juce::String("no pages") : currentPage().name, "5", false });
        return items;
    }

    juce::String InterfaceModel::mainTurnRole() const
    {
        if (typeList.open)
            return "Type";
        if (currentView == View::home)
            return currentLivePage == layersPage ? "Action" : (browser.inCategory ? "Sound" : "Category");
        return "Slot";
    }

    juce::String InterfaceModel::mainPressRole() const
    {
        const juce::String none = juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));
        if (typeList.open)
            return typeList.row == 0 ? "Back" : "Set";
        if (currentView == View::home)
        {
            if (currentLivePage == layersPage)
                return "Do";
            if (! browser.inCategory)
                return browser.category == recallRow ? juce::String("Restore")
                                                     : (browser.category < 0 ? (layerEmpty(currentPart) ? none : juce::String("Empty")) : juce::String("Open"));
            if (browser.row == 0)
                return "Back";
            const int preset = bank[static_cast<std::size_t>(browser.category)].presets[static_cast<std::size_t>(browser.row - 1)];
            return preset == playingPreset() ? none : juce::String("Load");
        }
        if (pendingChoice.has_value())
            return "Load";
        return currentSlot().typeId.isEmpty() ? none : juce::String("Type");
    }

    juce::String InterfaceModel::pageKnobRole() const
    {
        if (typeList.open)
            return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));
        return currentView == View::home ? "Live page" : "Page";
    }

    bool InterfaceModel::loadingInstrument() const { return synth.isLoadingInstrument(); }

    float InterfaceModel::loadingGlow()
    {
        constexpr double periodSeconds = 1.2;
        const double t = static_cast<double>(juce::Time::getMillisecondCounter() % 1200000u) / 1000.0;
        return static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * t / periodSeconds));
    }

    juce::String InterfaceModel::hint() const
    {
        // The computer too small for the instruments, or one that does not fit: said, never a silent sound (D162).
        const auto warning = coolsynth::synth::v3::SampleLibrary::instance().memoryWarning();
        if (loadingInstrument())
            return warning.isNotEmpty() ? warning
                                        : juce::String(juce::CharPointer_UTF8("Loading the instrument \xc2\xb7 the keys play once Home stops glowing"));
        if (pendingChoice.has_value())
            return juce::String(juce::CharPointer_UTF8("Press \xe2\x97\x8e or click the knob to load it \xc2\xb7 turn on to keep browsing"));
        if (warning.isNotEmpty())
            return warning;
        if (typeList.open)
            return juce::String(juce::CharPointer_UTF8("Turn \xe2\x97\x8e to pick \xc2\xb7 press to set \xc2\xb7 \xe2\x80\xb9 Back on top keeps the type"));
        if (currentView == View::home)
        {
            if (currentLivePage == layersPage)
                return juce::String(juce::CharPointer_UTF8("Turn \xe2\x97\x8e to pick \xc2\xb7 press to do it"));
            if (! browser.inCategory)
                return juce::String(juce::CharPointer_UTF8("Turn \xe2\x97\x8e to pick a category \xc2\xb7 press to open it"));
            if (browser.row == 0)
                return "Press to go back to the categories";
            return mainPressRole() == "Load" ? juce::String("Press to load")
                                             : juce::String(juce::CharPointer_UTF8("Playing now \xc2\xb7 pads 2-4 and 8 open its sections"));
        }
        return currentSlot().empty ? "Empty slot: press for a type" : juce::String();
    }

    DeviceScreen InterfaceModel::deviceScreen() const
    {
        DeviceScreen screen;
        auto position = [](int at, int count) { return count > 1 ? juce::roundToInt(127.0f * static_cast<float>(at) / static_cast<float>(count - 1)) : 0; };
        if (typeList.open || currentView == View::home)
        {
            const auto l = list();
            if (typeList.open)
                screen.line1 = "Type " + currentSlot().code;
            else
                screen.line1 = currentLivePage == layersPage ? juce::String("Layers")
                             : (browser.inCategory ? bank[static_cast<std::size_t>(browser.category)].name : juce::String("Categories"));
            auto row = l.rows[l.cursor];
            if (l.firstRowIsBack && l.cursor == 0)
                row = "< Back";
            if (l.cursor == l.used && ! (! typeList.open && ! browser.inCategory))
                row += " *";
            screen.line2 = row;
            screen.position = position(l.cursor, l.rows.size());
            if (loadingInstrument())
                screen.line1 = "Loading...";
            return screen;
        }
        const auto slots = slotsOf(currentSection);
        const auto slot = currentSlot();
        screen.line1 = loadingInstrument() ? juce::String("Loading...")
                     : pendingChoice.has_value() ? juce::String("Press to load")
                                                 : sectionName(currentSection) + " - " + (slot.empty ? juce::String("empty") : currentPage().name);
        screen.line2 = slot.code + " " + (slot.empty ? juce::String("empty") : slot.typeName);
        screen.position = position(slotCursor[static_cast<std::size_t>(currentSection)], static_cast<int>(slots.size()));
        return screen;
    }

    juce::String InterfaceModel::padLabel(int pad, bool shift) const
    {
        juce::ignoreUnused(shift);
        if (const int section = sectionOfPad(pad); section >= 0)
            return sectionName(section);
        switch (pad)
        {
            case 4: return "Stop";
            case 5: return "Play";
            case 6: return "Record";
            default: return currentPart == 0 ? "Home: A" : "Home: B"; // the part it switches (D188)
        }
    }

    PadLight InterfaceModel::padLight(int pad, bool shift) const
    {
        juce::ignoreUnused(shift);
        if (const int section = sectionOfPad(pad); section >= 0)
        {
            const bool lit = currentView == View::section && currentSection == section;
            return { sectionTint(section), lit ? 1.0f : 0.3f };
        }
        if (pad >= 4 && pad <= 6)
        {
            static constexpr std::array<Tint, 3> tints { Tint::stop, Tint::play, Tint::record };
            // the user's choice on the device: clearly white, green and red, though the recorder comes later; Stop
            // dimmed for now (D185)
            return { tints[static_cast<std::size_t>(pad - 4)], pad == 4 ? 0.3f : 1.0f };
        }
        // Home in the part's colour (D185): teal for A, yellow for B.
        const auto homeTint = currentPart == 0 ? Tint::home : Tint::partB;
        if (loadingInstrument()) // Home glows until the instrument is ready (D154, D156)
            return { homeTint, 0.1f + 0.9f * loadingGlow() };
        return { homeTint, currentView == View::home ? 1.0f : 0.3f };
    }

    juce::Colour InterfaceModel::padColour(int pad, bool shift) const
    {
        const auto light = padLight(pad, shift);
        return tintColour(light.tint).withMultipliedBrightness(light.brightness);
    }
}
