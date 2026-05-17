#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

namespace coolsynth::ui
{
    class PianoBarComponent final : public juce::Component,
                                    public juce::SettableTooltipClient,
                                    public juce::MidiKeyboardStateListener
    {
    public:
        enum class ViewMode { ledRow, keys };

        explicit PianoBarComponent(juce::MidiKeyboardState& state);
        ~PianoBarComponent() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

        void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
        void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;

        // Fixed-height row that hosts both views; never expands or collapses.
        static constexpr int desiredHeight = 120;
        int getDesiredHeight() const noexcept { return desiredHeight; }

        // Width the parent should give us so the keyboard fits at its natural size.
        int getDesiredWidth() const noexcept;

        ViewMode getViewMode() const noexcept { return viewMode; }
        void setViewMode(ViewMode newMode);

    private:
        void updateKeyboardRange();
        void applyViewMode();
        juce::Rectangle<int> getCentreArea() const noexcept;

        juce::MidiKeyboardState& keyboardState;
        ViewMode viewMode = ViewMode::ledRow;

        int currentOctaveOffset = 0; // 0 means C2 to C6 (starting note 36)
        static constexpr int baseStartNote = 36; // C2
        static constexpr int numKeys = 49;       // 4 octaves + 1 note
        static constexpr int leftColumnWidth = 36;
        static constexpr int columnGap = 6;

        juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

        juce::TextButton keysToggleButton { "KEYS" };
        juce::TextButton octaveDownButton { "-" };
        juce::TextButton octaveUpButton { "+" };
        juce::Label octaveLabel;
        std::array<float, 128> noteVelocities {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoBarComponent)
    };
}
