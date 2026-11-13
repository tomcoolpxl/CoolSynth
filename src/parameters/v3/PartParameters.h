#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "SlotParameters.h"
#include "synth/v3/Inserts.h"
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
        pA.mod1 .. mod24    matrix slots: src, dst, amount (-1..1), via, curve
        pA.genv.*           global envelope: delay .. release (ms), curves, loop, sync, trigger
        pA.step.*           s1 .. s16 (-1..1), slide1 .. slide16, division, length, glide, retrigger
        pA.macro1 .. 4      macros, 0..1
        pA.chaos.rate       the chaos source's rate (Hz)
        pA.voice.*          env2ToP1, velToP1, glide (ms), drift (cents), wheelToLfo1
        pA.play.*           mode, priority, voices, unison, unisonDetune, unisonSpread,
                            bendRange, glideLegato
        pA.chord.*          type, strum (ms, negative downwards), n1 .. n5 (the learned chord:
                            semitones above the key, 0 unused), learn
        pA.mix.*            level (dB), pan, transpose, fine (cents), keyLow, keyHigh, velLow,
                            velHigh, fx (the share through the effects), arp
        pA.i1 .. pA.i3      insert slots: type, p1 .. p8 (named and ranged by the type)

    Part B has the same parameters under "pB.". One parameter belongs to the patch as a whole:
        part.mode           A, Layer, Split, B
*/
namespace coolsynth::parameters::v3
{
    inline constexpr int partGeneratorSlots = 3;
    inline constexpr int partProcessorSlots = 3;
    inline constexpr int partInsertSlots = coolsynth::synth::v3::insertSlotCount;
    inline constexpr int partEnvelopes = 3;
    inline constexpr int partLocalLfos = 2;
    inline constexpr int partGlobalLfos = 2;

    [[nodiscard]] juce::String partPrefix(Part part);
    [[nodiscard]] juce::String envelopeParameterId(Part part, int oneBasedEnvelope, const char* field);
    [[nodiscard]] juce::String voiceParameterId(Part part, const char* field);
    [[nodiscard]] juce::String playParameterId(Part part, const char* field);
    /** type, strum, n1 .. n5, learn. */
    [[nodiscard]] juce::String chordParameterId(Part part, const juce::String& field);
    [[nodiscard]] const juce::StringArray& chordTypeNames();
    [[nodiscard]] juce::String mixParameterId(Part part, const juce::String& field);
    [[nodiscard]] const juce::StringArray& mixFields();
    inline constexpr char partModeParameterId[] = "part.mode";
    [[nodiscard]] const juce::StringArray& partModeNames();
    /** lfo is "lfo1", "lfo2", "glfo1" or "glfo2". */
    [[nodiscard]] juce::String lfoParameterId(Part part, const juce::String& lfo, const char* field);
    /** The fields of every LFO, in parameter order. */
    [[nodiscard]] const juce::StringArray& lfoFields();

    [[nodiscard]] juce::String matrixParameterId(Part part, int oneBasedSlot, const char* field);
    /** Choice names of the matrix: sources by ModSource, destinations by destination number, curves by ModCurve. */
    [[nodiscard]] const juce::StringArray& modSourceNames();
    [[nodiscard]] const juce::StringArray& modDestinationNames();
    [[nodiscard]] const juce::StringArray& modCurveNames();

    /** The global envelope's fields, the step modulator's fixed fields, in parameter order. */
    [[nodiscard]] const juce::StringArray& globalEnvelopeFields();
    [[nodiscard]] const juce::StringArray& stepFields();
    [[nodiscard]] juce::String globalEnvelopeParameterId(Part part, const char* field);
    [[nodiscard]] juce::String stepParameterId(Part part, const juce::String& field);
    [[nodiscard]] juce::String macroParameterId(Part part, int oneBasedMacro);
    [[nodiscard]] juce::String chaosParameterId(Part part);

    [[nodiscard]] std::unique_ptr<juce::AudioProcessorParameterGroup> makePartGroup(Part part);
    /** What belongs to the patch as a whole: the part mode. */
    [[nodiscard]] std::unique_ptr<juce::AudioProcessorParameterGroup> makePartsGroup();

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

    struct MatrixSlotPointers
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* destination = nullptr;
        std::atomic<float>* amount = nullptr;
        std::atomic<float>* via = nullptr;
        std::atomic<float>* curve = nullptr;
    };

    struct GlobalModulatorPointers
    {
        EnvelopePointers envelope {}; // velocity and keyTime unused
        std::atomic<float>* envelopeSync = nullptr;
        std::atomic<float>* envelopeTrigger = nullptr;
        std::array<std::atomic<float>*, coolsynth::synth::v3::stepCount> stepValues {};
        std::array<std::atomic<float>*, coolsynth::synth::v3::stepCount> stepSlides {};
        std::atomic<float>* stepDivision = nullptr;
        std::atomic<float>* stepLength = nullptr;
        std::atomic<float>* stepGlide = nullptr;
        std::atomic<float>* stepRetrigger = nullptr;
        std::array<std::atomic<float>*, coolsynth::synth::v3::macroCount> macros {};
        std::atomic<float>* chaosRate = nullptr;
    };

    struct MixPointers
    {
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* fine = nullptr;
        std::atomic<float>* keyLow = nullptr;
        std::atomic<float>* keyHigh = nullptr;
        std::atomic<float>* velLow = nullptr;
        std::atomic<float>* velHigh = nullptr;
        std::atomic<float>* fx = nullptr;
        std::atomic<float>* arp = nullptr;
    };

    struct PartParameterPointers
    {
        std::array<SlotParameterPointers, partGeneratorSlots> generators {};
        std::array<SlotParameterPointers, partProcessorSlots> processors {};
        std::array<SlotParameterPointers, partInsertSlots> inserts {};
        std::array<EnvelopePointers, partEnvelopes> envelopes {};
        std::array<LfoPointers, partLocalLfos> lfos {};
        std::array<LfoPointers, partGlobalLfos> globalLfos {};
        std::array<MatrixSlotPointers, coolsynth::synth::v3::matrixSlotCount> matrix {};
        GlobalModulatorPointers global {};
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
        std::atomic<float>* chordType = nullptr;
        std::atomic<float>* chordStrum = nullptr;
        std::array<std::atomic<float>*, coolsynth::synth::v3::learnedIntervalCount> chordNotes {};
        std::atomic<float>* chordLearn = nullptr;
        MixPointers mix {};
    };

    [[nodiscard]] PartParameterPointers bindPartPointers(juce::AudioProcessorValueTreeState& state, Part part);

    /** Fills everything except settings.arp, which lives outside the part. */
    void readPartSettings(const PartParameterPointers& pointers, coolsynth::synth::v3::PartSettings& settings) noexcept;
    void readPartMix(const PartParameterPointers& pointers, coolsynth::synth::v3::PartMix& mix) noexcept;
    void readPartInserts(const PartParameterPointers& pointers,
                         std::array<coolsynth::synth::v3::InsertSettings, partInsertSlots>& inserts) noexcept;
}
