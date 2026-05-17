# TODO

## ARP Expansion Phase D — Euclidean rhythm

Phase C is now implemented and locally verified. The next implementation chunk is Phase D from `ARP_EXPANSION_PLAN.md`.

- Add `arpRhythm`, `arpEuclideanPulses`, `arpEuclideanSteps`, and `arpEuclideanRotation` to the parameter layout and block decode path.
- Implement cached Bjorklund Euclidean cycle generation and integrate it into the arp step loop so rests advance rhythm position without advancing the melodic walk.
- Add deterministic regressions for known Euclidean patterns, pulse-vs-slot melodic advancement, and host-sync bar-reset behavior.
- Keep the UI temporary and minimal until the planned advanced overlay lands in Phase E.
