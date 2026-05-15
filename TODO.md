# TODO

`Phase 10`

- [ ] Define the V2 processor-state version and patch-format boundary for the new parameter contract. NOTE: BACKWARDS COMPATIBILITY IS NOT A REQUIREMENT!!! NOT SURE HOW NECESSARY NEXDT STEPS ARE
- [ ] Update patch save and load logic to round-trip only V2 parameter state.
- [ ] Reject incompatible V1 patch or processor-state payloads explicitly unless an import path is later approved.
- [ ] Add tests for V2 patch round-trip, malformed payload rejection, and V1 compatibility-boundary behavior.
- [ ] Update `README.md` and patch workflow documentation to describe the V2 compatibility break.
- [ ] Verify saved V2 patches reload identically in standalone and plugin workflows.
