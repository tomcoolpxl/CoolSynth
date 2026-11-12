#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "synth/v3/Module.h"

namespace
{
    using namespace coolsynth::synth::v3;

    // Outputs p[0] on both channels; takes a small table from the arena.
    struct ConstantGenerator
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Constant", 1,
                                              {{ { "value", "Value", -1.0f, 1.0f, 0.5f } }}, 1 };
            return d;
        }
        static std::size_t memoryFloats(double, int) { return 16; }

        void prepare(double, int, SlotMemory& memory) noexcept { table = memory.take(16); }
        void reset() noexcept { ++resets; }
        void noteOn(const NoteContext& note) noexcept { lastNote = note.noteNumber; ++noteOns; }
        void process(float* left, float* right, int n, const SlotParams& p) noexcept
        {
            std::fill_n(left, n, p.p[0]);
            std::fill_n(right, n, p.p[0]);
        }

        std::span<float> table;
        int lastNote = -1;
        int noteOns = 0;
        int resets = 0;
    };

    // Outputs -p[0]; wants a much larger arena so the slot must size for it.
    struct NegatedGenerator
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Negated", 2,
                                              {{ { "value", "Value", -1.0f, 1.0f, 0.5f } }}, 1 };
            return d;
        }
        static std::size_t memoryFloats(double sampleRate, int) { return static_cast<std::size_t>(sampleRate / 20.0); }

        void prepare(double sampleRate, int maxSubBlock, SlotMemory& memory) noexcept { line = memory.take(memoryFloats(sampleRate, maxSubBlock)); }
        void reset() noexcept {}
        void noteOn(const NoteContext& note) noexcept { lastNote = note.noteNumber; ++noteOns; }
        void process(float* left, float* right, int n, const SlotParams& p) noexcept
        {
            std::fill_n(left, n, -p.p[0]);
            std::fill_n(right, n, -p.p[0]);
        }

        std::span<float> line;
        int lastNote = -1;
        int noteOns = 0;
    };

    // Multiplies the input by p[0]; no memory.
    struct GainProcessor
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Gain", 0,
                                              {{ { "gain", "Gain", 0.0f, 1.0f, 1.0f } }}, 1 };
            return d;
        }
        static std::size_t memoryFloats(double, int) { return 0; }

        void prepare(double, int, SlotMemory&) noexcept {}
        void reset() noexcept {}
        void noteOn(const NoteContext&) noexcept {}
        void process(float* left, float* right, int n, const SlotParams& p) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                left[i] *= p.p[0];
                right[i] *= p.p[0];
            }
        }
    };

    // Passes the input through unchanged, but reports the first input sample it saw after a note.
    struct PassProcessor
    {
        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d { "Pass", 0, {}, 0 };
            return d;
        }
        static std::size_t memoryFloats(double, int) { return 64; }

        void prepare(double, int, SlotMemory& memory) noexcept { scratch = memory.take(64); }
        void reset() noexcept {}
        void noteOn(const NoteContext&) noexcept {}
        void process(float* left, float*, int n, const SlotParams&) noexcept
        {
            if (n > 0 && ! sawInput)
            {
                firstInput = left[0];
                sawInput = true;
            }
        }

        std::span<float> scratch;
        float firstInput = 0.0f;
        bool sawInput = false;
    };

    using GeneratorSlot = Slot<std::variant<ConstantGenerator, NegatedGenerator>>;
    using ProcessorSlot = Slot<std::variant<GainProcessor, PassProcessor>>;

    constexpr double kSampleRate = 48000.0;
    constexpr int kSubBlock = 32;

    struct Capture
    {
        std::vector<float> samples;

        float maxStep() const
        {
            float step = 0.0f;
            for (std::size_t i = 1; i < samples.size(); ++i)
                step = std::max(step, std::abs(samples[i] - samples[i - 1]));
            return step;
        }
    };

    template <typename SlotType>
    void renderBlocks(SlotType& slot, int blocks, const SlotParams& params, Capture& capture, float inputValue = 0.0f)
    {
        std::array<float, kSubBlock> left {}, right {};
        for (int b = 0; b < blocks; ++b)
        {
            left.fill(inputValue);
            right.fill(inputValue);
            slot.process(left.data(), right.data(), kSubBlock, params);
            capture.samples.insert(capture.samples.end(), left.begin(), left.end());
        }
    }
}

class V3ModuleTests final : public juce::UnitTest
{
public:
    V3ModuleTests() : juce::UnitTest("V3 module slots", "V3") {}

    void runTest() override
    {
        beginTest("descriptor_table_and_arena_sizing");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            expectEquals(GeneratorSlot::typeCount, 2);
            expect(juce::String(GeneratorSlot::descriptorFor(0).name) == "Constant");
            expect(juce::String(GeneratorSlot::descriptorFor(1).name) == "Negated");
            expectEquals(GeneratorSlot::descriptorFor(1).cost, 2);

            slot->prepare(kSampleRate, kSubBlock);
            expectEquals(slot->typeIndex(), 0);
            expectEquals(static_cast<int>(slot->memoryCapacityForTesting()), static_cast<int>(kSampleRate / 20.0));
            expectEquals(static_cast<int>(slot->memoryUsedForTesting()), 16);
            expectEquals(slot->crossfadeSamples(), 240);
            expect(slot->activeAs<ConstantGenerator>() != nullptr);
            expect(slot->activeAs<ConstantGenerator>()->table.size() == 16);
        }

