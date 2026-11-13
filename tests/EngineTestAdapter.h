#pragma once

#include <memory>
#include <span>

#include <juce_audio_basics/juce_audio_basics.h>

#include "synth/v3/EngineV3.h"

/*
    Gives the V3 engine the testing surface the V2 engine had, so the effect-rack and
    arpeggiator regressions written against V2 keep describing the same behaviour on V3.
    The settings struct keeps the V2 field names the tests set; the adapter maps them onto
    V3 slots: oscillator A and B onto two VA generators, the filter onto a ladder in P1, the
    amplifier and filter envelopes onto ENV1 and ENV2.
*/
namespace coolsynth::testing
{
    inline constexpr int voiceCount = coolsynth::synth::v3::Part::maxVoices;

    enum class OscillatorWaveShape { pulse, triangle, saw, sine };
    enum class FilterKeyTrackingMode { off, half, full };

    struct EnvelopeParameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 0.8f;
        float releaseSeconds = 0.3f;
    };

    struct OscillatorParameters
    {
        OscillatorWaveShape waveShape = OscillatorWaveShape::saw;
        float level = 0.8f;
        float fineCents = 0.0f;
        float pulseWidth = 0.5f;
    };

    struct FilterParameters
    {
        float cutoffHz = 3200.0f;
        float resonanceNormalized = 0.08f;
        float envelopeAmount = 0.35f;
        FilterKeyTrackingMode keyTracking = FilterKeyTrackingMode::half;
    };

    struct PerformanceParameters
    {
        float panSpread = 0.0f;
    };

    struct EngineParameters
    {
        OscillatorParameters oscA {};
        OscillatorParameters oscB { OscillatorWaveShape::saw, 0.68f, 4.0f, 0.5f };
        FilterParameters filter {};
        EnvelopeParameters ampEnvelope {};
        EnvelopeParameters filterEnvelope {};
        PerformanceParameters performance {};
        coolsynth::synth::ArpParametersV2 arp {};
        coolsynth::synth::DriveParametersV2 drive {};
        coolsynth::synth::PhaserParametersV2 phaser {};
        coolsynth::synth::ChorusParametersV2 chorus {};
        coolsynth::synth::DelayParametersV2 delay {};
        coolsynth::synth::ReverbParametersV2 reverb {};
        coolsynth::synth::CompressorParametersV2 compressor {};
        float masterGainLinear = 1.0f;
    };

    struct VoiceDebugState
    {
        bool active = false;
        bool keyDown = false;
        int noteNumber = -1;
    };

    class TestEngine final
    {
    public:
        TestEngine() : engine(std::make_unique<coolsynth::synth::v3::EngineV3>()) {}

        void prepare(double sampleRate, int blockSize, int channels) { engine->prepare(sampleRate, blockSize, channels); }

        void render(juce::AudioBuffer<float>& buffer,
                    std::span<const coolsynth::synth::EngineMidiEvent> events,
                    const EngineParameters& parameters,
                    const coolsynth::synth::EngineTransportInfo& transport = {})
        {
            settings = toSettings(parameters);
            engine->render(buffer, events, settings, transport);
        }

        void panic() { engine->panic(); }

        [[nodiscard]] int getActiveVoiceCountForTesting() const { return part().activeVoiceCountForTesting(); }
        [[nodiscard]] int getArpHeldNoteCountForTesting() const { return part().arpHeldNoteCountForTesting(); }
        [[nodiscard]] int getArpLatchedNoteCountForTesting() const { return part().arpLatchedNoteCountForTesting(); }
        [[nodiscard]] int getArpRingingNoteCountForTesting() const { return part().arpRingingNoteCountForTesting(); }
        [[nodiscard]] int getLastPlayedNoteForTesting() const { return part().lastPlayedNoteForTesting(); }

        void copyVoiceStatesForTesting(std::span<VoiceDebugState> states) const
        {
            for (std::size_t i = 0; i < states.size() && i < static_cast<std::size_t>(voiceCount); ++i)
            {
                const int index = static_cast<int>(i);
                const auto& voice = part().voiceForTesting(index);
                states[i] = { voice.isActive(), part().voiceKeyDownForTesting(index), voice.currentNote() };
            }
        }

    private:
        [[nodiscard]] coolsynth::synth::v3::Part& part() const { return engine->partForTesting(); }

        static float normalised(const coolsynth::synth::v3::ModuleDescriptor& d, int param, float units)
        {
            return coolsynth::synth::v3::unitsToNormalised(d.params[static_cast<std::size_t>(param)], units);
        }

        static coolsynth::synth::v3::GeneratorSettings generator(const OscillatorParameters& oscillator)
        {
            using coolsynth::synth::v3::VaGenerator;
            const auto& d = VaGenerator::descriptor();
            const auto wave = oscillator.waveShape == OscillatorWaveShape::pulse      ? VaGenerator::Wave::pulse
                            : oscillator.waveShape == OscillatorWaveShape::triangle ? VaGenerator::Wave::triangle
                            : oscillator.waveShape == OscillatorWaveShape::sine     ? VaGenerator::Wave::sine
                                                                                   : VaGenerator::Wave::saw;
            coolsynth::synth::v3::GeneratorSettings g;
            g.type = coolsynth::synth::v3::generatorType::va;
            g.p[VaGenerator::shape] = normalised(d, VaGenerator::shape, static_cast<float>(wave));
            g.p[VaGenerator::pulseWidth] = normalised(d, VaGenerator::pulseWidth, oscillator.pulseWidth);
            g.p[VaGenerator::unison] = 0.0f;
            g.p[VaGenerator::mix] = normalised(d, VaGenerator::mix, 0.67f);
            g.p[VaGenerator::randomPhase] = 0.0f;
            g.level = oscillator.level;
            g.pitchSemitones = oscillator.fineCents * 0.01f;
            return g;
        }

        static coolsynth::synth::v3::Envelope::Parameters envelope(const EnvelopeParameters& e)
        {
            coolsynth::synth::v3::Envelope::Parameters p;
            p.attack = e.attackSeconds;
            p.decay = e.decaySeconds;
            p.sustain = e.sustainLevel;
            p.release = e.releaseSeconds;
            return p;
        }

        static coolsynth::synth::v3::EngineSettings toSettings(const EngineParameters& p)
        {
            using coolsynth::synth::v3::LadderProcessor;
            coolsynth::synth::v3::EngineSettings s;
            auto& voice = s.part.voice;
            voice.generators[0] = generator(p.oscA);
            if (p.oscB.level > 0.0f)
                voice.generators[1] = generator(p.oscB);

            const auto& ladder = LadderProcessor::descriptor();
            auto& filter = voice.processors[0];
            filter.type = coolsynth::synth::v3::processorType::ladder;
            filter.p[LadderProcessor::cutoff] = normalised(ladder, LadderProcessor::cutoff, p.filter.cutoffHz);
            filter.p[LadderProcessor::resonance] = normalised(ladder, LadderProcessor::resonance, p.filter.resonanceNormalized);
            filter.p[LadderProcessor::keyTrack] = p.filter.keyTracking == FilterKeyTrackingMode::full ? 1.0f
                                                : p.filter.keyTracking == FilterKeyTrackingMode::half ? 0.5f
                                                                                                      : 0.0f;
            filter.p[LadderProcessor::bass] = normalised(ladder, LadderProcessor::bass, 0.5f);

            voice.envelopes[0] = envelope(p.ampEnvelope);
            voice.envelopes[0].velocitySensitivity = 1.0f;
            voice.envelopes[1] = envelope(p.filterEnvelope);
            voice.env2ToP1 = 0.5f * p.filter.envelopeAmount;

            s.part.arp = p.arp;
            s.drive = p.drive;
            s.phaser = p.phaser;
            s.chorus = p.chorus;
            s.delay = p.delay;
            s.reverb = p.reverb;
            s.compressor = p.compressor;
            s.masterGainLinear = p.masterGainLinear;
            return s;
        }

        std::unique_ptr<coolsynth::synth::v3::EngineV3> engine;
        coolsynth::synth::v3::EngineSettings settings;
    };
}
