#include <juce_core/juce_core.h>

#include <cmath>

#include "synth/v3/Modulation.h"

namespace
{
    using namespace coolsynth::synth::v3;

    const ModuleDescriptor& twoParamDescriptor()
    {
        static const ModuleDescriptor d { "Two", 1,
                                          {{ { "cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f, Skew::logarithmic, Unit::hertz, true },
                                             { "amount", "Amount", -1.0f, 1.0f, 0.0f, Skew::linear, Unit::generic, false } }},
                                          2 };
        return d;
    }

    constexpr double kOversampledRate = 96000.0;
    constexpr int kSubBlock = 32;
}

class V3ModulationTests final : public juce::UnitTest
{
public:
    V3ModulationTests() : juce::UnitTest("V3 modulation context", "V3") {}

    void runTest() override
    {
        beginTest("descriptor_mapping_linear_and_logarithmic_round_trip");
        {
            const auto& d = twoParamDescriptor();
            const auto& logParam = d.params[0];
            const auto& linParam = d.params[1];

            expectWithinAbsoluteError(descriptorToUnits(logParam, 0.0f), 20.0f, 1.0e-3f);
            expectWithinAbsoluteError(descriptorToUnits(logParam, 1.0f), 20000.0f, 1.0e-1f);
            expectWithinAbsoluteError(descriptorToUnits(logParam, 0.5f), std::sqrt(20.0f * 20000.0f), 1.0e-1f);
            expectWithinAbsoluteError(descriptorToUnits(linParam, 0.75f), 0.5f, 1.0e-6f);

            for (float t = 0.0f; t <= 1.0f; t += 0.125f)
            {
                expectWithinAbsoluteError(unitsToNormalised(logParam, descriptorToUnits(logParam, t)), t, 1.0e-5f);
                expectWithinAbsoluteError(unitsToNormalised(linParam, descriptorToUnits(linParam, t)), t, 1.0e-6f);
            }

            const ParameterDescriptor squaredParam { "depth", "Depth", 0.0f, 200.0f, 20.0f, Skew::squared, Unit::cents, true };
            expectWithinAbsoluteError(descriptorToUnits(squaredParam, 0.5f), 50.0f, 1.0e-4f);
            for (float t = 0.0f; t <= 1.0f; t += 0.125f)
                expectWithinAbsoluteError(unitsToNormalised(squaredParam, descriptorToUnits(squaredParam, t)), t, 1.0e-5f);

            expectWithinAbsoluteError(descriptorToUnits(logParam, 2.0f), 20000.0f, 1.0e-1f);
            expectWithinAbsoluteError(descriptorToUnits(linParam, -3.0f), -1.0f, 1.0e-6f);
        }

        beginTest("slot_base_smooths_linearly_and_lands_on_the_target");
        {
            SlotBase base;
            base.prepare(kOversampledRate, kSubBlock, 0.010); // one thirtieth per sub-block
            const float expectedStep = static_cast<float>(kSubBlock) / static_cast<float>(kOversampledRate * 0.010);
            expectWithinAbsoluteError(expectedStep, 1.0f / 30.0f, 1.0e-6f);

            base.setTarget(0, 1.0f);
            expect(base.isSmoothing());

            float previous = base.value(0);
            int steps = 0;
            while (base.isSmoothing() && steps < 100)
            {
                base.step();
                ++steps;
                const float now = base.value(0);
                expect(now >= previous);
                expect(now - previous <= expectedStep + 1.0e-6f);
                previous = now;
            }
            expect(steps == 30 || steps == 31); // float accumulation may need one landing step
            expectEquals(base.value(0), 1.0f);
            expect(! base.isSmoothing());

            // Reversing mid-way turns around without a jump.
            base.setTarget(0, 0.0f);
            base.step();
            expectWithinAbsoluteError(base.value(0), 1.0f - expectedStep, 1.0e-6f);
            base.snapToTargets();
            expectEquals(base.value(0), 0.0f);
        }

        beginTest("slot_base_scales_steps_for_pan_and_pitch_and_clamps_targets");
        {
            SlotBase base;
            base.prepare(kOversampledRate, kSubBlock, 0.010);
            base.setPanTarget(5.0f);
            base.setPitchTarget(-500.0f);
            base.setLevelTarget(3.0f);
            base.step();
            expectWithinAbsoluteError(base.panValue(), 2.0f / 30.0f, 1.0e-6f);
            expectWithinAbsoluteError(base.pitchValue(), -SlotBase::pitchSpanSemitones / 30.0f, 1.0e-4f);
            expectEquals(base.levelValue(), 1.0f); // target clamps to 1 and the level starts at 1
            for (int i = 0; i < 40; ++i)
                base.step();
            expectEquals(base.panValue(), 1.0f);
            expectEquals(base.pitchValue(), -SlotBase::pitchSpanSemitones);
        }

        beginTest("slot_base_prepare_snaps_and_wide_sub_blocks_step_at_once");
        {
            SlotBase base;
            base.setTarget(2, 0.4f);
            base.prepare(48000.0, 4800, 0.010); // sub-block longer than the smoothing time
            expectEquals(base.value(2), 0.4f);
            base.setTarget(2, 0.9f);
            base.step();
            expectEquals(base.value(2), 0.9f);
        }

        beginTest("build_slot_params_sums_clamps_maps_and_respects_modulatable");
        {
            const auto& d = twoParamDescriptor();
            SlotBase base;
            base.prepare(kOversampledRate, kSubBlock);
            base.setTarget(0, 0.5f);
            base.setTarget(1, 0.5f);
            base.setLevelTarget(0.8f);
            base.setPanTarget(0.5f);
            base.setPitchTarget(12.0f);
            base.snapToTargets();

            SlotOffsets offsets;
            offsets.p[0] = 0.25f;
            offsets.p[1] = 0.25f; // ignored: not modulatable
            offsets.level = 0.5f; // clamps to 1
            offsets.pan = 0.75f;  // clamps to 1
            offsets.pitchSemitones = -5.0f;
            offsets.amount = 0.9f;  // base amount 0, clamps to 1 after the sum

            const auto params = buildSlotParams(d, base, offsets);
            expectWithinAbsoluteError(params.p[0], descriptorToUnits(d.params[0], 0.75f), 1.0e-2f);
            expectWithinAbsoluteError(params.p[1], 0.0f, 1.0e-6f);
            expectEquals(params.p[2], 0.0f);
            expectEquals(params.level, 1.0f);
            expectEquals(params.pan, 1.0f);
            expectEquals(params.pitchSemitones, 7.0f);
            expectEquals(params.combineAmount, 0.9f);

            offsets.clear();
            offsets.p[0] = -2.0f;
            const auto low = buildSlotParams(d, base, offsets);
            expectWithinAbsoluteError(low.p[0], 20.0f, 1.0e-3f);
            expectEquals(low.level, 0.8f);
            expectEquals(low.pan, 0.5f);
            expectEquals(low.pitchSemitones, 12.0f);
        }

        beginTest("mod_sources_get_set_and_none_stays_zero");
        {
            ModSources sources;
            sources.set(ModSource::lfo1, -0.5f);
            sources.set(ModSource::velocity, 0.9f);
            sources.set(ModSource::none, 1.0f);
            expectEquals(sources.get(ModSource::lfo1), -0.5f);
            expectEquals(sources.get(ModSource::velocity), 0.9f);
            expectEquals(sources.get(ModSource::none), 0.0f);
            expectEquals(ModSources::count, static_cast<int>(ModSource::count));
            sources.clear();
            expectEquals(sources.get(ModSource::lfo1), 0.0f);
        }
    }
};

static V3ModulationTests v3ModulationTests;
