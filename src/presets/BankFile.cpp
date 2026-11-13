#include "BankFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "parameters/ParameterIDs.h"
#include "parameters/v3/PartParameters.h"
#include "synth/v3/Voice.h"

namespace coolsynth::presets
{
    namespace
    {
        namespace ids = coolsynth::parameters::ids;
        namespace pv3 = coolsynth::parameters::v3;
        namespace sv3 = coolsynth::synth::v3;

        struct Section
        {
            const char* enabledId;                      // set by a bare "on" or "off", or null
            std::map<juce::String, juce::String> keys;  // bank key to parameter ID
        };

        const std::map<juce::String, Section>& globalSections()
        {
            static const std::map<juce::String, Section> sections {
                { "arp", { ids::arpEnabled, {
                    { "tempo", ids::arpInternalTempoBpm }, { "rate", ids::arpRateDivision }, { "pattern", ids::arpPattern },
                    { "octaves", ids::arpOctaveRange }, { "gate", ids::arpGate }, { "swing", ids::arpSwing },
                    { "chance", ids::arpChance }, { "ratchet", ids::arpRatchetCount }, { "ratchetChance", ids::arpRatchetChance },
                    { "accentEvery", ids::arpAccentEvery }, { "accent", ids::arpAccentAmount }, { "rhythm", ids::arpRhythm },
                    { "pulses", ids::arpEuclideanPulses }, { "steps", ids::arpEuclideanSteps }, { "rotation", ids::arpEuclideanRotation },
                    { "latch", ids::arpLatch } } } },
                { "drive", { ids::driveEnabled, { { "amount", ids::driveAmount }, { "mix", ids::driveMix } } } },
                { "chorus", { ids::chorusEnabled, { { "rate", ids::chorusRateHz }, { "depth", ids::chorusDepth }, { "mix", ids::chorusMix } } } },
                { "delay", { ids::delayEnabled, { { "time", ids::delayTimeMs }, { "feedback", ids::delayFeedback }, { "mix", ids::delayMix },
                                                  { "sync", ids::delaySync }, { "division", ids::delayDivision }, { "mode", ids::delayMode } } } },
                { "reverb", { ids::reverbEnabled, { { "size", ids::reverbSize }, { "damping", ids::reverbDamping }, { "mix", ids::reverbMix } } } },
                { "phaser", { ids::phaserEnabled, { { "rate", ids::phaserRateHz }, { "depth", ids::phaserDepth } } } },
                { "compressor", { ids::compressorEnabled, { { "amount", ids::compressorAmount }, { "mix", ids::compressorMix } } } },
            };
            return sections;
        }

        const juce::StringArray& envelopeKeys()
        {
            static const juce::StringArray keys { "delay", "attack", "hold", "decay", "sustain", "release", "attackCurve",
                                                  "decayCurve", "releaseCurve", "velocity", "keyTime", "loop", "oneShot" };
            return keys;
        }

        const juce::StringArray& voiceKeys()
        {
            static const juce::StringArray keys { "env2ToP1", "velToP1", "glide", "drift", "wheelToLfo1" };
            return keys;
        }

        const juce::StringArray& playKeys()
        {
            static const juce::StringArray keys { "mode", "priority", "voices", "unison", "unisonDetune", "unisonSpread",
                                                  "bendRange", "glideLegato" };
            return keys;
        }

        const juce::StringArray& generatorFixedKeys()
        {
            static const juce::StringArray keys { "level", "pan", "oct", "semi", "fine", "key", "combine", "amount" };
            return keys;
        }

        /** "1".."3" from "g1", "p3", "env2" with the given letters; 0 when the key is not of that form. */
        int slotNumber(const juce::String& key, const char* prefix, int count)
        {
            if (! key.startsWith(prefix))
                return 0;
            const auto rest = key.substring(static_cast<int>(std::strlen(prefix)));
            if (rest.length() != 1 || ! juce::CharacterFunctions::isDigit(rest[0]))
                return 0;
            const int n = rest.getIntValue();
            return n >= 1 && n <= count ? n : 0;
        }

