#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

class SynthAudioProcessor;
namespace coolsynth::ui { class InterfaceModel; }

/*
    The V3 panel (docs/interface-v3.md): the Arturia MiniLab 3's layout on screen, driving the shared
    InterfaceModel. Shift and its strips on the left, the screen with the main knob under it, knobs
    1-8, four faders, eight pads, a short row of keys. Drawn at a design size of 1100 x 690 and scaled
    to the window. Keys: left/right turn the main knob; up/down move through a list (Home, a type list)
    or turn knob 5's pages in a section; Page Up/Down five rows, Home/End the ends; Enter presses the
    main knob; Escape or Backspace go back; 1-8 are the pads; Shift is Shift.

    The mouse, without a MiniLab (D181): every row, slot, page tab, card and trail item is clicked; the
    header's chip goes back and "Layer A" switches the layer; a right click goes back; the wheel scrolls
    the list it is over, turns a card's knob or the page tabs; a slot clicked again opens its type list.
*/
class PanelEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit PanelEditor(SynthAudioProcessor& processor);
    ~PanelEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

    /** Draws everything again from the model now (the timer does it 30 times a second). */
    void refresh() { timerCallback(); }

    /** Opens the controller setup's card (D211), as Prefs' Controller button does; or at its first step. */
    void showControllerSetup(bool stepByStep = false);
    /** Opens Preferences, as the Prefs button does (Escape closes it). */
    void showPreferences();

    /** For the app's MCP server (D227), message thread: the panel as text, as it is drawn now, with the names of what
        can be clicked; a click by one of those names, as the mouse does (a fader takes `value`, 0 to 1 of its travel);
        a picture of one part ("main screen", "layer screen", "knobs", "faders", "pads", "keys", "left", or "x,y,w,h"
        in the design's pixels), `zoom` (1 to 3) times its size. */
    [[nodiscard]] juce::String describeForMcp();
    [[nodiscard]] juce::String clickForMcp(const juce::String& name, double value = 0.0);
    [[nodiscard]] juce::Image pictureForMcp(const juce::String& area, float zoom, juce::String& error);

    /** CoolSynth's Options (D174), from the panel's button and the standalone window's: Audio and MIDI (where the
        app offers it), Multi-core Auto or Off, the last second's CPU work, this computer. Message thread. */
    static void showOptions(SynthAudioProcessor& synth, juce::Component& target);
    /** The last second's work in one line: "CPU 7%, slowest block 9%, helpers 1 3 2%". */
    [[nodiscard]] static juce::String cpuSummary(const SynthAudioProcessor& synth);
    /** Asks for a name and saves the sound as it is now in My Sounds (D180); asks before replacing one. */
    static void askToSaveSound(SynthAudioProcessor& synth, juce::Component& near);

    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 680;

private:
    class Knob;
    class Pad;
    class Screen;
    class Content;

    void timerCallback() override;
    [[nodiscard]] bool shiftHeld() const noexcept;

    SynthAudioProcessor& synth;
    coolsynth::ui::InterfaceModel& model;
    std::unique_ptr<Content> content;
    bool shiftKeyDown = false;
    int shownRevision = -1;

    friend class Content;
};
