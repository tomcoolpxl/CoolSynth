#include <cmath>
#include <limits>

#include <juce_core/juce_core.h>

#include "plugin/ProcessorScopeFifo.h"
#include "standalone/StandaloneMidiInput.h"
#include "standalone/SettingsStore.h"
#include "EngineTestAdapter.h"
#include "synth/HalfBand2x.h"

namespace
{
    coolsynth::testing::EnvelopeParameters makeStressEnvelope() noexcept
    {
        coolsynth::testing::EnvelopeParameters parameters;
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
        beginTest("global_delay_time_jumps_remain_finite_and_bounded");
        {
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 4000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.filter.envelopeAmount = 0.0f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.delay.enabled = true;
            parameters.delay.feedback = 0.85f;
            parameters.delay.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 32; ++block)
            {
                std::array<coolsynth::synth::EngineMidiEvent, 1> eventStorage {};
                int eventCount = 0;
                if (block == 0)
                    eventStorage[eventCount++] = { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 100.0f / 127.0f };
                else if (block == 20)
                    eventStorage[eventCount++] = { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f };

                parameters.delay.timeMs = (block % 2) != 0 ? 1000.0f : 1.0f;

                buffer.clear();
                engine.render(buffer,
                              std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.data(), static_cast<size_t>(eventCount)),
                              parameters);
                expectBufferFiniteAndBounded(*this, buffer, 100.0f);
            }
        }

        beginTest("master_gain_jumps_remain_finite_and_bounded");
        {
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 6000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.filter.envelopeAmount = 0.0f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.delay.enabled = false;
            parameters.masterGainLinear = 1.0f;

            juce::AudioBuffer<float> buffer(2, 256);
            for (int block = 0; block < 24; ++block)
            {
                std::array<coolsynth::synth::EngineMidiEvent, 1> eventStorage {};
                int eventCount = 0;
                if (block == 0)
                    eventStorage[eventCount++] = { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 100.0f / 127.0f };
                else if (block == 16)
                    eventStorage[eventCount++] = { coolsynth::synth::EngineMidiEventType::noteOff, 0, 67, 0.0f };

                parameters.masterGainLinear = (block % 2) != 0 ? 1.0f : 0.001f;

                buffer.clear();
                engine.render(buffer,
                              std::span<const coolsynth::synth::EngineMidiEvent>(eventStorage.data(), static_cast<size_t>(eventCount)),
                              parameters);
                expectBufferFiniteAndBounded(*this, buffer, 20.0f);
            }
        }

        beginTest("v2_fx_rack_zero_mix_preserves_the_dry_path");
        {
            coolsynth::testing::TestEngine dryEngine;
            coolsynth::testing::TestEngine bypassedFxEngine;
            dryEngine.prepare(48000.0, 256, 2);
            bypassedFxEngine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters dryParameters;
            dryParameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            dryParameters.oscA.level = 1.0f;
            dryParameters.oscB.level = 0.0f;
            dryParameters.ampEnvelope = makeStressEnvelope();
            dryParameters.filterEnvelope = makeStressEnvelope();
            dryParameters.filter.cutoffHz = 8000.0f;
            dryParameters.filter.resonanceNormalized = 0.1f;
            dryParameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            dryParameters.masterGainLinear = 0.5f;

            auto fxParameters = dryParameters;
            fxParameters.drive.enabled = true;
            fxParameters.drive.amount = 1.0f;
            fxParameters.drive.mix = 0.0f;
            fxParameters.phaser.enabled = true;
            fxParameters.phaser.rateHz = 3.0f;
            fxParameters.phaser.depth = 0.0f;
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
            fxParameters.compressor.enabled = true;
            fxParameters.compressor.amount = 1.0f;
            fxParameters.compressor.mix = 0.0f;

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
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 4000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.drive.enabled = true;
            parameters.drive.amount = 1.0f;
            parameters.drive.mix = 1.0f;
            parameters.phaser.enabled = true;
            parameters.phaser.rateHz = 8.0f;
            parameters.phaser.depth = 1.0f;
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
            parameters.compressor.enabled = true;
            parameters.compressor.amount = 1.0f;
            parameters.compressor.mix = 1.0f;
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

        beginTest("v2_fx_rack_processes_blocks_larger_than_the_prepared_size");
        {
            // prepareToPlay's block size is only a hint; a host may deliver a larger block.
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 4000.0f;
            parameters.filter.resonanceNormalized = 0.2f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.drive.enabled = true;
            parameters.drive.amount = 0.5f;
            parameters.drive.mix = 1.0f;
            parameters.phaser.enabled = true;
            parameters.phaser.depth = 1.0f;
            parameters.chorus.enabled = true;
            parameters.chorus.depth = 1.0f;
            parameters.chorus.mix = 1.0f;
            parameters.delay.enabled = true;
            parameters.delay.mix = 0.5f;
            parameters.reverb.enabled = true;
            parameters.reverb.mix = 1.0f;
            parameters.compressor.enabled = true;
            parameters.compressor.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            constexpr int oversizedBlock = 2048;
            juce::AudioBuffer<float> buffer(2, oversizedBlock);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);

            expectBufferFiniteAndBounded(*this, buffer, 100.0f);
            expect(buffer.getMagnitude(0, 0, oversizedBlock) > 0.0f, "oversized block must still render audio");
        }

        beginTest("disabling_time_based_v2_fx_clears_existing_tails");
        {
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 5000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
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

        beginTest("reverb_tail_rings_and_decays_after_note_off");
        {
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, 256, 2);

            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 5000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.delay.enabled = false;
            parameters.reverb.enabled = true;
            parameters.reverb.size = 0.7f;
            parameters.reverb.damping = 0.3f;
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

            float earlyRms = 0.0f;
            float lateRms = 0.0f;
            for (int block = 0; block < 60; ++block)
            {
                buffer.clear();
                engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
                expectBufferFiniteAndBounded(*this, buffer, 10.0f);
                const float rms = computeRms(buffer, 0, buffer.getNumSamples());
                if (block >= 4 && block < 8)
                    earlyRms = juce::jmax(earlyRms, rms);
                if (block >= 56)
                    lateRms = juce::jmax(lateRms, rms);
            }

            expect(earlyRms > 1.0e-3f, "plate must ring after the note is released");
            expect(lateRms > 1.0e-5f, "plate must still be audible 0.3 s into the tail");
            expect(lateRms < earlyRms, "plate tail must decay");
        }

        // At full wet the plate must sit in the same ballpark as the dry signal it replaces,
        // so a preset's reverb mix means the same thing it did with the previous reverb.
        beginTest("reverb_wet_level_is_comparable_to_dry");
        {
            coolsynth::testing::EnvelopeParameters held;
            held.attackSeconds = 0.01f;
            held.decaySeconds = 0.1f;
            held.sustainLevel = 1.0f;
            held.releaseSeconds = 0.2f;

            const auto measure = [&](float reverbMix, float size)
            {
                coolsynth::testing::TestEngine engine;
                engine.prepare(48000.0, 256, 2);

                coolsynth::testing::EngineParameters parameters;
                parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
                parameters.oscA.level = 1.0f;
                parameters.oscB.level = 0.0f;
                parameters.ampEnvelope = held;
                parameters.filterEnvelope = held;
                parameters.filter.cutoffHz = 3000.0f;
                parameters.filter.resonanceNormalized = 0.1f;
                parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
                parameters.reverb.enabled = reverbMix > 0.0f;
                parameters.reverb.size = size;
                parameters.reverb.damping = 0.5f;
                parameters.reverb.mix = reverbMix;
                parameters.masterGainLinear = 1.0f;

                std::array<coolsynth::synth::EngineMidiEvent, 3> chord {{
                    { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f },
                    { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f },
                    { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f }
                }};

                juce::AudioBuffer<float> buffer(2, 256);
                double sumSquares = 0.0;
                int counted = 0;
                for (int b = 0; b < 400; ++b)   // about 2.1 s
                {
                    buffer.clear();
                    engine.render(buffer,
                                  b == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(chord)
                                         : std::span<const coolsynth::synth::EngineMidiEvent>(),
                                  parameters);
                    if (b >= 280)
                    {
                        const float rms = computeRms(buffer, 0, buffer.getNumSamples());
                        sumSquares += static_cast<double>(rms) * static_cast<double>(rms);
                        ++counted;
                    }
                }
                return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(counted)));
            };

            const float dryRms = measure(0.0f, 0.5f);
            const float wetMidRms = measure(1.0f, 0.5f);
            const float wetLargeRms = measure(1.0f, 0.98f);

            logMessage("reverb rms: dry=" + juce::String(dryRms, 4) + " wet(size 0.5)=" + juce::String(wetMidRms, 4)
                       + " wet(size 0.98)=" + juce::String(wetLargeRms, 4));
            expect(wetMidRms > dryRms * 0.4f, "a medium plate at full wet must not be much quieter than dry");
            expect(wetMidRms < dryRms * 2.5f, "a medium plate at full wet must not be much louder than dry");
            expect(wetLargeRms > wetMidRms, "a larger plate must build up more level than a medium one");
        }

        beginTest("chorus_decorrelates_left_and_right");
        {
            coolsynth::testing::EngineParameters parameters;
            parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
            parameters.oscA.level = 1.0f;
            parameters.oscB.level = 0.0f;
            parameters.ampEnvelope = makeStressEnvelope();
            parameters.filterEnvelope = makeStressEnvelope();
            parameters.filter.cutoffHz = 5000.0f;
            parameters.filter.resonanceNormalized = 0.1f;
            parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
            parameters.performance.panSpread = 0.0f;
            parameters.chorus.rateHz = 1.0f;
            parameters.chorus.depth = 1.0f;
            parameters.chorus.mix = 1.0f;
            parameters.masterGainLinear = 0.5f;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
            }};

            const auto renderSideToMidRatio = [&](bool chorusEnabled)
            {
                auto withChorus = parameters;
                withChorus.chorus.enabled = chorusEnabled;

                coolsynth::testing::TestEngine engine;
                engine.prepare(48000.0, 256, 2);

                juce::AudioBuffer<float> buffer(2, 256);
                float side = 0.0f;
                float mid = 0.0f;
                for (int block = 0; block < 24; ++block)
                {
                    buffer.clear();
                    engine.render(buffer,
                                  block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(noteOn)
                                             : std::span<const coolsynth::synth::EngineMidiEvent>(),
                                  withChorus);
                    if (block < 8)
                        continue;
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float l = buffer.getSample(0, i);
                        const float r = buffer.getSample(1, i);
                        side += (l - r) * (l - r);
                        mid += (l + r) * (l + r);
                    }
                }
                return mid > 0.0f ? std::sqrt(side / mid) : 0.0f;
            };

            const float dryRatio = renderSideToMidRatio(false);
            const float chorusRatio = renderSideToMidRatio(true);
            expect(dryRatio < 1.0e-3f, "without chorus and pan spread both channels must be identical");
            expect(chorusRatio > 0.05f, "chorus must put different signals on left and right");
        }

        beginTest("v2_compressor_amount_reduces_steady_state_peaks_monotonically");
        {
            // Renders the same hot signal with three increasing compressor amounts and verifies
            // that the steady-state peak after the attack settles is monotonically non-increasing.
            const std::array<float, 3> amounts { 0.0f, 0.5f, 1.0f };
            std::array<float, 3> peaks {};

            for (size_t i = 0; i < amounts.size(); ++i)
            {
                coolsynth::testing::TestEngine engine;
                engine.prepare(48000.0, 256, 2);

                coolsynth::testing::EngineParameters parameters;
                parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
                parameters.oscA.level = 1.0f;
                parameters.oscB.level = 0.0f;
                parameters.ampEnvelope = makeStressEnvelope();
                parameters.filterEnvelope = makeStressEnvelope();
                parameters.filter.cutoffHz = 10000.0f;
                parameters.filter.resonanceNormalized = 0.1f;
                parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
                parameters.compressor.enabled = true;
                parameters.compressor.amount = amounts[i];
                parameters.compressor.mix = 1.0f;
                parameters.masterGainLinear = 1.0f;

                std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                    { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f }
                }};

                juce::AudioBuffer<float> buffer(2, 256);
                // Warm up: let the envelope follower settle past the 5 ms attack.
                for (int block = 0; block < 4; ++block)
                {
                    buffer.clear();
                    engine.render(buffer, block == 0 ? std::span<const coolsynth::synth::EngineMidiEvent>(noteOn) : std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
                }
                buffer.clear();
                engine.render(buffer, std::span<const coolsynth::synth::EngineMidiEvent>(), parameters);
                peaks[i] = computePeakAbs(buffer, 0, buffer.getNumSamples());
            }

            logMessage("compressor peaks: amount0=" + juce::String(peaks[0], 4)
                       + " amount0.5=" + juce::String(peaks[1], 4)
                       + " amount1=" + juce::String(peaks[2], 4));

            // Each step's peak must be <= the previous (small tolerance for makeup overshoot).
            expect(peaks[1] <= peaks[0] + 0.05f);
            expect(peaks[2] <= peaks[1] + 0.05f);
            // The fully compressed peak should clearly be lower than the bypass peak.
            expect(peaks[2] < peaks[0]);
        }
    }
};

