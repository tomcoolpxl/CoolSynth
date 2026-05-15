# TODO

`Phase 9`

- [ ] Update the bundled controller profile data and runtime mapping logic for the V2 parameter surface.
- [ ] Preserve notes, pitch bend, mod wheel, and sustain as reserved performance inputs outside generic MIDI learn.
- [ ] Extend standalone and plugin MIDI learn to the V2 controls.
- [ ] Persist and restore V2 learned mappings in standalone settings and plugin state.
- [ ] Add channel aftertouch only if it fits the stabilized MIDI/controller path without broader redesign.
- [ ] Add tests for learned-CC round-trip, controller-profile override precedence, and disconnect handling.
- [ ] Verify controller workflows manually in standalone and plugin mode.
