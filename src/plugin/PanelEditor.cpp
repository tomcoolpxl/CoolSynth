#include "PanelEditor.h"

#include "SynthAudioProcessor.h"
#include "interface/InterfaceModel.h"
#include "midi/ControllerSetup.h"
#include "presets/MySounds.h"
#include "synth/v3/AudioWorkers.h"
#include "BuildInfo.h"
#include <BinaryData.h>

namespace
{
    using coolsynth::ui::InterfaceModel;
    using coolsynth::ui::Tint;

    // The dark panel of the chosen mock-up (D143).
    namespace colours
    {
        const juce::Colour background { 0xff131619 };
        const juce::Colour panel { 0xff1e2226 };
        const juce::Colour panelEdge { 0xff2f353b };
        const juce::Colour knob { 0xff2b3036 };
        const juce::Colour knobEdge { 0xff3e454d };
        const juce::Colour ink { 0xffe4e8ec };
        const juce::Colour muted { 0xff9aa3ac };
        const juce::Colour print { 0xff8d959e };
        const juce::Colour screen { 0xff0b0d10 };
        const juce::Colour screenInk { 0xffe6ebf0 };
        const juce::Colour screenDim { 0xff79828c };
        const juce::Colour screenLine { 0xff262b31 };
        const juce::Colour padOff { 0xff1a1d21 };
        const juce::Colour link { 0xff7fa8ff };
        const juce::Colour mainKnob { 0xff1c1f23 };
        const juce::Colour bezel { 0xff272b30 }; // round the screens' glass, a shade lighter than the panel (D226)
    }

    juce::Font sans(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font mono(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Consolas", size, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::String emDash = juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));

    /** Text that fits a width: as it is, or its start and its end with the middle left out (D225), as a file's own name
        is mostly at its end and "CST2/1 kick" must not read as "CST2/1 k...". */
    juce::String fitMiddle(const juce::String& text, const juce::Font& font, int width)
    {
        if (juce::GlyphArrangement::getStringWidthInt(font, text) <= width)
            return text;
        const juce::String dots(juce::CharPointer_UTF8("\xe2\x80\xa6"));
        for (int keep = text.length() - 1; keep > 1; --keep)
        {
            const auto shorter = text.substring(0, (keep + 1) / 2) + dots + text.substring(text.length() - keep / 2);
            if (juce::GlyphArrangement::getStringWidthInt(font, shorter) <= width)
                return shorter;
        }
        return dots;
    }

    /** A mouse wheel's steps (D193): one notch is one step. JUCE gives a Windows notch as 0.234375 (120 / 2 / 256),
        which times the old 8.5 made 1.99 and so moved one row, then two. A high-resolution wheel's part-notches add
        up; a smooth wheel (a trackpad) scrolls as before. `sum` keeps what has not made a whole step yet. */
    int wheelSteps(const juce::MouseWheelDetails& wheel, float& sum)
    {
        if (wheel.isSmooth)
            sum += wheel.deltaY * 8.5f;
        else
        {
           #if JUCE_WINDOWS
            sum += wheel.deltaY / 0.234375f;
           #else
            sum += wheel.deltaY > 0.0f ? 1.0f : (wheel.deltaY < 0.0f ? -1.0f : 0.0f);
           #endif
        }
        const int steps = static_cast<int>(sum);
        sum -= static_cast<float>(steps);
        return steps;
    }
    const juce::String mainMark = juce::String(juce::CharPointer_UTF8("\xe2\x97\x8e"));

    /** An effect's type in four letters at most, for the layer screen's small blocks (D226). */
    juce::String shortType(const juce::String& type)
    {
        static const char* const names[][2] { { "Chorus", "Chor" }, { "Compressor", "Comp" }, { "Delay", "Dly" }, { "Drive", "Drv" },
                                              { "Flanger", "Flng" }, { "Grain", "Grn" }, { "Phaser", "Phsr" }, { "Pitch", "Ptch" },
                                              { "Reverb", "Rvb" }, { "Stutter", "Stut" } };
        for (const auto& name : names)
            if (type == name[0])
                return name[1];
        return type.substring(0, 4); // EQ, LoFi, Pump, Room as they are
    }
}

// ---- Hover (D204): what the mouse is over lights softly and the screen's hint line says what it does ------

namespace
{
    /** Tells when the mouse comes onto a component and leaves it, for components that have no hover of their own. */
    struct HoverWatcher final : juce::MouseListener
    {
        std::function<void(bool)> onHover;
        void mouseEnter(const juce::MouseEvent&) override { if (onHover) onHover(true); }
        void mouseExit(const juce::MouseEvent&) override { if (onHover) onHover(false); }
    };

    /** A fader that a right click does not move: it opens the controller's Learn instead (D211). */
    class PanelFader final : public juce::Slider
    {
    public:
        std::function<void()> onMenu;

        void mouseDown(const juce::MouseEvent& e) override
        {
            menuClick = e.mods.isPopupMenu();
            if (! menuClick)
                juce::Slider::mouseDown(e);
            else if (onMenu)
                onMenu();
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (! menuClick)
                juce::Slider::mouseDrag(e);
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            if (! menuClick)
                juce::Slider::mouseUp(e);
            menuClick = false;
        }

    private:
        bool menuClick = false;
    };
}

// ---- A knob that turns in detents --------------------------------------------------------------------

class PanelEditor::Knob final : public juce::Component
{
public:
    std::function<void(int)> onSteps;
    std::function<void()> onPress;
    std::function<void(bool)> onHover; // the mouse came onto it or left it (D204)
    bool hovered = false;

    void mouseEnter(const juce::MouseEvent&) override
    {
        hovered = true;
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        if (onHover)
            onHover(true);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = false;
        if (onHover)
            onHover(false);
        repaint();
    }
    float value = 0.0f;
    bool showsValue = false; // the pointer shows the value; otherwise it spins with the turns
    juce::Colour ring;       // transparent: the plain edge
    bool navigation = false; // knob 5's blue ring
    bool isMain = false;

    std::function<void()> onMenu; // a right click: the controller's Learn (D211)

    void mouseDown(const juce::MouseEvent& e) override
    {
        menuClick = e.mods.isPopupMenu();
        if (menuClick)
        {
            if (onMenu)
                onMenu();
            return;
        }
        lastY = e.position.y;
        moved = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (menuClick)
            return;
        const int steps = static_cast<int>((lastY - e.position.y) / 8.0f);
        if (steps != 0)
        {
            lastY -= static_cast<float>(steps) * 8.0f;
            moved = true;
            turn(steps);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (! moved && ! menuClick && onPress)
            onPress();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (const int steps = wheelSteps(wheel, wheelSum); steps != 0)
            turn(steps);
    }

    bool menuClick = false;

    /** Turns the pointer by `steps` detents without acting: the main knob and knob 5 follow the model's count of
        their turns, whatever turned them (D193). */
    void spinBy(int steps)
    {
        spin += static_cast<float>(steps) * 0.42f;
        repaint();
    }

    bool spinsItself = true; // false: spinBy() turns it, from the model's count

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto circle = juce::Rectangle<float>(size, size).withCentre(bounds.getCentre());
        if (navigation) // knob 5, the pages: a bright white ring, unlike every section's colour (D202)
        {
            g.setColour(juce::Colour(0xfff2f5f8));
            g.drawEllipse(circle.expanded(2.0f), 3.0f);
        }
        if (hovered) // a soft halo while the mouse is on it (D204)
        {
            const auto glow = ! ring.isTransparent() ? ring.withAlpha(1.0f) : (isMain ? juce::Colour(0xff8a939c) : colours::ink);
            g.setColour(glow.withAlpha(0.18f));
            g.drawEllipse(circle.expanded(3.0f), 3.0f);
        }
        g.setColour(isMain ? colours::mainKnob : colours::knob);
        g.fillEllipse(circle);
        const bool coloured = ! ring.isTransparent();
        g.setColour(coloured ? ring : (isMain ? juce::Colour(0xff30353b) : colours::knobEdge));
        g.drawEllipse(circle.reduced(coloured ? 1.5f : 0.5f), coloured ? 3.0f : 1.0f);

        const float angle = showsValue ? juce::degreesToRadians(-135.0f + 270.0f * juce::jlimit(0.0f, 1.0f, value)) : spin;
        const auto centre = circle.getCentre();
        const float r = size * 0.5f;
        juce::Path pointer;
        pointer.addRoundedRectangle(-1.5f, -r + 5.0f, 3.0f, r * 0.42f, 1.5f);
        g.setColour(isMain ? juce::Colour(0xff8a939c) : colours::ink);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre));

        // While it turns, its value in the middle on a disc of the knob's own grey, fading out once it stops (D183).
        // A number gets a smaller disc, so more of the pointer shows as it goes round; a name keeps the larger one.
        if (flashAlpha > 0.0f && flash.isNotEmpty())
        {
            const bool number = flash.containsAnyOf("0123456789");
            const auto inner = circle.reduced(size * (number ? 0.21f : 0.13f));
            g.setColour((isMain ? colours::mainKnob : colours::knob).withAlpha(flashAlpha));
            g.fillEllipse(inner);
            g.setColour(colours::ink.withAlpha(flashAlpha));
            g.setFont(mono(10.5f, true));
            const auto textArea = circle.reduced(size * 0.13f); // the same text size whatever the disc
            g.drawFittedText(flash, textArea.reduced(3.0f, textArea.getHeight() * 0.18f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        }
    }

    /** Shows `text` in the middle now (a turn); `nowMs` is when. */
    void showValue(const juce::String& text, juce::uint32 nowMs)
    {
        flash = text;
        flashAtMs = nowMs;
        flashAlpha = 1.0f;
        repaint();
    }

    /** The value stays while the knob turns and a moment after, then fades (called 30 times a second). */
    void ageValue(juce::uint32 nowMs)
    {
        if (flashAlpha <= 0.0f)
            return;
        const auto since = static_cast<float>(nowMs - flashAtMs);
        const float alpha = since < holdMs ? 1.0f : juce::jmax(0.0f, 1.0f - (since - holdMs) / fadeMs);
        if (alpha != flashAlpha)
        {
            flashAlpha = alpha;
            repaint();
        }
    }

    static constexpr float holdMs = 700.0f, fadeMs = 400.0f;

private:
    void turn(int steps)
    {
        if (spinsItself)
            spin += static_cast<float>(steps) * 0.42f;
        if (onSteps)
            onSteps(steps);
        repaint();
    }

    float lastY = 0.0f, wheelSum = 0.0f, spin = 0.0f;
    bool moved = false;
    juce::String flash;      // the value shown in the middle while turning
    juce::uint32 flashAtMs = 0;
    float flashAlpha = 0.0f;
};

// ---- The keys: flat black keys, half a white key wide, no note names ------------------------------------

namespace
{
    class FlatKeyboard final : public juce::MidiKeyboardComponent
    {
    public:
        using juce::MidiKeyboardComponent::MidiKeyboardComponent;

        juce::String getWhiteNoteText(int) override { return {}; }

        // The gap between white keys: 2 px (JUCE draws 1), in the separator colour (D191).
        void drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                           juce::Colour lineColour, juce::Colour textColour) override
        {
            juce::MidiKeyboardComponent::drawWhiteNote(note, g, area, isDown, isOver, juce::Colours::transparentBlack, textColour);
            g.setColour(lineColour);
            g.fillRect(area.withWidth(2.0f));
            if (note == getRangeEnd())
                g.fillRect(area.withTrimmedLeft(area.getWidth() - 2.0f));
        }

        void drawBlackNote(int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                           juce::Colour noteFillColour) override
        {
            auto c = noteFillColour;
            if (isDown)
                c = c.overlaidWith(findColour(keyDownOverlayColourId));
            else if (isOver)
                c = c.overlaidWith(findColour(mouseOverKeyOverlayColourId));
            g.setColour(c);
            g.fillRoundedRectangle(area.withTrimmedTop(-3.0f), 3.0f);
        }
    };
}

// ---- A pad --------------------------------------------------------------------------------------------

class PanelEditor::Pad final : public juce::Component
{
public:
    std::function<void()> onPress;
    juce::Colour colour { colours::print };
    juce::String face;
    juce::String sub; // small, inside at the bottom: what pressing it again does (D191)
    int symbol = -1; // 0 stop, 1 play, 2 record
    bool lit = false;
    float glow = -1.0f; // 0..1 while Home glows for a loading instrument (D156); below 0, no glow
    std::function<void(bool)> onHover; // (D204)
    bool hovered = false;

    void mouseEnter(const juce::MouseEvent&) override
    {
        hovered = true;
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        if (onHover)
            onHover(true);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = false;
        if (onHover)
            onHover(false);
        repaint();
    }

    std::function<void()> onMenu; // a right click: the controller's Learn (D211)

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (onPress)
            onPress();
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(2.0f);
        if (glow >= 0.0f)
        {
            // a soft halo that swells and fades, in rings so its edge blurs
            const auto base = colour.withMultipliedBrightness(1.0f / juce::jmax(0.1f, colour.getBrightness()));
            for (int ring = 3; ring >= 0; --ring)
            {
                g.setColour(base.withAlpha((0.10f + 0.22f * glow) * (1.0f - 0.2f * static_cast<float>(ring))));
                g.fillRoundedRectangle(r.expanded(1.0f + static_cast<float>(ring) * 1.5f * (0.5f + glow)), 11.0f + static_cast<float>(ring));
            }
        }
        else if (lit)
        {
            g.setColour(colour.withAlpha(hovered ? 0.5f : 0.35f));
            g.fillRoundedRectangle(r.expanded(2.0f), 11.0f);
        }
        else if (hovered) // a soft glow while the mouse is on it (D204)
        {
            g.setColour(colour.withAlpha(0.2f));
            g.fillRoundedRectangle(r.expanded(2.0f), 11.0f);
        }
        g.setColour(colours::padOff);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(colour.interpolatedWith(juce::Colour(0xff2a2e33), lit ? 0.0f : 0.25f));
        g.drawRoundedRectangle(r.reduced(1.5f), 9.0f, 3.0f);

        const auto ink = colour.withMultipliedBrightness(lit ? 1.0f : 2.2f).interpolatedWith(juce::Colours::white, 0.25f);
        g.setColour(ink);
        if (symbol < 0)
        {
            // The name stays in the pad's centre, as on every pad; the small line sits below it, at the bottom.
            if (sub.isNotEmpty())
            {
                g.setFont(sans(10.5f));
                g.setColour(ink.withAlpha(0.7f));
                g.drawText(sub, r.withTrimmedTop(r.getHeight() - 22.0f).withTrimmedBottom(4.0f), juce::Justification::centred);
                g.setColour(ink);
            }
            g.setFont(sans(16.0f, true)); // larger, to read from a distance (D185; was 12.5)
            g.drawText(face, r, juce::Justification::centred);
            return;
        }
        const float s = 12.0f;
        const auto box = juce::Rectangle<float>(s, s).withCentre(r.getCentre());
        if (symbol == 0)
            g.fillRect(box);
        else if (symbol == 1)
        {
            juce::Path p;
            p.addTriangle(box.getX(), box.getY(), box.getRight(), box.getCentreY(), box.getX(), box.getBottom());
            g.fillPath(p);
        }
        else
            g.fillEllipse(box);
    }
};

// ---- The screen ---------------------------------------------------------------------------------------

class PanelEditor::Screen final : public juce::Component
{
public:
    explicit Screen(InterfaceModel& m) : model(m) {}

    std::function<void()> onChanged;   // after a click changed the model
    std::function<bool()> fine;        // Shift held: cards turn ten times finer

