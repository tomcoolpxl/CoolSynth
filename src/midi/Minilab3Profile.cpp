#include "Minilab3Profile.h"

#include <array>

namespace coolsynth::midi
{
    namespace
    {
        // Verified MiniLab 3 Arturia-mode surface used by the shipped Phase 9 profile.
        constexpr std::array<Minilab3ControlDefinition, 14> verifiedControls {{
            { "keyboard", "Keyboard", Minilab3ControlCategory::keyboard, "note input", Minilab3Disposition::requiredForPhase7, 
              { VerifiedMidiMessageKind::note, 1, {0, 127}, {1, 127}, Minilab3ValueMode::velocity, true, false }, "" },
            { "knob1", "Knob 1", Minilab3ControlCategory::knob, "filter cutoff", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {74, 74}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Brightness" },
            { "knob2", "Knob 2", Minilab3ControlCategory::knob, "filter resonance", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {71, 71}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Timbre" },
            { "knob3", "Knob 3", Minilab3ControlCategory::knob, "filter envelope amount", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {76, 76}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Variation" },
            { "knob4", "Knob 4", Minilab3ControlCategory::knob, "oscillator B fine tune", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {77, 77}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Movement" },
            { "knob5", "Knob 5", Minilab3ControlCategory::knob, "amp attack", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {93, 93}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "FX A" },
            { "knob6", "Knob 6", Minilab3ControlCategory::knob, "amp decay", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {18, 18}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "FX B" },
            { "knob7", "Knob 7", Minilab3ControlCategory::knob, "amp sustain", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {19, 19}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Delay" },
            { "knob8", "Knob 8", Minilab3ControlCategory::knob, "amp release", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {16, 16}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Reverb" },
            { "fader1", "Fader 1", Minilab3ControlCategory::fader, "oscillator A level", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {82, 82}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Bass EQ" },
            { "fader2", "Fader 2", Minilab3ControlCategory::fader, "oscillator B level", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {83, 83}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Mid EQ" },
            { "fader3", "Fader 3", Minilab3ControlCategory::fader, "master gain", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {85, 85}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "High EQ" },
            { "encoder", "Main Encoder", Minilab3ControlCategory::encoder, "oscillator A wave", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::controlChange, 1, {114, 114}, {0, 127}, Minilab3ValueMode::absolute, false, false }, "Arturia mode" },
            { "pad8", "Pad 8", Minilab3ControlCategory::pad, "panic", Minilab3Disposition::requiredForPhase7,
              { VerifiedMidiMessageKind::note, 10, {43, 43}, {0, 127}, Minilab3ValueMode::velocity, true, false }, "G1, Bank A" }
        }};
    }

    std::span<const Minilab3ControlDefinition> getVerifiedMinilab3Profile() noexcept
    {
        return verifiedControls;
    }

    const Minilab3ControlDefinition* findVerifiedMinilab3Control(std::string_view controlId) noexcept
    {
        for (const auto& control : verifiedControls)
            if (control.controlId == controlId)
                return &control;

        return nullptr;
    }

    bool validateMinilab3Profile() noexcept
    {
        return !verifiedControls.empty();
    }
}
