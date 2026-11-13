#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "presets/BankFile.h"
#include "synth/v3/Inserts.h"
#include "synth/v3/RoomInsert.h"

namespace
{
    using namespace coolsynth::synth::v3;

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    /** A stereo signal through a room, block by block. `settingsAt` may change the settings as it goes. */
    template <typename SettingsAt>
    juce::AudioBuffer<float> through(RoomInsert& room, const juce::AudioBuffer<float>& input, SettingsAt&& settingsAt)
    {
        juce::AudioBuffer<float> out(2, input.getNumSamples());
        for (int start = 0; start < input.getNumSamples(); start += kBlock)
        {
            const int count = std::min(kBlock, input.getNumSamples() - start);
            juce::AudioBuffer<float> block(2, count);
            block.copyFrom(0, 0, input, 0, start, count);
            block.copyFrom(1, 0, input, 1, start, count);
            room.process(block, settingsAt(start));
            out.copyFrom(0, start, block, 0, 0, count);
            out.copyFrom(1, start, block, 1, 0, count);
        }
        return out;
    }

    juce::AudioBuffer<float> impulse(int length)
    {
        juce::AudioBuffer<float> b(2, length);
        b.clear();
        b.setSample(0, 0, 1.0f);
        b.setSample(1, 0, 1.0f);
        return b;
    }

    juce::AudioBuffer<float> noise(int length, float level = 0.25f)
    {
        juce::AudioBuffer<float> b(2, length);
        juce::Random random(20261113);
        for (int i = 0; i < length; ++i)
        {
            const float v = level * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample(0, i, v);
            b.setSample(1, i, v);
        }
        return b;
    }

    double energy(const juce::AudioBuffer<float>& b, int from, int to)
    {
        double sum = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = std::max(0, from); i < std::min(to, b.getNumSamples()); ++i)
                sum += static_cast<double>(b.getSample(channel, i)) * b.getSample(channel, i);
        return sum;
    }

    /** The decay time (60 dB) of an impulse response, from the fall of its remaining energy between -5 and -25 dB. */
    double decaySeconds(const juce::AudioBuffer<float>& response)
    {
        const int n = response.getNumSamples();
        std::vector<double> remaining(static_cast<std::size_t>(n) + 1, 0.0);
        for (int i = n - 1; i >= 0; --i)
            remaining[static_cast<std::size_t>(i)] = remaining[static_cast<std::size_t>(i) + 1]
                + static_cast<double>(response.getSample(0, i)) * response.getSample(0, i)
                + static_cast<double>(response.getSample(1, i)) * response.getSample(1, i);
        int at5 = -1, at25 = -1;
        for (int i = 0; i < n; ++i)
        {
            const double db = 10.0 * std::log10(remaining[static_cast<std::size_t>(i)] / remaining[0] + 1.0e-30);
            if (at5 < 0 && db <= -5.0)
                at5 = i;
            if (db <= -25.0)
            {
                at25 = i;
                break;
            }
        }
        return at5 >= 0 && at25 > at5 ? 3.0 * static_cast<double>(at25 - at5) / kRate : 0.0;
    }

    RoomInsert::Settings roomOf(float size, float damping, float width, float mix)
    {
        RoomInsert::Settings s;
        s.size = size;
        s.damping = damping;
        s.width = width;
        s.mix = mix;
        return s;
    }
}

class V3RoomTests final : public juce::UnitTest
{
public:
    V3RoomTests() : juce::UnitTest("V3 room insert", "V3") {}