        /** n from "s12" or "slide3" with the given prefix, 1..count; 0 when the key is not of that form. */
        int indexedKey(const juce::String& key, const char* prefix, int count)
        {
            if (! key.startsWith(prefix))
                return 0;
            const auto rest = key.substring(static_cast<int>(std::strlen(prefix)));
            if (rest.isEmpty() || ! rest.containsOnly("0123456789"))
                return 0;
            const int n = rest.getIntValue();
            return n >= 1 && n <= count ? n : 0;
        }

        /** A global envelope time: milliseconds, or bars and beats as at 120 BPM ("8bars", "2beats"). */
        juce::String envelopeTimeText(const juce::String& value)
        {
            const auto v = value.toLowerCase();
            for (const auto& [suffix, ms] : { std::pair<const char*, double> { "bars", 2000.0 }, { "bar", 2000.0 },
                                              { "beats", 500.0 }, { "beat", 500.0 } })
                if (v.endsWith(suffix))
                {
                    const auto number = v.dropLastCharacters(static_cast<int>(std::strlen(suffix))).trim();
                    if (number.isEmpty() || ! number.containsOnly("0123456789."))
                        return {};
                    const double total = number.getDoubleValue() * ms;
                    return std::floor(total) == total ? juce::String(static_cast<juce::int64>(total)) : juce::String(total, 3);
                }
            return value;
        }

        /** What a preset has set so far that later lines depend on. */
        struct PresetContext
        {
            std::array<int, pv3::partGeneratorSlots> generatorTypes { sv3::generatorType::va, sv3::generatorType::off, sv3::generatorType::off };
            std::array<int, pv3::partProcessorSlots> processorTypes { sv3::processorType::ladder, sv3::processorType::off, sv3::processorType::off };
            int nextMod = 1;
        };

        /** A source as the bank writes it ("lfo1", "env2", "wheel") to its choice name, or empty. */
        juce::String sourceText(const juce::String& token)
        {
            static const std::map<juce::String, juce::String> aliases {
                { "none", "Off" }, { "wheel", "Mod Wheel" }, { "bend", "Pitch Bend" }, { "genv", "Global Env" },
                { "random", "Note Random" }, { "vel", "Velocity" }, { "at", "Aftertouch" }, { "pressure", "MPE Pressure" },
                { "slide", "MPE Slide" }, { "releasevel", "Release Velocity" } };
            const auto key = pv3::normaliseOptionName(token);
            if (const auto found = aliases.find(key); found != aliases.end())
                return found->second;
            for (const auto& name : pv3::modSourceNames())
                if (pv3::normaliseOptionName(name) == key)
                    return name;
            return {};
        }

