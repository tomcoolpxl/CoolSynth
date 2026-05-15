#include <cmath>
#include <limits>

#include <juce_core/juce_core.h>

#include "plugin/ProcessorScopeFifo.h"
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

    float computeMean(const juce::AudioBuffer<float>& buffer)
    {
        double sum = 0.0;
        int count = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                sum += buffer.getSample(channel, sample);
                ++count;
            }
        }

        if (count == 0)
            return 0.0f;

        return static_cast<float>(sum / static_cast<double>(count));
    }

    float computeRms(const juce::AudioBuffer<float>& buffer,
                     int startSample = 0,
                     int endSample = std::numeric_limits<int>::max())
    {
        double sumSquares = 0.0;
        int count = 0;

        const auto clampedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
        const auto clampedEnd = juce::jlimit(clampedStart, buffer.getNumSamples(), endSample);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int sample = clampedStart; sample < clampedEnd; ++sample)
            {
                const double v = buffer.getSample(channel, sample);
                sumSquares += v * v;
                ++count;
            }
        }

        if (count == 0)
            return 0.0f;

        return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count)));
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
                voice.setNextFilterParameters({ highState ? 18000.0f : 120.0f, highState ? 1.0f : 0.0f, 0.0f, coolsynth::parameters::FilterKeyTrackingMode::off });

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

        beginTest("v2_fx_rack_zero_mix_preserves_the_dry_path");
        {
            coolsynth::synth::SynthEngineV2 dryEngine;
            coolsynth::synth::SynthEngineV2 bypassedFxEngine;
            dryEngine.prepare(48000.0, 256, 2);
            bypassedFxEngine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParametersV2 dryParameters;
            dryParameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            dryParameters.oscA.level = 1.0f;
            dryParameters.oscB.level = 0.0f;
            dryParameters.ampEnvelope = makeStressEnvelope();
            dryParameters.filterEnvelope = makeStressEnvelope();
            dryParameters.filter.cutoffHz = 8000.0f;
            dryParameters.filter.resonanceNormalized = 0.1f;
            dryParameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            dryParameters.masterGainLinear = 0.5f;

            auto fxParameters = dryParameters;
            fxParameters.drive.enabled = true;
            fxParameters.drive.amount = 1.0f;
            fxParameters.drive.mix = 0.0f;
            fxParameters.chorus.enabled = true;
            fxParameters.chorus.rateHz = 3.0f;
            fxParameters.chorus.depth = 1.0f;
            fxParameters.chorus.mix = 0.0f;
            fxParameters.delay.enabled = true;
            fxParameters.delay.timeMs = 500.0f;
            fxParameters.delay.feedback = 0.85f;
            fxParameters.delay.mix = 0.0f;
            fxParameters.reverb.enabled = true;
            fxParameters.reverb.size = 1.0f;
            fxParameters.reverb.damping = 0.0f;
            fxParameters.reverb.mix = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> dryBuffer(2, 256);
            juce::AudioBuffer<float> fxBuffer(2, 256);
            dryBuffer.clear();
            fxBuffer.clear();
            dryEngine.render(dryBuffer, noteOn, dryParameters);
            bypassedFxEngine.render(fxBuffer, noteOn, fxParameters);

            expectWithinAbsoluteError(computeAbsoluteDifferenceSum(dryBuffer, fxBuffer), 0.0f, 1.0e-6f);
        }

        beginTest("v2_fx_rack_extreme_settings_remain_finite_and_bounded");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParametersV2 parameters;
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 4000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            parameters.drive.enabled = true;
            parameters.drive.amount = 1.0f;
            parameters.drive.mix = 1.0f;
            parameters.chorus.enabled = true;
            parameters.chorus.rateHz = 5.0f;
            parameters.chorus.depth = 1.0f;
            parameters.chorus.mix = 1.0f;
            parameters.delay.enabled = true;
            parameters.delay.timeMs = 1000.0f;
            parameters.delay.feedback = 1.0f;
            parameters.delay.mix = 1.0f;
            parameters.reverb.enabled = true;
            parameters.reverb.size = 1.0f;
            parameters.reverb.damping = 0.0f;
            parameters.reverb.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 32; ++block)
            {
                std::array<coolsynth::synth::EngineMidiEvent, 1> eventStorage {{
                    block == 0
                        ? coolsynth::synth::EngineMidiEvent { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
                        : coolsynth::synth::EngineMidiEvent { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
                }};

                const auto events = block == 0
                    ? std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.data(), 1)
                    : (block == 8
                        ? std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.data(), 1)
                        : std::span<const coolsynth::synth::EngineMidiEvent>());

                buffer.clear();
                engine.render(buffer, events, parameters);
                expectBufferFiniteAndBounded(*this, buffer, 100.0f);
            }
        }

        beginTest("disabling_time_based_v2_fx_clears_existing_tails");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParametersV2 parameters;
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 5000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            parameters.delay.enabled = true;
            parameters.delay.timeMs = 300.0f;
            parameters.delay.feedback = 0.6f;
            parameters.delay.mix = 1.0f;
            parameters.reverb.enabled = true;
            parameters.reverb.size = 1.0f;
            parameters.reverb.damping = 0.0f;
            parameters.reverb.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOff {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            buffer.clear();
            engine.render(buffer, noteOff, parameters);

            float tailPeak = 0.0f;
            for (int block = 0; block < 4; ++block)
            {
                buffer.clear();
                engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
                tailPeak = juce::jmax(tailPeak, computePeakAbs(buffer, 0, buffer.getNumSamples()));
            }

            expect(tailPeak > 1.0e-4f);

            parameters.delay.enabled = false;
            parameters.reverb.enabled = false;
            buffer.clear();
            engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);

            expect(computePeakAbs(buffer, 0, buffer.getNumSamples()) < 1.0e-4f);
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

                buffer.clear();
                for (int block = 0; block < 6; ++block)
                {
                    buffer.clear();
                    engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
                }

                expectWithinAbsoluteError(computeMean(buffer), 0.0f, 0.05f);
            }
        }

        beginTest("sine_wave_shape_renders_audibly_and_stays_bounded");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::sine;
            parameters.oscA.level = 0.9f;
            parameters.oscB.level = 0.0f;
            parameters.mixer.noiseLevel = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 48, 1.0f }
            }};
            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectBufferFiniteAndBounded(*this, buffer, 10.0f);
            expect(computePeakAbs(buffer, 16, buffer.getNumSamples()) > 1.0e-3f);
            expectWithinAbsoluteError(computeMean(buffer), 0.0f, 0.05f);
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
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

