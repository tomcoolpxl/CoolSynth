# TODO

## Phase 7: Fixed MiniLab core controller mapping

- [x] Add `MidiMappingEngine` for fixed MiniLab 3 controller routing.
- [x] Route verified MiniLab waveform, ADSR, and master-gain controls to APVTS parameters.
- [x] Route the panic action through an explicit command path.
- [x] Keep MiniLab-specific constants isolated from synth-engine and voice code.
- [x] Verify controller-driven parameter changes update the UI in standalone mode.
- [x] Verify no host-notifying parameter writes occur in the audio callback.

## Phase 8: Per-voice low-pass filter slice

- [x] Add a per-voice low-pass filter to each synth voice.
- [x] Wire cutoff and resonance to the shared parameter model.
- [x] Add cutoff and resonance controls to the editor.
- [x] Extend fixed MiniLab mapping so Knob 2 controls cutoff and Knob 3 controls resonance.
- [x] Verify filter stability at 44.1 kHz and 48 kHz.
- [x] Verify cutoff and resonance are audibly reflected in standalone playback.

## Phase 9: Global delay slice

- [x] Add the global delay effect after voice mixing.
- [x] Wire delay time, feedback, and mix to the shared parameter model.
- [x] Add delay controls to the editor.
- [x] Extend fixed MiniLab mapping for delay mix, feedback, and time.
- [x] Clamp feedback to the safe maximum.
- [x] Verify manual delay-time changes remain stable and real-time safe.
