#include <cmath>

#include <juce_core/juce_core.h>

#include "standalone/StandaloneMidiInput.h"
#include "standalone/SettingsStore.h"
#include "synth/SynthEngine.h"
#include "synth/SynthEngineV2.h"
#include "synth/SynthVoice.h"

namespace
{
    coolsynth::synth::EnvelopeParameters makeStressEnvelope() noexcept
    {
        coolsynth::synth::EnvelopeParameters parameters;
        parameters.attackSeconds = 0.001f;
        parameters.decaySeconds = 0.01f;
        parameters.sustainLevel = 1.0f;
        parameters.releaseSeconds = 0.01f;
        return parameters;
    }

    void expectBufferFiniteAndBounded(juce::UnitTest& test,
                                      const juce::AudioBuffer<float>& buffer,
                                      float absoluteLimit)
    {
        bool allFinite = true;
        float maxAbs = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const float value = buffer.getSample(channel, sample);
                allFinite = allFinite && std::isfinite(value);
                maxAbs = juce::jmax(maxAbs, std::abs(value));
            }
        }

        test.expect(allFinite);
        test.expect(maxAbs < absoluteLimit);
    }

    float computePeakAbs(const juce::AudioBuffer<float>& buffer,
                         int startSample,
                         int endSample)
    {
        float peak = 0.0f;
        const auto clampedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
        const auto clampedEnd = juce::jlimit(clampedStart, buffer.getNumSamples(), endSample);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = clampedStart; sample < clampedEnd; ++sample)
                peak = juce::jmax(peak, std::abs(buffer.getSample(channel, sample)));

        return peak;
    }

    float computeAbsoluteDifferenceSum(const juce::AudioBuffer<float>& left,
                                       const juce::AudioBuffer<float>& right)
    {
        float difference = 0.0f;

        for (int channel = 0; channel < juce::jmin(left.getNumChannels(), right.getNumChannels()); ++channel)
        {
            for (int sample = 0; sample < juce::jmin(left.getNumSamples(), right.getNumSamples()); ++sample)
                difference += std::abs(left.getSample(channel, sample) - right.getSample(channel, sample));
        }

        return difference;
    }
}

class StandaloneMidiInputTests final : public juce::UnitTest
{
public:
    StandaloneMidiInputTests() : juce::UnitTest("StandaloneMidiInput", "CoolSynth") {}

    void runTest() override
    {
        beginTest("selected_device_disconnect_reports_disconnected_and_clears_transient_input_state");
        {
            juce::AudioDeviceManager deviceManager;
            juce::PropertySet props;
            coolsynth::standalone::StandaloneSettingsStore store(props);

            juce::Array<juce::MidiDeviceInfo> availableDevices;
            availableDevices.add({ "Controller", "device-1" });

            int disconnectCallbackCount = 0;

            coolsynth::standalone::StandaloneMidiInputController controller(
                deviceManager,
                &store,
                {},
                [&disconnectCallbackCount]
                {
                    ++disconnectCallbackCount;
                },
                [&availableDevices]
                {
                    return availableDevices;
                },
                [](const juce::String&, bool)
                {
                });

            controller.refreshDeviceListForTesting();
            expectEquals(controller.getSnapshot().availableInputs.size(), 1);

            const auto selectedIdentifier = controller.getSnapshot().availableInputs[0].identifier;
            expect(selectedIdentifier == "device-1");
            expect(controller.selectDeviceByIdentifier(selectedIdentifier));
            expect(controller.getSnapshot().status == coolsynth::standalone::MidiInputStatus::connected);

            controller.injectMidiMessageForTesting(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
            expect(controller.getLastMidiEventSnapshot().hasEvent);
            expect(controller.getPendingControllerEventCountForTesting() > 0);

            availableDevices.clear();
            controller.refreshDeviceListForTesting();

            const auto& snapshot = controller.getSnapshot();
            expect(snapshot.status == coolsynth::standalone::MidiInputStatus::disconnected);
            expect(snapshot.statusMessage.contains("Disconnected"));
            expectEquals(disconnectCallbackCount, 1);
            expect(!controller.getLastMidiEventSnapshot().hasEvent);
            expectEquals(controller.getPendingControllerEventCountForTesting(), 0);
        }
    }
};

class DspRegressionTests final : public juce::UnitTest
{
public:
    DspRegressionTests() : juce::UnitTest("DspRegression", "CoolSynth") {}

