#include "PianoBarComponent.h"

namespace coolsynth::ui
{
    PianoBarComponent::PianoBarComponent(juce::MidiKeyboardState& state)
        : keyboardState(state)
    {
        keyboardState.addListener(this);
        
        keyboard.setAvailableRange(startNote, startNote + numKeys - 1);
        keyboard.setScrollButtonsVisible(false);
        keyboard.setKeyWidth(40.0f);
        
        // Custom colours for the "purple overlay"
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::purple.withAlpha(0.5f));
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::purple.withAlpha(0.2f));

        addAndMakeVisible(keyboard);
        keyboard.setVisible(false);
        keyboard.setInterceptsMouseClicks(true, true);
    }

    PianoBarComponent::~PianoBarComponent()
    {
        keyboardState.removeListener(this);
    }

    void PianoBarComponent::paint(juce::Graphics& g)
    {
        if (collapsed)
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colours::black.brighter(0.1f));
            g.fillRoundedRectangle(bounds, 4.0f);
            
            const float ledSize = 8.0f;
            const float totalLedWidth = numKeys * ledSize;
            const float spacing = (bounds.getWidth() - totalLedWidth) / (numKeys + 1);
            
            for (int i = 0; i < numKeys; ++i)
            {
                int note = startNote + i;
                bool isDown = false;
                for (int ch = 0; ch <= 16; ++ch)
                {
                    if (keyboardState.isNoteOn(ch, note))
                    {
                        isDown = true;
                        break;
                    }
                }

                float x = spacing + i * (ledSize + spacing);
                float y = (bounds.getHeight() - ledSize) / 2.0f;
                
                if (isDown)
                {
                    g.setColour(juce::Colours::purple.withAlpha(0.4f));
                    g.fillEllipse(x - 2, y - 2, ledSize + 4, ledSize + 4);
                    g.setColour(juce::Colours::purple.brighter(0.5f));
                }
                else
                {
                    bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);
                    g.setColour(isBlackKey ? juce::Colours::black : juce::Colours::white.withAlpha(0.6f));
                }
                
                g.fillEllipse(x, y, ledSize, ledSize);
            }
        }
    }

    void PianoBarComponent::resized()
    {
        if (!collapsed)
        {
            keyboard.setBounds(getLocalBounds());
        }
    }

    void PianoBarComponent::handleNoteOn(juce::MidiKeyboardState*, int, int, float)
    {
        if (collapsed)
            juce::MessageManager::callAsync([this] { repaint(); });
    }

    void PianoBarComponent::handleNoteOff(juce::MidiKeyboardState*, int, int, float)
    {
        if (collapsed)
            juce::MessageManager::callAsync([this] { repaint(); });
    }

    void PianoBarComponent::mouseDown(const juce::MouseEvent& e)
    {
        // Toggle if clicking the bar itself (collapsed) or with Command/Ctrl
        if (collapsed || e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            collapsed = !collapsed;
            keyboard.setVisible(!collapsed);
            
            if (auto* parent = getParentComponent())
                parent->resized();
            
            repaint();
        }
    }
}