class V2ArpeggiatorTests final : public juce::UnitTest
{
public:
    V2ArpeggiatorTests() : juce::UnitTest("Arpeggiator", "CoolSynth") {}

    static coolsynth::testing::EnvelopeParameters makeArpFriendlyEnvelope() noexcept
    {
        coolsynth::testing::EnvelopeParameters parameters;
        parameters.attackSeconds = 0.001f;
        parameters.decaySeconds = 0.01f;
        parameters.sustainLevel = 1.0f;
        parameters.releaseSeconds = 2.0f; // long enough that voices survive a step
        return parameters;
    }

    static coolsynth::synth::ArpParametersV2 makeBasicArpParameters() noexcept
    {
        coolsynth::synth::ArpParametersV2 parameters;
        parameters.enabled = true;
        parameters.internalTempoBpm = 120.0f;
        parameters.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
        parameters.pattern = coolsynth::parameters::ArpPatternChoice::up;
        parameters.octaveRange = 1;
        parameters.gateLength = 0.5f;
        parameters.swingAmount = 0.0f;
        parameters.chance = 1.0f;
        parameters.ratchetCount = coolsynth::parameters::ArpRatchetChoice::off;
        parameters.ratchetChance = 0.0f;
        parameters.accentEvery = coolsynth::parameters::ArpAccentEveryChoice::off;
        parameters.accentAmount = 0.0f;
        parameters.rhythm = coolsynth::parameters::ArpRhythmChoice::straight;
        parameters.euclideanPulses = 4;
        parameters.euclideanSteps = 8;
        parameters.euclideanRotation = 0;
        parameters.latch = false;
        return parameters;
    }