    void runTest() override
    {
        beginTest("voice_filter_stays_finite_at_supported_sample_rates_under_extreme_resonance");
        {
            for (const double sampleRate : { 44100.0, 48000.0 })
            {
                coolsynth::synth::SynthVoice voice;
                juce::dsp::ProcessSpec spec { sampleRate, 256, 1 };
                voice.prepare(spec);
                voice.setWaveform(coolsynth::parameters::WaveformChoice::saw);
                voice.setNextEnvelopeParameters(makeStressEnvelope());
                voice.setNextFilterParameters({ 20000.0f, 1.0f });

                voice.startNote(84, 1.0f);

                juce::AudioBuffer<float> buffer(1, 256);
                for (int block = 0; block < 24; ++block)
                {
                    buffer.clear();
                    voice.renderNextBlock(buffer, 0, buffer.getNumSamples());
                    expectBufferFiniteAndBounded(*this, buffer, 50.0f);
                }
            }
        }

        beginTest("voice_filter_cutoff_and_resonance_jumps_stay_finite_and_bounded");
        {
            coolsynth::synth::SynthVoice voice;
            juce::dsp::ProcessSpec spec { 48000.0, 256, 1 };
            voice.prepare(spec);
            voice.setWaveform(coolsynth::parameters::WaveformChoice::saw);
            voice.setNextEnvelopeParameters(makeStressEnvelope());
            voice.setNextFilterParameters({ 20000.0f, 0.0f });

            voice.startNote(72, 1.0f);

            juce::AudioBuffer<float> buffer(1, 256);
            for (int block = 0; block < 32; ++block)
            {
                const bool highState = (block % 2) != 0;
                voice.setNextFilterParameters({ highState ? 18000.0f : 120.0f, highState ? 1.0f : 0.0f });

                buffer.clear();
                voice.renderNextBlock(buffer, 0, buffer.getNumSamples());
                expectBufferFiniteAndBounded(*this, buffer, 50.0f);
            }
        }

        beginTest("global_delay_time_jumps_remain_finite_and_bounded");
        {
            coolsynth::synth::SynthEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParameters parameters;
            parameters.waveform = coolsynth::parameters::WaveformChoice::saw;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 4000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.delay.feedback = 0.85f;
            parameters.delay.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 32; ++block)
            {
                juce::MidiBuffer midi;
                if (block == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
                if (block == 20)
                    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

                parameters.delay.timeMs = (block % 2) != 0 ? 1000.0f : 1.0f;

                buffer.clear();
                engine.render(buffer, midi, parameters);
                expectBufferFiniteAndBounded(*this, buffer, 100.0f);
            }
        }

        beginTest("master_gain_jumps_remain_finite_and_bounded");
        {
            coolsynth::synth::SynthEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParameters parameters;
            parameters.waveform = coolsynth::parameters::WaveformChoice::saw;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 6000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.delay.mix = 0.0f;
            parameters.masterGainLinear = 1.0f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 24; ++block)
            {
                juce::MidiBuffer midi;
                if (block == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8) 100), 0);
                if (block == 16)
                    midi.addEvent(juce::MidiMessage::noteOff(1, 67), 0);

                parameters.masterGainLinear = (block % 2) != 0 ? 1.0f : 0.001f;

                buffer.clear();
                engine.render(buffer, midi, parameters);
                expectBufferFiniteAndBounded(*this, buffer, 20.0f);
            }
        }
    }
};