        beginTest("slot_memory_hands_out_distinct_zeroed_spans");
        {
            SlotMemory memory;
            memory.allocate(24);
            auto a = memory.take(16);
            auto b = memory.take(8);
            expect(a.size() == 16 && b.size() == 8);
            expect(a.data() + 16 == b.data());
            a[0] = 1.0f;
            memory.rewind();
            auto c = memory.take(16);
            expectEquals(c[0], 0.0f);
            expectEquals(static_cast<int>(memory.usedFloats()), 16);
        }

        beginTest("generator_type_change_crossfades_without_a_step");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->prepare(kSampleRate, kSubBlock);
            SlotParams params;
            params.p[0] = 0.5f;

            Capture capture;
            renderBlocks(*slot, 4, params, capture);
            expectEquals(capture.samples.back(), 0.5f);

            slot->requestType(1);
            renderBlocks(*slot, 1, params, capture);
            expect(slot->isCrossfading());
            expectEquals(slot->typeIndex(), 1);

            renderBlocks(*slot, 8, params, capture);
            expect(! slot->isCrossfading());
            expectEquals(capture.samples.back(), -0.5f);

            const float allowedStep = 1.0f / static_cast<float>(slot->crossfadeSamples()) + 1.0e-5f;
            const float measured = capture.maxStep();
            logMessage("max step across the type change: " + juce::String(measured, 6)
                       + " (allowed " + juce::String(allowedStep, 6) + ")");
            expect(measured <= allowedStep);
        }

        beginTest("processor_fade_feeds_the_old_module_the_same_input");
        {
            auto slot = std::make_unique<ProcessorSlot>();
            slot->requestType(1); // start on Pass
            slot->prepare(kSampleRate, kSubBlock);
            SlotParams params;
            params.p[0] = 0.0f; // gain for the incoming Gain type

            Capture capture;
            renderBlocks(*slot, 2, params, capture, 1.0f);
            expectEquals(capture.samples.back(), 1.0f);
            expect(slot->activeAs<PassProcessor>() != nullptr);
            expectEquals(slot->activeAs<PassProcessor>()->firstInput, 1.0f);

            slot->requestType(0);
            renderBlocks(*slot, 9, params, capture, 1.0f);
            expect(! slot->isCrossfading());
            expectEquals(capture.samples.back(), 0.0f);

            // Without the input copy the fading Pass module would have processed Gain's zeros and
            // the output would have dropped to zero at once instead of ramping over the fade.
            const auto firstFaded = capture.samples[static_cast<std::size_t>(2 * kSubBlock)];
            expect(firstFaded > 0.99f);
            expect(capture.maxStep() <= 1.0f / static_cast<float>(slot->crossfadeSamples()) + 1.0e-5f);
        }

        beginTest("same_type_request_is_a_no_op");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->prepare(kSampleRate, kSubBlock);
            SlotParams params;
            params.p[0] = 0.5f;
            slot->requestType(0);
            Capture capture;
            renderBlocks(*slot, 1, params, capture);
            expect(! slot->isCrossfading());
            expectEquals(capture.samples.front(), 0.5f);
        }

        beginTest("request_during_a_fade_waits_for_the_fade");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->prepare(kSampleRate, kSubBlock);
            SlotParams params;
            params.p[0] = 0.5f;

            Capture capture;
            slot->requestType(1);
            renderBlocks(*slot, 1, params, capture);
            slot->requestType(0);
            renderBlocks(*slot, 1, params, capture);
            expectEquals(slot->typeIndex(), 1); // still fading to Negated; the request is queued

            renderBlocks(*slot, 20, params, capture);
            expectEquals(slot->typeIndex(), 0);
            expect(! slot->isCrossfading());
            expectEquals(capture.samples.back(), 0.5f);
            expect(capture.maxStep() <= 1.0f / static_cast<float>(slot->crossfadeSamples()) + 1.0e-5f);
        }

        beginTest("new_module_receives_the_held_note_and_old_module_is_reset_after_the_fade");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->prepare(kSampleRate, kSubBlock);
            NoteContext note;
            note.noteNumber = 64;
            slot->noteOn(note);

            SlotParams params;
            params.p[0] = 0.5f;
            Capture capture;
            slot->requestType(1);
            renderBlocks(*slot, 10, params, capture);

            const auto* negated = slot->activeAs<NegatedGenerator>();
            expect(negated != nullptr);
            expectEquals(negated->noteOns, 1);
            expectEquals(negated->lastNote, 64);
            expect(negated->line.size() == static_cast<std::size_t>(kSampleRate / 20.0));

            slot->requestType(0);
            renderBlocks(*slot, 10, params, capture);
            const auto* constant = slot->activeAs<ConstantGenerator>();
            expect(constant != nullptr);
            expectEquals(constant->lastNote, 64);
        }

        beginTest("prepare_applies_a_pending_type_without_a_fade");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->requestType(1);
            slot->prepare(kSampleRate, kSubBlock);
            expectEquals(slot->typeIndex(), 1);
            expect(! slot->isCrossfading());

            SlotParams params;
            params.p[0] = 0.5f;
            Capture capture;
            renderBlocks(*slot, 1, params, capture);
            expectEquals(capture.samples.front(), -0.5f);
        }

        beginTest("parameters_are_clamped_to_the_descriptor_range");
        {
            auto slot = std::make_unique<GeneratorSlot>();
            slot->prepare(kSampleRate, kSubBlock);
            SlotParams params;
            params.p[0] = 7.0f;
            Capture capture;
            renderBlocks(*slot, 1, params, capture);
            expectEquals(capture.samples.front(), 1.0f);
        }
    }
};

static V3ModuleTests v3ModuleTests;
