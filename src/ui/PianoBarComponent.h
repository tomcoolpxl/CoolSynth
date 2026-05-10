#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace coolsynth::ui
{
    class PianoBarComponent final : public juce::Component,
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
        int getDesiredHeight() const noexcept { return collapsed ? 24 : 100; }

        void mouseDown(const juce::MouseEvent& event) override;

    private:
        juce::MidiKeyboardState& keyboardState;
        bool collapsed = true;
        
        static constexpr int startNote = 48; // C3
        static constexpr int numKeys = 25;   // 2 octaves + 1 note (C to C)
        
        juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoBarComponent)
    };
}
