# TODO

`Phase 2` — Custom allocator with basic V2 note playback

- [x] Add explicit sample-offset engine event records for note, bend, mod wheel, and sustain input.
- [x] Implement deterministic V2 note allocation with sustain, panic, and musically defensible voice stealing.
- [x] Route `SynthAudioProcessor::processBlock()` through the new V2 event path and render contract.
- [x] Replace callback-lock-dependent live mapped-action handling with a non-blocking control bridge compatible with the new engine path.
- [x] Add tests for in-block event ordering, sustain behavior, panic, and voice stealing.
- [ ] Verify standalone and plugin basic note playback on the new allocator path.
