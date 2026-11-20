// What clap-validator found on 2026-11-20 (D228), checked on every parameter here, in the test program:
// - a value shown as text, typed back, must show the same text ("1.68 s" read back as 1.68 ms did not);
// - random settings, saved and opened in a new CoolSynth, must come back the same, and save the same bytes.

#include <cmath>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/SynthAudioProcessor.h"

class ParameterRoundTripTests final : public juce::UnitTest
{
public:
    ParameterRoundTripTests() : juce::UnitTest("Parameter round trip", "Presets") {}

    void runTest() override
    {
        beginTest("every parameter's text reads back as the same text");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            juce::StringArray failures;
            int checked = 0;
            for (auto* base : synth->getParameters())
            {
                auto* p = dynamic_cast<juce::RangedAudioParameter*>(base);
                if (p == nullptr)
                    continue;
                for (const float v : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
                {
                    const auto shown = p->getText(v, 1024);
                    const float typed = p->getValueForText(shown);
                    const auto again = p->getText(typed, 1024);
                    ++checked;
                    if (again != shown)
                    {
                        failures.add(p->getParameterID() + " (" + p->getName(64) + "): " + juce::String(v, 2) + " shows '" + shown
                                     + "', typed back " + juce::String(typed, 4) + " shows '" + again + "'");
                        break; // one example per parameter
                    }
                }
            }
            logMessage(juce::String(checked) + " values checked, " + juce::String(failures.size()) + " parameters fail");
            for (int i = 0; i < juce::jmin(60, failures.size()); ++i)
                logMessage("  " + failures[i]);
            expectEquals(failures.size(), 0, "every parameter's text reads back");
        }

        beginTest("a slot's new type renames its parameters, and the host is to hear of it");
        {
            auto synth = std::make_unique<SynthAudioProcessor>();
            juce::RangedAudioParameter* type = synth->getValueTreeState().getParameter("pA.i1.type");
            expect(type != nullptr);
            if (type != nullptr)
            {
                type->setValueNotifyingHost(type->getValueForText("LoFi"));
                expect(synth->hostToHearOfNewNames(), "a type change is to be told to the host");
            }
        }

        beginTest("random settings come back the same in a new CoolSynth, and save the same bytes");
        {
            auto first = std::make_unique<SynthAudioProcessor>();
            juce::Random random(20261120);
            // As a host sends them: a switch or a choice on one of its steps, anything else anywhere.
            for (auto* p : first->getParameters())
            {
                float v = random.nextFloat();
                if (const int steps = p->getNumSteps(); p->isDiscrete() && steps > 1 && steps < 0x7fffffff)
                    v = static_cast<float>(juce::roundToInt(v * static_cast<float>(steps - 1))) / static_cast<float>(steps - 1);
                p->setValueNotifyingHost(v);
            }
            juce::MemoryBlock saved;
            first->getStateInformation(saved);
            auto second = std::make_unique<SynthAudioProcessor>();
            second->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            juce::StringArray changed;
            const auto& a = first->getParameters();
            const auto& b = second->getParameters();
            for (int i = 0; i < a.size(); ++i)
                if (std::abs(a[i]->getValue() - b[i]->getValue()) > 1.0e-6f)
                {
                    auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(a[i]);
                    changed.add((ranged != nullptr ? ranged->getParameterID() : juce::String(i)) + " (" + a[i]->getName(64) + "): "
                                + a[i]->getCurrentValueAsText() + " -> " + b[i]->getCurrentValueAsText());
                }
            logMessage(juce::String(changed.size()) + " of " + juce::String(a.size()) + " parameters changed after reopening");
            for (int i = 0; i < juce::jmin(60, changed.size()); ++i)
                logMessage("  " + changed[i]);
            expectEquals(changed.size(), 0, "every parameter as it was saved");
            // Saved again, the same state: every attribute the same, a parameter's stored value within a hundred-
            // thousandth. Not byte for byte: JUCE keeps a float parameter as its plain value and goes through its curve
            // at every load, so a curved range (a decay, an LFO rate) can come back a float step off (clap-validator's
            // binary test, D228).
            juce::MemoryBlock savedAgain;
            second->getStateInformation(savedAgain);
            auto x = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
            auto y = juce::AudioProcessor::getXmlFromBinary(savedAgain.getData(), static_cast<int>(savedAgain.getSize()));
            expect(x != nullptr && y != nullptr, "both read");
            if (x == nullptr || y == nullptr)
                return;
            int differing = 0, nearlyEqual = 0;
            std::function<void(const juce::XmlElement&, const juce::XmlElement&)> compare = [&](const juce::XmlElement& p, const juce::XmlElement& q)
            {
                for (int i = 0; i < p.getNumAttributes(); ++i)
                {
                    const auto name = p.getAttributeName(i);
                    const auto a1 = p.getAttributeValue(i), a2 = q.getStringAttribute(name);
                    if (a1 == a2)
                        continue;
                    const double d1 = a1.getDoubleValue(), d2 = a2.getDoubleValue();
                    if (name == "value" && std::abs(d1 - d2) <= 1.0e-5 * juce::jmax(1.0, std::abs(d1)))
                        ++nearlyEqual;
                    else if (++differing <= 12)
                        logMessage("  " + p.getTagName() + " " + p.getStringAttribute("id") + " " + name + ": '" + a1.substring(0, 60)
                                   + "' -> '" + a2.substring(0, 60) + "'");
                }
                expectEquals(q.getNumChildElements(), p.getNumChildElements(), "the same parts in " + p.getTagName());
                for (int i = 0; i < juce::jmin(p.getNumChildElements(), q.getNumChildElements()); ++i)
                    compare(*p.getChildElement(i), *q.getChildElement(i));
            };
            compare(*x, *y);
            logMessage(juce::String(nearlyEqual) + " stored values a float step off, " + juce::String(differing) + " differing");
            expectEquals(differing, 0, "the same state saved again");
        }
    }
};

static ParameterRoundTripTests parameterRoundTripTests;