    // The mouse: rows, slots and page tabs are clicked; the four cards are dragged like their knobs.
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        // A right click goes back one level, as the ‹ Back row does (D181).
        if (e.mods.isPopupMenu())
        {
            model.back();
            if (onChanged)
                onChanged();
            return;
        }
        for (int k = 0; k < 4; ++k)
            if (cardAreas[static_cast<std::size_t>(k)].contains(p))
            {
                dragKnob = k + 1;
                dragY = e.position.y;
                dragMoved = false;
                return;
            }
        for (const auto& hit : hits)
            if (hit.area.contains(p))
            {
                hit.action();
                if (onChanged)
                    onChanged();
                return;
            }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragKnob < 0)
            return;
        const int steps = static_cast<int>((dragY - e.position.y) / 6.0f);
        if (steps != 0)
        {
            dragY -= static_cast<float>(steps) * 6.0f;
            dragMoved = true;
            model.turnKnob(dragKnob, steps, fine && fine());
            if (onChanged)
                onChanged();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragKnob > 0 && ! dragMoved)
        {
            model.pressKnob(dragKnob); // a click on a card loads the instrument it shows (D156)
            if (onChanged)
                onChanged();
        }
        dragKnob = -1;
    }

    // The wheel turns what it is over (D181): a card its knob, the page tabs knob 5, anything else the main knob,
    // so a long list scrolls, its ‹ Back row included.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int steps = wheelSteps(wheel, wheelSum);
        if (steps == 0)
            return;
        const auto p = e.getPosition();
        bool done = false;
        for (int k = 0; k < 4 && ! done; ++k)
            if (cardAreas[static_cast<std::size_t>(k)].contains(p))
            {
                model.turnKnob(k + 1, steps, fine && fine());
                done = true;
            }
        if (! done && tabsArea.contains(p))
        {
            model.turnPageKnob(steps > 0 ? -1 : 1);
            done = true;
        }
        if (! done)
            model.turnMain(-steps); // the wheel down goes down the list
        if (onChanged)
            onChanged();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        // What the mouse is over: the hand cursor, a soft highlight and, in the hint line, what it does (D204).
        mouse = e.getPosition();
        mouseInside = true;
        juce::String text;
        for (int k = 0; k < 4 && text.isEmpty(); ++k)
            if (cardAreas[static_cast<std::size_t>(k)].contains(mouse))
                text = "Knob " + juce::String(k + 1) + ": " + model.knob(k + 1).name + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 drag or scroll"));
        for (const auto& hit : hits)
            if (text.isEmpty() && hit.area.contains(mouse))
                text = hit.hint.isNotEmpty() ? hit.hint : juce::String("Click");
        setMouseCursor(text.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        hoverText = text;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        mouseInside = false;
        hoverText.clear();
        repaint();
    }

    juce::String externalHint; // what the panel's hovered knob, pad, fader or button does (D204)
    juce::String notice;       // the controller's word (D211): learning, a control learned, a controller found

    void paint(juce::Graphics& g) override
    {
        hits.clear();
        tabsArea = {}; // set again below when there are tabs
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(colours::screen);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(colours::screenLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        const bool typeList = model.typeListOpen();
        const bool home = model.view() == InterfaceModel::View::home && ! typeList;
        const auto list = model.list();
        // Home in the colour of the layer it is on (D194: Home picks that layer's sound), a section in its own
        const auto tint = coolsynth::ui::tintColour(home ? (model.part() == 0 ? Tint::home : Tint::partB) : InterfaceModel::sectionTint(model.section()));
        auto area = getLocalBounds().reduced(9, 7);

        // header: a chip in the section's colour, the part on the right
        auto header = area.removeFromTop(20);
        const auto chipText = home || typeList ? list.title : InterfaceModel::sectionName(model.section()).toUpperCase();
        g.setFont(mono(11.0f, true));
        const int chipWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), chipText) + 14;
        g.setColour(tint.withMultipliedBrightness(0.85f));
        const auto chip = header.removeFromLeft(chipWidth);
        g.fillRoundedRectangle(chip.toFloat(), 4.0f);
        g.setColour(colours::screen);
        g.drawText(chipText, header.withX(area.getX()).withWidth(chipWidth), juce::Justification::centred);
        g.setColour(colours::screenDim);
        g.setFont(mono(11.0f));
        const auto partText = "Layer " + juce::String(model.part() == 0 ? "A" : "B");
        g.drawText(partText, header, juce::Justification::centredRight);
        // The chip goes back one level; "Layer A" switches the layer, as Home pressed again does (D181, D185, D189).
        hits.push_back({ chip, [this] { model.back(); }, "Back one level" + clickWord() });
        const int partWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), partText) + 6;
        hits.push_back({ header.withTrimmedLeft(header.getWidth() - partWidth), [this] { model.togglePart(); },
                         "Switch to layer " + juce::String(model.part() == 0 ? "B" : "A") + clickWord() });

        // trail: where you are, with what each knob moves
        area.removeFromTop(4);
        auto trail = area.removeFromTop(16);
        float x = static_cast<float>(trail.getX());
        const auto trailItems = model.trail();
        for (std::size_t index = 0; index < trailItems.size(); ++index)
        {
            const auto& item = trailItems[index];
            const float itemStart = x;
            if (x > static_cast<float>(trail.getX()))
            {
                g.setColour(colours::screenLine.brighter(0.4f));
                g.drawText(">", juce::Rectangle<float>(x, static_cast<float>(trail.getY()), 12.0f, 16.0f), juce::Justification::centred);
                x += 12.0f;
            }
            if (item.mark.isNotEmpty())
            {
                const auto markBox = juce::Rectangle<float>(x, static_cast<float>(trail.getY()) + 2.0f, 16.0f, 12.0f);
                g.setColour(item.current ? colours::screenInk : colours::screenDim);
                g.drawRoundedRectangle(markBox, 6.0f, 1.0f);
                g.setFont(mono(9.0f));
                g.drawText(item.mark, markBox, juce::Justification::centred);
                x += 19.0f;
            }
            g.setFont(mono(10.5f, item.current));
            g.setColour(item.current ? colours::screenInk : colours::screenDim);
            const float w = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), item.text));
            g.drawText(item.text, juce::Rectangle<float>(x, static_cast<float>(trail.getY()), w + 2.0f, 16.0f), juce::Justification::centredLeft);
            x += w + 4.0f;
            // Each item is clickable: it goes where it names (D181).
            hits.push_back({ juce::Rectangle<float>(itemStart, static_cast<float>(trail.getY()), x - itemStart, 16.0f).toNearestInt(),
                             [this, index] { model.clickTrail(static_cast<int>(index)); }, "Go to " + item.text + clickWord() });
        }
        area.removeFromTop(3);
        g.setColour(colours::screenLine);
        g.fillRect(area.removeFromTop(1).expanded(9, 0));

        // the four cards of knobs 1-4, in the page's colour
        const auto pageTint = coolsynth::ui::tintColour(model.pageTint());
        auto cards = area.removeFromBottom(58);
        g.setColour(colours::screenLine);
        g.fillRect(cards.removeFromTop(1).expanded(9, 0));
        // An empty layer's macros: the cards say so instead of naming knobs that do nothing (D192).
        const bool emptyMacros = home && model.livePage() == 0 && model.currentLayerEmpty();
        if (emptyMacros)
        {
            g.setFont(mono(11.0f));
            g.setColour(colours::screenDim);
            g.drawText(model.currentPage().name, cards, juce::Justification::centred);
            for (auto& cardArea : cardAreas)
                cardArea = {};
        }
        cards.removeFromTop(6);
        const int cardWidth = cards.getWidth() / 4;
        for (int k = 1; k <= 4 && ! emptyMacros; ++k)
        {
            const auto view = model.knob(k);
            auto card = cards.removeFromLeft(cardWidth).reduced(3, 0);
            cardAreas[static_cast<std::size_t>(k - 1)] = view.active ? card : juce::Rectangle<int>();
            g.setFont(mono(9.0f));
            // a setting that does nothing now: dimmed, its role line saying when it acts (D201)
            const auto accent = view.idle ? colours::screenDim.darker(0.3f) : pageTint;
            g.setColour(view.pending ? pageTint : colours::screenDim);
            g.drawText(view.pending ? juce::String("CLICK TO LOAD") : (view.idle ? view.why.toUpperCase() : view.role.toUpperCase()),
                       card.removeFromTop(12), juce::Justification::centredLeft);
            g.setFont(mono(11.0f));
            g.setColour(view.active && ! view.idle ? colours::screenInk : (view.active ? colours::screenDim : colours::screenLine.brighter(0.3f)));
            g.drawText(fitMiddle(view.name, g.getCurrentFont(), card.getWidth()), card.removeFromTop(15), juce::Justification::centredLeft);
            g.setColour(view.active ? accent : colours::screenLine);
            g.drawText(view.active ? fitMiddle(view.value, g.getCurrentFont(), card.getWidth()) : juce::String(), card.removeFromTop(15),
                       juce::Justification::centredLeft);
            auto bar = card.removeFromTop(3).toFloat();
            g.setColour(colours::screenLine);
            g.fillRoundedRectangle(bar, 1.5f);
            if (view.active)
            {
                g.setColour(accent);
                g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, view.normalised)), 1.5f);
            }
        }

        // the page tabs, clickable: Home's live pages, or the focused slot's pages in a section
        if (! typeList)
        {
            juce::StringArray names;
            int current = 0;
            if (home)
            {
                names = model.livePageNames();
                current = model.livePage();
            }
            else
            {
                const auto slot = model.currentSlot();
                for (const auto& page : slot.pages)
                    names.add(page.name);
                current = model.pageIndexOf(model.section(), model.slotIndex(model.section()));
            }
            auto tabs = area.removeFromBottom(22);
            tabsArea = tabs;
            float tx = static_cast<float>(tabs.getX());
            g.setFont(mono(10.5f));
            for (int i = 0; i < names.size(); ++i)
            {
                const float w = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), names[i])) + 12.0f;
                const auto tab = juce::Rectangle<float>(tx, static_cast<float>(tabs.getY()) + 2.0f, w, 17.0f);
                const bool on = i == current;
                if (on)
                {
                    g.setColour(pageTint.withAlpha(0.22f));
                    g.fillRoundedRectangle(tab, 3.0f);
                }
                // the page knob 5 is on: outlined in its white (D202)
                g.setColour(on ? juce::Colour(0xfff2f5f8) : colours::screenLine.brighter(0.2f));
                g.drawRoundedRectangle(tab, 3.0f, on ? 1.3f : 1.0f);
                g.setColour(on ? colours::screenInk : colours::screenDim);
                g.drawText(names[i], tab, juce::Justification::centred);
                hits.push_back({ tab.toNearestInt(), [this, i] { model.clickPage(i); }, "Page " + names[i] + clickWord() });
                tx += w + 4.0f;
            }
        }

        // the hint line: what the mouse is over (D204), else the model's hint; its row is always kept, so nothing moves
        {
            const bool hovering = hoverText.isNotEmpty() || externalHint.isNotEmpty();
            const auto hint = notice.isNotEmpty() ? notice
                            : (hoverText.isNotEmpty() ? hoverText : (externalHint.isNotEmpty() ? externalHint : model.hint()));
            g.setFont(mono(10.0f));
            g.setColour(notice.isNotEmpty() ? coolsynth::ui::tintColour(Tint::home)
                                            : (hovering ? colours::screenInk.withAlpha(0.85f) : colours::screenDim));
            g.drawFittedText(hint, area.removeFromBottom(16), juce::Justification::centredLeft, 1, 0.8f);
        }
        area.removeFromTop(6);

        if (home || typeList)
            paintList(g, area, list, home ? tint : tint);
        else
            paintSection(g, area, tint);

        // the clickable part the mouse is over, softly lit (D204)
        if (mouseInside)
        {
            auto light = [&g](juce::Rectangle<int> r)
            {
                g.setColour(colours::screenInk.withAlpha(0.06f));
                g.fillRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f);
                g.setColour(colours::screenInk.withAlpha(0.22f));
                g.drawRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f, 1.0f);
            };
            bool lit = false;
            for (const auto& card : cardAreas)
                if (! lit && card.contains(mouse))
                {
                    light(card);
                    lit = true;
                }
            for (const auto& hit : hits)
                if (! lit && hit.area.contains(mouse))
                {
                    light(hit.area);
                    lit = true;
                }
        }
    }