    void runTest() override
    {
        beginTest("the_room_rings_for_the_time_its_size_asks");
        {
            for (const float size : { 0.0f, 0.4f, 0.7f, 1.0f })
            {
                auto room = std::make_unique<RoomInsert>();
                room->prepare(kRate);
                const auto wanted = RoomInsert::reverbSeconds(size);
                const auto response = through(*room, impulse(static_cast<int>(kRate * std::max(0.5, 2.0 * wanted))),
                                              [&](int) { return roomOf(size, 0.5f, 1.0f, 1.0f); });
                const double measured = decaySeconds(response);
                logMessage("size " + juce::String(size, 2) + ": asked " + juce::String(wanted, 3) + " s, measured " + juce::String(measured, 3) + " s");
                expectGreaterThan(measured, 0.7 * wanted);
                expectLessThan(measured, 1.2 * wanted);
            }
            expectWithinAbsoluteError(RoomInsert::reverbSeconds(0.0f), 0.1, 1.0e-9);
            expectWithinAbsoluteError(RoomInsert::reverbSeconds(1.0f), 1.2, 1.0e-9);
        }

        beginTest("its_reflections_come_at_once_and_are_its_loudest_part");
        {
            // A room of a quarter second: the first reflection within 10 ms, and more energy in the first
            // 40 ms than in all that follows: what a plate, which builds up, does not do.
            auto room = std::make_unique<RoomInsert>();
            room->prepare(kRate);
            const auto response = through(*room, impulse(48000), [](int) { return roomOf(0.37f, 0.5f, 1.0f, 1.0f); });
            int first = -1;
            for (int i = 1; i < response.getNumSamples() && first < 0; ++i)
                if (std::abs(response.getSample(0, i)) > 0.02f || std::abs(response.getSample(1, i)) > 0.02f)
                    first = i;
            expect(first > 0 && first < 480, "first reflection at sample " + juce::String(first));
            expectGreaterThan(energy(response, 0, 1920), energy(response, 1920, 48000));
            expectLessThan(energy(response, 24000, 48000), 1.0e-6 * energy(response, 0, 48000)); // half a second on, the room is silent
        }

        beginTest("the_room_is_as_loud_at_every_size_and_damping");
        {
            const auto input = noise(96000);
            const double inputEnergy = energy(input, 48000, 96000);
            for (const float size : { 0.0f, 0.3f, 0.6f, 1.0f })
                for (const float damping : { 0.0f, 0.5f, 1.0f })
                {
                    auto room = std::make_unique<RoomInsert>();
                    room->prepare(kRate);
                    const auto out = through(*room, input, [&](int) { return roomOf(size, damping, 1.0f, 1.0f); });
                    const double db = 10.0 * std::log10(energy(out, 48000, 96000) / inputEnergy);
                    expectWithinAbsoluteError(db, 0.0, 2.0, "size " + juce::String(size) + " damping " + juce::String(damping));
                }
        }

        beginTest("mix_and_width_do_what_they_say");
        {
            const auto input = noise(24000);

            auto dry = std::make_unique<RoomInsert>();
            dry->prepare(kRate);
            const auto untouched = through(*dry, input, [](int) { return roomOf(0.4f, 0.5f, 1.0f, 0.0f); });
            float worst = 0.0f;
            for (int i = 0; i < input.getNumSamples(); ++i)
                worst = std::max(worst, std::abs(untouched.getSample(0, i) - input.getSample(0, i)));
            expectLessThan(worst, 1.0e-6f, "at Mix 0 the part passes untouched");

            auto correlationAt = [&](float width)
            {
                auto room = std::make_unique<RoomInsert>();
                room->prepare(kRate);
                const auto out = through(*room, input, [&](int) { return roomOf(0.4f, 0.5f, width, 1.0f); });
                double ll = 0.0, rr = 0.0, lr = 0.0;
                for (int i = 4800; i < out.getNumSamples(); ++i)
                {
                    ll += static_cast<double>(out.getSample(0, i)) * out.getSample(0, i);
                    rr += static_cast<double>(out.getSample(1, i)) * out.getSample(1, i);
                    lr += static_cast<double>(out.getSample(0, i)) * out.getSample(1, i);
                }
                return lr / std::sqrt(ll * rr);
            };
            expectGreaterThan(correlationAt(0.0f), 0.999, "at Width 0 both channels carry the same room");
            expectLessThan(correlationAt(1.0f), 0.6, "at Width 1 the two microphones hear different reflections");
        }

        beginTest("a_change_of_size_rebuilds_the_room_without_a_click");
        {
            // A 220 Hz tone, the room alone; a second in, Size jumps. No step between two samples is larger than
            // the tone itself makes, the level dips and comes back, and the room is the new size afterwards.
            juce::AudioBuffer<float> tone(2, 144000);
            for (int i = 0; i < tone.getNumSamples(); ++i)
            {
                const float v = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * static_cast<float>(i) / static_cast<float>(kRate));
                tone.setSample(0, i, v);
                tone.setSample(1, i, v);
            }
            auto room = std::make_unique<RoomInsert>();
            room->prepare(kRate);
            const auto out = through(*room, tone, [](int start) { return roomOf(start < 48000 ? 0.2f : 0.8f, 0.5f, 1.0f, 1.0f); });
            float largestStep = 0.0f, largestBefore = 0.0f, peak = 0.0f;
            for (int i = 24000; i < out.getNumSamples(); ++i)
            {
                const float step = std::abs(out.getSample(0, i) - out.getSample(0, i - 1));
                (i < 47000 ? largestBefore : largestStep) = std::max(i < 47000 ? largestBefore : largestStep, step);
                peak = std::max(peak, std::abs(out.getSample(0, i)));
            }
            expectLessThan(largestStep, 2.5f * largestBefore + 0.01f);
            expectLessThan(peak, 2.0f);
            expectGreaterThan(energy(out, 120000, 144000), 0.05 * energy(tone, 120000, 144000));
            for (int i = 0; i < out.getNumSamples(); ++i)
                expect(std::isfinite(out.getSample(0, i)) && std::isfinite(out.getSample(1, i)));
        }