class V2PerformanceTests final : public juce::UnitTest
{
public:
    V2PerformanceTests() : juce::UnitTest("V2Performance", "CoolSynth") {}

    void runTest() override
    {
        beginTest("pitch_bend_range_changes_rendered_pitch_content");
        {
            auto narrowParameters = makeBasicParameters();
            narrowParameters.performance.pitchBendRangeSemitones = 1.0f;

            auto wideParameters = makeBasicParameters();
            wideParameters.performance.pitchBendRangeSemitones = 24.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 2> noteAndBend {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::pitchBend, 0, 0, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 narrowEngine;
            coolsynth::synth::SynthEngineV2 wideEngine;
            narrowEngine.prepare(48000.0, 256, 2);
            wideEngine.prepare(48000.0, 256, 2);

            juce::AudioBuffer<float> narrowBuffer(2, 256);
            juce::AudioBuffer<float> wideBuffer(2, 256);
            narrowBuffer.clear();
            wideBuffer.clear();

            narrowEngine.render(narrowBuffer, noteAndBend, narrowParameters);
            wideEngine.render(wideBuffer, noteAndBend, wideParameters);

            expect(computeAbsoluteDifferenceSum(narrowBuffer, wideBuffer) > 1.0f);
        }

        beginTest("mono_play_mode_keeps_only_one_active_voice");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            parameters.performance.playMode = coolsynth::parameters::PlayModeChoice::mono;
            parameters.performance.keyPriority = coolsynth::parameters::KeyPriorityChoice::last;

            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            engine.render(buffer, notes, parameters);

            expectEquals(engine.getActiveVoiceCountForTesting(), 1);

            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);

            bool foundLastNote = false;
            for (const auto& state : debugStates)
                foundLastNote = foundLastNote || (state.active && state.noteNumber == 67);

            expect(foundLastNote);
        }

        beginTest("mono_key_priority_low_falls_back_to_lowest_held_note");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            parameters.performance.playMode = coolsynth::parameters::PlayModeChoice::mono;
            parameters.performance.keyPriority = coolsynth::parameters::KeyPriorityChoice::low;