    static int collectNoteOns(const std::array<coolsynth::synth::EngineMidiEvent,
                                               coolsynth::synth::maxArpEventsPerBlock>& events,
                              int eventCount,
                              std::array<int, coolsynth::synth::maxArpEventsPerBlock>& outNotes) noexcept
    {
        int noteOnCount = 0;

        for (int index = 0; index < eventCount; ++index)
        {
            const auto& event = events[static_cast<size_t>(index)];
            if (event.type != coolsynth::synth::EngineMidiEventType::noteOn)
                continue;

            outNotes[static_cast<size_t>(noteOnCount++)] = static_cast<int>(event.noteNumber);
        }

        return noteOnCount;
    }

    static int renderArpBlock(coolsynth::synth::Arpeggiator& arp,
                              int blockSamples,
                              std::array<coolsynth::synth::EngineMidiEvent,
                                         coolsynth::synth::maxArpEventsPerBlock>& outEvents) noexcept
    {
        outEvents.fill({});
        return arp.generateEventsForBlock(blockSamples,
                                          48000.0,
                                          outEvents.data(),
                                          static_cast<int>(outEvents.size()));
    }

    static int findFirstNoteOnOffset(const std::array<coolsynth::synth::EngineMidiEvent,
                                                      coolsynth::synth::maxArpEventsPerBlock>& events,
                                     int eventCount) noexcept
    {
        for (int index = 0; index < eventCount; ++index)
        {
            if (events[static_cast<size_t>(index)].type
                == coolsynth::synth::EngineMidiEventType::noteOn)
                return events[static_cast<size_t>(index)].sampleOffset;
        }

        return -1;
    }

    static bool blockContainsNoteOn(const std::array<coolsynth::synth::EngineMidiEvent,
                                                     coolsynth::synth::maxArpEventsPerBlock>& events,
                                    int eventCount) noexcept
    {
        return findFirstNoteOnOffset(events, eventCount) >= 0;
    }

    static int collectEventsOfType(const std::array<coolsynth::synth::EngineMidiEvent,
                                                    coolsynth::synth::maxArpEventsPerBlock>& events,
                                   int eventCount,
                                   coolsynth::synth::EngineMidiEventType type,
                                   std::array<coolsynth::synth::EngineMidiEvent,
                                              coolsynth::synth::maxArpEventsPerBlock>& outEvents) noexcept
    {
        int outCount = 0;

        for (int index = 0; index < eventCount; ++index)
        {
            if (events[static_cast<size_t>(index)].type != type)
                continue;

            outEvents[static_cast<size_t>(outCount++)] = events[static_cast<size_t>(index)];
        }

        return outCount;
    }

