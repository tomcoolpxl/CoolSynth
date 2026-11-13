#include "EngineV3.h"

#include <algorithm>
#include <cmath>

namespace coolsynth::synth::v3
{
    EngineV3::EngineV3()
    {
        for (auto& part : parts)
            part = std::make_unique<Part>();
        for (auto& sent : sentNote)
            sent.fill(-1);
    }

    void EngineV3::prepare(double sampleRate, int maxBlockSize, int)
    {
        const int block = std::max(1, maxBlockSize);
        hostRate = std::max(1000.0, sampleRate);
        pumpPhase = 0.0;
        parts[0]->prepare(sampleRate, block, 0x5EEDu);
        parts[1]->prepare(sampleRate, block, 0xB5EEDu);
        // A key struck again under a new transpose is a note-off and a note-on; a mode change adds one event.
        for (auto& events : partEvents)
            events.assign(static_cast<std::size_t>(2 * coolsynth::synth::maxEngineEventsPerBlock + 2), {});
        partEventCount.fill(0);
        for (auto& sent : sentNote)
            sent.fill(-1);
        wasActive = { true, false };
        mixPrimed = false;
        fxRack.prepare(sampleRate, block, 2);
        work.setSize(2, block, false, true, true);
        partBuffer.setSize(2, block, false, true, true);
        dryBus.setSize(2, block, false, true, true);
        masterGain.reset(sampleRate, masterGainRampSeconds);
        masterGain.setCurrentAndTargetValue(1.0f);
        masterPrimed = false;
        prepared = true;
    }

    void EngineV3::panic() noexcept
    {
        for (auto& part : parts)
            part->reset();
        for (auto& sent : sentNote)
            sent.fill(-1);
        fxRack.clear();
        work.clear();
        partBuffer.clear();
        dryBus.clear();
    }

    EngineV3::MixGains EngineV3::gainsFor(const PartMix& mix) noexcept
    {
        const float level = mix.levelDb <= silentLevelDb ? 0.0f : juce::Decibels::decibelsToGain(std::min(mix.levelDb, 12.0f));
        const float pan = std::clamp(mix.pan, -1.0f, 1.0f);
        const float left = level * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const float right = level * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const float fx = std::clamp(mix.fx, 0.0f, 1.0f);
        return { left * fx, right * fx, left * (1.0f - fx), right * (1.0f - fx) };
    }

    void EngineV3::pushEvent(int partIndex, const EngineMidiEvent& event) noexcept
    {
        auto& events = partEvents[static_cast<std::size_t>(partIndex)];
        auto& count = partEventCount[static_cast<std::size_t>(partIndex)];
        if (count < static_cast<int>(events.size()))
            events[static_cast<std::size_t>(count++)] = event;
    }

    void EngineV3::pushNote(int partIndex, EngineMidiEventType type, int note, float value, int offset) noexcept
    {
        EngineMidiEvent event;
        event.type = type;
        event.sampleOffset = offset;
        event.noteNumber = static_cast<std::uint8_t>(note);
        event.value = value;
        pushEvent(partIndex, event);
    }

