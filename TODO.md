# TODO

`Phase 4` — Filter and dual-envelope tone path

- [ ] Implement dedicated filter ADSR and amp ADSR per voice.
- [ ] Implement the V2 resonant low-pass filter with cutoff, resonance, filter-envelope amount, and keyboard tracking.
- [ ] Wire envelope retrigger and voice-steal reset behavior into repeated-note and stolen-voice cases.
- [ ] Add tests for filter stability across supported sample rates and for envelope restart behavior.
- [ ] Manually verify init-patch bass, brass, pluck, and pad programming on the new dry voice path.
- [ ] Verify Debug build and `ctest` still pass after the filter/envelope cutover.