        /**
            A destination as the bank writes it to its choice name, or empty: "pitch", "vca", "pan",
            "lfo1.rate", "g2.pitch", "g1.level", "g1.pan", "g1.amount", a slot parameter by its key
            for the slot's type in this preset ("g1.pw", "p1.cutoff") or by number ("p1.p3").
        */
        juce::String destinationText(const juce::String& token, const PresetContext& context)
        {
            namespace d = sv3::modDestination;
            const auto& names = pv3::modDestinationNames();
            const auto t = token.toLowerCase();
            if (t == "off" || t == "none") return names[d::none];
            if (t == "pitch") return names[d::pitch];
            if (t == "vca") return names[d::vca];
            if (t == "pan") return names[d::pan];
            if (t == "lfo1.rate") return names[d::lfo1Rate];
            if (t == "lfo2.rate") return names[d::lfo2Rate];
            for (int e = 1; e <= d::envelopeTimes; ++e)
                if (t == "env" + juce::String(e) + ".time")
                    return names[d::envelopeTimeBase + e - 1];

            const int dot = t.indexOfChar('.');
            if (dot <= 0)
                return {};
            const auto slotName = t.substring(0, dot);
            const auto field = t.substring(dot + 1);
            const int generator = slotNumber(slotName, "g", d::generatorSlots);
            const int processor = generator == 0 ? slotNumber(slotName, "p", d::processorSlots) : 0;
            if (generator == 0 && processor == 0)
                return {};

            if (generator != 0)
            {
                const juce::StringArray fixed { "pitch", "level", "pan", "amount" };
                if (const int f = fixed.indexOf(field); f >= 0)
                    return names[d::generator(generator - 1, f)];
            }

            int index = -1;
            if (field.length() == 2 && field[0] == 'p' && field[1] >= '1' && field[1] <= '8')
            {
                index = field[1] - '1';
            }
            else
            {
                const auto& descriptor = generator != 0
                    ? sv3::GeneratorSlot::typeTable().descriptor(context.generatorTypes[static_cast<std::size_t>(generator - 1)])
                    : sv3::ProcessorSlot::typeTable().descriptor(context.processorTypes[static_cast<std::size_t>(processor - 1)]);
                for (int i = 0; i < descriptor.usedParams; ++i)
                    if (pv3::normaliseOptionName(descriptor.params[static_cast<std::size_t>(i)].id) == pv3::normaliseOptionName(field))
                        index = i;
            }
            if (index < 0)
                return {};
            return names[generator != 0 ? d::generatorParameter(generator - 1, index) : d::processorParameter(processor - 1, index)];
        }
    }

