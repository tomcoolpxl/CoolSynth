#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <juce_core/juce_core.h>

#include "Module.h"

/*
    V3 two-operator FM generator (DESIGN_V3.md section 3.1; REQUIREMENTS_V3.md: carrier and
    modulator with ratio, fine, index, feedback; phase modulation with sine operators).

    A sine modulator at Ratio times the note (Fine detunes it in cents) phase-modulates a sine
    carrier at Carrier times the note. Index is the peak phase deviation in radians; route ENV3
    or any source to it through the matrix for the classic index envelope. Feedback feeds the
    modulator back into its own phase, through the average of its last two outputs as Yamaha's
    chips are reported to do, which keeps high feedback from buzzing at half the sample rate;
    at full feedback the modulator approaches a saw. Mix crossfades the output from the carrier
    to the modulator itself.

    Aliasing: Carson's rule leaves a Bessel tail that still folds back, so the index is held
    where the first sideband that would fold into the audible band after the voice's decimation
    stays under 1e-4 (indexLimit()); low and middle notes never reach the limit. Feedback is
    reduced when the modulator leaves too little room for its harmonics. Index, feedback and ratios ramp
    across each sub-block, so a fast index envelope does not step.

    Phases run in double precision and restart at a fresh note, so every attack is the same. The
    output runs generatorLatencySamples late like every generator and hands its mono signal and
    the carrier's wrap track onward, so a VA can sync to the carrier. Combine FM adds the previous
    generator's signal to the carrier's phase; ring and AM work as on the VA.
*/
namespace coolsynth::synth::v3
{
    class FmGenerator final
    {
    public:
        enum Param : int { ratio = 0, fine, index, feedback, carrier, mix, paramCount };

        static constexpr int latencySamples = generatorLatencySamples;
        static constexpr double maxIndex = 10.0;
        static constexpr double maxFeedbackRadians = 1.5;
        static constexpr double externalFmRadians = 4.0; // phase deviation of combine FM at full amount

        static const ModuleDescriptor& descriptor()
        {
            static const ModuleDescriptor d {
                "FM2", 1,
                {{ { "ratio",    "Ratio",    0.25f, 16.0f,  1.0f, Skew::logarithmic, Unit::generic, true },
                   { "fine",     "Fine",     -100.0f, 100.0f, 0.0f, Skew::linear,    Unit::cents,   true },
                   { "index",    "Index",    0.0f,  10.0f,  1.0f, Skew::squared,     Unit::generic, true },
                   { "feedback", "Feedback", 0.0f,  1.0f,   0.0f, Skew::linear,      Unit::percent, true },
                   { "carrier",  "Carrier",  0.25f, 8.0f,   1.0f, Skew::logarithmic, Unit::generic, true },
                   { "mix",      "Mix",      0.0f,  1.0f,   0.0f, Skew::linear,      Unit::percent, true } }},
                paramCount };
            return d;
        }

        static std::size_t memoryFloats(double, int maxSubBlock) { return 2 * static_cast<std::size_t>(std::max(1, maxSubBlock)); }

        void prepare(double newSampleRate, int maxSubBlock, SlotMemory& memory) noexcept
        {
            sampleRate = std::max(1000.0, newSampleRate);
            const auto n = static_cast<std::size_t>(std::max(1, maxSubBlock));
            signalTrack = memory.take(n);
            wrapTrack = memory.take(n);
            reset();
        }

        void reset() noexcept
        {
            carrierPhase = modulatorPhase = 0.0;
            feedback1 = feedback2 = 0.0;
            ringLeft.fill(0.0f);
            ringRight.fill(0.0f);
            ringMono.fill(0.0f);
            ringWrap.fill(-1.0f);
            cursor = 0;
            primed = false;
            std::fill(signalTrack.begin(), signalTrack.end(), 0.0f);
            std::fill(wrapTrack.begin(), wrapTrack.end(), -1.0f);
            input = {};
        }

        void noteOn(const NoteContext&) noexcept {}
        void setLink(const GeneratorLink& in) noexcept { input = in; }
        [[nodiscard]] GeneratorLink link() const noexcept { return { signalTrack.data(), wrapTrack.data() }; }

