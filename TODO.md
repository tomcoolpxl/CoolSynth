# TODO

## ARP Expansion Phase F — Patch format + factory presets

Phase E is now implemented and locally verified. The next implementation chunk is Phase F from `ARP_EXPANSION_PLAN.md`.

- Bump the wrapped `.cspatch` and wrapped processor-state format versions for the full expanded arp parameter surface.
- OVERRIDING COMMENT: DO NOT CARE ABOUT BACKWARDS COMPATIBIULITY OF PATCHES!!!!!!!!
- Re-emit the factory preset source data with explicit defaults for the new arp rhythm and modifier fields.
- Add the first curated arp-focused factory preset slice that exercises the new pattern, rhythm, and modifier combinations without broadening scope beyond the approved Phase F plan.
- Add preset round-trip coverage or equivalent verification notes for the new arp-focused preset payloads.
