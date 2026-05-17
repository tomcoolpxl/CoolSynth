# TODO

## ARP Expansion Phase C — Ratchet + Accent

Phase B is now implemented and locally verified. The next implementation chunk is Phase C from `ARP_EXPANSION_PLAN.md`.

- Add `arpRatchetCount`, `arpRatchetChance`, `arpAccentEvery`, and `arpAccentAmount` to the parameter layout and block decode path.
- Implement ratchet sub-step emission and accent velocity scaling in `Arpeggiator::generateEventsForBlock()`, including chord-mode combinations.
- Add deterministic regressions for ratchet offsets/counts and accent scaling behavior.
- Keep the UI temporary and minimal until the planned advanced overlay lands in Phase E.
