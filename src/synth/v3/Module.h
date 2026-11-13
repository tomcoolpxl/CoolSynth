#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <juce_core/juce_core.h>

/*
    V3 module model (DESIGN_V3.md section 3).

    A module type is a plain class with four operations and two static descriptors. A Slot holds
    one module of its kind at a time inside a std::variant and switches type with a short
    crossfade, without allocating on the audio thread. Module constructors and prepare() must
    not touch the heap: buffers come from the SlotMemory arena the Slot sized in its own prepare().
*/
namespace coolsynth::synth::v3
{
    inline constexpr int maxSlotParameters = 8;

    enum class Skew { linear, logarithmic, squared };
    enum class Unit { generic, hertz, milliseconds, decibels, semitones, cents, percent, choice };

    struct ParameterDescriptor
    {
        const char* id = "";     // preset YAML key, e.g. "cutoff"
        const char* name = "";   // display name
        float min = 0.0f;
        float max = 1.0f;
        float def = 0.0f;
        Skew skew = Skew::linear;
        Unit unit = Unit::generic;
        bool modulatable = true;
    };

    struct ModuleDescriptor
    {
        const char* name = "";
        int cost = 0;            // cost class, summed per voice to cap polyphony
        std::array<ParameterDescriptor, maxSlotParameters> params {};
        int usedParams = 0;
    };

    struct NoteContext
    {
        int noteNumber = 60;
        float velocity = 1.0f;
        int glideFromNote = -1;
        int unisonIndex = 0;
        int unisonCount = 1;
        bool fromArp = false;
    };

    /** How a generator combines with the previous generator slot. */
    enum class Combine : int { off = 0, sync, ring, fm, am };

    /**
        What a generator hands the next one for its combine mode, in its own output time.
        The pointers cover the samples of its last process() call.
    */
    struct GeneratorLink
    {
        const float* signal = nullptr; // mono, unit level, after its own combine, before level and pan; null is silence
        const float* wraps = nullptr;  // per sample: tau in (0, 1] when its phase wrapped since the previous sample, else -1; null without a phase
    };

    /** Parameter values for one sub-block, in descriptor units with modulation applied. */
    struct SlotParams
    {
        std::array<float, maxSlotParameters> p {};
        float level = 1.0f;          // generators: applied by the voice, not by the module
        float pan = 0.0f;            // generators, -1..1
        float pitchSemitones = 0.0f; // generators: octave, semitone, fine and modulation
        float frequencyHz = 440.0f;  // set by the voice from the note, glide, bend and pitchSemitones
        Combine combine = Combine::off;
        float combineAmount = 0.0f;  // 0..1: FM depth, or the wet share of ring and AM
    };

    /** Arena of floats, sized once off the audio thread; modules take spans from it in prepare(). */
    class SlotMemory final
    {
    public:
        void allocate(std::size_t floats)
        {
            storage.assign(floats, 0.0f);
            used = 0;
        }

        void rewind() noexcept { used = 0; }

        /** Returns a zeroed span, or an empty span (and asserts) when the arena is too small. */
        [[nodiscard]] std::span<float> take(std::size_t floats) noexcept
        {
            if (used + floats > storage.size())
            {
                jassertfalse; // memoryFloats() of the module type under-reported its need
                return {};
            }

            std::span<float> span(storage.data() + used, floats);
            used += floats;
            std::fill(span.begin(), span.end(), 0.0f);
            return span;
        }

        [[nodiscard]] std::size_t capacity() const noexcept { return storage.size(); }
        [[nodiscard]] std::size_t usedFloats() const noexcept { return used; }

    private:
        std::vector<float> storage;
        std::size_t used = 0;
    };

    template <typename T>
    concept Module = requires(T module,
                              double sampleRate,
                              int numSamples,
                              SlotMemory& memory,
                              const NoteContext& note,
                              float* samples,
                              const SlotParams& params)
    {
        { T::descriptor() } -> std::convertible_to<const ModuleDescriptor&>;
        { T::memoryFloats(sampleRate, numSamples) } -> std::convertible_to<std::size_t>;
        module.prepare(sampleRate, numSamples, memory);
        module.reset();
        module.noteOn(note);
        module.process(samples, samples, numSamples, params);
    };

    /** The module types a slot kind accepts, as the parameter layer sees them. */
    struct SlotTypeTable
    {
        int count = 0;
        const ModuleDescriptor& (*descriptor)(int typeIndex) = nullptr;
    };

