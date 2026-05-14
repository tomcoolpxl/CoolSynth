# CoolSynth Project Rules

This is the main repo or agent memory file. If you need to adapt your memory file, adapt this file.

We are in "V2" phase right now.

Authoritative project files:

- `REQUIREMENTS.md`, `REQUIREMENTS_V2.md`
- `DESIGN.md`, `DESIGN_V2.md`
- `IMPLEMENTATION_PLAN.md`, `IMPLEMENTATION_PLAN_V2.md`
- `TODO.md`
- `DONE.md`

Project rules:

- Keep one `TODO.md` item small enough for one review cycle.
- Refresh `TODO.md` from the current phase in `IMPLEMENTATION_PLAN.md`.
- Update `TODO.md` before starting a new implementation chunk.
- Update `TODO.md` and `DONE.md` after implementation.
- Move an item to `DONE.md` only after the required checks, review, and doc updates are complete.
- `DONE.md` holds only verified work.
- Update `REQUIREMENTS.md` and `DESIGN.md` when scope or acceptance criteria change.
- Update `IMPLEMENTATION_PLAN.md` when the order or grouping of work changes.
- For narrow tasks, pass the exact authoritative files in the prompt instead of retyping context.
- Ask before making a large refactor, changing the directory structure, or removing tests.
- Before moving work to `DONE.md`, review the diff, run the required checks, and update docs if the change affected scope or structure.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

## Current Project State

- `Phase 15` is complete: the repository now has a manual-only Windows validation workflow and a tag-only Windows release workflow backed by `scripts/ci/*.ps1` and isolated CI CMake presets.
- Live proof completed on GitHub on 2026-05-09:
  - Manual validation run `25614177697` passed and produced diagnostics plus packaged artifacts.
  - Release run `25614334076` passed, published a disposable prerelease, and a rerun updated that same release rather than creating a duplicate.
- The workflow action pins now target the Node 24-compatible lines:
  - `actions/checkout` pinned to `v6.0.2`
  - `actions/upload-artifact` pinned to `v7.0.0`
  - `actions/download-artifact` pinned to `v8.0.1`
  - `softprops/action-gh-release` pinned to `v3`
- Node-runtime validation completed on 2026-05-10:
  - Manual validation run `25614878512` passed with the updated action pins and no Node 20 deprecation annotation.
  - Release run `25625463636` passed with the updated release workflow pins and no Node 20 deprecation annotation.
- Late V2 doc decisions fixed on 2026-05-14:
  - V2 ships with 8 voices by default.
  - A selectable 5-voice vintage-limited mode is not required for the first V2 release.
  - V2 keeps the `.cspatch` extension while intentionally breaking V1 patch/state compatibility.
  - First-release VST3 manual sign-off uses both Ableton Live Lite and REAPER on Windows.
  - The UI target is a one-page instrument panel first, with paging only as a fallback.
  - Aftertouch is a late V2 addition only if it remains straightforward after the main MIDI/controller path is stable.
- V2 `Phase 1` completed on 2026-05-14:
  - The APVTS surface now exposes the full grouped V2 parameter contract across oscillators, mixer, filter, dual envelopes, LFO, Poly Mod, performance, arp, drive, chorus, delay, reverb, and output.
  - `SynthAudioProcessor` now decodes a full V2 block snapshot and renders through a new `SynthEngineV2` seam that still delegates audible output to the current legacy engine path.
  - The current shared editor subset and bundled MiniLab 3 profile were retargeted from the old global `waveform` parameter to the new V2 `oscAWave` parameter so the shell remains usable during staged migration.
  - Added test coverage for V2 parameter-ID uniqueness and V2 parameter-state round-trip/sanitized restore behavior.
  - Local verification passed with `cmake --preset vs2022-debug`, `cmake --build --preset build-debug --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- The next active V2 implementation chunk is `Phase 2`: custom allocator with basic V2 note playback. `TODO.md` has been refreshed to that phase only.
