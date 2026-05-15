#include "PianoBarComponent.h"

#include "ActionButtonLookAndFeel.h"
#include "UiPalette.h"

namespace coolsynth::ui
{
    PianoBarComponent::PianoBarComponent(juce::MidiKeyboardState& state)
        : keyboardState(state)
    {
        keyboardState.addListener(this);
        
        keyboard.setScrollButtonsVisible(false);
        keyboard.setKeyWidth(32.0f);
        
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,
                           juce::Colour::fromRGB(164, 34, 28).withAlpha(0.42f));
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                           juce::Colour::fromRGB(164, 34, 28).withAlpha(0.18f));

        addAndMakeVisible(keyboard);
        keyboard.setVisible(false);
        keyboard.setInterceptsMouseClicks(true, true);

        ledModeButton.onClick = [this]
        {
            collapsed = true;
            keyboard.setVisible(false);

            if (auto* parent = getParentComponent())
                parent->resized();

            repaint();
        };
        octaveDownButton.onClick = [this] { 
            currentOctaveOffset = juce::jmax(-3, currentOctaveOffset - 1);
            updateKeyboardRange();
        };
        octaveUpButton.onClick = [this] { 
            currentOctaveOffset = juce::jmin(3, currentOctaveOffset + 1);
            updateKeyboardRange();
        };

        octaveLabel.setJustificationType(juce::Justification::centred);
        octaveLabel.setText("OCTAVE", juce::dontSendNotification);
        octaveLabel.setFont(juce::FontOptions(12.0f));
        octaveLabel.setColour(juce::Label::textColourId, palette::ledTextOff);

        applyGreenActionButtonStyle(ledModeButton, "ledModeButton");
        ledModeButton.setButtonText({});
        ledModeButton.setTooltip("RETURN TO LED STRIP\nCollapse the keyboard back to the compact LED view.");
        applyGreenActionButtonStyle(octaveDownButton);
        octaveDownButton.setTooltip("OCTAVE DOWN\nShift the visible keyboard range down by one octave.");
        applyGreenActionButtonStyle(octaveUpButton);
        octaveUpButton.setTooltip("OCTAVE UP\nShift the visible keyboard range up by one octave.");

        addChildComponent(ledModeButton);
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
        octaveLabel.setText("OCTAVE: " + juce::String(currentOctaveOffset), juce::dontSendNotification);
        repaint();
    }

    void PianoBarComponent::paint(juce::Graphics& g)
    {
        if (collapsed)
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour(palette::panelRaised);
            g.fillRoundedRectangle(bounds, 5.0f);
            
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
                    const auto velocity = noteVelocities[static_cast<size_t>(note)];
                    const auto accent = juce::jlimit(0.0f, 1.0f, std::pow(velocity, 0.55f));
                    const auto glowAlpha = 0.12f + (accent * 0.34f);
                    auto ledColour = juce::Colour::fromFloatRGBA(accent, 0.0f, 0.0f, 1.0f);

                    g.setColour(juce::Colour::fromRGB(120, 0, 0).withAlpha(glowAlpha));
                    g.fillEllipse(x - 2, y - 2, ledSize + 4, ledSize + 4);
                    g.setColour(ledColour);
                }
                else
                {
                    g.setColour(isBlackKey ? juce::Colours::black : juce::Colours::white.withAlpha(0.42f));
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
            auto ledButtonArea = getLocalBounds().reduced(6);
            ledModeButton.setBounds(ledButtonArea.removeFromTop(18).removeFromRight(18));

            auto controlsArea = bounds.removeFromLeft(120).reduced(5);
            octaveLabel.setBounds(controlsArea.removeFromTop(20));
            auto buttonsRow = controlsArea.removeFromTop(30);
            octaveDownButton.setBounds(buttonsRow.removeFromLeft(buttonsRow.getWidth() / 2).reduced(2));
            octaveUpButton.setBounds(buttonsRow.reduced(2));

            ledModeButton.setVisible(true);
            octaveDownButton.setVisible(true);
            octaveUpButton.setVisible(true);
            octaveLabel.setVisible(true);

            const float totalKeyboardWidth = keyboard.getTotalKeyboardWidth();
            bounds.removeFromRight(28);
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
            ledModeButton.setVisible(false);
            octaveDownButton.setVisible(false);
            octaveUpButton.setVisible(false);
            octaveLabel.setVisible(false);
        }
    }

    void PianoBarComponent::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
    {
        if (juce::isPositiveAndBelow(midiNoteNumber, static_cast<int>(noteVelocities.size())))
            noteVelocities[static_cast<size_t>(midiNoteNumber)] = juce::jlimit(0.0f, 1.0f, velocity);

        juce::MessageManager::callAsync([this] { repaint(); });
    }

    void PianoBarComponent::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
    {
        if (juce::isPositiveAndBelow(midiNoteNumber, static_cast<int>(noteVelocities.size())))
            noteVelocities[static_cast<size_t>(midiNoteNumber)] = 0.0f;

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
