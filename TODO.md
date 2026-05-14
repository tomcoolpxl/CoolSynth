# TODO

`Phase 7`

- [ ] Add held-note, latched-note, pattern, octave, and gate state for the V2 arpeggiator.
- [ ] Read plugin transport and tempo in `processBlock()` and pass a transport snapshot into the engine.
- [ ] Implement deterministic internal-rate fallback behavior for missing host timing.
- [ ] Route arp-generated note events through the allocator with sample offsets.
- [ ] Add tests for pattern ordering, latch behavior, gate timing, and tempo fallback.
- [ ] Verify standalone and Ableton Live Lite VST3 arp behavior manually before final multi-host release validation.