        void process(float* left, float* right, int numSamples, const SlotParams& params) noexcept
        {
            const double note = std::max(0.0, static_cast<double>(params.frequencyHz));
            const double carrierHz = std::min(0.45 * sampleRate, note * juce::jlimit(0.25, 8.0, static_cast<double>(params.p[carrier])));
            const double modulatorHz = std::min(0.45 * sampleRate, note * juce::jlimit(0.25, 16.0, static_cast<double>(params.p[ratio]))
                                                                       * std::exp2(juce::jlimit(-100.0, 100.0, static_cast<double>(params.p[fine])) / 1200.0));

            const double targetIndex = std::min(juce::jlimit(0.0, maxIndex, static_cast<double>(params.p[index])),
                                                indexLimit(carrierHz, modulatorHz, sampleRate));
            const double harmonicRoom = juce::jlimit(0.0, 1.0, (0.45 * sampleRate / std::max(1.0, modulatorHz) - 1.0) / 20.0);
            const double targetFeedback = maxFeedbackRadians * juce::jlimit(0.0, 1.0, static_cast<double>(params.p[feedback])) * harmonicRoom;
            const double targetCarrierStep = carrierHz / sampleRate;
            const double targetModulatorStep = modulatorHz / sampleRate;
            const float targetMix = juce::jlimit(0.0f, 1.0f, params.p[mix]);

            if (! primed)
            {
                currentIndex = targetIndex;
                currentFeedback = targetFeedback;
                currentCarrierStep = targetCarrierStep;
                currentModulatorStep = targetModulatorStep;
                currentMix = targetMix;
                primed = true;
            }

            const double steps = static_cast<double>(std::max(1, numSamples));
            const double indexStep = (targetIndex - currentIndex) / steps;
            const double feedbackStep = (targetFeedback - currentFeedback) / steps;
            const double carrierStepStep = (targetCarrierStep - currentCarrierStep) / steps;
            const double modulatorStepStep = (targetModulatorStep - currentModulatorStep) / steps;
            const float mixStep = (targetMix - currentMix) / static_cast<float>(steps);

            const float panAngle = (juce::jlimit(-1.0f, 1.0f, params.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            const float gainLeft = std::cos(panAngle), gainRight = std::sin(panAngle);

            const float amount = juce::jlimit(0.0f, 1.0f, params.combineAmount);
            const bool useFm = params.combine == Combine::fm && input.signal != nullptr && amount > 0.0f;
            const bool useRing = params.combine == Combine::ring && input.signal != nullptr;
            const bool useAm = params.combine == Combine::am && input.signal != nullptr;
            const double externalDepth = externalFmRadians * static_cast<double>(amount * amount);

            for (int s = 0; s < numSamples; ++s)
            {
                currentIndex += indexStep;
                currentFeedback += feedbackStep;
                currentCarrierStep += carrierStepStep;
                currentModulatorStep += modulatorStepStep;
                currentMix += mixStep;

                const double modulator = std::sin(kTwoPi * modulatorPhase + currentFeedback * 0.5 * (feedback1 + feedback2));
                feedback2 = feedback1;
                feedback1 = modulator;

                double phase = kTwoPi * carrierPhase + currentIndex * modulator;
                if (useFm)
                    phase += externalDepth * static_cast<double>(input.signal[s]);
                const double carrierOut = std::sin(phase);
                const float out = static_cast<float>((1.0 - static_cast<double>(currentMix)) * carrierOut + static_cast<double>(currentMix) * modulator);

                // The carrier's unmodulated phase marks the wraps a synced generator follows.
                float wrapTau = -1.0f;
                const double next = carrierPhase + currentCarrierStep;
                if (next >= 1.0 && currentCarrierStep > 0.0)
                    wrapTau = static_cast<float>(juce::jlimit(1.0e-6, 1.0, (1.0 - carrierPhase) / currentCarrierStep));
                carrierPhase = next - std::floor(next);
                modulatorPhase += currentModulatorStep;
                modulatorPhase -= std::floor(modulatorPhase);

                const unsigned int now = cursor & 3u;
                ringLeft[now] = out * gainLeft;
                ringRight[now] = out * gainRight;
                ringMono[now] = out;
                ringWrap[now] = wrapTau;

                const unsigned int delayed = (cursor + 2u) & 3u; // two samples back
                float outLeft = ringLeft[delayed], outRight = ringRight[delayed], outMono = ringMono[delayed];
                const float outWrap = ringWrap[delayed];
                ++cursor;

                if (useRing || useAm)
                {
                    const float modulatorIn = input.signal[s];
                    const float k = useRing ? 1.0f - amount + amount * modulatorIn : 1.0f - 0.5f * amount * (1.0f - modulatorIn);
                    outLeft *= k;
                    outRight *= k;
                    outMono *= k;
                }

                left[s] = outLeft;
                right[s] = outRight;
                signalTrack[static_cast<std::size_t>(s)] = outMono;
                wrapTrack[static_cast<std::size_t>(s)] = outWrap;
            }
        }

        [[nodiscard]] double effectiveIndexForTesting() const noexcept { return currentIndex; }

        /**
            The largest index whose aliases stay below kAliasFloor. Sidebands up to order n_s sit
            below 0.75 fs and fold, if at all, above 0.25 fs, into the band the voice's decimator
            removes; the first order beyond them, n = n_s + 1, must stay small. J_n(index) is close
            to (index / 2)^n / n! there, so the limit is 2 (n! kAliasFloor)^(1/n).
        */
        [[nodiscard]] static double indexLimit(double carrierHz, double modulatorHz, double rate) noexcept
        {
            const double safeOrder = std::floor((0.75 * rate - carrierHz) / std::max(1.0, modulatorHz));
            if (safeOrder < 0.0)
                return 0.0;
            const double n = safeOrder + 1.0;
            return std::min(maxIndex, 2.0 * std::exp((std::lgamma(n + 1.0) + std::log(kAliasFloor)) / n));
        }

    private:
        static constexpr double kTwoPi = 6.283185307179586476925;
        static constexpr double kAliasFloor = 1.0e-4; // largest Bessel amplitude allowed to fold into the audible band

        double sampleRate = 96000.0;
        double carrierPhase = 0.0, modulatorPhase = 0.0;
        double feedback1 = 0.0, feedback2 = 0.0;
        double currentIndex = 0.0, currentFeedback = 0.0, currentCarrierStep = 0.0, currentModulatorStep = 0.0;
        float currentMix = 0.0f;
        bool primed = false;
        std::array<float, 4> ringLeft {}, ringRight {}, ringMono {}, ringWrap {};
        unsigned int cursor = 0;
        std::span<float> signalTrack, wrapTrack;
        GeneratorLink input {};
    };

    static_assert(Module<FmGenerator>);
}