private:
    void paintList(juce::Graphics& g, juce::Rectangle<int> area, const coolsynth::ui::ListView& list, juce::Colour tint)
    {
        const int rowHeight = 19;
        const int visible = juce::jmax(1, area.getHeight() / rowHeight);
        const int first = juce::jlimit(0, juce::jmax(0, list.rows.size() - visible), list.cursor - visible / 2);
        const auto whole = area.withHeight(juce::jmin(area.getHeight(), visible * rowHeight));
        g.setFont(mono(12.0f));
        for (int i = first; i < juce::jmin(list.rows.size(), first + visible); ++i)
        {
            auto row = area.removeFromTop(rowHeight);
            hits.push_back({ row, [this, i] { model.clickListRow(i); }, list.rows[i] + clickWord() });
            const bool on = i == list.cursor;
            if (on)
            {
                g.setColour(tint.withAlpha(0.28f));
                g.fillRoundedRectangle(row.toFloat(), 3.0f);
            }
            auto text = row.reduced(6, 0);
            // What makes the sound, right-aligned: small transparent pills with rounded corners (D169), here only,
            // not on the MiniLab's own screen.
            if (static_cast<std::size_t>(i) < list.pills.size() && ! list.pills[static_cast<std::size_t>(i)].isEmpty())
            {
                const auto& pills = list.pills[static_cast<std::size_t>(i)];
                const auto pillFont = mono(9.0f);
                g.setFont(pillFont);
                for (int p = pills.size() - 1; p >= 0; --p)
                {
                    const int width = juce::GlyphArrangement::getStringWidthInt(pillFont, pills[p]) + 10;
                    auto pill = text.removeFromRight(width).toFloat().reduced(0.0f, 3.5f);
                    text.removeFromRight(3);
                    g.setColour(tint.withAlpha(on ? 0.16f : 0.08f));
                    g.fillRoundedRectangle(pill, pill.getHeight() / 2.0f);
                    g.setColour((on ? colours::screenInk : colours::screenDim).withAlpha(0.55f));
                    g.drawRoundedRectangle(pill, pill.getHeight() / 2.0f, 0.8f);
                    g.setColour(on ? colours::screenInk : colours::screenDim);
                    g.drawText(pills[p], pill, juce::Justification::centred);
                }
                text.removeFromRight(4);
                g.setFont(mono(12.0f));
            }
            if (on)
            {
                g.setColour(colours::screenInk);
                g.drawRoundedRectangle(text.removeFromLeft(18).toFloat().reduced(0.0f, 3.0f), 6.0f, 1.0f);
                g.setFont(mono(9.0f));
                g.drawText(mainMark, text.withX(text.getX() - 18).withWidth(18), juce::Justification::centred);
                text.removeFromLeft(4);
                g.setFont(mono(12.0f));
            }
            g.setColour(on ? colours::screenInk : colours::screenDim);
            juce::String label = list.rows[i];
            if (list.firstRowIsBack && i == 0)
                g.setFont(mono(12.0f).italicised());
            g.drawText(label, text, juce::Justification::centredLeft);
            g.setFont(mono(12.0f));
            if (i == list.used)
            {
                g.setColour(coolsynth::ui::tintColour(Tint::play));
                const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label);
                g.drawText(list.title.startsWith("TYPE") || list.title == "CATEGORIES" ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f"))
                                                                                       : juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f playing")),
                           text.withTrimmedLeft(w + 8), juce::Justification::centredLeft);
            }
        }

        // More rows above or below: that edge fades out (D204), the common sign that a list scrolls.
        const auto fade = [&g, &whole](bool top)
        {
            const float depth = 22.0f;
            const auto band = top ? whole.toFloat().withHeight(depth) : whole.toFloat().withTrimmedTop(static_cast<float>(whole.getHeight()) - depth);
            g.setGradientFill(juce::ColourGradient(colours::screen, band.getX(), top ? band.getY() : band.getBottom(),
                                                   colours::screen.withAlpha(0.0f), band.getX(), top ? band.getBottom() : band.getY(), false));
            g.fillRect(band);
        };
        if (first > 0)
            fade(true);
        if (first + visible < list.rows.size())
            fade(false);
    }

    void paintSection(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour tint)
    {
        const auto slots = model.slotsOf(model.section());
        const int focused = model.slotIndex(model.section());
        const auto rows = InterfaceModel::rowsOf(model.section());
        const bool fourRows = rows.size() > 3; // the Effects section: six layer slots and six at the end (D194)
        for (const auto& row : rows)
        {
            auto line = area.removeFromTop(fourRows ? 32 : 36);
            area.removeFromTop(fourRows ? 4 : 6);
            g.setFont(mono(9.5f));
            g.setColour(colours::screenDim);
            g.drawText(row.label.toUpperCase(), line.removeFromLeft(54), juce::Justification::centredLeft);
            for (std::size_t j = 0; j < row.slots.size(); ++j)
            {
                if (j > 0 && row.joiner.isNotEmpty())
                {
                    g.setColour(colours::screenDim);
                    g.setFont(mono(11.0f));
                    g.drawText(row.joiner, line.removeFromLeft(14), juce::Justification::centred);
                }
                const int index = row.slots[j];
                if (index >= static_cast<int>(slots.size()))
                    continue;
                const auto& slot = slots[static_cast<std::size_t>(index)];
                const bool on = index == focused;
                auto box = line.removeFromLeft(58).reduced(0, 2).toFloat();
                hits.push_back({ box.toNearestInt(), [this, index] { model.clickSlot(index); },
                                 slot.code + " " + (slot.empty ? juce::String("empty") : slot.typeName) + clickWord() });
                if (on)
                {
                    g.setColour(tint.withAlpha(0.3f));
                    g.fillRoundedRectangle(box, 4.0f);
                }
                g.setColour(on ? colours::screenInk : colours::screenLine.brighter(0.25f));
                if (slot.empty)
                {
                    const float dashes[] { 3.0f, 2.0f };
                    juce::Path p;
                    p.addRoundedRectangle(box.reduced(0.5f), 4.0f);
                    juce::Path dashed;
                    juce::PathStrokeType(1.0f).createDashedStroke(dashed, p, dashes, 2);
                    g.fillPath(dashed);
                }
                else
                    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
                auto text = box.reduced(5.0f, 2.0f);
                g.setFont(mono(10.5f));
                g.setColour(slot.empty ? colours::screenDim : colours::screenInk);
                g.drawText(slot.code + " " + (slot.empty ? juce::String("-") : slot.typeName), text.removeFromTop(14.0f), juce::Justification::centredLeft);
                g.setFont(mono(9.0f));
                g.setColour(colours::screenDim);
                const auto page = on && ! slot.empty ? model.currentPage().name : (slot.empty ? juce::String("empty") : slot.label);
                g.drawText(page, text, juce::Justification::centredLeft);
                line.removeFromLeft(2);
            }
            if (row.after.isNotEmpty())
            {
                g.setColour(colours::screenDim);
                g.setFont(mono(10.0f));
                g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + row.after.fromFirstOccurrenceOf(" ", false, false),
                           line, juce::Justification::centredLeft);
            }
        }

        // what the two knobs are on
        const auto slot = model.currentSlot();
        auto info = area.removeFromBottom(18);
        g.setFont(mono(11.0f, true));
        g.setColour(colours::screenInk);
        g.drawText(mainMark + " " + slot.label + "  " + (slot.empty ? juce::String("empty") : slot.typeName)
                       + "    5 " + (slot.empty ? juce::String("press for a type") : model.currentPage().name),
                   info, juce::Justification::centredLeft);
    }

    struct Hit
    {
        juce::Rectangle<int> area;
        std::function<void()> action;
        juce::String hint; // what a click does, for the hint line (D204)
    };

    static juce::String clickWord() { return juce::String(juce::CharPointer_UTF8(" \xc2\xb7 click")); }

    juce::String hoverText;    // the screen's own hovered part (D204)
    juce::Point<int> mouse;
    bool mouseInside = false;

    InterfaceModel& model;
    std::vector<Hit> hits;
    std::array<juce::Rectangle<int>, 4> cardAreas;
    juce::Rectangle<int> tabsArea; // the page tabs: the wheel turns knob 5 there (D181)
    float wheelSum = 0.0f;
    int dragKnob = -1;
    float dragY = 0.0f;
    bool dragMoved = false;
};

// ---- The panel --------------------------------------------------------------------------------------

// ---- Prefs: a dialog card over the panel (D197) -------------------------------------------------------

namespace
{
    /** Prefs as a dialog in the panel's own style: what was a menu (D174) laid out in sections, each choice a row of
        buttons with a line saying what it does. Over the dimmed panel; x, Escape or a click beside it closes it. */
    class PrefsCard final : public juce::Component
    {
    public:
        explicit PrefsCard(SynthAudioProcessor& s) : synth(s)
        {
            setVisible(false);
            setWantsKeyboardFocus(true);
            auto style = [](juce::TextButton& b, bool toggle)
            {
                b.setColour(juce::TextButton::buttonColourId, colours::knob);
                b.setColour(juce::TextButton::buttonOnColourId, colours::ink);
                b.setColour(juce::TextButton::textColourOffId, colours::ink);
                b.setColour(juce::TextButton::textColourOnId, colours::background);
                b.setClickingTogglesState(false);
                if (toggle)
                    b.setConnectedEdges(0);
            };
            for (auto* b : { &close, &save, &audio, &controller })
                style(*b, false);
            controller.setButtonText(juce::String(juce::CharPointer_UTF8("Controller setup\xe2\x80\xa6")));
            controller.onClick = [this]
            {
                hide();
                if (onController)
                    onController();
            };
            close.setButtonText(juce::String(juce::CharPointer_UTF8("\xc3\x97")));
            close.onClick = [this] { hide(); };
            save.setButtonText(juce::String(juce::CharPointer_UTF8("Save sound\xe2\x80\xa6")));
            save.onClick = [this]
            {
                hide();
                if (auto* parent = getParentComponent())
                    PanelEditor::askToSaveSound(synth, *parent);
            };
            audio.setButtonText(juce::String(juce::CharPointer_UTF8("Audio and MIDI settings\xe2\x80\xa6")));
            audio.onClick = [this]
            {
                // the settings open in a window of their own; Preferences stays open behind them (D199)
                if (synth.showAudioSettings)
                    synth.showAudioSettings();
            };
            for (auto* b : { &reset, &resetYes, &resetNo })
                style(*b, false);
            reset.setButtonText(juce::String(juce::CharPointer_UTF8("Reset the sound\xe2\x80\xa6")));
            reset.onClick = [this] { confirming = true; layOutReset(); repaint(); };
            resetYes.setButtonText("Reset");
            resetYes.onClick = [this]
            {
                confirming = false;
                synth.resetSound();
                if (onReset)
                    onReset();
                hide();
            };
            resetNo.setButtonText("Cancel");
            resetNo.onClick = [this] { confirming = false; layOutReset(); repaint(); };
            const char* multiNames[] { "Auto", "Off" };
            for (int i = 0; i < 2; ++i)
            {
                auto& b = multi[static_cast<std::size_t>(i)];
                style(b, true);
                b.setButtonText(multiNames[i]);
                b.onClick = [this, i] { synth.chooseMultiCore(i == 0); refresh(); };
                b.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
            }
            // Claude's way into the app (D225): only the app sets the hooks, so the plugin never shows this row.
            const char* mcpNames[] { "Off", "On" };
            for (int i = 0; i < 2; ++i)
            {
                auto& b = mcp[static_cast<std::size_t>(i)];
                style(b, true);
                b.setButtonText(mcpNames[i]);
                b.onClick = [this, i]
                {
                    if (synth.chooseMcpServer)
                        synth.chooseMcpServer(i == 1);
                    refresh();
                };
                b.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
                addChildComponent(b);
            }
            const char* curveNames[] { "Soft", "Normal", "Hard" };
            for (int i = 0; i < 3; ++i)
            {
                auto& b = curve[static_cast<std::size_t>(i)];
                style(b, true);
                b.setButtonText(curveNames[i]);
                b.onClick = [this, i] { synth.chooseVelocityCurve(i); refresh(); };
                b.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight
                                           : (i == 2 ? juce::Button::ConnectedOnLeft : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight));
            }
            // The player's keyboard size, for the line over the screen's keys (D218): MIDI never says it.
            const auto& sizes = coolsynth::midi::ControllerSetup::keyboardSizes;
            for (std::size_t i = 0; i < keySize.size(); ++i)
            {
                auto& b = keySize[i];
                style(b, true);
                b.setButtonText(sizes[i] == 0 ? juce::String("None") : juce::String(sizes[i]));
                b.onClick = [this, size = sizes[i]]
                {
                    coolsynth::midi::ControllerSetup::instance().setKeyboardKeys(size);
                    refresh();
                    if (auto* panel = getParentComponent())
                        panel->repaint(); // the line over the screen's keys
                };
                b.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight
                                           : (i + 1 == keySize.size() ? juce::Button::ConnectedOnLeft
                                                                      : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight));
                addAndMakeVisible(b);
            }
            for (auto* c : std::initializer_list<juce::Component*> { &close, &save, &audio, &controller, &multi[0], &multi[1], &curve[0], &curve[1],
                                                                     &curve[2], &reset, &resetYes, &resetNo })
                addAndMakeVisible(*c);
        }

        void show()
        {
            audio.setVisible(static_cast<bool>(synth.showAudioSettings)); // the app sets its hook after the panel is built
            for (auto& b : mcp)
                b.setVisible(static_cast<bool>(synth.chooseMcpServer));
            confirming = false;
            layOutReset();
            resized();
            refresh();
            setVisible(true);
            toFront(true);
            grabKeyboardFocus();
        }

        void hide()
        {
            setVisible(false);
            confirming = false;
        }

        std::function<void()> onReset; // the panel goes back to Home, layer A
        std::function<void()> onController; // the controller setup's card (D211)

        void layOutReset()
        {
            reset.setVisible(! confirming);
            resetYes.setVisible(confirming);
            resetNo.setVisible(confirming);
        }

        /** The choices as they are, and the computer's last second (the panel calls this 30 times a second). */
        void refresh()
        {
            if (! isVisible())
                return;
            multi[0].setToggleState(synth.multiCore(), juce::dontSendNotification);
            multi[1].setToggleState(! synth.multiCore(), juce::dontSendNotification);
            for (int i = 0; i < 3; ++i)
                curve[static_cast<std::size_t>(i)].setToggleState(synth.velocityCurve() == i, juce::dontSendNotification);
            const bool mcpOn = synth.mcpServerOn && synth.mcpServerOn();
            mcp[0].setToggleState(! mcpOn, juce::dontSendNotification);
            mcp[1].setToggleState(mcpOn, juce::dontSendNotification);
            const int keys = coolsynth::midi::ControllerSetup::instance().keyboardKeys();
            for (std::size_t i = 0; i < keySize.size(); ++i)
                keySize[i].setToggleState(coolsynth::midi::ControllerSetup::keyboardSizes[i] == keys, juce::dontSendNotification);
            repaint();
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                hide();
                return true;
            }
            return false;
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (! cardArea.contains(e.getPosition()))
                hide(); // a click beside the card
        }

        void resized() override
        {
            const bool withMcp = static_cast<bool>(synth.chooseMcpServer);
            // With the MCP row (the app's only) the rows sit a little closer, so the card still fits the panel.
            const int gap = withMcp ? 10 : 14;
            cardArea = juce::Rectangle<int>(480, (synth.showAudioSettings ? 622 : 562) + (withMcp ? 60 - 8 * (14 - gap) : 0))
                           .withCentre(getLocalBounds().getCentre());
            auto r = cardArea.reduced(28, 22);
            auto head = r.removeFromTop(30);
            close.setBounds(head.removeFromRight(30));
            iconArea = head.removeFromLeft(30);
            head.removeFromLeft(10);
            versionArea = head.removeFromRight(120).withTrimmedRight(8);
            titleArea = head;
            r.removeFromTop(14);
            auto section = [&](juce::Rectangle<int>& label, int height)
            {
                label = r.removeFromTop(16);
                auto row = r.removeFromTop(height);
                r.removeFromTop(gap);
                return row;
            };
            save.setBounds(section(soundLabel, 30).withWidth(170));
            if (synth.showAudioSettings)
                audio.setBounds(section(audioLabel, 30).withWidth(250));
            auto controllerRow = section(controllerLabel, 30);
            controller.setBounds(controllerRow.removeFromLeft(190));
            controllerText = controllerRow.withTrimmedLeft(12);
            auto keysRow = section(keysLabel, 48);
            auto keyButtons = keysRow.removeFromTop(30);
            for (auto& b : keySize)
                b.setBounds(keyButtons.removeFromLeft(64));
            keysText = keysRow;
            auto multiRow = section(multiLabel, 48);
            auto buttons = multiRow.removeFromTop(30);
            for (auto& b : multi)
                b.setBounds(buttons.removeFromLeft(90));
            multiText = multiRow;
            auto curveRow = section(curveLabel, 48);
            buttons = curveRow.removeFromTop(30);
            for (auto& b : curve)
                b.setBounds(buttons.removeFromLeft(90));
            curveText = curveRow;
            if (withMcp)
            {
                auto mcpRow = section(mcpLabel, 30);
                for (auto& b : mcp)
                    b.setBounds(mcpRow.removeFromLeft(70));
                mcpText = mcpRow.withTrimmedLeft(12);
            }
            auto resetRow = section(resetLabel, 30);
            reset.setBounds(resetRow.withWidth(170));
            resetYes.setBounds(resetRow.removeFromLeft(90));
            resetRow.removeFromLeft(8);
            resetNo.setBounds(resetRow.removeFromLeft(90));
            resetText = resetRow.withTrimmedLeft(12);
            computerLabel = r.removeFromTop(16);
            computerText = r.removeFromTop(40);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.55f)); // the panel behind, dimmed
            const auto card = cardArea.toFloat();
            g.setColour(colours::panel);
            g.fillRoundedRectangle(card, 14.0f);
            g.setColour(colours::panelEdge.brighter(0.2f));
            g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);

            g.drawImage(icon, iconArea.toFloat(), juce::RectanglePlacement::centred);
            g.setColour(colours::ink);
            g.setFont(sans(20.0f, true));
            g.drawText("Preferences", titleArea, juce::Justification::centredLeft);
            // Which CoolSynth this is, small and faint: the build's own version (CMakeLists.txt), never typed in here.
            g.setColour(colours::print.withAlpha(0.6f));
            g.setFont(sans(11.0f));
            g.drawText("CoolSynth " + coolsynth::build::getVersion(), versionArea, juce::Justification::centredRight);

            auto label = [&](juce::Rectangle<int> area, const juce::String& text)
            {
                g.setColour(colours::print);
                g.setFont(mono(10.0f, true));
                g.drawText(text, area, juce::Justification::centredLeft);
            };
            label(soundLabel, "SOUND");
            if (synth.showAudioSettings)
                label(audioLabel, "AUDIO AND MIDI");
            label(controllerLabel, "CONTROLLER");
            {
                const auto& setup = coolsynth::midi::ControllerSetup::instance();
                using Origin = coolsynth::midi::ControllerSetup::Origin;
                g.setColour(colours::muted);
                g.setFont(sans(12.0f));
                g.drawFittedText(setup.origin() == Origin::none ? juce::String("Nothing set up yet.")
                                     : (setup.origin() == Origin::found ? setup.map().name + ", set up by itself." : juce::String("Your own setup.")),
                                 controllerText, juce::Justification::centredLeft, 2);
            }
            label(keysLabel, "YOUR KEYBOARD");
            {
                const int keys = coolsynth::midi::ControllerSetup::instance().keyboardKeys();
                g.setColour(colours::muted);
                g.setFont(sans(12.5f));
                g.drawText(synth.getInterfaceModel().hardwareConnected() ? juce::String("Your MiniLab 3 needs no setting here: 25 keys.")
                               : (keys == 0 ? juce::String("No line over the screen's keys.")
                                            : "The line over the screen's keys shows your " + juce::String(keys) + " keys."),
                           keysText, juce::Justification::centredLeft);
            }
            label(multiLabel, "MULTI-CORE");
            label(curveLabel, "VELOCITY CURVE");
            label(resetLabel, "RESET");
            if (synth.chooseMcpServer)
            {
                label(mcpLabel, "CLAUDE (MCP)");
                g.setColour(colours::muted);
                g.setFont(sans(12.0f));
                g.drawFittedText(synth.mcpServerStatus ? synth.mcpServerStatus() : juce::String(), mcpText, juce::Justification::centredLeft, 2);
            }
            if (confirming)
            {
                g.setColour(colours::muted);
                g.setFont(sans(12.0f));
                g.drawFittedText(juce::String(SynthAudioProcessor::startSoundName) + " on layer A, layer B empty, faders at the top. Sure?", resetText,
                                 juce::Justification::centredLeft, 2);
            }
            label(computerLabel, "THIS COMPUTER, THE LAST SECOND");

            g.setColour(colours::muted);
            g.setFont(sans(12.5f));
            g.drawText(synth.multiCore() ? "Sounds that overlap play on several cores."
                                         : "Everything on the audio thread.", multiText, juce::Justification::centredLeft);
            const int c = synth.velocityCurve();
            g.drawText(c == SynthAudioProcessor::softCurve ? "A light touch plays loud."
                           : (c == SynthAudioProcessor::hardCurve ? "Loud takes a firm touch." : "As played."),
                       curveText, juce::Justification::centredLeft);

            const auto& workers = coolsynth::synth::v3::AudioWorkers::instance();
            g.setColour(colours::muted);
            g.setFont(mono(11.0f));
            auto lines = computerText;
            g.drawText(PanelEditor::cpuSummary(synth), lines.removeFromTop(18), juce::Justification::centredLeft);
            g.drawText(workers.cpu().describe() + ", " + juce::String(workers.workerCount())
                           + (workers.workerCount() == 1 ? " helper" : " helpers"),
                       lines.removeFromTop(18), juce::Justification::centredLeft);
        }

    private:
        SynthAudioProcessor& synth;
        juce::TextButton close, save, audio, controller, reset, resetYes, resetNo;
        bool confirming = false; // Reset asks first, in the card (D199)
        std::array<juce::TextButton, 2> multi;
        std::array<juce::TextButton, 3> curve;
        std::array<juce::TextButton, 2> mcp;
        juce::Rectangle<int> mcpLabel, mcpText;
        std::array<juce::TextButton, coolsynth::midi::ControllerSetup::keyboardSizes.size()> keySize;
        juce::Rectangle<int> cardArea, titleArea, iconArea, versionArea, soundLabel, audioLabel, controllerLabel, multiLabel, curveLabel,
                             resetLabel, computerLabel, controllerText, multiText, curveText, resetText, computerText, keysLabel, keysText;
        const juce::Image icon = smallIcon();

        /** CoolSynth's icon (resources/icon) at 64 px, halved step by step from 512 so its thin lines stay smooth;
            drawn at 30 it stays sharp on a screen scaled up to 200%. */
        static juce::Image smallIcon()
        {
            auto image = juce::ImageCache::getFromMemory(BinaryData::coolsynthicon512_png, BinaryData::coolsynthicon512_pngSize);
            while (image.isValid() && image.getWidth() > 64)
                image = image.rescaled(image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::highResamplingQuality);
            return image;
        }
    };
}
// ---- Controller: the guided setup (D211) ----------------------------------------------------------------

