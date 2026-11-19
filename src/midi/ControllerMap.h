#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

/*
    Any MIDI controller on the panel (D211): which of its knobs, faders, pads and buttons stand for which of the
    panel's own controls (its "places": the main knob, knobs 1-8, the faders, the pads, Shift), and which stand for
    one sound setting on any page. A controller then works the panel as the MiniLab does, whatever page is open.
    Pure data and pure functions; ControllerSetup keeps one map for the whole computer.
*/
namespace coolsynth::midi
{
    /** How a control reports a movement. */
    enum class Encoding
    {
        absolute,       // a position, 0-127 (a knob or fader with end stops, or an endless knob counting 0-127)
        binaryOffset,   // a turn: 64 + steps (65 one step right, 63 one left)
        twosComplement, // a turn: 1 one step right, 127 one left
        signBit,        // a turn: 1 one step right, 65 one left
        offsetGap,      // a turn: 64 + steps right, 63 - steps left; 63 never comes (the MiniLab 3's DAW program, D216)
    };

    /** One control on the controller: a controller number or a note, on one channel (1-16). */
    struct ControlId
    {
        enum class Kind { cc, note };
        Kind kind = Kind::cc;
        int channel = 1;
        int number = 0;

        bool operator==(const ControlId&) const = default;
        [[nodiscard]] bool isValid() const noexcept { return channel >= 1 && channel <= 16 && number >= 0 && number <= 127; }
        /** "CC 21, channel 1" or "note 36, channel 10". */
        [[nodiscard]] juce::String describe() const;
    };

    /** The panel's places a control can stand for. */
    enum class Place
    {
        mainTurn, mainLeft, mainRight, mainPress, back, shift,
        knob1, knob2, knob3, knob4, knob5, knob6, knob7, knob8,
        fader1, fader2, fader3, fader4,
        pad1, pad2, pad3, pad4, pad5, pad6, pad7, pad8,
        count
    };
    inline constexpr int placeCount = static_cast<int>(Place::count);

    [[nodiscard]] juce::String placeName(Place place);     // "Main knob", "Knob 5 (pages)", "Pad 1 (Home)"
    [[nodiscard]] juce::String placeKey(Place place);      // "mainTurn", "knob5", "pad1": the file's names
    [[nodiscard]] std::optional<Place> placeFromKey(const juce::String& key);
    [[nodiscard]] bool isButtonPlace(Place place) noexcept; // pressed, not turned
    [[nodiscard]] inline int knobOf(Place p) noexcept  { return static_cast<int>(p) - static_cast<int>(Place::knob1) + 1; }  // 1-8
    [[nodiscard]] inline int faderOf(Place p) noexcept { return static_cast<int>(p) - static_cast<int>(Place::fader1); }    // 0-3
    [[nodiscard]] inline int padOf(Place p) noexcept   { return static_cast<int>(p) - static_cast<int>(Place::pad1); }      // 0-7
    [[nodiscard]] inline Place knobPlace(int knob) noexcept { return static_cast<Place>(static_cast<int>(Place::knob1) + knob - 1); }

    struct PlaceLink
    {
        Place place = Place::mainTurn;
        ControlId control;
        Encoding encoding = Encoding::absolute;
    };

    struct SettingLink
    {
        juce::String parameterId;
        ControlId control;
        Encoding encoding = Encoding::absolute;
    };

    /** The whole setup: one control stands for one thing at most, and a place or a setting has one control at most. */
    struct ControllerMap
    {
        juce::String name;      // "Akai MPK mini mk3", or "Your controller"
        juce::String profileId; // the ready profile it began as; empty when made by hand
        std::vector<PlaceLink> places;
        std::vector<SettingLink> settings;

        [[nodiscard]] bool empty() const noexcept { return places.empty() && settings.empty(); }
        [[nodiscard]] const PlaceLink* placeFor(const ControlId& control) const noexcept;
        [[nodiscard]] const SettingLink* settingFor(const ControlId& control) const noexcept;
        [[nodiscard]] const PlaceLink* linkOf(Place place) const noexcept;
        [[nodiscard]] const SettingLink* linkOf(const juce::String& parameterId) const noexcept;
        /** Links a place, or a setting; whatever that control or that place or setting had before is let go. */
        void link(Place place, ControlId control, Encoding encoding);
        /** One more control for a place that keeps the ones it has: a controller whose programs send different
            numbers for the same knob (the MiniLab 3, D216). The control still stands for this place only. */
        void alsoLink(Place place, ControlId control, Encoding encoding);
        void link(const juce::String& parameterId, ControlId control, Encoding encoding);
        void forget(Place place);
        void forget(const juce::String& parameterId);

        [[nodiscard]] juce::var toVar() const;
        [[nodiscard]] static ControllerMap fromVar(const juce::var& value);
    };

    /** The messages a control can never be: mod wheel, sustain, bank select, data entry and the channel-mode
        messages keep their own meaning for the sound (D211). */
    [[nodiscard]] bool isReserved(const ControlId& control) noexcept;

    /** A turn's steps from a relative control's value (positive is right); 0 for an absolute one. */
    [[nodiscard]] int relativeSteps(Encoding encoding, int value) noexcept;

    /** What a control sent while it was turned right and then left (the guided setup asks for that): a position, or
        a turn in one of the three usual ways. A control that never sends the same value twice in a row away from its
        ends reports a position; one that does reports turns. */
    [[nodiscard]] Encoding guessEncoding(const std::vector<int>& values) noexcept;

    [[nodiscard]] juce::String encodingKey(Encoding encoding);
    [[nodiscard]] juce::String encodingName(Encoding encoding); // "position", "turns (64 +/- steps)", ...

    /** A controller CoolSynth knows: its factory setup, and the port names it shows on Windows (D211). */
    struct ReadyProfile
    {
        juce::String id;
        juce::String name;
        juce::StringArray portNames; // matched case-insensitively as parts of a MIDI input's name
        juce::String note;           // what program the keyboard must be on, what it lacks
        ControllerMap map;
    };

    [[nodiscard]] const std::vector<ReadyProfile>& readyProfiles();
    [[nodiscard]] const ReadyProfile* readyProfile(const juce::String& id);
    /** The first ready profile one of these MIDI inputs belongs to, in the order the inputs are given. */
    [[nodiscard]] const ReadyProfile* readyProfileForPorts(const juce::StringArray& inputNames);
}