            std::array<coolsynth::synth::EngineMidiEvent, 3> notesDown {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            engine.render(buffer, notesDown, parameters);

            std::array<coolsynth::synth::EngineMidiEvent, 1> releaseLatest {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 67, 0.0f }
            }};
            buffer.clear();
            engine.render(buffer, releaseLatest, parameters);

            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);

            bool foundLowestHeld = false;
            for (const auto& state : debugStates)
                foundLowestHeld = foundLowestHeld || (state.active && state.noteNumber == 60);

            expect(foundLowestHeld);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);
        }

        beginTest("unison_play_mode_stacks_all_voices_on_one_note");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            parameters.performance.playMode = coolsynth::parameters::PlayModeChoice::unison;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectEquals(engine.getActiveVoiceCountForTesting(),
                         static_cast<int>(coolsynth::synth::defaultVoiceCount));

            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> debugStates {};
            engine.copyVoiceStatesForTesting(debugStates);

            for (const auto& state : debugStates)
                expectEquals(state.noteNumber, 60);
        }

        beginTest("glide_produces_intermediate_pitch_content_in_mono_mode");
        {
            coolsynth::synth::SynthEngineV2 noGlideEngine;
            coolsynth::synth::SynthEngineV2 glideEngine;
            noGlideEngine.prepare(48000.0, 256, 2);
            glideEngine.prepare(48000.0, 256, 2);

            auto noGlide = makeBasicParameters();
            noGlide.performance.playMode = coolsynth::parameters::PlayModeChoice::mono;
            noGlide.performance.glideTimeSeconds = 0.0f;

            auto withGlide = noGlide;
            withGlide.performance.glideTimeSeconds = 0.25f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> firstNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 48, 1.0f }
            }};
            std::array<coolsynth::synth::EngineMidiEvent, 2> secondNote {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 48, 0.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 72, 1.0f }
            }};

            juce::AudioBuffer<float> noGlideBuffer(2, 256);
            juce::AudioBuffer<float> glideBuffer(2, 256);

            noGlideBuffer.clear();
            noGlideEngine.render(noGlideBuffer, firstNote, noGlide);
            glideBuffer.clear();
            glideEngine.render(glideBuffer, firstNote, withGlide);

            noGlideBuffer.clear();
            noGlideEngine.render(noGlideBuffer, secondNote, noGlide);
            glideBuffer.clear();
            glideEngine.render(glideBuffer, secondNote, withGlide);

            expectBufferFiniteAndBounded(*this, noGlideBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, glideBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(noGlideBuffer, glideBuffer) > 0.5f);
        }

        beginTest("vintage_drift_stays_within_bounded_cents_range");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 64, 2);

            auto parameters = makeBasicParameters();
            parameters.performance.playMode = coolsynth::parameters::PlayModeChoice::unison;
            parameters.performance.vintageAmount = 1.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expectBufferFiniteAndBounded(*this, buffer, 10.0f);
        }

        beginTest("mod_wheel_changes_rendered_lfo_modulation");
        {
            coolsynth::synth::SynthEngineV2 lowEngine;
            coolsynth::synth::SynthEngineV2 highEngine;
            lowEngine.prepare(48000.0, 1024, 2);
            highEngine.prepare(48000.0, 1024, 2);

            auto parameters = makeBasicParameters();
            parameters.lfo.rateHz = 8.0f;
            parameters.lfo.oscPitchDepth = 0.5f;
            parameters.lfo.modWheelDepth = 1.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 2> lowEvents {{
                { coolsynth::synth::EngineMidiEventType::modWheel, 0, 0, 0.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            std::array<coolsynth::synth::EngineMidiEvent, 2> highEvents {{
                { coolsynth::synth::EngineMidiEventType::modWheel, 0, 0, 1.0f },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> lowBuffer(2, 1024);
            juce::AudioBuffer<float> highBuffer(2, 1024);
            lowBuffer.clear();
            highBuffer.clear();
            lowEngine.render(lowBuffer, lowEvents, parameters);
            highEngine.render(highBuffer, highEvents, parameters);

            expectBufferFiniteAndBounded(*this, lowBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, highBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(lowBuffer, highBuffer) > 0.5f);
        }

        beginTest("lfo_sine_wave_shape_renders_audibly_and_stays_bounded");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 1024, 2);

            auto parameters = makeBasicParameters();
            parameters.lfo.waveShape = coolsynth::parameters::LfoWaveShape::sine;
            parameters.lfo.rateHz = 10.0f;
            parameters.lfo.oscPitchDepth = 1.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 1024);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expectBufferFiniteAndBounded(*this, buffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(buffer, juce::AudioBuffer<float>(2, 1024)) > 0.1f);
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
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

class V2VoiceSourcesTests final : public juce::UnitTest
{
public:
    V2VoiceSourcesTests() : juce::UnitTest("V2VoiceSources", "CoolSynth") {}

    void runTest() override
    {
        beginTest("noise_only_voice_renders_audible_output");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.level = 0.0f;
            parameters.oscB.level = 0.0f;
            parameters.mixer.noiseLevel = 1.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectBufferFiniteAndBounded(*this, buffer, 10.0f);
            expect(computeRms(buffer, 32, buffer.getNumSamples()) > 0.005f);
        }

        beginTest("muting_all_sources_renders_silence");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.oscA.level = 0.0f;
            parameters.oscB.level = 0.0f;
            parameters.mixer.noiseLevel = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectWithinAbsoluteError(computePeakAbs(buffer, 0, buffer.getNumSamples()), 0.0f, 1.0e-5f);
        }

        beginTest("oscillator_a_octave_shift_changes_rendered_output");
        {
            auto baseParameters = makeBasicParameters();
            baseParameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            baseParameters.oscA.level = 1.0f;

            auto lowParameters = baseParameters;
            lowParameters.oscA.octaveIndex = 0;

            auto highParameters = baseParameters;
            highParameters.oscA.octaveIndex = 4;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 lowEngine;
            coolsynth::synth::SynthEngineV2 highEngine;
            lowEngine.prepare(48000.0, 256, 2);
            highEngine.prepare(48000.0, 256, 2);

            juce::AudioBuffer<float> lowBuffer(2, 256);
            juce::AudioBuffer<float> highBuffer(2, 256);
            lowBuffer.clear();
            highBuffer.clear();
            lowEngine.render(lowBuffer, noteOn, lowParameters);
            highEngine.render(highBuffer, noteOn, highParameters);

            expectBufferFiniteAndBounded(*this, lowBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, highBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(lowBuffer, highBuffer) > 1.0f);
        }

        beginTest("oscillator_b_low_frequency_mode_changes_rendered_output");
        {
            auto audioRate = makeBasicParameters();
            audioRate.oscA.level = 0.0f;
            audioRate.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            audioRate.oscB.level = 1.0f;
            audioRate.oscB.lowFrequencyMode = false;

            auto lowFreq = audioRate;
            lowFreq.oscB.lowFrequencyMode = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 audioRateEngine;
            coolsynth::synth::SynthEngineV2 lowFreqEngine;
            audioRateEngine.prepare(48000.0, 1024, 2);
            lowFreqEngine.prepare(48000.0, 1024, 2);

            juce::AudioBuffer<float> audioRateBuffer(2, 1024);
            juce::AudioBuffer<float> lowFreqBuffer(2, 1024);
            audioRateBuffer.clear();
            lowFreqBuffer.clear();
            audioRateEngine.render(audioRateBuffer, noteOn, audioRate);
            lowFreqEngine.render(lowFreqBuffer, noteOn, lowFreq);

            expectBufferFiniteAndBounded(*this, audioRateBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, lowFreqBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(audioRateBuffer, lowFreqBuffer) > 5.0f);
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
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

class V2FilterAndEnvelopeTests final : public juce::UnitTest
{
public:
    V2FilterAndEnvelopeTests() : juce::UnitTest("V2FilterAndEnvelope", "CoolSynth") {}

    void runTest() override
    {
        beginTest("filter_cutoff_low_attenuates_more_than_high_for_a_high_note");
        {
            auto base = makeBasicParameters();
            base.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            base.oscA.level = 1.0f;
            base.filter.resonanceNormalized = 0.0f;
            base.filter.envelopeAmount = 0.0f;
            base.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            base.masterGainLinear = 1.0f;

            auto closed = base;
            closed.filter.cutoffHz = 120.0f;

            auto open = base;
            open.filter.cutoffHz = 12000.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 84, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 closedEngine;
            coolsynth::synth::SynthEngineV2 openEngine;
            closedEngine.prepare(48000.0, 1024, 2);
            openEngine.prepare(48000.0, 1024, 2);

            juce::AudioBuffer<float> closedBuffer(2, 1024);
            juce::AudioBuffer<float> openBuffer(2, 1024);
            closedBuffer.clear();
            openBuffer.clear();
            closedEngine.render(closedBuffer, noteOn, closed);
            openEngine.render(openBuffer, noteOn, open);

            expectBufferFiniteAndBounded(*this, closedBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, openBuffer, 10.0f);

            const float closedRms = computeRms(closedBuffer, 256, closedBuffer.getNumSamples());
            const float openRms = computeRms(openBuffer, 256, openBuffer.getNumSamples());
            expect(openRms > closedRms * 2.0f);
        }

        beginTest("filter_keyboard_tracking_full_lets_more_signal_through_high_notes");
        {
            auto base = makeBasicParameters();
            base.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            base.oscA.level = 1.0f;
            base.filter.cutoffHz = 300.0f;
            base.filter.resonanceNormalized = 0.0f;
            base.filter.envelopeAmount = 0.0f;
            base.masterGainLinear = 1.0f;

            auto trackingOff = base;
            trackingOff.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;

            auto trackingFull = base;
            trackingFull.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::full;

            std::array<coolsynth::synth::EngineMidiEvent, 1> highNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 84, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 offEngine;
            coolsynth::synth::SynthEngineV2 fullEngine;
            offEngine.prepare(48000.0, 1024, 2);
            fullEngine.prepare(48000.0, 1024, 2);

            juce::AudioBuffer<float> offBuffer(2, 1024);
            juce::AudioBuffer<float> fullBuffer(2, 1024);
            offBuffer.clear();
            fullBuffer.clear();
            offEngine.render(offBuffer, highNote, trackingOff);
            fullEngine.render(fullBuffer, highNote, trackingFull);

            expectBufferFiniteAndBounded(*this, offBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, fullBuffer, 10.0f);

            const float offRms = computeRms(offBuffer, 256, offBuffer.getNumSamples());
            const float fullRms = computeRms(fullBuffer, 256, fullBuffer.getNumSamples());
            expect(fullRms > offRms * 1.5f);
        }

        beginTest("filter_envelope_amount_creates_audible_sweep");
        {
            coolsynth::synth::EnvelopeParameters sweepEnvelope;
            sweepEnvelope.attackSeconds = 0.05f;
            sweepEnvelope.decaySeconds = 0.4f;
            sweepEnvelope.sustainLevel = 0.0f;
            sweepEnvelope.releaseSeconds = 0.1f;

            auto base = makeBasicParameters();
            base.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            base.oscA.level = 1.0f;
            base.filter.cutoffHz = 200.0f;
            base.filter.resonanceNormalized = 0.3f;
            base.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            base.filterEnvelope = sweepEnvelope;
            base.masterGainLinear = 1.0f;

            auto withSweep = base;
            withSweep.filter.envelopeAmount = 1.0f;

            auto withoutSweep = base;
            withoutSweep.filter.envelopeAmount = 0.0f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            coolsynth::synth::SynthEngineV2 sweepEngine;
            coolsynth::synth::SynthEngineV2 flatEngine;
            sweepEngine.prepare(48000.0, 2048, 2);
            flatEngine.prepare(48000.0, 2048, 2);

            juce::AudioBuffer<float> sweepBuffer(2, 2048);
            juce::AudioBuffer<float> flatBuffer(2, 2048);
            sweepBuffer.clear();
            flatBuffer.clear();
            sweepEngine.render(sweepBuffer, noteOn, withSweep);
            flatEngine.render(flatBuffer, noteOn, withoutSweep);

            expectBufferFiniteAndBounded(*this, sweepBuffer, 10.0f);
            expectBufferFiniteAndBounded(*this, flatBuffer, 10.0f);
            expect(computeAbsoluteDifferenceSum(sweepBuffer, flatBuffer) > 5.0f);
        }

        beginTest("stolen_voice_amp_envelope_restarts_from_attack");
        {
            coolsynth::synth::SynthEngineV2 engine(2);
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::EnvelopeParameters slowAttack;
            slowAttack.attackSeconds = 0.06f;
            slowAttack.decaySeconds = 0.3f;
            slowAttack.sustainLevel = 1.0f;
            slowAttack.releaseSeconds = 0.1f;

            auto parameters = makeBasicParameters();
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.ampEnvelope = slowAttack;
            parameters.filter.cutoffHz = 10000.0f;
            parameters.filter.envelopeAmount = 0.0f;
            parameters.masterGainLinear = 1.0f;

            juce::AudioBuffer<float> buffer(2, 256);

            std::array<coolsynth::synth::EngineMidiEvent, 1> firstNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, firstNote, parameters);

            for (int block = 0; block < 30; ++block)
            {
                buffer.clear();
                engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
            }

            const float sustainPeak = computePeakAbs(buffer, 0, buffer.getNumSamples());
            expect(sustainPeak > 0.05f);

            std::array<coolsynth::synth::EngineMidiEvent, 1> stealingNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, stealingNote, parameters);

            std::array<coolsynth::synth::EngineMidiEvent, 1> thirdNote {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f }
            }};
            buffer.clear();
            engine.render(buffer, thirdNote, parameters);

            const float earlyStolenPeak = computePeakAbs(buffer, 0, 16);
            expect(earlyStolenPeak < sustainPeak * 0.5f);
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
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

class V2PolyphonyHeadroomTests final : public juce::UnitTest
{
public:
    V2PolyphonyHeadroomTests() : juce::UnitTest("V2PolyphonyHeadroom", "CoolSynth") {}

    void runTest() override
    {
        beginTest("eight_voice_full_load_with_unity_master_stays_within_clip_margin");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::synth::BlockRenderParametersV2 parameters;
            parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.waveShape = coolsynth::parameters::OscillatorWaveShape::pulse;
            parameters.oscB.level = 1.0f;
            parameters.oscB.pulseWidth = 0.18f;
            parameters.oscB.fineCents = 12.0f;
            parameters.mixer.noiseLevel = 0.5f;
            parameters.ampEnvelope.attackSeconds = 0.001f;
            parameters.ampEnvelope.decaySeconds = 0.01f;
            parameters.ampEnvelope.sustainLevel = 1.0f;
            parameters.ampEnvelope.releaseSeconds = 0.05f;
            parameters.filterEnvelope = parameters.ampEnvelope;
            parameters.filter.cutoffHz = 8000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.filter.envelopeAmount = 0.0f;
            parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
            parameters.delay.enabled = false;
            parameters.masterGainLinear = 1.0f;

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
            float maxPeakAfterMaster = 0.0f;
            float maxEarlyPeakAfterMaster = 0.0f;

            for (int block = 0; block < 16; ++block)
            {
                buffer.clear();
                engine.render(buffer,
                              block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(noteOns)
                                         : std::span<const coolsynth::synth::EngineMidiEvent>(),
                              parameters);
                expectBufferFiniteAndBounded(*this, buffer, 10.0f);
                maxPeakAfterMaster = juce::jmax(maxPeakAfterMaster,
                                                computePeakAbs(buffer, 0, buffer.getNumSamples()));
                if (block == 0)
                    maxEarlyPeakAfterMaster = computePeakAbs(buffer, 0, 8);
            }

            expect(maxPeakAfterMaster > 0.5f);
            expect(maxPeakAfterMaster < 3.5f);
            expect(maxEarlyPeakAfterMaster < 0.5f);
        }
    }
};

class V2ArpeggiatorTests final : public juce::UnitTest
{
public:
    V2ArpeggiatorTests() : juce::UnitTest("V2Arpeggiator", "CoolSynth") {}

    static coolsynth::synth::EnvelopeParameters makeArpFriendlyEnvelope() noexcept
    {
        coolsynth::synth::EnvelopeParameters parameters;
        parameters.attackSeconds = 0.001f;
        parameters.decaySeconds = 0.01f;
        parameters.sustainLevel = 1.0f;
        parameters.releaseSeconds = 2.0f; // long enough that voices survive a step
        return parameters;
    }

    void runTest() override
    {
        beginTest("arp_disabled_passes_through_to_allocator_unchanged");
        {
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, 256, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = false;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectEquals(engine.getActiveVoiceCountForTesting(), 1);
            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
        }

        beginTest("pattern_up_walks_held_notes_in_ascending_order_with_octave_range_one");
        {
            constexpr int stepLength = 6000; // 48k / 120bpm / 8 = 1/16th
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = false;

            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false }
            }};

            const std::array<int, 6> expectedNotes { 60, 64, 67, 60, 64, 67 };

            juce::AudioBuffer<float> buffer(2, stepLength);

            buffer.clear();
            engine.render(buffer, notes, parameters);

            expectEquals(engine.getLastPlayedNoteForTesting(), expectedNotes[0]);

            for (int step = 1; step < static_cast<int>(expectedNotes.size()); ++step)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
                expectEquals(engine.getLastPlayedNoteForTesting(),
                             expectedNotes[static_cast<size_t>(step)]);
            }
        }

        beginTest("pattern_down_walks_held_notes_in_descending_order");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::down;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f, false }
            }};

            const std::array<int, 4> expectedNotes { 67, 64, 60, 67 };

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, notes, parameters);
            expectEquals(engine.getLastPlayedNoteForTesting(), expectedNotes[0]);
            for (int step = 1; step < static_cast<int>(expectedNotes.size()); ++step)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
                expectEquals(engine.getLastPlayedNoteForTesting(),
                             expectedNotes[static_cast<size_t>(step)]);
            }
        }

        beginTest("pattern_up_down_reflects_without_repeating_endpoints");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::upDown;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f, false }
            }};

            // total=3, reflected length = 2*3-2 = 4. Pattern: 60,64,67,64, 60,64,67,64, ...
            const std::array<int, 6> expectedNotes { 60, 64, 67, 64, 60, 64 };

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, notes, parameters);

            expectEquals(engine.getLastPlayedNoteForTesting(), expectedNotes[0]);
            for (int step = 1; step < static_cast<int>(expectedNotes.size()); ++step)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
                expectEquals(engine.getLastPlayedNoteForTesting(),
                             expectedNotes[static_cast<size_t>(step)]);
            }
        }

        beginTest("pattern_as_played_follows_held_note_insertion_order");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::asPlayed;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;

            // Press in this order: 67, 60, 64
            std::array<coolsynth::synth::EngineMidiEvent, 3> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false }
            }};

            const std::array<int, 6> expectedNotes { 67, 60, 64, 67, 60, 64 };

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, notes, parameters);

            expectEquals(engine.getLastPlayedNoteForTesting(), expectedNotes[0]);
            for (int step = 1; step < static_cast<int>(expectedNotes.size()); ++step)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
                expectEquals(engine.getLastPlayedNoteForTesting(),
                             expectedNotes[static_cast<size_t>(step)]);
            }
        }

        beginTest("octave_range_two_alternates_base_and_plus_twelve");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 2;
            parameters.arp.gateLength = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> notes {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            const std::array<int, 4> expected { 60, 72, 60, 72 };

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, notes, parameters);

            expectEquals(engine.getLastPlayedNoteForTesting(), expected[0]);
            for (int step = 1; step < static_cast<int>(expected.size()); ++step)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
                expectEquals(engine.getLastPlayedNoteForTesting(),
                             expected[static_cast<size_t>(step)]);
            }
        }

        beginTest("latch_keeps_pattern_alive_after_all_keys_released");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 4> pressAndRelease {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 64, 0.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, pressAndRelease, parameters);

            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
            expect(engine.getArpLatchedNoteCountForTesting() >= 2);

            // After release, pattern continues from latched [60, 64].
            // Next render with empty event stream advances by stepLength again.
            buffer.clear();
            engine.render(buffer, {}, parameters);
            const int secondStep = engine.getLastPlayedNoteForTesting();
            buffer.clear();
            engine.render(buffer, {}, parameters);
            const int thirdStep = engine.getLastPlayedNoteForTesting();
            expect(secondStep == 60 || secondStep == 64);
            expect(thirdStep == 60 || thirdStep == 64);
            expect(secondStep != thirdStep);
        }

        beginTest("latch_reset_on_first_note_after_clear");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = true;

            // Phase A: press + release 60.
            std::array<coolsynth::synth::EngineMidiEvent, 2> pressRelease {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, pressRelease, parameters);
            expect(engine.getArpLatchedNoteCountForTesting() >= 1);
            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);

            // Phase B: press 72 — latched set should clear, only 72 remains.
            std::array<coolsynth::synth::EngineMidiEvent, 1> pressNew {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 72, 1.0f, false }
            }};
            buffer.clear();
            engine.render(buffer, pressNew, parameters);
            expectEquals(engine.getArpLatchedNoteCountForTesting(), 1);
        }

        beginTest("gate_length_short_releases_within_step_window");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.1f; // short gate, ~600 samples
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            // After one full step's render, gate-off fired well within the block
            // so no voice should still be key-down.
            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            bool anyKeyDown = false;
            for (const auto& s : states)
                anyKeyDown = anyKeyDown || (s.active && s.keyDown);
            expect(! anyKeyDown);
        }

        beginTest("gate_length_long_carries_release_across_block");
        {
            constexpr int blockSamples = 3000; // half a step
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, blockSamples, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.95f; // clamped max → 5700 samples gate
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, blockSamples);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            // After half a step, gate is still on — ringing tracker holds the pending off.
            expect(engine.getArpRingingNoteCountForTesting() >= 1);
        }

        beginTest("internal_rate_falls_back_when_host_tempo_missing");
        {
            constexpr int blockSamples = 24000; // exactly one 1/4 step at 120 BPM 48k
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, blockSamples, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::quarter;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, blockSamples);
            buffer.clear();
            // No transport info → internal-rate fallback.
            engine.render(buffer, noteOn, parameters);

            expectEquals(engine.getLastPlayedNoteForTesting(), 60);
        }

        beginTest("host_sync_aligns_first_step_to_ppq_grid");
        {
            constexpr int blockSamples = 24000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, blockSamples, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 60.0f; // ignored when host sync wins
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::quarter;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = true;

            coolsynth::synth::EngineTransportInfo transport;
            transport.hostHasTempo = true;
            transport.hostHasPpq = true;
            transport.hostIsPlaying = true;
            transport.hostBpm = 120.0;
            // ppq = 0.5: next quarter boundary at ppq 1.0, which is 0.5 beats later.
            // At 120 BPM, samplesPerBeat = 48000 * 0.5 = 24000. So 0.5 beats = 12000 samples.
            transport.hostPpqAtBlockStart = 0.5;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, blockSamples);
            buffer.clear();
            engine.render(buffer, noteOn, parameters, transport);

            // Step should fire at sample offset ~12000 → first half of block silent, second half audible.
            float firstHalfPeak = computePeakAbs(buffer, 0, 8000);
            float secondHalfPeak = computePeakAbs(buffer, 14000, blockSamples);
            expect(firstHalfPeak < 1.0e-3f);
            expect(secondHalfPeak > 1.0e-3f);
        }

        beginTest("host_transport_stop_releases_currently_ringing_arp_note");
        {
            constexpr int blockSamples = 3000; // shorter than the long gate so the note is still held
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, blockSamples, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.95f; // ringing carries cross-block
            parameters.arp.latch = true;

            coolsynth::synth::EngineTransportInfo transport;
            transport.hostHasTempo = true;
            transport.hostHasPpq = true;
            transport.hostIsPlaying = true;
            transport.hostBpm = 120.0;
            transport.hostPpqAtBlockStart = 0.0;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, blockSamples);
            buffer.clear();
            engine.render(buffer, noteOn, parameters, transport);

            // A note is ringing with a deferred gate-off that transport stop should release.
            expect(engine.getArpRingingNoteCountForTesting() >= 1);

            // Now transport stops.
            transport.hostIsPlaying = false;
            // ppq increment unimportant for the test.
            buffer.clear();
            engine.render(buffer, {}, parameters, transport);

            int keyDownCount = 0;
            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            for (const auto& s : states)
                if (s.active && s.keyDown) ++keyDownCount;
            expectEquals(engine.getArpRingingNoteCountForTesting(), 0);
            expectEquals(keyDownCount, 0);
        }

        beginTest("arp_disable_mid_play_releases_ringing_note");
        {
            constexpr int blockSamples = 3000; // shorter than the long gate so the note is still held
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, blockSamples, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.95f;
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, blockSamples);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expect(engine.getArpRingingNoteCountForTesting() >= 1);

            // Disable arp; render again — ringing note should be released.
            parameters.arp.enabled = false;
            buffer.clear();
            engine.render(buffer, {}, parameters);

            int keyDownCount = 0;
            std::array<coolsynth::synth::VoiceDebugState, coolsynth::synth::defaultVoiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            for (const auto& s : states)
                if (s.active && s.keyDown) ++keyDownCount;
            expectEquals(engine.getArpRingingNoteCountForTesting(), 0);
            expectEquals(keyDownCount, 0);
        }

        beginTest("panic_clears_held_latched_and_ringing_arp_state");
        {
            constexpr int stepLength = 6000;
            coolsynth::synth::SynthEngineV2 engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.95f;
            parameters.arp.latch = true;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expect(engine.getArpHeldNoteCountForTesting() >= 1
                   || engine.getArpLatchedNoteCountForTesting() >= 1);

            engine.panic();

            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
            expectEquals(engine.getArpLatchedNoteCountForTesting(), 0);
            expectEquals(engine.getArpRingingNoteCountForTesting(), 0);
            expectEquals(engine.getActiveVoiceCountForTesting(), 0);
        }
    }

    static coolsynth::synth::BlockRenderParametersV2 makeBasicParameters() noexcept
    {
        coolsynth::synth::BlockRenderParametersV2 parameters;
        parameters.oscA.waveShape = coolsynth::parameters::OscillatorWaveShape::saw;
        parameters.oscA.level = 1.0f;
        parameters.oscB.level = 0.0f;
        parameters.ampEnvelope = makeArpFriendlyEnvelope();
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::parameters::FilterKeyTrackingMode::off;
        parameters.delay.enabled = false;
        parameters.masterGainLinear = 0.5f;
        return parameters;
    }
};