namespace
{
    namespace cm = coolsynth::midi;

    /** The controller setup as a card over the dimmed panel, like Prefs (D211): which setup is in use, the ready
        setups of the controllers CoolSynth knows, and the setup step by step, which asks for each of the panel's
        places in turn and listens for the control moved. Every control heard is saved at once, for this computer. */
    class ControllerCard final : public juce::Component, private juce::Timer
    {
    public:
        explicit ControllerCard(SynthAudioProcessor& s) : synth(s)
        {
            setVisible(false);
            setWantsKeyboardFocus(true);
            for (auto* b : { &close, &use, &start, &forgetAll, &forgetYes, &forgetNo, &previous, &skip, &done, &driverOn, &driverOff })
            {
                b->setColour(juce::TextButton::buttonColourId, colours::knob);
                b->setColour(juce::TextButton::buttonOnColourId, colours::ink);
                b->setColour(juce::TextButton::textColourOffId, colours::ink);
                b->setColour(juce::TextButton::textColourOnId, colours::background);
                addAndMakeVisible(*b);
            }
            close.setButtonText(juce::String(juce::CharPointer_UTF8("\xc3\x97")));
            close.onClick = [this] { hide(); };
            // The MiniLab driver on or off (D211): off, the MiniLab is a controller like any other.
            driverOn.setButtonText("On");
            driverOff.setButtonText("Off");
            driverOn.setConnectedEdges(juce::Button::ConnectedOnRight);
            driverOff.setConnectedEdges(juce::Button::ConnectedOnLeft);
            driverOn.onClick = [this] { cm::ControllerSetup::instance().setMiniLabDriverOn(true); layOut(); };
            driverOff.onClick = [this] { cm::ControllerSetup::instance().setMiniLabDriverOn(false); layOut(); };
            int id = 0;
            for (const auto& p : cm::readyProfiles())
                profiles.addItem(p.name, ++id);
            profiles.setColour(juce::ComboBox::backgroundColourId, colours::knob);
            profiles.setColour(juce::ComboBox::textColourId, colours::ink);
            profiles.setColour(juce::ComboBox::outlineColourId, colours::knobEdge);
            profiles.setColour(juce::ComboBox::arrowColourId, colours::muted);
            profiles.setTextWhenNothingSelected("Choose a controller");
            profiles.onChange = [this] { repaint(); };
            addAndMakeVisible(profiles);
            use.setButtonText("Use");
            use.onClick = [this] { useProfile(); };
            start.setButtonText(juce::String(juce::CharPointer_UTF8("Set up step by step\xe2\x80\xa6")));
            start.onClick = [this] { beginSteps(); };
            forgetAll.setButtonText(juce::String(juce::CharPointer_UTF8("Forget all\xe2\x80\xa6")));
            forgetAll.onClick = [this] { confirming = true; layOut(); };
            forgetYes.setButtonText("Forget");
            forgetYes.onClick = [this]
            {
                confirming = false;
                cm::ControllerSetup::instance().setMap({});
                layOut();
            };
            forgetNo.setButtonText("Cancel");
            forgetNo.onClick = [this] { confirming = false; layOut(); };
            previous.setButtonText("Back");
            previous.onClick = [this] { goToStep(step - 1); };
            skip.setButtonText("Skip");
            skip.onClick = [this] { goToStep(step + 1); };
            done.setButtonText("Done");
            done.onClick = [this] { endSteps(); };
        }

        ~ControllerCard() override { listenNoMore(); }

        void show()
        {
            confirming = false;
            stepping = false;
            const auto& setup = cm::ControllerSetup::instance();
            if (const auto* p = cm::readyProfile(setup.map().profileId))
                profiles.setText(p->name, juce::dontSendNotification);
            layOut();
            setVisible(true);
            toFront(true);
            grabKeyboardFocus();
            startTimerHz(10);
        }

        void hide()
        {
            listenNoMore();
            stepping = false;
            confirming = false;
            stopTimer();
            setVisible(false);
        }

        void startSteps() { beginSteps(); }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                if (stepping)
                    endSteps();
                else
                    hide();
                return true;
            }
            return false;
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (! cardArea.contains(e.getPosition()) && ! stepping)
                hide(); // a click beside the card (not while a step listens: a click is too easy then)
        }

        void resized() override { layOut(); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.55f));
            const auto card = cardArea.toFloat();
            g.setColour(colours::panel);
            g.fillRoundedRectangle(card, 14.0f);
            g.setColour(colours::panelEdge.brighter(0.2f));
            g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);
            g.setColour(colours::ink);
            g.setFont(sans(20.0f, true));
            g.drawText("Controller", titleArea, juce::Justification::centredLeft);

            auto label = [&g](juce::Rectangle<int> area, const juce::String& text)
            {
                g.setColour(colours::print);
                g.setFont(mono(10.0f, true));
                g.drawText(text, area, juce::Justification::centredLeft);
            };
            auto words = [&g](juce::Rectangle<int> area, const juce::String& text, juce::Colour colour, float size = 12.5f, int lines = 2)
            {
                g.setColour(colour);
                g.setFont(sans(size));
                g.drawFittedText(text, area, juce::Justification::topLeft, lines, 1.0f);
            };

            if (stepping)
            {
                const auto place = steps[static_cast<std::size_t>(step)];
                label(stepLabel, "STEP " + juce::String(step + 1) + " OF " + juce::String(static_cast<int>(steps.size())));
                g.setColour(colours::ink);
                g.setFont(sans(24.0f, true));
                g.drawText(cm::placeName(place), placeArea, juce::Justification::centredLeft);
                words(instructionArea, instruction(place), colours::muted, 13.5f, 4);
                const auto* link = cm::ControllerSetup::instance().map().linkOf(place);
                words(nowArea, "Now: " + (link != nullptr ? link->control.describe() + ", " + cm::encodingName(link->encoding)
                                                         : juce::String("nothing")), colours::muted);
                words(heardArea, heard.isNotEmpty() ? heard : juce::String(juce::CharPointer_UTF8("Listening for your controller\xe2\x80\xa6")),
                      coolsynth::ui::tintColour(Tint::home).withAlpha(heard.isNotEmpty() ? 1.0f : 0.6f), 13.5f, 2);
                return;
            }

            label(statusLabel, "IN USE");
            words(statusText, status(), colours::ink, 13.0f, 3);
            label(driverLabel, "MINILAB 3 DRIVER");
            words(driverText, cm::ControllerSetup::instance().miniLabDriverOn()
                                  ? "CoolSynth uses a MiniLab 3's screen, pads and knobs."
                                  : "A MiniLab 3 works like any other keyboard.",
                  colours::muted);
            label(readyLabel, "KNOWN CONTROLLERS");
            if (const auto* p = chosenProfile())
                words(readyText, p->note, colours::muted);
            else
                words(readyText, "If your keyboard is in this list, choose it and click Use.", colours::muted);
            label(stepsLabel, "STEP BY STEP");
            words(stepsText, "Asks for the main knob, the knobs, faders and pads, and Shift, one at a time; skip any your "
                             "keyboard lacks.", colours::muted);
            label(learnLabel, "ONE AT A TIME");
            words(learnText, "Right-click a knob, fader or pad on the panel to learn just that one, or to tie a knob to one "
                             "setting on every page.", colours::muted);
            label(forgetLabel, "FORGET");
            if (confirming)
                words(forgetText, "Every control linked, on this computer. Sure?", colours::muted);
        }

    private:
        SynthAudioProcessor& synth;
        juce::TextButton close, use, start, forgetAll, forgetYes, forgetNo, previous, skip, done, driverOn, driverOff;
        juce::Rectangle<int> driverLabel, driverText;
        juce::ComboBox profiles;
        bool confirming = false;
        bool stepping = false;
        bool listening = false;
        int learnToken = 0; // the learning this card started (review of D211)
        bool waitingForOther = false; // another window took the learning over: listen again once it is done
        int step = 0;
        std::vector<cm::Place> steps;
        juce::String heard; // what the step heard, or how to go on
        juce::uint32 advanceAt = 0;
        juce::Rectangle<int> cardArea, titleArea, statusLabel, statusText, readyLabel, readyText, stepsLabel, stepsText,
                             learnLabel, learnText, forgetLabel, forgetText, stepLabel, placeArea, instructionArea, nowArea, heardArea;

        const cm::ReadyProfile* chosenProfile() const
        {
            const int index = profiles.getSelectedItemIndex();
            const auto& all = cm::readyProfiles();
            return index >= 0 && index < static_cast<int>(all.size()) ? &all[static_cast<std::size_t>(index)] : nullptr;
        }

        juce::String status() const
        {
            const auto& setup = cm::ControllerSetup::instance();
            const auto& map = setup.map();
            juce::String text;
            const int controls = static_cast<int>(map.places.size() + map.settings.size());
            const auto count = juce::String(controls) + (controls == 1 ? " control" : " controls");
            switch (setup.origin())
            {
                case cm::ControllerSetup::Origin::found:
                    text = map.name + ": set up by itself, because it is plugged in.";
                    break;
                case cm::ControllerSetup::Origin::saved:
                    text = "Your own setup: " + count + " linked.";
                    break;
                case cm::ControllerSetup::Origin::none:
                default:
                    text = "Nothing set up: your keys play, but your knobs and pads do not move CoolSynth's panel.";
                    break;
            }
            text += " The app and the plugin share it, in every project.";
            if (synth.getInterfaceModel().hardwareConnected())
                text += " Your MiniLab 3 needs no setup: CoolSynth handles it.";
            return text;
        }

        static juce::String instruction(cm::Place place)
        {
            using cm::Place;
            switch (place)
            {
                case Place::mainTurn:
                    return "The knob for browsing sounds and lists. Turn the knob you want a little right, then left, and let "
                           "go. No knob to spare? Press a button instead: it steps left, and the next step asks for a "
                           "button that steps right.";
                case Place::mainRight: return "The button for one step right.";
                case Place::mainPress: return "Press what you want for opening and loading: the browsing knob's own press, a button or a pad.";
                case Place::knob5:     return "The knob for turning pages. Turn it a little right, then left, and let go.";
                case Place::shift:     return "Press the button you want to hold as Shift: it makes the knobs move in small steps.";
                case Place::back:      return "Press the button you want for going back.";
                default:
                    break;
            }
            if (cm::isButtonPlace(place))
                return "Press the pad or button you want for it.";
            if (place >= Place::fader1 && place <= Place::fader4)
                return "Move the fader (or knob) you want for it up and down.";
            return "Turn the knob you want for it a little right, then left, and let go.";
        }

        void useProfile()
        {
            if (const auto* p = chosenProfile())
                cm::ControllerSetup::instance().setMap(p->map);
            repaint();
        }

        void beginSteps()
        {
            using cm::Place;
            steps = { Place::mainTurn, Place::mainPress, Place::knob5, Place::knob1, Place::knob2, Place::knob3, Place::knob4,
                      Place::knob6, Place::knob7, Place::knob8, Place::fader1, Place::fader2, Place::fader3, Place::fader4,
                      Place::pad1, Place::pad2, Place::pad3, Place::pad4, Place::pad5, Place::pad6, Place::pad7, Place::pad8,
                      Place::shift, Place::back };
            stepping = true;
            confirming = false;
            goToStep(0);
        }

        void endSteps()
        {
            listenNoMore();
            stepping = false;
            layOut();
        }

        void goToStep(int next)
        {
            if (next < 0)
                next = 0;
            if (next >= static_cast<int>(steps.size()))
            {
                endSteps();
                return;
            }
            step = next;
            heard = {};
            advanceAt = 0;
            layOut();
            listen();
        }

        void listen()
        {
            const auto place = steps[static_cast<std::size_t>(step)];
            auto& setup = cm::ControllerSetup::instance();
            juce::Component::SafePointer<ControllerCard> self(this);
            listening = true;
            waitingForOther = false;
            learnToken = setup.learn(&synth.getControllerInput(),
                        cm::isButtonPlace(place) ? cm::ControllerSetup::Expect::press : cm::ControllerSetup::Expect::turn,
                        [self, place](const cm::ControllerSetup::Heard& h)
                        {
                            if (self != nullptr)
                                self->heardControl(place, h);
                        },
                        [self](const juce::String& progress)
                        {
                            if (self != nullptr)
                            {
                                self->heard = progress;
                                self->repaint();
                            }
                        });
        }

        void listenNoMore()
        {
            if (listening)
                cm::ControllerSetup::instance().stopLearning(learnToken);
            listening = false;
            waitingForOther = false;
        }

        void heardControl(cm::Place place, const cm::ControllerSetup::Heard& h)
        {
            listening = false;
            auto& setup = cm::ControllerSetup::instance();
            auto map = setup.map();
            using cm::Place;
            if (h.button && ! cm::isButtonPlace(place))
            {
                if (place != Place::mainTurn)
                {
                    heard = "That was a button, but this needs a knob. Turn a knob, or press Skip.";
                    repaint();
                    listen();
                    return;
                }
                // No knob for browsing: two buttons, left and right.
                map.forget(Place::mainTurn);
                map.link(Place::mainLeft, h.control, cm::Encoding::absolute);
                if (step + 1 >= static_cast<int>(steps.size()) || steps[static_cast<std::size_t>(step + 1)] != Place::mainRight)
                    steps.insert(steps.begin() + step + 1, Place::mainRight);
                place = Place::mainLeft;
            }
            else
                map.link(place, h.control, h.encoding);
            if (map.name.isEmpty() || setup.origin() == cm::ControllerSetup::Origin::none)
                map.name = "Your controller";
            setup.setMap(std::move(map));
            heard = "Got it: " + h.control.describe() + (h.button ? juce::String() : ", " + cm::encodingName(h.encoding)) + ".";
            advanceAt = juce::Time::getMillisecondCounter() + 900; // a moment to read it, then the next step
            repaint();
        }

        void timerCallback() override
        {
            // Another CoolSynth window started learning meanwhile: this step waits until it is done, then listens again.
            auto& setup = cm::ControllerSetup::instance();
            if (listening && ! setup.learningWith(learnToken))
            {
                listening = false;
                waitingForOther = true;
                heard = "Another CoolSynth window is learning: this step waits for it.";
                repaint();
            }
            if (waitingForOther && stepping && ! setup.isLearning())
            {
                waitingForOther = false;
                heard = {};
                listen();
                repaint();
            }
            if (advanceAt != 0 && static_cast<juce::int32>(juce::Time::getMillisecondCounter() - advanceAt) >= 0)
            {
                advanceAt = 0;
                goToStep(step + 1);
            }
            if (! stepping)
                layOut(); // a controller plugged in or out, another program's save
        }

        void layOut()
        {
            cardArea = juce::Rectangle<int>(520, 570).withCentre(getLocalBounds().getCentre());
            auto r = cardArea.reduced(28, 22);
            auto head = r.removeFromTop(30);
            close.setBounds(head.removeFromRight(30));
            titleArea = head;
            r.removeFromTop(12);

            for (auto* c : std::initializer_list<juce::Component*> { &profiles, &use, &start, &forgetAll, &forgetYes, &forgetNo,
                                                                     &driverOn, &driverOff })
                c->setVisible(! stepping);
            const bool on = cm::ControllerSetup::instance().miniLabDriverOn();
            driverOn.setToggleState(on, juce::dontSendNotification);
            driverOff.setToggleState(! on, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &previous, &skip, &done })
                c->setVisible(stepping);

            if (stepping)
            {
                stepLabel = r.removeFromTop(16);
                r.removeFromTop(6);
                placeArea = r.removeFromTop(34);
                r.removeFromTop(8);
                instructionArea = r.removeFromTop(76);
                r.removeFromTop(8);
                nowArea = r.removeFromTop(20);
                r.removeFromTop(10);
                heardArea = r.removeFromTop(44);
                auto buttons = r.removeFromBottom(30);
                previous.setBounds(buttons.removeFromLeft(90));
                buttons.removeFromLeft(8);
                skip.setBounds(buttons.removeFromLeft(90));
                done.setBounds(buttons.removeFromRight(90));
                previous.setEnabled(step > 0);
                repaint();
                return;
            }

            auto section = [&r](juce::Rectangle<int>& label, int height)
            {
                label = r.removeFromTop(16);
                auto row = r.removeFromTop(height);
                r.removeFromTop(12);
                return row;
            };
            statusText = section(statusLabel, 52);
            auto driverRow = section(driverLabel, 30);
            driverOn.setBounds(driverRow.removeFromLeft(70));
            driverOff.setBounds(driverRow.removeFromLeft(70));
            driverText = driverRow.withTrimmedLeft(12);
            auto ready = section(readyLabel, 70);
            auto row = ready.removeFromTop(30);
            profiles.setBounds(row.removeFromLeft(330));
            row.removeFromLeft(8);
            use.setBounds(row.removeFromLeft(80));
            readyText = ready.withTrimmedTop(6);
            auto stepsRow = section(stepsLabel, 70);
            start.setBounds(stepsRow.removeFromTop(30).withWidth(200));
            stepsText = stepsRow.withTrimmedTop(6);
            learnText = section(learnLabel, 36);
            auto forgetRow = section(forgetLabel, 30);
            forgetAll.setBounds(forgetRow.withWidth(140));
            forgetYes.setBounds(forgetRow.removeFromLeft(90));
            forgetRow.removeFromLeft(8);
            forgetNo.setBounds(forgetRow.removeFromLeft(90));
            forgetText = forgetRow.withTrimmedLeft(12);
            forgetAll.setVisible(! confirming);
            forgetYes.setVisible(confirming);
            forgetNo.setVisible(confirming);
            repaint();
        }
    };
}

