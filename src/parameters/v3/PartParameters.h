#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "SlotParameters.h"
#include "synth/v3/Part.h"

/*
    V3 part parameters (DESIGN_V3.md section 9.1): every parameter of one part, built from slot
    paths, and the lock-free reader that turns them into PartSettings once per block.

        pA.g1 .. pA.g3      generator slots (G1 defaults to VA)
        pA.p1 .. pA.p3      processor slots (P1 defaults to the ladder)
        pA.env1 .. env3     delay, attack, hold, decay, sustain, release (ms), curves, velocity,
                            keyTime, loop, oneShot; env1 drives the amplifier
        pA.lfo1, pA.lfo2    local LFOs: shape, rate (Hz), sync, division, phase (degrees),
                            retrigger, delay and fade (ms), oneShot
        pA.glfo1, pA.glfo2  global LFOs, the same fields; synced and free-running by default
        pA.voice.*          env2ToP1, velToP1, glide (ms), drift (cents), wheelToLfo1
        pA.play.*           mode, priority, voices, unison, unisonDetune, unisonSpread,
                            bendRange, glideLegato
*/
namespace coolsynth::parameters::v3
{
    inline constexpr int partGeneratorSlots = 3;
    inline constexpr int partProcessorSlots = 3;
    inline constexpr int partEnvelopes = 3;
    inline constexpr int partLocalLfos = 2;
    inline constexpr int partGlobalLfos = 2;

    [[nodiscard]] juce::String partPrefix(Part part);
    [[nodiscard]] juce::String envelopeParameterId(Part part, int oneBasedEnvelope, const char* field);
    [[nodiscard]] juce::String voiceParameterId(Part part, const char* field);
    [[nodiscard]] juce::String playParameterId(Part part, const char* field);
    /** lfo is "lfo1", "lfo2", "glfo1" or "glfo2". */
    [[nodiscard]] juce::String lfoParameterId(Part part, const juce::String& lfo, const char* field);
    /** The fields of every LFO, in parameter order. */
    [[nodiscard]] const juce::StringArray& lfoFields();

    [[nodiscard]] std::unique_ptr<juce::AudioProcessorParameterGroup> makePartGroup(Part part);

    /** True for IDs inside a part ("pA." or "pB."), which MIDI learn accepts. */
    [[nodiscard]] bool isPartParameterId(juce::StringRef parameterId) noexcept;

    struct EnvelopePointers
    {
        std::atomic<float>* delay = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* hold = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* attackCurve = nullptr;
        std::atomic<float>* decayCurve = nullptr;
        std::atomic<float>* releaseCurve = nullptr;
        std::atomic<float>* velocity = nullptr;
        std::atomic<float>* keyTime = nullptr;
        std::atomic<float>* loop = nullptr;
        std::atomic<float>* oneShot = nullptr;
    };

    struct LfoPointers
    {
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* phase = nullptr;
        std::atomic<float>* retrigger = nullptr;
        std::atomic<float>* delay = nullptr;
        std::atomic<float>* fade = nullptr;
        std::atomic<float>* oneShot = nullptr;
    };

    struct PartParameterPointers
    {
        std::array<SlotParameterPointers, partGeneratorSlots> generators {};
        std::array<SlotParameterPointers, partProcessorSlots> processors {};
        std::array<EnvelopePointers, partEnvelopes> envelopes {};
        std::array<LfoPointers, partLocalLfos> lfos {};
        std::array<LfoPointers, partGlobalLfos> globalLfos {};
        std::atomic<float>* wheelToLfo1 = nullptr;
        std::atomic<float>* env2ToP1 = nullptr;
        std::atomic<float>* velToP1 = nullptr;
        std::atomic<float>* glide = nullptr;
        std::atomic<float>* drift = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* priority = nullptr;
        std::atomic<float>* voices = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* unisonDetune = nullptr;
        std::atomic<float>* unisonSpread = nullptr;
        std::atomic<float>* bendRange = nullptr;
        std::atomic<float>* glideLegato = nullptr;
    };

    [[nodiscard]] PartParameterPointers bindPartPointers(juce::AudioProcessorValueTreeState& state, Part part);

    /** Fills everything except settings.arp, which lives outside the part. */
    void readPartSettings(const PartParameterPointers& pointers, coolsynth::synth::v3::PartSettings& settings) noexcept;
}