        beginTest("the_room_stays_in_bounds_and_falls_silent");
        {
            // Full-scale noise for a second at the largest size, then nothing: bounded, and silent three decay times on.
            juce::AudioBuffer<float> input(2, 48000 * 6);
            input.clear();
            juce::Random random(7);
            for (int i = 0; i < 48000; ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                input.setSample(0, i, v);
                input.setSample(1, i, -v * 0.5f);
            }
            auto room = std::make_unique<RoomInsert>();
            room->prepare(kRate);
            const auto out = through(*room, input, [](int) { return roomOf(1.0f, 0.0f, 1.0f, 1.0f); });
            float peak = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i)
                peak = std::max(peak, std::max(std::abs(out.getSample(0, i)), std::abs(out.getSample(1, i))));
            expectLessThan(peak, 4.0f);
            expectLessThan(energy(out, 48000 * 5, 48000 * 6), 1.0e-8);
        }

        beginTest("room_is_the_thirteenth_insert_type_and_a_bank_line_sets_it");
        {
            expectEquals(static_cast<int>(insertType::room), 12); // saved in patches: it stays the thirteenth
            expectEquals(juce::String(insertDescriptor(insertType::room).name), juce::String("Room"));
            expectEquals(insertDescriptor(insertType::room).usedParams, 4);

            const auto parsed = coolsynth::presets::parseBankFile(R"([In A Room]
room = Greenhouse
g1 = VA shape=saw
i1 = Room size=0.3 damping=0.6 width=0.8 mix=0.25
)", "room.csbank");
            expect(parsed.errors.isEmpty(), parsed.errors.joinIntoString("\n"));
            expectEquals(static_cast<int>(parsed.presets.size()), 1);
            bool typed = false, sized = false;
            for (const auto& entry : parsed.presets.front().entries)
            {
                typed = typed || (entry.parameterId == "pA.i1.type" && entry.text == "Room");
                sized = sized || (entry.parameterId == "pA.i1.p1" && entry.text == "0.3");
            }
            expect(typed, "the slot's type is Room");
            expect(sized, "its first value is the size");

            // In a slot it runs: a slot switched to Room and fed a click answers with reflections.
            auto slot = std::make_unique<InsertSlot>();
            slot->prepare(kRate, kBlock);
            InsertSettings settings;
            settings.type = insertType::room;
            const auto& d = insertDescriptor(insertType::room);
            for (int i = 0; i < d.usedParams; ++i)
                settings.p[static_cast<std::size_t>(i)] = unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            settings.p[3] = 1.0f;
            double answered = 0.0;
            for (int block = 0; block < 20; ++block)
            {
                juce::AudioBuffer<float> buffer(2, kBlock);
                buffer.clear();
                if (block == 0)
                {
                    buffer.setSample(0, 0, 1.0f);
                    buffer.setSample(1, 0, 1.0f);
                }
                slot->process(buffer, settings, InsertContext {});
                answered += energy(buffer, 0, kBlock);
            }
            expectGreaterThan(answered, 0.01);
        }
    }
};

static V3RoomTests v3RoomTests;