class PanelEditor::Content final : public juce::Component
{
public:
    explicit Content(PanelEditor& o)
        : owner(o), model(o.model), screen(o.model),
          keyboard(o.synth.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        screen.onChanged = [this] { owner.timerCallback(); };
        screen.fine = [this] { return owner.shiftHeld(); };
        addAndMakeVisible(screen);

        mainKnob.isMain = true;
        mainKnob.onHover = [this](bool on)
        {
            setHoverHint(on ? "Main knob: turn for " + model.mainTurnRole() + ", press: " + model.mainPressRole() : juce::String());
        };
        mainKnob.spinsItself = false; // it follows every main-knob turn, the wheel's and the MiniLab's too (D193)
        mainKnob.onSteps = [this](int s) { model.turnMain(s); owner.timerCallback(); };
        mainKnob.onPress = [this] { model.pressMain(); owner.timerCallback(); };
        mainKnob.onMenu = [this] { showLearnMenu(coolsynth::midi::Place::mainTurn, mainKnob); };
        addAndMakeVisible(mainKnob);

        for (int k = 0; k < 8; ++k)
        {
            auto& knob = knobs[static_cast<std::size_t>(k)];
            const int number = k + 1;
            knob.navigation = number == 5;
            knob.onHover = [this, k](bool on)
            {
                const auto label = k == 4 ? juce::String("pages") : knobLabels[static_cast<std::size_t>(k)];
                setHoverHint(on ? "Knob " + juce::String(k + 1) + ": " + label + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 turn, drag or scroll \xc2\xb7 right-click: learn"))
                                : juce::String());
            };
            knob.spinsItself = number != 5; // knob 5 follows every page turn, as the main knob (D193)
            knob.onSteps = [this, number](int s)
            {
                if (number == 5)
                    model.turnPageKnob(s);
                else
                    model.turnKnob(number, s, owner.shiftHeld());
                owner.timerCallback();
            };
            knob.onPress = [this, number]
            {
                model.pressKnob(number); // loads an instrument the knob shows (D156)
                owner.timerCallback();
            };
            knob.onMenu = [this, k] { showLearnMenu(coolsynth::midi::knobPlace(k + 1), knobs[static_cast<std::size_t>(k)]); };
            addAndMakeVisible(knob);
        }

        for (int f = 0; f < 4; ++f)
        {
            auto& fader = faders[static_cast<std::size_t>(f)];
            fader.setSliderStyle(juce::Slider::LinearVertical);
            fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            // The Master's track goes on a quarter beyond its top, for the boost (D225); the others end at their top.
            fader.setRange(0.0, f == 3 ? 1.0 + masterBoostTravel : 1.0);
            fader.setColour(juce::Slider::trackColourId, colours::ink.withAlpha(0.8f));
            fader.setColour(juce::Slider::backgroundColourId, colours::knob);
            fader.setColour(juce::Slider::thumbColourId, colours::ink);
            fader.onValueChange = [this, f]
            {
                if (updatingFaders)
                    return;
                const auto value = static_cast<float>(faders[static_cast<std::size_t>(f)].getValue());
                if (f == 3 && value > 1.0f)
                {
                    if (model.faderValue(3) < 0.999f)
                        model.moveFader(3, 1.0f, false);
                    model.setMasterBoost(12.0f * (value - 1.0f) / static_cast<float>(masterBoostTravel));
                }
                else
                    model.moveFader(f, value, false);
            };
            // a fader lights its thumb and names itself while the mouse is on it (D204)
            auto& watcher = faderWatchers[static_cast<std::size_t>(f)];
            watcher.onHover = [this, f](bool on)
            {
                auto& fd = faders[static_cast<std::size_t>(f)];
                fd.setColour(juce::Slider::thumbColourId, on && fd.isEnabled() ? juce::Colours::white : colours::ink);
                setHoverHint(on ? "Fader " + juce::String(f + 1) + ": " + model.faderName(f)
                                      + (fd.isEnabled() ? juce::String(juce::CharPointer_UTF8(" \xc2\xb7 drag \xc2\xb7 right-click: learn")) : juce::String(" (the MiniLab's)"))
                                : juce::String());
            };
            fader.addMouseListener(&watcher, false);
            fader.onMenu = [this, f]
            {
                showLearnMenu(static_cast<coolsynth::midi::Place>(static_cast<int>(coolsynth::midi::Place::fader1) + f),
                              faders[static_cast<std::size_t>(f)]);
            };
            addAndMakeVisible(fader);
        }

        for (int p = 0; p < 8; ++p)
        {
            auto& pad = pads[static_cast<std::size_t>(p)];
            pad.symbol = p >= 4 && p <= 6 ? p - 4 : -1;
            pad.onPress = [this, p]
            {
                model.pressPad(p, owner.shiftHeld());
                owner.timerCallback();
            };
            pad.onMenu = [this, p]
            {
                showLearnMenu(static_cast<coolsynth::midi::Place>(static_cast<int>(coolsynth::midi::Place::pad1) + p),
                              pads[static_cast<std::size_t>(p)]);
            };
            pad.onHover = [this, p](bool on) // (D204)
            {
                const auto face = pads[static_cast<std::size_t>(p)].face;
                const juce::String what = p == InterfaceModel::homePad ? face + ", again: layer switch"
                                        : (p >= 4 && p <= 6 ? juce::String("the recorder, later") : face);
                setHoverHint(on ? "Pad " + juce::String(p + 1) + ": " + what + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 click \xc2\xb7 right-click: learn")) : juce::String());
            };
            addAndMakeVisible(pad);
        }

        // Options where Shift was (D185): the screen no longer keeps a Shift of its own; the computer's Shift key and
        // the MiniLab's still turn the knobs finely.
        options.setButtonText("Prefs"); // "Options" had to be squeezed to fit Shift's width
        options.onClick = [this] { prefs.show(); }; // a dialog card since D197 (was a menu)
        options.setTooltip("Save sound, multi-core, velocity curve, your controller, the CPU's work; audio and MIDI in the app");
        for (auto* button : { &options, &hold, &octDown, &octUp })
        {
            button->setColour(juce::TextButton::buttonColourId, colours::knob);
            button->setColour(juce::TextButton::buttonOnColourId, colours::ink);
            button->setColour(juce::TextButton::textColourOffId, colours::ink);
            button->setColour(juce::TextButton::textColourOnId, colours::background);
            addAndMakeVisible(*button);
        }
        hold.setButtonText("Hold");
        // buttons light a little and say what they do while the mouse is on them (D204)
        const std::array<std::pair<juce::TextButton*, juce::String>, 4> buttonHints { {
            { &options, "Preferences" }, { &hold, "Hold the notes, or latch the arp" },
            { &octDown, "The screen's keys an octave down" }, { &octUp, "The screen's keys an octave up" } } };
        for (std::size_t i = 0; i < buttonHints.size(); ++i)
        {
            auto* button = buttonHints[i].first;
            const auto text = buttonHints[i].second;
            buttonWatchers[i].onHover = [this, button, text](bool on)
            {
                button->setColour(juce::TextButton::buttonColourId, on && button->isEnabled() ? colours::knob.brighter(0.25f) : colours::knob);
                setHoverHint(on ? text + (button->isEnabled() ? juce::String(juce::CharPointer_UTF8(" \xc2\xb7 click")) : juce::String(" (the MiniLab's own)"))
                                : juce::String());
            };
            button->addMouseListener(&buttonWatchers[i], false);
        }
        octDown.setButtonText("Oct -");
        octUp.setButtonText("Oct +");
        // Without a MiniLab these work on screen (D184): Hold holds the notes (or latches a running arp), the octave
        // shifts the keys. With a MiniLab connected they grey out: the keyboard has its own.
        hold.setTooltip("Holds the notes like a sustain pedal; with the arp on, latches it. The MiniLab's own Hold when it is connected.");
        octDown.setTooltip("The keys an octave down. The MiniLab's own when it is connected.");
        octUp.setTooltip("The keys an octave up. The MiniLab's own when it is connected.");
        hold.onClick = [this] { model.toggleHold(); owner.timerCallback(); };
        octDown.onClick = [this] { model.shiftOctave(-1); owner.timerCallback(); };
        octUp.onClick = [this] { model.shiftOctave(1); owner.timerCallback(); };

        for (auto* button : { &hold, &octDown, &octUp })
            button->setTooltip("Works inside the keyboard itself");

        keyboard.setAvailableRange(firstKey, firstKey + shownKeySpan); // five octaves (D192)
        keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xfff6f7f8));
        keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff1c1f23));
        keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xffa9b1ba)); // a shade darker (D191)
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, coolsynth::ui::tintColour(Tint::home).withAlpha(0.6f));
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, coolsynth::ui::tintColour(Tint::home).withAlpha(0.25f));
        keyboard.setBlackNoteWidthProportion(0.5f);
        keyboard.setWantsKeyboardFocus(false);
        keyboard.setKeyWidth(42.0f);
        addAndMakeVisible(keyboard);
        addChildComponent(prefs); // last, so it lies over everything when shown (D197)
        prefs.onReset = [this] { model.resetView(); owner.timerCallback(); };
        addChildComponent(controllerCard); // over everything too (D211)
        prefs.onController = [this] { controllerCard.show(); };

        setSize(designWidth, designHeight);
    }

    ~Content() override { cancelLearning(); }

    /** What the panel's hovered control does, for the screen's hint line (D204). */
    void setHoverHint(const juce::String& text)
    {
        if (screen.externalHint == text)
            return;
        screen.externalHint = text;
        screen.repaint();
    }

    // ---- The controller, one control at a time (D211) ----------------------------------------------------

    /** The screen's hint line in the panel's teal, for a while (0: until replaced). */
    void setNotice(const juce::String& text, int milliseconds)
    {
        screen.notice = text;
        noticeUntil = milliseconds > 0 ? juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(milliseconds) : 0;
        screen.repaint();
    }

    /** True when a Learn was listening and stops now. */
    bool cancelLearning()
    {
        if (! learningHere)
            return false;
        learningHere = false;
        coolsynth::midi::ControllerSetup::instance().stopLearning(learnToken); // only this panel's own (review of D211)
        setNotice({}, 1);
        return true;
    }

    /** A right click on a knob, fader or pad: learn a control for its place, or (knobs) for the setting it shows. */
    void showLearnMenu(coolsynth::midi::Place place, juce::Component& near)
    {
        namespace cm = coolsynth::midi;
        const auto& map = cm::ControllerSetup::instance().map();
        auto lower = [](const juce::String& name) { return name.substring(0, 1).toLowerCase() + name.substring(1); };
        const auto placeLabel = lower(cm::placeName(place));
        juce::PopupMenu menu;
        const auto* link = map.linkOf(place);
        menu.addSectionHeader(cm::placeName(place) + ": " + (link != nullptr ? link->control.describe() : juce::String("no control")));
        const bool followsPage = place >= cm::Place::knob1 && place <= cm::Place::knob4;
        menu.addItem(1, "Learn for " + placeLabel + (followsPage ? " (follows the page)" : juce::String()));
        juce::String parameterId, settingLabel;
        if (place >= cm::Place::knob1 && place <= cm::Place::knob8 && place != cm::Place::knob5)
            if (auto* p = model.knobParameter(cm::knobOf(place)))
            {
                parameterId = p->getParameterID();
                settingLabel = model.settingName(parameterId);
                const auto* setting = map.linkOf(parameterId);
                menu.addItem(2, "Learn for " + settingLabel + " (on every page)"
                                    + (setting != nullptr ? ", now " + setting->control.describe() : juce::String()));
            }
        const auto* press = place == cm::Place::mainTurn ? map.linkOf(cm::Place::mainPress) : nullptr;
        if (place == cm::Place::mainTurn)
            menu.addItem(5, "Learn for the main knob's press" + (press != nullptr ? ", now " + press->control.describe() : juce::String()));
        if (link != nullptr)
            menu.addItem(3, "Forget " + placeLabel + "'s control");
        if (parameterId.isNotEmpty() && map.linkOf(parameterId) != nullptr)
            menu.addItem(4, "Forget " + settingLabel + "'s control");
        if (press != nullptr)
            menu.addItem(6, "Forget the main knob press's control");
        menu.addSeparator();
        menu.addItem(9, juce::String(juce::CharPointer_UTF8("Controller setup\xe2\x80\xa6")));

        juce::Component::SafePointer<Content> self(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&near),
                           [self, place, parameterId, settingLabel](int chosen)
                           {
                               if (self == nullptr || chosen == 0)
                                   return;
                               auto& setup = cm::ControllerSetup::instance();
                               auto changed = setup.map();
                               switch (chosen)
                               {
                                   case 1: self->learn(place, {}, {}); return;
                                   case 2: self->learn(place, parameterId, settingLabel); return;
                                   case 5: self->learn(cm::Place::mainPress, {}, {}); return;
                                   case 3: changed.forget(place); break;
                                   case 4: changed.forget(parameterId); break;
                                   case 6: changed.forget(cm::Place::mainPress); break;
                                   case 9: self->controllerCard.show(); return;
                                   default: return;
                               }
                               setup.setMap(std::move(changed));
                               self->setNotice("Forgotten.", 2500);
                           });
    }

    /** Listens for the next control moved, for a place or, with a parameter, for that setting. */
    void learn(coolsynth::midi::Place place, const juce::String& parameterId, const juce::String& settingLabel)
    {
        namespace cm = coolsynth::midi;
        cancelLearning();
        const bool press = parameterId.isEmpty() && cm::isButtonPlace(place);
        const auto what = parameterId.isNotEmpty() ? settingLabel : cm::placeName(place);
        const auto escape = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 Esc cancels"));
        setNotice("Learning " + what + ": " + (press ? "press a pad or button on your keyboard" : "turn a knob a little right, then left")
                      + escape, 0);
        learningHere = true;
        juce::Component::SafePointer<Content> self(this);
        learnToken = cm::ControllerSetup::instance().learn(
            &owner.synth.getControllerInput(),
            press ? cm::ControllerSetup::Expect::press : cm::ControllerSetup::Expect::turn,
            [self, place, parameterId, what](const cm::ControllerSetup::Heard& h)
            {
                if (self == nullptr)
                    return;
                self->learningHere = false;
                auto& setup = cm::ControllerSetup::instance();
                auto changed = setup.map();
                if (changed.name.isEmpty())
                    changed.name = "Your controller";
                if (h.button && ! cm::isButtonPlace(place) && parameterId.isEmpty() && place == cm::Place::mainTurn)
                {
                    // no knob for browsing: two buttons, left and right
                    changed.forget(cm::Place::mainTurn);
                    changed.link(cm::Place::mainLeft, h.control, cm::Encoding::absolute);
                    setup.setMap(std::move(changed));
                    self->learn(cm::Place::mainRight, {}, {});
                    return;
                }
                if (h.button && (parameterId.isNotEmpty() || ! cm::isButtonPlace(place)))
                {
                    self->setNotice("That was a button, but " + what + " needs a knob. Right-click it to try again.", 6000);
                    return;
                }
                if (parameterId.isNotEmpty())
                    changed.link(parameterId, h.control, h.encoding);
                else
                    changed.link(place, h.control, h.encoding);
                setup.setMap(std::move(changed));
                self->setNotice(what + " is now " + h.control.describe()
                                    + (h.button ? juce::String() : ", " + cm::encodingName(h.encoding)) + ".", 6000);
            },
            [self](const juce::String& progress)
            {
                if (self != nullptr)
                    self->setNotice(progress + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 Esc cancels")), 0);
            });
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        // the layer panels and the CPU reading: lit and named while the mouse is on them (D204)
        int over = -1;
        for (int layer = 0; layer < 2; ++layer)
            if (layerPanels[static_cast<std::size_t>(layer)].contains(e.getPosition()))
                over = layer;
        const bool onCpu = cpuArea.contains(e.getPosition());
        const ChainBlock* onBlock = nullptr; // a block of a layer's chain (D226)
        for (const auto& block : chainBlocks)
            if (block.area.contains(e.getPosition()))
                onBlock = &block;
        const int blockKey = onBlock != nullptr ? onBlock->key : -1;
        setMouseCursor(over >= 0 || onCpu ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        // What the mouse is on, said on the layer screen itself, in the info row's place (D226, the user's choice).
        const juce::String dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
        juce::String hint;
        if (onBlock != nullptr)
            hint = onBlock->hint;
        else if (over >= 0)
        {
            const juce::String name = "Layer " + juce::String(over == 0 ? "A" : "B");
            if (layerTitles[static_cast<std::size_t>(over)].contains(e.getPosition()))
                hint = name + dot + "click: copy, swap, solo, empty";
            else
                hint = name + dot + (model.layerEmpty(over) ? juce::String("empty") : model.layerSoundName(over)) + dot
                       + "click: choose a sound in Home" + dot + "right click: copy, swap";
        }
        else if (onCpu)
            hint = "CPU" + dot + "click: Preferences";

        if (over != hoveredPanel || onCpu != hoveredCpu || blockKey != hoveredBlock || hint != layerHint)
        {
            hoveredPanel = over;
            hoveredCpu = onCpu;
            hoveredBlock = blockKey;
            layerHint = hint;
            setHoverHint({}); // the main screen's hint line is for the rest of the panel
            repaint(layerScreen);
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoveredPanel >= 0 || hoveredCpu || hoveredBlock >= 0 || layerHint.isNotEmpty())
        {
            hoveredPanel = -1;
            hoveredCpu = false;
            hoveredBlock = -1;
            layerHint.clear();
            repaint(layerScreen);
        }
    }

    void showPreferences() { prefs.show(); }

    void showControllerCard(bool stepByStep)
    {
        controllerCard.show();
        if (stepByStep)
            controllerCard.startSteps();
    }

    /** Escape stops a Learn and closes Prefs or the controller card first (D197, D211). */
    bool closePrefs()
    {
        if (cancelLearning())
            return true;
        if (controllerCard.isVisible())
        {
            controllerCard.hide();
            return true;
        }
        if (! prefs.isVisible())
            return false;
        prefs.hide();
        return true;
    }

    void refresh()
    {
        screen.repaint();
        prefs.refresh();
        // The controller's word (D211): a ready setup found, said once in this program for a while; a notice's time over.
        const auto& setup = coolsynth::midi::ControllerSetup::instance();
        // A Learn taken over by another CoolSynth window: this one's prompt goes.
        if (learningHere && ! setup.learningWith(learnToken))
        {
            learningHere = false;
            setNotice({}, 1);
        }
        // Setup notices wait while a Learn listens: its prompt stays on the screen (review of D211).
        if (setup.noticeSerial() != shownSetupNotice && ! learningHere)
        {
            shownSetupNotice = setup.noticeSerial();
            setNotice(setup.noticeText(), 9000);
        }
        if (noticeUntil != 0 && static_cast<juce::int32>(juce::Time::getMillisecondCounter() - noticeUntil) >= 0)
            setNotice({}, 0);
        // The main knob and knob 5 turn with every step the model took, at most five detents at once (End, Home).
        if (const auto turns = model.mainTurnCount(); turns != shownMainTurns)
        {
            mainKnob.spinBy(static_cast<int>(juce::jlimit<std::int64_t>(-5, 5, turns - shownMainTurns)));
            shownMainTurns = turns;
        }
        if (const auto turns = model.pageTurnCount(); turns != shownPageTurns)
        {
            knobs[4].spinBy(static_cast<int>(juce::jlimit<std::int64_t>(-5, 5, turns - shownPageTurns)));
            shownPageTurns = turns;
        }
        // A knob turned (by the mouse, the wheel or the MiniLab) shows its value in its middle (D183).
        const auto now = juce::Time::getMillisecondCounter();
        if (const auto& popup = model.popup(); popup.serial != shownPopup)
        {
            shownPopup = popup.serial;
            if (! popup.fader && popup.control >= 1 && popup.control <= 8)
                knobs[static_cast<std::size_t>(popup.control - 1)].showValue(popup.value, now);
        }
        for (auto& knob : knobs)
            knob.ageValue(now);

        // A MiniLab connected (D184): the screen's Hold and octave grey out, and the faders follow the MiniLab's
        // without taking the mouse; without one, all of them work.
        const bool hardware = model.hardwareConnected();
        for (auto* button : { &hold, &octDown, &octUp })
            if (button->isEnabled() == hardware)
            {
                button->setEnabled(! hardware);
                button->setAlpha(hardware ? 0.35f : 1.0f);
            }
        for (auto& fader : faders)
            if (fader.isEnabled() == hardware)
                fader.setEnabled(! hardware);
        hold.setToggleState(model.holdOn(), juce::dontSendNotification);
        // Five octaves (D192). Without a MiniLab the screen's Oct buttons move them; with one they move, by whole
        // octaves, only as far as needed to keep its 25 keys in sight.
        int start = juce::jlimit(0, 127 - shownKeySpan, firstKey + 12 * model.octaveShift());
        if (model.hardwareConnected())
        {
            const int low = model.keyboardLowKey(), high = low + model.keyboardKeySpan();
            start = firstKey;
            while (high > start + shownKeySpan && start + 12 <= 127 - shownKeySpan)
                start += 12;
            while (low < start && start >= 12)
                start -= 12;
        }
        if (start != shownStart)
        {
            shownStart = start;
            keyboard.setAvailableRange(start, start + shownKeySpan);
            keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / static_cast<float>(whiteKeys));
        }
        const bool shiftOn = owner.shiftHeld();
        const auto pageTint = coolsynth::ui::tintColour(model.pageTint());
        for (int k = 0; k < 8; ++k)
        {
            auto& knob = knobs[static_cast<std::size_t>(k)];
            const int number = k + 1;
            if (number == 5)
            {
                knobLabels[static_cast<std::size_t>(k)] = model.pageKnobRole();
                knob.repaint();
                continue;
            }
            const auto view = model.knob(number);
            knob.showsValue = view.active;
            knob.value = view.normalised;
            // Knobs 1-4 in the page's colour; Glide (6) the edited layer's, as the layer screen; Brightness and Space
            // (7, 8), the whole sound's, light grey (D195): every knob that acts shows a ring.
            if (number <= 4)
                knob.ring = view.active ? pageTint : juce::Colours::transparentBlack;
            else if (number == 6)
                knob.ring = coolsynth::ui::tintColour(model.part() == 0 ? Tint::home : Tint::partB);
            else
                knob.ring = juce::Colour(0xffc4cad1);
            if (view.idle) // does nothing now: a faint ring, still turnable (D201)
                knob.ring = knob.ring.withAlpha(0.28f);
            knobIdle[static_cast<std::size_t>(k)] = view.idle;
            knobLabels[static_cast<std::size_t>(k)] = view.name;
            knob.repaint();
        }
        updatingFaders = true;
        for (int f = 0; f < 4; ++f)
        {
            auto& fader = faders[static_cast<std::size_t>(f)];
            if (! fader.isMouseButtonDown())
                fader.setValue(model.faderValue(f) + (f == 3 ? masterBoostTravel * model.masterBoostDb() / 12.0f : 0.0f),
                               juce::dontSendNotification);
        }
        updatingFaders = false;
        for (int p = 0; p < 8; ++p)
        {
            auto& pad = pads[static_cast<std::size_t>(p)];
            pad.colour = model.padColour(p, shiftOn);
            const int padSection = InterfaceModel::sectionOfPad(p);
            pad.lit = (padSection >= 0 && model.view() == InterfaceModel::View::section && model.section() == padSection)
                      || (p == InterfaceModel::homePad && model.view() == InterfaceModel::View::home);
            pad.glow = p == InterfaceModel::homePad && model.loadingInstrument() ? InterfaceModel::loadingGlow() : -1.0f; // glows while loading (D156)
            pad.face = model.padLabel(p, shiftOn);
            pad.sub = p == InterfaceModel::homePad ? juce::String("Layer switch") : juce::String();
            pad.repaint();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::background);
        const auto panelBounds = getLocalBounds().reduced(10).toFloat();
        g.setColour(colours::panel);
        g.fillRoundedRectangle(panelBounds, 22.0f);
        g.setColour(colours::panelEdge);
        g.drawRoundedRectangle(panelBounds.reduced(0.5f), 22.0f, 1.0f);

        // the screen's bezel, round the glass and the main knob: a grey a shade lighter than the panel (D226)
        g.setColour(colours::bezel);
        g.fillRoundedRectangle(screenBox.toFloat(), 12.0f);
        g.setColour(colours::panelEdge);
        g.drawRoundedRectangle(screenBox.toFloat().reduced(0.5f), 12.0f, 1.0f);

        // strips and the left column's print
        g.setFont(sans(10.0f));
        g.setColour(colours::print);
        g.drawText("Chord", chordPrint, juce::Justification::centred);
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x90 Transpose \xe2\x86\x92")), transposePrint, juce::Justification::centred);
        for (int s = 0; s < 2; ++s)
        {
            auto strip = strips[static_cast<std::size_t>(s)].toFloat();
            g.setColour(colours::knobEdge);
            g.drawRoundedRectangle(strip, 10.0f, 1.0f);
            g.setColour(colours::print);
            g.drawText(s == 0 ? "Pitch" : "Mod", strip.removeFromBottom(22.0f), juce::Justification::centred);
        }
        // A thin line over the player's keyboard, as far as its notes tell (D192): the MiniLab's 25 keys, or the size
        // chosen in Preferences (D218); named in small print in the line's own teal, which moves with it (D203). With
        // another keyboard the screen's Oct buttons still move the screen's keys, so only the part in sight is drawn.
        const int lineLow = model.keyboardLowKey(), lineHigh = lineLow + model.keyboardKeySpan();
        if (model.keyboardKeys() > 0 && lineHigh >= shownStart && lineLow <= shownStart + shownKeySpan)
        {
            const auto first = keyboard.getRectangleForKey(juce::jmax(lineLow, shownStart));
            const auto last = keyboard.getRectangleForKey(juce::jmin(lineHigh, shownStart + shownKeySpan));
            const float x0 = static_cast<float>(keyboard.getX()) + first.getX();
            const float x1 = static_cast<float>(keyboard.getX()) + last.getRight();
            const float y = static_cast<float>(keyboard.getY()) - 8.0f;
            const auto teal = coolsynth::ui::tintColour(Tint::home).withAlpha(0.85f);
            g.setColour(teal);
            g.fillRoundedRectangle(juce::Rectangle<float>(x0 + 1.0f, y, x1 - x0 - 2.0f, 3.0f), 1.5f);
            g.setFont(sans(10.5f));
            if (x1 - x0 >= 80.0f) // a sliver at the edge goes unnamed rather than cut short
                g.drawText("Your keyboard", juce::Rectangle<float>(x0, y - 16.0f, x1 - x0, 14.0f), juce::Justification::centred, false);
        }

        paintLayerScreen(g);

        // main knob roles
        g.setFont(sans(11.0f));
        g.setColour(colours::screenDim);
        g.drawText("turn", mainTurnArea.withHeight(16), juce::Justification::centredRight);
        g.drawText("press", mainPressArea.withHeight(16), juce::Justification::centredLeft);
        g.setFont(sans(12.5f, true));
        g.setColour(colours::screenInk);
        g.drawText(model.mainTurnRole(), mainTurnArea.withTrimmedTop(16), juce::Justification::centredRight);
        g.drawText(model.mainPressRole(), mainPressArea.withTrimmedTop(16), juce::Justification::centredLeft);

        // knob numbers and names
        for (int k = 0; k < 8; ++k)
        {
            const auto b = knobs[static_cast<std::size_t>(k)].getBounds();
            g.setFont(sans(12.0f, true));
            g.setColour(colours::print);
            g.drawText(juce::String(k + 1), b.getX() - 10, b.getBottom() + 1, b.getWidth() + 20, 16, juce::Justification::centred);
            g.setFont(sans(14.0f)); // larger (D191; was 12)
            g.setColour(knobLabels[static_cast<std::size_t>(k)] == emDash || knobIdle[static_cast<std::size_t>(k)] ? colours::muted : colours::ink);
            g.drawFittedText(knobLabels[static_cast<std::size_t>(k)], b.getX() - 16, b.getBottom() + 17, b.getWidth() + 32, 36,
                             juce::Justification::centredTop, 2);
        }

        // fader numbers and names, on the lower knobs' lines (D226)
        const int numberLine = knobs[4].getBottom() + 1;
        for (int f = 0; f < 4; ++f)
        {
            const auto b = faders[static_cast<std::size_t>(f)].getBounds();
            g.setFont(sans(11.0f, true));
            g.setColour(colours::print);
            g.drawText(juce::String(f + 1), b.getX() - 10, numberLine, b.getWidth() + 20, 16, juce::Justification::centred);
            g.setFont(sans(12.0f));
            g.setColour(colours::ink);
            g.drawText(model.faderName(f), b.getX() - 18, numberLine + 16, b.getWidth() + 36, 16, juce::Justification::centred);
        }
        // The Master's top, the sound as designed, marked where its boost begins (D225).
        {
            const auto& master = faders[3];
            const auto b = master.getBounds();
            const float y = static_cast<float>(b.getY()) + static_cast<float>(master.getPositionOfValue(1.0));
            // On the Master's own right, clear of the Effects fader beside it.
            g.setColour(colours::print);
            g.drawLine(static_cast<float>(b.getRight()) - 2.0f, y, static_cast<float>(b.getRight()) + 6.0f, y, 1.0f);
            g.setFont(sans(9.0f));
            g.drawText("0 dB", b.getRight() + 8, static_cast<int>(y) - 7, 30, 14, juce::Justification::centredLeft);
            // the boost over the Master's own top, under the layer screen (D226)
            const auto boost = model.masterBoostDb();
            if (boost > 0.0f)
            {
                g.setColour(coolsynth::ui::tintColour(Tint::sound));
                g.setFont(sans(10.0f, true));
                g.drawText("+" + juce::String(boost, 1) + " dB", b.getCentreX() - 30, b.getY() - 15, 60, 13, juce::Justification::centred);
            }
        }

    }

    void resized() override
    {
        // the MiniLab 3's layout: left column, screen, knobs, faders; pads under them; keys at the bottom
        const int top = 26, left = 38; // the top strip gave way when Options moved to the left column (D185)
        const int leftWidth = 118, screenWidth = 360, knobsWidth = 300, gap = 26;
        // the faders a little further apart than the knobs' gap would leave them (D191): 44 px each, was 40
        const int screenX = left + leftWidth + gap, knobsX = screenX + screenWidth + gap, fadersX = knobsX + knobsWidth + gap - 8;
        const int faderWidth = designWidth - 38 - fadersX;

        // left column
        options.setBounds(left, top, 54, 30);
        hold.setBounds(left + 64, top, 54, 30);
        chordPrint = { left + 64, top + 32, 54, 14 };
        octDown.setBounds(left, top + 50, 54, 30);
        octUp.setBounds(left + 64, top + 50, 54, 30);
        transposePrint = { left, top + 86, leftWidth, 14 };

        // the screen and the main knob
        screenBox = { screenX, top, screenWidth, 418 };
        screen.setBounds(screenBox.reduced(12).withHeight(318));
        const int knobY = screen.getBottom() + 12;
        mainKnob.setBounds(screenBox.getCentreX() - 34, knobY, 68, 68);
        mainTurnArea = { screenBox.getX() + 20, knobY + 18, screenBox.getWidth() / 2 - 60, 34 };
        mainPressArea = { screenBox.getCentreX() + 44, knobY + 18, screenBox.getWidth() / 2 - 60, 34 };

        // Knobs 1-8 in two rows, 132 apart, low on the panel (D226, the user's layout): a lower knob's name on two
        // lines ends where the main screen's bezel does.
        const int cell = knobsWidth / 4;
        const int upperRow = top + 179, lowerRow = upperRow + 132;
        for (int k = 0; k < 8; ++k)
        {
            const int row = k / 4, col = k % 4;
            knobs[static_cast<std::size_t>(k)].setBounds(knobsX + col * cell + (cell - 62) / 2, row == 0 ? upperRow : lowerRow, 62, 62);
        }

        // The faders as tall as the two knob rows (D226): at the top their thumb is level with the upper knobs' top
        // edge (a knob draws its circle 4 px in, a fader its thumb 6 px), at the bottom their track ends by the lower
        // knobs' bottom edge; their numbers and names on the lower knobs' lines.
        const int faderCell = faderWidth / 4;
        for (int f = 0; f < 4; ++f)
            faders[static_cast<std::size_t>(f)].setBounds(fadersX + f * faderCell + (faderCell - 32) / 2, upperRow - 2,
                                                          32, lowerRow + 62 - (upperRow - 2));

        // The layer screen above the knobs and the faders, from knob 1 to the Master (D195, grown in D226), down to
        // 26 px above the upper knobs' circles, as far as the knobs keep from the main screen.
        layerScreen = juce::Rectangle<int>(knobs[0].getX(), top, faders[3].getRight() - knobs[0].getX(), upperRow - 22 - top);

        // pads under the screen, knobs and faders
        const int padsY = screenBox.getBottom() + 22;
        const int padsWidth = designWidth - 38 - screenX;
        const int padCell = padsWidth / 8;
        for (int p = 0; p < 8; ++p)
            pads[static_cast<std::size_t>(p)].setBounds(screenX + p * padCell + 8, padsY, padCell - 16, 82);

        // strips reach down to the pads' bottom edge
        const int stripTop = top + 108;
        const int padBottom = padsY + 82;
        strips[0] = { left, stripTop, 54, padBottom - stripTop };
        strips[1] = { left + 64, stripTop, 54, padBottom - stripTop };

        // keys
        keyboard.setBounds(left, padBottom + 30, designWidth - 2 * left, designHeight - padBottom - 30 - 32);
        keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / static_cast<float>(whiteKeys));

        prefs.setBounds(getLocalBounds()); // the dialog covers the panel; its card sits in the middle (D197)
        controllerCard.setBounds(getLocalBounds());
        // the CPU: the layer screen's bottom right corner (D195; beside the main knob from D191)
        cpuArea = layerScreen.reduced(16, 13).removeFromBottom(14).removeFromRight(70); // as paintLayerScreen's info row
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (cpuArea.contains(e.getPosition()))
            prefs.show();
        // A block of a layer's chain: that layer's section, on that slot (D226). A right click keeps the layer's menu.
        if (! e.mods.isPopupMenu())
            for (const auto& block : chainBlocks)
                if (block.area.contains(e.getPosition()))
                {
                    model.openSlot(block.layer, block.section, block.slot);
                    owner.timerCallback();
                    return;
                }
        for (int layer = 0; layer < 2; ++layer)
        {
            if (! layerPanels[static_cast<std::size_t>(layer)].contains(e.getPosition()))
                continue;
            // LAYER A clicked, or the panel right-clicked: the Layers page's actions, for this layer (D196, D226).
            if (e.mods.isPopupMenu() || layerTitles[static_cast<std::size_t>(layer)].contains(e.getPosition()))
                showLayerMenu(layer);
            else
            {
                // Its sound's name or the panel elsewhere: Home on this layer, to choose another sound (D226; it
                // picked the layer only, D195).
                model.openHomeFor(layer);
                owner.timerCallback();
            }
        }
    }

    /** A right click on a layer panel: the Layers page's actions (D196), worded for that layer, whichever layer the
        sections edit. Copying, swapping and the octaves can be undone. */
    void showLayerMenu(int layer)
    {
        const juce::String name = layer == 0 ? "A" : "B";
        juce::PopupMenu menu;
        menu.addSectionHeader("Layer " + name + ": " + (model.layerEmpty(layer) ? juce::String("empty") : model.layerSoundName(layer)));
        const auto actions = model.layerActionNames(layer);
        for (int i = 0; i < actions.size(); ++i)
        {
            if (i == 3 || i == 6)
                menu.addSeparator(); // copy and swap; the other layer from this one; solo and empty
            if (i == 6)
                menu.addItem(i + 1, "Solo " + name, true, owner.synth.soloLayer() == layer); // ticked while solo
            else
                menu.addItem(i + 1, actions[i]);
        }
        juce::Component::SafePointer<Content> self(this);
        // The panel as the target first, for the menu's scale on a hi-DPI screen and, since JUCE 9.0.5, a native
        // window tied to the plugin's; then the layer panel's area, where it opens.
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                                                     .withTargetScreenArea(localAreaToGlobal(layerPanels[static_cast<std::size_t>(layer)])),
                           [self, layer](int chosen)
                           {
                               if (self == nullptr || chosen == 0)
                                   return;
                               self->model.doLayerAction(chosen - 1, layer);
                               self->owner.timerCallback();
                           });
    }

    /** The layer screen (D195): two lit panels, layer A teal and B yellow, each with what it plays; the layer the
        sections edit lit, the other dim; an empty layer says so. Under them the layer mode, the tempo, the voices
        sounding and, in the bottom right corner, the CPU. */
    /** One section's row of a layer's chain (D226): Sound's sources joined by +, Shape's processors by arrows,
        Motion's envelopes and LFOs, the layer's six effects and then M, the chain at the end, which is both layers'.
        A block shows its type (an effect's in four letters), an empty one its slot, dashed; the slot the knobs are
        on is outlined white. A click opens it (InterfaceModel::openSlot). */
    void paintChainRow(juce::Graphics& g, juce::Rectangle<int> row, int layer, int section, bool lit)
    {
        const auto slots = model.slotsOf(section, layer);
        const auto tint = coolsynth::ui::tintColour(InterfaceModel::sectionTint(section));
        const bool effects = section == 3;
        const int count = effects ? 6 : static_cast<int>(slots.size()); // the effects: the layer's six, then M
        const int gap = section <= 1 ? 12 : 3;
        const int endWidth = effects ? 30 : 0;
        const int width = (row.getWidth() - endWidth - (count - 1) * gap) / count;
        const int open = model.openSlotOf(layer, section);
        const juce::String arrow = juce::String(juce::CharPointer_UTF8("\xe2\x86\x92"));
        const juce::String layerName = layer == 0 ? "Layer A" : "Layer B";
        const juce::String dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
        static const juce::StringArray motionNames { "Amp", "Filt", "Mod", "LFO1", "LFO2" };

        auto block = [&](juce::Rectangle<int> box, int index, const juce::String& label, bool on, const juce::String& hint)
        {
            const int key = layer * 100 + section * 10 + index;
            const bool isOpen = index == open || (index >= 6 && open >= 6);
            const auto r = box.toFloat().reduced(0.5f);
            if (on)
            {
                g.setColour(tint.withAlpha(lit ? 0.24f : 0.12f));
                g.fillRoundedRectangle(r, 3.0f);
                g.setColour(tint.withAlpha(lit ? 0.85f : 0.45f));
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
            }
            else
            {
                const float dashes[] { 2.5f, 2.0f };
                juce::Path outline, dashed;
                outline.addRoundedRectangle(r, 3.0f);
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
                g.setColour(colours::screenLine.brighter(0.35f));
                g.fillPath(dashed);
            }
            if (isOpen || key == hoveredBlock)
            {
                g.setColour(isOpen ? colours::screenInk : tint.brighter(0.4f));
                g.drawRoundedRectangle(r, 3.0f, 1.5f);
            }
            g.setFont(mono(9.0f));
            g.setColour(on ? (lit ? colours::screenInk : colours::screenInk.withAlpha(0.6f)) : colours::screenDim.withAlpha(0.7f));
            g.drawFittedText(label, box.reduced(2, 0), juce::Justification::centred, 1, 0.7f);
            chainBlocks.push_back({ box, layer, section, index, key, hint });
        };

        for (int i = 0; i < count && i < static_cast<int>(slots.size()); ++i)
        {
            if (i > 0)
            {
                const auto joint = row.removeFromLeft(gap);
                if (section <= 1)
                {
                    g.setFont(mono(10.0f));
                    g.setColour(colours::screenDim);
                    g.drawText(section == 0 ? juce::String("+") : arrow, joint, juce::Justification::centred);
                }
            }
            const auto& slot = slots[static_cast<std::size_t>(i)];
            const bool motion = section == 2; // envelopes and LFOs have one type each and are always there
            const juce::String label = motion ? motionNames[i] : (slot.empty ? slot.code : (effects ? shortType(slot.typeName) : slot.typeName));
            const juce::String what = slot.code + " " + (motion ? slot.label : (slot.empty ? juce::String("empty") : slot.typeName));
            block(row.removeFromLeft(width), i, label, motion || ! slot.empty,
                  layerName + dot + what + dot + (motion ? juce::String("click: edit it") : juce::String("click: edit it, again: its types")));
        }
        if (effects)
        {
            g.setFont(mono(10.0f));
            g.setColour(colours::screenDim);
            g.drawText(arrow, row.removeFromLeft(12), juce::Justification::centred);
            block(row, 6, "M", true, "M: the chain at the end, F1-F6, both layers'" + dot + "click: edit it");
        }
    }

    void paintLayerScreen(juce::Graphics& g)
    {
        chainBlocks.clear(); // drawn again below, with the places a click opens
        const auto box = layerScreen.toFloat();
        g.setColour(colours::bezel); // the main screen's bezel (D226)
        g.fillRoundedRectangle(box, 12.0f);
        g.setColour(colours::panelEdge);
        g.drawRoundedRectangle(box.reduced(0.5f), 12.0f, 1.0f);
        const auto glass = box.reduced(8.0f);
        g.setColour(colours::screen);
        g.fillRoundedRectangle(glass, 6.0f);
        g.setColour(colours::screenLine);
        g.drawRoundedRectangle(glass.reduced(0.5f), 6.0f, 1.0f);

        auto inner = layerScreen.reduced(16, 13);
        auto infoRow = inner.removeFromBottom(14);
        inner.removeFromBottom(7);
        const int gap = 8;
        const int panelWidth = (inner.getWidth() - gap) / 2;
        for (int layer = 0; layer < 2; ++layer)
        {
            auto panel = (layer == 0 ? inner.withWidth(panelWidth) : inner.withTrimmedLeft(panelWidth + gap));
            layerPanels[static_cast<std::size_t>(layer)] = panel;
            const bool lit = model.part() == layer && ! model.editingMaster(); // the end chain is neither layer's (D195)
            const bool empty = model.layerEmpty(layer);
            const auto tint = coolsynth::ui::tintColour(layer == 0 ? Tint::home : Tint::partB);
            const auto r = panel.toFloat();
            if (lit)
            {
                g.setColour(tint.withAlpha(0.16f));
                g.fillRoundedRectangle(r, 5.0f);
            }
            const bool hover = hoveredPanel == layer; // (D204)
            g.setColour(lit ? tint : (hover ? tint.withAlpha(0.6f) : colours::screenLine.brighter(0.25f)));
            g.drawRoundedRectangle(r.reduced(0.75f), 5.0f, lit || hover ? 1.5f : 1.0f);

            // the head: LAYER A, the sound's name; SOLO or MUTED at the right while a layer is heard alone (D196)
            auto text = panel.reduced(8, 6);
            auto head = text.removeFromTop(16);
            g.setFont(mono(10.0f, true));
            g.setColour(lit ? tint : tint.withMultipliedBrightness(0.55f).withMultipliedSaturation(0.6f));
            const juce::String title = layer == 0 ? "LAYER A" : "LAYER B";
            const auto titleArea = head.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), title) + 8);
            layerTitles[static_cast<std::size_t>(layer)] = titleArea; // a click: the layer's menu (D226)
            g.drawText(title, titleArea, juce::Justification::centredLeft);
            if (const int solo = owner.synth.soloLayer(); solo >= 0)
            {
                const juce::String word = solo == layer ? "SOLO" : "MUTED";
                g.drawText(word, head.removeFromRight(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), word) + 4),
                           juce::Justification::centredRight);
            }
            g.setFont(sans(13.5f, ! empty));
            g.setColour(empty ? colours::screenDim.withAlpha(lit ? 0.9f : 0.6f) : (lit ? colours::screenInk : colours::screenDim));
            g.drawFittedText(empty ? juce::String("empty") : model.layerSoundName(layer), head, juce::Justification::centredLeft, 1, 0.8f);

            // The layer's chain (D226, the user's choice): a row per section in its pad's colour, every slot a block.
            text.removeFromTop(4);
            for (int section = 0; section < InterfaceModel::sectionCount; ++section)
            {
                paintChainRow(g, text.removeFromTop(17), layer, section, lit);
                text.removeFromTop(3);
            }
        }

        // the info row: mode, tempo, voices; the CPU at the right, amber from 60%, red from 90%
        const float percent = juce::jlimit(0.0f, 100.0f, owner.synth.cpuLoad().percent[0]);
        const juce::String dot(juce::CharPointer_UTF8(" \xc2\xb7 "));
        if (model.editingMaster()) // both layers dark: the chain at the end is the whole sound's
        {
            g.setFont(mono(10.0f, true));
            g.setColour(colours::screenInk);
            const juce::String master = "MASTER";
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), master) + 8;
            g.drawText(master, infoRow.removeFromLeft(w), juce::Justification::centredLeft);
        }
        g.setFont(mono(10.0f));
        if (layerHint.isNotEmpty()) // the mouse on this screen: what it points at, here (D226, the user's choice)
        {
            g.setColour(colours::screenInk);
            g.drawFittedText(layerHint, infoRow.withTrimmedRight(cpuArea.getWidth() + 6), juce::Justification::centredLeft, 1, 0.8f);
        }
        else
        {
            g.setColour(colours::screenDim);
            g.drawText(model.layerModeText() + dot + juce::String(juce::roundToInt(owner.synth.tempoBpm())) + " BPM" + dot
                           + juce::String(owner.synth.voicesPlaying()) + " of " + juce::String(owner.synth.voiceLimit()) + " voices",
                       infoRow.withTrimmedRight(cpuArea.getWidth()), juce::Justification::centredLeft);
        }
        auto cpu = infoRow.removeFromRight(cpuArea.getWidth()).toFloat();
        g.setFont(mono(11.0f));
        g.setColour(percent >= 90.0f ? coolsynth::ui::tintColour(Tint::record)
                                     : (percent >= 60.0f ? coolsynth::ui::tintColour(Tint::sound) : colours::screenDim));
        const auto number = juce::String(juce::roundToInt(percent)) + "%";
        const auto numberWidth = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), number));
        g.drawText(number, cpu.removeFromRight(numberWidth + 2.0f), juce::Justification::centredRight);
        cpu.removeFromRight(5.0f);
        g.setFont(mono(9.0f));
        g.setColour(colours::screenDim.darker(0.4f));
        g.drawText("CPU", cpu, juce::Justification::centredRight);
    }

