#include "SoundUnit.h"

#include "diagnostics/FlightRecorder.h"

#include <algorithm>
#include <cmath>

namespace coolsynth::synth::v3
{
    namespace
    {
        /** Knob 8, Space (D176): a reverb's, delay's or room's wet share times 2^space, so twice at the top and half
            at the bottom; other effects as they are. The effects smooth their mix themselves. */
        InsertSettings withSpace(const InsertSettings& settings, float space) noexcept
        {
            if (space == 0.0f)
                return settings;
            std::size_t mix = 0;
            if (settings.type == insertType::delay || settings.type == insertType::reverb)
                mix = 2;
            else if (settings.type == insertType::room)
                mix = 3;
            else
                return settings;
            auto spaced = settings;
            spaced.p[mix] = std::clamp(settings.p[mix] * std::exp2(std::clamp(space, -1.0f, 1.0f)), 0.0f, 1.0f);
            return spaced;
        }

        /** Silences NaN and infinity in a stage's output; how many there were. A broken value in a filter's or a
            reverb's state stays there and would silence the sound until a panic: the stage is reset instead (D170). */
        int scrubBroken(float* left, float* right, int n) noexcept
        {
            int broken = 0;
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite(left[i]))
                {
                    left[i] = 0.0f;
                    ++broken;
                }
                if (! std::isfinite(right[i]))
                {
                    right[i] = 0.0f;
                    ++broken;
                }
            }
            return broken;
        }

        void reportBroken(int stage, int samples) noexcept
        {
            diag::record(diag::Event::stageBroken, stage, samples);
        }

        // The flight recorder's stage timing: two clock reads per stage, kept for the block's slowest one.
        struct StageClock
        {
            StageTiming& slowest;
            int code;
            double start = juce::Time::getMillisecondCounterHiRes();
            ~StageClock()
            {
                const float took = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - start);
                if (took > slowest.milliseconds)
                    slowest = { code, took };
            }
        };
    }

    SoundUnit::SoundUnit()
    {
        for (auto& part : parts)
            part = std::make_unique<Part>();
        for (auto& sent : sentNote)
            sent.fill(-1);
    }

    void SoundUnit::prepare(double sampleRate, int maxBlockSize, std::uint32_t seed)
    {
        const int block = std::max(1, maxBlockSize);
        hostRate = std::max(1000.0, sampleRate);
        parts[0]->prepare(sampleRate, block, seed);
        parts[1]->prepare(sampleRate, block, seed ^ 0xB0000u);
        // A key struck again under a new transpose is a note-off and a note-on; a mode change adds one event.
        for (auto& events : partEvents)
            events.assign(static_cast<std::size_t>(2 * coolsynth::synth::maxEngineEventsPerBlock + 2), {});
        for (auto& slots : inserts)
            for (auto& slot : slots)
                slot.prepare(sampleRate, block);
        for (auto& slot : *masterSlots)
            slot.prepare(sampleRate, block);
        work.setSize(2, block, false, true, true);
        for (auto& buffer : partBuffers)
            buffer.setSize(2, block, false, true, true);
        dryBus.setSize(2, block, false, true, true);
        masterGain.reset(sampleRate, masterGainRampSeconds);
        // Freshly prepared voices are left as they are: a reset would draw their random phases and drifts again, and
        // every preset would start from other random values than it was levelled and auditioned with.
        for (auto& sent : sentNote)
            sent.fill(-1);
        partEventCount.fill(0);
        wasActive = { true, false };
        mixPrimed = false;
        masterGain.setCurrentAndTargetValue(1.0f);
        masterPrimed = false;
        peak = 0.0f;
        freeBeats = 0.0;
    }

    void SoundUnit::panic() noexcept
    {
        for (auto& part : parts)
            part->reset();
        for (auto& sent : sentNote)
            sent.fill(-1);
        for (auto& slots : inserts)
            for (auto& slot : slots)
                slot.clear();
        for (auto& slot : *masterSlots)
            slot.clear();
        partEventCount.fill(0);
        wasActive = { true, false };
        mixPrimed = false;
        masterGain.setCurrentAndTargetValue(1.0f);
        masterPrimed = false;
        peak = 0.0f;
        freeBeats = 0.0;
    }

    SoundUnit::MixGains SoundUnit::gainsFor(const PartMix& mix) noexcept
    {
        const float level = mix.levelDb <= silentLevelDb ? 0.0f : juce::Decibels::decibelsToGain(std::min(mix.levelDb, 12.0f));
        const float pan = std::clamp(mix.pan, -1.0f, 1.0f);
        const float left = level * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const float right = level * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const float fx = std::clamp(mix.fx, 0.0f, 1.0f);
        return { left * fx, right * fx, left * (1.0f - fx), right * (1.0f - fx) };
    }

    void SoundUnit::pushEvent(int partIndex, const EngineMidiEvent& event) noexcept
    {
        auto& events = partEvents[static_cast<std::size_t>(partIndex)];
        auto& count = partEventCount[static_cast<std::size_t>(partIndex)];
        if (count < static_cast<int>(events.size()))
            events[static_cast<std::size_t>(count++)] = event;
    }

    void SoundUnit::pushNote(int partIndex, EngineMidiEventType type, int note, float value, int offset) noexcept
    {
        EngineMidiEvent event;
        event.type = type;
        event.sampleOffset = offset;
        event.noteNumber = static_cast<std::uint8_t>(note);
        event.value = value;
        pushEvent(partIndex, event);
    }

    void SoundUnit::routeEvents(std::span<const EngineMidiEvent> events, const EngineSettings& settings,
                                const std::array<bool, partCount>& active) noexcept
    {
        for (int p = 0; p < partCount; ++p)
        {
            const auto index = static_cast<std::size_t>(p);
            partEventCount[index] = 0;
            if (wasActive[index] && ! active[index])
            {
                // The mode took this part off the keys: it lets go of what it holds and rings out.
                EngineMidiEvent release;
                release.type = EngineMidiEventType::allNotesOff;
                pushEvent(p, release);
                sentNote[index].fill(-1);
            }
            wasActive[index] = active[index];
        }

        const bool both = active[0] && active[1];
        for (const auto& event : events)
        {
            const int key = std::min(127, static_cast<int>(event.noteNumber));
            switch (event.type)
            {
                case EngineMidiEventType::noteOn:
                    for (int p = 0; p < partCount; ++p)
                    {
                        const auto index = static_cast<std::size_t>(p);
                        const auto& mix = settings.mix[index];
                        auto& sent = sentNote[index][static_cast<std::size_t>(key)];

                        bool plays = active[index];
                        if (plays && both)
                        {
                            const int velocity = std::clamp(static_cast<int>(std::lround(event.value * 127.0f)), 1, 127);
                            plays = velocity >= mix.velocityLow && velocity <= mix.velocityHigh
                                    && (settings.partMode != PartMode::split || (key >= mix.keyLow && key <= mix.keyHigh));
                        }
                        const int note = key + mix.transpose;
                        plays = plays && note >= 0 && note <= 127;

                        if (sent >= 0 && (! plays || sent != note))
                        {
                            pushNote(p, EngineMidiEventType::noteOff, sent, 0.0f, event.sampleOffset);
                            sent = -1;
                        }
                        if (plays)
                        {
                            pushNote(p, EngineMidiEventType::noteOn, note, event.value, event.sampleOffset);
                            sent = static_cast<std::int8_t>(note);
                        }
                    }
                    break;

                case EngineMidiEventType::noteOff:
                    for (int p = 0; p < partCount; ++p)
                    {
                        auto& sent = sentNote[static_cast<std::size_t>(p)][static_cast<std::size_t>(key)];
                        if (sent >= 0)
                        {
                            pushNote(p, EngineMidiEventType::noteOff, sent, event.value, event.sampleOffset);
                            sent = -1;
                        }
                    }
                    break;

                case EngineMidiEventType::polyPressure:
                    // A key's pressure goes to the note that key sent each part (its transpose applied).
                    for (int p = 0; p < partCount; ++p)
                        if (const int sent = sentNote[static_cast<std::size_t>(p)][static_cast<std::size_t>(key)]; sent >= 0)
                            pushNote(p, EngineMidiEventType::polyPressure, sent, event.value, event.sampleOffset);
                    break;

                case EngineMidiEventType::allNotesOff:
                case EngineMidiEventType::allSoundOff:
                    for (auto& sent : sentNote)
                        sent.fill(-1);
                    [[fallthrough]];
                default:
                    for (int p = 0; p < partCount; ++p)
                        pushEvent(p, event);
                    break;
            }
        }
    }

    void SoundUnit::render(float* sumLeft, float* sumRight, int n, std::span<const EngineMidiEvent> events,
                           const EngineSettings& settings, const EngineTransportInfo& transport,
                           float fadeFrom, float fadeTo, StageTiming& slowest) noexcept
    {
        const std::array<bool, partCount> active { settings.partMode != PartMode::bOnly, settings.partMode != PartMode::aOnly };
        routeEvents(events, settings, active);

        std::array<int, partCount> voices { voicesWanted(settings.parts[0]), voicesWanted(settings.parts[1]) };
        if (active[0] && active[1])
            voices = voiceLimits(voices);

        // The voices first: both parts, each into its own buffer.
        std::array<bool, partCount> rendered {};
        bool soundOff = false;
        bool phraseStart = false;
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            auto& part = *parts[p];
            // A part the mode leaves out costs nothing once it has rung out.
            if (! active[p] && partEventCount[p] == 0 && ! part.isSounding())
                continue;

            auto& patch = resolved[p];
            patch = settings.parts[p];
            if (patch.playMode == PlayMode::poly)
                patch.polyphony = voices[p];
            patch.arp.enabled = patch.arp.enabled && settings.mix[p].arp;
            patch.tuneSemitones = settings.mix[p].fineCents * 0.01f;

            const StageClock stageClock { slowest, p == 0 ? 1 : 2 };
            part.render(partBuffers[p].getWritePointer(0), partBuffers[p].getWritePointer(1), n,
                        std::span<const EngineMidiEvent>(partEvents[p].data(), static_cast<std::size_t>(partEventCount[p])), patch, transport);
            if (const int broken = scrubBroken(partBuffers[p].getWritePointer(0), partBuffers[p].getWritePointer(1), n); broken > 0)
            {
                part.reset();
                reportBroken(p == 0 ? 1 : 2, broken);
            }
            soundOff = part.consumeSoundOff() || soundOff;
            phraseStart = part.consumePhraseStart() || phraseStart;
            rendered[p] = true;
        }

        if (soundOff)
        {
            for (auto& slots : inserts)
                for (auto& slot : slots)
                    slot.clear();
            for (auto& slot : *masterSlots)
                slot.clear();
        }

        // Tempo and beat: the host's while it plays, otherwise the arp's tempo and a count that
        // starts again with the first note after silence. Synced delays and pumps follow them.
        const bool hostTempo = transport.hostHasTempo && transport.hostBpm > 0.0;
        const double bpm = hostTempo ? transport.hostBpm : std::clamp(static_cast<double>(settings.parts[0].arp.internalTempoBpm), 20.0, 400.0);
        InsertContext beat;
        beat.bpm = bpm;
        beat.beatsPerSample = bpm / 60.0 / hostRate;
        if (hostTempo && transport.hostHasPpq && transport.hostIsPlaying)
        {
            beat.beatsAtStart = transport.hostPpqAtBlockStart;
            freeBeats = transport.hostPpqAtBlockStart + beat.beatsPerSample * n;
        }
        else
        {
            if (phraseStart)
                freeBeats = 0.0;
            beat.beatsAtStart = freeBeats;
            freeBeats += beat.beatsPerSample * n;
        }

        // Then each part through its inserts and into the mix.
        juce::AudioBuffer<float> stereo(work.getArrayOfWritePointers(), 2, n);
        stereo.clear();
        float* l = stereo.getWritePointer(0);
        float* r = stereo.getWritePointer(1);
        float* dryLeft = dryBus.getWritePointer(0);
        float* dryRight = dryBus.getWritePointer(1);
        bool dryUsed = false;
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            const auto target = gainsFor(settings.mix[p]);
            const auto from = mixPrimed ? lastGains[p] : target;
            lastGains[p] = target;
            if (! rendered[p])
                continue;

            juce::AudioBuffer<float> partAudio(partBuffers[p].getArrayOfWritePointers(), 2, n);
            const StageClock insertsClock { slowest, p == 0 ? 3 : 4 };
            for (std::size_t slot = 0; slot < inserts[p].size(); ++slot)
            {
                inserts[p][slot].process(partAudio, withSpace(settings.inserts[p][slot], settings.space), beat);
                if (const int broken = scrubBroken(partAudio.getWritePointer(0), partAudio.getWritePointer(1), n); broken > 0)
                {
                    inserts[p][slot].clear();
                    reportBroken(p == 0 ? 3 : 4, broken);
                }
            }

            const float* pl = partAudio.getReadPointer(0);
            const float* pr = partAudio.getReadPointer(1);
            const bool dry = from.anyDry() || target.anyDry();
            if (dry && ! dryUsed)
            {
                std::fill_n(dryLeft, n, 0.0f);
                std::fill_n(dryRight, n, 0.0f);
                dryUsed = true;
            }

            // The part's gains move across the block, so a level or pan change never steps.
            const float step = 1.0f / static_cast<float>(n);
            const float wetLeftStep = (target.wetLeft - from.wetLeft) * step, wetRightStep = (target.wetRight - from.wetRight) * step;
            float wetLeft = from.wetLeft, wetRight = from.wetRight;
            for (int i = 0; i < n; ++i)
            {
                wetLeft += wetLeftStep;
                wetRight += wetRightStep;
                l[i] += pl[i] * wetLeft;
                r[i] += pr[i] * wetRight;
            }
            if (dry)
            {
                const float dryLeftStep = (target.dryLeft - from.dryLeft) * step, dryRightStep = (target.dryRight - from.dryRight) * step;
                float gainLeft = from.dryLeft, gainRight = from.dryRight;
                for (int i = 0; i < n; ++i)
                {
                    gainLeft += dryLeftStep;
                    gainRight += dryRightStep;
                    dryLeft[i] += pl[i] * gainLeft;
                    dryRight[i] += pr[i] * gainRight;
                }
            }
        }
        mixPrimed = true;

        // The master slots, F1 to F6 in turn.
        for (std::size_t slot = 0; slot < masterSlots->size(); ++slot)
        {
            const StageClock masterClock { slowest, 10 + static_cast<int>(slot) };
            (*masterSlots)[slot].process(stereo, withSpace(settings.master[slot], settings.space), beat);
            if (const int broken = scrubBroken(l, r, n); broken > 0)
            {
                (*masterSlots)[slot].clear();
                reportBroken(10 + static_cast<int>(slot), broken);
            }
        }

        // What the parts kept out of the effects joins here.
        if (dryUsed)
        {
            for (int i = 0; i < n; ++i)
            {
                l[i] += dryLeft[i];
                r[i] += dryRight[i];
            }
        }

        // The sound's own master gain (the first block after prepare starts at the setting: ramping from unity would
        // make the first notes after loading louder than the patch), then the fade, into the engine's sum.
        if (! masterPrimed)
        {
            masterGain.setCurrentAndTargetValue(std::max(0.0f, settings.masterGainLinear));
            masterPrimed = true;
        }
        masterGain.setTargetValue(std::max(0.0f, settings.masterGainLinear));
        const float fadeStep = (fadeTo - fadeFrom) / static_cast<float>(n);
        float fade = fadeFrom;
        float blockPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = masterGain.getNextValue();
            const float a = l[i] * g, b = r[i] * g;
            blockPeak = std::max(blockPeak, std::max(std::abs(a), std::abs(b)));
            fade += fadeStep;
            sumLeft[i] += a * fade;
            sumRight[i] += b * fade;
        }
        peak = blockPeak;
    }
}
