#include "Voice.h"

#include <algorithm>
#include <cmath>

#include "DspMath.h"

namespace coolsynth::synth::v3
{
    namespace
    {
        constexpr float kSilentLevel = 1.0e-4f;              // a voice this quiet is restarted without a fade
        constexpr float kDriftSmoothingSeconds = 0.25f;      // each of two cascaded one-poles
        constexpr float kDriftMinHoldSeconds = 0.3f;
        constexpr float kDriftHoldRangeSeconds = 0.5f;
    }

    void Voice::prepare(double newSampleRate, std::uint32_t seed)
    {
        sampleRate = static_cast<float>(std::max(1000.0, newSampleRate));
        for (auto& g : generators)
        {
            g.slot.prepare(newSampleRate, maxSubBlock);
            g.base.prepare(newSampleRate, maxSubBlock);
        }
        for (auto& p : processors)
        {
            p.slot.prepare(newSampleRate, maxSubBlock);
            p.base.prepare(newSampleRate, maxSubBlock);
        }
        for (auto& e : envelopes)
            e.setSampleRate(newSampleRate);
        for (std::size_t i = 0; i < lfos.size(); ++i)
            lfos[i].prepare(newSampleRate, seed * 31u + static_cast<std::uint32_t>(i) + 1u);
        for (auto& applied : appliedEnvelopes)
            applied.attack = -1.0f; // forces the first apply

        stealTotal = std::max(1, static_cast<int>(std::lround(stealFadeSeconds * newSampleRate)));
        rng = seed * 2654435761u + 0x9E3779B9u;
        if (rng == 0)
            rng = 1;
        driftStage = driftValue = driftTarget = 0.0f;
        driftSamplesToNext = 0;
        prepared = true;
        reset();
    }

    void Voice::reset() noexcept
    {
        restartModules();
        for (auto& l : lfos)
            l.reset();
        lfoRestartPending = false;
        pending = false;
        pendingReleased = false;
        released = false;
        stealRemaining = 0;
        envelopeHold = 0;
    }

    bool Voice::isActive() const noexcept
    {
        return prepared && (pending || stealRemaining > 0 || envelopes[0].isActive());
    }

    void Voice::restartModules() noexcept
    {
        for (auto& g : generators)
        {
            g.slot.reset();
            g.mixedLevel = 0.0f;
        }
        for (auto& p : processors)
            p.slot.reset();
        for (auto& e : envelopes)
            e.reset();
    }

    void Voice::start(const NoteStart& start) noexcept
    {
        if (! prepared)
            return;

        // The note always waits for the next render, which reads the patch before it starts the
        // note: starting here would run the envelopes with the previous patch's times. A sounding
        // voice first fades its old note out; a start during that fade replaces the waiting note.
        const bool sounding = stealRemaining > 0 || (envelopes[0].isActive() && envelopes[0].getValue() > kSilentLevel);
        if (sounding && stealRemaining == 0)
            stealRemaining = stealTotal;
        pendingNote = start;
        pending = true;
        pendingReleased = false;
    }

    void Voice::startPendingNote() noexcept
    {
        restartModules();
        pending = false;
        envelopeHold = generatorLatencySamples;
        beginNote(pendingNote);
        snapToSettings();
        if (pendingReleased)
        {
            pendingReleased = false;
            release(releaseVelocity);
        }
    }

    void Voice::retrigger(const NoteStart& start) noexcept
    {
        if (! isActive() || pending || stealRemaining > 0)
        {
            Voice::start(start);
            return;
        }
        note = start;
        released = false;
        lfoRestartPending = true;
        retarget(start.note, start.detuneCents, start.glideFrom >= 0);
        const float keyOctaves = (static_cast<float>(start.note) - 60.0f) / 12.0f;
        for (auto& e : envelopes)
            e.noteOn(start.velocity, keyOctaves);
    }

    void Voice::legato(const NoteStart& start) noexcept
    {
        if (! isActive())
        {
            Voice::start(start);
            return;
        }
        if (pending)
        {
            pendingNote.note = start.note;
            pendingNote.detuneCents = start.detuneCents;
            return;
        }
        note.note = start.note;
        note.detuneCents = start.detuneCents;
        retarget(start.note, start.detuneCents, start.glideFrom >= 0);
    }

    void Voice::release(float velocity) noexcept
    {
        if (pending)
        {
            pendingReleased = true;
            releaseVelocity = velocity;
            return;
        }
        if (! isActive() || released)
            return;
        released = true;
        releaseVelocity = velocity;
        for (auto& e : envelopes)
            e.noteOff();
    }

