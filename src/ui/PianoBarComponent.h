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
        explicit PianoBarComponent(juce::MidiKeyboardState& state);
        ~PianoBarComponent() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        
        void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
        void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;

        bool isCollapsed() const noexcept { return collapsed; }
        int getDesiredHeight() const noexcept { return collapsed ? 24 : 120; }

        void mouseDown(const juce::MouseEvent& event) override;

    private:
        void updateKeyboardRange();

        juce::MidiKeyboardState& keyboardState;
        bool collapsed = true;
        
        int currentOctaveOffset = 0; // 0 means C2 to C6 (starting note 36)
        static constexpr int baseStartNote = 36; // C2
        static constexpr int numKeys = 49;       // 4 octaves + 1 note
        
        juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
        
        juce::TextButton ledModeButton { "LED" };
        juce::TextButton octaveDownButton { "-" };
        juce::TextButton octaveUpButton { "+" };
        juce::Label octaveLabel;
        std::array<float, 128> noteVelocities {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoBarComponent)
    };
}
