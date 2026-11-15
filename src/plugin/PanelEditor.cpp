#include "PanelEditor.h"

#include "SynthAudioProcessor.h"
#include "interface/InterfaceModel.h"
#include "presets/MySounds.h"
#include "synth/v3/AudioWorkers.h"

namespace
{
    using coolsynth::ui::InterfaceModel;
    using coolsynth::ui::Tint;

    // The dark panel of the mock-up (docs/ui-v3-mockups-2.html).
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
}

// ---- A knob that turns in detents --------------------------------------------------------------------

class PanelEditor::Knob final : public juce::Component
{
public:
    std::function<void(int)> onSteps;
    std::function<void()> onPress;
    float value = 0.0f;
    bool showsValue = false; // the pointer shows the value; otherwise it spins with the turns
    juce::Colour ring;       // transparent: the plain edge
    bool navigation = false; // knob 5's blue ring
    bool isMain = false;

    void mouseDown(const juce::MouseEvent& e) override
    {
        lastY = e.position.y;
        moved = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
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
        if (! moved && onPress)
            onPress();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (const int steps = wheelSteps(wheel, wheelSum); steps != 0)
            turn(steps);
    }

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
        if (navigation)
        {
            g.setColour(colours::link.withAlpha(0.45f));
            g.drawEllipse(circle.expanded(2.0f), 3.0f);
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

    void mouseDown(const juce::MouseEvent&) override
    {
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
            g.setColour(colour.withAlpha(0.35f));
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
        bool over = false;
        for (const auto& area : cardAreas)
            over = over || area.contains(e.getPosition());
        for (const auto& hit : hits)
            over = over || hit.area.contains(e.getPosition());
        setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

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
        hits.push_back({ chip, [this] { model.back(); } });
        const int partWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), partText) + 6;
        hits.push_back({ header.withTrimmedLeft(header.getWidth() - partWidth), [this] { model.togglePart(); } });

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
                             [this, index] { model.clickTrail(static_cast<int>(index)); } });
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
            g.setColour(view.pending ? pageTint : colours::screenDim);
            g.drawText(view.pending ? juce::String("CLICK TO LOAD") : view.role.toUpperCase(), card.removeFromTop(12),
                       juce::Justification::centredLeft);
            g.setFont(mono(11.0f));
            g.setColour(view.active ? colours::screenInk : colours::screenLine.brighter(0.3f));
            g.drawText(view.name, card.removeFromTop(15), juce::Justification::centredLeft);
            g.setColour(view.active ? pageTint : colours::screenLine);
            g.drawText(view.active ? view.value : juce::String(), card.removeFromTop(15), juce::Justification::centredLeft);
            auto bar = card.removeFromTop(3).toFloat();
            g.setColour(colours::screenLine);
            g.fillRoundedRectangle(bar, 1.5f);
            if (view.active)
            {
                g.setColour(pageTint);
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
                g.setColour(on ? pageTint : colours::screenLine.brighter(0.2f));
                g.drawRoundedRectangle(tab, 3.0f, 1.0f);
                g.setColour(on ? colours::screenInk : colours::screenDim);
                g.drawText(names[i], tab, juce::Justification::centred);
                hits.push_back({ tab.toNearestInt(), [this, i] { model.clickPage(i); } });
                tx += w + 4.0f;
            }
        }

        // the hint line
        const auto hint = model.hint();
        if (hint.isNotEmpty())
        {
            g.setFont(mono(10.0f));
            g.setColour(colours::screenDim);
            g.drawText(hint, area.removeFromBottom(16), juce::Justification::centredLeft);
        }
        area.removeFromTop(6);

        if (home || typeList)
            paintList(g, area, list, home ? tint : tint);
        else
            paintSection(g, area, tint);
    }