    namespace detail
    {
        template <typename Variant>
        struct VariantTypes;

        template <typename... Ts>
        struct VariantTypes<std::variant<Ts...>>
        {
            static_assert((Module<Ts> && ...), "every slot type must satisfy the Module concept");

            static constexpr int count = static_cast<int>(sizeof...(Ts));

            static std::size_t maxMemoryFloats(double sampleRate, int maxSubBlock)
            {
                return std::max({ std::size_t { 0 }, Ts::memoryFloats(sampleRate, maxSubBlock)... });
            }

            static const ModuleDescriptor& descriptor(int index)
            {
                static const std::array<const ModuleDescriptor*, sizeof...(Ts)> table { &Ts::descriptor()... };
                return *table[static_cast<std::size_t>(index)];
            }

            static void emplace(std::variant<Ts...>& variant, int index) noexcept
            {
                emplaceImpl(variant, index, std::index_sequence_for<Ts...> {});
            }

        private:
            template <std::size_t... I>
            static void emplaceImpl(std::variant<Ts...>& variant, int index, std::index_sequence<I...>) noexcept
            {
                ((static_cast<int>(I) == index ? static_cast<void>(variant.template emplace<I>()) : void()), ...);
            }
        };
    }

    /**
        One slot of a voice or an effect chain. Variant lists the module types of the slot's kind.

        Type changes are requested from any thread and applied at the start of the next process()
        call: the new module is constructed into the inactive instance, prepared from that
        instance's arena, given the last note, and crossfaded in over crossfadeSeconds while the
        old module keeps running on a copy of the input. A request that arrives during a fade
        waits for the fade to finish.
    */
    template <typename Variant>
    class Slot final
    {
        using Types = detail::VariantTypes<Variant>;

    public:
        static constexpr int typeCount = Types::count;
        static constexpr double crossfadeSeconds = 0.005;

        static const ModuleDescriptor& descriptorFor(int typeIndex) { return Types::descriptor(typeIndex); }
        static SlotTypeTable typeTable() noexcept { return { typeCount, &Types::descriptor }; }

        /** Message thread. Sizes both arenas and the scratch, applies a pending type at once, prepares the active module. */
        void prepare(double newSampleRate, int newMaxSubBlock)
        {
            sampleRate = newSampleRate;
            maxSubBlock = std::max(1, newMaxSubBlock);

            const auto floats = Types::maxMemoryFloats(sampleRate, maxSubBlock);
            for (auto& instance : instances)
                instance.memory.allocate(floats);

            scratchLeft.assign(static_cast<std::size_t>(maxSubBlock), 0.0f);
            scratchRight.assign(static_cast<std::size_t>(maxSubBlock), 0.0f);

            fadeTotal = std::max(1, static_cast<int>(std::lround(crossfadeSeconds * sampleRate)));
            fadeRemaining = 0;

            if (const int pending = pendingType.exchange(-1); pending >= 0 && pending < typeCount)
                Types::emplace(instances[active].module, pending);

            prepareInstance(instances[active]);
            prepared = true;
        }

        void reset() noexcept
        {
            fadeRemaining = 0;
            std::visit([](auto& module) { module.reset(); }, instances[active].module);
        }

        /** Any thread. Takes effect at the next process() with a crossfade, or at prepare() without one. */
        void requestType(int typeIndex) noexcept
        {
            jassert(typeIndex >= 0 && typeIndex < typeCount);
            pendingType.store(typeIndex);
        }

        /** Audio thread, for a silent voice about to start a note: applies a waiting type at once, without a crossfade. */
        void switchTypeNow() noexcept
        {
            const int pending = pendingType.exchange(-1);
            fadeRemaining = 0;
            if (pending < 0 || pending >= typeCount || pending == typeIndex())
                return;

            Types::emplace(instances[active].module, pending);
            prepareInstance(instances[active]);
            if (noteStarted)
                std::visit([&](auto& module) { module.noteOn(lastNote); }, instances[active].module);
        }

        /** Generator slots: the previous generator's link, handed to every module this slot runs. */
        void setLink(const GeneratorLink& link) noexcept { inputLink = link; }

        /** Generator slots: what the active module hands the next generator, valid until the next process(). */
        [[nodiscard]] GeneratorLink link() const noexcept
        {
            return std::visit([](const auto& module) -> GeneratorLink
            {
                if constexpr (requires { module.link(); })
                    return module.link();
                else
                    return {};
            }, instances[active].module);
        }

