#include "BankFile.h"

#include <map>

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
                { "delay", { ids::delayEnabled, { { "time", ids::delayTimeMs }, { "feedback", ids::delayFeedback }, { "mix", ids::delayMix } } } },
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
    }

    BankParseResult parseBankFile(const juce::String& text, const juce::String& sourceFile)
    {
        BankParseResult result;
        const auto lines = juce::StringArray::fromLines(text);

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
