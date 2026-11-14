#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/v3/LiveBuffer.h"
#include "synth/v3/Part.h"

/*
    Grains from a live sound (decision D139): a Grain slot set to Live reads the voice's recording of
    the generator before it.
*/
namespace
{
    using namespace coolsynth::synth::v3;
    using Type = coolsynth::synth::EngineMidiEventType;

    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }

    double frequency(const std::vector<float>& x, double rate, std::size_t from, std::size_t to)
    {
        int crossings = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                ++crossings;
        return crossings * rate / static_cast<double>(to - from);
    }

    template <typename Descriptor>
    void defaults(std::array<float, maxSlotParameters>& p, const Descriptor& d, std::initializer_list<std::pair<int, float>> units = {})
    {
        for (int i = 0; i < d.usedParams; ++i)
            p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
        for (const auto& [index, value] : units)
            p[static_cast<std::size_t>(index)] = unitsToNormalised(d.params[static_cast<std::size_t>(index)], value);
    }

    /** G1 a sine the mix does not hear; G2 a Grain cloud set to Live on it. */
    PartSettings liveCloud(float scan, float semitones = 0.0f)
    {
        PartSettings s;
        auto& source = s.voice.generators[0];
        source.type = generatorType::va;
        defaults(source.p, VaGenerator::descriptor(), { { VaGenerator::shape, 0.0f } });
        source.level = 0.0f; // heard only through the grains
        auto& cloud = s.voice.generators[1];
        cloud.type = generatorType::grain;
        defaults(cloud.p, GrainGenerator::descriptor(), { { GrainGenerator::position, 0.1f }, { GrainGenerator::scan, scan },
                                                          { GrainGenerator::size, 80.0f }, { GrainGenerator::density, 40.0f },
                                                          { GrainGenerator::spray, 0.0f }, { GrainGenerator::pitchSpray, 0.0f },
                                                          { GrainGenerator::spread, 0.0f } });
        cloud.level = 1.0f;
        cloud.combine = Combine::live;
        cloud.pitchSemitones = semitones;
        Envelope::Parameters held;
        held.attack = 0.002f;
        held.decay = 0.1f;
        held.sustain = 1.0f;
        held.release = 0.05f;
        s.voice.envelopes = { held, held, held };
        return s;
    }

    std::vector<float> render(Part& part, const PartSettings& s, int samples, std::vector<EngineMidiEvent> events = {})
    {
        std::vector<float> left(static_cast<std::size_t>(samples)), right(static_cast<std::size_t>(samples));
        for (int start = 0; start < samples; start += 256)
        {
            const int n = std::min(256, samples - start);
            const auto span = start == 0 ? std::span<const EngineMidiEvent>(events) : std::span<const EngineMidiEvent>();
            part.render(left.data() + start, right.data() + start, n, span, s, {});
        }
        return left;
    }

    EngineMidiEvent event(Type type, int note, float value)
    {
        EngineMidiEvent e;
        e.type = type;
        e.noteNumber = static_cast<uint8_t>(note);
        e.value = value;
        return e;
    }
}

class V3LiveGrainTests final : public juce::UnitTest
{
public:
    V3LiveGrainTests() : juce::UnitTest("V3 live grains", "CoolSynth") {}

    void runTest() override
    {
        beginTest("the_recording_keeps_the_audible_band_at_half_the_voice_rate_and_its_darker_levels_lose_the_top");
        {
            auto buffer = std::make_unique<LiveBuffer>();
            buffer->prepare(2.0 * kRate);
            expectEquals(buffer->frames(), static_cast<int>(kRate), "one second at the host rate");
            auto tone = [&](double hz)
            {
                buffer->clear();
                std::vector<float> x(static_cast<std::size_t>(2.0 * kRate * 0.5));
                for (std::size_t i = 0; i < x.size(); ++i)
                    x[i] = static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / (2.0 * kRate)));
                buffer->record(x.data(), static_cast<int>(x.size()));
                std::array<double, LiveBuffer::levels> levels {};
                for (int level = 0; level < LiveBuffer::levels; ++level)
                {
                    double sum = 0.0;
                    for (int f = 4000; f < 20000; ++f)
                        sum += static_cast<double>(buffer->read(level, f)) * buffer->read(level, f);
                    levels[static_cast<std::size_t>(level)] = std::sqrt(sum / 16000.0) * std::sqrt(2.0);
                }
                return levels;
            };
            const auto low = tone(1000.0);
            expectWithinAbsoluteError(low[0], 1.0, 0.01, "a 1 kHz tone is kept");
            expectWithinAbsoluteError(low[2], 1.0, 0.02, "also at the darkest level");
            expectEquals(buffer->writePosition(), static_cast<int>(0.5 * kRate), "half a second recorded");
            expectLessThan(tone(20000.0)[1], 0.1, "20 kHz: the middle level is 20 dB down");
            expectLessThan(tone(10000.0)[2], 0.1, "10 kHz: the darkest level is 20 dB down");
            expectLessThan(tone(30000.0)[0], 0.001, "above the host's band: gone, as from the voice's own output");
        }

        beginTest("live_grains_sound_the_generator_before_them_at_its_pitch_and_the_slots_transposition");
        {
            for (const auto& [semitones, hz] : { std::pair<float, double> { 0.0f, 440.0 }, { 12.0f, 880.0 } })
            {
                auto part = std::make_unique<Part>();
                part->prepare(kRate, 512, 3);
                const auto s = liveCloud(1.0f, semitones);
                const auto out = render(*part, s, 48000, { event(Type::noteOn, 69, 1.0f) });
                expectGreaterThan(rms(out, 24000, 48000), 0.02, "the cloud sounds");
                expectWithinAbsoluteError(frequency(out, kRate, 24000, 48000), hz, hz * 0.02, juce::String(semitones) + " semitones");
            }
        }

        beginTest("scan_at_0_freezes_the_cloud_while_the_source_moves_on");
        {
            auto pitchAfterBend = [&](bool frozen)
            {
                auto part = std::make_unique<Part>();
                part->prepare(kRate, 512, 3);
                render(*part, liveCloud(1.0f), 24000, { event(Type::noteOn, 69, 1.0f) }); // half a second recorded
                const auto s = liveCloud(frozen ? 0.0f : 1.0f);
                const auto out = render(*part, s, 48000, { event(Type::pitchBend, 0, 1.0f) }); // the source goes up a tone
                return frequency(out, kRate, 24000, 48000);
            };
            expectWithinAbsoluteError(pitchAfterBend(false), 440.0 * std::exp2(2.0 / 12.0), 10.0, "running: the cloud follows the source");
            expectWithinAbsoluteError(pitchAfterBend(true), 440.0, 9.0, "frozen: it keeps what it had");
        }
    }
};

static V3LiveGrainTests v3LiveGrainTests;