private:
    void paintList(juce::Graphics& g, juce::Rectangle<int> area, const coolsynth::ui::ListView& list, juce::Colour tint)
    {
        const int rowHeight = 19;
        const int visible = juce::jmax(1, area.getHeight() / rowHeight);
        const int first = juce::jlimit(0, juce::jmax(0, list.rows.size() - visible), list.cursor - visible / 2);
        g.setFont(mono(12.0f));
        for (int i = first; i < juce::jmin(list.rows.size(), first + visible); ++i)
        {
            auto row = area.removeFromTop(rowHeight);
            hits.push_back({ row, [this, i] { model.clickListRow(i); } });
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
                hits.push_back({ box.toNearestInt(), [this, index] { model.clickSlot(index); } });
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
    };

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
        mainKnob.spinsItself = false; // it follows every main-knob turn, the wheel's and the MiniLab's too (D193)
        mainKnob.onSteps = [this](int s) { model.turnMain(s); owner.timerCallback(); };
        mainKnob.onPress = [this] { model.pressMain(); owner.timerCallback(); };
        addAndMakeVisible(mainKnob);

        for (int k = 0; k < 8; ++k)
        {
            auto& knob = knobs[static_cast<std::size_t>(k)];
            const int number = k + 1;
            knob.navigation = number == 5;
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
            addAndMakeVisible(knob);
        }

        for (int f = 0; f < 4; ++f)
        {
            auto& fader = faders[static_cast<std::size_t>(f)];
            fader.setSliderStyle(juce::Slider::LinearVertical);
            fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            fader.setRange(0.0, 1.0);
            fader.setColour(juce::Slider::trackColourId, colours::ink.withAlpha(0.8f));
            fader.setColour(juce::Slider::backgroundColourId, colours::knob);
            fader.setColour(juce::Slider::thumbColourId, colours::ink);
            fader.onValueChange = [this, f]
            {
                if (! updatingFaders)
                    model.moveFader(f, static_cast<float>(faders[static_cast<std::size_t>(f)].getValue()), false);
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
            addAndMakeVisible(pad);
        }

        // Options where Shift was (D185): the screen no longer keeps a Shift of its own; the computer's Shift key and
        // the MiniLab's still turn the knobs finely.
        options.setButtonText("Prefs"); // "Options" had to be squeezed to fit Shift's width
        options.onClick = [this] { PanelEditor::showOptions(owner.synth, options); };
        options.setTooltip("Save sound, multi-core, velocity curve, the CPU's work; audio and MIDI in the app");
        for (auto* button : { &options, &hold, &octDown, &octUp })
        {
            button->setColour(juce::TextButton::buttonColourId, colours::knob);
            button->setColour(juce::TextButton::buttonOnColourId, colours::ink);
            button->setColour(juce::TextButton::textColourOffId, colours::ink);
            button->setColour(juce::TextButton::textColourOnId, colours::background);
            addAndMakeVisible(*button);
        }
        hold.setButtonText("Hold");
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

        setSize(designWidth, designHeight);
    }

    void refresh()
    {
        screen.repaint();
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
            const int low = model.miniLabLowKey(), high = low + InterfaceModel::miniLabKeySpan;
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
            knobLabels[static_cast<std::size_t>(k)] = view.name;
            knob.repaint();
        }
        updatingFaders = true;
        for (int f = 0; f < 4; ++f)
        {
            auto& fader = faders[static_cast<std::size_t>(f)];
            if (! fader.isMouseButtonDown())
                fader.setValue(model.faderValue(f), juce::dontSendNotification);
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

        // the screen's black surround
        g.setColour(juce::Colour(0xff0b0d10));
        g.fillRoundedRectangle(screenBox.toFloat(), 12.0f);

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
        // A thin line over the MiniLab's 25 keys, as far as its notes tell (D192), named in small print.
        if (model.hardwareConnected())
        {
            const int low = model.miniLabLowKey(), high = low + InterfaceModel::miniLabKeySpan;
            const auto first = keyboard.getRectangleForKey(juce::jmax(low, shownStart));
            const auto last = keyboard.getRectangleForKey(juce::jmin(high, shownStart + shownKeySpan));
            const float x0 = static_cast<float>(keyboard.getX()) + first.getX();
            const float x1 = static_cast<float>(keyboard.getX()) + last.getRight();
            const float y = static_cast<float>(keyboard.getY()) - 8.0f;
            g.setColour(coolsynth::ui::tintColour(Tint::home).withAlpha(0.85f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x0 + 1.0f, y, x1 - x0 - 2.0f, 3.0f), 1.5f);
            g.setFont(sans(10.5f));
            g.setColour(colours::print);
            g.drawText("MiniLab keys", juce::Rectangle<float>(x0, y - 16.0f, x1 - x0, 14.0f), juce::Justification::centred);
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
            g.setColour(knobLabels[static_cast<std::size_t>(k)] == emDash ? colours::muted : colours::ink);
            g.drawFittedText(knobLabels[static_cast<std::size_t>(k)], b.getX() - 16, b.getBottom() + 17, b.getWidth() + 32, 36,
                             juce::Justification::centredTop, 2);
        }

        // fader numbers and names
        for (int f = 0; f < 4; ++f)
        {
            const auto b = faders[static_cast<std::size_t>(f)].getBounds();
            g.setFont(sans(11.0f, true));
            g.setColour(colours::print);
            g.drawText(juce::String(f + 1), b.getX() - 10, b.getBottom() + 2, b.getWidth() + 20, 15, juce::Justification::centred);
            g.setFont(sans(12.0f));
            g.setColour(colours::ink);
            g.drawText(model.faderName(f), b.getX() - 18, b.getBottom() + 17, b.getWidth() + 36, 16, juce::Justification::centred);
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

        // knobs 1-8 in two rows
        const int cell = knobsWidth / 4;
        for (int k = 0; k < 8; ++k)
        {
            const int row = k / 4, col = k % 4;
            knobs[static_cast<std::size_t>(k)].setBounds(knobsX + col * cell + (cell - 62) / 2, top + (row == 0 ? 104 : 236), 62, 62);
        }
        // The layer screen above knobs 1-4, as wide as from knob 1 to knob 4 (D195); the rows moved down for it.
        layerScreen = juce::Rectangle<int>(knobs[0].getX(), top, knobs[3].getRight() - knobs[0].getX(), 82);

        // faders
        const int faderCell = faderWidth / 4;
        for (int f = 0; f < 4; ++f)
            faders[static_cast<std::size_t>(f)].setBounds(fadersX + f * faderCell + (faderCell - 32) / 2, top + 10, 32, 330);

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

        // the CPU: the layer screen's bottom right corner (D195; beside the main knob from D191)
        cpuArea = layerScreen.reduced(12, 8).removeFromBottom(14).removeFromRight(70);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (cpuArea.contains(e.getPosition()))
            PanelEditor::showOptions(owner.synth, options);
        // A layer panel picks the layer the sections edit, as Home pressed again does (D195).
        for (int layer = 0; layer < 2; ++layer)
            if (layerPanels[static_cast<std::size_t>(layer)].contains(e.getPosition()) && model.part() != layer)
            {
                model.togglePart();
                owner.timerCallback();
            }
    }

    /** The layer screen (D195): two lit panels, layer A teal and B yellow, each with what it plays; the layer the
        sections edit lit, the other dim; an empty layer says so. Under them the layer mode, the tempo, the voices
        sounding and, in the bottom right corner, the CPU. */
    void paintLayerScreen(juce::Graphics& g)
    {
        const auto box = layerScreen.toFloat();
        g.setColour(juce::Colour(0xff0b0d10)); // the main screen's black surround
        g.fillRoundedRectangle(box, 10.0f);
        const auto glass = box.reduced(5.0f);
        g.setColour(colours::screen);
        g.fillRoundedRectangle(glass, 6.0f);
        g.setColour(colours::screenLine);
        g.drawRoundedRectangle(glass.reduced(0.5f), 6.0f, 1.0f);

        auto inner = layerScreen.reduced(12, 9);
        auto infoRow = inner.removeFromBottom(14);
        inner.removeFromBottom(5);
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
            g.setColour(lit ? tint : colours::screenLine.brighter(0.25f));
            g.drawRoundedRectangle(r.reduced(0.75f), 5.0f, lit ? 1.5f : 1.0f);

            auto text = panel.reduced(8, 4);
            g.setFont(mono(10.0f, true));
            g.setColour(lit ? tint : tint.withMultipliedBrightness(0.55f).withMultipliedSaturation(0.6f));
            auto head = text.removeFromTop(13);
            g.drawText(layer == 0 ? "LAYER A" : "LAYER B", head, juce::Justification::centredLeft);
            if (const int solo = owner.synth.soloLayer(); solo >= 0) // a layer heard alone (D196)
                g.drawText(solo == layer ? "SOLO" : "MUTED", head, juce::Justification::centredRight);
            g.setFont(sans(13.5f, ! empty));
            g.setColour(empty ? colours::screenDim.withAlpha(lit ? 0.9f : 0.6f) : (lit ? colours::screenInk : colours::screenDim));
            g.drawFittedText(empty ? juce::String("empty") : model.layerSoundName(layer), text, juce::Justification::centredLeft, 1, 0.8f);
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
        g.setColour(colours::screenDim);
        g.drawText(model.layerModeText() + dot + juce::String(juce::roundToInt(owner.synth.tempoBpm())) + " BPM" + dot
                       + juce::String(owner.synth.voicesPlaying()) + " voices",
                   infoRow.withTrimmedRight(cpuArea.getWidth()), juce::Justification::centredLeft);
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
    std::array<juce::Slider, 4> faders;
    std::array<Pad, 8> pads;
    juce::TextButton options, hold, octDown, octUp;
    FlatKeyboard keyboard;
    juce::Rectangle<int> screenBox, chordPrint, transposePrint, mainTurnArea, mainPressArea, cpuArea, layerScreen;
    std::array<juce::Rectangle<int>, 2> layerPanels; // the layer screen's two panels, clickable (D195)
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
        // That name is taken: replace it, or not.
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Save sound",
                                           "My Sounds already has \"" + coolsynth::presets::MySounds::cleanName(name) + "\". Replace it?",
                                           "Replace", "Cancel", safeNear.getComponent(),
                                           juce::ModalCallbackFunction::create([&synth, name, saved](int replace)
                                           {
                                               if (replace == 1)
                                                   saved(synth.saveMySound(name, true), name);
                                           }));
    }), true);
}