    void Voice::beginNote(const NoteStart& start) noexcept
    {
        note = start;
        released = false;
        releaseVelocity = 0.0f;
        targetPitch = static_cast<float>(start.note) + start.detuneCents * 0.01f;
        currentPitch = start.glideFrom >= 0 ? static_cast<float>(start.glideFrom) + start.detuneCents * 0.01f : targetPitch;
        glideSpan = std::abs(targetPitch - currentPitch);
        noteRandom = nextRandom();
        lfoRestartPending = true;

        NoteContext context;
        context.noteNumber = start.note;
        context.velocity = start.velocity;
        context.glideFromNote = start.glideFrom;
        context.unisonIndex = start.unisonIndex;
        context.unisonCount = start.unisonCount;
        context.fromArp = start.fromArp;
        for (auto& g : generators)
            g.slot.noteOn(context);
        for (auto& p : processors)
            p.slot.noteOn(context);

        const float keyOctaves = (static_cast<float>(start.note) - 60.0f) / 12.0f;
        for (auto& e : envelopes)
            e.noteOn(start.velocity, keyOctaves);
    }

    void Voice::retarget(int noteNumber, float detuneCents, bool glide) noexcept
    {
        targetPitch = static_cast<float>(noteNumber) + detuneCents * 0.01f;
        if (! glide)
            currentPitch = targetPitch;
        glideSpan = std::abs(targetPitch - currentPitch);
    }

    void Voice::applySettings(const VoiceSettings& settings) noexcept
    {
        for (std::size_t i = 0; i < generators.size(); ++i)
        {
            auto& g = generators[i];
            const auto& s = settings.generators[i];
            const int type = std::clamp(s.type, 0, GeneratorSlot::typeCount - 1);
            if (type != g.slot.typeIndex())
                g.slot.requestType(type);
            for (int k = 0; k < maxSlotParameters; ++k)
                g.base.setTarget(k, s.p[static_cast<std::size_t>(k)]);
            g.base.setLevelTarget(s.level);
            g.base.setPanTarget(s.pan);
            g.base.setPitchTarget(s.pitchSemitones);
            g.base.setAmountTarget(s.combineAmount);
        }

        for (std::size_t i = 0; i < processors.size(); ++i)
        {
            auto& p = processors[i];
            const auto& s = settings.processors[i];
            const int type = std::clamp(s.type, 0, ProcessorSlot::typeCount - 1);
            if (type != p.slot.typeIndex())
                p.slot.requestType(type);
            for (int k = 0; k < maxSlotParameters; ++k)
                p.base.setTarget(k, s.p[static_cast<std::size_t>(k)]);
        }

        for (std::size_t i = 0; i < envelopes.size(); ++i)
        {
            auto parameters = settings.envelopes[i];
            if (i == 0)
            {
                // The amplifier never switches on or off in less than a click-free time.
                parameters.attack = std::max(parameters.attack, minAmpAttackSeconds);
                parameters.release = std::max(parameters.release, minAmpReleaseSeconds);
            }
            if (! (parameters == appliedEnvelopes[i]))
            {
                envelopes[i].setParameters(parameters);
                appliedEnvelopes[i] = parameters;
            }
        }
    }

    void Voice::snapToSettings() noexcept
    {
        for (auto& g : generators)
        {
            g.slot.switchTypeNow();
            g.base.snapToTargets();
            g.mixedLevel = g.base.levelValue();
        }
        for (auto& p : processors)
        {
            p.slot.switchTypeNow();
            p.base.snapToTargets();
        }
    }

    void Voice::render(float* left, float* right, int numSamples, const VoiceSettings& settings, const VoiceGlobals& globals) noexcept
    {
        if (! isActive())
            return;

        applySettings(settings);
        if (pending && stealRemaining == 0)
            startPendingNote();

        int done = 0;
        while (done < numSamples && isActive())
        {
            int n = std::min(maxSubBlock, numSamples - done);
            if (stealRemaining > 0)
                n = std::min(n, stealRemaining);

            renderSubBlock(left + done, right + done, done, n, settings, globals);
            done += n;

            if (stealRemaining > 0)
            {
                stealRemaining -= n;
                if (stealRemaining == 0)
                    startPendingNote(); // the old note is silent now: start the waiting one from a clean slate
            }
        }
    }