    void runTest() override
    {
        beginTest("arp_disabled_passes_through_to_allocator_unchanged");
        {
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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

        beginTest("new_patterns_with_empty_working_set_emit_nothing");
        {
            constexpr int stepLength = 6000;
            const std::array patterns {
                coolsynth::parameters::ArpPatternChoice::converge,
                coolsynth::parameters::ArpPatternChoice::diverge,
                coolsynth::parameters::ArpPatternChoice::inside,
                coolsynth::parameters::ArpPatternChoice::outside,
                coolsynth::parameters::ArpPatternChoice::random,
                coolsynth::parameters::ArpPatternChoice::randomWalk,
                coolsynth::parameters::ArpPatternChoice::chord,
            };

            for (const auto pattern : patterns)
            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = pattern;
                arp.setParameters(parameters);

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                const int eventCount = renderArpBlock(arp, stepLength, events);
                expectEquals(eventCount, 0);
            }
        }

        beginTest("new_patterns_single_note_collapse_to_repeating_single_note");
        {
            constexpr int stepLength = 6000;
            const std::array patterns {
                coolsynth::parameters::ArpPatternChoice::converge,
                coolsynth::parameters::ArpPatternChoice::diverge,
                coolsynth::parameters::ArpPatternChoice::inside,
                coolsynth::parameters::ArpPatternChoice::outside,
                coolsynth::parameters::ArpPatternChoice::random,
                coolsynth::parameters::ArpPatternChoice::randomWalk,
                coolsynth::parameters::ArpPatternChoice::chord,
            };

            for (const auto pattern : patterns)
            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = pattern;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 0.8f);

                for (int step = 0; step < 3; ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    const int noteOnCount = collectNoteOns(events, eventCount, noteOns);

                    expectEquals(noteOnCount, 1);
                    if (noteOnCount == 1)
                        expectEquals(noteOns[0], 60);
                }
            }
        }

        beginTest("pattern_converge_and_diverge_follow_documented_index_formulas");
        {
            constexpr int stepLength = 6000;

            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::converge;
                parameters.octaveRange = 2;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 1.0f);
                arp.onNoteOn(64, 1.0f);
                arp.onNoteOn(67, 1.0f);

                const std::array<int, 6> expected { 60, 79, 64, 76, 67, 72 };

                for (int step = 0; step < static_cast<int>(expected.size()); ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                    expectEquals(noteOnCount, 1);
                    if (noteOnCount == 1)
                        expectEquals(noteOns[0], expected[static_cast<size_t>(step)]);
                }
            }

            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::diverge;
                parameters.octaveRange = 2;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 1.0f);
                arp.onNoteOn(64, 1.0f);
                arp.onNoteOn(67, 1.0f);

                const std::array<int, 6> expected { 72, 67, 76, 64, 79, 60 };

                for (int step = 0; step < static_cast<int>(expected.size()); ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                    expectEquals(noteOnCount, 1);
                    if (noteOnCount == 1)
                        expectEquals(noteOns[0], expected[static_cast<size_t>(step)]);
                }
            }
        }

        beginTest("pattern_inside_and_outside_reorder_within_each_octave");
        {
            constexpr int stepLength = 6000;

            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::inside;
                parameters.octaveRange = 2;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 1.0f);
                arp.onNoteOn(64, 1.0f);
                arp.onNoteOn(67, 1.0f);

                const std::array<int, 6> expected { 60, 67, 64, 72, 79, 76 };

                for (int step = 0; step < static_cast<int>(expected.size()); ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                    expectEquals(noteOnCount, 1);
                    if (noteOnCount == 1)
                        expectEquals(noteOns[0], expected[static_cast<size_t>(step)]);
                }
            }

            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::outside;
                parameters.octaveRange = 2;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 1.0f);
                arp.onNoteOn(64, 1.0f);
                arp.onNoteOn(67, 1.0f);

                const std::array<int, 6> expected { 67, 60, 64, 79, 72, 76 };

                for (int step = 0; step < static_cast<int>(expected.size()); ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                    expectEquals(noteOnCount, 1);
                    if (noteOnCount == 1)
                        expectEquals(noteOns[0], expected[static_cast<size_t>(step)]);
                }
            }
        }

        beginTest("pattern_random_is_exactly_reproducible_under_seed");
        {
            constexpr int stepLength = 6000;
            constexpr uint64_t seed = 0x1234abcdULL;
            const std::array<int, 3> sortedNotes { 60, 64, 67 };

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);
            arp.setSeedForTesting(seed);

            auto parameters = makeBasicArpParameters();
            parameters.pattern = coolsynth::parameters::ArpPatternChoice::random;
            parameters.octaveRange = 2;
            arp.setParameters(parameters);
            arp.onNoteOn(67, 1.0f);
            arp.onNoteOn(60, 1.0f);
            arp.onNoteOn(64, 1.0f);

            juce::Random referenceRng(static_cast<int64_t>(seed));

            for (int step = 0; step < 16; ++step)
            {
                const int octaveShift = referenceRng.nextInt(2);
                const int noteIndex = referenceRng.nextInt(3);
                const int expectedNote = sortedNotes[static_cast<size_t>(noteIndex)]
                    + (12 * octaveShift);

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                const int eventCount = renderArpBlock(arp, stepLength, events);
                const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                expectEquals(noteOnCount, 1);
                if (noteOnCount == 1)
                    expectEquals(noteOns[0], expectedNote);
            }
        }

        beginTest("pattern_random_walk_is_exactly_reproducible_under_seed");
        {
            constexpr int stepLength = 6000;
            constexpr uint64_t seed = 0x3456fedcULL;
            const std::array<int, 3> sortedNotes { 60, 64, 67 };
            constexpr int octaveRange = 2;
            constexpr int totalSweepLength = static_cast<int>(sortedNotes.size()) * octaveRange;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);
            arp.setSeedForTesting(seed);

            auto parameters = makeBasicArpParameters();
            parameters.pattern = coolsynth::parameters::ArpPatternChoice::randomWalk;
            parameters.octaveRange = octaveRange;
            arp.setParameters(parameters);
            arp.onNoteOn(67, 1.0f);
            arp.onNoteOn(60, 1.0f);
            arp.onNoteOn(64, 1.0f);

            juce::Random referenceRng(static_cast<int64_t>(seed));
            int walkIndex = 0;

            for (int step = 0; step < 16; ++step)
            {
                const float roll = referenceRng.nextFloat();
                int nextIndex = walkIndex;

                if (roll < 0.50f)
                    ++nextIndex;
                else if (roll < 0.75f)
                    --nextIndex;

                if (nextIndex < 0)
                    nextIndex = 1;
                else if (nextIndex >= totalSweepLength)
                    nextIndex = totalSweepLength - 2;

                walkIndex = juce::jlimit(0, totalSweepLength - 1, nextIndex);

                const int expectedNote = sortedNotes[static_cast<size_t>(walkIndex % 3)]
                    + (12 * (walkIndex / 3));

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                const int eventCount = renderArpBlock(arp, stepLength, events);
                const int noteOnCount = collectNoteOns(events, eventCount, noteOns);
                expectEquals(noteOnCount, 1);
                if (noteOnCount == 1)
                    expectEquals(noteOns[0], expectedNote);
            }
        }

        beginTest("pattern_chord_emits_full_stabs_cycles_octaves_and_cleans_up_ringing_notes");
        {
            {
                constexpr int blockSamples = 6000;

                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::chord;
                parameters.octaveRange = 2;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 0.9f);
                arp.onNoteOn(64, 0.8f);
                arp.onNoteOn(67, 0.7f);

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                int eventCount = renderArpBlock(arp, blockSamples, events);
                int noteOnCount = collectNoteOns(events, eventCount, noteOns);

                expectEquals(noteOnCount, 3);
                if (noteOnCount == 3)
                {
                    expectEquals(noteOns[0], 60);
                    expectEquals(noteOns[1], 64);
                    expectEquals(noteOns[2], 67);
                }

                eventCount = renderArpBlock(arp, blockSamples, events);
                noteOnCount = collectNoteOns(events, eventCount, noteOns);

                expectEquals(noteOnCount, 3);
                if (noteOnCount == 3)
                {
                    expectEquals(noteOns[0], 72);
                    expectEquals(noteOns[1], 76);
                    expectEquals(noteOns[2], 79);
                }
            }

            {
                constexpr int blockSamples = 3000;

                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.pattern = coolsynth::parameters::ArpPatternChoice::chord;
                parameters.octaveRange = 2;
                parameters.gateLength = 0.95f;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 0.9f);
                arp.onNoteOn(64, 0.8f);
                arp.onNoteOn(67, 0.7f);

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                int eventCount = renderArpBlock(arp, blockSamples, events);
                int noteOnCount = collectNoteOns(events, eventCount, noteOns);

                expectEquals(noteOnCount, 3);
                expectEquals(arp.getRingingNoteCountForTesting(), 3);

                parameters.enabled = false;
                arp.setParameters(parameters);
                eventCount = renderArpBlock(arp, blockSamples, events);

                int noteOffCount = 0;
                for (int index = 0; index < eventCount; ++index)
                    if (events[static_cast<size_t>(index)].type
                        == coolsynth::synth::EngineMidiEventType::noteOff)
                        ++noteOffCount;

                expectEquals(noteOffCount, 3);
                expectEquals(arp.getRingingNoteCountForTesting(), 0);
            }
        }

        beginTest("swing_offsets_every_other_step_under_internal_clock");
        {
            constexpr int stepLength = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.swingAmount = 0.5f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 1.0f);

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};

            int eventCount = renderArpBlock(arp, stepLength, events);
            expectEquals(findFirstNoteOnOffset(events, eventCount), 0);

            eventCount = renderArpBlock(arp, stepLength, events);
            expectEquals(findFirstNoteOnOffset(events, eventCount), 1500);

            eventCount = renderArpBlock(arp, stepLength, events);
            expectEquals(findFirstNoteOnOffset(events, eventCount), 0);
        }

        beginTest("swing_stays_rate_relative_under_host_sync");
        {
            constexpr int blockSamples = 24000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.rate = coolsynth::parameters::ArpRateChoice::quarter;
            parameters.swingAmount = 0.5f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 1.0f);

            coolsynth::synth::EngineTransportInfo transport;
            transport.hostHasTempo = true;
            transport.hostHasPpq = true;
            transport.hostIsPlaying = true;
            transport.hostBpm = 120.0;

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};

            transport.hostPpqAtBlockStart = 0.0;
            arp.setTransportInfo(transport);
            int eventCount = renderArpBlock(arp, blockSamples, events);
            expectEquals(findFirstNoteOnOffset(events, eventCount), 0);

            transport.hostPpqAtBlockStart = 1.0;
            arp.setTransportInfo(transport);
            eventCount = renderArpBlock(arp, blockSamples, events);
            expectEquals(findFirstNoteOnOffset(events, eventCount), 6000);
        }

        beginTest("chance_uses_seeded_rng_and_skips_exact_known_count_over_long_run");
        {
            constexpr int stepLength = 6000;
            constexpr int stepCount = 1000;
            constexpr uint64_t seed = 0x5a17cafeULL;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);
            arp.setSeedForTesting(seed);

            auto parameters = makeBasicArpParameters();
            parameters.chance = 0.5f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 1.0f);

            juce::Random referenceRng(static_cast<int64_t>(seed));
            int expectedNoteOnCount = 0;
            for (int step = 0; step < stepCount; ++step)
                if (referenceRng.nextFloat() < 0.5f)
                    ++expectedNoteOnCount;

            int actualNoteOnCount = 0;
            for (int step = 0; step < stepCount; ++step)
            {
                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                const int eventCount = renderArpBlock(arp, stepLength, events);
                actualNoteOnCount += collectNoteOns(events, eventCount, noteOns);
            }

            expectEquals(actualNoteOnCount, expectedNoteOnCount);
            expectEquals(stepCount - actualNoteOnCount, stepCount - expectedNoteOnCount);
        }

        beginTest("euclidean_known_patterns_match_standard_sequences");
        {
            struct EuclideanCase
            {
                int pulses;
                int steps;
                int rotation;
                std::array<bool, 16> expected;
                int expectedLength;
            };

            const std::array<EuclideanCase, 4> cases {{
                { 3, 8, 0, { true, false, false, true, false, false, true, false }, 8 },
                { 5, 8, 0, { true, false, true, true, false, true, true, false }, 8 },
                { 7, 12, 0, { true, false, true, false, true, true, false, true, false, true, true, false }, 12 },
                { 3, 8, 1, { false, false, true, false, false, true, false, true }, 8 },
            }};

            constexpr int stepLength = 6000;

            for (const auto& testCase : cases)
            {
                coolsynth::synth::Arpeggiator arp;
                arp.prepare(48000.0);

                auto parameters = makeBasicArpParameters();
                parameters.rhythm = coolsynth::parameters::ArpRhythmChoice::euclidean;
                parameters.euclideanPulses = testCase.pulses;
                parameters.euclideanSteps = testCase.steps;
                parameters.euclideanRotation = testCase.rotation;
                arp.setParameters(parameters);
                arp.onNoteOn(60, 1.0f);

                for (int step = 0; step < testCase.expectedLength; ++step)
                {
                    std::array<coolsynth::synth::EngineMidiEvent,
                               coolsynth::synth::maxArpEventsPerBlock> events {};
                    const int eventCount = renderArpBlock(arp, stepLength, events);
                    expect(blockContainsNoteOn(events, eventCount)
                           == testCase.expected[static_cast<size_t>(step)]);
                }
            }
        }

        beginTest("euclidean_rests_advance_rhythm_slots_without_advancing_the_melodic_walk");
        {
            constexpr int stepLength = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.rhythm = coolsynth::parameters::ArpRhythmChoice::euclidean;
            parameters.euclideanPulses = 3;
            parameters.euclideanSteps = 8;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 1.0f);
            arp.onNoteOn(64, 1.0f);
            arp.onNoteOn(67, 1.0f);
            arp.onNoteOn(72, 1.0f);

            const std::array<bool, 8> expectedPulseSlots { true, false, false, true, false, false, true, false };
            const std::array<int, 3> expectedNotes { 60, 64, 67 };
            int emittedIndex = 0;

            for (int slot = 0; slot < static_cast<int>(expectedPulseSlots.size()); ++slot)
            {
                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<int, coolsynth::synth::maxArpEventsPerBlock> notes {};
                const int eventCount = renderArpBlock(arp, stepLength, events);
                const int noteOnCount = collectNoteOns(events, eventCount, notes);

                expectEquals(noteOnCount, expectedPulseSlots[static_cast<size_t>(slot)] ? 1 : 0);
                if (noteOnCount == 1)
                    expectEquals(notes[0], expectedNotes[static_cast<size_t>(emittedIndex++)]);
            }
        }

        beginTest("host_synced_euclidean_cycle_resets_at_bar_start");
        {
            constexpr int stepLength = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.rhythm = coolsynth::parameters::ArpRhythmChoice::euclidean;
            parameters.euclideanPulses = 1;
            parameters.euclideanSteps = 7;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 1.0f);

            coolsynth::synth::EngineTransportInfo transport;
            transport.hostHasTempo = true;
            transport.hostHasPpq = true;
            transport.hostIsPlaying = true;
            transport.hostBpm = 120.0;

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};

            transport.hostPpqAtBlockStart = 0.0;
            arp.setTransportInfo(transport);
            int eventCount = renderArpBlock(arp, stepLength, events);
            expect(blockContainsNoteOn(events, eventCount));

            transport.hostPpqAtBlockStart = 3.5;
            arp.setTransportInfo(transport);
            eventCount = renderArpBlock(arp, stepLength, events);
            expect(blockContainsNoteOn(events, eventCount));

            transport.hostPpqAtBlockStart = 4.0;
            arp.setTransportInfo(transport);
            eventCount = renderArpBlock(arp, stepLength, events);
            expect(blockContainsNoteOn(events, eventCount));
        }

        beginTest("ratchet_emits_exact_sub_step_offsets_and_note_counts");
        {
            constexpr int blockSamples = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.ratchetCount = coolsynth::parameters::ArpRatchetChoice::x3;
            parameters.ratchetChance = 1.0f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 0.5f);

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};
            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> noteOns {};
            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> noteOffs {};
            const int eventCount = renderArpBlock(arp, blockSamples, events);
            const int noteOnCount = collectEventsOfType(events,
                                                        eventCount,
                                                        coolsynth::synth::EngineMidiEventType::noteOn,
                                                        noteOns);
            const int noteOffCount = collectEventsOfType(events,
                                                         eventCount,
                                                         coolsynth::synth::EngineMidiEventType::noteOff,
                                                         noteOffs);

            expectEquals(noteOnCount, 3);
            expectEquals(noteOffCount, 3);

            if (noteOnCount == 3)
            {
                expectEquals(noteOns[0].sampleOffset, 0);
                expectEquals(noteOns[1].sampleOffset, 1000);
                expectEquals(noteOns[2].sampleOffset, 2000);
            }

            if (noteOffCount == 3)
            {
                expectEquals(noteOffs[0].sampleOffset, 1000);
                expectEquals(noteOffs[1].sampleOffset, 2000);
                expectEquals(noteOffs[2].sampleOffset, 3000);
            }
        }

        beginTest("accent_boosts_velocity_on_emitted_step_grid_not_raw_steps");
        {
            constexpr int blockSamples = 6000;
            constexpr uint64_t seed = 0x0ddc0ffeULL;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);
            arp.setSeedForTesting(seed);

            auto parameters = makeBasicArpParameters();
            parameters.chance = 0.5f;
            parameters.accentEvery = coolsynth::parameters::ArpAccentEveryChoice::every2;
            parameters.accentAmount = 0.5f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 0.4f);

            juce::Random referenceRng(static_cast<int64_t>(seed));
            int emittedStepIndex = 0;

            for (int step = 0; step < 12; ++step)
            {
                const bool emits = referenceRng.nextFloat() < 0.5f;

                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> events {};
                std::array<coolsynth::synth::EngineMidiEvent,
                           coolsynth::synth::maxArpEventsPerBlock> noteOns {};
                const int eventCount = renderArpBlock(arp, blockSamples, events);
                const int noteOnCount = collectEventsOfType(events,
                                                            eventCount,
                                                            coolsynth::synth::EngineMidiEventType::noteOn,
                                                            noteOns);

                expectEquals(noteOnCount, emits ? 1 : 0);
                if (! emits)
                    continue;

                const float expectedVelocity = (emittedStepIndex % 2) == 0 ? 0.6f : 0.4f;
                expectWithinAbsoluteError(noteOns[0].value, expectedVelocity, 0.0001f);
                ++emittedStepIndex;
            }
        }

        beginTest("chord_ratchet_and_accent_apply_uniformly_across_the_whole_step");
        {
            constexpr int blockSamples = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.pattern = coolsynth::parameters::ArpPatternChoice::chord;
            parameters.ratchetCount = coolsynth::parameters::ArpRatchetChoice::x2;
            parameters.ratchetChance = 1.0f;
            parameters.accentEvery = coolsynth::parameters::ArpAccentEveryChoice::every2;
            parameters.accentAmount = 0.25f;
            arp.setParameters(parameters);
            arp.onNoteOn(60, 0.6f);
            arp.onNoteOn(64, 0.5f);
            arp.onNoteOn(67, 0.4f);

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};
            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> noteOns {};
            int eventCount = renderArpBlock(arp, blockSamples, events);
            int noteOnCount = collectEventsOfType(events,
                                                  eventCount,
                                                  coolsynth::synth::EngineMidiEventType::noteOn,
                                                  noteOns);

            expectEquals(noteOnCount, 6);
            if (noteOnCount == 6)
            {
                expectEquals(noteOns[0].sampleOffset, 0);
                expectEquals(noteOns[1].sampleOffset, 0);
                expectEquals(noteOns[2].sampleOffset, 0);
                expectEquals(noteOns[3].sampleOffset, 1500);
                expectEquals(noteOns[4].sampleOffset, 1500);
                expectEquals(noteOns[5].sampleOffset, 1500);
                expectWithinAbsoluteError(noteOns[0].value, 0.75f, 0.0001f);
                expectWithinAbsoluteError(noteOns[1].value, 0.625f, 0.0001f);
                expectWithinAbsoluteError(noteOns[2].value, 0.5f, 0.0001f);
            }

            eventCount = renderArpBlock(arp, blockSamples, events);
            noteOnCount = collectEventsOfType(events,
                                              eventCount,
                                              coolsynth::synth::EngineMidiEventType::noteOn,
                                              noteOns);

            expectEquals(noteOnCount, 6);
            if (noteOnCount == 6)
            {
                expectWithinAbsoluteError(noteOns[0].value, 0.6f, 0.0001f);
                expectWithinAbsoluteError(noteOns[1].value, 0.5f, 0.0001f);
                expectWithinAbsoluteError(noteOns[2].value, 0.4f, 0.0001f);
            }
        }

        beginTest("ratcheted_chord_step_is_dropped_atomically_when_it_would_overflow_event_capacity");
        {
            constexpr int blockSamples = 6000;

            coolsynth::synth::Arpeggiator arp;
            arp.prepare(48000.0);

            auto parameters = makeBasicArpParameters();
            parameters.pattern = coolsynth::parameters::ArpPatternChoice::chord;
            parameters.ratchetCount = coolsynth::parameters::ArpRatchetChoice::x4;
            parameters.ratchetChance = 1.0f;
            arp.setParameters(parameters);

            for (int note = 0; note < coolsynth::synth::maxArpHeldNotes; ++note)
                arp.onNoteOn(48 + note, 0.7f);

            std::array<coolsynth::synth::EngineMidiEvent,
                       coolsynth::synth::maxArpEventsPerBlock> events {};
            const int eventCount = renderArpBlock(arp, blockSamples, events);

            expectEquals(eventCount, 0);
            expectEquals(arp.getRingingNoteCountForTesting(), 0);
        }

        beginTest("octave_range_two_alternates_base_and_plus_twelve");
        {
            constexpr int stepLength = 6000;
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            std::array<coolsynth::testing::VoiceDebugState, coolsynth::testing::voiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            bool anyKeyDown = false;
            for (const auto& s : states)
                anyKeyDown = anyKeyDown || (s.active && s.keyDown);
            expect(! anyKeyDown);
        }

        beginTest("gate_length_long_carries_release_across_block");
        {
            constexpr int blockSamples = 3000; // half a step
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            coolsynth::testing::TestEngine engine;
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
            std::array<coolsynth::testing::VoiceDebugState, coolsynth::testing::voiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            for (const auto& s : states)
                if (s.active && s.keyDown) ++keyDownCount;
            expectEquals(engine.getArpRingingNoteCountForTesting(), 0);
            expectEquals(keyDownCount, 0);
        }

        beginTest("arp_disable_mid_play_releases_ringing_note");
        {
            constexpr int blockSamples = 3000; // shorter than the long gate so the note is still held
            coolsynth::testing::TestEngine engine;
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
            std::array<coolsynth::testing::VoiceDebugState, coolsynth::testing::voiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            for (const auto& s : states)
                if (s.active && s.keyDown) ++keyDownCount;
            expectEquals(engine.getArpRingingNoteCountForTesting(), 0);
            expectEquals(keyDownCount, 0);
        }

        beginTest("enabling_arp_while_a_key_is_held_releases_the_voice_and_seeds_the_arp");
        {
            // Regression: a voice started with the arp off stayed keyDown forever once the
            // arp was switched on, because its note-off was then routed to the arp only.
            constexpr int stepLength = 6000; // 48k / 120bpm / 8 = 1/16th
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, stepLength, 2);

            const auto countKeyDownVoices = [&engine]
            {
                std::array<coolsynth::testing::VoiceDebugState, coolsynth::testing::voiceCount> states {};
                engine.copyVoiceStatesForTesting(states);
                int keyDown = 0;
                for (const auto& s : states)
                    if (s.active && s.keyDown) ++keyDown;
                return keyDown;
            };

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = false;

            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOn {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, noteOn, parameters);
            expectEquals(engine.getActiveVoiceCountForTesting(), 1);
            expectEquals(countKeyDownVoices(), 1);

            // Switch the arp on while the key is still held.
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = false;
            buffer.clear();
            engine.render(buffer, {}, parameters);

            // The held key now drives the arp and no allocator voice is left keyDown.
            expectEquals(engine.getArpHeldNoteCountForTesting(), 1);
            expectEquals(countKeyDownVoices(), 0);

            // Releasing the key must empty the arp's held set and leave nothing keyDown.
            std::array<coolsynth::synth::EngineMidiEvent, 1> noteOff {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f, false }
            }};
            buffer.clear();
            engine.render(buffer, noteOff, parameters);
            buffer.clear();
            engine.render(buffer, {}, parameters);

            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
            expectEquals(countKeyDownVoices(), 0);
        }

        beginTest("disabling_arp_with_keys_held_forgets_them_so_re_enable_plays_no_ghost_notes");
        {
            constexpr int stepLength = 6000;
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = false;

            std::array<coolsynth::synth::EngineMidiEvent, 3> chord {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 64, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 67, 1.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, chord, parameters);
            expectEquals(engine.getArpHeldNoteCountForTesting(), 3);

            // Switch the arp off while the chord is still held: it must forget the keys.
            parameters.arp.enabled = false;
            buffer.clear();
            engine.render(buffer, {}, parameters);
            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);

            // Release the keys with the arp off, then switch it back on with nothing held.
            std::array<coolsynth::synth::EngineMidiEvent, 3> release {{
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 60, 0.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 64, 0.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, 0, 67, 0.0f, false }
            }};
            buffer.clear();
            engine.render(buffer, release, parameters);

            parameters.arp.enabled = true;
            for (int block = 0; block < 4; ++block)
            {
                buffer.clear();
                engine.render(buffer, {}, parameters);
            }

            int keyDownCount = 0;
            std::array<coolsynth::testing::VoiceDebugState, coolsynth::testing::voiceCount> states {};
            engine.copyVoiceStatesForTesting(states);
            for (const auto& s : states)
                if (s.active && s.keyDown) ++keyDownCount;

            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
            expectEquals(keyDownCount, 0);
        }

        beginTest("fast_key_tap_within_single_block_still_triggers_arp_step");
        {
            // A short, fast key tap whose noteOn and noteOff both land in the
            // same audio block must still fire an arp step. Regression: prior
            // behavior absorbed all note events into the held set before
            // generateEventsForBlock, so a tap was added-then-removed and the
            // step generator saw an empty set.
            constexpr int stepLength = 6000; // 48k / 120bpm / 8 = 1/16th
            coolsynth::testing::TestEngine engine;
            engine.prepare(48000.0, stepLength, 2);

            auto parameters = makeBasicParameters();
            parameters.arp.enabled = true;
            parameters.arp.internalTempoBpm = 120.0f;
            parameters.arp.rate = coolsynth::parameters::ArpRateChoice::sixteenth;
            parameters.arp.pattern = coolsynth::parameters::ArpPatternChoice::up;
            parameters.arp.octaveRange = 1;
            parameters.arp.gateLength = 0.5f;
            parameters.arp.latch = false;

            // Press + release within the same block (~125 ms tap inside a 125 ms block).
            std::array<coolsynth::synth::EngineMidiEvent, 2> tap {{
                { coolsynth::synth::EngineMidiEventType::noteOn, 0, 60, 1.0f, false },
                { coolsynth::synth::EngineMidiEventType::noteOff, stepLength - 1, 60, 0.0f, false }
            }};

            juce::AudioBuffer<float> buffer(2, stepLength);
            buffer.clear();
            engine.render(buffer, tap, parameters);

            // Step must have fired and routed the tapped note through the allocator.
            expectEquals(engine.getLastPlayedNoteForTesting(), 60);

            // After the block the deferred note-off must have cleared the held set,
            // so the next empty block must not produce any further step on that note.
            expectEquals(engine.getArpHeldNoteCountForTesting(), 0);
        }

        beginTest("panic_clears_held_latched_and_ringing_arp_state");
        {
            constexpr int stepLength = 6000;
            coolsynth::testing::TestEngine engine;
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

    static coolsynth::testing::EngineParameters makeBasicParameters() noexcept
    {
        coolsynth::testing::EngineParameters parameters;
        parameters.oscA.waveShape = coolsynth::testing::OscillatorWaveShape::saw;
        parameters.oscA.level = 1.0f;
        parameters.oscB.level = 0.0f;
        parameters.ampEnvelope = makeArpFriendlyEnvelope();
        parameters.filterEnvelope = makeStressEnvelope();
        parameters.filter.cutoffHz = 8000.0f;
        parameters.filter.resonanceNormalized = 0.1f;
        parameters.filter.envelopeAmount = 0.0f;
        parameters.filter.keyTracking = coolsynth::testing::FilterKeyTrackingMode::off;
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
static V2ArpeggiatorTests v2ArpeggiatorTests;
static V2ScopeFifoTests v2ScopeFifoTests;

class HalfBandDecimatorTests final : public juce::UnitTest
{
public:
    HalfBandDecimatorTests() : juce::UnitTest("HalfBandDecimator", "CoolSynth") {}

    void runTest() override
    {
        // B5: Mono and unison modes both produce audible, bounded output.
        // The 2x decimator must pass the audio band untouched and bury everything above the host Nyquist.
        beginTest("halfband_decimator_passes_band_and_rejects_images");
        {
            const double hostRate = 48000.0;
            const double overRate = hostRate * 2.0;
            const int outputSamples = 8192;

            const auto measure = [&](double toneHz)
            {
                coolsynth::synth::HalfBandDecimator2x decimator;
                double sumSquares = 0.0;
                int counted = 0;
                for (int i = 0; i < outputSamples; ++i)
                {
                    const auto t0 = static_cast<double>(2 * i) / overRate;
                    const auto t1 = static_cast<double>(2 * i + 1) / overRate;
                    const float out = decimator.process(static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * toneHz * t0)),
                                                        static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * toneHz * t1)));
                    if (i >= 512)
                    {
                        sumSquares += static_cast<double>(out) * static_cast<double>(out);
                        ++counted;
                    }
                }
                return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(counted)));
            };

            const float passbandRms = measure(10000.0);
            const float stopbandRms = measure(30000.0);
            const float unityRms = static_cast<float>(1.0 / std::sqrt(2.0));

            logMessage("decimator rms: 10 kHz=" + juce::String(passbandRms, 4) + " 30 kHz=" + juce::String(stopbandRms, 6));
            expect(std::abs(passbandRms - unityRms) < 0.02f, "10 kHz must pass at unity");
            expect(stopbandRms < unityRms * 0.0002f, "30 kHz image must be rejected by more than 70 dB");
        }
    }
};

static HalfBandDecimatorTests halfBandDecimatorTests;