class V2ScopeFifoTests final : public juce::UnitTest
{
public:
    V2ScopeFifoTests() : juce::UnitTest("V2ScopeFifo", "CoolSynth") {}

    void runTest() override
    {
        beginTest("scope_fifo_basic_round_trip");
        {
            coolsynth::plugin::ProcessorScopeFifo fifo;

            // Write 1024 samples, read them back, verify values.
            constexpr int numSamples = 1024;
            std::vector<float> writeData(numSamples);
            for (int i = 0; i < numSamples; ++i)
                writeData[static_cast<size_t>(i)] = static_cast<float>(i) * 0.001f;

            fifo.write(writeData.data(), numSamples);

            std::vector<float> readData(numSamples, 0.0f);
            const int numRead = fifo.read(readData.data(), numSamples);

            expectEquals(numRead, numSamples);
            bool allMatch = true;
            for (int i = 0; i < numSamples && allMatch; ++i)
                allMatch = (readData[static_cast<size_t>(i)] == writeData[static_cast<size_t>(i)]);
            expect(allMatch, "round-trip data must match");
        }

        beginTest("scope_fifo_overflow_does_not_crash_and_drops_gracefully");
        {
            coolsynth::plugin::ProcessorScopeFifo fifo;

            // Fill to capacity.
            const int capacity = coolsynth::plugin::ProcessorScopeFifo::capacity;
            std::vector<float> fillData(static_cast<size_t>(capacity), 1.0f);
            fifo.write(fillData.data(), capacity);

            // Writing more to a full FIFO silently drops the excess — must not crash.
            std::vector<float> extra(100, 2.0f);
            fifo.write(extra.data(), 100);

            // Only the originally written data should be readable.
            std::vector<float> readData(static_cast<size_t>(capacity + 200), 0.0f);
            const int numRead = fifo.read(readData.data(), capacity + 200);

            // Must have read at most capacity samples and no more than what we wrote.
            expect(numRead <= capacity, "cannot read more than capacity");
            expect(numRead > 0, "some samples must be readable after write");
        }

        beginTest("scope_fifo_clear_resets_to_empty");
        {
            coolsynth::plugin::ProcessorScopeFifo fifo;

            std::vector<float> data(512, 0.5f);
            fifo.write(data.data(), 512);
            fifo.clear();

            std::vector<float> readData(512, 0.0f);
            const int numRead = fifo.read(readData.data(), 512);
            expectEquals(numRead, 0, "clear must drain all pending samples");
        }
    }
};

static StandaloneMidiInputTests standaloneMidiInputTests;
static DspRegressionTests dspRegressionTests;
static V2AllocatorTests v2AllocatorTests;
static V2PerformanceTests v2PerformanceTests;
static V2VoiceSourcesTests v2VoiceSourcesTests;
static V2FilterAndEnvelopeTests v2FilterAndEnvelopeTests;
static V2PolyphonyHeadroomTests v2PolyphonyHeadroomTests;
static V2ArpeggiatorTests v2ArpeggiatorTests;
static V2ScopeFifoTests v2ScopeFifoTests;
