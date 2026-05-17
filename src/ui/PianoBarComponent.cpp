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
        keyboard.setInterceptsMouseClicks(true, true);

        keysToggleButton.setClickingTogglesState(true);
        keysToggleButton.onClick = [this]
        {
            setViewMode(keysToggleButton.getToggleState() ? ViewMode::keys : ViewMode::ledRow);
        };

        octaveDownButton.onClick = [this]
        {
            currentOctaveOffset = juce::jmax(-3, currentOctaveOffset - 1);
            updateKeyboardRange();
        };
        octaveUpButton.onClick = [this]
        {
            currentOctaveOffset = juce::jmin(3, currentOctaveOffset + 1);
            updateKeyboardRange();
        };

        octaveLabel.setJustificationType(juce::Justification::centred);
        octaveLabel.setText("OCT", juce::dontSendNotification);
        octaveLabel.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        octaveLabel.setColour(juce::Label::textColourId, palette::ledTextOff);

        applyGreenActionButtonStyle(keysToggleButton, "keysToggleButton");
        keysToggleButton.setTooltip("KEYS / LED\nToggle between the LED meter view and the on-screen keyboard.");
        applyGreenActionButtonStyle(octaveDownButton);
        octaveDownButton.setTooltip("OCTAVE DOWN\nShift the visible keyboard / LED range down by one octave.");
        applyGreenActionButtonStyle(octaveUpButton);
        octaveUpButton.setTooltip("OCTAVE UP\nShift the visible keyboard / LED range up by one octave.");

        addAndMakeVisible(keysToggleButton);
        addAndMakeVisible(octaveDownButton);
        addAndMakeVisible(octaveUpButton);
        addAndMakeVisible(octaveLabel);

        updateKeyboardRange();
        applyViewMode();
    }

    PianoBarComponent::~PianoBarComponent()
    {
        keyboardState.removeListener(this);
    }

    int PianoBarComponent::getDesiredWidth() const noexcept
    {
        const auto kbWidth = static_cast<int>(std::ceil(keyboard.getTotalKeyboardWidth()));
        return leftColumnWidth + columnGap + kbWidth;
    }

    void PianoBarComponent::setViewMode(ViewMode newMode)
    {
        if (newMode == viewMode)
            return;

        viewMode = newMode;
        applyViewMode();
        repaint();
    }

    void PianoBarComponent::applyViewMode()
    {
        const bool keysVisible = (viewMode == ViewMode::keys);
        keyboard.setVisible(keysVisible);
        // Keep the toggle's latched state in sync with the actual mode in case it was set programmatically.
        if (keysToggleButton.getToggleState() != keysVisible)
            keysToggleButton.setToggleState(keysVisible, juce::dontSendNotification);
    }

    void PianoBarComponent::updateKeyboardRange()
    {
        const int startNote = baseStartNote + (currentOctaveOffset * 12);
        keyboard.setAvailableRange(startNote, startNote + numKeys - 1);
        repaint();
    }

    juce::Rectangle<int> PianoBarComponent::getCentreArea() const noexcept
    {
        auto bounds = getLocalBounds();
        bounds.removeFromLeft(leftColumnWidth + columnGap);
        return bounds;
    }

    void PianoBarComponent::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(palette::panelRaised);
        g.fillRoundedRectangle(bounds, 5.0f);

        if (viewMode != ViewMode::ledRow)
            return;

        // Draw the LED-dot view inside the same rectangle the keyboard would occupy.
        const auto centre = getCentreArea().toFloat();

        const int numWhiteKeys = 29; // C to C across 4 octaves = 29 white keys in 49 notes
        const float ledSize = 6.0f;
        const float whiteSpacing = (centre.getWidth() - (numWhiteKeys * ledSize)) / (numWhiteKeys + 1);

        const int startNote = baseStartNote + (currentOctaveOffset * 12);
        int whiteKeyIndex = 0;
        for (int i = 0; i < numKeys; ++i)
        {
            int note = startNote + i;
            if (note < 0 || note > 127) continue;

            const bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);

            float x = 0.0f;
            if (! isBlackKey)
            {
                x = centre.getX() + whiteSpacing + whiteKeyIndex * (ledSize + whiteSpacing);
                ++whiteKeyIndex;
            }
            else
            {
                x = centre.getX() + whiteSpacing + (whiteKeyIndex - 1) * (ledSize + whiteSpacing) + (ledSize + whiteSpacing) / 2.0f;
            }

            const float y = centre.getY() + (centre.getHeight() - ledSize) / 2.0f;

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

    void PianoBarComponent::resized()
    {
        auto bounds = getLocalBounds();

        auto leftColumn = bounds.removeFromLeft(leftColumnWidth);
        bounds.removeFromLeft(columnGap);
        // What remains in `bounds` is the centre area shared by the LED bar and the keyboard.

        leftColumn.reduce(2, 4);

        keysToggleButton.setBounds(leftColumn.removeFromTop(22));
        leftColumn.removeFromTop(4);
        octaveLabel.setBounds(leftColumn.removeFromTop(12));
        leftColumn.removeFromTop(2);
        octaveUpButton.setBounds(leftColumn.removeFromTop(16));
        leftColumn.removeFromTop(2);
        octaveDownButton.setBounds(leftColumn.removeFromTop(16));

        // The keyboard always occupies the centre area at its natural width; if the parent
        // shrinks us below that, the keyboard fills the available space instead.
        keyboard.setBounds(bounds);
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
}
