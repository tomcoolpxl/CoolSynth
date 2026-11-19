#pragma once

#include <cstdint>

#include <array>
#include <functional>
#include <map>
#include <optional>
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
    enum class Tint { sound, shape, motion, effects, perform, home, stop, play, record, partB }; // partB: Home while part B is on (D185)

    /** The colour of a tint, as on the panel and the mock-up. */
    [[nodiscard]] juce::Colour tintColour(Tint tint) noexcept;
    /** The colour to send the MiniLab 3's pad LEDs for a tint, so it lights as the screen shows it (D184). */
    [[nodiscard]] juce::Colour miniLabColour(Tint tint) noexcept;

    /** A pad's light: which colour, how bright (1 lit, 0.3 dim). */
    struct PadLight
    {
        Tint tint = Tint::home;
        float brightness = 1.0f;
    };

    /** What makes a preset's sound, for the pills in the software sound browser (D169, docs/interface-v3.md 6):
        one label per engine its generator slots use, in slot order (part A, then part B where the preset plays
        it), at most three: VA, Sample, FM, WT, Grain, Physical; Noise only when no other engine plays. */
    [[nodiscard]] juce::StringArray enginePills(int factoryPreset);
    /** The same rules on any sound: `entry(parameter id, default)` gives a slot type's or the part mode's name. */
    [[nodiscard]] juce::StringArray enginePillsOf(const std::function<juce::String(const juce::String& id, const juce::String& otherwise)>& entry);

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
        bool pending = false; // an instrument chosen but not loaded yet: press or click to load (D156)
        bool idle = false;    // it can be set, but does nothing now (D201): shown dimmed, `why` says when it acts
        juce::String why;     // "both layers", "arp off", "B empty", "layer empty"
    };

    struct ListView
    {
        juce::String title;               // "CATEGORIES", "OUTRUN", "TYPE G1"
        juce::StringArray rows;
        std::vector<juce::StringArray> pills; // per row, the sound's engines (sound lists only; the panel's own screen)
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
        bool pad = false;  // a pad pressed: the MiniLab shows its pad pop-up (D186)
        int serial = 0;    // grows with every new pop-up
        int control = -1;  // knob 1-8 or fader 0-3 that raised it
    };

    class InterfaceModel
    {
    public:
        enum class View { home, section };
        static constexpr int sectionCount = 4;   // Sound, Shape, Motion, Effects
        static constexpr int livePageCount = 6;  // Macros, Arp, Groove, Chord, Voice, Layers (D196)
        static constexpr int layersPage = 5;
        /** The Layers page's actions, for the layer Home is on (D196). */
        [[nodiscard]] juce::StringArray layerActionNames() const { return layerActionNames(currentPart); }
        /** The same actions worded for a layer (0 A, 1 B): a right click on its panel on the screen offers them. */
        [[nodiscard]] juce::StringArray layerActionNames(int layer) const;
        /** One of those actions, for that layer (an index into layerActionNames). */
        void doLayerAction(int action, int layer);

        explicit InterfaceModel(SynthAudioProcessor& processor);

        // ---- Input ------------------------------------------------------------------------
        void turnMain(int steps);
        /** Every step the main knob was turned, from anywhere (the screen, the wheel, the keys, the MiniLab): the
            screen's knob turns with them (D193). Knob 5's page turns likewise. */
        [[nodiscard]] std::int64_t mainTurnCount() const noexcept { return mainTurns; }
        [[nodiscard]] std::int64_t pageTurnCount() const noexcept { return pageTurns; }
        void pressMain();
        void turnPageKnob(int steps);                         // knob 5
        void turnKnob(int knob, int steps, bool fine);        // knobs 1-4 and 6-8
        /** A MiniLab knob turned `counts` (D200): about 40 to a quarter turn. A number moves a step per count; a
            switch a step per 10 counts and a list (or knob 5's pages) an option per 6, so they do not race. */
        void turnKnobFromDevice(int knob, int counts, bool fine);
        static constexpr int countsPerSwitchStep = 10;
        static constexpr int countsPerListStep = 6;
        /** A click on knob 1-4 (the panel's knob or its card): loads the instrument it shows, if one is waiting. */
        void pressKnob(int knob);
        void pressPad(int pad, bool shift);                   // pads 0-7
        /** Faders 0-3; with pickup the value takes over only once it meets the stored one. */
        void moveFader(int fader, float value01, bool pickup);

        // ---- Any controller (D211) ---------------------------------------------------------------
        /** A controller's knob with end stops on knob 1-8, moved from `from` to `to` (0-127): value scaling. The value
            moves the way the knob does, by the share of the room each has left that way, so the two meet at the ends
            and the sound never jumps. A list, a switch, an instrument or knob 5's pages move an option each time the
            scaled place passes the next one. */
        void moveKnobFromController(int knob, int from, int to);
        /** A controller's control tied to one sound setting: moved by value scaling, or turned by steps. */
        void moveSettingFromController(const juce::String& parameterId, int from, int to);
        void turnSettingFromController(const juce::String& parameterId, int steps, bool fine);
        /** The sound setting knob 1-4 or 6-8 moves now, or null: what "Learn for Cutoff" ties a control to. */
        [[nodiscard]] juce::RangedAudioParameter* knobParameter(int knob) const;
        /** A setting's name as the panel shows it ("Cutoff"). */
        [[nodiscard]] juce::String settingName(const juce::String& parameterId) const;

        // ---- The mouse on the screen ----------------------------------------------------------
        /** A click on a row of the list in view: moves there and presses (opens, loads, goes back, sets the type). */
        void clickListRow(int row);
        /** A click on a slot of the section map: focuses it. */
        void clickSlot(int slot);
        /** A click on a page tab: the live page in Home, the focused slot's page in a section. */
        void clickPage(int page);
        /** One level up (D181): closes the type list, leaves a sound list for the rooms, a section for Home. */
        void back();
        /** A click on an item of the trail (D181): Categories, the room, the sound go there; the item on the main
            knob presses it; the page item turns to the next page. */
        void clickTrail(int item);

        // ---- State ------------------------------------------------------------------------
        [[nodiscard]] View view() const noexcept { return currentView; }
        [[nodiscard]] int section() const noexcept { return currentSection; }
        [[nodiscard]] int part() const noexcept { return currentPart; }
        /** The layer the sections edit has every generator and processor slot Off: it is left out (D190). */
        [[nodiscard]] bool currentLayerEmpty() const;
        /** For the layer screen (D195): a layer is empty, what it plays, and the layer mode in words. */
        [[nodiscard]] bool layerEmpty(int layer) const;
        [[nodiscard]] juce::String layerSoundName(int layer) const;
        [[nodiscard]] juce::String layerModeText() const;
        [[nodiscard]] bool typeListOpen() const noexcept { return typeList.open; }
        [[nodiscard]] bool inCategory() const noexcept { return browser.inCategory; }
        [[nodiscard]] int livePage() const noexcept { return currentLivePage; }
        [[nodiscard]] int revision() const noexcept { return changeCount; }

        /** Shift held on the MiniLab, shared with the panel so its Shift lights and acts too. */
        void setHardwareShift(bool held) noexcept { hardwareShift = held; touched(); }
        [[nodiscard]] bool hardwareShiftHeld() const noexcept { return hardwareShift; }

        /** Hold and Oct -/+ on the screen (D184), for whoever has no MiniLab: Hold holds the notes like a sustain
            pedal, or latches the arpeggio when the arp is on; the octave shifts the screen's keys. Greyed out while
            a MiniLab is connected, which has its own. */
        void toggleHold();
        [[nodiscard]] bool holdOn() const noexcept { return hold; }
        void shiftOctave(int steps);
        [[nodiscard]] int octaveShift() const noexcept { return octave; }
        static constexpr int maxOctaveShift = 3;
        /** The standalone app's MiniLab driver says whether the MiniLab is there; the plugin never does. */
        void setHardwareConnected(bool connected);
        [[nodiscard]] bool hardwareConnected() const noexcept { return hardware; }

        /** The player's keyboard over the screen's keys (D192, D203, D218): the MiniLab's 25 keys while it is
            connected, else the size chosen in Preferences; 0, no line. Its place goes by the notes it plays, as no
            keyboard says where its octave buttons put it: a key played outside the keys it was thought to have moves
            them, by whole octaves, as little as holds that key. */
        [[nodiscard]] int keyboardKeys() const noexcept;
        [[nodiscard]] int keyboardLowKey() const noexcept;
        [[nodiscard]] int keyboardKeySpan() const noexcept { return juce::jmax(0, keyboardKeys() - 1); }
        void keyPlayed(int note) noexcept;
        /** Where a keyboard of that size starts without an octave shift: C3 (48) for 25 and 37 keys (the MiniLab's),
            C2 (36) for 49 and 61, A0 (21) for 88. */
        [[nodiscard]] static int homeLowKey(int keys) noexcept;

        // ---- What to show -----------------------------------------------------------------
        [[nodiscard]] static juce::String sectionName(int section);
        [[nodiscard]] static Tint sectionTint(int section) noexcept;
        [[nodiscard]] std::vector<Slot> slotsOf(int section) const;
        [[nodiscard]] static std::vector<SectionRow> rowsOf(int section);
        [[nodiscard]] int slotIndex(int section) const noexcept { return slotCursor[static_cast<std::size_t>(section)]; }
        [[nodiscard]] int pageIndexOf(int section, int slot) const;
        [[nodiscard]] Slot currentSlot() const;
        /** The Effects section is on a slot of the chain at the end (F1-F6), which belongs to neither layer (D195). */
        [[nodiscard]] bool editingMaster() const;
        [[nodiscard]] Page currentPage() const;               // section page, or the live page in Home
        [[nodiscard]] Page livePageAt(int livePage) const;    // one of Home's live pages, for the CLAP pages (D216)
        [[nodiscard]] juce::StringArray livePageNames() const;
        [[nodiscard]] Tint pageTint() const noexcept;         // the colour of knobs 1-4
        [[nodiscard]] KnobView knob(int knob) const;          // 1-4 and 6-8
        [[nodiscard]] ListView list() const;                  // Home or the type list; empty rows in a section
        [[nodiscard]] std::vector<TrailItem> trail() const;
        [[nodiscard]] juce::String mainTurnRole() const;
        [[nodiscard]] juce::String mainPressRole() const;
        [[nodiscard]] juce::String pageKnobRole() const;
        [[nodiscard]] juce::String hint() const;
        /** True while the sound's sampled instrument is still being read in: Home glows, the screen says
            "Loading..." and the keys wait (D154, D156). */
        [[nodiscard]] bool loadingInstrument() const;
        /** The glow's brightness now, 0 to 1: a smooth pulse a little under once a second. */
        [[nodiscard]] static float loadingGlow();
        /** True while knob 1-4 shows an instrument that is chosen but not loaded (D156). */
        [[nodiscard]] bool choicePending() const noexcept { return pendingChoice.has_value(); }
        [[nodiscard]] DeviceScreen deviceScreen() const;
        [[nodiscard]] juce::Colour padColour(int pad, bool shift) const; // colour times brightness
        [[nodiscard]] PadLight padLight(int pad, bool shift) const;       // for the MiniLab: its own colours
        /** The pads, left to right (D184): Home, Sound, Shape, Motion, Stop, Play, Record, Effects. */
        static constexpr int homePad = 0;
        static constexpr int stopPad = 4; // every sound off, its tails too
        /** Layer A or B for the sections to edit (D185, D189): Home pressed again while Home is open, or the screen's "Layer A". */
        void togglePart();
        /** After a reset: Home, layer A, its first live page (D199). */
        void resetView();
        [[nodiscard]] static int sectionOfPad(int pad) noexcept;   // -1 for Home and the recorder's pads
        [[nodiscard]] static int padOfSection(int section) noexcept;
        [[nodiscard]] juce::String padLabel(int pad, bool shift) const;
        [[nodiscard]] const Popup& popup() const noexcept { return lastPopup; }
        [[nodiscard]] juce::String playingPresetName() const;
        [[nodiscard]] juce::String faderName(int fader) const;
        [[nodiscard]] float faderValue(int fader) const;
        /** The Master's boost above its top, 0 to 12 dB (D225), and its setting from the screen's fader. */
        [[nodiscard]] float masterBoostDb() const;
        void setMasterBoost(float db);

        // ---- The bank -----------------------------------------------------------------------
        struct Category
        {
            juce::String name;
            std::vector<int> presets; // indices into the factory bank
        };
        [[nodiscard]] const std::vector<Category>& categories() const noexcept { return bank; }
        [[nodiscard]] int categoryOfPreset(int preset) const noexcept;
        /** Reads the My Sounds folder again and rebuilds the rooms, My Sounds first when it holds anything (D180). */
        void refreshMySounds();
        /** Saved sounds have ids from here on in the rooms' lists; factory sounds are their bank index. */
        static constexpr int mySoundBase = 1000000;

    private:
        static constexpr int recallRow = -2; // the browser's row for an edit to go back to (D225)
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

        /** A sampled slot's instrument or style: choosing one reads an instrument into memory, so turning only
            shows the choice and a press loads it (D156). */
        struct PendingChoice
        {
            juce::String id;
            int knob = 0;
            float value = 0.0f; // normalised
            int index = 0;      // in the list as browsed: the instruments, then the user's files (D207)
        };
        [[nodiscard]] bool loadsInstrument(const juce::String& parameterId) const;
        void commitPendingChoice();

        /** The user's files and folders (kits) a Sample slot can play after the instruments (D207), read from the User
            folder when browsing starts. With none, one line says where to put them. */
        struct UserEntry
        {
            juce::String name; // the path inside the User folder, forward slashes; empty for the "none yet" line
            bool kit = false;
        };
        std::vector<UserEntry> userEntries;
        juce::uint32 userEntriesReadAt = 0; // when the folder was last read
        juce::String userFolderPath;
        /** Reads the user's folder again. `fromController`: at most once a second, as a controller's knob with end stops
            sends a hundred messages a second and each would read it (review of D211); a turn of a knob reads it each time,
            so files just dropped in show at once. */
        void refreshUserEntries(bool fromController = false);
        /** The layer (0, 1) and generator slot (1..3) of a Sample slot's source (".p1"), or false. */
        [[nodiscard]] bool sampleSourceOf(const juce::String& parameterId, int& part, int& slot) const;
        /** Where the slot's source sits in the browsed list: its instrument, or the user's file after them. */
        [[nodiscard]] int loadedSourceIndex(const juce::RangedAudioParameter& source, int part, int slot) const;
        [[nodiscard]] juce::String userEntryText(int entry) const;
        /** A file of the user's on a knob's two lines (D225): where it is ("Kit: CST2", "CST2/", "Your files"),
            then its name without the extension (a kit: how many files), so a kit and a file in it never look alike. */
        void describeUserFile(const juce::String& file, bool kit, juce::String& where, juce::String& name) const;
        void cancelPendingChoice() noexcept { pendingChoice.reset(); }

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
        [[nodiscard]] juce::String soundName(int sound) const;
        [[nodiscard]] juce::StringArray soundPills(int sound) const;
        [[nodiscard]] std::vector<juce::String> faderParameterIds(int fader) const;
        void touched() noexcept { ++changeCount; }

        SynthAudioProcessor& synth;
        juce::AudioProcessorValueTreeState& state;
        std::vector<Category> bank;
        std::vector<juce::File> mySoundFiles; // the My Sounds room, A to Z

        View currentView = View::section;
        int currentSection = 0;
        int currentPart = 0;
        int currentLivePage = 0;
        std::array<int, sectionCount> slotCursor {};
        std::map<juce::String, int> pageOfSlot;
        Browser browser;
        TypeList typeList;
        bool hold = false;     // the screen's Hold (D184)
        int octave = 0;        // the screen's keys, octaves from C3
        bool hardware = false; // a MiniLab connected
        int keyboardOctave = 0;     // the player's keyboard's octave shift, as its notes tell (D192)
        int keyboardOctaveKeys = 0; // the size that shift was found for: another size starts again at home
        int layerAction = 0;   // the Layers page's action under the main knob (D196)
        std::array<int, 9> deviceCounts {};            // counts not yet a step, per MiniLab knob (D200)
        std::array<juce::String, 9> deviceTargets {};  // what each knob moved last: another target starts afresh
        /** Where value scaling left a controller's knob (D211): its place, finer than the setting keeps it, while the
            setting stays where it was put; anything else moving it starts afresh from where it is. */
        struct ScaledPlace
        {
            juce::String target;
            float position = 0.0f;
            float seenValue = -1.0f; // a number's value as set
            int seenIndex = -1;      // a list's option as set
        };
        std::array<ScaledPlace, 9> scaledKnobs {};
        std::map<juce::String, ScaledPlace> scaledSettings;
        /** Value scaling of `position` (0-1) for a knob moved from h0 to h1 (0-1). */
        [[nodiscard]] static float valueScaled(float position, float h0, float h1) noexcept;
        /** A list of `count` options at `index`, value-scaled: turns it by the options passed. */
        void scaleList(ScaledPlace& place, const juce::String& target, int index, int count, float h0, float h1,
                       const std::function<void(int)>& turn);
        /** A number, value-scaled; true when it moved. */
        bool scaleNumber(ScaledPlace& place, juce::RangedAudioParameter& p, float h0, float h1);
        void doLayerAction(int action) { doLayerAction(action, currentPart); }
        std::int64_t mainTurns = 0, pageTurns = 0; // steps turned, for the screen's knobs (D193)
        Popup lastPopup;
        int changeCount = 0;
        bool hardwareShift = false;
        std::optional<PendingChoice> pendingChoice;
    };
}
