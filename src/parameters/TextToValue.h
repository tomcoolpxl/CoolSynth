#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>

/*
    A typed value read back as the parameter shows it (D228). clap-validator found "1.68 s" read back as 1.68 ms,
    "10 %" as 100 %, and "off" and "A 100%" not at all: JUCE's default reading takes only the number, in the
    parameter's own units, while the displays add units and words. This reads any display by its own function, so
    every parameter's text reads back as the same text, whatever its format:
    - a text the parameter shows somewhere on its travel ("off", "A = B", "A 100%") is that place;
    - otherwise the number with its unit (ms, s, Hz, kHz, %, dB) is found where the display says that number,
      as the displays run one way along the travel.
*/
namespace coolsynth::parameters
{
    /** The amount a display says, in its base unit (seconds, hertz, a fraction for %, dB): "1.68 s" is 1.68,
        "120 ms" 0.12, "12.0 kHz" 12000, "10 %" 0.1, "off" the lowest of all. False when it says no number. */
    inline bool amountOfText(const juce::String& raw, double& amount)
    {
        const auto text = raw.trim().toLowerCase();
        if (text == "off" || text == "-inf" || text == "-inf db")
        {
            amount = -std::numeric_limits<double>::max();
            return true;
        }
        int start = -1;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            const bool digit = juce::CharacterFunctions::isDigit(c);
            const bool leadsNumber = (c == '-' || c == '+' || c == '.') && i + 1 < text.length()
                                     && (juce::CharacterFunctions::isDigit(text[i + 1]) || text[i + 1] == '.');
            if (digit || leadsNumber)
            {
                start = i;
                break;
            }
        }
        if (start < 0)
            return false;
        int end = start + 1;
        while (end < text.length() && (juce::CharacterFunctions::isDigit(text[end]) || text[end] == '.'))
            ++end;
        amount = text.substring(start, end).getDoubleValue();
        const auto unit = text.substring(end).trim();
        if (unit.startsWith("ms"))
            amount *= 0.001;
        else if (unit.startsWith("khz"))
            amount *= 1000.0;
        else if (unit.startsWith("%"))
            amount *= 0.01;
        return true;
    }

    /** The normalised value (0 to 1) whose display, `show(value)`, says `text`; `fallback` when it says nothing
        that display ever shows. */
    inline float valueForText(const juce::String& text, const std::function<juce::String(float)>& show, float fallback)
    {
        const auto wanted = text.trim();
        // The plain reading, when it shows what was typed: exact values stay exact ("0" is 0, not somewhere that
        // also shows "0").
        if (const auto plain = show(fallback).trim(); plain.equalsIgnoreCase(wanted))
            return fallback;
        else if (double typed = 0.0, shown = 0.0;
                 juce::String("+-.0123456789").containsChar(wanted[0]) // a number, not a name with one ("LP24")
                 && amountOfText(wanted, typed) && amountOfText(plain, shown)
                 && std::abs(typed - shown) <= 1.0e-9 * juce::jmax(1.0, std::abs(typed)))
            return fallback; // the same amount, written another way ("-12 dB" for "-12.0 dB")
        // A text shown somewhere on the travel: the middle of the stretch that shows it.
        constexpr int grid = 2000;
        int first = -1, last = -1;
        for (int i = 0; i <= grid; ++i)
            if (show(static_cast<float>(i) / grid).trim().equalsIgnoreCase(wanted))
            {
                if (first < 0)
                    first = i;
                last = i;
            }
            else if (first >= 0)
                break;
        if (first >= 0)
            return static_cast<float>(first + last) / (2.0f * grid);

        // Otherwise by the amount it says, where the display says the same amount.
        double target = 0.0;
        if (! amountOfText(wanted, target))
            return fallback;
        auto amountAt = [&show](float v)
        {
            double a = 0.0;
            return amountOfText(show(v), a) ? a : std::numeric_limits<double>::quiet_NaN();
        };
        const double low = amountAt(0.0f), high = amountAt(1.0f);
        if (std::isnan(low) || std::isnan(high) || low == high)
            return fallback;
        const bool rising = high > low;
        auto atOrPast = [&](float v, bool from)
        {
            const double a = amountAt(v);
            if (std::isnan(a))
                return false;
            return rising == from ? a >= target : a <= target;
        };
        // the first place at or past the amount, and the last place at or before it
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 48; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (atOrPast(mid, true) ? hi : lo) = mid;
        }
        const float start = hi;
        lo = 0.0f;
        hi = 1.0f;
        for (int i = 0; i < 48; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (atOrPast(mid, false) ? lo : hi) = mid;
        }
        const float end = lo;
        return juce::jlimit(0.0f, 1.0f, end >= start ? 0.5f * (start + end) : start);
    }

    /** A float parameter whose typed values read back as it shows them (D228). */
    class FloatParameter : public juce::AudioParameterFloat
    {
    public:
        using juce::AudioParameterFloat::AudioParameterFloat;

        float getValueForText(const juce::String& text) const override
        {
            // JUCE keeps its float parameter's own text functions private: its display through the base class, and
            // its plain reading (the number, in the parameter's units) as the fallback.
            const auto& base = static_cast<const juce::AudioProcessorParameter&>(*this);
            const float plain = convertTo0to1(getNormalisableRange().getRange().clipValue(text.getFloatValue()));
            return valueForText(text, [&base](float v) { return base.getText(v, 1024); }, plain);
        }
    };
}
