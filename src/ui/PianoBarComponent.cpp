#include "PianoBarComponent.h"

namespace coolsynth::ui
{
    PianoBarComponent::PianoBarComponent(juce::MidiKeyboardState& state)
        : keyboardState(state)
    {
        keyboardState.addListener(this);
        
        keyboard.setScrollButtonsVisible(false);
        keyboard.setKeyWidth(32.0f);
        
        // Custom colours for the "purple overlay"
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::purple.withAlpha(0.5f));
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::purple.withAlpha(0.2f));

        addAndMakeVisible(keyboard);
        keyboard.setVisible(false);
        keyboard.setInterceptsMouseClicks(true, true);

        octaveDownButton.onClick = [this] { 
            currentOctaveOffset = juce::jmax(-3, currentOctaveOffset - 1);
            updateKeyboardRange();
        };
        octaveUpButton.onClick = [this] { 
            currentOctaveOffset = juce::jmin(3, currentOctaveOffset + 1);
            updateKeyboardRange();
        };

        octaveLabel.setJustificationType(juce::Justification::centred);
        octaveLabel.setText("Octave", juce::dontSendNotification);
        octaveLabel.setFont(juce::FontOptions(12.0f));

        addChildComponent(octaveDownButton);
        addChildComponent(octaveUpButton);
        addChildComponent(octaveLabel);

        updateKeyboardRange();
    }

    PianoBarComponent::~PianoBarComponent()
    {
        keyboardState.removeListener(this);
    }

    void PianoBarComponent::updateKeyboardRange()
    {
        const int startNote = baseStartNote + (currentOctaveOffset * 12);
        keyboard.setAvailableRange(startNote, startNote + numKeys - 1);
        octaveLabel.setText("Octave: " + juce::String(currentOctaveOffset), juce::dontSendNotification);
        repaint();
    }

    void PianoBarComponent::paint(juce::Graphics& g)
    {
        if (collapsed)
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colours::black.brighter(0.1f));
            g.fillRoundedRectangle(bounds, 4.0f);
            
            const int numWhiteKeys = 29; // C to C (4 octaves) has 29 white keys in 49 notes
            const float ledSize = 6.0f;
            
            const float whiteSpacing = (bounds.getWidth() - (numWhiteKeys * ledSize)) / (numWhiteKeys + 1);
            
            const int startNote = baseStartNote + (currentOctaveOffset * 12);
            int whiteKeyIndex = 0;
            for (int i = 0; i < numKeys; ++i)
            {
                int note = startNote + i;
                if (note < 0 || note > 127) continue;

                bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);
                
                float x = 0;
                if (!isBlackKey)
                {
                    x = whiteSpacing + whiteKeyIndex * (ledSize + whiteSpacing);
                    whiteKeyIndex++;
                }
                else
                {
                    x = whiteSpacing + (whiteKeyIndex - 1) * (ledSize + whiteSpacing) + (ledSize + whiteSpacing) / 2.0f;
                }

                float y = (bounds.getHeight() - ledSize) / 2.0f;
                
                bool isDown = false;
                for (int ch = 0; ch <= 16; ++ch)
                {
                    if (keyboardState.isNoteOn(ch, note))
                    {
                        isDown = true;
                        break;
                    }
                }
                
                if (isDown)
                {
                    g.setColour(juce::Colours::purple.withAlpha(0.4f));
                    g.fillEllipse(x - 2, y - 2, ledSize + 4, ledSize + 4);
                    g.setColour(juce::Colours::purple.brighter(0.5f));
                }
                else
                {
                    g.setColour(isBlackKey ? juce::Colours::black : juce::Colours::white.withAlpha(0.6f));
                }
                
                g.fillEllipse(x, y, ledSize, ledSize);
            }
        }
    }

    void PianoBarComponent::resized()
    {
        auto bounds = getLocalBounds();

        if (!collapsed)
        {
            auto controlsArea = bounds.removeFromLeft(100).reduced(5);
            octaveLabel.setBounds(controlsArea.removeFromTop(20));
            auto buttonsRow = controlsArea.removeFromTop(30);
            octaveDownButton.setBounds(buttonsRow.removeFromLeft(buttonsRow.getWidth() / 2).reduced(2));
            octaveUpButton.setBounds(buttonsRow.reduced(2));

            octaveDownButton.setVisible(true);
            octaveUpButton.setVisible(true);
            octaveLabel.setVisible(true);

            const float totalKeyboardWidth = keyboard.getTotalKeyboardWidth();
            if (totalKeyboardWidth < bounds.getWidth())
            {
                auto keyboardBounds = bounds.withSizeKeepingCentre((int)totalKeyboardWidth, bounds.getHeight());
                keyboard.setBounds(keyboardBounds);
            }
            else
            {
                keyboard.setBounds(bounds);
            }
        }
        else
        {
            octaveDownButton.setVisible(false);
            octaveUpButton.setVisible(false);
            octaveLabel.setVisible(false);
        }
    }

    void PianoBarComponent::handleNoteOn(juce::MidiKeyboardState*, int, int, float)
    {
        juce::MessageManager::callAsync([this] { repaint(); });
    }

    void PianoBarComponent::handleNoteOff(juce::MidiKeyboardState*, int, int, float)
    {
        juce::MessageManager::callAsync([this] { repaint(); });
    }

    void PianoBarComponent::mouseDown(const juce::MouseEvent& e)
    {
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
