#include "SynthEngine.h"

namespace
{
    std::optional<coolsynth::synth::EngineMidiEvent> makeLegacyEngineEvent(const juce::MidiMessage& message,
                                                                           int sampleOffset) noexcept
    {
        using namespace coolsynth::synth;

        EngineMidiEvent event;
        event.sampleOffset = juce::jmax(0, sampleOffset);

        if (message.isNoteOn())
        {
            event.type = EngineMidiEventType::noteOn;
            event.noteNumber = static_cast<uint8_t>(message.getNoteNumber());
            event.value = juce::jlimit(0.0f, 1.0f, message.getFloatVelocity());
            return event;
        }

        if (message.isNoteOff())
        {
            event.type = EngineMidiEventType::noteOff;
            event.noteNumber = static_cast<uint8_t>(message.getNoteNumber());
            return event;
        }

        if (message.isPitchWheel())
        {
            const auto normalized = static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f;
            event.type = EngineMidiEventType::pitchBend;
            event.value = juce::jlimit(-1.0f, 1.0f, normalized);
            return event;
        }

        if (message.isControllerOfType(1))
        {
            event.type = EngineMidiEventType::modWheel;
            event.value = static_cast<float>(message.getControllerValue()) / 127.0f;
            return event;
        }

        if (message.isControllerOfType(64))
        {
            event.type = EngineMidiEventType::sustainPedal;
            event.value = message.getControllerValue() >= 64 ? 1.0f : 0.0f;
            return event;
        }

        return std::nullopt;
    }
}

namespace coolsynth::synth
{
    void SynthEngine::prepare(double sampleRate, int samplesPerBlock, int outputChannelCount)
    {
        v2Engine.prepare(sampleRate, samplesPerBlock, outputChannelCount);
    }

    void SynthEngine::releaseResources() noexcept
    {
        v2Engine.releaseResources();
    }

    void SynthEngine::render(juce::AudioBuffer<float>& outputBuffer,
                             juce::MidiBuffer& midiMessages,
                             const BlockRenderParameters& parameters)
    {
        std::array<EngineMidiEvent, maxEngineEventsPerBlock> events {};
        int eventCount = 0;

        for (const auto metadata : midiMessages)
        {
            if (eventCount >= static_cast<int>(events.size()))
                break;

            if (const auto event = makeLegacyEngineEvent(metadata.getMessage(), metadata.samplePosition))
                events[static_cast<size_t>(eventCount++)] = *event;
        }

        v2Engine.render(outputBuffer,
                        std::span<const EngineMidiEvent>(events.data(), static_cast<size_t>(eventCount)),
                        mapToV2Parameters(parameters));
    }

    void SynthEngine::panic() noexcept
    {
        v2Engine.panic();
    }

    BlockRenderParametersV2 SynthEngine::mapToV2Parameters(const BlockRenderParameters& parameters) noexcept
    {
        BlockRenderParametersV2 mapped;
        mapped.ampEnvelope = parameters.ampEnvelope;
        mapped.filter.cutoffHz = parameters.filter.cutoffHz;
        mapped.filter.resonanceNormalized = parameters.filter.resonanceNormalized;
        mapped.delay.enabled = parameters.delay.mix > 0.0f || parameters.delay.feedback > 0.0f;
        mapped.delay.timeMs = parameters.delay.timeMs;
        mapped.delay.feedback = parameters.delay.feedback;
        mapped.delay.mix = parameters.delay.mix;
        mapped.masterGainLinear = parameters.masterGainLinear;

        switch (parameters.waveform)
        {
            case coolsynth::parameters::WaveformChoice::sine:
                mapped.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::sine;
                break;

            case coolsynth::parameters::WaveformChoice::square:
                mapped.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
                break;

            case coolsynth::parameters::WaveformChoice::saw:
            default:
                mapped.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
                break;
        }

        mapped.oscA.level = 1.0f;
        mapped.oscB.level = 0.0f;
        return mapped;
    }
}
