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
    to the window. Keys: arrows turn the main knob (left/right) and knob 5 (up/down), Enter presses
    it, 1-8 are the pads, Shift is Shift.
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

    /** CoolSynth's Options (D174), from the panel's button and the standalone window's: Audio and MIDI (where the
        app offers it), Multi-core Auto or Off, the last second's CPU work, this computer. Message thread. */
    static void showOptions(SynthAudioProcessor& synth, juce::Component& target);
    /** The last second's work in one line: "CPU 7%, slowest block 9%, helpers 1 3 2%". */
    [[nodiscard]] static juce::String cpuSummary(const SynthAudioProcessor& synth);
    /** Asks for a name and saves the sound as it is now in My Sounds (D180); asks before replacing one. */
    static void askToSaveSound(SynthAudioProcessor& synth, juce::Component& near);

    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 690;

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
    bool shiftLatched = false;
    bool shiftKeyDown = false;
    int shownRevision = -1;

    friend class Content;
};
