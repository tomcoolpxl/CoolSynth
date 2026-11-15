#include "EngineV3.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "SampleGenerator.h"
#include "SampleLibrary.h"
#include "AudioWorkers.h"

namespace coolsynth::synth::v3
{
    bool EngineV3::sampledInstrumentPending(const EngineSettings& settings) noexcept
    {
        const auto& d = SampleGenerator::descriptor();
        const std::array<bool, partCount> playing { settings.partMode != PartMode::bOnly, settings.partMode != PartMode::aOnly };
        for (int p = 0; p < partCount; ++p)
        {
            if (! playing[static_cast<std::size_t>(p)])
                continue;
            for (const auto& generator : settings.parts[static_cast<std::size_t>(p)].voice.generators)
            {
                if (generator.type != generatorType::sample)
                    continue;
                const int which = static_cast<int>(std::lround(descriptorToUnits(d.params[SampleGenerator::sample], generator.p[SampleGenerator::sample])));
                if (which < SampleGenerator::firstInstrument)
                    continue;
                const int instrument = which - SampleGenerator::firstInstrument;
                const int style = static_cast<int>(std::lround(descriptorToUnits(d.params[SampleGenerator::style], generator.p[SampleGenerator::style])));
                auto& library = SampleLibrary::instance();
                if (library.request(instrument, style) == nullptr && ! library.missing(instrument, style))
                    return true;
            }
        }
        return false;
    }

    EngineV3::EngineV3()
    {
        for (auto& unit : units)
            unit = std::make_unique<SoundUnit>();
        keyOwner.fill(-1);
    }

    void EngineV3::prepare(double sampleRate, int maxBlockSize, int)
    {
        const int block = std::max(1, maxBlockSize);
        hostRate = std::max(1000.0, sampleRate);
        // Unit 0 keeps the seeds the parts always had (0x5EED and 0xB5EED), so rendered presets stay as they were.
        for (int u = 0; u < unitCount; ++u)
            units[static_cast<std::size_t>(u)]->prepare(sampleRate, block, 0x5EEDu + 0x10001u * static_cast<std::uint32_t>(u));
        for (auto& events : unitEvents)
            events.assign(static_cast<std::size_t>(2 * coolsynth::synth::maxEngineEventsPerBlock + 16), {});
        pieceEvents.assign(static_cast<std::size_t>(coolsynth::synth::maxEngineEventsPerBlock), {});
        sum.setSize(2, block, false, true, true);
        for (auto& out : unitOut)
            out.setSize(2, block, false, true, true);
        AudioWorkers::instance().prepare(); // the helpers exist before the first block (D164)
        // Every unit idle; none is reset, as each was just prepared (a reset would draw new random phases).
        for (auto& s : state)
        {
            s.phase = Phase::idle;
            s.keysHeld = 0;
            s.pedalDown = false;
            s.releasedSeconds = 0.0;
            s.quietSeconds = 0.0;
            s.fade = 1.0f;
        }
        current = 0;
        state[0].phase = Phase::playing;
        keyOwner.fill(-1);
        controllers = {};
        generationPrimed = false;
        prepared = true;
    }

    void EngineV3::panic() noexcept
    {
        for (int u = 0; u < unitCount; ++u)
            endUnit(u);
        state[static_cast<std::size_t>(current)].phase = Phase::playing;
        keyOwner.fill(-1);
        controllers = {};
        sum.clear();
    }

    void EngineV3::endUnit(int u) noexcept
    {
        units[static_cast<std::size_t>(u)]->panic();
        auto& s = state[static_cast<std::size_t>(u)];
        s.phase = Phase::idle;
        s.keysHeld = 0;
        s.pedalDown = false;
        s.releasedSeconds = 0.0;
        s.quietSeconds = 0.0;
        s.fade = 1.0f;
        for (auto& owner : keyOwner)
            if (owner == u)
                owner = -1;
    }

    void EngineV3::startFading(int u) noexcept
    {
        if (u >= 0 && state[static_cast<std::size_t>(u)].phase == Phase::ringing)
            state[static_cast<std::size_t>(u)].phase = Phase::fading;
    }

    int EngineV3::oldestRinging() const noexcept
    {
        int oldest = -1;
        for (int u = 0; u < unitCount; ++u)
            if (state[static_cast<std::size_t>(u)].phase == Phase::ringing
                && (oldest < 0 || state[static_cast<std::size_t>(u)].order < state[static_cast<std::size_t>(oldest)].order))
                oldest = u;
        return oldest;
    }