private:
    PanelEditor& owner;
    InterfaceModel& model;
    Screen screen;
    Knob mainKnob;
    std::array<Knob, 8> knobs;
    std::array<juce::String, 8> knobLabels;
    std::array<bool, 8> knobIdle {}; // knobs that do nothing now, drawn dimmed (D201)
    std::array<HoverWatcher, 4> faderWatchers;  // (D204)
    std::array<HoverWatcher, 4> buttonWatchers; // Prefs, Hold, Oct -, Oct +
    int hoveredPanel = -1;
    bool hoveredCpu = false;
    std::array<PanelFader, 4> faders;
    static constexpr double masterBoostTravel = 0.25; // the Master's track beyond its top, +12 dB at its end (D225)
    std::array<Pad, 8> pads;
    juce::TextButton options, hold, octDown, octUp;
    FlatKeyboard keyboard;
    juce::Rectangle<int> screenBox, chordPrint, transposePrint, mainTurnArea, mainPressArea, cpuArea, layerScreen;
    std::array<juce::Rectangle<int>, 2> layerPanels; // the layer screen's two panels, clickable (D195)
    struct ChainBlock
    {
        juce::Rectangle<int> area;
        int layer = 0, section = 0, slot = 0, key = 0;
        juce::String hint;
    };
    std::vector<ChainBlock> chainBlocks; // the layer screen's chain, each block clickable (D226)
    int hoveredBlock = -1;               // its key (layer * 100 + section * 10 + slot) while the mouse is on it
    std::array<juce::Rectangle<int>, 2> layerTitles; // LAYER A and LAYER B: a click opens the layer's menu (D226)
    juce::String layerHint;              // what the mouse is on in the layer screen, said in its info row (D226)
    PrefsCard prefs { owner.synth };                 // over everything, hidden until Prefs is pressed (D197)
    ControllerCard controllerCard { owner.synth };   // the controller setup, from Prefs or a right click (D211)
    bool learningHere = false;                       // a right click's Learn is listening (D211)
    int learnToken = 0;                              // and which learning it is (review of D211)
    juce::uint32 noticeUntil = 0;                    // when the screen's notice goes; 0: it stays
    static inline int shownSetupNotice = 0;          // the setup's last notice shown, by any panel in this program
    std::array<juce::Rectangle<int>, 2> strips;
    bool updatingFaders = false;
    int shownPopup = -1; // the model's last pop-up already shown in a knob (D183)
    static constexpr int firstKey = 36, shownKeySpan = 60, whiteKeys = 36; // C2 to C7, five octaves (D192)
    int shownStart = firstKey; // the lowest key the screen shows
    std::int64_t shownMainTurns = 0, shownPageTurns = 0; // the model's turn counts the knobs show (D193)
};

