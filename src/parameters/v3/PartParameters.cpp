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

        auto voice = std::make_unique<juce::AudioProcessorParameterGroup>(partPrefix(part) + ".voice", "Voice", "|");
        voice->addChild(floatParameter(voiceParameterId(part, "env2ToP1"), "Env 2 to P1", { -1.0f, 1.0f }, 0.2f, percentAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "velToP1"), "Velocity to P1", { -1.0f, 1.0f }, 0.0f, percentAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "glide"), "Glide", squaredRange(0.0f, 5000.0f), 0.0f, msAttributes()));
        voice->addChild(floatParameter(voiceParameterId(part, "drift"), "Drift", { 0.0f, 25.0f }, 0.0f, centsAttributes()));
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

        return group;
    }

    PartParameterPointers bindPartPointers(juce::AudioProcessorValueTreeState& state, Part part)
    {
        PartParameterPointers p;
        for (int i = 0; i < partGeneratorSlots; ++i)
            p.generators[static_cast<std::size_t>(i)] = bindSlotPointers(state, { part, SlotKind::generator, i + 1 });
        for (int i = 0; i < partProcessorSlots; ++i)
            p.processors[static_cast<std::size_t>(i)] = bindSlotPointers(state, { part, SlotKind::processor, i + 1 });

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
    }
}