    int EngineV3::earlierSoundsRinging() const noexcept
    {
        int count = 0;
        for (const auto& s : state)
            count += s.phase == Phase::ringing || s.phase == Phase::fading ? 1 : 0;
        return count;
    }

    void EngineV3::followSoundChange(const EngineSettings& settings) noexcept
    {
        const auto incoming = settings.parts[0].soundGeneration;
        if (! generationPrimed)
        {
            generation = incoming;
            state[static_cast<std::size_t>(current)].generation = incoming;
            generationPrimed = true;
            return;
        }
        if (incoming == generation)
            return;
        generation = incoming;

        // The same settings loaded again change nothing: a host restoring the state, a preset chosen twice.
        *compareScratch = settings;
        for (auto& part : compareScratch->parts)
            part.soundGeneration = lastSettings->parts[0].soundGeneration;
        auto& playing = state[static_cast<std::size_t>(current)];
        if (std::memcmp(compareScratch.get(), lastSettings.get(), sizeof(EngineSettings)) == 0
            || (! units[static_cast<std::size_t>(current)]->isSounding() && playing.keysHeld == 0))
        {
            playing.generation = incoming; // nothing rings, or nothing changed: the same unit plays on
            return;
        }

        // The sound being played becomes an earlier sound, with the settings it had (D158).
        playing.phase = Phase::ringing;
        playing.order = ++changes;
        *playing.settings = *lastSettings;
        playing.releasedSeconds = 0.0;
        playing.quietSeconds = 0.0;
        playing.fade = 1.0f;

        int ringing = 0;
        for (const auto& s : state)
            ringing += s.phase == Phase::ringing ? 1 : 0;
        while (ringing > earlierSoundsKept)
        {
            startFading(oldestRinging());
            --ringing;
        }

        // A unit for the new sound: an idle one, else the fading one furthest down is cut.
        int next = -1;
        for (int u = 0; u < unitCount && next < 0; ++u)
            if (state[static_cast<std::size_t>(u)].phase == Phase::idle)
                next = u;
        if (next < 0)
        {
            for (int u = 0; u < unitCount; ++u)
                if (state[static_cast<std::size_t>(u)].phase == Phase::fading
                    && (next < 0 || state[static_cast<std::size_t>(u)].fade < state[static_cast<std::size_t>(next)].fade))
                    next = u;
            if (next < 0)
                next = oldestRinging();
            endUnit(next);
        }
        current = next;
        state[static_cast<std::size_t>(current)].phase = Phase::playing;
        state[static_cast<std::size_t>(current)].generation = incoming;
        controllersToCurrent = true; // it starts with the pedal, the bend and the wheels where they are
    }

    void EngineV3::renderUnitTask(void* engine, int index) noexcept
    {
        auto& e = *static_cast<EngineV3*>(engine);
        auto& task = e.tasks[static_cast<std::size_t>(index)];
        auto& out = e.unitOut[static_cast<std::size_t>(task.unit)];
        float* l = out.getWritePointer(0);
        float* r = out.getWritePointer(1);
        std::fill_n(l, e.taskBlock, 0.0f);
        std::fill_n(r, e.taskBlock, 0.0f);
        task.timing = {};
        const auto u = static_cast<std::size_t>(task.unit);
        e.units[u]->render(l, r, e.taskBlock,
                           std::span<const EngineMidiEvent>(e.unitEvents[u].data(), static_cast<std::size_t>(e.unitEventCount[u])),
                           *task.settings, *e.taskTransport, task.fadeFrom, task.fadeTo, task.timing);
    }

    std::uint32_t EngineV3::oldestSoundingGeneration() const noexcept
    {
        std::uint32_t oldest = generation;
        for (const auto& s : state)
            if (s.phase != Phase::idle && static_cast<std::int32_t>(s.generation - oldest) < 0)
                oldest = s.generation;
        return oldest;
    }

    void EngineV3::pushEvent(int u, const EngineMidiEvent& event) noexcept
    {
        auto& events = unitEvents[static_cast<std::size_t>(u)];
        auto& count = unitEventCount[static_cast<std::size_t>(u)];
        if (count < static_cast<int>(events.size()))
            events[static_cast<std::size_t>(count++)] = event;
    }

