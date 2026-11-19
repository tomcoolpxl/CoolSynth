#pragma once

#include <array>
#include <optional>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

/*
    The MiniLab 3's DAW program, as tested on the device on 2026-11-14 (docs/minilab3-screen.md,
    decisions D146-D147): connect, pad colours, the scroll screen, knob and fader pop-ups, and the
    messages its controls send. Pure functions, no device access.
*/
namespace coolsynth::midi::minilab3
{
    /** The port the DAW program talks on. */
    inline constexpr char portName[] = "Minilab3 MIDI";

    inline bool isDawPort(const juce::String& deviceName)
    {
        const auto name = deviceName.toLowerCase();
        return name.contains("minilab3") && ! name.contains("din") && ! name.contains("mcu") && ! name.contains("alv");
    }

    inline juce::MidiMessage sysex(std::initializer_list<juce::uint8> body)
    {
        std::vector<juce::uint8> data { 0x00, 0x20, 0x6B, 0x7F, 0x42 };
        data.insert(data.end(), body.begin(), body.end());
        return juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));
    }

    inline juce::MidiMessage connect()    { return sysex({ 0x02, 0x00, 0x40, 0x6A, 0x21 }); }
    inline juce::MidiMessage disconnect() { return sysex({ 0x02, 0x00, 0x40, 0x6A, 0x20 }); }
    // Leaving, as Arturia's and Bitwig's scripts do (D216): the screen cleared, then the disconnect in both forms the
    // scripts use (Ableton's `02 00`, Bitwig's and FL's `02 02`). No message switches the MiniLab's program: it stays
    // in the DAW program until Shift + pad 3.
    inline juce::MidiMessage clearScreen()      { return sysex({ 0x04, 0x01, 0x60, 0x0A, 0x0A, 0x5F, 0x51, 0x00 }); }
    inline juce::MidiMessage disconnectOther()  { return sysex({ 0x02, 0x02, 0x40, 0x6A, 0x20 }); }

    /** Text the screen can show: printable ASCII only ("·" becomes "-"), cut to maxChars. */
    inline std::vector<juce::uint8> screenText(const juce::String& text, int maxChars)
    {
        std::vector<juce::uint8> bytes;
        for (auto c : text)
        {
            if (static_cast<int>(bytes.size()) >= maxChars)
                break;
            if (c == 0x00B7 || c == 0x2014 || c == 0x2013)
                c = '-';
            else if (c == 0x2039)
                c = '<';
            else if (c == 0x203A)
                c = '>';
            if (c >= 0x20 && c < 0x7F)
                bytes.push_back(static_cast<juce::uint8>(c));
        }
        return bytes;
    }

    inline juce::uint8 to7Bit(juce::uint8 component) noexcept { return static_cast<juce::uint8>(component >> 1); }

    /** One pad of bank A (0-7) in any colour, kept until changed. */
    inline juce::MidiMessage padColour(int pad, juce::Colour colour)
    {
        return sysex({ 0x02, 0x02, 0x16, static_cast<juce::uint8>(0x34 + juce::jlimit(0, 7, pad)),
                       to7Bit(colour.getRed()), to7Bit(colour.getGreen()), to7Bit(colour.getBlue()) });
    }

    namespace detail
    {
        inline juce::MidiMessage screen(std::vector<juce::uint8> options, const juce::String& line1, int width1,
                                        const juce::String& line2, int width2)
        {
            std::vector<juce::uint8> data { 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x04, 0x02, 0x60 };
            data.insert(data.end(), options.begin(), options.end());
            data.push_back(0x01);
            const auto a = screenText(line1, width1);
            data.insert(data.end(), a.begin(), a.end());
            data.push_back(0x00);
            data.push_back(0x02);
            const auto b = screenText(line2, width2);
            data.insert(data.end(), b.begin(), b.end());
            data.push_back(0x00);
            return juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));
        }
    }

    /** The scroll screen: line 1 small (18 characters), line 2 big (16) between the device's arrows, and a
        marker at position/127. Stays until replaced. */
    inline juce::MidiMessage scrollScreen(const juce::String& smallLine, const juce::String& bigLine, int position)
    {
        return detail::screen({ 0x1F, 0x06, 0x00, static_cast<juce::uint8>(juce::jlimit(0, 127, position)), 0x00, 0x7F, 0x00, 0x00 },
                              smallLine, 18, bigLine, 16);
    }

    /** A pop-up with a knob graphic (fader: a bar), name and value; the device draws the graphic only while a
        control moves, and hides the pop-up again by itself. */
    inline juce::MidiMessage popup(bool fader, const juce::String& name, const juce::String& value, float normalised)
    {
        const auto v = static_cast<juce::uint8>(juce::jlimit(0, 127, juce::roundToInt(normalised * 127.0f)));
        return detail::screen({ 0x1F, static_cast<juce::uint8>(fader ? 0x04 : 0x03), 0x02, v, 0x00, 0x00 }, name, 16, value, 18);
    }

    /** The full screen: top line big (16 characters), bottom line small (18), no arrows, no marker. Stays until
        replaced. Firmware 1.2.0 shows pop-ups only while a control moves; this it always shows (D191). */
    inline juce::MidiMessage fullScreen(const juce::String& bigLine, const juce::String& smallLine)
    {
        return detail::screen({ 0x1F, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00 }, bigLine, 16, smallLine, 18);
    }

    // ---- What the controls send (channel 1 unless noted) -------------------------------------------

    inline constexpr std::array<int, 8> knobControllers { 86, 87, 89, 90, 110, 111, 116, 117 };
    inline constexpr std::array<int, 4> faderControllers { 14, 15, 30, 31 };
    inline constexpr int mainTurnController = 28;  // tested: 65 one click right, 62 one click left
    inline constexpr int mainPressController = 118; // 127 pressed, 0 released
    inline constexpr int shiftController = 27;      // tested: 127 pressed, 0 released (Oct and Hold send nothing)
    inline constexpr int padChannel = 10;
    inline constexpr int firstPadNote = 36;

    struct Control
    {
        enum class Kind { knob, fader, mainTurn, mainPress, shift, pad, other };
        Kind kind = Kind::other;
        int index = 0;  // knob 0-7, fader 0-3, pad 0-7
        int value = 0;  // controller value; turn: the signed step count; press/shift/pad: 1 down, 0 up
    };

    /** Decodes a message from the DAW port; Kind::other for anything to pass on to the sound (keys, bend,
        mod wheel, pressure, sustain). */
    inline Control decode(const juce::MidiMessage& message)
    {
        Control control;
        if (message.isController() && message.getChannel() == 1)
        {
            const int cc = message.getControllerNumber(), value = message.getControllerValue();
            for (int i = 0; i < static_cast<int>(knobControllers.size()); ++i)
                if (knobControllers[static_cast<std::size_t>(i)] == cc)
                    return { Control::Kind::knob, i, value };
            for (int i = 0; i < static_cast<int>(faderControllers.size()); ++i)
                if (faderControllers[static_cast<std::size_t>(i)] == cc)
                    return { Control::Kind::fader, i, value };
            if (cc == mainTurnController)
            {
                // Tested on the device: one click right is 65 (faster 66), one click left is 62 (faster 61);
                // 63 never comes. So below 64 the step is value - 63.
                const int steps = value > 64 ? value - 64 : (value < 64 ? value - 63 : 0);
                return { Control::Kind::mainTurn, 0, steps };
            }
            if (cc == mainPressController)
                return { Control::Kind::mainPress, 0, value > 0 ? 1 : 0 };
            if (cc == shiftController)
                return { Control::Kind::shift, 0, value > 0 ? 1 : 0 };
        }
        if ((message.isNoteOn() || message.isNoteOff()) && message.getChannel() == padChannel)
        {
            const int pad = message.getNoteNumber() - firstPadNote;
            if (pad >= 0 && pad < 8)
                return { Control::Kind::pad, pad, message.isNoteOn() ? 1 : 0 };
        }
        return control;
    }
}
