#include "Part.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace coolsynth::synth::v3
{
    void Part::prepare(double hostSampleRate, int maxBlockSize, std::uint32_t seed)
    {
        hostRate = std::max(1000.0, hostSampleRate);
        maxBlock = std::max(1, maxBlockSize);
        WavetableBank::instance(); // built here, off the audio thread, before any slot can switch to it

        const double voiceRate = hostRate * oversampleFactor;
        for (std::size_t i = 0; i < voices.size(); ++i)
            voices[i].prepare(voiceRate, seed + static_cast<std::uint32_t>(i) * 7919u);

        busLeft.assign(static_cast<std::size_t>(maxBlock * oversampleFactor), 0.0f);
        busRight.assign(static_cast<std::size_t>(maxBlock * oversampleFactor), 0.0f);
        frames.assign(static_cast<std::size_t>((maxBlock * oversampleFactor) / Voice::maxSubBlock + 2), {});
        for (std::size_t i = 0; i < globalLfos.size(); ++i)
            globalLfos[i].prepare(voiceRate, seed ^ (0x51ED2700u + static_cast<std::uint32_t>(i)));
        globalEnvelope.setSampleRate(voiceRate);
        appliedGlobalEnvelope.attack = -1.0f; // forces the first apply
        stepModulator.prepare(voiceRate);
        chaos.prepare(voiceRate, seed ^ 0xC4A05000u);
        arpeggiator.prepare(hostRate);

        // The polyphase decimator has a short, fixed group delay: measure it once with an impulse.
        HalfBandDecimator2x probe;
        float peak = 0.0f;
        for (int i = 0; i < 64; ++i)
        {
            const float out = std::abs(probe.process(i == 0 ? 1.0f : 0.0f, 0.0f));
            if (out > peak)
            {
                peak = out;
                decimatorLatency = i;
            }
        }
        generatorLatency = (VaGenerator::latencySamples + oversampleFactor - 1) / oversampleFactor;

        prepared = true;
        reset();
    }

    void Part::reset() noexcept
    {
        for (auto& v : voices)
            v.kill();
        state.fill({});
        heldCount = 0;
        arpeggiator.panic();
        decimatorLeft.reset();
        decimatorRight.reset();
        std::fill(busLeft.begin(), busLeft.end(), 0.0f);
        std::fill(busRight.begin(), busRight.end(), 0.0f);
        bendNormalised = 0.0f;
        wheel = 0.0f;
        sustainDown = false;
        previousArpEnabled = false;
        lastNote = -1;
        monoNote = -1;
        monoVoiceCount = 0;
        globals = {};
        for (auto& l : globalLfos)
            l.reset();
        globalEnvelope.reset();
        stepModulator.reset();
        chaos.reset();
        macrosPrimed = false;
        keyIsDown.fill(false);
        keysDown = 0;
    }

    bool Part::anyVoiceActive() const noexcept
    {
        for (const auto& v : voices)
            if (v.isActive())
                return true;
        return false;
    }

    void Part::trackKeys(const EngineMidiEvent& event, const PartSettings& settings) noexcept
    {
        const auto note = static_cast<std::size_t>(std::clamp(static_cast<int>(event.noteNumber), 0, 127));
        switch (event.type)
        {
            case EngineMidiEventType::noteOn:
            {
                const bool firstOfPhrase = keysDown == 0;
                if (! keyIsDown[note])
                {
                    keyIsDown[note] = true;
                    ++keysDown;
                }
                if (firstOfPhrase || settings.globalEnvelopeTrigger == GenvTrigger::everyNote)
                    globalEnvelope.noteOn(1.0f, 0.0f);
                break;
            }
            case EngineMidiEventType::noteOff:
                if (keyIsDown[note])
                {
                    keyIsDown[note] = false;
                    --keysDown;
                }
                closeGlobalEnvelopeIfUnheld(settings);
                break;
            case EngineMidiEventType::allNotesOff:
            case EngineMidiEventType::resetControllers:
                keyIsDown.fill(false);
                keysDown = 0;
                globalEnvelope.noteOff();
                break;
            case EngineMidiEventType::allSoundOff:
                keyIsDown.fill(false);
                keysDown = 0;
                globalEnvelope.reset();
                break;
            default:
                break;
        }
    }

    void Part::closeGlobalEnvelopeIfUnheld(const PartSettings& settings) noexcept
    {
        const bool arpHolds = settings.arp.enabled && arpeggiator.getLatchedNoteCountForTesting() > 0;
        if (keysDown == 0 && ! sustainDown && ! arpHolds)
            globalEnvelope.noteOff();
    }

    int Part::activeVoiceCountForTesting() const noexcept
    {
        int count = 0;
        for (const auto& v : voices)
            count += v.isActive() ? 1 : 0;
        return count;
    }

    void Part::render(float* left,
                      float* right,
                      int numSamples,
                      std::span<const EngineMidiEvent> events,
                      const PartSettings& settings,
                      const EngineTransportInfo& transport) noexcept
    {
        if (! prepared)
        {
            std::fill_n(left, numSamples, 0.0f);
            std::fill_n(right, numSamples, 0.0f);
            return;
        }

        if (settings.playMode != currentMode)
        {
            // A different play mode owns the voices differently: let everything ring out and start clean.
            releaseAll();
            currentMode = settings.playMode;
        }

        std::size_t first = 0;
        for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlock)
        {
            const int n = std::min(maxBlock, numSamples - chunkStart);
            std::size_t last = first;
            while (last < events.size() && events[last].sampleOffset < chunkStart + n)
                ++last;
            renderChunk(left + chunkStart, right + chunkStart, n, events.subspan(first, last - first), chunkStart, settings, transport);
            first = last;
        }
    }

    void Part::renderChunk(float* left, float* right, int n, std::span<const EngineMidiEvent> events, int offsetBase,
                           const PartSettings& settings, const EngineTransportInfo& transport) noexcept
    {
        const int voiceSamples = n * oversampleFactor;
        std::fill_n(busLeft.data(), voiceSamples, 0.0f);
        std::fill_n(busRight.data(), voiceSamples, 0.0f);
        updateGlobals(settings);

        const bool hostTempo = transport.hostHasTempo && transport.hostBpm > 0.0;
        chunkClock.bpm = hostTempo ? transport.hostBpm : std::clamp(static_cast<double>(settings.arp.internalTempoBpm), 20.0, 400.0);
        chunkClock.playing = hostTempo && transport.hostHasPpq && transport.hostIsPlaying;
        ppqPerHostSample = chunkClock.bpm / 60.0 / hostRate;
        chunkClock.ppq = transport.hostPpqAtBlockStart + static_cast<double>(offsetBase) * ppqPerHostSample;

        // The global envelope reads this block's settings before any of its events can start it.
        auto genvParameters = settings.globalEnvelope;
        genvParameters.velocitySensitivity = 0.0f;
        genvParameters.keyTimeTracking = 0.0f;
        if (settings.globalEnvelopeSync)
        {
            const float scale = static_cast<float>(120.0 / std::max(1.0, chunkClock.bpm));
            genvParameters.delay *= scale;
            genvParameters.attack *= scale;
            genvParameters.hold *= scale;
            genvParameters.decay *= scale;
            genvParameters.release *= scale;
        }
        if (! (genvParameters == appliedGlobalEnvelope))
        {
            globalEnvelope.setParameters(genvParameters);
            appliedGlobalEnvelope = genvParameters;
        }

        const bool arpEnabled = settings.arp.enabled;
        arpeggiator.setParameters(settings.arp);
        arpeggiator.setTransportInfo(transport);

        if (arpEnabled && ! previousArpEnabled)
        {
            // Keys already down when the arp switches on now belong to it; the voices they started let go.
            for (int i = 0; i < heldCount; ++i)
                arpeggiator.onNoteOn(held[static_cast<std::size_t>(i)].note, held[static_cast<std::size_t>(i)].velocity);
            heldCount = 0;
            for (std::size_t i = 0; i < voices.size(); ++i)
                if (state[i].keyDown)
                    releaseVoice(static_cast<int>(i), 0.0f);
            monoVoiceCount = 0;
        }
        previousArpEnabled = arpEnabled;

        // Note-ons reach the arp before it generates this block's steps, note-offs after.
        if (arpEnabled)
            for (const auto& event : events)
                if (event.type == EngineMidiEventType::noteOn)
                    arpeggiator.onNoteOn(static_cast<int>(event.noteNumber), std::clamp(event.value, 0.0f, 1.0f));

        std::array<EngineMidiEvent, coolsynth::synth::maxArpEventsPerBlock> arpEvents {};
        const int arpCount = arpeggiator.generateEventsForBlock(n, hostRate, arpEvents.data(), static_cast<int>(arpEvents.size()));

        if (arpEnabled)
        {
            for (const auto& event : events)
            {
                switch (event.type)
                {
                    case EngineMidiEventType::noteOff:
                        arpeggiator.onNoteOff(static_cast<int>(event.noteNumber));
                        break;
                    case EngineMidiEventType::allNotesOff:
                    case EngineMidiEventType::allSoundOff:
                    case EngineMidiEventType::resetControllers:
                        arpeggiator.onAllNotesOff();
                        break;
                    default:
                        break;
                }
            }
        }

        int rendered = 0;
        std::size_t midiIndex = 0;
        int arpIndex = 0;
        while (midiIndex < events.size() || arpIndex < arpCount)
        {
            const int midiOffset = midiIndex < events.size()
                ? std::clamp(events[midiIndex].sampleOffset - offsetBase, 0, n - 1)
                : std::numeric_limits<int>::max();
            const int arpOffset = arpIndex < arpCount
                ? std::clamp(arpEvents[static_cast<std::size_t>(arpIndex)].sampleOffset, 0, n)
                : std::numeric_limits<int>::max();
            const int next = std::min(midiOffset, arpOffset);
            if (next >= n)
                break;

            if (next > rendered)
            {
                renderSpan(rendered, next - rendered, settings);
                rendered = next;
            }

            // An arp step's note-off at this offset goes before a coincident note-on.
            if (arpOffset == next)
            {
                handleEvent(arpEvents[static_cast<std::size_t>(arpIndex)], settings);
                ++arpIndex;
            }

            if (midiOffset == next && midiIndex < events.size())
            {
                const auto& event = events[midiIndex++];
                trackKeys(event, settings);
                const bool isNote = event.type == EngineMidiEventType::noteOn || event.type == EngineMidiEventType::noteOff;
                if (! (arpEnabled && isNote)) // the arp already has it
                    handleEvent(event, settings);
            }
        }

        if (rendered < n)
            renderSpan(rendered, n - rendered, settings);

        float* bl = busLeft.data();
        float* br = busRight.data();
        for (int i = 0; i < voiceSamples; ++i)
        {
            bl[i] *= voiceHeadroom;
            br[i] *= voiceHeadroom;
        }
        decimatorLeft.processBlock(bl, left, n);
        decimatorRight.processBlock(br, right, n);
    }

    void Part::renderSpan(int start, int count, const PartSettings& settings) noexcept
    {
        const int offset = start * oversampleFactor;
        const int length = count * oversampleFactor;

        // The global sources for this span, one frame per voice sub-block.
        if (! macrosPrimed)
        {
            macroValues = settings.macros;
            macrosPrimed = true;
        }
        const float macroCoefficient = 1.0f - std::exp(-static_cast<float>(Voice::maxSubBlock) / (0.01f * static_cast<float>(hostRate * oversampleFactor)));

        LfoClock clock = chunkClock;
        clock.ppq += static_cast<double>(start) * ppqPerHostSample;
        const double ppqPerVoiceSample = ppqPerHostSample / oversampleFactor;
        const int frameCount = std::min(static_cast<int>(frames.size()), (length + Voice::maxSubBlock - 1) / Voice::maxSubBlock);
        for (int f = 0; f < frameCount; ++f)
        {
            const int samples = std::min(Voice::maxSubBlock, length - f * Voice::maxSubBlock);
            LfoClock frameClock = clock;
            frameClock.ppq += static_cast<double>(f * Voice::maxSubBlock) * ppqPerVoiceSample;
            auto& frame = frames[static_cast<std::size_t>(f)];
            // Order as in trackedGlobalSources: GLFO1, GLFO2, GENV, STEP, CHAOS, MACRO1..4.
            frame.values[0] = globalLfos[0].advance(samples, settings.globalLfos[0], frameClock);
            frame.values[1] = globalLfos[1].advance(samples, settings.globalLfos[1], frameClock);
            frame.values[2] = globalEnvelope.getValue();
            globalEnvelope.advance(samples);
            frame.values[3] = stepModulator.advance(samples, settings.step, frameClock);
            frame.values[4] = chaos.advance(samples, settings.chaosRateHz);
            for (int k = 0; k < macroCount; ++k)
            {
                auto& value = macroValues[static_cast<std::size_t>(k)];
                value += (std::clamp(settings.macros[static_cast<std::size_t>(k)], 0.0f, 1.0f) - value) * macroCoefficient;
                frame.values[static_cast<std::size_t>(5 + k)] = value;
            }
        }
        globals.clock = clock;
        globals.ppqPerSample = ppqPerVoiceSample;
        globals.frames = frames.data();
        globals.frameCount = frameCount;

        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            if (! v.isActive())
                continue;
            v.render(busLeft.data() + offset, busRight.data() + offset, length, settings.voice, globals);
            if (! v.isActive())
            {
                state[i].keyDown = false;
                state[i].sustained = false;
            }
        }
    }

    void Part::updateGlobals(const PartSettings& settings) noexcept
    {
        globals.bendSemitones = bendNormalised * std::clamp(settings.bendRangeSemitones, 0.0f, 48.0f);
        globals.sources.set(ModSource::bend, bendNormalised);
        globals.sources.set(ModSource::wheel, wheel);
    }

    void Part::handleEvent(const EngineMidiEvent& event, const PartSettings& settings) noexcept
    {
        const int note = static_cast<int>(event.noteNumber);
        switch (event.type)
        {
            case EngineMidiEventType::noteOn:
                if (! anyVoiceActive())
                {
                    // The first note after silence restarts the global LFOs and the step modulator.
                    for (std::size_t k = 0; k < globalLfos.size(); ++k)
                        globalLfos[k].restart(settings.globalLfos[k]);
                    if (settings.step.retrigger)
                        stepModulator.restart();
                    phraseStartPending = true;
                }
                if (settings.playMode == PlayMode::poly)
                    polyNoteOn(note, std::clamp(event.value, 0.0f, 1.0f), event.fromArp, settings);
                else
                    monoNoteOn(note, std::clamp(event.value, 0.0f, 1.0f), event.fromArp, settings);
                break;

            case EngineMidiEventType::noteOff:
                if (settings.playMode == PlayMode::poly)
                    polyNoteOff(note, std::clamp(event.value, 0.0f, 1.0f));
                else
                    monoNoteOff(note, std::clamp(event.value, 0.0f, 1.0f), settings);
                break;

            case EngineMidiEventType::pitchBend:
                bendNormalised = std::clamp(event.value, -1.0f, 1.0f);
                updateGlobals(settings);
                break;

            case EngineMidiEventType::modWheel:
                wheel = std::clamp(event.value, 0.0f, 1.0f);
                updateGlobals(settings);
                break;

            case EngineMidiEventType::sustainPedal:
            {
                const bool down = event.value >= 0.5f;
                if (down != sustainDown)
                {
                    sustainDown = down;
                    if (! down)
                    {
                        releaseSustained();
                        closeGlobalEnvelopeIfUnheld(settings);
                    }
                }
                break;
            }

            case EngineMidiEventType::allNotesOff:
                releaseAll();
                sustainDown = false;
                break;

            case EngineMidiEventType::allSoundOff:
                // Silence now: voices, the decimators' memory and (through the engine) effect tails.
                for (auto& v : voices)
                    v.kill();
                state.fill({});
                heldCount = 0;
                monoVoiceCount = 0;
                sustainDown = false;
                decimatorLeft.reset();
                decimatorRight.reset();
                soundOffPending = true;
                break;

            case EngineMidiEventType::resetControllers:
                bendNormalised = 0.0f;
                wheel = 0.0f;
                updateGlobals(settings);
                if (sustainDown)
                {
                    sustainDown = false;
                    releaseSustained();
                }
                break;
        }
    }

    int Part::glideSource(const PartSettings& settings, bool overlapping) const noexcept
    {
        const bool glide = settings.voice.glideSeconds > 0.0f && lastNote >= 0 && (! settings.glideLegatoOnly || overlapping);
        return glide ? lastNote : -1;
    }

    void Part::polyNoteOn(int note, float velocity, bool fromArp, const PartSettings& settings) noexcept
    {
        const bool overlapping = heldCount > 0;
        addHeld(note, velocity);

        NoteStart start;
        start.note = note;
        start.velocity = velocity;
        start.glideFrom = glideSource(settings, overlapping);
        start.fromArp = fromArp;
        const auto order = ++counter;

        // A key that is still sounding takes its own voice again, so repeated notes never stack.
        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            if (voices[i].isActive() && state[i].note == note)
            {
                voices[i].retrigger(start);
                state[i] = { note, true, false, order };
                lastNote = note;
                return;
            }
        }

        const int index = pickVoice(std::clamp(settings.polyphony, 1, maxVoices));
        voices[static_cast<std::size_t>(index)].start(start);
        state[static_cast<std::size_t>(index)] = { note, true, false, order };
        lastNote = note;
    }

    void Part::polyNoteOff(int note, float releaseVelocity) noexcept
    {
        removeHeld(note);
        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            if (! state[i].keyDown || state[i].note != note)
                continue;
            state[i].keyDown = false;
            if (sustainDown)
                state[i].sustained = true;
            else
                voices[i].release(releaseVelocity);
        }
    }

    int Part::pickVoice(int limit) const noexcept
    {
        int best = -1;

        // 1. The silent voice used least recently, so every voice gets its turn.
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        for (int i = 0; i < limit; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            if (! voices[index].isActive() && state[index].order < oldest)
            {
                oldest = state[index].order;
                best = i;
            }
        }
        if (best >= 0)
            return best;

        // 2. The quietest released voice, then 3. the quietest one only the pedal holds.
        for (const bool pedal : { false, true })
        {
            float quietest = std::numeric_limits<float>::max();
            for (int i = 0; i < limit; ++i)
            {
                const auto index = static_cast<std::size_t>(i);
                if (state[index].keyDown || state[index].sustained != pedal)
                    continue;
                const float level = voices[index].ampLevel();
                if (level < quietest)
                {
                    quietest = level;
                    best = i;
                }
            }
            if (best >= 0)
                return best;
        }

        // 4. The oldest held note.
        oldest = std::numeric_limits<std::uint64_t>::max();
        for (int i = 0; i < limit; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            if (state[index].order < oldest)
            {
                oldest = state[index].order;
                best = i;
            }
        }
        return std::max(0, best);
    }

    void Part::monoNoteOn(int note, float velocity, bool fromArp, const PartSettings& settings) noexcept
    {
        const bool overlapping = heldCount > 0;
        addHeld(note, velocity);
        const int target = priorityNote(settings.keyPriority);
        if (overlapping && target == monoNote)
            return; // a key of lower priority: the sounding note stays
        playMono(target, heldVelocity(target), overlapping, fromArp, settings);
    }

    void Part::monoNoteOff(int note, float releaseVelocity, const PartSettings& settings) noexcept
    {
        removeHeld(note);
        if (note != monoNote)
            return;

        if (heldCount > 0)
        {
            // Back to the key that now has priority.
            const int target = priorityNote(settings.keyPriority);
            playMono(target, heldVelocity(target), true, false, settings);
            return;
        }

        for (int i = 0; i < monoVoiceCount; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            if (! state[index].keyDown)
                continue;
            state[index].keyDown = false;
            if (sustainDown)
                state[index].sustained = true;
            else
                voices[index].release(releaseVelocity);
        }
    }

    void Part::playMono(int note, float velocity, bool overlapping, bool fromArp, const PartSettings& settings) noexcept
    {
        const int count = settings.playMode == PlayMode::unison ? std::clamp(settings.unisonVoices, 2, maxUnisonVoices) : 1;
        for (int i = count; i < monoVoiceCount; ++i)
            releaseVoice(i, 0.0f);
        monoVoiceCount = count;

        const bool glide = settings.voice.glideSeconds > 0.0f && monoNote >= 0 && (! settings.glideLegatoOnly || overlapping);
        const auto order = ++counter;
        for (int i = 0; i < count; ++i)
        {
            const float u = count > 1 ? -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
            NoteStart start;
            start.note = note;
            start.velocity = velocity;
            start.glideFrom = glide ? monoNote : -1;
            start.detuneCents = settings.unisonDetuneCents * u * (count > 1 ? 1.0f : 0.0f);
            start.pan = std::clamp(settings.unisonSpread * u, -1.0f, 1.0f);
            start.unisonIndex = i;
            start.unisonCount = count;
            start.fromArp = fromArp;

            auto& v = voices[static_cast<std::size_t>(i)];
            if (! v.isActive())
                v.start(start);
            else if (settings.playMode == PlayMode::legato && overlapping)
                v.legato(start);
            else
                v.retrigger(start);
            state[static_cast<std::size_t>(i)] = { note, true, false, order };
        }
        monoNote = note;
        lastNote = note;
    }

    void Part::addHeld(int note, float velocity) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
        {
            auto& h = held[static_cast<std::size_t>(i)];
            if (h.note == note)
            {
                h.velocity = velocity;
                h.order = ++counter;
                return;
            }
        }
        if (heldCount < maxHeldNotes)
            held[static_cast<std::size_t>(heldCount++)] = { note, velocity, ++counter };
    }

    void Part::removeHeld(int note) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
        {
            if (held[static_cast<std::size_t>(i)].note != note)
                continue;
            for (int j = i + 1; j < heldCount; ++j)
                held[static_cast<std::size_t>(j - 1)] = held[static_cast<std::size_t>(j)];
            --heldCount;
            return;
        }
    }

    int Part::priorityNote(KeyPriority priority) const noexcept
    {
        int chosen = -1;
        std::uint64_t latest = 0;
        for (int i = 0; i < heldCount; ++i)
        {
            const auto& h = held[static_cast<std::size_t>(i)];
            switch (priority)
            {
                case KeyPriority::last:
                    if (chosen < 0 || h.order > latest)
                    {
                        chosen = h.note;
                        latest = h.order;
                    }
                    break;
                case KeyPriority::low:
                    if (chosen < 0 || h.note < chosen)
                        chosen = h.note;
                    break;
                case KeyPriority::high:
                    if (chosen < 0 || h.note > chosen)
                        chosen = h.note;
                    break;
            }
        }
        return chosen;
    }

    float Part::heldVelocity(int note) const noexcept
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[static_cast<std::size_t>(i)].note == note)
                return held[static_cast<std::size_t>(i)].velocity;
        return 1.0f;
    }

    void Part::releaseVoice(int index, float releaseVelocity) noexcept
    {
        auto& s = state[static_cast<std::size_t>(index)];
        s.keyDown = false;
        s.sustained = false;
        voices[static_cast<std::size_t>(index)].release(releaseVelocity);
    }

    void Part::releaseAll() noexcept
    {
        for (int i = 0; i < maxVoices; ++i)
            releaseVoice(i, 0.0f);
        heldCount = 0;
        monoVoiceCount = 0;
    }

    void Part::releaseSustained() noexcept
    {
        for (int i = 0; i < maxVoices; ++i)
            if (state[static_cast<std::size_t>(i)].sustained)
                releaseVoice(i, 0.0f);
    }
}