    void EngineV3::routeEvents(std::span<const EngineMidiEvent> events) noexcept
    {
        unitEventCount.fill(0);
        if (controllersToCurrent)
        {
            for (const auto& held : controllers)
                if (held.seen)
                    pushEvent(current, held.event);
            state[static_cast<std::size_t>(current)].pedalDown = controllers[controllerSustain].seen && controllers[controllerSustain].event.value >= 0.5f;
            controllersToCurrent = false;
        }

        auto toEverySound = [&](const EngineMidiEvent& event)
        {
            for (int u = 0; u < unitCount; ++u)
                if (state[static_cast<std::size_t>(u)].phase != Phase::idle)
                    pushEvent(u, event);
        };
        auto remember = [&](int which, const EngineMidiEvent& event)
        {
            auto held = event;
            held.sampleOffset = 0;
            controllers[static_cast<std::size_t>(which)] = { held, true };
        };

        for (const auto& event : events)
        {
            const auto key = static_cast<std::size_t>(std::min(127, static_cast<int>(event.noteNumber)));
            switch (event.type)
            {
                case EngineMidiEventType::noteOn:
                {
                    const int owner = keyOwner[key];
                    if (owner >= 0 && owner != current)
                    {
                        // The key again before its release reached the sound it started: that note lets go now.
                        auto release = event;
                        release.type = EngineMidiEventType::noteOff;
                        release.value = 0.0f;
                        pushEvent(owner, release);
                        auto& held = state[static_cast<std::size_t>(owner)].keysHeld;
                        held = std::max(0, held - 1);
                    }
                    pushEvent(current, event);
                    if (owner != current)
                        ++state[static_cast<std::size_t>(current)].keysHeld;
                    keyOwner[key] = static_cast<std::int8_t>(current);
                    break;
                }
                case EngineMidiEventType::noteOff:
                {
                    const int owner = keyOwner[key];
                    pushEvent(owner >= 0 ? owner : current, event);
                    if (owner >= 0)
                    {
                        auto& held = state[static_cast<std::size_t>(owner)].keysHeld;
                        held = std::max(0, held - 1);
                        keyOwner[key] = -1;
                    }
                    break;
                }
                case EngineMidiEventType::polyPressure:
                    pushEvent(keyOwner[key] >= 0 ? keyOwner[key] : current, event);
                    break;
                case EngineMidiEventType::sustainPedal:
                    toEverySound(event);
                    for (auto& s : state)
                        if (s.phase != Phase::idle)
                            s.pedalDown = event.value >= 0.5f;
                    remember(controllerSustain, event);
                    break;
                case EngineMidiEventType::pitchBend:
                    toEverySound(event);
                    remember(controllerBend, event);
                    break;
                case EngineMidiEventType::allNotesOff:
                case EngineMidiEventType::resetControllers:
                    toEverySound(event);
                    keyOwner.fill(-1);
                    for (auto& s : state)
                        s.keysHeld = 0;
                    if (event.type == EngineMidiEventType::resetControllers)
                        controllers = {};
                    break;
                case EngineMidiEventType::allSoundOff:
                    // Silence now: every earlier sound ends, the one played hears it.
                    for (int u = 0; u < unitCount; ++u)
                        if (u != current && state[static_cast<std::size_t>(u)].phase != Phase::idle)
                            endUnit(u);
                    pushEvent(current, event);
                    keyOwner.fill(-1);
                    state[static_cast<std::size_t>(current)].keysHeld = 0;
                    break;
                case EngineMidiEventType::modWheel:
                    pushEvent(current, event);
                    remember(controllerWheel, event);
                    break;
                case EngineMidiEventType::channelPressure:
                    pushEvent(current, event);
                    remember(controllerPressure, event);
                    break;
                case EngineMidiEventType::expression:
                    pushEvent(current, event);
                    remember(controllerExpression, event);
                    break;
                default:
                    pushEvent(current, event);
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
        // Memory of an instrument this block may pick up is reused only after the block (D155).
        const SampleLibrary::ReadScope readingSamples;
        const int maxBlock = sum.getNumSamples();
        if (n <= maxBlock)
        {
            renderBlock(buffer, events, settings, transport);
            return;
        }

        // Rare: a host block longer than prepared. Rendered in prepared-size pieces, so nothing grows on the audio
        // thread (D159); each piece gets its own events, offsets from its start, and the beat it starts on.
        std::size_t first = 0;
        for (int start = 0; start < n; start += maxBlock)
        {
            const int length = std::min(maxBlock, n - start);
            std::size_t count = 0;
            while (first < events.size() && events[first].sampleOffset < start + length && count < pieceEvents.size())
            {
                auto event = events[first++];
                event.sampleOffset = std::max(0, event.sampleOffset - start);
                pieceEvents[count++] = event;
            }
            juce::AudioBuffer<float> piece(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, length);
            auto pieceTransport = transport;
            if (pieceTransport.hostHasTempo && pieceTransport.hostBpm > 0.0)
                pieceTransport.hostPpqAtBlockStart += pieceTransport.hostBpm / 60.0 / hostRate * static_cast<double>(start);
            renderBlock(piece, std::span<const EngineMidiEvent>(pieceEvents.data(), count), settings, pieceTransport);
        }
    }

    void EngineV3::renderBlock(juce::AudioBuffer<float>& buffer,
                               std::span<const EngineMidiEvent> events,
                               const EngineSettings& settings,
                               const EngineTransportInfo& transport) noexcept
    {
        const int n = buffer.getNumSamples();
        slowest = {};
        followSoundChange(settings);
        routeEvents(events);

        // Short of time: the oldest earlier sound gives way, as on the Kronos.
        if (load > overloadShare)
            startFading(oldestRinging());
        load = 0.0f;

        // First the bookkeeping, here: holds, fades and what each sounding unit is to render.
        const double seconds = static_cast<double>(n) / hostRate;
        taskTotal = 0;
        for (int u = 0; u < unitCount; ++u)
        {
            auto& s = state[static_cast<std::size_t>(u)];
            if (s.phase == Phase::idle)
                continue;
            if (s.phase == Phase::ringing)
            {
                // Its keys and the pedal up: five seconds more, then it fades.
                if (s.keysHeld == 0 && ! s.pedalDown)
                {
                    s.releasedSeconds += seconds;
                    if (s.releasedSeconds >= holdSeconds)
                        startFading(u);
                }
                else
                    s.releasedSeconds = 0.0;
            }
            const float fadeFrom = s.fade;
            if (s.phase == Phase::fading)
                s.fade = std::max(0.0f, s.fade - static_cast<float>(seconds / fadeSeconds));
            tasks[static_cast<std::size_t>(taskTotal++)] = { u, u == current ? &settings : s.settings.get(), fadeFrom, s.fade, {} };
        }

        // Then the units: on several cores when more than one sounds (D164), each into its own buffer.
        taskBlock = n;
        taskTransport = &transport;
        if (multiCore && taskTotal > 1)
            AudioWorkers::instance().run(&EngineV3::renderUnitTask, this, taskTotal);
        else
            for (int i = 0; i < taskTotal; ++i)
                renderUnitTask(this, i);

        // Added up in unit order, so the sum is the same whichever core rendered what.
        float* sl = sum.getWritePointer(0);
        float* sr = sum.getWritePointer(1);
        std::fill_n(sl, n, 0.0f);
        std::fill_n(sr, n, 0.0f);
        for (int i = 0; i < taskTotal; ++i)
        {
            const auto& task = tasks[static_cast<std::size_t>(i)];
            const float* ul = unitOut[static_cast<std::size_t>(task.unit)].getReadPointer(0);
            const float* ur = unitOut[static_cast<std::size_t>(task.unit)].getReadPointer(1);
            for (int k = 0; k < n; ++k)
            {
                sl[k] += ul[k];
                sr[k] += ur[k];
            }
            if (task.timing.milliseconds > slowest.milliseconds)
                slowest = task.timing;
            const int u = task.unit;
            if (u == current)
                continue;
            // An earlier sound ends once faded out, or silent a quarter of a second (its tails gone).
            auto& s = state[static_cast<std::size_t>(u)];
            const bool silent = ! units[static_cast<std::size_t>(u)]->isSounding() && units[static_cast<std::size_t>(u)]->lastPeak() < quietLevel;
            s.quietSeconds = silent ? s.quietSeconds + seconds : 0.0;
            if (s.fade <= 0.0f || s.quietSeconds >= quietSeconds)
                endUnit(u);
        }

        for (int i = 0; i < n; ++i)
        {
            clippedSamples += (std::abs(sl[i]) > 0.8f ? 1 : 0) + (std::abs(sr[i]) > 0.8f ? 1 : 0);
            sl[i] = softClip(sl[i]);
            sr[i] = softClip(sr[i]);
        }

        const int channels = buffer.getNumChannels();
        if (channels == 1)
        {
            float* out = buffer.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (sl[i] + sr[i]);
        }
        else
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom(ch, 0, sum, std::min(ch, 1), 0, n);

        *lastSettings = settings;
    }
}