    void Voice::renderSubBlock(float* left, float* right, int offset, int n, const VoiceSettings& settings, const VoiceGlobals& globals) noexcept
    {
        if (lfoRestartPending)
        {
            for (std::size_t i = 0; i < lfos.size(); ++i)
                lfos[i].restart(settings.lfos[i]);
            lfoRestartPending = false;
        }

        advancePitch(n, settings);
        advanceDrift(n);

        const float pitch = currentPitch + globals.bendSemitones + driftValue * settings.driftCents * 0.01f;
        const float noteHz = dsp::pitchToHz(pitch);

        // At a fresh note the envelopes wait while the generators' first samples are still on their way.
        const int held = std::min(envelopeHold, n);
        envelopeHold -= held;
        float* amp = ampBuffer.data();
        for (int s = 0; s < n; ++s)
            amp[s] = s < held ? 0.0f : envelopes[0].getNextSample();
        const float env2 = envelopes[1].advance(n - held);
        const float env3 = envelopes[2].advance(n - held);

        sources = globals.sources;
        if (globals.frames != nullptr && globals.frameCount > 0)
        {
            const auto& frame = globals.frames[std::min(offset / maxSubBlock, globals.frameCount - 1)];
            for (std::size_t k = 0; k < trackedGlobalSources.size(); ++k)
                sources.set(trackedGlobalSources[k], frame.values[k]);
        }

        LfoClock clock = globals.clock;
        clock.ppq += static_cast<double>(offset) * globals.ppqPerSample;
        const float wheelAmount = std::clamp(settings.wheelToLfo1, 0.0f, 1.0f);
        const float lfo1Depth = 1.0f - wheelAmount + wheelAmount * globals.sources.get(ModSource::wheel);
        sources.set(ModSource::lfo1, lfo1Depth * lfos[0].advance(n, settings.lfos[0], clock));
        sources.set(ModSource::lfo2, lfos[1].advance(n, settings.lfos[1], clock));

        sources.set(ModSource::velocity, note.velocity);
        sources.set(ModSource::releaseVelocity, releaseVelocity);
        sources.set(ModSource::key, std::clamp((static_cast<float>(note.note) - 60.0f) / 64.0f, -1.0f, 1.0f));
        sources.set(ModSource::noteRandom, noteRandom);
        sources.set(ModSource::drift, driftValue);
        sources.set(ModSource::env1, amp[n - 1]);
        sources.set(ModSource::env2, env2);
        sources.set(ModSource::env3, env3);

        float* mixL = mixLeft.data();
        float* mixR = mixRight.data();
        std::fill_n(mixL, n, 0.0f);
        std::fill_n(mixR, n, 0.0f);

        GeneratorLink previous {};
        for (std::size_t i = 0; i < generators.size(); ++i)
        {
            auto& g = generators[i];
            const auto& gs = settings.generators[i];

            g.base.step(n);
            SlotOffsets offsets;
            offsets.pan = note.pan;
            SlotParams params = buildSlotParams(g.slot.descriptor(), g.base, offsets);
            params.frequencyHz = dsp::pitchToHz((gs.keyTrack ? pitch : 60.0f) + params.pitchSemitones);
            params.combine = i == 0 ? Combine::off : gs.combine;

            g.slot.setLink(params.combine != Combine::off ? previous : GeneratorLink {});
            g.slot.process(g.left.data(), g.right.data(), n, params);
            previous = g.slot.link();

            const float from = g.mixedLevel;
            const float to = params.level;
            g.mixedLevel = to;
            if (from <= 0.0f && to <= 0.0f)
                continue;

            const float step = (to - from) / static_cast<float>(n);
            float level = from;
            const float* gl = g.left.data();
            const float* gr = g.right.data();
            for (int s = 0; s < n; ++s)
            {
                level += step;
                mixL[s] += gl[s] * level;
                mixR[s] += gr[s] * level;
            }
        }

        for (std::size_t j = 0; j < processors.size(); ++j)
        {
            auto& p = processors[j];
            p.base.step(n);
            SlotOffsets offsets;
            if (j == 0)
                offsets.p[0] = settings.env2ToP1 * env2 + settings.velocityToP1 * note.velocity;
            SlotParams params = buildSlotParams(p.slot.descriptor(), p.base, offsets);
            params.frequencyHz = noteHz;
            p.slot.process(mixL, mixR, n, params);
        }

        const float fadeScale = 1.0f / static_cast<float>(stealTotal);
        for (int s = 0; s < n; ++s)
        {
            float gain = amp[s];
            if (stealRemaining > 0)
                gain *= static_cast<float>(stealRemaining - 1 - s) * fadeScale;
            left[s] += mixL[s] * gain;
            right[s] += mixR[s] * gain;
        }
    }

    void Voice::advancePitch(int n, const VoiceSettings& settings) noexcept
    {
        if (currentPitch == targetPitch)
            return;
        if (settings.glideSeconds <= 0.0f || glideSpan <= 0.0f)
        {
            currentPitch = targetPitch;
            return;
        }
        const float step = glideSpan * static_cast<float>(n) / (settings.glideSeconds * sampleRate);
        const float remaining = targetPitch - currentPitch;
        currentPitch = std::abs(remaining) <= step ? targetPitch : currentPitch + std::copysign(step, remaining);
    }

    void Voice::advanceDrift(int n) noexcept
    {
        driftSamplesToNext -= n;
        if (driftSamplesToNext <= 0)
        {
            driftTarget = nextRandom();
            const float holdSeconds = kDriftMinHoldSeconds + kDriftHoldRangeSeconds * 0.5f * (1.0f + nextRandom());
            driftSamplesToNext = static_cast<int>(holdSeconds * sampleRate);
        }
        const float c = 1.0f - std::exp(-static_cast<float>(n) / (kDriftSmoothingSeconds * sampleRate));
        driftStage += (driftTarget - driftStage) * c;
        driftValue += (driftStage - driftValue) * c;
    }

    float Voice::nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<float>(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
}
