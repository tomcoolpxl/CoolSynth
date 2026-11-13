#include "PartParameters.h"

#include <cmath>

namespace coolsynth::parameters::v3
{
    namespace
    {
        namespace sv3 = coolsynth::synth::v3;

        juce::ParameterID makeId(const juce::String& id) { return { id, parameterVersionHint }; }

        juce::NormalisableRange<float> logRange(float minValue, float maxValue)
        {
            return { minValue, maxValue,
                     [](float start, float end, float proportion) { return start * std::pow(end / start, proportion); },
                     [](float start, float end, float value) { return std::log(value / start) / std::log(end / start); },
                     [](float, float, float value) { return value; } };
        }

        juce::NormalisableRange<float> squaredRange(float minValue, float maxValue)
        {
            return { minValue, maxValue,
                     [](float start, float end, float proportion) { return start + (end - start) * proportion * proportion; },
                     [](float start, float end, float value) { return std::sqrt(std::max(0.0f, (value - start) / (end - start))); },
                     [](float, float, float value) { return value; } };
        }

        juce::AudioParameterFloatAttributes msAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return v < 1000.0f ? juce::String(v, v < 10.0f ? 1 : 0) + " ms" : juce::String(v / 1000.0f, 2) + " s";
            });
        }

        juce::AudioParameterFloatAttributes percentAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return juce::String(static_cast<int>(std::round(v * 100.0f))) + " %";
            });
        }

        juce::AudioParameterFloatAttributes centsAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return juce::String(v, 1) + " ct";
            });
        }

        std::unique_ptr<juce::AudioParameterFloat> floatParameter(const juce::String& id, const juce::String& name,
                                                                  juce::NormalisableRange<float> range, float defaultValue,
                                                                  juce::AudioParameterFloatAttributes attributes = {})
        {
            return std::make_unique<juce::AudioParameterFloat>(makeId(id), name, range, defaultValue, attributes);
        }

        struct EnvelopeDefaults
        {
            float attackMs, decayMs, sustain, releaseMs, velocity;
        };

        constexpr std::array<EnvelopeDefaults, partEnvelopes> kEnvelopeDefaults { {
            { 2.0f, 300.0f, 1.0f, 300.0f, 1.0f },  // env1: the amplifier, follows velocity
            { 2.0f, 400.0f, 0.3f, 400.0f, 0.0f },  // env2: P1, usually the filter
            { 5.0f, 500.0f, 0.5f, 500.0f, 0.0f },  // env3: free
        } };

        std::unique_ptr<juce::AudioProcessorParameterGroup> makeEnvelopeGroup(Part part, int envelope)
        {
            const auto& d = kEnvelopeDefaults[static_cast<std::size_t>(envelope - 1)];
            auto id = [&](const char* field) { return envelopeParameterId(part, envelope, field); };
            const juce::String n = "Env " + juce::String(envelope) + " ";

            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".env" + juce::String(envelope),
                                                                              "Envelope " + juce::String(envelope), "|");
            group->addChild(floatParameter(id("delay"), n + "Delay", squaredRange(0.0f, 5000.0f), 0.0f, msAttributes()));
            group->addChild(floatParameter(id("attack"), n + "Attack", logRange(0.5f, 10000.0f), d.attackMs, msAttributes()));
            group->addChild(floatParameter(id("hold"), n + "Hold", squaredRange(0.0f, 5000.0f), 0.0f, msAttributes()));
            group->addChild(floatParameter(id("decay"), n + "Decay", logRange(1.0f, 20000.0f), d.decayMs, msAttributes()));
            group->addChild(floatParameter(id("sustain"), n + "Sustain", { 0.0f, 1.0f }, d.sustain, percentAttributes()));
            group->addChild(floatParameter(id("release"), n + "Release", logRange(1.0f, 20000.0f), d.releaseMs, msAttributes()));
            group->addChild(floatParameter(id("attackCurve"), n + "Attack Curve", { -1.0f, 1.0f }, -0.3f));
            group->addChild(floatParameter(id("decayCurve"), n + "Decay Curve", { -1.0f, 1.0f }, -0.6f));
            group->addChild(floatParameter(id("releaseCurve"), n + "Release Curve", { -1.0f, 1.0f }, -0.6f));
            group->addChild(floatParameter(id("velocity"), n + "Velocity", { 0.0f, 1.0f }, d.velocity, percentAttributes()));
            group->addChild(floatParameter(id("keyTime"), n + "Key Time", { 0.0f, 1.0f }, 0.0f, percentAttributes()));
            group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id("loop")), n + "Loop", false));
            group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id("oneShot")), n + "One Shot", false));
            return group;
        }

        juce::AudioParameterFloatAttributes hzAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return juce::String(v, v < 1.0f ? 3 : 2) + " Hz";
            });
        }

        juce::AudioParameterFloatAttributes degreeAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return juce::String(static_cast<int>(std::round(v))) + juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
            });
        }

        std::unique_ptr<juce::AudioProcessorParameterGroup> makeLfoGroup(Part part, const juce::String& lfo, const juce::String& title, bool global)
        {
            auto id = [&](const char* field) { return lfoParameterId(part, lfo, field); };
            const juce::String n = title + " ";

            juce::StringArray divisions;
            for (const auto& d : sv3::lfoDivisions)
                divisions.add(d.name);

            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + "." + lfo, title, "|");
            group->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(id("shape")), n + "Shape",
                juce::StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Random", "Steps" }, 0));
            group->addChild(floatParameter(id("rate"), n + "Rate", logRange(0.01f, 50.0f), 2.0f, hzAttributes()));
            group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id("sync")), n + "Sync", global));
            group->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(id("division")), n + "Division", divisions,
                                                                         sv3::lfoDefaultDivision));
            group->addChild(floatParameter(id("phase"), n + "Phase", { 0.0f, 360.0f }, 0.0f, degreeAttributes()));
            group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id("retrigger")), n + "Retrigger", ! global));
            group->addChild(floatParameter(id("delay"), n + "Delay", squaredRange(0.0f, 10000.0f), 0.0f, msAttributes()));
            group->addChild(floatParameter(id("fade"), n + "Fade In", squaredRange(0.0f, 10000.0f), 0.0f, msAttributes()));
            group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id("oneShot")), n + "One Shot", false));
            return group;
        }

        juce::AudioParameterFloatAttributes amountAttributes()
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
            {
                return juce::String(v, 3);
            });
        }

        std::unique_ptr<juce::AudioProcessorParameterGroup> makeMatrixGroup(Part part)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".matrix", "Matrix", "|");
            for (int i = 1; i <= sv3::matrixSlotCount; ++i)
            {
                auto id = [&](const char* field) { return makeId(matrixParameterId(part, i, field)); };
                const juce::String n = "Mod " + juce::String(i) + " ";
                auto slot = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".mod" + juce::String(i),
                                                                                 "Mod " + juce::String(i), "|");
                slot->addChild(std::make_unique<juce::AudioParameterChoice>(id("src"), n + "Source", modSourceNames(), 0));
                slot->addChild(std::make_unique<juce::AudioParameterChoice>(id("dst"), n + "Destination", modDestinationNames(), 0));
                slot->addChild(std::make_unique<juce::AudioParameterFloat>(id("amount"), n + "Amount", juce::NormalisableRange<float> { -1.0f, 1.0f },
                                                                           0.0f, amountAttributes()));
                slot->addChild(std::make_unique<juce::AudioParameterChoice>(id("via"), n + "Via", modSourceNames(), 0));
                slot->addChild(std::make_unique<juce::AudioParameterChoice>(id("curve"), n + "Curve", modCurveNames(), 0));
                group->addChild(std::move(slot));
            }
            return group;
        }

        std::unique_ptr<juce::AudioProcessorParameterGroup> makeGlobalModulatorGroups(Part part)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".global", "Global Modulators", "|");

            auto genv = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".genv", "Global Envelope", "|");
            auto g = [&](const char* field) { return globalEnvelopeParameterId(part, field); };
            genv->addChild(floatParameter(g("delay"), "GEnv Delay", squaredRange(0.0f, 60000.0f), 0.0f, msAttributes()));
            genv->addChild(floatParameter(g("attack"), "GEnv Attack", logRange(0.5f, 60000.0f), 1000.0f, msAttributes()));
            genv->addChild(floatParameter(g("hold"), "GEnv Hold", squaredRange(0.0f, 60000.0f), 0.0f, msAttributes()));
            genv->addChild(floatParameter(g("decay"), "GEnv Decay", logRange(1.0f, 60000.0f), 1000.0f, msAttributes()));
            genv->addChild(floatParameter(g("sustain"), "GEnv Sustain", { 0.0f, 1.0f }, 1.0f, percentAttributes()));
            genv->addChild(floatParameter(g("release"), "GEnv Release", logRange(1.0f, 60000.0f), 1000.0f, msAttributes()));
            genv->addChild(floatParameter(g("attackCurve"), "GEnv Attack Curve", { -1.0f, 1.0f }, 0.0f));
            genv->addChild(floatParameter(g("decayCurve"), "GEnv Decay Curve", { -1.0f, 1.0f }, -0.6f));
            genv->addChild(floatParameter(g("releaseCurve"), "GEnv Release Curve", { -1.0f, 1.0f }, -0.6f));
            genv->addChild(std::make_unique<juce::AudioParameterBool>(makeId(g("loop")), "GEnv Loop", false));
            genv->addChild(std::make_unique<juce::AudioParameterBool>(makeId(g("sync")), "GEnv Sync", false));
            genv->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(g("trigger")), "GEnv Trigger",
                                                                        juce::StringArray { "First Note", "Every Note" }, 0));
            group->addChild(std::move(genv));

            auto step = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".step", "Step Modulator", "|");
            for (int i = 1; i <= sv3::stepCount; ++i)
                step->addChild(floatParameter(stepParameterId(part, "s" + juce::String(i)), "Step " + juce::String(i), { -1.0f, 1.0f }, 0.0f));
            for (int i = 1; i <= sv3::stepCount; ++i)
                step->addChild(std::make_unique<juce::AudioParameterBool>(makeId(stepParameterId(part, "slide" + juce::String(i))),
                                                                          "Step " + juce::String(i) + " Slide", false));
            juce::StringArray divisions;
            for (const auto& d : sv3::lfoDivisions)
                divisions.add(d.name);
            step->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(stepParameterId(part, "division")), "Step Division",
                                                                        divisions, sv3::stepDefaultDivision));
            step->addChild(std::make_unique<juce::AudioParameterInt>(makeId(stepParameterId(part, "length")), "Step Length", 1,
                                                                     sv3::stepCount, sv3::stepCount));
            step->addChild(floatParameter(stepParameterId(part, "glide"), "Step Glide", { 0.0f, 1.0f }, 0.5f, percentAttributes()));
            step->addChild(std::make_unique<juce::AudioParameterBool>(makeId(stepParameterId(part, "retrigger")), "Step Retrigger", true));
            group->addChild(std::move(step));

            auto macros = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".macros", "Macros", "|");
            for (int i = 1; i <= sv3::macroCount; ++i)
                macros->addChild(floatParameter(macroParameterId(part, i), "Macro " + juce::String(i), { 0.0f, 1.0f }, 0.0f, percentAttributes()));
            group->addChild(std::move(macros));

            auto chaosGroup = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".chaos", "Chaos", "|");
            chaosGroup->addChild(floatParameter(chaosParameterId(part), "Chaos Rate", logRange(0.01f, 10.0f), 0.5f, hzAttributes()));
            group->addChild(std::move(chaosGroup));
            return group;
        }

        LfoPointers bindLfoPointers(juce::AudioProcessorValueTreeState& state, Part part, const juce::String& lfo)
        {
            auto get = [&](const char* field) { return state.getRawParameterValue(lfoParameterId(part, lfo, field)); };
            LfoPointers p;
            p.shape = get("shape");
            p.rate = get("rate");
            p.sync = get("sync");
            p.division = get("division");
            p.phase = get("phase");
            p.retrigger = get("retrigger");
            p.delay = get("delay");
            p.fade = get("fade");
            p.oneShot = get("oneShot");
            return p;
        }

        float load(const std::atomic<float>* p, float fallback) noexcept { return p != nullptr ? p->load() : fallback; }
        int loadInt(const std::atomic<float>* p, int fallback) noexcept
        {
            return p != nullptr ? static_cast<int>(std::lround(p->load())) : fallback;
        }
    }

    juce::String partPrefix(Part part) { return part == Part::a ? "pA" : "pB"; }

    juce::String envelopeParameterId(Part part, int oneBasedEnvelope, const char* field)
    {
        return partPrefix(part) + ".env" + juce::String(oneBasedEnvelope) + "." + field;
    }

    juce::String voiceParameterId(Part part, const char* field) { return partPrefix(part) + ".voice." + field; }
    juce::String playParameterId(Part part, const char* field) { return partPrefix(part) + ".play." + field; }
    juce::String chordParameterId(Part part, const juce::String& field) { return partPrefix(part) + ".chord." + field; }

    juce::String mixParameterId(Part part, const juce::String& field) { return partPrefix(part) + ".mix." + field; }

    const juce::StringArray& mixFields()
    {
        static const juce::StringArray fields { "level", "pan", "transpose", "fine", "keyLow", "keyHigh", "velLow", "velHigh", "fx", "arp" };
        return fields;
    }

    const juce::StringArray& partModeNames()
    {
        static const juce::StringArray names { "A", "Layer", "Split", "B" };
        return names;
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makePartsGroup()
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("parts", "Parts", "|");
        group->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(partModeParameterId), "Part Mode", partModeNames(), 0));
        return group;
    }

    const juce::StringArray& chordTypeNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray n { "Off", "Octave", "Fifth", "Major", "Minor", "Sus2", "Sus4", "Major 7", "Minor 7",
                                  "Dominant 7", "Major 9", "Minor 9", "Minor 11", "Learned" };
            jassert(n.size() == sv3::chordType::count);
            return n;
        }();
        return names;
    }

    juce::String lfoParameterId(Part part, const juce::String& lfo, const char* field)
    {
        return partPrefix(part) + "." + lfo + "." + field;
    }

    juce::String matrixParameterId(Part part, int oneBasedSlot, const char* field)
    {
        return partPrefix(part) + ".mod" + juce::String(oneBasedSlot) + "." + field;
    }

    const juce::StringArray& modSourceNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray n { "Off", "Velocity", "Release Velocity", "Key", "Note Random", "Aftertouch", "MPE Pressure",
                                  "MPE Slide", "Drift", "Env 1", "Env 2", "Env 3", "LFO 1", "LFO 2", "GLFO 1", "GLFO 2",
                                  "Global Env", "Step", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Mod Wheel", "Pitch Bend",
                                  "Expression", "Chaos" };
            jassert(n.size() == sv3::ModSources::count);
            return n;
        }();
        return names;
    }

    const juce::StringArray& modDestinationNames()
    {
        static const juce::StringArray names = []
        {
            namespace d = sv3::modDestination;
            juce::StringArray n { "Off", "Pitch", "VCA", "Pan", "LFO 1 Rate", "LFO 2 Rate" };
            for (int g = 1; g <= d::generatorSlots; ++g)
            {
                const juce::String slot = "G" + juce::String(g) + " ";
                n.addArray(juce::StringArray { slot + "Pitch", slot + "Level", slot + "Pan", slot + "Amount" });
                for (int k = 1; k <= sv3::maxSlotParameters; ++k)
                    n.add(slot + "Param " + juce::String(k));
            }
            for (int p = 1; p <= d::processorSlots; ++p)
                for (int k = 1; k <= sv3::maxSlotParameters; ++k)
                    n.add("P" + juce::String(p) + " Param " + juce::String(k));
            for (int e = 1; e <= d::envelopeTimes; ++e)
                n.add("Env " + juce::String(e) + " Time");
            jassert(n.size() == d::count);
            return n;
        }();
        return names;
    }

    const juce::StringArray& globalEnvelopeFields()
    {
        static const juce::StringArray fields { "delay", "attack", "hold", "decay", "sustain", "release", "attackCurve",
                                                "decayCurve", "releaseCurve", "loop", "sync", "trigger" };
        return fields;
    }

    const juce::StringArray& stepFields()
    {
        static const juce::StringArray fields { "division", "length", "glide", "retrigger" };
        return fields;
    }

    juce::String globalEnvelopeParameterId(Part part, const char* field) { return partPrefix(part) + ".genv." + field; }
    juce::String stepParameterId(Part part, const juce::String& field) { return partPrefix(part) + ".step." + field; }
    juce::String macroParameterId(Part part, int oneBasedMacro) { return partPrefix(part) + ".macro" + juce::String(oneBasedMacro); }
    juce::String chaosParameterId(Part part) { return partPrefix(part) + ".chaos.rate"; }

    const juce::StringArray& modCurveNames()
    {
        static const juce::StringArray names { "Linear", "Unipolar", "Exponential", "Logarithmic", "S-Curve", "Quantized" };
        return names;
    }

    const juce::StringArray& lfoFields()
    {
        static const juce::StringArray fields { "shape", "rate", "sync", "division", "phase", "retrigger", "delay", "fade", "oneShot" };
        return fields;
    }

    bool isPartParameterId(juce::StringRef parameterId) noexcept
    {
        const juce::String id(parameterId);
        return id.startsWith("pA.") || id.startsWith("pB.");
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> makePartGroup(Part part)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part), part == Part::a ? "Part A" : "Part B", "|");

        for (int i = 1; i <= partGeneratorSlots; ++i)
            group->addChild(makeSlotGroup({ part, SlotKind::generator, i }, sv3::GeneratorSlot::typeTable(),
                                          "Generator " + juce::String(i), i == 1 ? sv3::generatorType::va : sv3::generatorType::off));
        for (int i = 1; i <= partProcessorSlots; ++i)
            group->addChild(makeSlotGroup({ part, SlotKind::processor, i }, sv3::ProcessorSlot::typeTable(),
                                          "Processor " + juce::String(i), i == 1 ? sv3::processorType::ladder : sv3::processorType::off));
        for (int i = 1; i <= partEnvelopes; ++i)
            group->addChild(makeEnvelopeGroup(part, i));
        for (int i = 1; i <= partLocalLfos; ++i)
            group->addChild(makeLfoGroup(part, "lfo" + juce::String(i), "LFO " + juce::String(i), false));
        for (int i = 1; i <= partGlobalLfos; ++i)
            group->addChild(makeLfoGroup(part, "glfo" + juce::String(i), "Global LFO " + juce::String(i), true));
        group->addChild(makeMatrixGroup(part));
        group->addChild(makeGlobalModulatorGroups(part));

        auto voice = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".voice", "Voice", "|");
        voice->addChild(floatParameter(voiceParameterId(part, "env2ToP1"), "Env 2 to P1", { -1.0f, 1.0f }, 0.2f, percentAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "velToP1"), "Velocity to P1", { -1.0f, 1.0f }, 0.0f, percentAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "glide"), "Glide", squaredRange(0.0f, 5000.0f), 0.0f, msAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "drift"), "Drift", { 0.0f, 25.0f }, 0.0f, centsAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "wheelToLfo1"), "Wheel to LFO 1", { 0.0f, 1.0f }, 0.0f, percentAttributes()));
        group->addChild(std::move(voice));

        auto play = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".play", "Play", "|");
        play->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(playParameterId(part, "mode")), "Play Mode",
                                                                    juce::StringArray { "Poly", "Mono", "Legato", "Unison" }, 0));
        play->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(playParameterId(part, "priority")), "Key Priority",
                                                                    juce::StringArray { "Last", "Low", "High" }, 0));
        play->addChild(std::make_unique<juce::AudioParameterInt>(makeId(playParameterId(part, "voices")), "Voices", 1,
                                                                 sv3::Part::maxVoices, sv3::Part::maxVoices));
        play->addChild(std::make_unique<juce::AudioParameterInt>(makeId(playParameterId(part, "unison")), "Unison Voices", 2,
                                                                 sv3::Part::maxUnisonVoices, 4));
        play->addChild(floatParameter(playParameterId(part, "unisonDetune"), "Unison Detune", { 0.0f, 100.0f }, 15.0f, centsAttributes()));
        play->addChild(floatParameter(playParameterId(part, "unisonSpread"), "Unison Spread", { 0.0f, 1.0f }, 0.7f, percentAttributes()));
        play->addChild(std::make_unique<juce::AudioParameterInt>(makeId(playParameterId(part, "bendRange")), "Bend Range", 0, 24, 2));
        play->addChild(std::make_unique<juce::AudioParameterBool>(makeId(playParameterId(part, "glideLegato")), "Glide Legato Only", false));
        group->addChild(std::move(play));

        auto chord = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".chord", "Chord Memory", "|");
        chord->addChild(std::make_unique<juce::AudioParameterChoice>(makeId(chordParameterId(part, "type")), "Chord", chordTypeNames(), 0));
        chord->addChild(floatParameter(chordParameterId(part, "strum"), "Chord Strum", { -200.0f, 200.0f }, 0.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
                                       {
                                           return juce::String(v, 1) + " ms";
                                       })));
        for (int i = 1; i <= sv3::learnedIntervalCount; ++i)
            chord->addChild(std::make_unique<juce::AudioParameterInt>(makeId(chordParameterId(part, "n" + juce::String(i))),
                                                                      "Learned Note " + juce::String(i), 0, sv3::maxLearnedInterval, 0));
        chord->addChild(std::make_unique<juce::AudioParameterBool>(makeId(chordParameterId(part, "learn")), "Chord Learn", false));
        group->addChild(std::move(chord));

        // Split by default at middle C: part A below it, part B from it upwards. Layer ignores the key range.
        const bool lower = part == Part::a;
        auto mix = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".mix", "Part Mix", "|");
        mix->addChild(floatParameter(mixParameterId(part, "level"), "Part Level", { -60.0f, 6.0f }, 0.0f,
                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
                                     {
                                         return v <= -60.0f ? juce::String("off") : juce::String(v, 1) + " dB";
                                     })));
        mix->addChild(floatParameter(mixParameterId(part, "pan"), "Part Pan", { -1.0f, 1.0f }, 0.0f, amountAttributes()));
        mix->addChild(std::make_unique<juce::AudioParameterInt>(makeId(mixParameterId(part, "transpose")), "Part Transpose", -36, 36, 0));
        mix->addChild(floatParameter(mixParameterId(part, "fine"), "Part Fine", { -100.0f, 100.0f }, 0.0f, centsAttributes()));
        mix->addChild(std::make_unique<juce::AudioParameterInt>(makeId(mixParameterId(part, "keyLow")), "Key Low", 0, 127, lower ? 0 : 60));
        mix->addChild(std::make_unique<juce::AudioParameterInt>(makeId(mixParameterId(part, "keyHigh")), "Key High", 0, 127, lower ? 59 : 127));
        mix->addChild(std::make_unique<juce::AudioParameterInt>(makeId(mixParameterId(part, "velLow")), "Velocity Low", 1, 127, 1));
        mix->addChild(std::make_unique<juce::AudioParameterInt>(makeId(mixParameterId(part, "velHigh")), "Velocity High", 1, 127, 127));
        mix->addChild(floatParameter(mixParameterId(part, "fx"), "Part Effects", { 0.0f, 1.0f }, 1.0f, percentAttributes()));
        mix->addChild(std::make_unique<juce::AudioParameterBool>(makeId(mixParameterId(part, "arp")), "Part Arp", true));
        group->addChild(std::move(mix));

        for (int i = 1; i <= partInsertSlots; ++i)
            group->addChild(makeSlotGroup({ part, SlotKind::insert, i }, sv3::insertTypeTable(), "Insert " + juce::String(i),
                                          sv3::insertType::off));

        return group;
    }

    PartParameterPointers bindPartPointers(juce::AudioProcessorValueTreeState& state, Part part)
    {
        PartParameterPointers p;
        for (int i = 0; i < partGeneratorSlots; ++i)
            p.generators[static_cast<std::size_t>(i)] = bindSlotPointers(state, { part, SlotKind::generator, i + 1 });
        for (int i = 0; i < partProcessorSlots; ++i)
            p.processors[static_cast<std::size_t>(i)] = bindSlotPointers(state, { part, SlotKind::processor, i + 1 });
        for (int i = 0; i < partInsertSlots; ++i)
            p.inserts[static_cast<std::size_t>(i)] = bindSlotPointers(state, { part, SlotKind::insert, i + 1 });

        for (int i = 0; i < partEnvelopes; ++i)
        {
            auto& e = p.envelopes[static_cast<std::size_t>(i)];
            auto get = [&](const char* field) { return state.getRawParameterValue(envelopeParameterId(part, i + 1, field)); };
            e.delay = get("delay");
            e.attack = get("attack");
            e.hold = get("hold");
            e.decay = get("decay");
            e.sustain = get("sustain");
            e.release = get("release");
            e.attackCurve = get("attackCurve");
            e.decayCurve = get("decayCurve");
            e.releaseCurve = get("releaseCurve");
            e.velocity = get("velocity");
            e.keyTime = get("keyTime");
            e.loop = get("loop");
            e.oneShot = get("oneShot");
        }

        for (int i = 0; i < partLocalLfos; ++i)
            p.lfos[static_cast<std::size_t>(i)] = bindLfoPointers(state, part, "lfo" + juce::String(i + 1));
        for (int i = 0; i < partGlobalLfos; ++i)
            p.globalLfos[static_cast<std::size_t>(i)] = bindLfoPointers(state, part, "glfo" + juce::String(i + 1));
        p.wheelToLfo1 = state.getRawParameterValue(voiceParameterId(part, "wheelToLfo1"));
        {
            auto& g = p.global;
            auto get = [&](const char* field) { return state.getRawParameterValue(globalEnvelopeParameterId(part, field)); };
            g.envelope.delay = get("delay");
            g.envelope.attack = get("attack");
            g.envelope.hold = get("hold");
            g.envelope.decay = get("decay");
            g.envelope.sustain = get("sustain");
            g.envelope.release = get("release");
            g.envelope.attackCurve = get("attackCurve");
            g.envelope.decayCurve = get("decayCurve");
            g.envelope.releaseCurve = get("releaseCurve");
            g.envelope.loop = get("loop");
            g.envelopeSync = get("sync");
            g.envelopeTrigger = get("trigger");
            for (int i = 0; i < sv3::stepCount; ++i)
            {
                g.stepValues[static_cast<std::size_t>(i)] = state.getRawParameterValue(stepParameterId(part, "s" + juce::String(i + 1)));
                g.stepSlides[static_cast<std::size_t>(i)] = state.getRawParameterValue(stepParameterId(part, "slide" + juce::String(i + 1)));
            }
            g.stepDivision = state.getRawParameterValue(stepParameterId(part, "division"));
            g.stepLength = state.getRawParameterValue(stepParameterId(part, "length"));
            g.stepGlide = state.getRawParameterValue(stepParameterId(part, "glide"));
            g.stepRetrigger = state.getRawParameterValue(stepParameterId(part, "retrigger"));
            for (int i = 0; i < sv3::macroCount; ++i)
                g.macros[static_cast<std::size_t>(i)] = state.getRawParameterValue(macroParameterId(part, i + 1));
            g.chaosRate = state.getRawParameterValue(chaosParameterId(part));
        }

        for (int i = 0; i < sv3::matrixSlotCount; ++i)
        {
            auto& m = p.matrix[static_cast<std::size_t>(i)];
            auto get = [&](const char* field) { return state.getRawParameterValue(matrixParameterId(part, i + 1, field)); };
            m.source = get("src");
            m.destination = get("dst");
            m.amount = get("amount");
            m.via = get("via");
            m.curve = get("curve");
        }

        p.env2ToP1 = state.getRawParameterValue(voiceParameterId(part, "env2ToP1"));
        p.velToP1 = state.getRawParameterValue(voiceParameterId(part, "velToP1"));
        p.glide = state.getRawParameterValue(voiceParameterId(part, "glide"));
        p.drift = state.getRawParameterValue(voiceParameterId(part, "drift"));
        p.mode = state.getRawParameterValue(playParameterId(part, "mode"));
        p.priority = state.getRawParameterValue(playParameterId(part, "priority"));
        p.voices = state.getRawParameterValue(playParameterId(part, "voices"));
        p.unison = state.getRawParameterValue(playParameterId(part, "unison"));
        p.unisonDetune = state.getRawParameterValue(playParameterId(part, "unisonDetune"));
        p.unisonSpread = state.getRawParameterValue(playParameterId(part, "unisonSpread"));
        p.bendRange = state.getRawParameterValue(playParameterId(part, "bendRange"));
        p.glideLegato = state.getRawParameterValue(playParameterId(part, "glideLegato"));
        p.chordType = state.getRawParameterValue(chordParameterId(part, "type"));
        p.chordStrum = state.getRawParameterValue(chordParameterId(part, "strum"));
        for (int i = 0; i < sv3::learnedIntervalCount; ++i)
            p.chordNotes[static_cast<std::size_t>(i)] = state.getRawParameterValue(chordParameterId(part, "n" + juce::String(i + 1)));
        p.chordLearn = state.getRawParameterValue(chordParameterId(part, "learn"));
        p.mix.level = state.getRawParameterValue(mixParameterId(part, "level"));
        p.mix.pan = state.getRawParameterValue(mixParameterId(part, "pan"));
        p.mix.transpose = state.getRawParameterValue(mixParameterId(part, "transpose"));
        p.mix.fine = state.getRawParameterValue(mixParameterId(part, "fine"));
        p.mix.keyLow = state.getRawParameterValue(mixParameterId(part, "keyLow"));
        p.mix.keyHigh = state.getRawParameterValue(mixParameterId(part, "keyHigh"));
        p.mix.velLow = state.getRawParameterValue(mixParameterId(part, "velLow"));
        p.mix.velHigh = state.getRawParameterValue(mixParameterId(part, "velHigh"));
        p.mix.fx = state.getRawParameterValue(mixParameterId(part, "fx"));
        p.mix.arp = state.getRawParameterValue(mixParameterId(part, "arp"));
        return p;
    }

    void readPartSettings(const PartParameterPointers& pointers, coolsynth::synth::v3::PartSettings& settings) noexcept
    {
        for (std::size_t i = 0; i < settings.voice.generators.size(); ++i)
        {
            const auto t = readSlotTargets(pointers.generators[i]);
            auto& g = settings.voice.generators[i];
            g.type = t.typeIndex;
            g.p = t.p;
            g.level = t.level;
            g.pan = t.pan;
            g.pitchSemitones = t.pitchSemitones;
            g.keyTrack = t.keyTrack;
            g.combine = t.combine;
            g.combineAmount = t.amount;
        }

        for (std::size_t i = 0; i < settings.voice.processors.size(); ++i)
        {
            const auto t = readSlotTargets(pointers.processors[i]);
            settings.voice.processors[i].type = t.typeIndex;
            settings.voice.processors[i].p = t.p;
        }

        for (std::size_t i = 0; i < settings.voice.envelopes.size(); ++i)
        {
            const auto& e = pointers.envelopes[i];
            auto& out = settings.voice.envelopes[i];
            out.delay = 0.001f * load(e.delay, 0.0f);
            out.attack = 0.001f * load(e.attack, 2.0f);
            out.hold = 0.001f * load(e.hold, 0.0f);
            out.decay = 0.001f * load(e.decay, 300.0f);
            out.sustain = load(e.sustain, 1.0f);
            out.release = 0.001f * load(e.release, 300.0f);
            out.attackCurve = load(e.attackCurve, -0.3f);
            out.decayCurve = load(e.decayCurve, -0.6f);
            out.releaseCurve = load(e.releaseCurve, -0.6f);
            out.velocitySensitivity = load(e.velocity, 0.0f);
            out.keyTimeTracking = load(e.keyTime, 0.0f);
            out.loop = load(e.loop, 0.0f) >= 0.5f;
            out.oneShot = load(e.oneShot, 0.0f) >= 0.5f;
        }

        auto readLfo = [](const LfoPointers& l, bool global)
        {
            sv3::LfoSettings s;
            s.shape = static_cast<sv3::LfoShape>(juce::jlimit(0, sv3::lfoShapeCount - 1, loadInt(l.shape, 0)));
            s.rateHz = load(l.rate, 2.0f);
            s.sync = load(l.sync, global ? 1.0f : 0.0f) >= 0.5f;
            s.division = juce::jlimit(0, static_cast<int>(sv3::lfoDivisions.size()) - 1, loadInt(l.division, sv3::lfoDefaultDivision));
            s.startPhase = load(l.phase, 0.0f) / 360.0f;
            s.retrigger = load(l.retrigger, global ? 0.0f : 1.0f) >= 0.5f;
            s.delaySeconds = 0.001f * load(l.delay, 0.0f);
            s.fadeSeconds = 0.001f * load(l.fade, 0.0f);
            s.oneShot = load(l.oneShot, 0.0f) >= 0.5f;
            return s;
        };
        for (std::size_t i = 0; i < settings.voice.lfos.size(); ++i)
            settings.voice.lfos[i] = readLfo(pointers.lfos[i], false);
        for (std::size_t i = 0; i < settings.globalLfos.size(); ++i)
            settings.globalLfos[i] = readLfo(pointers.globalLfos[i], true);
        settings.voice.wheelToLfo1 = load(pointers.wheelToLfo1, 0.0f);

        {
            const auto& g = pointers.global;
            auto& out = settings.globalEnvelope;
            out.delay = 0.001f * load(g.envelope.delay, 0.0f);
            out.attack = 0.001f * load(g.envelope.attack, 1000.0f);
            out.hold = 0.001f * load(g.envelope.hold, 0.0f);
            out.decay = 0.001f * load(g.envelope.decay, 1000.0f);
            out.sustain = load(g.envelope.sustain, 1.0f);
            out.release = 0.001f * load(g.envelope.release, 1000.0f);
            out.attackCurve = load(g.envelope.attackCurve, 0.0f);
            out.decayCurve = load(g.envelope.decayCurve, -0.6f);
            out.releaseCurve = load(g.envelope.releaseCurve, -0.6f);
            out.loop = load(g.envelope.loop, 0.0f) >= 0.5f;
            out.velocitySensitivity = 0.0f;
            out.keyTimeTracking = 0.0f;
            settings.globalEnvelopeSync = load(g.envelopeSync, 0.0f) >= 0.5f;
            settings.globalEnvelopeTrigger = static_cast<sv3::GenvTrigger>(juce::jlimit(0, 1, loadInt(g.envelopeTrigger, 0)));
            for (std::size_t i = 0; i < settings.step.values.size(); ++i)
            {
                settings.step.values[i] = load(g.stepValues[i], 0.0f);
                settings.step.slides[i] = load(g.stepSlides[i], 0.0f) >= 0.5f;
            }
            settings.step.division = juce::jlimit(0, static_cast<int>(sv3::lfoDivisions.size()) - 1, loadInt(g.stepDivision, sv3::stepDefaultDivision));
            settings.step.length = juce::jlimit(1, sv3::stepCount, loadInt(g.stepLength, sv3::stepCount));
            settings.step.glide = load(g.stepGlide, 0.5f);
            settings.step.retrigger = load(g.stepRetrigger, 1.0f) >= 0.5f;
            for (std::size_t i = 0; i < settings.macros.size(); ++i)
                settings.macros[i] = load(g.macros[i], 0.0f);
            settings.chaosRateHz = load(g.chaosRate, 0.5f);
        }

        settings.voice.matrix = {};
        for (const auto& m : pointers.matrix)
        {
            sv3::MatrixSlot slot;
            slot.source = static_cast<sv3::ModSource>(juce::jlimit(0, sv3::ModSources::count - 1, loadInt(m.source, 0)));
            slot.destination = juce::jlimit(0, sv3::modDestination::count - 1, loadInt(m.destination, 0));
            slot.amount = load(m.amount, 0.0f);
            slot.via = static_cast<sv3::ModSource>(juce::jlimit(0, sv3::ModSources::count - 1, loadInt(m.via, 0)));
            slot.curve = static_cast<sv3::ModCurve>(juce::jlimit(0, sv3::modCurveCount - 1, loadInt(m.curve, 0)));
            settings.voice.matrix.add(slot);
        }

        settings.voice.env2ToP1 = load(pointers.env2ToP1, 0.0f);
        settings.voice.velocityToP1 = load(pointers.velToP1, 0.0f);
        settings.voice.glideSeconds = 0.001f * load(pointers.glide, 0.0f);
        settings.voice.driftCents = load(pointers.drift, 0.0f);

        settings.playMode = static_cast<sv3::PlayMode>(juce::jlimit(0, 3, loadInt(pointers.mode, 0)));
        settings.keyPriority = static_cast<sv3::KeyPriority>(juce::jlimit(0, 2, loadInt(pointers.priority, 0)));
        settings.polyphony = loadInt(pointers.voices, sv3::Part::maxVoices);
        settings.unisonVoices = loadInt(pointers.unison, 4);
        settings.unisonDetuneCents = load(pointers.unisonDetune, 15.0f);
        settings.unisonSpread = load(pointers.unisonSpread, 0.7f);
        settings.bendRangeSemitones = load(pointers.bendRange, 2.0f);
        settings.glideLegatoOnly = load(pointers.glideLegato, 0.0f) >= 0.5f;

        settings.chord.type = juce::jlimit(0, sv3::chordType::count - 1, loadInt(pointers.chordType, 0));
        settings.chord.strumMs = load(pointers.chordStrum, 0.0f);
        for (std::size_t i = 0; i < settings.chord.learned.size(); ++i)
            settings.chord.learned[i] = juce::jlimit(0, sv3::maxLearnedInterval, loadInt(pointers.chordNotes[i], 0));
        settings.chord.learn = load(pointers.chordLearn, 0.0f) >= 0.5f;
    }

    void readPartMix(const PartParameterPointers& pointers, coolsynth::synth::v3::PartMix& mix) noexcept
    {
        auto value = [](const std::atomic<float>* p, float fallback) { return p != nullptr ? p->load(std::memory_order_relaxed) : fallback; };
        auto whole = [&](const std::atomic<float>* p, int fallback) { return static_cast<int>(std::lround(value(p, static_cast<float>(fallback)))); };
        const auto& m = pointers.mix;
        mix.levelDb = value(m.level, 0.0f);
        mix.pan = value(m.pan, 0.0f);
        mix.transpose = juce::jlimit(-36, 36, whole(m.transpose, 0));
        mix.fineCents = value(m.fine, 0.0f);
        mix.keyLow = juce::jlimit(0, 127, whole(m.keyLow, 0));
        mix.keyHigh = juce::jlimit(0, 127, whole(m.keyHigh, 127));
        mix.velocityLow = juce::jlimit(1, 127, whole(m.velLow, 1));
        mix.velocityHigh = juce::jlimit(1, 127, whole(m.velHigh, 127));
        mix.fx = juce::jlimit(0.0f, 1.0f, value(m.fx, 1.0f));
        mix.arp = value(m.arp, 1.0f) >= 0.5f;
    }

    void readPartInserts(const PartParameterPointers& pointers,
                         std::array<coolsynth::synth::v3::InsertSettings, partInsertSlots>& inserts) noexcept
    {
        for (std::size_t i = 0; i < inserts.size(); ++i)
        {
            const auto targets = readSlotTargets(pointers.inserts[i]);
            inserts[i].type = juce::jlimit(0, sv3::insertType::count - 1, targets.typeIndex);
            inserts[i].p = targets.p;
        }
    }
}