// ---- The editor ---------------------------------------------------------------------------------------

PanelEditor::PanelEditor(SynthAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor), synth(processor), model(processor.getInterfaceModel())
{
    content = std::make_unique<Content>(*this);
    addAndMakeVisible(*content);
    // In a DAW, the CoolSynth whose window opened last drives the MiniLab (D211).
    synth.claimMiniLab();
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    if (auto* sizeLimits = getConstrainer())
    {
        sizeLimits->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
        sizeLimits->setSizeLimits(designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    }
    setSize(designWidth, designHeight);
    content->refresh();
    startTimerHz(30);
}

PanelEditor::~PanelEditor() = default;

void PanelEditor::showControllerSetup(bool stepByStep) { content->showControllerCard(stepByStep); }
void PanelEditor::showPreferences() { content->showPreferences(); }

bool PanelEditor::shiftHeld() const noexcept
{
    // the on-screen Shift (clicked or the computer's Shift key) or the MiniLab's own
    return shiftKeyDown || model.hardwareShiftHeld();
}

void PanelEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff131619)); }

void PanelEditor::resized()
{
    const float scale = juce::jmin(static_cast<float>(getWidth()) / designWidth, static_cast<float>(getHeight()) / designHeight);
    content->setTransform(juce::AffineTransform::scale(scale));
    content->setBounds(0, 0, designWidth, designHeight);
}