    void EngineV3::routeEvents(std::span<const EngineMidiEvent> events, const EngineSettings& settings,
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

    void EngineV3::render(juce::AudioBuffer<float>& buffer,
                          std::span<const EngineMidiEvent> events,
                          const EngineSettings& settings,
                          const EngineTransportInfo& transport) noexcept
    {
        const int n = buffer.getNumSamples();
        if (! prepared || n <= 0)
        {
            buffer.clear();
            return;
        }

        // Rare: a host block longer than prepared. The buffers grow once; the parts split on their own.
        if (work.getNumSamples() < n)
        {
            work.setSize(2, n, false, false, true);
            partBuffer.setSize(2, n, false, false, true);
            dryBus.setSize(2, n, false, false, true);
        }

        const std::array<bool, partCount> active { settings.partMode != PartMode::bOnly, settings.partMode != PartMode::aOnly };
        routeEvents(events, settings, active);

        std::array<int, partCount> voices { voicesWanted(settings.parts[0]), voicesWanted(settings.parts[1]) };
        if (active[0] && active[1])
            voices = voiceLimits(voices);

        juce::AudioBuffer<float> stereo(work.getArrayOfWritePointers(), 2, n);
        stereo.clear();
        float* l = stereo.getWritePointer(0);
        float* r = stereo.getWritePointer(1);
        float* dryLeft = dryBus.getWritePointer(0);
        float* dryRight = dryBus.getWritePointer(1);
        bool dryUsed = false;
        bool soundOff = false;
        bool phraseStart = false;

        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            auto& part = *parts[p];
            const auto target = gainsFor(settings.mix[p]);
            const auto from = mixPrimed ? lastGains[p] : target;
            lastGains[p] = target;

            // A part the mode leaves out costs nothing once it has rung out.
            if (! active[p] && partEventCount[p] == 0 && ! part.isSounding())
                continue;

            auto& patch = resolved[p];
            patch = settings.parts[p];
            if (patch.playMode == PlayMode::poly)
                patch.polyphony = voices[p];
            patch.arp.enabled = patch.arp.enabled && settings.mix[p].arp;
            patch.tuneSemitones = settings.mix[p].fineCents * 0.01f;

            float* pl = partBuffer.getWritePointer(0);
            float* pr = partBuffer.getWritePointer(1);
            part.render(pl, pr, n, std::span<const EngineMidiEvent>(partEvents[p].data(), static_cast<std::size_t>(partEventCount[p])),
                        patch, transport);
            soundOff = part.consumeSoundOff() || soundOff;
            phraseStart = part.consumePhraseStart() || phraseStart;

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

        if (soundOff)
            fxRack.clear();

        // A synced delay takes its time from the tempo the parts play at: the host's, else the arp's.
        const bool hostTempo = transport.hostHasTempo && transport.hostBpm > 0.0;
        const double bpm = hostTempo ? transport.hostBpm : std::clamp(static_cast<double>(settings.parts[0].arp.internalTempoBpm), 20.0, 400.0);
        auto delay = settings.delay;
        if (delay.sync)
            delay.timeMs = coolsynth::synth::delayDivisionMs(delay.division, bpm);

        // The pump follows the beat: the host's song position while it plays, otherwise its own
        // count at the tempo, restarted with the first note after silence.
        coolsynth::synth::ExtraFxParameters extra { settings.lofi, settings.flanger, settings.eq, settings.pump };
        {
            const auto& division = coolsynth::synth::delayDivisions[static_cast<std::size_t>(
                std::clamp(settings.pump.division, 0, static_cast<int>(coolsynth::synth::delayDivisions.size()) - 1))];
            extra.pump.phasePerSample = bpm / 60.0 / division.beats / hostRate;
            if (hostTempo && transport.hostHasPpq && transport.hostIsPlaying)
                pumpPhase = transport.hostPpqAtBlockStart / division.beats;
            else if (phraseStart)
                pumpPhase = 0.0;
            pumpPhase -= std::floor(pumpPhase);
            extra.pump.phaseAtStart = pumpPhase;
            pumpPhase += extra.pump.phasePerSample * n;
        }
        fxRack.process(stereo, settings.drive, settings.phaser, settings.chorus, delay, settings.reverb, settings.compressor, extra);

        // What the parts kept out of the effects joins here.
        if (dryUsed)
        {
            for (int i = 0; i < n; ++i)
            {
                l[i] += dryLeft[i];
                r[i] += dryRight[i];
            }
        }

        // The first block after prepare starts at the setting; ramping from unity would make the
        // first notes after loading louder than the patch.
        if (! masterPrimed)
        {
            masterGain.setCurrentAndTargetValue(std::max(0.0f, settings.masterGainLinear));
            masterPrimed = true;
        }
        masterGain.setTargetValue(std::max(0.0f, settings.masterGainLinear));
        for (int i = 0; i < n; ++i)
        {
            const float g = masterGain.getNextValue();
            l[i] = softClip(l[i] * g);
            r[i] = softClip(r[i] * g);
        }

        const int channels = buffer.getNumChannels();
        if (channels == 1)
        {
            float* out = buffer.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (l[i] + r[i]);
            return;
        }
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, stereo, std::min(ch, 1), 0, n);
    }
}