class V2AllocatorTests final : public juce::UnitTest
{
public:
    V2AllocatorTests() : juce::UnitTest("V2Allocator", "CoolSynth") {}

    void runTest() override
    {
        beginTest("sample_offset_note_events_render_only_after_their_offset");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 128, 2);

            auto parameters = makeBasicParameters();

            std::array<coolsynth::synth::EngineMidiEvent, 1> events {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 64, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 128);
            buffer.clear();
            engine.render(buffer, events, parameters);

            float firstHalfMax = 0.0f;
            float secondHalfMax = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < 64; ++sample)
                    firstHalfMax = juce::jmax(firstHalfMax, std::abs(buffer.getSample(channel, sample)));

                for (int sample = 64; sample < buffer.getNumSamples(); ++sample)
                    secondHalfMax = juce::jmax(secondHalfMax, std::abs(buffer.getSample(channel, sample)));
            }

            expectWithinAbsoluteError(firstHalfMax, 0.0f, 1.0e-6f);
            expect(secondHalfMax > 1.0e-4f);
        }

        beginTest("sustain_holds_released_notes_until_pedal_up");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::EngineMidiEvent, 2> sustainAndRelease {{
                { coolsynth::synth::EngineMidiEventType::sustainPedal, 0, 0, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
            }};
            buffer.clear();
            engine.render(buffer, sustainAndRelease, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);
            bool sustainedVoiceFound = false;
            for (const auto& state : debugStates)
                sustainedVoiceFound = sustainedVoiceFound || (state.active && state.noteNumber == 60 && state.sustained);

            expect(sustainedVoiceFound);

            std::array<coolsynth::synth::EngineMidiEvent, 1> sustainUp {{
                { coolsynth::synth::EngineMidiEventType::sustainPedal, 0, 0, 0.0f }
            }};
            for (int block = 0; block < 12; ++block)
            {
                buffer.clear();
                engine.render(buffer, block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(sustainUp)
                                                 : std::span<const coolsynth::synth::EngineMidiEvent>(),
                              parameters);
            }

            expectEquals(engine.getActiveVoiceCountForTesting(), 0);
        }

        beginTest("voice_stealing_prefers_released_voices_before_held_voices");
        {
            coolsynth::synth::SynthEngineV2 engine(2);
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 2> firstChord {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 62, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, firstChord, parameters);

            std::array<coolsynth::synth::EngineMidiEvent, 1> releaseOldest {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
            }};
            buffer.clear();
            engine.render(buffer, releaseOldest, parameters);

            std::array<coolsynth::synth::EngineMidiEvent, 1> stealReleased {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, stealReleased, parameters);

            std::array<coolsynth::synth::VoiceDebugState, 2> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);

            bool sawHeld62 = false;
            bool sawNew64 = false;
            bool sawStolen60 = false;
            for (const auto& state : debugStates)
            {
                sawHeld62 = sawHeld62 || (state.active && state.noteNumber == 62 && state.keyDown);
                sawNew64 = sawNew64 || (state.active && state.noteNumber == 64 && state.keyDown);
                sawStolen60 = sawStolen60 || (state.active && state.noteNumber == 60);
            }

            expect(sawHeld62);
            expect(sawNew64);
            expect(!sawStolen60);
        }

        beginTest("voice_stealing_falls_back_to_oldest_held_voice_when_none_are_released");
        {
            coolsynth::synth::SynthEngineV2 engine(2);
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 62, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, notes, parameters);

            std::array<coolsynth::synth::VoiceDebugState, 2> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);

            bool saw62 = false;
            bool saw64 = false;
            bool saw60 = false;
            for (const auto& state : debugStates)
            {
                saw60 = saw60 || (state.active && state.noteNumber == 60);
                saw62 = saw62 || (state.active && state.noteNumber == 62);
                saw64 = saw64 || (state.active && state.noteNumber == 64);
            }

            expect(!saw60);
            expect(saw62);
            expect(saw64);
        }

        beginTest("panic_clears_all_active_voices_immediately");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expect(engine.getActiveVoiceCountForTesting() > 0);

            engine.panic();
            buffer.clear();
            engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 0);

            float maxAbs = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    maxAbs = juce::jmax(maxAbs, std::abs(buffer.getSample(channel, sample)));

            expectWithinAbsoluteError(maxAbs, 0.0f, 1.0e-6f);
        }

        beginTest("all_notes_off_releases_active_and_sustained_voices");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::EngineMidiEvent, 2> sustainAndRelease {{
                { coolsynth::synth::EngineMidiEventType::sustainPedal, 0, 0, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
            }};
            buffer.clear();
            engine.render(buffer, sustainAndRelease, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::EngineMidiEvent, 1> allNotesOff {{
                { coolsynth::synth::EngineMidiEventType::allNotesOff, 0, 0, 0.0f }
            }};
            for (int block = 0; block < 12; ++block)
            {
                buffer.clear();
                engine.render(buffer,
                              block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(allNotesOff)
                                         : std::span<const coolsynth::synth::EngineMidiEvent>(),
                              parameters);
            }

            expectEquals(engine.getActiveVoiceCountForTesting(), 0);
        }

        beginTest("reset_controllers_releases_sustained_voices");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            juce::AudioBuffer<float> buffer(2, 64);

            std::array<coolsynth::synth::EngineMidiEvent, 3> noteWithSustainAndBend {{
                { coolsynth::synth::EngineMidiEventType::pitchBend, 0, 0, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::sustainPedal, 0, 0, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, noteWithSustainAndBend, parameters);

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOff {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
            }};
            buffer.clear();
            engine.render(buffer, noteOff, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::EngineMidiEvent, 1> resetControllers {{
                { coolsynth::synth::EngineMidiEventType::resetControllers, 0, 0, 0.0f }
            }};
            for (int block = 0; block < 12; ++block)
            {
                buffer.clear();
                engine.render(buffer,
                              block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(resetControllers)
                                         : std::span<const coolsynth::synth::EngineMidiEvent>(),
                              parameters);
            }

            expectEquals(engine.getActiveVoiceCountForTesting(), 0);

            std::array<coolsynth::synth::EngineMidiEvent, 1> newNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, newNote, parameters);

            float maxAbs = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    maxAbs = juce::jmax(maxAbs, std::abs(buffer.getSample(channel, sample)));
            }

            expect(maxAbs > 1.0e-4f);
        }

        beginTest("pulse_width_extremes_remain_audible_and_bounded");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.mixer.noiseLevel = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            juce::AudioBuffer<float> buffer(2, 256);

            for (const float pulseWidth : { 0.05f, 0.95f })
            {
                parameters.oscA.pulseWidth = pulseWidth;
                engine.panic();
                buffer.clear();
                engine.render(buffer, noteOn, parameters);
                expectBufferFiniteAndBounded(*this, buffer, 10.0f);
                expect(computePeakAbs(buffer, 16, buffer.getNumSamples()) > 1.0e-3f);
            }
        }

        beginTest("oscillator_sync_changes_rendered_output_without_destabilizing_the_voice");
        {
            auto baseParameters = makeBasicParameters();
            baseParameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            baseParameters.oscA.level = 0.9f;
            baseParameters.oscA.octaveIndex = 3;
            baseParameters.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            baseParameters.oscB.level = 0.8f;
            baseParameters.oscB.octaveIndex = 2;
            baseParameters.mixer.noiseLevel = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 57, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 unsyncedEngine;
            coolsynth::synth::SynthEngineV2 syncedEngine;
            unsyncedEngine.prepare(48000.0, 256, 2);
            syncedEngine.prepare(48000.0, 256, 2);

            juce::AudioBuffer<float> unsyncedBuffer(2, 256);
            juce::AudioBuffer<float> syncedBuffer(2, 256);

            auto unsyncedParameters = baseParameters;
            auto syncedParameters = baseParameters;
            syncedParameters.oscA.syncEnabled = true;

            unsyncedBuffer.clear();
            unsyncedEngine.render(unsyncedBuffer, noteOn, unsyncedParameters);
            syncedBuffer.clear();
            syncedEngine.render(syncedBuffer, noteOn, syncedParameters);

            expectBufferFiniteAndBounded(*this, unsyncedBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, syncedBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(unsyncedBuffer, syncedBuffer) > 0.5f);
        }

        beginTest("oscillator_detune_changes_the_dual_oscillator_render");
        {
            auto tunedParameters = makeBasicParameters();
            tunedParameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            tunedParameters.oscA.level = 0.85f;
            tunedParameters.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            tunedParameters.oscB.level = 0.85f;
            tunedParameters.oscB.fineCents = 0.0f;

            auto detunedParameters = tunedParameters;
            detunedParameters.oscB.fineCents = 18.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 tunedEngine;
            coolsynth::synth::SynthEngineV2 detunedEngine;
            tunedEngine.prepare(48000.0, 256, 2);
            detunedEngine.prepare(48000.0, 256, 2);

            juce::AudioBuffer<float> tunedBuffer(2, 256);
            juce::AudioBuffer<float> detunedBuffer(2, 256);
            tunedBuffer.clear();
            tunedEngine.render(tunedBuffer, noteOn, tunedParameters);
            detunedBuffer.clear();
            detunedEngine.render(detunedBuffer, noteOn, detunedParameters);

            expectBufferFiniteAndBounded(*this, tunedBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, detunedBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(tunedBuffer, detunedBuffer) > 0.5f);
        }

        beginTest("dense_simultaneous_note_ons_do_not_produce_an_oversized_start_click");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
            parameters.oscA.level = 1.0f;
            parameters.oscA.pulseWidth = 0.12f;
            parameters.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscB.level = 1.0f;
            parameters.oscB.fineCents = 12.0f;
            parameters.mixer.noiseLevel = 0.35f;

            std::array<coolsynth::synth::EngineMidiEvent, coolsynth::synth::defaultVoiceCount> noteOns {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 48, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 52, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 55, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 59, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 71, 1.0f },
            }};

            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOns, parameters);

            expectBufferFiniteAndBounded(*this, buffer, 10.0f);

            const auto earlyPeak = computePeakAbs(buffer, 0, 8);
            const auto laterPeak = computePeakAbs(buffer, 16, buffer.getNumSamples());
            expect(earlyPeak < 0.35f);
            expect(laterPeak > 1.0e-2f);
        }

        beginTest("full_mixer_overload_path_stays_finite_and_bounded");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscA.syncEnabled = true;
            parameters.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
            parameters.oscB.level = 1.0f;
            parameters.oscB.pulseWidth = 0.18f;
            parameters.oscB.fineCents = 9.0f;
            parameters.mixer.noiseLevel = 1.0f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 20; ++block)
            {
                juce::HeapBlock<coolsynth::synth::EngineMidiEvent> eventStorage(1);
                std::span<const coolsynth::synth::EngineMidiEvent> events;

                if (block == 0)
                {
                    eventStorage[0] = { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f };
                    events = std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.get(), 1);
                }
                else if (block == 12)
                {
                    eventStorage[0] = { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f };
                    events = std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.get(), 1);
                }

                buffer.clear();
                engine.render(buffer, events, parameters);
                expectBufferFiniteAndBounded(*this, buffer, 10.0f);
            }
        }
    }

private:
    static coolsynth::synth::BlockRenderParametersV2 makeBasicParameters() noexcept
    {
        coolsynth::synth::BlockRenderParametersV2 parameters;
        parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
        parameters.oscA.level = 1.0f;
        parameters.oscB.level = 0.0f;
        parameters.ampEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

static StandaloneMidiInputTests standaloneMidiInputTests;
static DspRegressionTests dspRegressionTests;
static V2AllocatorTests v2AllocatorTests;