    BankParseResult parseBankFile(const juce::String& text, const juce::String& sourceFile)
    {
        BankParseResult result;
        const auto lines = juce::StringArray::fromLines(text);
        PresetContext context;

        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const auto line = lines[lineIndex].upToFirstOccurrenceOf("#", false, false).trim();
            if (line.isEmpty())
                continue;

            auto error = [&](const juce::String& message)
            {
                result.errors.add(sourceFile + ":" + juce::String(lineIndex + 1) + ": " + message);
            };

            if (line.startsWithChar('['))
            {
                if (! line.endsWithChar(']') || line.length() < 3)
                {
                    error("a preset header is [Name]");
                    continue;
                }
                BankPreset preset;
                context = {};
                preset.name = line.substring(1, line.length() - 1).trim();
                preset.sourceFile = sourceFile;
                result.presets.push_back(std::move(preset));
                continue;
            }

            if (result.presets.empty())
            {
                error("a setting before the first [Name]");
                continue;
            }
            auto& preset = result.presets.back();

            const int equals = line.indexOfChar('=');
            if (equals <= 0)
            {
                error("expected key = value");
                continue;
            }
            const auto key = line.substring(0, equals).trim();
            const auto value = line.substring(equals + 1).trim();
            auto tokens = juce::StringArray::fromTokens(value, " \t", "");
            tokens.removeEmptyStrings();

            auto add = [&](const juce::String& id, const juce::String& v) { preset.entries.push_back({ id, v }); };

            // key=value pairs into IDs; bare words go to the handler, or are an error.
            auto readPairs = [&](int firstToken, auto&& idForKey, auto&& bareWord)
            {
                for (int t = firstToken; t < tokens.size(); ++t)
                {
                    const auto& token = tokens[t];
                    const int split = token.indexOfChar('=');
                    if (split < 0)
                    {
                        if (! bareWord(token))
                            error("'" + token + "' needs a key=value form");
                        continue;
                    }
                    const auto k = token.substring(0, split);
                    const auto v = token.substring(split + 1);
                    const auto id = idForKey(k);
                    if (id.isEmpty())
                        error("'" + key + "' has no setting '" + k + "'");
                    else if (v.isEmpty())
                        error("'" + k + "' has no value");
                    else
                        add(id, v);
                }
            };
            auto noBareWords = [](const juce::String&) { return false; };

            if (key == "room")
            {
                preset.category = value;
                continue;
            }

            if (key == "master")
            {
                add(ids::masterGainDb, value);
                continue;
            }

            const int generator = slotNumber(key, "g", pv3::partGeneratorSlots);
            const int processor = generator == 0 ? slotNumber(key, "p", pv3::partProcessorSlots) : 0;
            if (generator != 0 || processor != 0)
            {
                const pv3::SlotPath path { pv3::Part::a, generator != 0 ? pv3::SlotKind::generator : pv3::SlotKind::processor,
                                           generator != 0 ? generator : processor };
                const auto table = generator != 0 ? sv3::GeneratorSlot::typeTable() : sv3::ProcessorSlot::typeTable();
                if (tokens.isEmpty())
                {
                    error("'" + key + "' names a slot type first");
                    continue;
                }

                int typeIndex = -1;
                for (int i = 0; i < table.count; ++i)
                    if (pv3::normaliseOptionName(table.descriptor(i).name) == pv3::normaliseOptionName(tokens[0]))
                        typeIndex = i;
                if (typeIndex < 0)
                {
                    error("'" + tokens[0] + "' is not a type for " + key);
                    continue;
                }

                const auto& descriptor = table.descriptor(typeIndex);
                if (generator != 0)
                    context.generatorTypes[static_cast<std::size_t>(generator - 1)] = typeIndex;
                else
                    context.processorTypes[static_cast<std::size_t>(processor - 1)] = typeIndex;
                add(pv3::typeParameterId(path), descriptor.name);
                readPairs(1, [&](const juce::String& k) -> juce::String
                {
                    for (int i = 0; i < descriptor.usedParams; ++i)
                        if (k == descriptor.params[static_cast<std::size_t>(i)].id)
                            return pv3::genericParameterId(path, i + 1);
                    if (generator != 0 && generatorFixedKeys().contains(k))
                        return pv3::fixedParameterId(path, k.toRawUTF8());
                    return {};
                }, noBareWords);
                continue;
            }

            if (const int envelope = slotNumber(key, "env", pv3::partEnvelopes); envelope != 0)
            {
                readPairs(0, [&](const juce::String& k) -> juce::String
                {
                    return envelopeKeys().contains(k) ? pv3::envelopeParameterId(pv3::Part::a, envelope, k.toRawUTF8()) : juce::String();
                }, noBareWords);
                continue;
            }

            if (key == "mod" || (key.startsWith("mod") && key.length() > 3 && key.substring(3).containsOnly("0123456789")))
            {
                const int slot = key == "mod" ? context.nextMod : key.substring(3).getIntValue();
                if (slot < 1 || slot > sv3::matrixSlotCount)
                {
                    error(key == "mod" ? juce::String("more than 24 modulation slots") : "'" + key + "' is not a slot from mod1 to mod24");
                    continue;
                }
                context.nextMod = std::max(context.nextMod, slot + 1);
                for (const auto& token : tokens)
                {
                    const int split = token.indexOfChar('=');
                    const auto k = split > 0 ? token.substring(0, split) : token;
                    const auto v = split > 0 ? token.substring(split + 1) : juce::String();
                    if (split <= 0 || v.isEmpty())
                    {
                        error("'" + token + "' needs a key=value form");
                        continue;
                    }
                    juce::String resolved;
                    if (k == "src" || k == "via")
                        resolved = sourceText(v);
                    else if (k == "dst")
                        resolved = destinationText(v, context);
                    else if (k == "amount" || k == "curve")
                        resolved = v;
                    else
                    {
                        error("'" + key + "' has no setting '" + k + "'");
                        continue;
                    }
                    if (resolved.isEmpty())
                        error("'" + v + "' is not a " + (k == "dst" ? "destination of this preset" : "modulation source"));
                    else
                        add(pv3::matrixParameterId(pv3::Part::a, slot, k.toRawUTF8()), resolved);
                }
                continue;
            }

            if (key == "genv" || key == "step" || key == "macros" || key == "chaos")
            {
                for (const auto& token : tokens)
                {
                    const int split = token.indexOfChar('=');
                    const auto k = split > 0 ? token.substring(0, split) : token;
                    const auto v = split > 0 ? token.substring(split + 1) : juce::String();
                    if (split <= 0 || v.isEmpty())
                    {
                        error("'" + token + "' needs a key=value form");
                        continue;
                    }

                    if (key == "genv")
                    {
                        if (! pv3::globalEnvelopeFields().contains(k))
                        {
                            error("'genv' has no setting '" + k + "'");
                            continue;
                        }
                        const bool time = k == "delay" || k == "attack" || k == "hold" || k == "decay" || k == "release";
                        const auto timeText = time ? envelopeTimeText(v) : v;
                        if (timeText.isEmpty())
                            error("'" + v + "' is not a time");
                        else
                            add(pv3::globalEnvelopeParameterId(pv3::Part::a, k.toRawUTF8()), timeText);
                    }
                    else if (key == "step")
                    {
                        if (k == "values" || k == "slides")
                        {
                            const auto items = juce::StringArray::fromTokens(v, ",", "");
                            if (items.size() < 1 || items.size() > sv3::stepCount)
                            {
                                error("'" + k + "' takes 1 to 16 comma-separated values");
                                continue;
                            }
                            for (int i = 0; i < items.size(); ++i)
                            {
                                auto item = items[i].trim();
                                if (k == "slides")
                                    item = item == "1" ? juce::String("on") : (item == "0" ? juce::String("off") : item);
                                add(pv3::stepParameterId(pv3::Part::a, (k == "values" ? "s" : "slide") + juce::String(i + 1)), item);
                            }
                        }
                        else if (pv3::stepFields().contains(k) || indexedKey(k, "s", sv3::stepCount) != 0
                                 || indexedKey(k, "slide", sv3::stepCount) != 0)
                        {
                            add(pv3::stepParameterId(pv3::Part::a, k), v);
                        }
                        else
                        {
                            error("'step' has no setting '" + k + "'");
                        }
                    }
                    else if (key == "macros")
                    {
                        const int macro = k.startsWith("macro") && k.length() == 6 ? k.getLastCharacters(1).getIntValue() : 0;
                        if (macro < 1 || macro > sv3::macroCount)
                            error("'macros' has no setting '" + k + "'");
                        else
                            add(pv3::macroParameterId(pv3::Part::a, macro), v);
                    }
                    else if (k == "rate")
                    {
                        add(pv3::chaosParameterId(pv3::Part::a), v);
                    }
                    else
                    {
                        error("'chaos' has no setting '" + k + "'");
                    }
                }
                continue;
            }

            if (key == "lfo1" || key == "lfo2" || key == "glfo1" || key == "glfo2")
            {
                readPairs(0, [&](const juce::String& k) -> juce::String
                {
                    return pv3::lfoFields().contains(k) ? pv3::lfoParameterId(pv3::Part::a, key, k.toRawUTF8()) : juce::String();
                }, noBareWords);
                continue;
            }

            if (key == "voice" || key == "play")
            {
                const bool voice = key == "voice";
                readPairs(0, [&](const juce::String& k) -> juce::String
                {
                    if (voice)
                        return voiceKeys().contains(k) ? pv3::voiceParameterId(pv3::Part::a, k.toRawUTF8()) : juce::String();
                    return playKeys().contains(k) ? pv3::playParameterId(pv3::Part::a, k.toRawUTF8()) : juce::String();
                }, noBareWords);
                continue;
            }

            if (const auto section = globalSections().find(key); section != globalSections().end())
            {
                readPairs(0, [&](const juce::String& k) -> juce::String
                {
                    const auto found = section->second.keys.find(k);
                    return found != section->second.keys.end() ? found->second : juce::String();
                }, [&](const juce::String& word)
                {
                    if (section->second.enabledId == nullptr || (word != "on" && word != "off"))
                        return false;
                    add(section->second.enabledId, word);
                    return true;
                });
                continue;
            }

            error("unknown setting '" + key + "'");
        }

        return result;
    }
}
