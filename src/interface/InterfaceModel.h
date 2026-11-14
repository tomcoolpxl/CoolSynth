#pragma once

#include <array>
#include <map>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

class SynthAudioProcessor;

/*
    The V3 interface's logic (docs/interface-v3.md, decisions D140-D147), independent of drawing:
    navigation E ("one map per section"), Home as the sound browser and live page, the type list,
    knobs, faders with pickup, and what the MiniLab's own screen and pads show. The on-screen panel
    and the MiniLab driver both drive one instance and read it back. Message thread only.
*/
namespace coolsynth::ui
{
    enum class Tint { sound, shape, motion, effects, perform, home, stop, play, record };

    /** The colour of a tint, as on the panel and the mock-up. */
    [[nodiscard]] juce::Colour tintColour(Tint tint) noexcept;

    struct Page
    {
        juce::String name;
        std::array<juce::String, 4> ids;    // parameter IDs for knobs 1-4; empty: no parameter
        std::array<juce::String, 4> labels; // names to show; empty: the parameter's own name
    };

    struct Slot
    {
        juce::String code;     // "G1", "P2", "E1", "F3"
        juce::String label;    // "Gen 1", "Amp env", "Master 3"
        juce::String typeId;   // the type parameter, empty for slots with one type
        juce::String typeName; // "VA", "Ladder", "Env", "LFO"
        bool empty = false;
        std::vector<Page> pages;
    };

    struct SectionRow
    {
        juce::String label;  // "mixed", "in a row", "envelopes", ...
        std::vector<int> slots;
        juce::String joiner; // "+", "→" or empty
        juce::String after;  // "→ Shape" or empty
    };

    struct KnobView
    {
        juce::String role;  // "Tone", "knob 1", ...
        juce::String name;  // what the knob changes; "—" when nothing
        juce::String value; // its value as text
        float normalised = 0.0f;
        bool active = false;
    };

    struct ListView
    {
        juce::String title;               // "CATEGORIES", "OUTRUN", "TYPE G1"
        juce::StringArray rows;
        int cursor = 0;
        int used = -1;                    // the row playing or in use, -1 none
        bool firstRowIsBack = false;
    };

    struct TrailItem
    {
        juce::String text;
        juce::String mark; // "◎" (main knob), "5" (knob 5) or empty
        bool current = false;
    };

    struct DeviceScreen
    {
        juce::String line1; // small: where you are
        juce::String line2; // big: what the main knob is on
        int position = 0;   // the scroll marker, 0..127
    };

    struct Popup
    {
        juce::String name;
        juce::String value;
        float normalised = 0.0f;
        bool fader = false;
        int serial = 0;    // grows with every new pop-up
        int control = -1;  // knob 1-8 or fader 0-3 that raised it
    };

    class InterfaceModel
    {
    public:
        enum class View { home, section };
        static constexpr int sectionCount = 4;   // Sound, Shape, Motion, Effects
        static constexpr int livePageCount = 5;  // Macros, Arp, Groove, Chord, Voice

        explicit InterfaceModel(SynthAudioProcessor& processor);

        // ---- Input ------------------------------------------------------------------------
        void turnMain(int steps);
        void pressMain();
        void turnPageKnob(int steps);                         // knob 5
        void turnKnob(int knob, int steps, bool fine);        // knobs 1-4 and 6-8
        void pressPad(int pad, bool shift);                   // pads 0-7
        /** Faders 0-3; with pickup the value takes over only once it meets the stored one. */
        void moveFader(int fader, float value01, bool pickup);

        // ---- The mouse on the screen ----------------------------------------------------------
        /** A click on a row of the list in view: moves there and presses (opens, loads, goes back, sets the type). */
        void clickListRow(int row);
        /** A click on a slot of the section map: focuses it. */
        void clickSlot(int slot);
        /** A click on a page tab: the live page in Home, the focused slot's page in a section. */
        void clickPage(int page);

        // ---- State ------------------------------------------------------------------------
        [[nodiscard]] View view() const noexcept { return currentView; }
        [[nodiscard]] int section() const noexcept { return currentSection; }
        [[nodiscard]] int part() const noexcept { return currentPart; }
        [[nodiscard]] bool typeListOpen() const noexcept { return typeList.open; }
        [[nodiscard]] bool inCategory() const noexcept { return browser.inCategory; }
        [[nodiscard]] int livePage() const noexcept { return currentLivePage; }
        [[nodiscard]] int revision() const noexcept { return changeCount; }

