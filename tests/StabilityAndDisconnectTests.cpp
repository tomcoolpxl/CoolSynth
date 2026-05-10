#include <cmath>

#include <juce_core/juce_core.h>

#include "standalone/StandaloneMidiInput.h"
#include "standalone/SettingsStore.h"
#include "synth/SynthEngine.h"
#include "synth/SynthSound.h"
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

                coolsynth::synth::SynthSound sound;
                voice.startNote(84, 1.0f, &sound, 0);

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

            coolsynth::synth::SynthSound sound;
            voice.startNote(72, 1.0f, &sound, 0);

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

static StandaloneMidiInputTests standaloneMidiInputTests;
static DspRegressionTests dspRegressionTests;
