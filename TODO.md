# TODO

`Phase 3` — Dual-oscillator mixer voice core

- [ ] Implement oscillator A controls and waveform behavior required by the V2 parameter surface.
- [ ] Implement oscillator B controls, low-frequency mode, and sync-support behavior required by the V2 parameter surface.
- [ ] Add per-voice noise generation and explicit oscillator/noise mixer levels.
- [ ] Add bounded pre-filter overload or gain staging that supports the target dry tone without destabilizing render.
- [ ] Reduce patch-dependent note-start click behavior on dense simultaneous note-ons without flattening the intended attack character.
- [ ] Add tests or render assertions for pulse-width limits, sync reset behavior, detune behavior, note-start transient behavior, and mixer stability.
- [ ] Verify dry dual-oscillator and noise-mixed tones in standalone playback.