        [[nodiscard]] int typeIndex() const noexcept { return static_cast<int>(instances[active].module.index()); }
        [[nodiscard]] const ModuleDescriptor& descriptor() const { return Types::descriptor(typeIndex()); }
        [[nodiscard]] bool isCrossfading() const noexcept { return fadeRemaining > 0; }
        [[nodiscard]] int crossfadeSamples() const noexcept { return fadeTotal; }

        void noteOn(const NoteContext& note) noexcept
        {
            lastNote = note;
            noteStarted = true;
            std::visit([&](auto& module) { module.noteOn(note); }, instances[active].module);
        }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            jassert(prepared && numSamples <= maxSubBlock);
            applyPendingType();

            if (fadeRemaining <= 0)
            {
                run(instances[active], left, right, numSamples, params);
                return;
            }

            // The fading module sees the same input as the new one; generators simply overwrite it.
            std::copy_n(left, numSamples, scratchLeft.data());
            std::copy_n(right, numSamples, scratchRight.data());

            run(instances[active], left, right, numSamples, params);
            run(instances[1 - active], scratchLeft.data(), scratchRight.data(), numSamples, params);

            const float fadeScale = 1.0f / static_cast<float>(fadeTotal);
            for (int i = 0; i < numSamples; ++i)
            {
                if (fadeRemaining > 0)
                    --fadeRemaining;

                const float gainIn = 1.0f - static_cast<float>(fadeRemaining) * fadeScale;
                const float gainOut = 1.0f - gainIn;
                left[i] = left[i] * gainIn + scratchLeft[static_cast<std::size_t>(i)] * gainOut;
                right[i] = right[i] * gainIn + scratchRight[static_cast<std::size_t>(i)] * gainOut;
            }

            if (fadeRemaining == 0)
                std::visit([](auto& module) { module.reset(); }, instances[1 - active].module);
        }

        /** Testing: the active module if it is of type T. */
        template <typename T>
        [[nodiscard]] const T* activeAs() const noexcept
        {
            return std::get_if<T>(&instances[active].module);
        }

        [[nodiscard]] std::size_t memoryCapacityForTesting() const noexcept { return instances[active].memory.capacity(); }
        [[nodiscard]] std::size_t memoryUsedForTesting() const noexcept { return instances[active].memory.usedFloats(); }

    private:
        struct Instance
        {
            Variant module;
            SlotMemory memory;
        };

        void prepareInstance(Instance& instance) noexcept
        {
            instance.memory.rewind();
            std::visit([&](auto& module) { module.prepare(sampleRate, maxSubBlock, instance.memory); }, instance.module);
        }

        void applyPendingType() noexcept
        {
            int pending = pendingType.exchange(-1);
            if (pending < 0)
                return;

            if (pending == typeIndex())
                return;

            if (fadeRemaining > 0)
            {
                // Wait for the running fade; keep the request unless a newer one already replaced it.
                int expected = -1;
                pendingType.compare_exchange_strong(expected, pending);
                return;
            }

            const int next = 1 - active;
            auto& instance = instances[next];
            Types::emplace(instance.module, pending);
            prepareInstance(instance);

            if (noteStarted)
                std::visit([&](auto& module) { module.noteOn(lastNote); }, instance.module);

            active = next;
            fadeRemaining = fadeTotal;
        }

        void run(Instance& instance, float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            // A module only ever sees values inside its own descriptor ranges, also while it fades out.
            const auto& descriptor = Types::descriptor(static_cast<int>(instance.module.index()));
            SlotParams clamped = params;
            for (int i = 0; i < descriptor.usedParams; ++i)
            {
                const auto& range = descriptor.params[static_cast<std::size_t>(i)];
                clamped.p[static_cast<std::size_t>(i)] = std::clamp(params.p[static_cast<std::size_t>(i)], range.min, range.max);
            }

            std::visit([&](auto& module)
            {
                if constexpr (requires { module.setLink(inputLink); })
                    module.setLink(inputLink);
                module.process(left, right, numSamples, clamped);
            }, instance.module);
        }

        std::array<Instance, 2> instances {};
        int active = 0;
        std::atomic<int> pendingType { -1 };
        std::vector<float> scratchLeft, scratchRight;
        double sampleRate = 48000.0;
        int maxSubBlock = 0;
        int fadeTotal = 1;
        int fadeRemaining = 0;
        NoteContext lastNote {};
        GeneratorLink inputLink {};
        bool noteStarted = false;
        bool prepared = false;
    };
}
