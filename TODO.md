# TODO

## ARP Expansion Phase E — Advanced overlay UI

Phase D is now implemented and locally verified. The next implementation chunk is Phase E from `ARP_EXPANSION_PLAN.md`.

- Add `ArpAdvancedOverlay` and the `Advanced...` entry point in the shared editor without changing the one-page panel structure.
- Move the new Euclidean controls plus the existing chance, ratchet, and accent controls into the overlay, keeping APVTS wiring shared and state-preserving.
- Add the Euclidean visualizer and a compact status summary strip for non-default overlay state.
- Add editor-level regressions or verification notes for overlay show/hide behavior, attachment lifetime, and Straight/EUclidean control visibility rules.