void PanelEditor::timerCallback()
{
    // the model changes from the panel, the MiniLab and presets; a light redraw at 30 Hz keeps all in step
    juce::ignoreUnused(shownRevision);
    content->refresh();
}

bool PanelEditor::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    // In a list (Home's rooms and sounds, a type list) up and down move through it; in a section's map they turn
    // knob 5's pages. Escape and Backspace go back one level; Page Up and Down move five rows, Home and End to the
    // ends (D181).
    if (key == juce::KeyPress::escapeKey && content->closePrefs())
        return true;
    const bool listOnScreen = model.typeListOpen() || model.view() == InterfaceModel::View::home;
    if (code == juce::KeyPress::rightKey) model.turnMain(1);
    else if (code == juce::KeyPress::leftKey) model.turnMain(-1);
    else if (code == juce::KeyPress::upKey) listOnScreen ? model.turnMain(-1) : model.turnPageKnob(1);
    else if (code == juce::KeyPress::downKey) listOnScreen ? model.turnMain(1) : model.turnPageKnob(-1);
    else if (code == juce::KeyPress::pageUpKey) model.turnMain(-5);
    else if (code == juce::KeyPress::pageDownKey) model.turnMain(5);
    else if (code == juce::KeyPress::homeKey) model.turnMain(-10000);
    else if (code == juce::KeyPress::endKey) model.turnMain(10000);
    else if (code == juce::KeyPress::escapeKey || code == juce::KeyPress::backspaceKey) model.back();
    else if (code == juce::KeyPress::returnKey) model.pressMain();
    else if (code >= '1' && code <= '8') model.pressPad(code - '1', shiftHeld());
    else
        return false;
    content->refresh();
    return true;
}

bool PanelEditor::keyStateChanged(bool) { return false; }

void PanelEditor::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    shiftKeyDown = modifiers.isShiftDown();
    content->refresh();
}

// ---- Options (D174) ------------------------------------------------------------------------------------

juce::String PanelEditor::cpuSummary(const SynthAudioProcessor& synth)
{
    const auto load = synth.cpuLoad();
    juce::String text = "CPU " + juce::String(juce::roundToInt(load.percent[0])) + "%, slowest block "
                        + juce::String(juce::roundToInt(100.0f * load.worstBlock)) + "%";
    if (load.threads > 1)
    {
        text << ", helpers";
        for (int t = 1; t < load.threads; ++t)
            text << " " << juce::roundToInt(load.percent[static_cast<std::size_t>(t)]);
        text << "%";
    }
    return text;
}

void PanelEditor::showOptions(SynthAudioProcessor& synth, juce::Component& target)
{
    const bool multiCore = synth.multiCore();
    const auto& workers = coolsynth::synth::v3::AudioWorkers::instance();
    juce::PopupMenu menu;
    menu.addItem(9, "Save sound...");
    menu.addSeparator();
    if (synth.showAudioSettings)
    {
        menu.addItem(1, "Audio and MIDI settings...");
        menu.addSeparator();
    }
    menu.addSectionHeader("Multi-core");
    menu.addItem(2, "Auto: sounds that overlap play on several cores", true, multiCore);
    menu.addItem(3, "Off: everything on the audio thread", true, ! multiCore);
    menu.addSeparator();
    menu.addSectionHeader("Velocity curve");
    const int curve = synth.velocityCurve();
    menu.addItem(6, "Soft: a light touch plays loud", true, curve == SynthAudioProcessor::softCurve);
    menu.addItem(7, "Normal: as played", true, curve == SynthAudioProcessor::normalCurve);
    menu.addItem(8, "Hard: loud takes a firm touch", true, curve == SynthAudioProcessor::hardCurve);
    menu.addSeparator();
    menu.addSectionHeader("The last second");
    menu.addItem(4, cpuSummary(synth) + " of the time", false, false);
    menu.addItem(5, "This computer: " + workers.cpu().describe() + "; " + juce::String(workers.workerCount())
                        + (workers.workerCount() == 1 ? " helper" : " helpers"), false, false);
    juce::Component::SafePointer<juce::Component> safeTarget(&target);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
                       [&synth, safeTarget](int chosen)
                       {
                           if (safeTarget == nullptr)
                               return; // the window closed meanwhile
                           if (chosen == 1 && synth.showAudioSettings)
                               synth.showAudioSettings();
                           else if (chosen == 2 || chosen == 3)
                               synth.chooseMultiCore(chosen == 2);
                           else if (chosen >= 6 && chosen <= 8)
                               synth.chooseVelocityCurve(chosen - 6);
                           else if (chosen == 9)
                               askToSaveSound(synth, *safeTarget);
                       });
}

void PanelEditor::askToSaveSound(SynthAudioProcessor& synth, juce::Component& near)
{
    // A name for the sound as it is now (D180); the name it has is offered. Async: nothing waits on the window.
    auto* window = new juce::AlertWindow("Save sound", "A name for this sound in My Sounds:", juce::MessageBoxIconType::NoIcon, &near);
    window->addTextEditor("name", synth.getPatchName());
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::Component> safeNear(&near);
    window->enterModalState(true, juce::ModalCallbackFunction::create([&synth, window, safeNear](int result)
    {
        if (result != 1 || safeNear == nullptr)
            return;
        const auto name = window->getTextEditorContents("name");
        const auto saved = [&synth, safeNear](SynthAudioProcessor::SaveResult r, const juce::String& what)
        {
            if (r == SynthAudioProcessor::SaveResult::failed && safeNear != nullptr)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save sound",
                                                       "\"" + what + "\" could not be saved in " + coolsynth::presets::MySounds::folder().getFullPathName() + ".");
            else if (r == SynthAudioProcessor::SaveResult::saved)
                synth.getInterfaceModel().refreshMySounds();
        };
        const auto result1 = synth.saveMySound(name, false);
        if (result1 != SynthAudioProcessor::SaveResult::exists)
        {
            saved(result1, name);
            return;
        }
        // That name is taken: replace it, or not. Each button's result code is explicit (JUCE 9 deprecates the
        // OK/Cancel shortcut that numbered them by position).
        juce::AlertWindow::showAsync(juce::MessageBoxOptions {}
                                         .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Save sound")
                                         .withMessage("My Sounds already has \"" + coolsynth::presets::MySounds::cleanName(name) + "\". Replace it?")
                                         .withButton(juce::MessageBoxButtonOptions("Replace", 1).withRole(juce::MessageBoxButtonRole::destroy))
                                         .withButton(juce::MessageBoxButtonOptions("Cancel", 0).withRole(juce::MessageBoxButtonRole::cancel))
                                         .withAssociatedComponent(safeNear.getComponent()),
                                     [&synth, name, saved](int replace)
                                     {
                                         if (replace == 1)
                                             saved(synth.saveMySound(name, true), name);
                                     });
    }), true);
}