        /** Shift held on the MiniLab, shared with the panel so its Shift lights and acts too. */
        void setHardwareShift(bool held) noexcept { hardwareShift = held; touched(); }
        [[nodiscard]] bool hardwareShiftHeld() const noexcept { return hardwareShift; }

        // ---- What to show -----------------------------------------------------------------
        [[nodiscard]] static juce::String sectionName(int section);
        [[nodiscard]] static Tint sectionTint(int section) noexcept;
        [[nodiscard]] std::vector<Slot> slotsOf(int section) const;
        [[nodiscard]] static std::vector<SectionRow> rowsOf(int section);
        [[nodiscard]] int slotIndex(int section) const noexcept { return slotCursor[static_cast<std::size_t>(section)]; }
        [[nodiscard]] int pageIndexOf(int section, int slot) const;
        [[nodiscard]] Slot currentSlot() const;
        [[nodiscard]] Page currentPage() const;               // section page, or the live page in Home
        [[nodiscard]] juce::StringArray livePageNames() const;
        [[nodiscard]] Tint pageTint() const noexcept;         // the colour of knobs 1-4
        [[nodiscard]] KnobView knob(int knob) const;          // 1-4 and 6-8
        [[nodiscard]] ListView list() const;                  // Home or the type list; empty rows in a section
        [[nodiscard]] std::vector<TrailItem> trail() const;
        [[nodiscard]] juce::String mainTurnRole() const;
        [[nodiscard]] juce::String mainPressRole() const;
        [[nodiscard]] juce::String pageKnobRole() const;
        [[nodiscard]] juce::String hint() const;
        [[nodiscard]] DeviceScreen deviceScreen() const;
        [[nodiscard]] juce::Colour padColour(int pad, bool shift) const; // colour times brightness
        [[nodiscard]] juce::String padLabel(int pad, bool shift) const;
        [[nodiscard]] const Popup& popup() const noexcept { return lastPopup; }
        [[nodiscard]] juce::String playingPresetName() const;
        [[nodiscard]] juce::String faderName(int fader) const;
        [[nodiscard]] float faderValue(int fader) const;

        // ---- The bank -----------------------------------------------------------------------
        struct Category
        {
            juce::String name;
            std::vector<int> presets; // indices into the factory bank
        };
        [[nodiscard]] const std::vector<Category>& categories() const noexcept { return bank; }
        [[nodiscard]] int categoryOfPreset(int preset) const noexcept;

    private:
        struct Browser
        {
            bool inCategory = true;
            int category = 0;
            int row = 0; // inside a category: 0 is ‹ Back, 1.. the sounds
        };
        struct TypeList
        {
            bool open = false;
            int row = 0; // 0 is ‹ Back, 1.. the types
        };

        juce::RangedAudioParameter* parameter(const juce::String& id) const;
        void stepParameter(juce::RangedAudioParameter& parameter, int steps, bool fine);
        void setNormalised(juce::RangedAudioParameter& parameter, float value);
        void raisePopup(const juce::String& name, const juce::String& value, float normalised, bool fader, int control);
        void openHome();
        void openSection(int section);
        void openTypeList();
        void chooseType(int typeIndex);
        juce::StringArray typeNames() const;
        juce::String slotKey(int section, int slot) const;
        [[nodiscard]] int playingPreset() const;
        void loadPreset(int preset);
        [[nodiscard]] std::vector<juce::String> faderParameterIds(int fader) const;
        void touched() noexcept { ++changeCount; }

        SynthAudioProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        std::vector<Category> bank;

        View currentView = View::section;
        int currentSection = 0;
        int currentPart = 0;
        int currentLivePage = 0;
        std::array<int, sectionCount> slotCursor {};
        std::map<juce::String, int> pageOfSlot;
        Browser browser;
        TypeList typeList;
        std::array<bool, 4> faderCaught {};
        std::array<float, 4> faderLast { -1.0f, -1.0f, -1.0f, -1.0f };
        Popup lastPopup;
        int changeCount = 0;
        bool hardwareShift = false;
    };
}
